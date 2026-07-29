/**
 * @file clear_button.cpp
 *
 * @brief Implementation of the clear button IO.
 *
 */

#include <Arduino.h>

#include "clear_button.h"
#include "../configs.h"
#include "../display/display.h"
#include "../logger.h"

void Clear_button::begin() {
    pinMode(io_config::kClearButtonPin, INPUT_PULLUP);
    seedIoState(state, digitalRead(io_config::kClearButtonPin) == HIGH);
}

bool Clear_button::isReady() {
    updateIoState(state, digitalRead(io_config::kClearButtonPin) == HIGH);
    return onRisingEdge(state);
}

void Clear_button::action() {
    if(isDisplayQueueEmpty()) {
        debug_logs::ioLogging("Clearing display");
        clearDisplay(DisplayColor::White);
    }
    setNextUpdateCooldownMs(0);
}
