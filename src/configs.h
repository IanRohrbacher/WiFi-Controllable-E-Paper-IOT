/**
 * @file configs.h
 *
 * @brief Build-time configuration constants used across the application.
 *
 * @details
 * Every tunable value in the firmware lives here, grouped by the module
 * that consumes it. Nothing in this file allocates or runs logic, it is
 * read-only, compile-time configuration shared by every other module via
 * `#include "configs.h"`.
 *
 */

#pragma once

#include <stddef.h>
#include <cstdint>
#include <stdarg.h>
#include <stdio.h>
#include <Arduino.h>
#include <ESP8266WiFi.h>

namespace main_config {
/** @brief Delay, in milliseconds, between successive loop() iterations. */
inline constexpr uint8_t kRefreshIntervalMs = 20;

} // namespace main_config

namespace wifi_config {
/** @brief Maximum simultaneous client leases. */
inline constexpr uint8_t kMaxClientLeases = 8;

/** @brief SSID broadcast (or hidden) by the access point. */
inline constexpr char kApSsid[] = "http://portal.local";
/** @brief Password required to join the access point. */
inline constexpr char kApPassword[] = "password";
/** @brief WiFi channel the access point operates on. */
inline constexpr uint8_t kApChannel = 1;
/** @brief When true, the access point's SSID is broadcast instead of hidden. */
inline constexpr bool kBroadCastAp = true;

/** @brief IP address assigned to the access point itself. */
inline const IPAddress kLocalIp = IPAddress(192, 168, 4, 1);
/** @brief Gateway address advertised to connected clients. */
inline const IPAddress kGateway = IPAddress(192, 168, 4, 1);
/** @brief Subnet mask advertised to connected clients. */
inline const IPAddress kSubnet = IPAddress(255, 255, 255, 0);

/** @brief TCP port the captive portal web server listens on. */
inline constexpr uint8_t kWebPort = 80;

/** @brief Maximum time, in milliseconds, to wait for the AP to come up. */
inline constexpr unsigned long kMaxApStartTimeout = 15UL * 1000UL; // 15s

/**
 * @brief Length of an active client session, in milliseconds.
 *
 * @par Options
 * Any positive duration, or 0 to disable session expiry entirely.
 * 
 */
inline constexpr unsigned long kSessionDurationMs = 5UL * 60UL * 1000UL; // 5 minutes

/**
 * @brief Length of the post disconnect reconnect block, in milliseconds.
 *
 * @par Options
 * Any positive duration, or 0 to disable the reconnect block entirely.
 * 
 */
inline constexpr unsigned long kBlockedDurationMs = 5UL * 60UL * 1000UL; // 5 minutes

/** @brief Maximum number of MACs that may be serving a reconnect block at once. */
inline constexpr uint8_t kMaxBlockedEntries = 50;

/** @brief Maximum number of MACs that may have banked, unused session time at once. */
inline constexpr uint8_t kMaxStaleEntries = 50;

/** @brief How long, in milliseconds, a lease may go unseen before removal. */
inline constexpr unsigned long kLeaseStaleMs = 1UL * 15UL * 1000UL;

/** @brief Interval, in milliseconds, between AP thread ticks. */
inline constexpr uint8_t kThreadRefreshIntervalMs = 20;

} // namespace wifi_config

namespace dns_config {
/** @brief mDNS and wildcard captive DNS hostname for the portal. */
inline constexpr char kPortalHost[] = "portal";
/** @brief UDP port the captive DNS server listens on. */
inline constexpr uint8_t kDnsPort = 53;

/** @brief Number of retries attempted when the DNS server fails to start. */
inline constexpr uint8_t kDNSInitAttempts = 3;

} // namespace dns_config

namespace web_config {
/** @brief When true, captive portal detection endpoints are registered. */
inline constexpr bool kEnablePortal = true;

/** @brief LittleFS path of the main index page. */
inline constexpr char kHtmlIndexPath[] = "/html/index.html";
/** @brief LittleFS directory served for HTML assets. */
inline constexpr char kHtmlDir[] = "/html/";
/** @brief LittleFS directory served for stylesheet assets. */
inline constexpr char kStylesDir[] = "/styles/";
/** @brief LittleFS directory served for JavaScript assets. */
inline constexpr char kJsDir[] = "/js/";

/** @brief Route accepting a new bitmap frame upload. */
inline constexpr char kDisplayFrameRoute[] = "/api/display/frame";
/** @brief Route reporting the active panel's dimensions and rotation. */
inline constexpr char kDisplayStatusRoute[] = "/api/display/status";
/** @brief Route reporting the requesting client's lease status. */
inline constexpr char kLeaseStatusRoute[] = "/api/lease/status";

/** @brief LittleFS path of the page served to blocked clients. */
inline constexpr char kBlockedHtmlPath[] = "/html/blocked.html";

/** @brief Delay, in milliseconds, between LittleFS remount attempts. */
inline constexpr unsigned long kLittleFSRemountIntervalMs = 1UL * 30UL * 1000UL; // 30 seconds
/** @brief Number of retries attempted when mounting LittleFS fails. */
inline constexpr uint8_t kLittleFSRemountAttempts = 3;

} // namespace web_config

