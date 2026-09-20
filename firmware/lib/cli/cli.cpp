#include "cli.hpp"

#include <Arduino.h>
#include <string.h>

// ===================== Module State =============================================
static Print*              g_output = NULL;
static CliMeasurementHooks g_hooks  = {};

// Production hooks route straight to the measurement modules.
static const CliMeasurementHooks k_default_hooks = {
    light_readings_sweep_n,
    light_readings_compute_sweep_stats,
    thermistor_reader_measure_celsius,
};

// Returns the active output stream, defaulting to Serial when no override is installed.
static Print* active_output(void) {
  return (g_output != NULL) ? g_output : &Serial;
}

void cli_initialize(void) {
  g_hooks = k_default_hooks;
  active_output()->println("phrog ready");
}

void cli_poll(void) {
  // TODO(phrog): accumulate newline-terminated input from Serial and call cli_dispatch_command.
}

CliDispatchResult cli_dispatch_command(const char* command_token) {
  if ((command_token == NULL) || (command_token[0] == '\0')) {
    return CLI_DISPATCH_EMPTY_COMMAND;
  }

  // TODO(phrog): command table (help, baseline, sample, temp, settings ...).
  if (strcmp(command_token, "help") == 0) {
    active_output()->println("help\tList commands");
    return CLI_DISPATCH_OK;
  }

  return CLI_DISPATCH_UNKNOWN_COMMAND;
}

void cli_test_set_measurement_hooks(const CliMeasurementHooks* hooks) {
  g_hooks = (hooks != NULL) ? *hooks : k_default_hooks;
}

void cli_test_set_output(Print* output) {
  g_output = output;
}
