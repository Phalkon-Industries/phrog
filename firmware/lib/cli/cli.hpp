#ifndef CLI_HPP
#define CLI_HPP

#include "light_readings.hpp"
#include "phoenix_guard.hpp"
#include "thermistor_reader.hpp"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

class Print;

// Sweeps averaged per baseline or sample. At 10 SPS with four discarded conversions per state a
// sweep takes about two seconds, so this keeps a measurement around ten seconds. It should become a
// stored setting once phrog_settings has fields.
#define CLI_MEASUREMENT_SWEEP_COUNT 5u

// Target window for lit readings in monitor mode, as percent of positive full scale, matching the
// adc_monitor bench examples.
#define CLI_MONITOR_GOOD_MIN_PERCENT 70
#define CLI_MONITOR_GOOD_MAX_PERCENT 90

/**
 * @brief Dispatch outcomes for CLI command parsing.
 */
typedef enum CliDispatchResult {
  CLI_DISPATCH_OK              = 0,
  CLI_DISPATCH_EMPTY_COMMAND   = -1,
  CLI_DISPATCH_UNKNOWN_COMMAND = -2,
} CliDispatchResult;

/**
 * @brief Measurement and I/O hooks that let tests drive the CLI without hardware.
 *
 * Production uses light_readings, thermistor_reader, the NAU7802 rate switch, and Serial.
 */
struct CliMeasurementHooks {
  int (*sweep_n)(uint32_t sweep_count, LightReadingsSweepCollection* results_out);
  int (*sweep)(LightReadingsSweepSample* result_out);
  int (*compute_stats)(const LightReadingsSweepCollection* sweep_collection, LightReadingsSweepStats* stats_out);
  int (*measure_temperature)(float* temperature_c_out);
  int (*read_input)(void);            /**< Next pending input byte, or -1 when none is waiting. */
  int (*set_monitor_rate)(bool fast); /**< Switch the ADC to a fast rate for monitor mode, or restore it. */
};

/**
 * @brief Initialise CLI state. Serial must already be configured by the caller.
 */
void cli_initialize(void);

/**
 * @brief Poll the serial interface for newline-terminated commands and dispatch them.
 */
void cli_poll(void);

/**
 * @brief Dispatch a single tokenised command.
 *
 * @param command_token Null-terminated command string to interpret.
 * @return CLI_DISPATCH_OK when handled, otherwise the matching error.
 */
CliDispatchResult cli_dispatch_command(const char* command_token);

/**
 * @brief Report whether a baseline has been captured in this session (test-only helper).
 */
bool cli_test_is_baseline_cached(void);

/**
 * @brief Copy the cached baseline statistics (test-only helper); ignored when @p stats_out is NULL.
 */
void cli_test_get_baseline_stats(LightReadingsSweepStats* stats_out);

/**
 * @brief Override measurement hooks for testing. Passing NULL restores defaults.
 *
 * @param hooks Replacement hook table; pass NULL to restore defaults.
 */
void cli_test_set_measurement_hooks(const CliMeasurementHooks* hooks);

/**
 * @brief Override the CLI output stream for tests. Passing NULL restores Serial.
 *
 * @param output Alternate Print implementation; pass NULL to restore Serial.
 */
void cli_test_set_output(Print* output);

#endif  // CLI_HPP