namespace display_config {
/** @brief When true, the panel is cleared to white on module start. */
inline constexpr bool kClearOnStart = false;

/** @brief Delay, in milliseconds, between display driver init attempts. */
inline constexpr unsigned long kDriverInitIntervalMs = 1UL * 5UL * 1000UL; // 5 seconds
/** @brief Number of retries attempted when display driver init fails. */
inline constexpr uint8_t kDriverInitAttempts = 3;

/**
 * @brief Display rotation applied client side by the browser editor.
 *
 * @par Options
 * 0, 90, 180, or 270 degrees.
 *
 */
inline constexpr uint16_t kRotationDegrees = 180;

/**
 * @brief Minimum time, in milliseconds, between queued frames being uploaded.
 *
 * @details
 * Ticks down continuously regardless of queue contents. If it reaches 0 while
 * the queue is empty, it simply holds at 0 until a frame is queued, which then
 * updated immediately.
 *
 * @par Options
 * Any positive duration, or 0 to update queued frames back to back.
 *
 */
inline constexpr unsigned long kDisplayCooldownMs = 1UL * 30UL * 1000UL; // 30 seconds

/** @brief Interval, in milliseconds, between display thread ticks. */
inline constexpr uint8_t kThreadRefreshIntervalMs = 20;

/** @brief LittleFS directory the frame queue's per-frame files live in. */
inline constexpr char kFramesDir[] = "/frames/";

/** @brief LittleFS path an in-progress frame upload is staged at before it commits. */
inline constexpr char kUploadTmpPath[] = "/frames/upload.tmp";

/** @brief Free flash space, in bytes, kept in reserve beyond one worst-case frame. */
inline constexpr size_t kMinFreeFlashBytes = 8192;

} // namespace display_config

namespace io_config {
/** @brief GPIO pin (D10, GPIO1) the clear button is wired to. */
inline constexpr uint8_t kClearButtonPin = D10;

/** @brief Interval, in milliseconds, between IO thread ticks. */
inline constexpr uint8_t kThreadRefreshIntervalMs = 20;

} // namespace io_config

namespace debug_config {
/** @brief Interval, in milliseconds, used by cooperative thread ticks. */
inline constexpr uint8_t kThreadRefreshIntervalMs = 20;

/** @brief When true, the onboard status LED reflects device state. */
inline constexpr bool kEnableStatusLight = true;

/** @brief Master switch for all verbose debug logging. */
inline constexpr bool kEnableVerboseLogging = false;
/** @brief Interval, in milliseconds, between flushes of the log queue. */
inline constexpr unsigned long kLoopLogDelay = 1UL * 1UL * 1000UL; // 1 second

/** @brief Enables WiFi/AP controller log messages. */
inline constexpr bool kEnableWiFiLogging = false && kEnableVerboseLogging;
/** @brief Prefix prepended to WiFi/AP controller log messages. */
inline constexpr const char* kWiFiPrefix = "[WIFI]";
/** @brief Interval, in milliseconds, between periodic WiFi status logs. */
inline constexpr unsigned long kWiFiLoopDelay = 1UL * 5UL * 1000UL; // 5 seconds

/** @brief Enables client lease log messages. */
inline constexpr bool kEnableLeaseLogging = false && kEnableVerboseLogging;
/** @brief Prefix prepended to client lease log messages. */
inline constexpr const char* kLeasePrefix = "[Lease]";

/** @brief Enables status LED log messages. */
inline constexpr bool kEnableLEDLogging = false && kEnableVerboseLogging;
/** @brief Prefix prepended to status LED log messages. */
inline constexpr const char* kLEDPrefix = "[LED]";
/** @brief Interval, in milliseconds, between periodic LED status logs. */
inline constexpr unsigned long kLEDLoopDelay = 1UL * 5UL * 1000UL; // 5 seconds

/** @brief Enables captive DNS log messages. */
inline constexpr bool kEnableDNSLogging = false && kEnableVerboseLogging;
/** @brief Prefix prepended to captive DNS log messages. */
inline constexpr const char* kDNSPrefix = "[DNS]";
/** @brief Interval, in milliseconds, between periodic DNS status logs. */
inline constexpr unsigned long kDNSLoopDelay = 1UL * 5UL * 1000UL; // 5 seconds

/** @brief Enables web server log messages. */
inline constexpr bool kEnableWebLogging = false && kEnableVerboseLogging;
/** @brief Prefix prepended to web server log messages. */
inline constexpr const char* kWebPrefix = "[Web]";

/** @brief Enables display module log messages. */
inline constexpr bool kEnableDisplayLogging = false && kEnableVerboseLogging;
/** @brief Prefix prepended to display module log messages. */
inline constexpr const char* kDisplayPrefix = "[Display]";
/** @brief Interval, in milliseconds, between periodic display status logs. */
inline constexpr unsigned long kDisplayLoopDelay = 1UL * 5UL * 1000UL; // 5 seconds

/** @brief Enables IO scheduler log messages. */
inline constexpr bool kEnableIOLogging = false && kEnableVerboseLogging;
/** @brief Prefix prepended to IO scheduler log messages. */
inline constexpr const char* kIOPrefix = "[IO]";

} // namespace debug_config
