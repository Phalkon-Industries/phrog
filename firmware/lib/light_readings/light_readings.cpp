#include "light_readings.hpp"

#include "nau7802.hpp"
#include <Arduino.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

// ===================== Module State =============================================
static bool                g_initialized          = false;
static LightReadingsConfig g_config               = {};
static bool                g_last_sweep_saturated = false;

LightReadingsSweepSample g_light_readings_sweep_storage[LIGHT_READINGS_MAX_SWEEP_COUNT];

// ===================== Sweep Helpers ============================================

// Reports whether a code sits at either clipping level of the 24-bit result.
static bool code_is_saturated(int32_t code) {
  return (code >= LIGHT_READINGS_SATURATION_POSITIVE_CODE) || (code <= LIGHT_READINGS_SATURATION_NEGATIVE_CODE);
}

// Discards the configured number of conversions so the decimation filter has flushed the previous
// LED or channel state, then returns the next one. Every read goes through the ADC's own timeout.
static int read_settled_code(int32_t* code_out) {
  for (uint32_t index = 0u; index < g_config.settle_conversions; ++index) {
    int32_t discarded = 0;
    GUARD(nau7802_read_conversion(g_config.adc_timeout_us, &discarded));
  }
  return nau7802_read_conversion(g_config.adc_timeout_us, code_out);
}

// Reads one photodiode with the LEDs off and then with its own LED lit. The channel is switched
// first so both readings come from the same multiplexer position.
static int read_photodiode_pair(Nau7802Channel channel, const LightReadingsChannelConfig& lit, int32_t* dark_out,
                                int32_t* lit_out) {
  // Step 1: Dark reading on this photodiode.
  GUARD(nau7802_select_channel(channel));
  GUARD(led_driver_set_channel(LedDriverChannel::LED_DRIVER_CHANNEL_OFF));
  if (g_config.dark_settle_time_us > 0u) {
    delayMicroseconds(g_config.dark_settle_time_us);
  }
  GUARD(read_settled_code(dark_out));

  // Step 2: Lit reading with the matching LED.
  GUARD(led_driver_set_channel(lit.led));
  if (lit.settle_time_us > 0u) {
    delayMicroseconds(lit.settle_time_us);
  }
  return read_settled_code(lit_out);
}

// Runs the full sweep; the caller is responsible for turning the LEDs off afterwards.
static int run_sweep(LightReadingsSweepSample* result_out) {
  // Blue photodiode is NAU7802 channel 1, green is channel 2 (docs/pin-map.md). Blue first
  // because the ADC powers up on channel 1, saving one multiplexer switch per sweep.
  GUARD(read_photodiode_pair(Nau7802Channel::NAU7802_CHANNEL_1, g_config.blue, &result_out->dark_blue_code,
                             &result_out->blue_code));
  GUARD(read_photodiode_pair(Nau7802Channel::NAU7802_CHANNEL_2, g_config.green, &result_out->dark_green_code,
                             &result_out->green_code));
  return LIGHT_READINGS_OK;
}

// ===================== Statistics Helpers =======================================
// Ported from Phoenix: Welford's running mean and variance plus least-squares sums for drift.

struct RunningStats {
  uint32_t sample_count;
  double   mean;
  double   m2;
  int32_t  min_value;
  int32_t  max_value;
  double   sum_x;
  double   sum_y;
  double   sum_xy;
  double   sum_xx;
};

static void running_stats_reset(RunningStats* stats) {
  stats->sample_count = 0u;
  stats->mean         = 0.0;
  stats->m2           = 0.0;
  stats->min_value    = INT32_MAX;
  stats->max_value    = INT32_MIN;
  stats->sum_x        = 0.0;
  stats->sum_y        = 0.0;
  stats->sum_xy       = 0.0;
  stats->sum_xx       = 0.0;
}

// Folds one sample into the accumulator; sample_index is the regression abscissa for drift.
static void running_stats_update(RunningStats* stats, uint32_t sample_index, int32_t value) {
  ++stats->sample_count;

  // Step 1: Welford update keeps the variance numerically stable over long collections.
  const double double_value = static_cast<double>(value);
  const double delta        = double_value - stats->mean;
  stats->mean += delta / static_cast<double>(stats->sample_count);
  const double delta2 = double_value - stats->mean;
  stats->m2 += delta * delta2;

  // Step 2: Regression sums for the drift slope.
  const double double_index = static_cast<double>(sample_index);
  stats->sum_x += double_index;
  stats->sum_y += double_value;
  stats->sum_xy += double_index * double_value;
  stats->sum_xx += double_index * double_index;

  // Step 3: Extremes.
  if (value < stats->min_value) {
    stats->min_value = value;
  }
  if (value > stats->max_value) {
    stats->max_value = value;
  }
}

// Least-squares slope of value against index; zero when the indices cannot define a line.
static double running_stats_slope(const RunningStats& stats) {
  if (stats.sample_count < 2u) {
    return 0.0;
  }
  const double count       = static_cast<double>(stats.sample_count);
  const double denominator = (count * stats.sum_xx) - (stats.sum_x * stats.sum_x);
  if (fabs(denominator) < 1e-12) {
    return 0.0;
  }
  return ((count * stats.sum_xy) - (stats.sum_x * stats.sum_y)) / denominator;
}

