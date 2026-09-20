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

// ===================== SAADC Setup ==============================================
// The divider is driven from a GPIO at VDD and read against the VDD/4 reference with 1/4 gain, so
// the code is the divider ratio itself and rail drift cancels (the same ratiometric idea Phoenix
// uses with its reference divider, without needing one).
#define THERMISTOR_READER_ADC_RESOLUTION_BITS 14
#define THERMISTOR_READER_ADC_FULL_SCALE_CODE ((1 << THERMISTOR_READER_ADC_RESOLUTION_BITS) - 1)
#define THERMISTOR_READER_SAMPLE_COUNT 8u  // Averaged per measurement to knock down SAADC noise.

/**
 * @brief Board-level configuration for the single sample thermistor.
 *
 * Topology: drive pin -> series resistor -> signal pin -> thermistor -> GND. The drive pin
 * energises the divider only during a measurement to limit self-heating. Temperature is computed
 * with the segmented Steinhart-Hart table for the Phoenix sample thermistor, which phrog shares.
 */
struct ThermistorReaderConfig {
  int      signal_pin;             /**< Arduino analog-capable pin wired to the divider midpoint. */
  int      drive_pin;              /**< GPIO that powers the divider; pass -1 when tied to the rail. */
  uint32_t series_resistance_ohms; /**< Fixed resistor between the drive pin and the signal pin. */
  float    r25_ohms;               /**< Nominal thermistor resistance at 25 C. */
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
 * @return THERMISTOR_READER_OK on success, THERMISTOR_READER_ERR_INVALID_ARG on NULL config, a
 *         negative signal pin, a zero series resistance, or a non-positive r25.
 */
int thermistor_reader_initialize(const ThermistorReaderConfig* config);

/**
 * @brief Energise the divider, sample it, convert to temperature, and de-energise.
 *
 * The drive pin is returned low even when the conversion fails.
 *
 * @param temperature_c_out Destination for the temperature in C.
 * @return THERMISTOR_READER_OK on success, THERMISTOR_READER_ERR_COMPUTE_FAILURE when the divider
 *         reads open or shorted or the resistance falls outside the model, or a negative error code.
 */
int thermistor_reader_measure_celsius(float* temperature_c_out);

/**
 * @brief Convert an averaged SAADC code into the thermistor resistance.
 *
 * With the ratiometric setup the code is V_signal / V_drive, so
 * R_thermistor = R_series * code / (full_scale - code).
 *
 * @param code Averaged SAADC code, 0..THERMISTOR_READER_ADC_FULL_SCALE_CODE.
 * @param series_resistance_ohms Fixed resistor between drive and signal.
 * @param resistance_out Destination for the thermistor resistance in ohms.
 * @return THERMISTOR_READER_OK, THERMISTOR_READER_ERR_INVALID_ARG on NULL output, or
 *         THERMISTOR_READER_ERR_COMPUTE_FAILURE when the code is at either rail (open or short).
 */
int thermistor_reader_code_to_resistance(int32_t code, uint32_t series_resistance_ohms, float* resistance_out);

/**
 * @brief Convert a thermistor resistance to Celsius with the segmented Steinhart-Hart model.
 *
 * Coefficients are the Phoenix sample-thermistor table (10 k NTC), selected by R/R25 band.
 *
 * @param resistance_ohms Measured thermistor resistance.
 * @param r25_ohms Nominal resistance at 25 C.
 * @param calibration_offset_c Offset added to the result.
 * @param temperature_c_out Destination for the temperature in C.
 * @return THERMISTOR_READER_OK, THERMISTOR_READER_ERR_INVALID_ARG on NULL output, or
 *         THERMISTOR_READER_ERR_COMPUTE_FAILURE for non-positive inputs or a non-physical result.
 */
int thermistor_reader_convert_steinhart_hart_temperature(float resistance_ohms, float r25_ohms,
                                                         float calibration_offset_c, float* temperature_c_out);

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
