#include "cli.hpp"

#include "nau7802.hpp"
#include "ph_equations.hpp"
#include "phrog_settings.hpp"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

// ===================== Constants ================================================
static const size_t k_max_command_length   = 32u;
static const double k_default_salinity_psu = 35.0;

// Firmware version string reported by the 'v' command for host sanity checks.
static const char* const k_firmware_version = "phrog-cli 0.1.0";

// Monitor mode: positive full scale of the 24-bit result and the clip level shared with light_readings.
static const int32_t k_full_scale_code = 8388607;

// Monitor mode runs the ADC at 80 SPS so a full sweep takes a fraction of a second.
static const Nau7802SampleRate k_monitor_sample_rate = Nau7802SampleRate::NAU7802_SPS_80;

// ===================== Module State =============================================
static Print*                  g_output                  = NULL;
static CliMeasurementHooks     g_hooks                   = {};
static bool                    g_cli_ready               = false;
static bool                    g_ready_banner_sent       = false;
static bool                    g_serial_was_connected    = false;
static bool                    g_baseline_valid          = false;
static LightReadingsSweepStats g_baseline_stats          = {};
static Nau7802Settings         g_settings_before_monitor = {};

// ===================== Production Hooks =========================================

// Reads one byte from Serial if any is waiting; the monitor uses this to spot a keypress.
static int default_read_input(void) {
  return (Serial.available() > 0) ? Serial.read() : -1;
}

// Switches the ADC to the monitor rate and back, restoring whatever was applied before.
static int default_set_monitor_rate(bool fast) {
  if (fast) {
    GUARD(nau7802_get_settings(&g_settings_before_monitor));
    Nau7802Settings monitor_settings = g_settings_before_monitor;
    monitor_settings.sample_rate     = k_monitor_sample_rate;
    return nau7802_apply_settings(&monitor_settings);
  }
  return nau7802_apply_settings(&g_settings_before_monitor);
}

static const CliMeasurementHooks k_default_hooks = {
    light_readings_sweep_n,
    light_readings_sweep,
    light_readings_compute_sweep_stats,
    thermistor_reader_measure_celsius,
    default_read_input,
    default_set_monitor_rate,
};

// ===================== Output Helpers ===========================================

// Returns the active output stream, defaulting to Serial when no override is installed.
static Print* active_output(void) {
  return (g_output != NULL) ? g_output : &Serial;
}

// Emits the shared tab-separated error format: error<TAB>label<TAB>code.
static void emit_error(const char* label, int error_code) {
  active_output()->print("error\t");
  active_output()->print(label);
  active_output()->print("\t");
  active_output()->println(error_code);
}

#define CLI_GUARD_EMIT(label, expression) GUARD_EMIT(emit_error, label, expression)

// Emits fixed-width summary stats for one sweep field so the columns line up.
static void emit_field_summary(const char* field_name, const LightReadingsStatisticSummary& summary) {
  char line[120];
  snprintf(line, sizeof(line), "%-12s %5lu %14.2f %10.2f %10ld %10ld %10.4f", field_name,
           static_cast<unsigned long>(summary.sample_count), summary.mean, summary.standard_deviation,
           static_cast<long>(summary.min_value), static_cast<long>(summary.max_value), summary.drift_slope);
  active_output()->println(line);
}

static void emit_stats_table(const LightReadingsSweepStats& stats) {
  active_output()->println("field        count           mean     stddev        min        max      drift");
  emit_field_summary("dark_blue", stats.dark_blue);
  emit_field_summary("blue", stats.blue);
  emit_field_summary("dark_green", stats.dark_green);
  emit_field_summary("green", stats.green);
  if (light_readings_last_sweep_detected_saturation()) {
    active_output()->println("warning\tsaturated\tturn the amplifier trimpot down and repeat");
  }
}

// ===================== Absorbance ===============================================

