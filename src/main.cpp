/**
 * @file main.cpp
 *
 * @brief Application entry point wiring together every firmware module.
 *
 * @details
 * @c setup() brings up the status LED, then the WiFi access point, then
 * captive DNS and mDNS using the AP's own IP, then the e-paper display, then
 * the IO scheduler. Once the IO scheduler has configured the clear button's
 * pin, holding that button through boot wipes the queued-frame flash storage
 * (see @c clearFrameQueue()) before falling through to the normal idle state.
 * Any failure along the way is reflected on the status LED, and @c setup()
 * blocks on that failed state before falling through to the idle pattern. @c
 * loop() then ticks the status LED, DNS, mDNS, WiFi, display, and IO modules
 * once per iteration and periodically flushes queued debug logs via @c
 * debug_logs::flushLogs().
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
#include "io/io_scheduler.h"

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
    setStatusState(BlinkState::EPaperFail);
  }

  // Start the IO scheduler (buttons and other generic IOs).
  startIoModule();

  // Holding the clear button through boot wipes the queued-frame flash storage
  if (clearButton.isHeld()) {
    debug_logs::ioLogging("Clear button held at boot, wiping the frame queue");
    clearFrameQueue();
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
  updateDisplayModule();
  updateIoModule();

  if (millis() - nowLoop >= debug_config::kLoopLogDelay) {
    debug_logs::flushLogs();
    nowLoop = millis();
  }
  delay(main_config::kRefreshIntervalMs);
}
