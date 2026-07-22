/**
 * @file constants.h
 * @brief Build-time configuration constants used across the application.
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
inline constexpr uint8_t kRefreshIntervalMs = 20;

}  // namespace main_config

namespace wifi_config {
inline constexpr uint8_t kMaxClientLeases = 8; // Limited by the ESP8266's internal bitmap of 8 connected stations.

inline constexpr char kApSsid[] = "http://portal.local";
inline constexpr char kApPassword[] = "password";
inline constexpr uint8_t kApChannel = 1;
inline constexpr bool kBroadCastAp = true;

inline const IPAddress kLocalIp = IPAddress(192, 168, 4, 1);
inline const IPAddress kGateway = IPAddress(192, 168, 4, 1);
inline const IPAddress kSubnet = IPAddress(255, 255, 255, 0);

inline constexpr uint8_t kWebPort = 80;

inline constexpr unsigned long kMaxApStartTimeout = 15UL * 1000UL;//ms = 15s
inline constexpr unsigned long kMaxLeaseTimeMs = 1UL * 30UL * 1000UL;
inline constexpr unsigned long kLeaseStaleMs = 1UL * 15UL * 1000UL;

inline constexpr uint8_t kThreadRefreshIntervalMs = 20;
} // namespace wifi_config

namespace dns_config {
// mDNS hostname (without ".local") and the wildcard captive-DNS domain
// clients are redirected for. Browsing to http://<kPortalHost>.local/
// resolves via mDNS regardless of the client's DNS/DoH configuration.
inline constexpr char kPortalHost[] = "portal";
inline constexpr uint8_t kDnsPort = 53;

inline constexpr uint8_t kDNSInitAttempts = 3;
} // namespace dns_config

namespace web_config {
inline constexpr bool kEnablePortal = true;

inline constexpr char kHtmlIndexPath[] = "/html/index.html";
inline constexpr char kHtmlDir[] = "/html/";
inline constexpr char kStylesDir[] = "/styles/";
inline constexpr char kJsDir[] = "/js/";

inline constexpr char kDisplayFrameRoute[] = "/api/display/frame";
inline constexpr char kDisplayStatusRoute[] = "/api/display/status";

inline constexpr unsigned long kLittleFSRemountIntervalMs = 1UL * 30UL * 1000UL; // 30 seconds
inline constexpr uint8_t kLittleFSRemountAttempts = 3;
}  // namespace web_config

namespace display_config {
inline constexpr bool kClearOnStart = false;

inline constexpr unsigned long kDriverInitIntervalMs = 1UL * 5UL * 1000UL; // 5 seconds
inline constexpr uint8_t kDriverInitAttempts = 3;
} // namespace display_config

namespace debug_config {
inline constexpr uint8_t kThreadRefreshIntervalMs = 20;

// Enable verbose debug logging for WiFi and captive portal operations.
inline constexpr bool kEnableVerboseLogging = false;
inline constexpr bool kEnableStatusLight = true;
inline constexpr unsigned long kLoopLogDelay = 1UL * 1UL * 1000UL; // 1 seconds

inline constexpr bool kEnableWiFiLogging = false && kEnableVerboseLogging;
inline constexpr const char* kWiFiPrefix = "[WIFI]";
inline constexpr unsigned long kWiFiLoopDelay = 1UL * 5UL * 1000UL; // 1 second

inline constexpr bool kEnableLeaseLogging = false && kEnableVerboseLogging;
inline constexpr const char* kLeasePrefix = "[Lease]";
inline constexpr unsigned long kLeaseLoopDelay = 1UL * 5UL * 1000UL; // 5 seconds

inline constexpr bool kEnableLEDLogging = false && kEnableVerboseLogging;
inline constexpr const char* kLEDPrefix = "[LED]";
inline constexpr unsigned long kLEDLoopDelay = 1UL * 5UL * 1000UL; // 5 seconds

inline constexpr bool kEnableDNSLogging = false && kEnableVerboseLogging;
inline constexpr const char* kDNSPrefix = "[DNS]";
inline constexpr unsigned long kDNSLoopDelay = 1UL * 5UL * 1000UL; // 5 seconds

inline constexpr bool kEnableWebLogging = false && kEnableVerboseLogging;
inline constexpr const char* kWebPrefix = "[Web]";
inline constexpr unsigned long kWebLoopDelay = 1UL * 5UL * 1000UL; // 5 seconds

inline constexpr bool kEnableDisplayLogging = false && kEnableVerboseLogging;
inline constexpr const char* kDisplayPrefix = "[Display]";
inline constexpr unsigned long kDisplayLoopDelay = 1UL * 5UL * 1000UL; // 5 seconds
}  // namespace debug_config