// Absorbance for one colour from dark-corrected baseline and sample means.
static int compute_channel_absorbance(const LightReadingsStatisticSummary& reference_lit,
                                      const LightReadingsStatisticSummary& reference_dark,
                                      const LightReadingsStatisticSummary& sample_lit,
                                      const LightReadingsStatisticSummary& sample_dark, double* absorbance_out) {
  GUARD_NONNULL(absorbance_out);
  if (!reference_lit.has_samples || !reference_dark.has_samples || !sample_lit.has_samples ||
      !sample_dark.has_samples) {
    return PH_EQUATIONS_ERR_INVALID_ARG;
  }
  const double reference_intensity = reference_lit.mean - reference_dark.mean;
  const double sample_intensity    = sample_lit.mean - sample_dark.mean;
  if ((reference_intensity <= 0.0) || (sample_intensity <= 0.0)) {
    return PH_EQUATIONS_ERR_INVALID_ARG;
  }
  return ph_equations_calc_absorbance(reference_intensity, sample_intensity, absorbance_out);
}

static int compute_absorbance_pair(const LightReadingsSweepStats& baseline, const LightReadingsSweepStats& sample,
                                   double* absorbance_blue_out, double* absorbance_green_out) {
  GUARD(compute_channel_absorbance(baseline.blue, baseline.dark_blue, sample.blue, sample.dark_blue,
                                   absorbance_blue_out));
  return compute_channel_absorbance(baseline.green, baseline.dark_green, sample.green, sample.dark_green,
                                    absorbance_green_out);
}

// ===================== Command Handlers =========================================

static int handle_help(void);

// Sweeps per measurement: the stored setting when available, the compile-time default otherwise.
static uint32_t measurement_sweep_count(void) {
  const PhrogSettings* settings = phrog_settings_get();
  return (settings != NULL) ? settings->measurement_sweep_count : CLI_MEASUREMENT_SWEEP_COUNT;
}

// Reports the firmware version and the active settings for host sanity checks.
static int handle_version(void) {
  active_output()->println(k_firmware_version);
  const PhrogSettings* settings = phrog_settings_get();
  if (settings == NULL) {
    active_output()->println("settings: not initialized");
    return PHX_OK;
  }
  char line[80];
  snprintf(line, sizeof(line), "measurement_sweep_count: %u", static_cast<unsigned>(settings->measurement_sweep_count));
  active_output()->println(line);
  snprintf(line, sizeof(line), "thermistor_calibration_offset_c: %.3f",
           static_cast<double>(settings->thermistor_calibration_offset_c));
  active_output()->println(line);
  return PHX_OK;
}

// Captures a baseline and caches its statistics for later samples.
static int handle_baseline(void) {
  active_output()->println("Taking baseline...");
  delay(1);  // Let the line leave the USB buffer before the sweeps monopolise the loop.

  LightReadingsSweepCollection sweeps = {0u, g_light_readings_sweep_storage};
  CLI_GUARD_EMIT("sweep", g_hooks.sweep_n(measurement_sweep_count(), &sweeps));
  LightReadingsSweepStats stats = {};
  CLI_GUARD_EMIT("stats", g_hooks.compute_stats(&sweeps, &stats));

  g_baseline_stats = stats;
  g_baseline_valid = true;
  emit_stats_table(stats);
  return PHX_OK;
}

