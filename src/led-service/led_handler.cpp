/**
 * @file led_handler.cpp
 * 
 * @brief Implementation of the status LED handler for indicating device states for visual feedback.
 * 
 * @details
 * This module manages the status LED on the device to provide visual feedback
 * about the current state of the system. It defines different blinking
 * patterns for various pre defined states. The LED patterns are designed to
 * be easily distinguishable so that users can quickly understand the device's
 * status at a glance. The module includes functions to start the LED service,
 * change the LED state, and update the LED based on its current state. It
 * uses a cooperative threading model to manage the LED patterns without
 * blocking the main execution flow of the device.
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
 * Member variables/functions used internally by the led service.
 * These are not intended to be used outside of this module.
 * @{
 */
namespace {
/** @brief Timestamp for adding debug logs for the led loop */
unsigned long nowLoop = 0;

/** @brief The GPIO pin connected to the board's status LED */
constexpr uint8_t kLEDPinBoard = LED_BUILTIN_AUX;
/** @brief The GPIO pin connected to the ESP32's status LED */
constexpr uint8_t kLEDPinEsp = LED_BUILTIN;

/** @brief Structure representing a single step in the LED pattern */
struct LedStep {
    bool on;
    uint16_t duration;
    uint16_t power = 255; // 0-255, only used for the board LED which supports PWM brightness control
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

constexpr LedStep kIdlePattern[] = {
    {true, 1000},
    {false, 1000},
};
/** @} */ // end of LEDPatterns

/** @brief The current state of the status LED */
static BlinkState currentState = BlinkState::Idle;
/** @brief A counter for logging pattern updates */
static uint8_t loggingCounter = 0;

/** @brief Structure to hold the state of a LED pattern */
struct PatternState {
    unsigned long lastTime = 0;
    size_t index = 0;
};

/** @brief Structure to represent a view of a LED pattern */
struct PatternView {
    const LedStep* values = nullptr;
    size_t length = 0;
};

/** 
 * @brief Helper function to create a PatternView from a static array of LedSteps.
 * 
 * @tparam N The size of the input array.
 * 
 * @param ledPattern The static array of LedStep structures.
 * 
 * @return A PatternView representing the LED pattern.
 * 
 */
template <size_t N>
constexpr PatternView makePatternView(const LedStep (&ledPattern)[N]) {
    return PatternView{ledPattern, N};
}

/** @brief Structure to manage the execution of a LED pattern */
struct LedPatternRunner {
    PatternView boardPattern = {&kLedOff, 1}; // Default to an "off" pattern if not provided
    PatternView espPattern = {&kLedOff, 1}; // Default to an "off" pattern if not provided
    const char* logMessage = nullptr;
    uint8_t maxLogging = 0;
    PatternState boardState = {};
    PatternState espState = {};
    char* name = "Undefined";
    /**
     * @brief Helper function to reset the state of the LED pattern runner, allowing it to start from the beginning of the pattern sequence.
     * 
     * @param offsetMs An optional offset in milliseconds to apply to the lastTime timestamps, allowing for synchronization of pattern changes.
     * 
     * @return The status of the pattern state reset attempt.
     * @retval true The pattern state was reset successfully, allowing the LED pattern to start from the beginning of the sequence.
     * @retval false An error occurred while resetting the pattern state, which may affect the behavior of the LED pattern runner.
     * 
     */
    bool resetPatternState(unsigned long offsetMs = 0) {
        try {
            boardState.lastTime = millis() - offsetMs;
            boardState.index = 0;
            espState.lastTime = millis() - offsetMs;
            espState.index = 0;
        } catch (const std::exception& e) {
            debug_logs::ledLogging("Error resetting pattern state for %s: %s", name, e.what());
            return false;
        }
        return true;
    }
};

/**
 * @defgroup LedPatternRunner Instances
 * Instances for managing different LED patterns
 * @{
 */
static LedPatternRunner setupRunner = {};
static LedPatternRunner wifiFailRunner = {};
static LedPatternRunner idleRunner = {};
/** @} */ // end of LEDPatterns

/**
 * @brief Helper function to get the LED pattern runner corresponding to a given BlinkState.
 * 
 * @param state The BlinkState for which to retrieve the corresponding LedPatternRunner.
 * 
 * @return A pointer to the LedPatternRunner associated with the specified BlinkState, or nullptr if the state is invalid.
 *
 */
LedPatternRunner* getRunnerByState(BlinkState state) {
    switch (state) {
        case BlinkState::Setup:
            return &setupRunner;
        case BlinkState::WiFiFail:
            return &wifiFailRunner;
        case BlinkState::Idle:
            return &idleRunner;
        default:
            return nullptr;
    }
}

/** @brief Helper function to set the state of a LED pin.
 * 
 * @param ledPin The pin number of the LED to control.
 * @param step The LedStep structure defining the LED state.
 * 
 * @return The status of the LED state change attempt.
 * @retval true The LED state was set successfully according to the provided LedStep.
 * @retval false An error occurred while setting the LED state, which may affect the behavior of the LED.
 *
 */
bool setLed(uint8_t ledPin, LedStep step) {
    try{
        analogWrite(ledPin, step.on ? LOW : step.power);
        return true;
    } catch (const std::exception& e) {
        debug_logs::ledLogging("Error setting LED on pin %u: %s", ledPin, e.what());
        return false;
    }
}

/** @brief Helper function to log pattern updates.
 * 
 * @param logMessage The message to log.
 * @param maxLogging An optional maximum number of times to log the message, preventing excessive logging for frequently updated patterns. A value of 0 means no limit.
 * 
 * @return The status of the logging attempt.
 * @retval true The message was logged successfully.
 * @retval false The maximum logging limit was set and has been reached, preventing the message from being logged. Or a error occurred while logging the message, which may affect the logging behavior.
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
 * @brief Helper function to run the LED pattern logic for the current state, updating the LED states based on the defined patterns and timing.
 * 
 * @param runner The LedPatternRunner instance containing the patterns and state for the current BlinkState.
 * 
 * @return The status of the LED pattern update attempt.
 * @retval true The LED pattern was updated successfully based on the current state and timing.
 * @retval false An error occurred while updating the LED pattern, which may affect the behavior of the status LED.
 *
 */
bool threadPatternHelper( LedPatternRunner& runner ) {
    try {
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
    } catch (const std::exception& e) {
        debug_logs::ledLogging("Error in threadPatternHelper for %s: %s", runner.name, e.what());
        return false;
    }
    return true;
}

/** @brief Resets the patterns for all LED runners.
 * 
 * @param offsetMs The time offset in milliseconds to reset the patterns with.
 * 
 * @return The status of the pattern reset attempt.
 * @retval true The patterns were reset successfully for all runners.
 * @retval false An error occurred while resetting the patterns, which may affect the behavior of the LED runners.
 *
 */
bool resetPatterns(unsigned long offsetMs = 0) {
    try {
        setupRunner.resetPatternState(offsetMs);
        wifiFailRunner.resetPatternState(offsetMs);
        idleRunner.resetPatternState(offsetMs);
        return true;
    } catch (const std::exception& e) {
        debug_logs::ledLogging("Error in resetPatterns: %s", e.what());
        return false;
    }
}
/** @brief Resets the pattern for a specific LED runner.
 * 
 * @param state The blink state for which to reset the pattern.
 * @param offsetMs The time offset in milliseconds to reset the pattern with.
 * 
 * @return The status of the pattern reset attempt.
 * @retval true The pattern was reset successfully for the specified runner.
 * @retval false An error occurred while resetting the pattern, which may affect the behavior of the LED runner.
 *
 */
bool resetPatterns(BlinkState state, unsigned long offsetMs = 0) {
    try {
        getRunnerByState(state)->resetPatternState(offsetMs);
        debug_logs::ledLogging("Reset %s pattern state.", getRunnerByState(state)->name);
        return true;
    } catch (const std::exception& e) {
        debug_logs::ledLogging("Error in resetPatterns for state %s: %s", getRunnerByState(state)->name, e.what());
        return false;
    }
}

/**
 * @brief The main tick function for the status LED thread, which advances the LED patterns based on the current state.
 * 
 * @par Parameters
 * None.
 * 
 * @return The status of the LED tick attempt.
 * @retval true The LED patterns were advanced successfully based on the current state and timing.
 * @retval false An error occurred while advancing the LED patterns, which may affect the behavior of the status LED.
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

}  // namespace
/** @} */ // end of Private

/**
 * @defgroup Public
 * Public API for the led service, declared in led_handler.h.
 * @{
 */
bool startStatusLED() {
    if (!debug_config::kEnableStatusLight) {
        debug_logs::ledLogging("Status light is disabled, cannot start status LED service.");
        return false;
    }

    pinMode(kLEDPinEsp, OUTPUT);
    analogWrite(kLEDPinEsp, LOW);
    pinMode(kLEDPinBoard, OUTPUT);
    analogWrite(kLEDPinBoard, LOW);

    // Initialize pattern runners with their respective patterns and log messages
    setupRunner.espPattern = makePatternView(kSetupPattern);
    setupRunner.logMessage = "Device is starting up...";
    setupRunner.maxLogging = 1;
    setupRunner.name = "Setup";
    // ------------------------------------------------------------------
    wifiFailRunner.boardPattern = makePatternView(kWiFiFailBoardPattern);
    wifiFailRunner.espPattern = makePatternView(kWiFiFailEspPattern);
    wifiFailRunner.logMessage = "WiFi failed to start.";
    wifiFailRunner.maxLogging = 1;
    wifiFailRunner.name = "WiFiFail";
    // ------------------------------------------------------------------
    idleRunner.boardPattern = makePatternView(kIdlePattern);
    idleRunner.logMessage = "Device is idle.";
    idleRunner.maxLogging = 10;
    idleRunner.name = "Idle";
    // ------------------------------------------------------------------

    statusLedThread.setInterval(debug_config::kThreadRefreshIntervalMs);

    resetPatterns();

    debug_logs::ledLogging("Started status LED service.");
    return true;
}

bool setStatusState(BlinkState state) {
    if (!debug_config::kEnableStatusLight) {
        debug_logs::ledLogging("Status light is disabled, cannot set state to %s.", getRunnerByState(state)->name);
        return false;
    }

    try {
        currentState = state;
        loggingCounter = 0;
        resetPatterns(state);
    } catch (const std::exception& e) {
        debug_logs::ledLogging("Error in setStatusState: %s", e.what());
        return false;
    }

    return true;
}

bool updateStatusLED() {
    if (!debug_config::kEnableStatusLight) {
        debug_logs::ledLogging("Status light is disabled, skipping LED update.");
        return false;
    }

    try {
        if (statusLedThread.shouldRun()) statusLedThread.run();
        
        if (millis() - nowLoop >= debug_config::kLEDLoopDelay) {
            debug_logs::ledLogging("LED update loop processed.");
            nowLoop = millis();
        }
    } catch (const std::exception& e) {
        debug_logs::ledLogging("Error in updateStatusLED: %s", e.what());
        return false;
    }
    return true;
}
/** @} */ // end of Public
