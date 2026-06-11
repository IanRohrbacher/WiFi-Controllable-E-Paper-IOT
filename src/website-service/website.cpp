/**
 * @file web_server.cpp
 * @brief Web server to serve a web interface.
 *
 * This module registers the HTTP endpoints to be used by user to interface
 * with the e-paper screen.
 */


#include <LittleFS.h>
#include <ESP8266WebServer.h>

#include "configs.h"
#include "logger.h"

namespace {
void handleRoot(ESP8266WebServer& server) {
    File file = LittleFS.open(web_config::kHtmlIndexPath, "r");

    if (!file) {
        server.send(500, "text/plain", "index.html not found");
        return;
    }

    server.streamFile(file, "text/html");
    file.close();
}

void setupPortalEndpoints(ESP8266WebServer& server) {
    // TODO fix all devices to open captive portal on "/"
    // list of working devices: iPadOS 18
    // list of known non-working devices: windows 11, kali purple, android 16

    // Android/chromeOS
    server.on("/generate_204", [&server]() {
        server.sendHeader("Location", "/", true);
        debug_logs::webLogging("Redirecting /generate_204 to /");
        server.send(302, "text/plain", "");
    });

    // iOS/macOS
    server.on("/hotspot-detect.html", [&server]() {
        server.sendHeader("Location", "/", true);
        debug_logs::webLogging("Redirecting /hotspot-detect.html to /");
        server.send(302, "text/plain", "");
    });

    // Windows
    server.on("/connecttest.txt", [&server]() {
        server.sendHeader("Location", "/", true);
        debug_logs::webLogging("Redirecting /connecttest.txt to /");
        server.send(302, "text/plain", "");
    });
    server.on("/ncsi.txt", [&server]() {
        server.sendHeader("Location", "/", true);
        debug_logs::webLogging("Redirecting /ncsi.txt to /");
        server.send(302, "text/plain", "");
    });
}

void registerRoutes(ESP8266WebServer& server) {
    // Main page
    server.on("/", HTTP_GET, [&server]() {
        handleRoot(server);
    });

    // Static assets
    server.serveStatic(web_config::kHtmlDir, LittleFS, web_config::kHtmlDir);
    server.serveStatic(web_config::kJsDir, LittleFS, web_config::kJsDir);
    server.serveStatic(web_config::kStylesDir, LittleFS, web_config::kStylesDir);

    // Example API endpoint
    // server.on("/api/status", HTTP_GET, [&server]() {
    //     server.send(
    //         200,
    //         "application/json",
    //         R"({"status":"ok"})"
    //     );
    // });

    // Captive portal fallback
    server.onNotFound([&server]() { 
        // Serve index.html for all unknown routes
        // Useful for SPAs and captive portals
        File file = LittleFS.open(web_config::kHtmlIndexPath, "r");

        debug_logs::webLogging("Serving index.html for unknown route");
        if (file) {
            server.streamFile(file, "text/html");
            file.close();
        } else {
            server.send( 404, "application/json", R"({"error":"not_found"})"
            );
        }
    });
}

}  // namespace

// -----------------------------------------------------------------------------
// Public API (declared in web_server.h)
// -----------------------------------------------------------------------------

bool startWebService(ESP8266WebServer& server) {
    uint8_t attempts = 0;
    while (!LittleFS.begin() && attempts < web_config::kLittleFSRemountAttempts) {
        debug_logs::webLogging("Failed to mount LittleFS, attempt %d", attempts + 1);
        delay(web_config::kLittleFSRemountIntervalMs);
        attempts++;
    }
    if (attempts == web_config::kLittleFSRemountAttempts) {
        debug_logs::webLogging("Failed to mount LittleFS after %d attempts, aborting web server start", web_config::kLittleFSRemountAttempts);
        return false;
    }
    registerRoutes(server);
    if (web_config::kEnablePortal) setupPortalEndpoints(server);

    server.begin();
    return true;
}
