// #include "captive_server.h"
// #include "portal_interface.h"

/**
 * @file wifi_controller.cpp
 * @brief WiFi/AP lifecycle and integration glue for the captive portal.
 *
 * Responsibilities:
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

namespace {
unsigned long nowLoop = 0;

ESP8266WebServer server(wifi_config::kWebPort);

const char* apIpString() {
    static char apIp[16];
    snprintf(apIp, sizeof(apIp), "%d.%d.%d.%d", WiFi.softAPIP()[0], WiFi.softAPIP()[1], WiFi.softAPIP()[2], WiFi.softAPIP()[3]);
    return apIp;
}

uint8_t apConnectedSize() {
    return WiFi.softAPgetStationNum();
}

void apTick() {
    // Handle client requests and lease timeouts.
    server.handleClient();
    updateLeases();
}

Thread apThread = Thread([]() {
    apTick();
});
}  // namespace

// -----------------------------------------------------------------------------
// Public API (declared in wifi_controller.h)
// -----------------------------------------------------------------------------

bool startWiFiService() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(wifi_config::kApSsid, wifi_config::kApPassword, wifi_config::kApChannel, !wifi_config::kBroadCastAp, wifi_config::kMaxClientLeases);
    WiFi.softAPConfig (wifi_config::kLocalIp, wifi_config::kGateway, wifi_config::kSubnet);

    unsigned long startTime = millis();
    while (!WiFi.status() && millis() - startTime < wifi_config::kMaxApStartTimeout) {
        debug_logs::wifiLogging("Waiting for AP to start...");
        delay(200);
    }

    startWebService(server);

    apThread.setInterval(wifi_config::kThreadRefreshIntervalMs);
    
    debug_logs::wifiLogging("Started AP with IP: %s", apIpString());
    return WiFi.status();
}

bool updateWiFiService() {
    if (apThread.shouldRun()) apThread.run();

    if (millis() - nowLoop >= debug_config::kWiFiLoopDelay) {
        debug_logs::wifiLogging("AP connected clients: %u", apConnectedSize());
        nowLoop = millis();
    }
    return true;
}
