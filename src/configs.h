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

namespace debug_config {
// Enable verbose debug logging for WiFi and captive portal operations.
inline constexpr bool kEnableSerial = true;
inline constexpr bool kEnableVerboseLogging = true && kEnableSerial;

inline constexpr bool kEnableStatusLight = true;
inline constexpr bool kEnableStatusLogging = true && kEnableVerboseLogging && kEnableStatusLight;
inline constexpr const char* kStatusPrefix = "[STATUS] ";
}  // namespace debug_config

namespace debug_logs {
inline bool statusLogging(const char* message) {
    if(debug_config::kEnableStatusLogging) {
        Serial.print(debug_config::kStatusPrefix);
        Serial.println(message);
        return true;
    }
    return false;
}
} // namespace debug_logs