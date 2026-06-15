/**
 * @file led_handler.cpp
 * 
 * @brief 
 * 
 */

#include <Arduino.h>
#include <thread.h>
#include <cstddef>
#include <utility>

#include "led_handler.h"
#include "configs.h"
#include "logger.h"

namespace {
unsigned long nowLoop = 0;

constexpr uint8_t kLEDPinBoard = LED_BUILTIN_AUX;
constexpr uint8_t kLEDPinEsp = LED_BUILTIN;
struct LedStep {
    bool on;
    uint16_t duration;
    uint16_t power = 255; // 0-255, only used for the board LED which supports PWM brightness control
};
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
    PatternView boardPattern = {&kLedOff, 1}; // Default to an "off" pattern if not provided
    PatternView espPattern = {&kLedOff, 1}; // Default to an "off" pattern if not provided
    const char* logMessage = nullptr;
    uint8_t maxLogging = 0;
    PatternState boardState = {};
    PatternState espState = {};
    void resetPatternState(unsigned long offsetMs = 0) {
        boardState.lastTime = millis() - offsetMs;
        boardState.index = 0;
        espState.lastTime = millis() - offsetMs;
        espState.index = 0;
    }
};

template <size_t N>
constexpr PatternView makePatternView(const LedStep (&ledPattern)[N]) {
    return PatternView{ledPattern, N};
}

static LedPatternRunner setupRunner = {};
static LedPatternRunner wifiFailRunner = {};
static LedPatternRunner idleRunner = {};

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

void threadPatternHelper( LedPatternRunner& runner ) {
    unsigned long now = millis();

    if (runner.boardPattern.values == nullptr || runner.boardPattern.length == 0) return;
    if (runner.espPattern.values == nullptr || runner.espPattern.length == 0) return;

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

    pinMode(kLEDPinEsp, OUTPUT);
    analogWrite(kLEDPinEsp, LOW);
    pinMode(kLEDPinBoard, OUTPUT);
    analogWrite(kLEDPinBoard, LOW);

    setupRunner.espPattern = makePatternView(kSetupPattern);
    setupRunner.logMessage = "Device is starting up...";
    setupRunner.maxLogging = 1;

    wifiFailRunner.boardPattern = makePatternView(kWiFiFailBoardPattern);
    wifiFailRunner.espPattern = makePatternView(kWiFiFailEspPattern);
    wifiFailRunner.logMessage = "WiFi failed to start.";
    wifiFailRunner.maxLogging = 1;

    idleRunner.boardPattern = makePatternView(kIdlePattern);
    idleRunner.logMessage = "Device is idle.";
    idleRunner.maxLogging = 10;

    statusLedThread.setInterval(debug_config::kThreadRefreshIntervalMs);

    setupRunner.resetPatternState();
    wifiFailRunner.resetPatternState();
    idleRunner.resetPatternState();

    debug_logs::ledLogging("Started status LED service.");
    return true;
}

bool setStatusState(BlinkState state) {
    if (!debug_config::kEnableStatusLight) return false;

    currentState = state;
    loggingCounter = 0;
    analogWrite(kLEDPinEsp, LOW);
    analogWrite(kLEDPinBoard, LOW);

    switch (currentState) {
        case BlinkState::Setup:
            setupRunner.resetPatternState();
            debug_logs::ledLogging("Reset setup pattern state.");
            break;
        case BlinkState::WiFiFail:
            wifiFailRunner.resetPatternState();
            debug_logs::ledLogging("Reset WiFi fail pattern state.");
            break;
        case BlinkState::Idle:
            idleRunner.resetPatternState();
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