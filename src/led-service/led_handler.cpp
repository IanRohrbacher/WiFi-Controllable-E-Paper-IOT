/**
 * @file led_handler.cpp
 * @brief LED handler for managing onboard LED patterns.
 */

#include <Arduino.h>
#include <thread.h>
#include <cstddef>
#include <utility>

#include "led_handler.h"
#include "configs.h"
#include "logger.h"

namespace {
unsigned long nowLoop = millis();

constexpr uint8_t kLedPinEsp = LED_BUILTIN;
constexpr uint8_t kLedPinBoard = LED_BUILTIN_AUX;
struct LedStep {
    bool on;
    uint16_t duration;
    uint16_t power = 255; // 0-255, only used for the board LED which supports PWM brightness control
};

static BlinkState currentState = BlinkState::Idle;
static uint8_t loggingCounter = 0;

struct PatternState {
    unsigned long lastTime = 0;
    size_t index = 0;
};

struct PatternView {
    const LedStep* values = nullptr;
    size_t length = 0;
};

struct LedPatternRunner {
    PatternView pattern;
    uint8_t ledPin;
    const char* logMessage = nullptr;
    uint8_t maxLogging = 0;
    PatternState state = {};
};

struct DualLedPatternRunner {
    PatternView firstPattern;
    uint8_t firstLedPin;
    PatternView secondPattern;
    uint8_t secondLedPin;
    const char* logMessage = nullptr;
    uint8_t maxLogging = 0;
    PatternState state = {};
};

template <size_t N>
constexpr PatternView makePatternView(const LedStep (&ledPattern)[N]) {
    return PatternView{ledPattern, N};
}

static LedPatternRunner setupRunner;
static DualLedPatternRunner wifiFailRunner;
static LedPatternRunner idleRunner;

void setLed(uint8_t ledPin, LedStep step) {
    analogWrite(ledPin, step.on ? LOW : step.power);
}

// maxLogging of 0 means unlimited logging, otherwise it limits how many times the log message will be printed for a given pattern
void threadPatternLogHelper(const char* logMessage, uint8_t maxLogging) {
    if (logMessage != nullptr && (maxLogging == 0 || loggingCounter < maxLogging)) {
        debug_logs::ledLogging(logMessage);
        loggingCounter++;
    }
}

void threadPatternHelper(
    LedPatternRunner& runner
) {
    unsigned long now = millis();

    if (runner.pattern.values == nullptr || runner.pattern.length == 0) {
        return;
    }

    if (now - runner.state.lastTime >= runner.pattern.values[runner.state.index].duration) {
        runner.state.lastTime = now;
        setLed(runner.ledPin, runner.pattern.values[runner.state.index]);
        runner.state.index = (runner.state.index + 1) % runner.pattern.length;
    }
    threadPatternLogHelper(runner.logMessage, runner.maxLogging);
}

void threadPatternHelper(
    DualLedPatternRunner& runner
) {
    unsigned long now = millis();

    if (runner.firstPattern.values == nullptr || runner.firstPattern.length == 0) return;
    if (runner.secondPattern.values == nullptr || runner.secondPattern.length == 0) return;

    if (now - runner.state.lastTime >= runner.firstPattern.values[runner.state.index].duration) {
        runner.state.lastTime = now;
        setLed(runner.firstLedPin, runner.firstPattern.values[runner.state.index]);
        runner.state.index = (runner.state.index + 1) % runner.firstPattern.length;
    }

    if (now - runner.state.lastTime >= runner.secondPattern.values[runner.state.index].duration) {
        runner.state.lastTime = now;
        setLed(runner.secondLedPin, runner.secondPattern.values[runner.state.index]);
        runner.state.index = (runner.state.index + 1) % runner.secondPattern.length;
    }
    threadPatternLogHelper(runner.logMessage, runner.maxLogging);
}

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

void resetPatternState(PatternState& state, unsigned long offsetMs = 0) {
    state.lastTime = millis() - offsetMs;
    state.index = 0;
}

void statusLedTick() {
    switch (currentState) {
        case BlinkState::Setup:
            threadPatternHelper(setupRunner);
            break;
        case BlinkState::WiFiFail:
            threadPatternHelper(wifiFailRunner);
            break;
        case BlinkState::Idle:
            threadPatternHelper(idleRunner);
            break;
    }
}

Thread statusLedThread = Thread([]() {
    statusLedTick();
});

}  // namespace

// -----------------------------------------------------------------------------
// Public API (declared in led_handler.h)
// -----------------------------------------------------------------------------

bool startStatusLED() {
    if (!debug_config::kEnableStatusLight) return false;

    pinMode(kLedPinEsp, OUTPUT);
    analogWrite(kLedPinEsp, LOW);
    pinMode(kLedPinBoard, OUTPUT);
    analogWrite(kLedPinBoard, LOW);

    setupRunner = {
        makePatternView(kSetupPattern), 
        kLedPinBoard, 
        "Device is starting up...",
        1,
    };
    wifiFailRunner = {
        makePatternView(kWiFiFailEspPattern),
        kLedPinEsp,
        makePatternView(kWiFiFailBoardPattern),
        kLedPinBoard,
        "WiFi failed to start.",
        1,
    };
    idleRunner = {
        makePatternView(kIdlePattern), 
        kLedPinBoard, 
        "Device is idle.",
        10,
    };

    statusLedThread.setInterval(debug_config::kThreadRefreshIntervalMs);

    resetPatternState(setupRunner.state);
    resetPatternState(wifiFailRunner.state);
    resetPatternState(idleRunner.state);

    debug_logs::ledLogging("Started status LED service.");
    return true;
}

bool setStatusState(BlinkState state) {
    if (!debug_config::kEnableStatusLight) return false;

    currentState = state;
    loggingCounter = 0;
    analogWrite(kLedPinEsp, LOW);
    analogWrite(kLedPinBoard, LOW);

    switch (currentState) {
        case BlinkState::Setup:
            resetPatternState(setupRunner.state);
            debug_logs::ledLogging("Reset setup pattern state.");
            break;
        case BlinkState::WiFiFail:
            resetPatternState(wifiFailRunner.state);
            debug_logs::ledLogging("Reset WiFi fail pattern state.");
            break;
        case BlinkState::Idle:
            resetPatternState(idleRunner.state);
            debug_logs::ledLogging("Reset idle pattern state.");
            break;
    }

    return true;
}

bool updateStatusLED() {
    if (!debug_config::kEnableStatusLight) return false;

    if (statusLedThread.shouldRun()) statusLedThread.run();

    if (millis() - nowLoop >= debug_config::kLEDLoopDelay) {
        debug_logs::ledLogging("LED update loop processed.");
        nowLoop = millis();
    }
    return true;
}