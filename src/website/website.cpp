/**
 * @file website.cpp
 * 
 * @brief Implementation of the web server for serving the captive portal and API endpoints.
 * 
 * @details
 * Mounts @c LittleFS for static assets, then registers the API routes plus a
 * broad set of platform-specific captive-portal probe paths (see @c
 * setupPortalEndpoints()) so operating systems detect the portal and pop their
 * sign-in flow automatically, instead of relying on the user to open a browser
 * themselves.
 *
 */


#include <LittleFS.h>
#include <ESP8266WebServer.h>

#include "configs.h"
#include "logger.h"
#include "display/display.h"
#include "wifi/wifi_lease.h"

/**
 * @defgroup Private
 * Member variables/functions used internally by the web server module.
 * These are not intended to be used outside of this module.
 * @{
 */
namespace {
/**
 * @brief Convert a @c LeaseState enum value to a string.
 *
 * @param state Lease state to describe.
 * @return A statically-allocated, null-terminated lowercase state name.
 * 
 */
const char* leaseStateToString(LeaseState state) {
    switch (state) {
        case LeaseState::Active: return "active";
        case LeaseState::Blocked: return "blocked";
        case LeaseState::Unlimited: return "unlimited";
        case LeaseState::Unknown:
        default: return "unknown";
    }
}

/**
 * @brief Serve a file from LittleFS at the given path as the response body.
 *
 * @param server Reference to the ESP8266WebServer instance handling the request.
 * @param path LittleFS path of the file to stream.
 * @param notFoundMessage Message logged (and sent as a 500) if the file can't be opened.
 *
 * @return Whether the file was served.
 * @retval true The file was successfully served to the client.
 * @retval false The file could not be opened, a 500 response was sent instead.
 * 
 */
bool serveHtmlFile(ESP8266WebServer& server, const char* path, const char* notFoundMessage) {
    File file = LittleFS.open(path, "r");

    if (!file) {
        debug_logs::webLogging("%s", notFoundMessage);
        server.send(500, "text/plain", notFoundMessage);
        return false;
    }

    server.streamFile(file, "text/html");
    file.close();
    return true;
}

/**
 * @brief Handle requests to the root path ("/") of the web server.
 *
 * @details
 * Serves @c blocked.html instead of @c index.html when the requesting client
 * is currently blocked.
 *
 * @param server Reference to the ESP8266WebServer instance handling the request.
 *
 * @return The status of the request handling attempt.
 * @retval true The appropriate page was successfully served to the client.
 * @retval false The page could not be opened; an error response was sent instead.
 *
 * @see isBlocked()
 *
 */
bool handleRoot(ESP8266WebServer& server) {
    if (isBlocked(server.client().remoteIP())) {
        return serveHtmlFile(server, web_config::kBlockedHtmlPath, "Failed to open blocked.html for root path");
    }
    return serveHtmlFile(server, web_config::kHtmlIndexPath, "Failed to open index.html for root path");
}

/**
 * @brief Register a captive-portal probe path that redirects to "/".
 *
 * @param server Reference to the ESP8266WebServer instance to register on.
 * @param path Probe path to match, e.g. "/generate_204".
 * @param body Response body sent alongside the redirect.
 *
 * @par Returns
 * Nothing.
 *
 */
void registerCaptiveRedirect(ESP8266WebServer& server, const char* path, const char* body = "Success") {
    server.on(path, [&server, path, body]() {
        server.sendHeader("Location", "/", true);
        debug_logs::webLogging("Redirecting %s to /", path);
        server.send(302, "text/plain", body);
    });
}

/**
 * @brief Set up routes for captive portal redirection for various platforms.
 *
 * @param server Reference to the ESP8266WebServer instance to set up the routes on.
 * 
 * @par Returns
 * Nothing.
 * 
 * @par Endpoints
 * - Android/ChromeOS/most OEMs: "/generate_204", "/gen_204" (Xiaomi/MIUI and China-region Qualcomm builds hit their own domains but the same path)
 * - /e/OS (privacy-focused Android fork): "/net_204"
 * - iOS/macOS: "/hotspot-detect.html", "/library/test/success.html"
 * - Windows: "/connecttest.txt" (current, Win10 1607+) and "/ncsi.txt" (legacy, pre-1607), both bodies confirmed verbatim against Microsoft's own NCSI docs
 * - Linux (NetworkManager/GNOME): "/check_network_status.txt"
 * - Firefox: "/success.txt", "/canonical.html"
 * - Kindle/FireOS: "/kindle-wifi/wifistub.html", "/kindle-wifi/wifiredirect.html", "/blank.html"
 * - Microsoft Edge (edge-http.microsoft.com): "/captiveportal/generate_204"
 *
 * @note
 * Original sources:
 * https://captivebehavior.wballiance.com/,
 * https://madscitech.com/faqs/captive-portal-test-urls/,
 * https://en.wikipedia.org/wiki/Captive_portal
 * Scraping the internet with AI found the following sources:
 * the AOSP captive-portal-detection README, Microsoft's own NCSI FAQ doc, and
 * several community-maintained probe-URL lists (gists/uptest).
 * These findings that don't change the original three endpoint list but are
 * worth recording:
 *  - NetworkManager's captive-portal check host is NOT standardized across
 *    distros, it's a distro-packaged config value (Arch defaults to
 *    ping.archlinux.org, others pick their own), so there is no single "Linux"
 *    path to add beyond the GNOME default already registered. Diminishing
 *    returns to chase every distro; the @c onNotFound() catch-all covers the rest.
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
 * Known non-working devices: Kali Purple, Galaxy S23 (Android 16)
 *
 */
void setupPortalEndpoints(ESP8266WebServer& server) {
    // Android/ChromeOS
    registerCaptiveRedirect(server, "/generate_204");
    // legacy/alternate path used by some older Android and Chrome builds
    registerCaptiveRedirect(server, "/gen_204");
    // /e/OS (privacy-focused Android fork) uses its own generate_204-style path
    registerCaptiveRedirect(server, "/net_204");

    // iOS/macOS
    registerCaptiveRedirect(server, "/hotspot-detect.html");
    // older/alternate Apple captive check path
    registerCaptiveRedirect(server, "/library/test/success.html");

    // Windows
    registerCaptiveRedirect(server, "/connecttest.txt", "Microsoft Connect Test");
    registerCaptiveRedirect(server, "/ncsi.txt", "Microsoft NCSI");
    // Microsoft Edge (edge-http.microsoft.com/captiveportal/generate_204)
    registerCaptiveRedirect(server, "/captiveportal/generate_204", "");

    // linux
    registerCaptiveRedirect(server, "/check_network_status.txt", "NetworkManager is online");

    // firefox
    registerCaptiveRedirect(server, "/success.txt", "success");
    // firefox checks canonical.html first, expecting a redirect/meta-refresh
    // chain to success.txt; anything else (including this) reads as captive
    registerCaptiveRedirect(server, "/canonical.html", "");

    // Amazon Kindle/FireOS (Silk browser)
    registerCaptiveRedirect(server, "/kindle-wifi/wifistub.html", "OK");
    registerCaptiveRedirect(server, "/kindle-wifi/wifiredirect.html", "OK");
    registerCaptiveRedirect(server, "/blank.html", "");
}

/**
 * @brief Register routes for the web server, including the main page and static assets.
 *
 * @details
 * Registers "/" and the LittleFS-backed static asset directories, the three
 * API endpoints (display status, lease status, frame upload), and an @c
 * onNotFound() fallback that serves @c index.html or @c blocked.html for any
 * unmatched path, which is what makes every captive-portal probe land on the
 * portal itself.
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
        char body[96];
        snprintf(body, sizeof(body), R"({"width":%u,"height":%u,"rotation":%u})", displayWidth(), displayHeight(), display_config::kRotationDegrees);
        server.sendHeader("Cache-Control", "no-store");
        server.send(200, "application/json", body);
    });

    server.on(web_config::kLeaseStatusRoute, HTTP_GET, [&server]() {
        const LeaseStatus status = getLeaseStatus(server.client().remoteIP());
        char body[64];
        snprintf(body, sizeof(body), R"({"state":"%s","remainingMs":%lu})", leaseStateToString(status.state), status.remainingMs);
        server.sendHeader("Cache-Control", "no-store");
        server.send(200, "application/json", body);
    });

    server.on(web_config::kDisplayFrameRoute, HTTP_POST,
        [&server]() {
            if (isBlocked(server.client().remoteIP())) {
                debug_logs::webLogging("Rejected /api/display/frame upload: client is blocked");
                server.send(403, "application/json", R"({"status":"error","message":"session expired"})");
                return;
            }

            if (displayQueueStatus() != DisplayStatus::Success) {
                const unsigned long retryAfterMs = displayNextUpdateMs();
                debug_logs::webLogging("Rejected /api/display/frame upload: %s, retry in %lu ms", displayStatusMessage(DisplayStatus::Busy), retryAfterMs);
                char body[128];
                snprintf(body, sizeof(body), R"({"status":"error","message":"Display queue is full, try again later.","retryAfterMs":%lu})", retryAfterMs);
                server.send(409, "application/json", body);
                return;
            }

            const DisplayStatus status = finishFrameUpload();
            if (status == DisplayStatus::Success) {
                server.send(200, "application/json", R"({"status":"ok","queued":true})");
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
                if (!isBlocked(server.client().remoteIP()) && displayQueueStatus() == DisplayStatus::Success) {
                    beginFrameUpload();
                }
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                writeFrameChunk(upload.buf, upload.currentSize);
            }
        });

    // Captive portal fallback
    server.onNotFound([&server]() {
        debug_logs::webLogging("Serving fallback page for unknown route");
        if (isBlocked(server.client().remoteIP())) {
            serveHtmlFile(server, web_config::kBlockedHtmlPath, "Failed to open blocked.html for unknown route");
            return;
        }

        File file = LittleFS.open(web_config::kHtmlIndexPath, "r");
        if (file) {
            server.streamFile(file, "text/html");
            file.close();
        } else {
            server.send(404, "application/json", R"({"error":"not_found"})");
        }
    });
}

} // namespace
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
