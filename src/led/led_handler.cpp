/**
 * @file led_handler.cpp
 * 
 * @brief Implementation of the status LED handler.
 *
 * @details
 * Each @c BlinkState maps to an @c LedPatternRunner holding independent on/off
 * patterns for the board LED and the ESP LED, advanced by a cooperative @c
 * Thread ticking every @c debug_config::kThreadRefreshIntervalMs.
 *
 */

#include <Arduino.h>
#include <thread.h>
#include <cstddef>
#include <utility>

#include "led_handler.h"
#include "configs.h"
#include "logger.h"

/**
 * @defgroup Private
 * Member variables/functions used internally by the led module.
 * These are not intended to be used outside of this module.
 * @{
 */
namespace {
/** @brief Timestamp for adding debug logs for the led loop */
unsigned long nowLoop = 0;

/** @brief The GPIO pin connected to the board's status LED */
constexpr uint8_t kLEDPinBoard = LED_BUILTIN_AUX;
/** @brief The GPIO pin connected to the ESP8266's status LED */
constexpr uint8_t kLEDPinEsp = LED_BUILTIN;

/** @brief A single step in an LED pattern. */
struct LedStep {
    /** @brief Whether the LED is on for this step. */
    bool on;
    /** @brief How long, in milliseconds, this step lasts. */
    uint16_t duration;
    /** @brief PWM brightness from 0 to 255, only used by the board LED. */
    uint16_t power = 255;
};

/**
 * @defgroup LEDPatterns
 * Predefined LED patterns for different device states.
 * @{
 */
constexpr LedStep kLedOn = {true, 0};
constexpr LedStep kLedOff = {false, 0};

constexpr LedStep kSetupPattern[] = {
    {true, 250},
    {false, 250},
};

constexpr LedStep kWiFiFailEspPattern[] = {
    {true, 150},
    {false, 500},
    {true, 200},
    {false, 200},
};
constexpr LedStep kWiFiFailBoardPattern[] = {
    {false, 150},
    {true, 500},
    {false, 200},
    {true, 200},
};

constexpr LedStep kDNSFailPattern[] = {
    {false, 300},
    {true, 300},
    {false, 100},
    {true, 100},
};

constexpr LedStep kEInkFailPattern[] = {
    {true, 100},
    {false, 100},
    {true, 100},
    {false, 100},
    {true, 500},
    {false, 100},
};

constexpr LedStep kIdlePattern[] = {
    {true, 1000},
    {false, 1000},
};
/** @} */ // end of LEDPatterns

/** @brief The current state of the status LED */
static BlinkState currentState = BlinkState::Idle;
/** @brief A counter for logging pattern updates */
static uint8_t loggingCounter = 0;

/** @brief Playback position within a single LED pattern. */
struct PatternState {
    /** @brief millis() timestamp the current step started at. */
    unsigned long lastTime = 0;
    /** @brief Index of the step currently playing. */
    size_t index = 0;
};

/** @brief Non-owning view over a static array of @c LedStep. */
struct PatternView {
    /** @brief Pointer to the first step of the pattern. */
    const LedStep* values = nullptr;
    /** @brief Number of steps in the pattern. */
    size_t length = 0;
};

/**
 * @brief Wrap a static LedStep array in a PatternView.
 *
 * @tparam N Length of the input array, deduced by the compiler.
 *
 * @param ledPattern Static array of @c LedStep to view.
 *
 * @return A @c PatternView over @p ledPattern.
 *
 */
template <size_t N>
constexpr PatternView makePatternView(const LedStep (&ledPattern)[N]) {
    return PatternView{ledPattern, N};
}

/** @brief Pairs a board LED pattern and an ESP LED pattern for one BlinkState. */
struct LedPatternRunner {
    /** @brief Pattern played on the board LED, defaults to steady off. */
    PatternView boardPattern = {&kLedOff, 1};
    /** @brief Pattern played on the ESP LED, defaults to steady off. */
    PatternView espPattern = {&kLedOff, 1};
    /** @brief Message logged when this runner becomes active, or nullptr. */
    const char* logMessage = nullptr;
    /** @brief Maximum times @c logMessage may be logged, 0 means no limit. */
    uint8_t maxLogging = 0;
    /** @brief Playback position within @c boardPattern. */
    PatternState boardState = {};
    /** @brief Playback position within @c espPattern. */
    PatternState espState = {};
    /** @brief Human readable name used in log messages. */
    const char* name = "Undefined";

