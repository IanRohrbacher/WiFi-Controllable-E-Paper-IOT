/**
 * @file led_handler.h
 * @brief 
 * 
 */

#pragma once

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
bool startStatusLED();

/**
 * Change the active LED state.
 *
 * @param state The new `BlinkState` value to apply immediately.
 */
bool setStatusState(BlinkState state);

/**
 * Advance the LED status controller by one tick if scheduled.
 *
 * Call this from the main loop (or from short blocking paths) so the
 * cooperative status thread can progress without blocking the system.
 */
bool updateStatusLED();
