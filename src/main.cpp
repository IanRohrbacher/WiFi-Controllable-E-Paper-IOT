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

#include "dns-service/captive_dns.h"
#include "configs.h"
#include "debug-service/led_handler.h"

void setup() {
  if(debug_config::kEnableSerial) {
    Serial.begin(115200);
    while (!Serial) {}
  }

  // Create and run a thread for the onboard LED blinking so it
  // can run independently of the main loop and indicate status
  // without blocking other operations.
  if(debug_config::kEnableStatusLight) {
    startStatusLED();
    setStatusState(BlinkState::Setup);
  }

  // Start DNS service early with a placeholder IP. This makes the DNS
  // responder ready to accept queries immediately; we'll restart it
  // with the real AP IP after the softAP is up.
  startDNSService(IPAddress(0, 0, 0, 0), portal_config::kDnsPort);

  setStatusState(BlinkState::Idle);
}

void loop() {
  // Ensure DNS requests are processed, then let the WiFi controller
  // handle HTTP requests and lease processing.
  if(debug_config::kEnableStatusLight) updateStatusLED();
  updateDNSService();

}