// Runs the sample workflow: sweeps, temperature, absorbance, and pH against the cached baseline.
static int handle_sample(void) {
  // Step 1: Absorbance needs a reference, so refuse without one.
  if (!g_baseline_valid) {
    emit_error("missing_baseline", PHX_ERR_NOT_INITIALIZED);
    return PHX_ERR_NOT_INITIALIZED;
  }

  active_output()->println("Taking sample...");
  delay(1);  // Let the line leave the USB buffer before the sweeps monopolise the loop.

  // Step 2: Sweeps and statistics.
  LightReadingsSweepCollection sweeps       = {0u, g_light_readings_sweep_storage};
  LightReadingsSweepStats      sample_stats = {};
  CLI_GUARD_EMIT("sweep", g_hooks.sweep_n(measurement_sweep_count(), &sweeps));
  CLI_GUARD_EMIT("stats", g_hooks.compute_stats(&sweeps, &sample_stats));

  // Step 3: Sample temperature for the pH equations.
  float sample_temperature_c = 0.0f;
  CLI_GUARD_EMIT("temperature_sample", g_hooks.measure_temperature(&sample_temperature_c));

  // Step 4: Absorbance on both colours against the baseline.
  double absorbance_blue  = 0.0;
  double absorbance_green = 0.0;
  CLI_GUARD_EMIT("absorbance",
                 compute_absorbance_pair(g_baseline_stats, sample_stats, &absorbance_blue, &absorbance_green));

  // Step 5: R-ratio and pH; a failure here still reports the raw numbers for diagnostics.
  double r_ratio  = 0.0;
  double ph_value = 0.0;
  bool   ph_valid = (ph_equations_calc_r_ratio(absorbance_green, absorbance_blue, &r_ratio) == PH_EQUATIONS_OK);
  if (ph_valid) {
    ph_valid = (ph_equations_compute_ph(r_ratio, static_cast<double>(sample_temperature_c), k_default_salinity_psu,
                                        &ph_value) == PH_EQUATIONS_OK);
  }

  // Step 6: Report.
  emit_stats_table(sample_stats);
  active_output()->println();
  active_output()->println("temp_sample   abs_blue   abs_green    r_ratio        pH");
  char result_line[120];
  if (ph_valid) {
    snprintf(result_line, sizeof(result_line), "%11.2f %10.6f %11.6f %10.6f %9.4f",
             static_cast<double>(sample_temperature_c), absorbance_blue, absorbance_green, r_ratio, ph_value);
  }
  else {
    snprintf(result_line, sizeof(result_line), "%11.2f %10.6f %11.6f %10.6f     ERROR",
             static_cast<double>(sample_temperature_c), absorbance_blue, absorbance_green, r_ratio);
  }
  active_output()->println(result_line);
  return PHX_OK;
}

// Verdict for a lit monitor reading against the target window, with trimpot advice.
static void monitor_verdict(int32_t code, int32_t percent, const char* trimpot, const char** verdict_out, char* advice,
                            size_t advice_capacity) {
  advice[0] = '\0';
  if (code >= LIGHT_READINGS_SATURATION_POSITIVE_CODE) {
    *verdict_out = "SATURATED";
    snprintf(advice, advice_capacity, "turn %s down a lot", trimpot);
  }
  else if (percent > CLI_MONITOR_GOOD_MAX_PERCENT) {
    *verdict_out = "HIGH";
    snprintf(advice, advice_capacity, "turn %s down", trimpot);
  }
  else if (percent < CLI_MONITOR_GOOD_MIN_PERCENT) {
    *verdict_out = "LOW";
    snprintf(advice, advice_capacity, "turn %s up", trimpot);
  }
  else {
    *verdict_out = "GOOD";
  }
}

// One fixed-width monitor row; dark rows carry no verdict.
static void emit_monitor_row(const char* photodiode, const char* led, int32_t code, const char* trimpot) {
  const int32_t percent    = static_cast<int32_t>((static_cast<int64_t>(code) * 100) / k_full_scale_code);
  const char*   verdict    = "";
  char          advice[24] = "";
  if (trimpot != NULL) {
    monitor_verdict(code, percent, trimpot, &verdict, advice, sizeof(advice));
  }
  char line[96];
  snprintf(line, sizeof(line), "%-10s %-10s %10ld %5ld%%  %-10s %-20s", photodiode, led, static_cast<long>(code),
           static_cast<long>(percent), verdict, advice);
  active_output()->println(line);
}

// Drains any pending input so the keypress that ended the monitor is not parsed as a command.
static void drain_input(void) {
  while (g_hooks.read_input() >= 0) {
  }
}

// Live monitor for trimpot tuning: sweeps and prints a table until any byte arrives.
static int handle_monitor(void) {
  active_output()->println("monitor: sweeping until any key is pressed");
  CLI_GUARD_EMIT("monitor_rate", g_hooks.set_monitor_rate(true));

  int return_code = PHX_OK;
  while (true) {
    // Step 1: One sweep gives all four monitor readings.
    LightReadingsSweepSample sample = {};
    return_code                     = g_hooks.sweep(&sample);
    if (return_code != PHX_OK) {
      emit_error("sweep", return_code);
      break;
    }

    // Step 2: Print the block, then a separator so each pass reads as one snapshot.
    active_output()->println("photodiode led              code pct_fs  verdict    advice");
    emit_monitor_row("blue_pd", "dark", sample.dark_blue_code, NULL);
    emit_monitor_row("blue_pd", "blue_led", sample.blue_code, "RV1");
    emit_monitor_row("green_pd", "dark", sample.dark_green_code, NULL);
    emit_monitor_row("green_pd", "green_led", sample.green_code, "RV2");
    active_output()->println();

    // Step 3: Any byte ends the monitor.
    if (g_hooks.read_input() >= 0) {
      drain_input();
      break;
    }
  }

  // Step 4: Always put the ADC back the way it was, even after a sweep error.
  const int restore_code = g_hooks.set_monitor_rate(false);
  if (restore_code != PHX_OK) {
    emit_error("monitor_rate", restore_code);
  }
  active_output()->println("monitor: stopped");
  return (return_code != PHX_OK) ? return_code : restore_code;
}

