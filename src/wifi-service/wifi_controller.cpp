/**
 * @file wifi_controller.cpp
 * 
 * @brief Controller for managing WiFi access point and client leases.
 * 
 * @details
 * This module is responsible for setting up the ESP8266 as a WiFi access
 * point, managing client connections, and serving the captive portal web
 * interface. It uses the ESP8266WiFi library to configure the WiFi settings
 * and handle client connections. The module also integrates with the web
 * server to serve the captive portal interface and with the DNS service to
 * handle captive portal redirection. Client leases are managed to keep track
 * of connected clients and their activity, allowing for proper handling of
 * client timeouts and disconnections.
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
#include "dns-service/captive_dns.h"
#include "website-service/website.h"

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
 * @brief Get the IP address of the access point as a string.
 * 
 * @details
 * This function retrieves the IP address of the access point and formats it as
 * a string in the standard dotted-decimal notation (e.g., "192.168.4.1").
 * 
 * @par Parameters
 * None.
 * 
 * @return The IP address of the access point in string format (e.g., "192.168.4.1").
 * 
 */
const char* apIpString() {
    static char apIp[16];
    snprintf(apIp, sizeof(apIp), "%d.%d.%d.%d", WiFi.softAPIP()[0], WiFi.softAPIP()[1], WiFi.softAPIP()[2], WiFi.softAPIP()[3]);
    return apIp;
}

/**
 * @brief Get the number of clients currently connected to the access point.
 * 
 * @details
 * This function queries the ESP8266 WiFi library to determine how many clients
 * are currently connected to the access point. This information can be useful
 * for monitoring the status of the AP and managing client leases.
 * 
 * @par Parameters
 * None.
 * 
 * @return The number of clients currently connected to the access point.
 * 
 */
uint8_t apConnectedSize() {
    return WiFi.softAPgetStationNum();
}

/**
 * @brief Handle the access point tick, processing client requests and managing leases.
 * 
 * @details
 * This function is called regularly (e.g., from a thread) to allow the WiFi
 * service to process incoming client requests through the web server and to
 * manage client leases by checking for timeouts and updating lease
 * information. It ensures that the AP remains responsive to clients and that
 * leases are properly maintained.
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
}  // namespace
/** @} */ // end of Private

/**
 * @defgroup Public
 * Public API for the WiFi controller, declared in wifi_controller.h.
 * @{
 */
bool startWiFiService() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(wifi_config::kApSsid, wifi_config::kApPassword, wifi_config::kApChannel, !wifi_config::kBroadCastAp, wifi_config::kMaxClientLeases);
    WiFi.softAPConfig (wifi_config::kLocalIp, wifi_config::kGateway, wifi_config::kSubnet);

    unsigned long startTime = millis();
    while (!WiFi.status() && millis() - startTime < wifi_config::kMaxApStartTimeout) {
        debug_logs::wifiLogging("Waiting for AP to start...");
        delay(200);
    }

    if (!startWebService(server)) {
        debug_logs::wifiLogging("Failed to start web service.");
        return false;
    }

    apThread.setInterval(wifi_config::kThreadRefreshIntervalMs);
    
    debug_logs::wifiLogging("Started AP with IP: %s", apIpString());
    return WiFi.status();
}

void updateWiFiService() {
    if (apThread.shouldRun()) apThread.run();

    if (millis() - nowLoop >= debug_config::kWiFiLoopDelay) {
        debug_logs::wifiLogging("AP connected clients: %u", apConnectedSize());
        nowLoop = millis();
    }
}
/** @} */ // end of Public
