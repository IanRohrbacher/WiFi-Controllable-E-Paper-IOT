/**
 * @file main.cpp
 *
 * @brief Application entry point wiring together every firmware module.
 *
 * @details
 * @c setup() brings up the status LED, then the WiFi access point, then
 * captive DNS and mDNS using the AP's own IP, then the e-paper display. Any
 * failure along the way is reflected on the status LED, and @c setup() blocks
 * on that failed state before falling through to the idle pattern. @c loop()
 * then ticks the status LED, DNS, mDNS, and WiFi modules once per iteration
 * and periodically flushes queued debug logs via @c debug_logs::flushLogs().
 *
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

   // Create and run a thread for the onboard LED blinking.
   // This allows it to run independently of the main loop and indicate status without blocking other operations.
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

  // Start mDNS so the device is also reachable at http://portal.local/,
  // which keeps working even on clients that bypass the AP's DNS server.
  startMDNSModule();

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
  updateMDNSModule();
  updateWiFiModule();

  if (millis() - nowLoop >= debug_config::kLoopLogDelay) {
    debug_logs::flushLogs();
    nowLoop = millis();
  }
  delay(main_config::kRefreshIntervalMs);
}