    /**
     * @brief Restart both patterns from their first step.
     *
     * @param offsetMs Offset, in milliseconds, applied to both patterns'
     * start timestamps so a caller can stagger multiple runners.
     *
     * @par Returns
     * Nothing.
     */
    void resetPatternState(unsigned long offsetMs = 0) {
        boardState.lastTime = millis() - offsetMs;
        boardState.index = 0;
        espState.lastTime = millis() - offsetMs;
        espState.index = 0;
    }
};

/**
 * @defgroup LedPatternRunner Instances
 * Instances for managing different LED patterns
 * @{
 */
/** @brief Runner for BlinkState::Setup. */
static LedPatternRunner setupRunner = {};
/** @brief Runner for BlinkState::WiFiFail. */
static LedPatternRunner wifiFailRunner = {};
/** @brief Runner for BlinkState::DNSFail. */
static LedPatternRunner dnsFailRunner = {};
/** @brief Runner for BlinkState::EInkFail. */
static LedPatternRunner eInkFailRunner = {};
/** @brief Runner for BlinkState::Idle. */
static LedPatternRunner idleRunner = {};
/** @} */ // end of LEDPatterns

/**
 * @brief Look up the LedPatternRunner for a given BlinkState.
 *
 * @param state State to look up.
 *
 * @return Pointer to the matching @c LedPatternRunner.
 * @retval nullptr No runner is defined for the given state.
 *
 */
LedPatternRunner* getRunnerByState(BlinkState state) {
    switch (state) {
        case BlinkState::Setup:
            return &setupRunner;
        case BlinkState::WiFiFail:
            return &wifiFailRunner;
        case BlinkState::DNSFail:
            return &dnsFailRunner;
        case BlinkState::EInkFail:
            return &eInkFailRunner;
        case BlinkState::Idle:
            return &idleRunner;
        default:
            debug_logs::ledLogging("No LED pattern runner defined for the given state.");
            return nullptr;
    }
}

/**
 * @brief Drive a single LED pin to the state described by one LedStep.
 *
 * @param ledPin Pin number of the LED to control.
 * @param step Step describing the desired on/off and brightness.
 *
 * @par Returns
 * Nothing.
 *
 */
void setLed(uint8_t ledPin, LedStep step) {
    analogWrite(ledPin, step.on ? LOW : step.power);
}

/**
 * @brief Log a pattern's message, honoring its optional logging cap.
 *
 * @param logMessage Message to log, or nullptr to log nothing.
 * @param maxLogging Maximum times to log this message, 0 means no limit.
 *
 * @return Whether the message was logged.
 * @retval true The message was logged and @c loggingCounter was incremented.
 * @retval false @p logMessage was nullptr, or @p maxLogging was already reached.
 *
 */
bool threadPatternLogHelper(const char* logMessage, uint8_t maxLogging = 0) {
    if (logMessage != nullptr && (maxLogging == 0 || loggingCounter < maxLogging)) {
        debug_logs::ledLogging(logMessage);
        loggingCounter++;
        return true;
    }
    return false;
}

/**
 * @brief Advance a runner's board and ESP patterns by one tick each.
 *
 * @details
 * Checks each pattern's current step against its elapsed time, and when a step
 * has run its full duration, drives the corresponding pin to the next step.
 * The board and ESP patterns advance independently of each other.
 *
 * @param runner Runner whose patterns and timing state should advance.
 *
 * @return Whether the runner had valid patterns to advance.
 * @retval true Both patterns were checked and advanced as needed.
 * @retval false @p runner's board or ESP pattern is empty or unset.
 *
 */
bool threadPatternHelper(LedPatternRunner& runner) {
    unsigned long now = millis();

    if (runner.boardPattern.values == nullptr || runner.boardPattern.length == 0) return false;
    if (runner.espPattern.values == nullptr || runner.espPattern.length == 0) return false;

    if (now - runner.boardState.lastTime >= runner.boardPattern.values[runner.boardState.index].duration) {
        runner.boardState.lastTime = now;
        setLed(kLEDPinBoard, runner.boardPattern.values[runner.boardState.index]);
        runner.boardState.index = (runner.boardState.index + 1) % runner.boardPattern.length;
    }

    if (now - runner.espState.lastTime >= runner.espPattern.values[runner.espState.index].duration) {
        runner.espState.lastTime = now;
        setLed(kLEDPinEsp, runner.espPattern.values[runner.espState.index]);
        runner.espState.index = (runner.espState.index + 1) % runner.espPattern.length;
    }
    threadPatternLogHelper(runner.logMessage, runner.maxLogging);

    return true;
}

/**
 * @brief Reset every LED runner's pattern state.
 *
 * @param offsetMs Offset, in milliseconds, forwarded to each runner's @c
 * resetPatternState().
 *
 * @par Returns
 * Nothing.
 */
void resetPatterns(unsigned long offsetMs = 0) {
    setupRunner.resetPatternState(offsetMs);
    wifiFailRunner.resetPatternState(offsetMs);
    dnsFailRunner.resetPatternState(offsetMs);
    eInkFailRunner.resetPatternState(offsetMs);
    idleRunner.resetPatternState(offsetMs);
}

/**
 * @brief Reset a single BlinkState's runner and log the change.
 *
 * @param state Blink state whose runner should be reset.
 * @param offsetMs Offset, in milliseconds, forwarded to the runner's
 * @c resetPatternState().
 *
 * @par Returns
 * Nothing.
 *
 */
void resetPatterns(BlinkState state, unsigned long offsetMs = 0) {
    getRunnerByState(state)->resetPatternState(offsetMs);
    debug_logs::ledLogging("Reset %s pattern state.", getRunnerByState(state)->name);
}

/**
 * @brief Thread callback that advances the current state's LED patterns.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void statusLedTick() {
    if (!threadPatternHelper(*getRunnerByState(currentState))) {
        debug_logs::ledLogging("Error occurred while updating LED pattern for state %s.", getRunnerByState(currentState)->name);
    }
}

/** @brief The thread for managing the status LED updates. */
Thread statusLedThread = Thread([]() {
    statusLedTick();
});

} // namespace
/** @} */ // end of Private

