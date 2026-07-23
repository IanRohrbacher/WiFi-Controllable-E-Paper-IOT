/**
 * @headerfile led_handler.h "src/led/led_handler.h"
 * 
 */

#pragma once

/** @brief Enumerates the possible states for the status LED. */
enum class BlinkState : int8_t {
	/** @brief Device is starting up, before any module has succeeded or failed. */
	Setup,
	/** @brief The WiFi access point failed to start. */
	WiFiFail,
	/** @brief The captive DNS server failed to start. */
	DNSFail,
	/** @brief The e-paper display failed to initialize. */
	EInkFail,
	/** @brief Every module started successfully, device is running normally. */
	Idle,
};

/**
 * @brief Start the status LED module and initialize the LED patterns.
 *
 * @details
 * Configures the LED GPIO pins and registers each @c BlinkState's pattern
 * runner. Call once during @c setup(), before @c setStatusState().
 *
 * @par Parameters
 * None.
 *
 * @return The status of the LED module startup attempt.
 * @retval true The LEDs were initialized.
 * @retval false @c debug_config::kEnableStatusLight is false.
 *
 */
bool startStatusLED();

/**
 * @brief Set the current state of the status LED to control its blinking pattern.
 *
 * @details
 * Resets the new state's pattern so it starts from its first step rather than
 * wherever the previous state's pattern was left off.
 *
 * @param state The desired state for the status LED.
 *
 * @return The status of the state change attempt.
 * @retval true The state was changed successfully.
 * @retval false The LED module is disabled and is not running.
 *
 */
bool setStatusState(BlinkState state);

/**
 * @brief Update the status LED based on its current state.
 *
 * @details
 * Call regularly from the main loop. Advances the current state's LED patterns
 * only once per @c debug_config::kThreadRefreshIntervalMs, via the module's
 * cooperative thread.
 *
 * @par Parameters
 * None.
 *
 * @return The status of the LED update attempt.
 * @retval true The LED was updated based on its current state.
 * @retval false The LED module is disabled and is not running.
 *
 */
bool updateStatusLED();

/**
 * @brief Check if the status LED is in a failed state.
 *
 * @par Parameters
 * None.
 *
 * @return The status of the failed state check.
 * @retval true The LED is in a failed state.
 * @retval false The LED is not in a failed state.
 *
 */
bool inFailedState();
