/**
 * @file led_handler.h
 * @brief Small helper API for the onboard status LED.
 */

#pragma once

#include <stdint.h>

enum class BlinkState : int8_t {
	Setup,
	WiFiFail,
	Idle,
};

/**
 * Configure the onboard LED pins and initialize the status controller.
 *
 * Sets up the hardware pins and prepares internal pattern runners. Call
 * this from `setup()` before calling `setStatusState()`.
 */
void startStatusLED();

/**
 * Change the active LED state.
 *
 * @param state The new `BlinkState` value to apply immediately.
 */
void setStatusState(BlinkState state);

/**
 * Advance the LED status controller by one tick if scheduled.
 *
 * Call this from the main loop (or from short blocking paths) so the
 * cooperative status thread can progress without blocking the system.
 */
void updateStatusLED();
