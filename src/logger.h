/**
 * @file logger.h
 * @brief Logger for debug messages.
 *
 */

#pragma once

#include "configs.h"
#include <stdarg.h>

namespace {
constexpr uint8_t kMaxMessages = 32;
constexpr uint8_t kMessageSize = 128;

struct LogMessage {
    char text[kMessageSize];
};

inline LogMessage queue[kMaxMessages];
inline volatile uint8_t head = 0;
inline volatile uint8_t tail = 0;

inline bool pushLog(const char *prefix, const char *fmt, va_list args) {
    uint8_t nextHead = (head + 1) % kMaxMessages;

    // Buffer full
    if (nextHead == tail) return false;

    char buffer[kMessageSize - 16]; // Leave space for prefix and null terminator
    vsnprintf(buffer, sizeof(buffer), fmt, args);

    snprintf(queue[head].text, sizeof(queue[head].text), "%s %s", prefix, buffer);

    head = nextHead;
    return true;
}

inline bool popLog(LogMessage &msg) {
    if (tail == head) return false;

    msg = queue[tail];
    tail = (tail + 1) % kMaxMessages;
    return true;
}

inline bool lineBreak(const char *breaker = "---------------------------------------------------") {    
    snprintf(queue[head].text, sizeof(queue[head].text), "%s", breaker);
    head = (head + 1) % kMaxMessages;
    
    return true;
}

inline bool addTimestamp() {
    unsigned long timestamp = millis();
    snprintf(queue[head].text, sizeof(queue[head].text), "Hours: %ld Minutes: %ld Seconds: %ld", (timestamp/1000/60/60)%24, (timestamp/1000/60)%60, (timestamp/1000)%60);
    head = (head + 1) % kMaxMessages;
    
    return true;
}

inline void flushLogs() {
    LogMessage msg;
    addTimestamp();
    lineBreak();
    while (popLog(msg)) Serial.println(msg.text);
}
} // namespace

namespace debug_logs {
#define DEFINE_LOGGER(name, enabled, prefix)        \
    inline bool name(const char *fmt, ...) {        \
        if (!(enabled)) return false;               \
        va_list args;                               \
        va_start(args, fmt);                        \
        bool result = pushLog(prefix, fmt, args);   \
        va_end(args);                               \
        flushLogs();                                \
        return result;                              \
    }

    DEFINE_LOGGER(wifiLogging, debug_config::kEnableWiFiLogging, debug_config::kWiFiPrefix)
    DEFINE_LOGGER(leaseLogging, debug_config::kEnableLeaseLogging, debug_config::kLeasePrefix)
    DEFINE_LOGGER(ledLogging, debug_config::kEnableLEDLogging, debug_config::kLEDPrefix)
    DEFINE_LOGGER(dnsLogging, debug_config::kEnableDNSLogging, debug_config::kDNSPrefix)
#undef DEFINE_LOGGER
} // namespace debug_logs