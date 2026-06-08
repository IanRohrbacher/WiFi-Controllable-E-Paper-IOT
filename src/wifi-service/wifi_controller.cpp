#include "wifi_controller.h"

#include "configs.h"
#include "dns-service/captive_dns.h"
#include "wifi_lease.h"
#include "thread.h"
// #include "captive_server.h"
// #include "portal_interface.h"

/**
 * @file wifi_controller.cpp
 * @brief WiFi/AP lifecycle and integration glue for the captive portal.
 *
 * Responsibilities:
 */

#include <Arduino.h>
#include <cstring>
#include <ESP8266WiFi.h>

namespace {
ESP8266WebServer server(wifi_config::kWebPort);

String apIpString() {
    return WiFi.softAPIP().toString();
}

uint8_t apConnectedSize() {
    return WiFi.softAPgetStationNum();
}

void apTick() {
    // Handle client requests and lease timeouts.
    server.handleClient();
    updateLeases();

    debug_logs::wifiLogging("AP IP: %s, Clients: %d, Leases: %d", apIpString().c_str(), apConnectedSize(), getLeaseCount());
}

Thread apThread = Thread([]() {
    apTick();
});
}  // namespace

// -----------------------------------------------------------------------------
// Public API (declared in h/wifi/wifi_controller.h)
// -----------------------------------------------------------------------------

bool startWiFiService() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(wifi_config::kApSsid, wifi_config::kApPassword, wifi_config::kApChannel, wifi_config::kBroadCastAp, wifi_config::kMaxClientLeases);

    unsigned long startTime = millis();
    while (!WiFi.status() && millis() - startTime < wifi_config::kMaxApStartTimeout) {
        delay(200);
    }

    apThread.setInterval(wifi_config::kThreadRefreshIntervalMs);
    
    return WiFi.status();
}

bool updateWiFiService() {
    if (apThread.shouldRun()) {
        apThread.run();
    }
    return true;
}
