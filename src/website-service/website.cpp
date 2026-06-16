/**
 * @file website.cpp
 * 
 * @brief Implementation of the web server for serving the captive portal and API endpoints.
 * 
 * @details
 * This module sets up an ESP8266WebServer instance to serve the captive
 * portal's web interface and API endpoints. It mounts the LittleFS filesystem
 * to serve static assets and defines the necessary routes for the web server.
 * The main page is served at the root path ("/"), and static assets are
 * served from their respective directories. Additionally, it includes logic to
 * handle captive portal redirection for various platforms by redirecting known
 * captive portal detection paths to the root path. The web server is designed
 * to work in conjunction with the WiFi and DNS services to provide a seamless
 * captive portal experience for clients connecting to the access point.
 * 
 */


#include <LittleFS.h>
#include <ESP8266WebServer.h>

#include "configs.h"
#include "logger.h"

/**
 * @defgroup Private
 * Member variables/functions used internally by the web server service.
 * These are not intended to be used outside of this module.
 * @{
 */
namespace {
/**
 * @brief Handle requests to the root path ("/") by serving the index.htmlfile from LittleFS.
 * 
 * @details
 * This function attempts to open the index.html file from the LittleFS
 * filesystem and stream it to the client. If the file cannot be opened, it
 * sends a 500 Internal Server Error response.
 * 
 * @param server Reference to the ESP8266WebServer instance handling the request.
 * 
 * @return The status of the request handling attempt.
 * @retval true The index.html file was successfully served to the client.
 * @retval false An error occurred while trying to serve the index.html file, and an error response was sent to the client.
 * 
 */
bool handleRoot(ESP8266WebServer& server) {
    File file = LittleFS.open(web_config::kHtmlIndexPath, "r");

    if (!file) {
        server.send(500, "text/plain", "index.html not found");
        return false;
    }

    server.streamFile(file, "text/html");
    file.close();
    return true;
}

/**
 * @brief Set up routes for captive portal redirection for various platforms.
 * 
 * @details
 * This function defines routes for known captive portal detection paths used
 * by different platforms (e.g., Android, iOS, Windows) and redirects them to
 * the root path ("/"). This ensures that clients attempting to detect captive
 * portals are properly redirected to the captive portal's main page.
 * 
 * @param server Reference to the ESP8266WebServer instance to set up the routes on.
 * 
 * @par Returns
 * Nothing.
 * 
 * @par Endpoints
 * - Android/chromeOS: "/generate_204"
 * - iOS/macOS: "/hotspot-detect.html"
 * - Windows: "/connecttest.txt" and "/ncsi.txt"
 * 
 * @note
 * As of the current implementation, some platforms may not properly trigger
 * captive portal detection with these routes. Testing and adjustments may be
 * needed to ensure compatibility across all target platforms.
 * Known working devices: iPadOS 18
 * Known non-working devices: Windows 11, Kali Purple, Android 16
 * 
 */
void setupPortalEndpoints(ESP8266WebServer& server) {
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

/**
 * @brief Register routes for the web server, including the main page and static assets.
 * 
 * @details
 * This function sets up all static routes for the web server for the website.
 * It defines a route for the root path ("/") to serve the main page, and
 * uses the serveStatic method to serve static assets from the LittleFS
 * filesystem. Additionally, it sets up a catch-all route for unknown
 * paths to serve the index.html file, which is useful for single-page
 * applications and captive portal redirection. Lastly, it includes any API
 * endpoints that the captive portal may need to function properly.
 *
 * @param server Reference to the ESP8266WebServer instance to register the routes on.
 * 
 * @par Returns
 * Nothing.
 * 
 */
void registerRoutes(ESP8266WebServer& server) {
    
    // Main page with static assets
    server.on("/", HTTP_GET, [&server]() {
        handleRoot(server);
    });

    server.serveStatic(web_config::kHtmlDir, LittleFS, web_config::kHtmlDir);
    server.serveStatic(web_config::kJsDir, LittleFS, web_config::kJsDir);
    server.serveStatic(web_config::kStylesDir, LittleFS, web_config::kStylesDir);
    
    // API endpoints
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
/** @} */ // end of Private

/**
 * @defgroup Public
 * Public API for the web server service, declared in web_server.h.
 * @{
 */
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

    if (web_config::kEnablePortal) {
        setupPortalEndpoints(server);
    }

    server.begin();
    debug_logs::webLogging("Web server started successfully");
    return true;
}
/** @} */ // end of Public