/**
 * @defgroup Public
 * Public API for the led module, declared in led_handler.h.
 * @{
 */
bool startStatusLED() {
    if (!debug_config::kEnableStatusLight) {
        debug_logs::ledLogging("Status light is disabled, cannot start status LED module.");
        return false;
    }

    pinMode(kLEDPinEsp, OUTPUT);
    analogWrite(kLEDPinEsp, LOW);
    pinMode(kLEDPinBoard, OUTPUT);
    analogWrite(kLEDPinBoard, LOW);

    // Initialize pattern runners with their respective patterns and log messages.
    setupRunner.espPattern = makePatternView(kSetupPattern);
    setupRunner.logMessage = "Device is starting up...";
    setupRunner.maxLogging = 1;
    setupRunner.name = "Setup";

    wifiFailRunner.boardPattern = makePatternView(kWiFiFailBoardPattern);
    wifiFailRunner.espPattern = makePatternView(kWiFiFailEspPattern);
    wifiFailRunner.logMessage = "WiFi failed to start.";
    wifiFailRunner.maxLogging = 1;
    wifiFailRunner.name = "WiFiFail";

    dnsFailRunner.boardPattern = makePatternView(kDNSFailPattern);
    dnsFailRunner.logMessage = "DNS failed to start.";
    dnsFailRunner.maxLogging = 1;
    dnsFailRunner.name = "DNSFail";

    eInkFailRunner.boardPattern = makePatternView(kEInkFailPattern);
    eInkFailRunner.logMessage = "E-Ink display failed to initialize.";
    eInkFailRunner.maxLogging = 1;
    eInkFailRunner.name = "EInkFail";

    idleRunner.boardPattern = makePatternView(kIdlePattern);
    idleRunner.logMessage = "Device is idle.";
    idleRunner.maxLogging = 10;
    idleRunner.name = "Idle";

    statusLedThread.setInterval(debug_config::kThreadRefreshIntervalMs);

    resetPatterns();

    debug_logs::ledLogging("Started status LED module.");
    return true;
}

bool setStatusState(BlinkState state) {
    if (!debug_config::kEnableStatusLight) {
        debug_logs::ledLogging("Status light is disabled, cannot set state to %s.", getRunnerByState(state)->name);
        return false;
    }

    currentState = state;
    loggingCounter = 0;
    resetPatterns(state);

    return true;
}

bool updateStatusLED() {
    if (!debug_config::kEnableStatusLight) {
        debug_logs::ledLogging("Status light is disabled, skipping LED update.");
        return false;
    }

    if (statusLedThread.shouldRun()) statusLedThread.run();
    
    if (millis() - nowLoop >= debug_config::kLEDLoopDelay) {
        debug_logs::ledLogging("LED update loop processed.");
        nowLoop = millis();
    }
    return true;
}

bool inFailedState() {
    return
    currentState == BlinkState::WiFiFail ||
    currentState == BlinkState::DNSFail ||
    currentState == BlinkState::EInkFail;
}
/** @} */ // end of Public
