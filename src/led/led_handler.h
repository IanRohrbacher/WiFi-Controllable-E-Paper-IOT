/**
 * @headerfile led_handler.h "src/led/led_handler.h"
 * 
 */

#pragma once

/** @brief Enumerates the possible states for the status LED. */
enum class BlinkState : int8_t {
	Setup,
	WiFiFail,
	DNSFail,
	EInkFail,
	Idle,
};

/**
 * @brief Start the status LED module and initialize the LED patterns.
 *
 * @details
 * This function initializes the status LED module, setting up the necessary
 * GPIO pins and configuring the LED patterns for different device states. It
 * should be called during the setup phase of the application to ensure that
 * the status LED is ready to indicate the device's state.
 *
 * @par Parameters
 * None.
 *
 * @return The status of the LED module startup attempt.
 * @retval true The LED module was started successfully and the LEDs are initialized.
 * @retval false The LED module failed to start, possibly due to a configuration issue or hardware problem.
 * 
 */
bool startStatusLED();

/**
 * @brief Set the current state of the status LED to control its blinking pattern.
 * 
 * @details
 * This function changes the current state of the status LED, which determines
 * the blinking pattern that will be displayed. It also resets the pattern
 * state for the new state to ensure the pattern starts from the beginning when
 * the state changes.
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
 * This function should be called regularly in the main loop to ensure the LED
 * patterns are updated according to the current state. It checks if the LED
 * module is running and processes the LED patterns, updating the LED states
 * as needed.
 *
 * @param state The current state of the status LED, which determines the blinking pattern to display.
 *
 * @return The status of the LED update attempt.
 * @retval true The LED was updated successfully based on its current state.
 * @retval false The LED module is disabled and is not running.
 * 
 */
bool updateStatusLED();

/**
 * @brief Check if the status LED is in a failed state.
 * 
 * @details
 * This function checks if the status LED is currently indicating a failed
 * state, which could be due to any critical modules failing to start. It can
 * be used to determine if the device is in an error condition that requires
 * attention.
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