struct CliCommandEntry {
  const char* name;
  int (*handler)(void);
  const char* help;
};

static const CliCommandEntry k_commands[] = {
    {"b", handle_baseline, "Capture baseline sweeps"},
    {"s", handle_sample, "Capture sample sweeps + pH"},
    {"c", handle_monitor, "Live ADC monitor for trimpot tuning (any key stops)"},
    {"v", handle_version, "Print firmware version"},
    {"help", handle_help, "List commands"},
    {NULL, NULL, NULL},
};

// Lists every command with its help text.
static int handle_help(void) {
  for (size_t index = 0u; k_commands[index].name != NULL; ++index) {
    active_output()->print(k_commands[index].name);
    active_output()->print("\t");
    active_output()->println(k_commands[index].help);
  }
  return PHX_OK;
}

// ===================== Public API ===============================================

void cli_initialize(void) {
  g_hooks             = k_default_hooks;
  g_baseline_valid    = false;
  g_cli_ready         = true;
  g_ready_banner_sent = false;
  memset(&g_baseline_stats, 0, sizeof(g_baseline_stats));
  // The ready banner waits for a host to connect; see cli_poll.
}

CliDispatchResult cli_dispatch_command(const char* command_token) {
  if ((command_token == NULL) || (command_token[0] == '\0')) {
    return CLI_DISPATCH_EMPTY_COMMAND;
  }
  for (size_t index = 0u; k_commands[index].name != NULL; ++index) {
    if (strcmp(command_token, k_commands[index].name) == 0) {
      (void) k_commands[index].handler();
      return CLI_DISPATCH_OK;
    }
  }
  active_output()->print("error\tunknown_command\t");
  active_output()->println(command_token);
  return CLI_DISPATCH_UNKNOWN_COMMAND;
}

void cli_poll(void) {
  if (!g_cli_ready) {
    return;
  }

  // Step 1: Re-send the banner whenever a host (re)connects.
  const bool serial_connected = static_cast<bool>(Serial);
  if (!serial_connected && g_serial_was_connected) {
    g_ready_banner_sent = false;
  }
  g_serial_was_connected = serial_connected;
  if (!g_ready_banner_sent && serial_connected) {
    active_output()->println("phrog-cli ready (commands: b, s, c, v, help)");
    g_ready_banner_sent = true;
  }

  // Step 2: Accumulate a line and dispatch it on newline.
  static char   command_buffer[k_max_command_length] = {0};
  static size_t command_length                       = 0u;
  while (Serial.available() > 0) {
    const int incoming_byte = Serial.read();
    if (incoming_byte < 0) {
      break;
    }
    const char incoming_char = static_cast<char>(incoming_byte);
    if ((incoming_char == '\n') || (incoming_char == '\r')) {
      command_buffer[command_length] = '\0';
      if (command_length > 0u) {
        (void) cli_dispatch_command(command_buffer);
      }
      command_length = 0u;
      continue;
    }
    if (command_length < (k_max_command_length - 1u)) {
      command_buffer[command_length] = incoming_char;
      ++command_length;
    }
  }
}

bool cli_test_is_baseline_cached(void) {
  return g_baseline_valid;
}

void cli_test_get_baseline_stats(LightReadingsSweepStats* stats_out) {
  if (stats_out == NULL) {
    return;
  }
  *stats_out = g_baseline_stats;
}

void cli_test_set_measurement_hooks(const CliMeasurementHooks* hooks) {
  g_hooks = (hooks != NULL) ? *hooks : k_default_hooks;
}

void cli_test_set_output(Print* output) {
  g_output = output;
}
