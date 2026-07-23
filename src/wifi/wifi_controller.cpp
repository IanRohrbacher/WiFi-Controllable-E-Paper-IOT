/**
 * @file wifi_controller.cpp
 * 
 * @brief Controller for managing the WiFi access point and client leases.
 *
 * @details
 * Brings up the ESP8266 as a SoftAP via @c WiFi.softAP(), starts the web
 * server to serve the captive portal, and ticks both HTTP handling and lease
 * bookkeeping from a single cooperative thread.
 *
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <thread.h>

#include "configs.h"
#include "logger.h"
#include "wifi_lease.h"
#include "wifi_controller.h"
#include "website/website.h"

/**
 * @defgroup Private
 * Member variables/functions used internally by the WiFi controller.
 * These are not intended to be used outside of this module.
 * @{
 */
namespace {
/** @brief Timestamp for adding debug logs for the WiFi loop */
unsigned long nowLoop = 0;

/** @brief Web server instance for handling HTTP requests */
ESP8266WebServer server(wifi_config::kWebPort);

/**
 * @brief Number of clients currently connected to the access point.
 *
 * @par Parameters
 * None.
 *
 * @return Current station count, as reported by @c WiFi.softAPgetStationNum().
 *
 */
uint8_t apConnectedSize() {
    return WiFi.softAPgetStationNum();
}

/**
 * @brief One cooperative-thread tick, servicing HTTP requests then leases.
 *
 * @details
 * Calls @c server.handleClient() then @c updateLeases().
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void apTick() {
    server.handleClient();
    updateLeases();
}

/** @brief Thread for handling the access point tick */
Thread apThread = Thread([]() {
    apTick();
});
} // namespace
/** @} */ // end of Private

/**
 * @defgroup Public
 * Public API for the WiFi controller, declared in wifi_controller.h.
 * @{
 */
bool startWiFiModule() {
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(wifi_config::kApSsid, wifi_config::kApPassword, wifi_config::kApChannel, !wifi_config::kBroadCastAp, wifi_config::kMaxClientLeases)) {
        debug_logs::wifiLogging("WiFi.softAP() call failed.");
        return false;
    }
    WiFi.softAPConfig(wifi_config::kLocalIp, wifi_config::kGateway, wifi_config::kSubnet);

    unsigned long startTime = millis();
    while (WiFi.softAPIP() == IPAddress(0, 0, 0, 0) && millis() - startTime < wifi_config::kMaxApStartTimeout) {
        debug_logs::wifiLogging("Waiting for AP to start...");
        delay(200);
    }
    if (WiFi.softAPIP() == IPAddress(0, 0, 0, 0)) {
        debug_logs::wifiLogging("Timed out waiting for the AP to come up.");
        return false;
    }

    if (!startWebModule(server)) {
        debug_logs::wifiLogging("Failed to start web module.");
        return false;
    }

    apThread.setInterval(wifi_config::kThreadRefreshIntervalMs);

    debug_logs::wifiLogging("Started AP with IP: %s", WiFi.softAPIP().toString().c_str());
    return true;
}

void updateWiFiModule() {
    if (apThread.shouldRun()) apThread.run();

    if (millis() - nowLoop >= debug_config::kWiFiLoopDelay) {
        debug_logs::wifiLogging("AP connected clients: %u", apConnectedSize());
        nowLoop = millis();
    }
}
/** @} */ // end of Public
