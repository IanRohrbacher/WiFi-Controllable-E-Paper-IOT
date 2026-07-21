/**
 * @file main.cpp
 * @brief Application entry point wiring to the portal controller.
 *
 * The main Arduino `setup()` starts the DNS module first (with a
 * placeholder IP), then starts the WiFi/AP via the controller. After the
 * AP is started the DNS module is restarted with the actual AP IP so
 * captive DNS responses resolve correctly. The `loop()` processes DNS
 * and the WiFi controller to serve clients.
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>

#include "configs.h"
#include "logger.h"
#include "led/led_handler.h"
#include "wifi/wifi_controller.h"
#include "dns/captive_dns.h"
#include "display/display.h"

namespace {
/** @brief Timestamp for adding debug logs for the main loop */
unsigned long nowLoop = 0;
}

void setup() {
  // Initialize serial for debug logging if enabled.
  if (debug_config::kEnableVerboseLogging) {
    Serial.begin(115200);
    while (!Serial) {}
  }

  // Create and run a thread for the onboard LED blinking so it
  // can run independently of the main loop and indicate status
  // without blocking other operations.
  startStatusLED();
  setStatusState(BlinkState::Setup);

  // Start the WiFi module (AP mode) and captive DNS.
  if (!startWiFiModule()) {
    setStatusState(BlinkState::WiFiFail);
  }

  // Start DNS with the AP IP so redirects resolve to the device.
  if (!startDNSModule(WiFi.softAPIP(), dns_config::kDnsPort)) {
    setStatusState(BlinkState::DNSFail);
  }

  // Start the e-paper display.
  if (!startDisplayModule(display_config::kClearOnStart)) {
    // setStatusState(BlinkState::EInkFail);
  }
  
  while(inFailedState()) {
    updateStatusLED();
    delay(main_config::kRefreshIntervalMs);
  }

  setStatusState(BlinkState::Idle);
}

void loop() {
  // Ensure DNS requests are processed, then let the WiFi controller
  // handle HTTP requests and lease processing.
  updateStatusLED();
  updateDNSModule();
  updateWiFiModule();

  if (millis() - nowLoop >= debug_config::kLoopLogDelay) {
    debug_logs::flushLogs();
    nowLoop = millis();
  }
  delay(main_config::kRefreshIntervalMs);
}
