/**
 * @file logger.h
 * @brief Logger for debug messages.
 *
 */

#pragma once

#include "configs.h"
#include <stdarg.h>

namespace debug_logs {
/** @brief Maximum number of log messages that can be queued. */
constexpr uint8_t kMaxMessages = 32;
/** @brief Maximum size of each log message. */
constexpr uint8_t kMessageSize = 128;
/** @brief Structure for storing log messages. */
struct LogMessage {
    char text[kMessageSize];
};
/** @brief Queue for storing log messages. */
inline LogMessage queue[kMaxMessages];
/** @brief Head index for the log message queue. */
inline volatile uint8_t head = 0;
/** @brief Tail index for the log message queue. */
inline volatile uint8_t tail = 0;

/**
 * @brief Helper function to push a log message into the queue with a specified prefix and formatted message.
 * 
 * @param prefix The prefix to prepend to the log message (e.g., "[WIFI]", "[DNS]").
 * @param fmt The format string for the log message, similar to printf-style formatting.
 * @param args The variable argument list for the log message.
 * 
 * @return The status of the log push attempt.
 * @retval true if the log message was successfully added to the queue.
 * @retval false if the log message could not be added (e.g., queue is full).
 * 
 */
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

/**
 * @brief Helper function to pop a log message from the queue.
 *
 * @param msg The LogMessage structure to store the popped message.
 * 
 * @return The status of the log pop attempt.
 * @retval true if a log message was successfully popped from the queue.
 * @retval false if the queue is empty and no message was popped.
 * 
 */
inline bool popLog(LogMessage &msg) {
    if (tail == head) return false;

    msg = queue[tail];
    tail = (tail + 1) % kMaxMessages;
    return true;
}

/**
 * @brief Flush all log messages from the queue and print them.
 * 
 * @par Parameters
 * None.
 *
 * @return The status of the flush attempt.
 * @retval true if log messages were successfully flushed.
 * @retval false if the queue is empty and no messages were flushed.
 * 
 */
inline bool flushLogs() {
    if (head == tail) return false; // No messages to flush

    Serial.println("---------------------------------------------------");
    unsigned long timestamp = millis();
    char formatedTime[64];
    snprintf(formatedTime, sizeof(formatedTime), "Hours: %ld Minutes: %ld Seconds: %ld", (timestamp/1000/60/60)%24, (timestamp/1000/60)%60, (timestamp/1000)%60);
    Serial.println(formatedTime);
    
    LogMessage msg;
    while (popLog(msg)) Serial.println(msg.text);
    return true;
}

/**
 * @brief Macro to define a logger function for a specific log type (e.g., WiFi, DNS) with a given prefix and enabled flag.
 * 
 * @param name The name of the logger function to define (e.g., wifiLogging).
 * @param enabled The boolean flag that determines whether logging for this type is enabled.
 * @param prefix The prefix string to prepend to log messages of this type (e.g., "[WIFI]").
 * 
 * @return The status of the log attempt.
 * @retval true if the log message was successfully added to the queue.
 * @retval false if logging is disabled or the log message could not be added (e.g., queue is full).
 * 
 */
#define DEFINE_LOGGER(name, enabled, prefix)        \
    inline bool name(const char *fmt, ...) {        \
        if (!(enabled)) return false;               \
        va_list args;                               \
        va_start(args, fmt);                        \
        bool result = pushLog(prefix, fmt, args);   \
        va_end(args);                               \
        return result;                              \
    }

    DEFINE_LOGGER(wifiLogging,    debug_config::kEnableWiFiLogging,    debug_config::kWiFiPrefix)
    DEFINE_LOGGER(leaseLogging,   debug_config::kEnableLeaseLogging,   debug_config::kLeasePrefix)
    DEFINE_LOGGER(ledLogging,     debug_config::kEnableLEDLogging,     debug_config::kLEDPrefix)
    DEFINE_LOGGER(dnsLogging,     debug_config::kEnableDNSLogging,     debug_config::kDNSPrefix)
    DEFINE_LOGGER(webLogging,     debug_config::kEnableWebLogging,     debug_config::kWebPrefix)
    DEFINE_LOGGER(displayLogging, debug_config::kEnableDisplayLogging, debug_config::kDisplayPrefix)
#undef DEFINE_LOGGER
} // namespace debug_logs