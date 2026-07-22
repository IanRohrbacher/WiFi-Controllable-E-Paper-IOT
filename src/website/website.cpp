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
 * to work in conjunction with the WiFi and DNS modules to provide a seamless
 * captive portal experience for clients connecting to the access point.
 * 
 */


#include <LittleFS.h>
#include <ESP8266WebServer.h>

#include "configs.h"
#include "logger.h"
#include "display/display.h"

/**
 * @defgroup Private
 * Member variables/functions used internally by the web server module.
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
        debug_logs::webLogging("Failed to open index.html for root path");
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
 * - Android/ChromeOS/most OEMs: "/generate_204", "/gen_204" (Xiaomi/MIUI and
 *   China-region Qualcomm builds hit their own domains but the same path)
 * - /e/OS (privacy-focused Android fork): "/net_204"
 * - iOS/macOS: "/hotspot-detect.html", "/library/test/success.html"
 * - Windows: "/connecttest.txt" (current, Win10 1607+) and "/ncsi.txt" (legacy,
 *   pre-1607) - both bodies confirmed verbatim against Microsoft's own NCSI docs
 * - Linux (NetworkManager/GNOME): "/check_network_status.txt"
 * - Firefox: "/success.txt", "/canonical.html"
 * - Kindle/FireOS: "/kindle-wifi/wifistub.html", "/kindle-wifi/wifiredirect.html", "/blank.html"
 * - Microsoft Edge (edge-http.microsoft.com): "/captiveportal/generate_204"
 *
 * @note
 * Origanl sources:
 * https://captivebehavior.wballiance.com/,
 * https://madscitech.com/faqs/captive-portal-test-urls/,
 * https://en.wikipedia.org/wiki/Captive_portal
 * Scraping the internet found the following sources:
 * the AOSP captive-portal-detection README, Microsoft's own NCSI FAQ doc, and
 * several community-maintained probe-URL lists (gists/uptest).
 * These findings that don't change the original three endpoint list but are
 * worth recording:
 *  - NetworkManager's captive-portal check host is NOT standardized across
 *    distros, it's a distro-packaged config value (Arch defaults to
 *    ping.archlinux.org, others pick their own), so there is no single "Linux"
 *    path to add beyond the GNOME default already registered. Diminishing
 *    returns to chase every distro; the onNotFound() catch-all covers the rest.
 *  - Confirmed via Microsoft's own docs: Windows 11 always uses the HTTP probe
 *    only, and no longer falls back to the dns.msftncsi.com DNS-mismatch signal
 *    that older Windows used as a second detection path.
 *  - Some probes (observed for a Google check variant) hit a hardcoded IP
 *    literal instead of a hostname. A DNS-wildcard captive portal cannot
 *    intercept those.
 *  - Samsung Android also probes a CloudFront-hosted check
 *    (d2uzsrnmmf6tds.cloudfront.net) with no publicly documented path found.
 *  - Gaming consoles, smart TVs (Tizen/webOS/Android TV/Roku), and KaiOS have
 *    no publicly documented captive-portal probe scheme found.
 *  - No platform's connectivity *probe* was found to require HTTPS. Probes are
 *    deliberately plain HTTP everywhere, specifically so a captive portal can
 *    intercept them at all.
 * Known working devices: iPadOS 18, Windows 11
 * Known non-working devices: Kali Purple, Galaxy S23 - Android 16
 *
 */
