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

  setStatusState(BlinkState::Idle);
}

void loop() {
  if(debug_config::kEnableStatusLight) updateStatusLED();

}