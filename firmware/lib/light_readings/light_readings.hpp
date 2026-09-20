#ifndef LIGHT_READINGS_HPP
#define LIGHT_READINGS_HPP

#include "led_driver.hpp"
#include "phoenix_guard.hpp"
#include <stdbool.h>
#include <stdint.h>

// ===================== Return Codes =============================================
#define LIGHT_READINGS_OK PHX_OK
#define LIGHT_READINGS_ERR_INVALID_ARG PHX_ERR_INVALID_ARG
#define LIGHT_READINGS_ERR_NOT_INITIALIZED PHX_ERR_NOT_INITIALIZED
#define LIGHT_READINGS_ERR_NOT_IMPLEMENTED PHX_ERR_NOT_IMPLEMENTED
#define LIGHT_READINGS_ERR_TIMEOUT PHX_ERR_TIMEOUT
#define LIGHT_READINGS_ERR_SWEEP_CAPACITY_EXCEEDED (PHX_ERR_MODULE_BASE - 1)

/** @brief Maximum number of sweeps a collection can hold. */
#define LIGHT_READINGS_MAX_SWEEP_COUNT 500u

// NAU7802 codes at or beyond these are treated as clipped: the 24-bit result tops out at 8388607
// and the chip returns 8388606 when the input exceeds the reference range, so leave a little slack.
#define LIGHT_READINGS_SATURATION_POSITIVE_CODE 8388600
#define LIGHT_READINGS_SATURATION_NEGATIVE_CODE (-8388600)

/**
 * @brief Per-LED sweep settings.
 */
struct LightReadingsChannelConfig {
  LedDriverChannel led;            /**< LED lit while sampling this photodiode. */
  uint32_t         settle_time_us; /**< Delay after switching the LED on before the ADC is trusted. */
};

/**
 * @brief Board-level configuration for a full dark/blue/dark/green sweep.
 *
 * phrog has one ADC with a multiplexer in front of two photodiodes, so every sweep switches the
 * NAU7802 channel between colours. After any LED or channel change the ADC's decimation filter
 * still holds the previous state for a few conversions; settle_conversions of them are discarded
 * before the reading that is kept.
 */
struct LightReadingsConfig {
  LightReadingsChannelConfig green;               /**< Green LED and its photodiode (NAU7802 channel 2). */
  LightReadingsChannelConfig blue;                /**< Blue LED and its photodiode (NAU7802 channel 1). */
  uint32_t                   dark_settle_time_us; /**< Delay after turning LEDs off before the dark read. */
  uint32_t                   settle_conversions;  /**< Conversions discarded after each LED or channel change. */
  uint32_t                   adc_timeout_us;      /**< Maximum wait for each NAU7802 conversion. */
};

/**
 * @brief Raw ADC codes captured for a single sweep. Each photodiode gets its own dark reading
 * because the two amplifiers have independent offsets.
 */
struct LightReadingsSweepSample {
  int32_t dark_blue_code;  /**< Blue photodiode with both LEDs off. */
  int32_t dark_green_code; /**< Green photodiode with both LEDs off. */
  int32_t blue_code;       /**< Blue photodiode with the blue LED lit. */
  int32_t green_code;      /**< Green photodiode with the green LED lit. */
};

/**
 * @brief Fixed-capacity collection of sweeps backed by caller-provided storage.
 */
struct LightReadingsSweepCollection {
  uint32_t                  sweep_count; /**< Number of valid entries in @p sweeps. */
  LightReadingsSweepSample* sweeps;      /**< Storage for at least the requested sweep count. */
};

/**
 * @brief Global sweep storage for callers that do not want to dedicate their own buffer.
 */
extern LightReadingsSweepSample g_light_readings_sweep_storage[LIGHT_READINGS_MAX_SWEEP_COUNT];

/**
 * @brief Summary statistics for one sweep field across a collection.
 */
struct LightReadingsStatisticSummary {
  uint32_t sample_count;       /**< Number of samples contributing to the metrics. */
  double   mean;               /**< Sample mean. */
  double   standard_deviation; /**< Sample standard deviation (n - 1); zero for fewer than two samples. */
  int32_t  min_value;          /**< Minimum observed code. */
  int32_t  max_value;          /**< Maximum observed code. */
  double   drift_slope;        /**< Least-squares slope of code against sweep index. */
  bool     has_samples;        /**< False when the collection was empty. */
};

/**
 * @brief Per-field statistics for a sweep collection.
 */
struct LightReadingsSweepStats {
  uint32_t                      sweep_count;
  LightReadingsStatisticSummary dark_blue;
  LightReadingsStatisticSummary dark_green;
  LightReadingsStatisticSummary blue;
  LightReadingsStatisticSummary green;
};

/**
 * @brief Cache the sweep configuration.
 *
 * The LED driver and NAU7802 must be initialised before the first sweep; their errors propagate
 * unchanged from the sweep calls.
 *
 * @param config Pointer to the board-level configuration.
 * @return LIGHT_READINGS_OK on success, LIGHT_READINGS_ERR_INVALID_ARG on NULL config or a zero
 *         ADC timeout.
 */
int light_readings_initialize(const LightReadingsConfig* config);

/**
 * @brief Perform one sweep: blue photodiode dark then lit, green photodiode dark then lit.
 *
 * Both LEDs are off when the call returns, whatever the outcome.
 *
 * @param result_out Destination for the four codes.
 * @return LIGHT_READINGS_OK on success or a propagated error from led_driver or nau7802.
 */
int light_readings_sweep(LightReadingsSweepSample* result_out);

/**
 * @brief Perform @p sweep_count consecutive sweeps into a collection.
 *
 * @param sweep_count Number of sweeps to capture (1..LIGHT_READINGS_MAX_SWEEP_COUNT).
 * @param results_out Collection whose @p sweeps storage holds at least @p sweep_count entries.
 * @return LIGHT_READINGS_OK on success, LIGHT_READINGS_ERR_INVALID_ARG for NULL pointers or a zero
 *         count, LIGHT_READINGS_ERR_SWEEP_CAPACITY_EXCEEDED when the count exceeds the maximum, or
 *         a propagated error code.
 */
int light_readings_sweep_n(uint32_t sweep_count, LightReadingsSweepCollection* results_out);

/**
 * @brief Compute per-field statistics over a collection.
 *
 * @param sweep_collection Source collection; @p sweeps may be NULL only when the count is zero.
 * @param stats_out Destination statistics.
 * @return LIGHT_READINGS_OK on success or LIGHT_READINGS_ERR_INVALID_ARG.
 */
int light_readings_compute_sweep_stats(const LightReadingsSweepCollection* sweep_collection,
                                       LightReadingsSweepStats*            stats_out);

/**
 * @brief Report whether any code in the most recent sweep was at the ADC's clipping level.
 *
 * On phrog a clipped lit reading means the amplifier trimpot for that colour is set too high.
 */
bool light_readings_last_sweep_detected_saturation(void);

/**
 * @brief Turn LEDs off and release state.
 *
 * @return LIGHT_READINGS_OK on success, LIGHT_READINGS_ERR_NOT_INITIALIZED if never initialised.
 */
int light_readings_shutdown(void);

/** @brief Reset internal state between Unity test cases. */
void light_readings_reset_for_test(void);

/** @brief Retrieve the cached configuration for test inspection. */
void light_readings_get_config_for_test(LightReadingsConfig* config_out);

#endif  // LIGHT_READINGS_HPP
