/**
 * @file main.cpp
 * @brief Application entry point wiring to the portal controller.
 *
 * The main Arduino `setup()` starts the DNS service first (with a
 * placeholder IP), then starts the WiFi/AP via the controller. After the
 * AP is started the DNS service is restarted with the actual AP IP so
 * captive DNS responses resolve correctly. The `loop()` processes DNS
 * and the WiFi controller to serve clients.
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>

#include "configs.h"
#include "led-service/led_handler.h"
#include "wifi-service/wifi_controller.h"
#include "dns-service/captive_dns.h"

void setup() {
  if(debug_config::kEnableVerboseLogging) {
    Serial.begin(115200);
    while (!Serial) {}
  }

  // Create and run a thread for the onboard LED blinking so it
  // can run independently of the main loop and indicate status
  // without blocking other operations.
  startStatusLED();
  setStatusState(BlinkState::Setup);

  // Start the WiFi service (AP mode) and captive DNS.
  if(!startWiFiService()) {
    setStatusState(BlinkState::WiFiFail);
    while(true) {
      updateStatusLED();
      delay(main_config::kRefreshIntervalMs);
    }
  }
  
  // Start DNS with the AP IP so redirects resolve to the device.
  startDNSService(WiFi.softAPIP(), dns_config::kDnsPort);

  setStatusState(BlinkState::Idle);
}

void loop() {
  // Ensure DNS requests are processed, then let the WiFi controller
  // handle HTTP requests and lease processing.
  updateStatusLED();
  updateDNSService();
  updateWiFiService();
  delay(main_config::kRefreshIntervalMs);
}