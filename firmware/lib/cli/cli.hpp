#ifndef CLI_HPP
#define CLI_HPP

#include "light_readings.hpp"
#include "phoenix_guard.hpp"
#include "thermistor_reader.hpp"
#include <stdbool.h>
#include <stddef.h>

class Print;

/**
 * @brief Dispatch outcomes for CLI command parsing.
 */
typedef enum CliDispatchResult {
  CLI_DISPATCH_OK              = 0,
  CLI_DISPATCH_EMPTY_COMMAND   = -1,
  CLI_DISPATCH_UNKNOWN_COMMAND = -2,
} CliDispatchResult;

/**
 * @brief Measurement hooks that allow tests to stub sweep and statistic helpers.
 *
 * Production uses the default light_readings / thermistor_reader helpers.
 */
struct CliMeasurementHooks {
  int (*sweep_n)(uint32_t sweep_count, LightReadingsSweepCollection* results_out);
  int (*compute_stats)(const LightReadingsSweepCollection* sweep_collection, LightReadingsSweepStats* stats_out);
  int (*measure_temperature)(float* temperature_c_out);
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
