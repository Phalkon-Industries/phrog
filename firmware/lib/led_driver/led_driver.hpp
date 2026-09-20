#ifndef LED_DRIVER_HPP
#define LED_DRIVER_HPP

#include "phoenix_guard.hpp"
#include <stdint.h>

// Return codes for the LED driver helper.
#define LED_DRIVER_OK PHX_OK
#define LED_DRIVER_ERR_INVALID_ARG PHX_ERR_INVALID_ARG
#define LED_DRIVER_ERR_NOT_INITIALIZED PHX_ERR_NOT_INITIALIZED
#define LED_DRIVER_ERR_NOT_IMPLEMENTED PHX_ERR_NOT_IMPLEMENTED
#define LED_DRIVER_ERR_INVALID_STATE (PHX_ERR_MODULE_BASE - 10)

/**
 * @brief Logical LED selection. phrog drives each LED from its own GPIO, so at most one is lit.
 */
enum class LedDriverChannel : uint8_t {
  LED_DRIVER_CHANNEL_OFF = 0u,
  LED_DRIVER_CHANNEL_GREEN,
  LED_DRIVER_CHANNEL_BLUE,
};

/**
 * @brief GPIO assignments for the LED drive transistors.
 */
struct LedDriverConfig {
  int green_drive_pin; /**< GPIO driving the green LED (active high). */
  int blue_drive_pin;  /**< GPIO driving the blue LED (active high). */
};

/**
 * @brief Configure the drive pins as outputs parked low and record the configuration.
 *
 * @param config Pointer to the pin assignments.
 * @return LED_DRIVER_OK on success, LED_DRIVER_ERR_INVALID_ARG on NULL config.
 */
int led_driver_initialize(const LedDriverConfig* config);

/**
 * @brief Light the requested LED and extinguish the other.
 *
 * @param channel LED to drive, or LED_DRIVER_CHANNEL_OFF to turn both off.
 * @return LED_DRIVER_OK on success or a negative error code.
 */
int led_driver_set_channel(LedDriverChannel channel);

/**
 * @brief Report the last channel commanded via led_driver_set_channel.
 *
 * @param channel_out Destination for the recorded channel.
 * @return LED_DRIVER_OK on success or a negative error code.
 */
int led_driver_get_channel(LedDriverChannel* channel_out);

/**
 * @brief Turn both LEDs off and release state tracking.
 *
 * @return LED_DRIVER_OK on success, LED_DRIVER_ERR_NOT_INITIALIZED if never initialised.
 */
int led_driver_shutdown(void);

/** @brief Reset internal state between Unity test cases. */
void led_driver_reset_for_test(void);

#endif  // LED_DRIVER_HPP
