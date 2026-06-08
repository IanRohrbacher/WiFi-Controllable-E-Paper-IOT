/**
 * @file constants.h
 * @brief Build-time configuration constants used across the portal.
 *
 * These inline constexpr values define AP credentials, captive portal host
 * names, timeouts, and LittleFS paths used by the web interface.
 */

#pragma once

#include <stddef.h>
#include <cstdint>
#include <stdarg.h>
#include <stdio.h>
#include <Arduino.h>

namespace main_config {
inline constexpr uint8_t kRefreshIntervalMs = 20;

}  // namespace main_config

namespace wifi_config {
inline constexpr uint8_t kMaxClientLeases = 8; // Limited by the ESP8266's internal bitmap of 8 connected stations.

inline constexpr char kApSsid[] = "wifi-portal";
inline constexpr char kApPassword[] = "password";
inline constexpr uint8_t kApChannel = 1;
inline constexpr bool kBroadCastAp = true;
inline constexpr uint8_t kWebPort = 80;

inline constexpr unsigned long kMaxApStartTimeout = 15UL * 1000UL;//ms = 15s
inline constexpr unsigned long kMaxLeaseTimeMs = 3UL * 60UL * 1000UL;
inline constexpr unsigned long kLeaseStaleMs = 1UL * 60UL * 1000UL;

inline constexpr uint8_t kThreadRefreshIntervalMs = 20;
} // namespace wifi_config

namespace dns_config {
inline constexpr char kPortalHost[] = "portaldns";
inline constexpr uint8_t kDnsPort = 53;

} // namespace dns_config

namespace portal_config {

} // namespace portal_config

namespace web_config {
inline constexpr char kHtmlIndexPath[] = "/html/index.html";
inline constexpr char kHtmlDir[] = "/html/";
inline constexpr char kStylesDir[] = "/styles/";
inline constexpr char kTsDir[] = "/ts/";

}  // namespace web_config

namespace debug_config {
inline constexpr uint8_t kThreadRefreshIntervalMs = 20;

// Enable verbose debug logging for WiFi and captive portal operations.
inline constexpr bool kEnableVerboseLogging = true;
inline constexpr bool kEnableStatusLight = true;

inline constexpr bool kEnableWiFiLogging = true && kEnableVerboseLogging;
inline constexpr const char* kWiFiPrefix = "[WIFI] ";

inline constexpr bool kEnableLeaseLogging = true && kEnableVerboseLogging;
inline constexpr const char* kLeasePrefix = "[Lease] ";

inline constexpr bool kEnableLEDLogging = false && kEnableVerboseLogging;
inline constexpr const char* kLEDPrefix = "[LED] ";

inline constexpr bool kEnableDNSLogging = false && kEnableVerboseLogging;
inline constexpr const char* kDNSPrefix = "[DNS] ";
}  // namespace debug_config

namespace debug_logs {
inline bool vLogging(const char* prefix, const char* fmt, va_list args) {
    if (!debug_config::kEnableVerboseLogging) return false;

    static char buffer[128]; // 96 || 128 || 256
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    Serial.print(prefix);
    Serial.println(buffer);

    return true;
}

#define DEFINE_LOGGER(name, enabled, prefix)            \
inline bool name(const char* fmt, ...) {                \
    if (!(enabled)) return false;                       \
    va_list args;                                       \
    va_start(args, fmt);                                \
    bool result = vLogging(prefix, fmt, args);    \
    va_end(args);                                       \
    return result;                                      \
}

DEFINE_LOGGER(miscLogging,  debug_config::kEnableVerboseLogging,    "[test]")

DEFINE_LOGGER(wifiLogging,  debug_config::kEnableWiFiLogging,       debug_config::kWiFiPrefix)
DEFINE_LOGGER(leaseLogging, debug_config::kEnableLeaseLogging,      debug_config::kLeasePrefix)
DEFINE_LOGGER(ledLogging,   debug_config::kEnableLEDLogging,        debug_config::kLEDPrefix)
DEFINE_LOGGER(dnsLogging,   debug_config::kEnableDNSLogging,        debug_config::kDNSPrefix)

#undef DEFINE_LOGGER
} // namespace debug_logs