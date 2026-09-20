#ifndef THERMISTOR_READER_HPP
#define THERMISTOR_READER_HPP

#include "phoenix_guard.hpp"
#include <stdint.h>

// ===================== Return Codes =============================================
#define THERMISTOR_READER_OK PHX_OK
#define THERMISTOR_READER_ERR_INVALID_ARG PHX_ERR_INVALID_ARG
#define THERMISTOR_READER_ERR_NOT_INITIALIZED PHX_ERR_NOT_INITIALIZED
#define THERMISTOR_READER_ERR_NOT_IMPLEMENTED PHX_ERR_NOT_IMPLEMENTED
#define THERMISTOR_READER_ERR_COMPUTE_FAILURE PHX_ERR_HARDWARE_FAILURE

/**
 * @brief Board-level configuration for the single sample thermistor.
 *
 * phrog measures the thermistor divider on the nRF52840 internal SAADC. The drive pin
 * energises the divider only during a measurement to limit self-heating.
 */
struct ThermistorReaderConfig {
  int      signal_pin;             /**< Arduino analog-capable pin wired to the divider midpoint. */
  int      drive_pin;              /**< GPIO that powers the divider; pass -1 when tied to the rail. */
  uint32_t series_resistance_ohms; /**< Fixed resistor in series with the thermistor. */
  float    r25_ohms;               /**< Nominal thermistor resistance at 25 C. */
  float    beta_constant;          /**< Beta constant for the Beta-parameter equation. */
  uint32_t settle_time_us;         /**< Delay after driving the divider before sampling. */
  float    calibration_offset_c;   /**< Offset added to the computed temperature in C. */
};

/** @brief Hook signature for configuring a GPIO pin mode. */
typedef void (*thermistor_reader_pin_mode_fn_t)(int pin, uint32_t mode);

/** @brief Hook signature for driving a GPIO pin high or low. */
typedef void (*thermistor_reader_digital_write_fn_t)(int pin, uint32_t value);

/** @brief Hook signature for microsecond-precision busy-wait delays. */
typedef void (*thermistor_reader_delay_us_fn_t)(uint32_t delay_us);

/** @brief Hook signature for reading the SAADC; returns the raw conversion code. */
typedef int32_t (*thermistor_reader_analog_read_fn_t)(int pin);

/**
 * @brief Cache configuration, install production hooks, and park the drive pin low.
 *
 * @param config Pointer to the board-level configuration.
 * @return THERMISTOR_READER_OK on success, THERMISTOR_READER_ERR_INVALID_ARG on NULL config.
 */
int thermistor_reader_initialize(const ThermistorReaderConfig* config);

/**
 * @brief Energise the divider, sample it, convert to temperature, and de-energise.
 *
 * @param temperature_c_out Destination for the temperature in C.
 * @return THERMISTOR_READER_OK on success or a negative error code.
 */
int thermistor_reader_measure_celsius(float* temperature_c_out);

/** @brief Reset internal state between Unity test cases. */
void thermistor_reader_reset_for_test(void);

/** @brief Retrieve the cached configuration for test inspection. */
void thermistor_reader_get_config_for_test(ThermistorReaderConfig* config_out);

/**
 * @brief Replace GPIO/ADC hooks with test-controlled stubs; NULL entries keep the production default.
 */
void thermistor_reader_set_hooks_for_test(thermistor_reader_pin_mode_fn_t      pin_mode,
                                          thermistor_reader_digital_write_fn_t digital_write,
                                          thermistor_reader_delay_us_fn_t      delay_us,
                                          thermistor_reader_analog_read_fn_t   analog_read);

/**
 * @brief Return the resistance computed during the most recent measurement.
 *
 * @param resistance_out Destination for the resistance in ohms.
 * @return THERMISTOR_READER_OK on success or a negative error code.
 */
int thermistor_reader_get_last_resistance_for_test(float* resistance_out);

#endif  // THERMISTOR_READER_HPP
