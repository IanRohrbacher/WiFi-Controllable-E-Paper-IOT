/**
 * @file logger.h
 * @headerfile logger.h "src/logger.h"
 *
 * @brief Fixed-size, non-allocating queue of debug log messages.
 *
 */

#pragma once

#include "configs.h"
#include <stdarg.h>

namespace debug_logs {
/** @brief Maximum number of log messages that can be queued. */
constexpr uint8_t kMaxMessages = 32;
/** @brief Maximum size, in bytes, of each log message. */
constexpr uint8_t kMessageSize = 128;

/** @brief A single queued, null terminated log message. */
struct LogMessage {
    /** @brief Formatted, prefixed message text. */
    char text[kMessageSize];
};

/** @brief Ring buffer holding queued log messages. */
inline LogMessage queue[kMaxMessages];
/** @brief Index of the next slot @c pushLog() will write to. */
inline volatile uint8_t head = 0;
/** @brief Index of the next slot @c popLog() will read from. */
inline volatile uint8_t tail = 0;

/**
 * @brief Push a formatted, prefixed message into the log queue.
 *
 * @details
 * Formats @p fmt / @p args into a scratch buffer, prepends @p prefix, and
 * writes the result into the queue slot at @c head. The queue is a fixed-size
 * ring buffer with no dynamic allocation, so a full queue simply rejects the
 * new message rather than growing.
 *
 * @param prefix Text prepended to the message, such as "[WIFI]".
 * @param fmt Printf style format string for the message body.
 * @param args Variable argument list matching @p fmt.
 *
 * @return Whether the message was queued.
 * @retval true The message was queued.
 * @retval false The queue was full and the message was dropped or verbose logging is disabled.
 *
 */
inline bool pushLog(const char *prefix, const char *fmt, va_list args) {
    if (!debug_config::kEnableVerboseLogging) return false;
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
 * @brief Pop the oldest queued log message.
 *
 * @param msg Destination that receives the popped message.
 *
 * @return Whether a message was popped.
 * @retval true A message was popped into @p msg.
 * @retval false The queue was empty or verbose logging is disabled.
 *
 * @see pushLog()
 *
 */
inline bool popLog(LogMessage &msg) {
    if (!debug_config::kEnableVerboseLogging) return false;
    if (tail == head) return false;

    msg = queue[tail];
    tail = (tail + 1) % kMaxMessages;
    return true;
}

/**
 * @brief Print every queued log message to Serial, then clear the queue.
 *
 * @details
 * Prints @p separator, an uptime header, then pops and prints every message
 * currently in the queue in order. Intended to be called periodically from the
 * main loop rather than from inside @c pushLog(), so Serial output stays
 * batched. Skipped entirely when verbose logging is disabled, since Serial
 * itself is never started in that case.
 *
 * @param separator Line printed before the uptime header, once per
 * flush.
 *
 * @return Whether any messages were printed.
 * @retval true One or more messages were printed.
 * @retval false The queue was empty or verbose logging is disabled.
 *
 */
inline bool flushLogs(const char *separator = "---------------------------------------------------") {
    if (!debug_config::kEnableVerboseLogging) return false;
    if (head == tail) return false; // No messages to flush

    Serial.println(separator);
    unsigned long timestamp = millis();
    char formatedTime[64];
    snprintf(formatedTime, sizeof(formatedTime), "Hours: %ld Minutes: %ld Seconds: %ld", (timestamp/1000/60/60)%24, (timestamp/1000/60)%60, (timestamp/1000)%60);
    Serial.println(formatedTime);

    LogMessage msg;
    while (popLog(msg)) Serial.println(msg.text);
    return true;
}

/**
 * @brief Define a named logger function gated by an enabled flag.
 *
 * @details
 * Expands to an inline function @c name(fmt, ...) that forwards to @c
 * pushLog() with @p prefix, but only when @p enabled is true at compile time.
 * Used below to generate one differently prefixed logger per module without
 * repeating the va_list boilerplate for each one.
 *
 * @param name Identifier for the generated logger function.
 * @param enabled Compile time boolean gating whether this logger emits.
 * @param prefix Text prepended to every message from this logger.
 *
 * @return Whether the message was queued.
 * @retval true The message was queued.
 * @retval false enabled was false, the queue was full, or verbose logging is disabled overall.
 *
 */
#define DEFINE_LOGGER(name, enabled, prefix)        \
    inline bool name(const char *fmt, ...) {        \
        if (!(enabled)) return false;               \
        va_list args;                               \
        va_start(args, fmt);                        \
        bool result = pushLog(prefix, fmt, args);   \
        va_end(args);                                \
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