static void populate_summary(const RunningStats& stats, LightReadingsStatisticSummary* summary_out) {
  summary_out->sample_count = stats.sample_count;
  summary_out->has_samples  = stats.sample_count > 0u;
  summary_out->mean         = summary_out->has_samples ? stats.mean : 0.0;
  summary_out->min_value    = summary_out->has_samples ? stats.min_value : 0;
  summary_out->max_value    = summary_out->has_samples ? stats.max_value : 0;
  summary_out->drift_slope  = summary_out->has_samples ? running_stats_slope(stats) : 0.0;
  summary_out->standard_deviation =
      (stats.sample_count > 1u) ? sqrt(stats.m2 / static_cast<double>(stats.sample_count - 1u)) : 0.0;
}

// ===================== Public API ===============================================

int light_readings_initialize(const LightReadingsConfig* config) {
  GUARD_NONNULL(config);
  if (config->adc_timeout_us == 0u) {
    return LIGHT_READINGS_ERR_INVALID_ARG;
  }
  memcpy(&g_config, config, sizeof(LightReadingsConfig));
  g_last_sweep_saturated = false;
  g_initialized          = true;
  return LIGHT_READINGS_OK;
}

int light_readings_sweep(LightReadingsSweepSample* result_out) {
  GUARD_NONNULL(result_out);
  GUARD_INITIALIZED(g_initialized);

  // Step 1: Run the sweep, then park the LEDs whatever happened so a failure never leaves one lit.
  const int sweep_return_code = run_sweep(result_out);
  const int led_return_code   = led_driver_set_channel(LedDriverChannel::LED_DRIVER_CHANNEL_OFF);
  GUARD(sweep_return_code);
  GUARD(led_return_code);

  // Step 2: Record whether anything clipped so callers can warn about trimpot settings.
  g_last_sweep_saturated = code_is_saturated(result_out->dark_blue_code) ||
                           code_is_saturated(result_out->dark_green_code) || code_is_saturated(result_out->blue_code) ||
                           code_is_saturated(result_out->green_code);
  return LIGHT_READINGS_OK;
}

int light_readings_sweep_n(uint32_t sweep_count, LightReadingsSweepCollection* results_out) {
  GUARD_NONNULL(results_out);
  GUARD_INITIALIZED(g_initialized);
  if ((sweep_count == 0u) || (results_out->sweeps == NULL)) {
    return LIGHT_READINGS_ERR_INVALID_ARG;
  }
  if (sweep_count > LIGHT_READINGS_MAX_SWEEP_COUNT) {
    return LIGHT_READINGS_ERR_SWEEP_CAPACITY_EXCEEDED;
  }

  // Sweeps land in the collection as they complete so a failure part-way leaves a usable prefix.
  results_out->sweep_count = 0u;
  for (uint32_t index = 0u; index < sweep_count; ++index) {
    GUARD(light_readings_sweep(&results_out->sweeps[index]));
    results_out->sweep_count = index + 1u;
  }
  return LIGHT_READINGS_OK;
}

int light_readings_compute_sweep_stats(const LightReadingsSweepCollection* sweep_collection,
                                       LightReadingsSweepStats*            stats_out) {
  GUARD_NONNULL(sweep_collection);
  GUARD_NONNULL(stats_out);
  if ((sweep_collection->sweeps == NULL) && (sweep_collection->sweep_count > 0u)) {
    return LIGHT_READINGS_ERR_INVALID_ARG;
  }

  // Step 1: Clear the output so an empty collection yields deterministic zeros.
  memset(stats_out, 0, sizeof(LightReadingsSweepStats));
  stats_out->sweep_count = sweep_collection->sweep_count;

  // Step 2: Accumulate every field, then summarise.
  RunningStats dark_blue;
  RunningStats dark_green;
  RunningStats blue;
  RunningStats green;
  running_stats_reset(&dark_blue);
  running_stats_reset(&dark_green);
  running_stats_reset(&blue);
  running_stats_reset(&green);
  for (uint32_t index = 0u; index < sweep_collection->sweep_count; ++index) {
    const LightReadingsSweepSample& sweep = sweep_collection->sweeps[index];
    running_stats_update(&dark_blue, index, sweep.dark_blue_code);
    running_stats_update(&dark_green, index, sweep.dark_green_code);
    running_stats_update(&blue, index, sweep.blue_code);
    running_stats_update(&green, index, sweep.green_code);
  }
  populate_summary(dark_blue, &stats_out->dark_blue);
  populate_summary(dark_green, &stats_out->dark_green);
  populate_summary(blue, &stats_out->blue);
  populate_summary(green, &stats_out->green);
  return LIGHT_READINGS_OK;
}

bool light_readings_last_sweep_detected_saturation(void) {
  return g_last_sweep_saturated;
}

int light_readings_shutdown(void) {
  GUARD_INITIALIZED(g_initialized);
  // Park the LEDs first; the driver may already be down, which is not this module's failure.
  (void) led_driver_set_channel(LedDriverChannel::LED_DRIVER_CHANNEL_OFF);
  g_initialized          = false;
  g_last_sweep_saturated = false;
  return LIGHT_READINGS_OK;
}

void light_readings_reset_for_test(void) {
  g_initialized          = false;
  g_last_sweep_saturated = false;
  memset(&g_config, 0, sizeof(LightReadingsConfig));
}

void light_readings_get_config_for_test(LightReadingsConfig* config_out) {
  if (config_out == NULL) {
    return;
  }
  memcpy(config_out, &g_config, sizeof(LightReadingsConfig));
}
