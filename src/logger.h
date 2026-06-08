/**
 * @file logger.h
 * @brief Logger for debug messages.
 *
 * This file provides a set of logging functions for debug messages, which can be
 * enabled or disabled at compile time. Each log message is prefixed with a tag 
 * (e.g. "[WIFI]") to indicate the source of the log.
 */

#pragma once

#include "configs.h"

namespace debug_logs {
inline bool lineBreak(const char* breaker = "---------------------------------------------------") {
    if (!debug_config::kEnableVerboseLogging) return false;

    Serial.println(breaker);
    return true;
}
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