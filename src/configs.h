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
inline constexpr size_t kMaxClientLeases = 8;

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
inline constexpr uint16_t kDnsPort = 53;

} // namespace dns_config

namespace debug_config {
inline constexpr uint8_t kThreadRefreshIntervalMs = 20;

// Enable verbose debug logging for WiFi and captive portal operations.
inline constexpr bool kEnableSerial = true;
inline constexpr bool kEnableVerboseLogging = true && kEnableSerial;

inline constexpr bool kEnableWiFiLogging = true && kEnableVerboseLogging;
inline constexpr const char* kWiFiPrefix = "[WIFI] ";

inline constexpr bool kEnableLeaseLogging = true && kEnableVerboseLogging;
inline constexpr const char* kLeasePrefix = "[Lease] ";

inline constexpr bool kEnableDNSLogging = false && kEnableVerboseLogging;
inline constexpr const char* kDNSPrefix = "[DNS] ";

inline constexpr bool kEnableStatusLight = true;
inline constexpr bool kEnableStatusLogging = false && kEnableVerboseLogging && kEnableStatusLight;
inline constexpr const char* kStatusPrefix = "[STATUS] ";
}  // namespace debug_config

namespace debug_logs {
inline bool statusLogging(const char* prefix, const char* fmt, ...) {
    if(debug_config::kEnableStatusLogging) {
        char buffer[256];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        Serial.print(debug_config::kStatusPrefix);
        Serial.println(buffer);
        return true;
    }
    return false;
}

inline bool statusLogging(const char* fmt, ...) {
    if (!debug_config::kEnableStatusLogging) return false;
    return statusLogging(debug_config::kStatusPrefix, fmt);
}
inline bool wifiLogging(const char* fmt, ...) {
    if (!debug_config::kEnableWiFiLogging) return false;
    return statusLogging(debug_config::kWiFiPrefix, fmt);
}
inline bool leaseLogging(const char* fmt, ...) {
    if (!debug_config::kEnableLeaseLogging) return false;
    return statusLogging(debug_config::kLeasePrefix, fmt);
}
} // namespace debug_logs