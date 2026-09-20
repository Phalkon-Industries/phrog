#ifndef LIGHT_READINGS_HPP
#define LIGHT_READINGS_HPP

#include "led_driver.hpp"
#include "phoenix_guard.hpp"
#include <stdint.h>

// ===================== Return Codes =============================================
#define LIGHT_READINGS_OK PHX_OK
#define LIGHT_READINGS_ERR_INVALID_ARG PHX_ERR_INVALID_ARG
#define LIGHT_READINGS_ERR_NOT_INITIALIZED PHX_ERR_NOT_INITIALIZED
#define LIGHT_READINGS_ERR_NOT_IMPLEMENTED PHX_ERR_NOT_IMPLEMENTED
#define LIGHT_READINGS_ERR_TIMEOUT PHX_ERR_TIMEOUT
#define LIGHT_READINGS_ERR_SWEEP_CAPACITY_EXCEEDED (PHX_ERR_MODULE_BASE - 1)

/** @brief Maximum number of sweeps a collection can hold (bounds stack usage). */
#define LIGHT_READINGS_MAX_SWEEP_COUNT 500u

/**
 * @brief Per-LED sweep settings.
 */
struct LightReadingsChannelConfig {
  LedDriverChannel led;            /**< LED lit while sampling this channel. */
  uint32_t         settle_time_us; /**< Delay after switching the LED before the ADC samples. */
};

/**
 * @brief Board-level configuration for a full dark/green/blue sweep.
 */
struct LightReadingsConfig {
  LightReadingsChannelConfig green;               /**< Green LED channel settings. */
  LightReadingsChannelConfig blue;                /**< Blue LED channel settings. */
  uint32_t                   dark_settle_time_us; /**< Delay after turning LEDs off before the dark sample. */
  uint32_t                   adc_timeout_us;      /**< Maximum wait for each NAU7802 conversion. */
};

/**
 * @brief One sweep: dark, green, and blue conversions from the NAU7802.
 */
struct LightReadingsSweepSample {
  int32_t dark_code;
  int32_t green_code;
  int32_t blue_code;
};

/**
 * @brief Fixed-capacity collection of sweeps.
 */
struct LightReadingsSweepCollection {
  LightReadingsSweepSample samples[LIGHT_READINGS_MAX_SWEEP_COUNT];
  uint32_t                 count;
};

/**
 * @brief Summary statistics for one channel across a collection.
 */
struct LightReadingsStatisticSummary {
  double  mean;
  double  standard_deviation;
  int32_t minimum;
  int32_t maximum;
};

/**
 * @brief Per-channel statistics for a sweep collection.
 */
struct LightReadingsSweepStats {
  LightReadingsStatisticSummary dark;
  LightReadingsStatisticSummary green;
  LightReadingsStatisticSummary blue;
  uint32_t                      sample_count;
};

/**
 * @brief Cache configuration and confirm the LED driver and ADC are ready.
 *
 * @param config Pointer to the board-level configuration.
 * @return LIGHT_READINGS_OK on success, LIGHT_READINGS_ERR_INVALID_ARG on NULL config.
 */
int light_readings_initialize(const LightReadingsConfig* config);

/**
 * @brief Perform a single dark/green/blue sweep.
 *
 * @param result_out Destination for the sweep sample.
 * @return LIGHT_READINGS_OK on success or a negative error code.
 */
int light_readings_sweep(LightReadingsSweepSample* result_out);

/**
 * @brief Perform @p sweep_count consecutive sweeps into a collection.
 *
 * @param sweep_count Number of sweeps to capture (1..LIGHT_READINGS_MAX_SWEEP_COUNT).
 * @param results_out Destination collection.
 * @return LIGHT_READINGS_OK on success or a negative error code.
 */
int light_readings_sweep_n(uint32_t sweep_count, LightReadingsSweepCollection* results_out);

/**
 * @brief Compute per-channel statistics over a collection.
 *
 * @param sweep_collection Source collection.
 * @param stats_out Destination statistics.
 * @return LIGHT_READINGS_OK on success or a negative error code.
 */
int light_readings_compute_sweep_stats(const LightReadingsSweepCollection* sweep_collection,
                                       LightReadingsSweepStats*            stats_out);

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
