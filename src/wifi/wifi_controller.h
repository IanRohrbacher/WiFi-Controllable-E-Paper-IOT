/**
 * @headerfile wifi_controller.h "src/wifi/wifi_controller.h"
 *
 */

#pragma once

/**
 * @brief Start the WiFi module in AP mode and initialize the web server.
 *
 * @details
 * Configures the ESP8266 as an access point using @c wifi_config, waits up to
 * @c wifi_config::kMaxApStartTimeout for the AP interface to come up, then
 * starts the web server. Fails immediately, without waiting out the timeout,
 * if @c WiFi.softAP() itself fails.
 *
 * @par Parameters
 * None.
 *
 * @return The status of the WiFi module startup attempt.
 * @retval true The AP is active and the web server was started.
 * @retval false @c WiFi.softAP() failed, the AP did not come up within the timeout, or the web server failed to start.
 *
 */
bool startWiFiModule();

/**
 * @brief Tick the AP thread and periodically log the connected client count.
 *
 * @details
 * Call regularly from the main loop. Runs the cooperative AP thread once its
 * interval has elapsed, which services HTTP requests and lease bookkeeping.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void updateWiFiModule();
