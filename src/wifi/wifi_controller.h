/**
 * @headerfile wifi_controller.h "src/wifi/wifi_controller.h"
 *
 */

#pragma once

/**
 * @brief Start the WiFi module in AP mode and initialize the web server.
 *
 * @details
 * This function configures the ESP8266 to operate in Access Point (AP) mode
 * using the settings defined in `wifi_config`. It then attempts to start the
 * web server to serve the captive portal interface. The function includes a
 * timeout mechanism to wait for the AP to start successfully.
 *
 * @par Parameters
 * None.
 *
 * @return The status of the WiFi module startup attempt.
 * @retval true The WiFi module was started successfully and the AP is active.
 * @retval false The WiFi module failed to start, possibly due to a configuration issue or failure to start the web server.
 *
 */
bool startWiFiModule();

/**
 * @brief Update the WiFi module by handling client requests and managing leases.
 *
 * @details
 * This function should be called regularly (e.g., from the main loop) to allow
 * the WiFi module to process incoming client requests, manage connected clients,
 * and handle lease timeouts. It checks if the AP thread is scheduled to run and
 * executes it if necessary.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void updateWiFiModule();