void setupPortalEndpoints(ESP8266WebServer& server) {
    // Android/ChromeOS
    server.on("/generate_204", [&server]() {
        server.sendHeader("Location", "/", true);
        debug_logs::webLogging("Redirecting /generate_204 to /");
        server.send(302, "text/plain", "Success");
    });

    // legacy/alternate path used by some older Android and Chrome builds
    server.on("/gen_204", [&server]() {
        server.sendHeader("Location", "/", true);
        debug_logs::webLogging("Redirecting /gen_204 to /");
        server.send(302, "text/plain", "Success");
    });
    // /e/OS (privacy-focused Android fork) uses its own generate_204-style path
    server.on("/net_204", [&server]() {
        server.sendHeader("Location", "/", true);
        debug_logs::webLogging("Redirecting /net_204 to /");
        server.send(302, "text/plain", "Success");
    });

    // iOS/macOS
    server.on("/hotspot-detect.html", [&server]() {
        server.sendHeader("Location", "/", true);
        debug_logs::webLogging("Redirecting /hotspot-detect.html to /");
        server.send(302, "text/plain", "Success");
    });
    // older/alternate Apple captive check path
    server.on("/library/test/success.html", [&server]() {
        server.sendHeader("Location", "/", true);
        debug_logs::webLogging("Redirecting /library/test/success.html to /");
        server.send(302, "text/plain", "Success");
    });

    // Windows
    server.on("/connecttest.txt", [&server]() {
        server.sendHeader("Location", "/", true);
        debug_logs::webLogging("Redirecting /connecttest.txt to /");
        server.send(302, "text/plain", "Microsoft Connect Test");
    });
    server.on("/ncsi.txt", [&server]() {
        server.sendHeader("Location", "/", true);
        debug_logs::webLogging("Redirecting /ncsi.txt to /");
        server.send(302, "text/plain", "Microsoft NCSI");
    });
    // Microsoft Edge (edge-http.microsoft.com/captiveportal/generate_204)
    server.on("/captiveportal/generate_204", [&server]() {
        server.sendHeader("Location", "/", true);
        debug_logs::webLogging("Redirecting /captiveportal/generate_204 to /");
        server.send(302, "text/plain", "");
    });

    // linux
    server.on("/check_network_status.txt", [&server]() {
        server.sendHeader("Location", "/", true);
        debug_logs::webLogging("Redirecting /check_network_status.txt to /");
        server.send(302, "text/plain", "NetworkManager is online");
    });

    // firefox
    server.on("/success.txt", [&server]() {
        server.sendHeader("Location", "/", true);
        server.send(302, "text/plain", "success");
    });
    // firefox checks canonical.html first, expecting a redirect/meta-refresh
    // chain to success.txt; anything else (including this) reads as captive
    server.on("/canonical.html", [&server]() {
        server.sendHeader("Location", "/", true);
        debug_logs::webLogging("Redirecting /canonical.html to /");
        server.send(302, "text/plain", "");
    });

    // Amazon Kindle/FireOS (Silk browser)
    server.on("/kindle-wifi/wifistub.html", [&server]() {
        server.sendHeader("Location", "/", true);
        debug_logs::webLogging("Redirecting /kindle-wifi/wifistub.html to /");
        server.send(302, "text/plain", "OK");
    });
    server.on("/kindle-wifi/wifiredirect.html", [&server]() {
        server.sendHeader("Location", "/", true);
        debug_logs::webLogging("Redirecting /kindle-wifi/wifiredirect.html to /");
        server.send(302, "text/plain", "OK");
    });
    server.on("/blank.html", [&server]() {
        server.sendHeader("Location", "/", true);
        debug_logs::webLogging("Redirecting /blank.html to /");
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
    server.on(web_config::kDisplayStatusRoute, HTTP_GET, [&server]() {
        char body[64];
        snprintf(body, sizeof(body), R"({"width":%u,"height":%u})", displayWidth(), displayHeight());
        server.send(200, "application/json", body);
    });

    server.on(web_config::kDisplayFrameRoute, HTTP_POST,
        [&server]() {
            const DisplayStatus status = finishFrameUpload();
            if (status == DisplayStatus::Success) {
                if (refreshDisplay()) {
                    server.send(200, "application/json", R"({"status":"ok","displayed":true})");
                } else {
                    debug_logs::webLogging("Frame accepted but the display module is not running; panel was not refreshed.");
                    server.send(200, "application/json", R"({"status":"ok","displayed":false})");
                }
            } else {
                char body[128];
                snprintf(body, sizeof(body), R"({"status":"error","message":"%s"})", displayStatusMessage(status));
                debug_logs::webLogging("Rejected /api/display/frame upload: %s", displayStatusMessage(status));
                server.send(400, "application/json", body);
            }
        },
        [&server]() {
            HTTPUpload& upload = server.upload();
            if (upload.status == UPLOAD_FILE_START) {
                beginFrameUpload();
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                writeFrameChunk(upload.buf, upload.currentSize);
            }
        });

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
 * Public API for the web server module, declared in website.h.
 * @{
 */
bool startWebModule(ESP8266WebServer& server) {
    uint8_t attempts = 0;
    while (!LittleFS.begin() && attempts < web_config::kLittleFSRemountAttempts) {
        debug_logs::webLogging("Failed to mount LittleFS, attempt %d", ++attempts);
        delay(web_config::kLittleFSRemountIntervalMs);
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
