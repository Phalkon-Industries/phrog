#include "cli.hpp"
#include "ph_equations.hpp"
#include "unity_config.h"
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>
#include <unity.h>

// ===================== Output capture ============================================

class RecordingPrint : public Print {
 public:
  RecordingPrint() {
    reset();
  }

  void reset(void) {
    buffer_       = "";
    current_line_ = "";
    last_line_    = "";
  }

  String buffer(void) const {
    return buffer_;
  }

  String last_line(void) const {
    return last_line_;
  }

  // Number of times @p needle appears in everything captured so far.
  int count(const char* needle) const {
    int occurrences = 0;
    int position    = buffer_.indexOf(needle);
    while (position >= 0) {
      ++occurrences;
      position = buffer_.indexOf(needle, position + 1);
    }
    return occurrences;
  }

 protected:
  size_t write(uint8_t character) override {
    const char ch = static_cast<char>(character);
    buffer_ += ch;
    if (ch == '\n') {
      last_line_    = current_line_;
      current_line_ = "";
    }
    else if (ch != '\r') {
      current_line_ += ch;
    }
    return 1u;
  }

 private:
  String buffer_;
  String current_line_;
  String last_line_;
};

// ===================== Hook stubs ================================================

static LightReadingsStatisticSummary make_summary(uint32_t sample_count, double mean, double standard_deviation,
                                                  int32_t min_value, int32_t max_value, double drift_slope) {
  LightReadingsStatisticSummary summary = {};
  summary.sample_count                  = sample_count;
  summary.mean                          = mean;
  summary.standard_deviation            = standard_deviation;
  summary.min_value                     = min_value;
  summary.max_value                     = max_value;
  summary.drift_slope                   = drift_slope;
  summary.has_samples                   = true;
  return summary;
}

static LightReadingsSweepStats make_default_baseline_stats(void) {
  LightReadingsSweepStats stats = {};
  stats.sweep_count             = CLI_MEASUREMENT_SWEEP_COUNT;
  stats.dark_blue               = make_summary(4u, 1.25, 0.5, -2, 3, 0.1);
  stats.dark_green              = make_summary(4u, 2.5, 0.75, -1, 4, 0.2);
  stats.blue                    = make_summary(4u, 3.75, 1.25, 0, 6, 0.3);
  stats.green                   = make_summary(4u, 4.5, 1.5, 1, 7, 0.4);
  return stats;
}

static LightReadingsSweepStats make_default_sample_stats(void) {
  LightReadingsSweepStats stats = make_default_baseline_stats();
  stats.dark_blue.mean          = 1.0;
  stats.blue.mean               = 2.0;
  stats.dark_green.mean         = 2.0;
  stats.green.mean              = 3.25;
  return stats;
}

static void set_means(LightReadingsSweepStats* stats, double dark_blue, double blue, double dark_green, double green) {
  stats->dark_blue.mean  = dark_blue;
  stats->blue.mean       = blue;
  stats->dark_green.mean = dark_green;
  stats->green.mean      = green;
}

static RecordingPrint          g_recording_print;
static uint32_t                g_last_sweep_requested       = 0u;
static LightReadingsSweepStats g_stub_baseline_stats        = make_default_baseline_stats();
static LightReadingsSweepStats g_stub_sample_stats          = make_default_sample_stats();
static bool                    g_stub_use_sample_stats_next = false;
static float                   g_stub_temperature_c         = 24.0f;

// Monitor-mode stubs: fixed sweep codes, a key that arrives after N passes, and a rate recorder.
static LightReadingsSweepSample g_stub_monitor_sample   = {621, 223, 7570044, 7612940};
static uint32_t                 g_stub_monitor_passes   = 0u;
static uint32_t                 g_stub_key_after_passes = 2u;
static int                      g_stub_pending_bytes    = 0;
static int                      g_stub_rate_calls       = 0;
static bool                     g_stub_rate_last_fast   = false;

static int stub_sweep_n_success(uint32_t sweep_count, LightReadingsSweepCollection* results_out) {
  g_last_sweep_requested = sweep_count;
  if (results_out != NULL) {
    results_out->sweep_count = sweep_count;
  }
  return LIGHT_READINGS_OK;
}

static int stub_sweep_n_error(uint32_t sweep_count, LightReadingsSweepCollection* results_out) {
  g_last_sweep_requested = sweep_count;
  return LIGHT_READINGS_ERR_INVALID_ARG;
}

static int stub_sweep_success(LightReadingsSweepSample* result_out) {
  if (result_out == NULL) {
    return LIGHT_READINGS_ERR_INVALID_ARG;
  }
  *result_out = g_stub_monitor_sample;
  ++g_stub_monitor_passes;
  if (g_stub_monitor_passes >= g_stub_key_after_passes) {
    g_stub_pending_bytes = 2;  // A key plus its newline, both of which the monitor must swallow.
  }
  return LIGHT_READINGS_OK;
}

static int stub_compute_success(const LightReadingsSweepCollection* sweep_collection,
                                LightReadingsSweepStats*            stats_out) {
  if ((sweep_collection == NULL) || (stats_out == NULL)) {
    return LIGHT_READINGS_ERR_INVALID_ARG;
  }
  *stats_out                   = g_stub_use_sample_stats_next ? g_stub_sample_stats : g_stub_baseline_stats;
  stats_out->sweep_count       = sweep_collection->sweep_count;
  g_stub_use_sample_stats_next = false;
  return LIGHT_READINGS_OK;
}

static int stub_temperature_success(float* temperature_c_out) {
  if (temperature_c_out == NULL) {
    return THERMISTOR_READER_ERR_INVALID_ARG;
  }
  *temperature_c_out = g_stub_temperature_c;
  return THERMISTOR_READER_OK;
}

static int stub_temperature_error(float* temperature_c_out) {
  (void) temperature_c_out;
  return THERMISTOR_READER_ERR_NOT_INITIALIZED;
}

static int stub_read_input(void) {
  if (g_stub_pending_bytes > 0) {
    --g_stub_pending_bytes;
    return 'x';
  }
  return -1;
}

static int stub_set_monitor_rate(bool fast) {
  ++g_stub_rate_calls;
  g_stub_rate_last_fast = fast;
  return PHX_OK;
}

static const CliMeasurementHooks k_stub_hooks_success = {
    stub_sweep_n_success,     stub_sweep_success, stub_compute_success,
    stub_temperature_success, stub_read_input,    stub_set_monitor_rate,
};
static const CliMeasurementHooks k_stub_hooks_sweep_error = {
    stub_sweep_n_error,       stub_sweep_success, stub_compute_success,
    stub_temperature_success, stub_read_input,    stub_set_monitor_rate,
};
static const CliMeasurementHooks k_stub_hooks_temperature_error = {
    stub_sweep_n_success,   stub_sweep_success, stub_compute_success,
    stub_temperature_error, stub_read_input,    stub_set_monitor_rate,
};

void setUp(void) {
  cli_test_set_output(&g_recording_print);
  cli_initialize();
  cli_test_set_measurement_hooks(&k_stub_hooks_success);
  g_recording_print.reset();
  g_last_sweep_requested       = 0u;
  g_stub_baseline_stats        = make_default_baseline_stats();
  g_stub_sample_stats          = make_default_sample_stats();
  g_stub_use_sample_stats_next = false;
  g_stub_temperature_c         = 24.0f;
  g_stub_monitor_sample        = {621, 223, 7570044, 7612940};
  g_stub_monitor_passes        = 0u;
  g_stub_key_after_passes      = 2u;
  g_stub_pending_bytes         = 0;
  g_stub_rate_calls            = 0;
  g_stub_rate_last_fast        = false;
}

void tearDown(void) {
  cli_test_set_measurement_hooks(NULL);
  cli_test_set_output(NULL);
}

// ===================== Dispatch ==================================================

static void test_dispatch_rejects_empty_command(void) {
  TEST_ASSERT_EQUAL_INT(CLI_DISPATCH_EMPTY_COMMAND, cli_dispatch_command(""));
  TEST_ASSERT_EQUAL_INT(CLI_DISPATCH_EMPTY_COMMAND, cli_dispatch_command(NULL));
}

static void test_dispatch_rejects_unknown_command(void) {
  TEST_ASSERT_EQUAL_INT(CLI_DISPATCH_UNKNOWN_COMMAND, cli_dispatch_command("definitely_not_a_command"));
  TEST_ASSERT_TRUE(g_recording_print.last_line().startsWith("error\tunknown_command\t"));
}

static void test_help_lists_every_command(void) {
  TEST_ASSERT_EQUAL_INT(CLI_DISPATCH_OK, cli_dispatch_command("help"));
  const String output = g_recording_print.buffer();
  TEST_ASSERT_TRUE(output.indexOf("b\t") >= 0);
  TEST_ASSERT_TRUE(output.indexOf("s\t") >= 0);
  TEST_ASSERT_TRUE(output.indexOf("c\t") >= 0);
  TEST_ASSERT_TRUE(output.indexOf("v\t") >= 0);
  TEST_ASSERT_TRUE(output.indexOf("help\t") >= 0);
}

static void test_version_prints_firmware_name(void) {
  TEST_ASSERT_EQUAL_INT(CLI_DISPATCH_OK, cli_dispatch_command("v"));
  TEST_ASSERT_TRUE(g_recording_print.last_line().startsWith("phrog-cli "));
}

// ===================== Baseline and sample =======================================

static void test_baseline_caches_stats_with_configured_sweep_count(void) {
  TEST_ASSERT_FALSE(cli_test_is_baseline_cached());
  TEST_ASSERT_EQUAL_INT(CLI_DISPATCH_OK, cli_dispatch_command("b"));
  TEST_ASSERT_TRUE(cli_test_is_baseline_cached());
  TEST_ASSERT_EQUAL_UINT32(CLI_MEASUREMENT_SWEEP_COUNT, g_last_sweep_requested);

  LightReadingsSweepStats cached = {};
  cli_test_get_baseline_stats(&cached);
  TEST_ASSERT_EQUAL_UINT32(CLI_MEASUREMENT_SWEEP_COUNT, cached.sweep_count);
  TEST_ASSERT_EQUAL_UINT32(4u, cached.dark_blue.sample_count);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.4f, static_cast<float>(cached.green.drift_slope));

  const String output = g_recording_print.buffer();
  TEST_ASSERT_TRUE(output.indexOf("Taking baseline...") >= 0);
  TEST_ASSERT_TRUE(output.indexOf("dark_blue") >= 0);
  TEST_ASSERT_TRUE(output.indexOf("dark_green") >= 0);
}

static void test_baseline_failure_reports_error_and_leaves_no_cache(void) {
  cli_test_set_measurement_hooks(&k_stub_hooks_sweep_error);
  TEST_ASSERT_EQUAL_INT(CLI_DISPATCH_OK, cli_dispatch_command("b"));
  TEST_ASSERT_FALSE(cli_test_is_baseline_cached());
  TEST_ASSERT_TRUE(g_recording_print.last_line().startsWith("error\tsweep\t"));
}

static void test_sample_without_baseline_reports_missing(void) {
  TEST_ASSERT_EQUAL_INT(CLI_DISPATCH_OK, cli_dispatch_command("s"));
  TEST_ASSERT_TRUE(g_recording_print.last_line().startsWith("error\tmissing_baseline\t"));
}

static void test_sample_emits_stats_temperature_and_ph(void) {
  set_means(&g_stub_baseline_stats, 10.0, 110.0, 12.0, 212.0);
  set_means(&g_stub_sample_stats, 11.0, 61.0, 13.0, 93.0);
  g_stub_temperature_c = 24.25f;

  TEST_ASSERT_EQUAL_INT(CLI_DISPATCH_OK, cli_dispatch_command("b"));
  g_stub_use_sample_stats_next = true;
  g_recording_print.reset();
  TEST_ASSERT_EQUAL_INT(CLI_DISPATCH_OK, cli_dispatch_command("s"));
  TEST_ASSERT_TRUE(cli_test_is_baseline_cached());

  const String output = g_recording_print.buffer();
  TEST_ASSERT_TRUE(output.indexOf("Taking sample...") >= 0);
  TEST_ASSERT_TRUE(output.indexOf("temp_sample") >= 0);
  TEST_ASSERT_TRUE(output.indexOf("abs_blue") >= 0);
  TEST_ASSERT_TRUE(output.indexOf("pH") >= 0);

  // Expected pH from the same equations the CLI uses, with dark-corrected intensities.
  double absorbance_blue = 0.0, absorbance_green = 0.0, r_ratio = 0.0, expected_ph = 0.0;
  TEST_ASSERT_EQUAL_INT(PH_EQUATIONS_OK, ph_equations_calc_absorbance(100.0, 50.0, &absorbance_blue));
  TEST_ASSERT_EQUAL_INT(PH_EQUATIONS_OK, ph_equations_calc_absorbance(200.0, 80.0, &absorbance_green));
  TEST_ASSERT_EQUAL_INT(PH_EQUATIONS_OK, ph_equations_calc_r_ratio(absorbance_green, absorbance_blue, &r_ratio));
  TEST_ASSERT_EQUAL_INT(PH_EQUATIONS_OK, ph_equations_compute_ph(r_ratio, 24.25, 35.0, &expected_ph));

  String last_line = g_recording_print.last_line();
  last_line.trim();
  const int last_space = last_line.lastIndexOf(' ');
  TEST_ASSERT_TRUE(last_space > 0);
  String ph_field = last_line.substring(last_space + 1);
  ph_field.trim();
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, static_cast<float>(expected_ph), ph_field.toFloat());
}

static void test_sample_reports_temperature_error(void) {
  TEST_ASSERT_EQUAL_INT(CLI_DISPATCH_OK, cli_dispatch_command("b"));
  g_stub_use_sample_stats_next = true;
  cli_test_set_measurement_hooks(&k_stub_hooks_temperature_error);
  g_recording_print.reset();
  TEST_ASSERT_EQUAL_INT(CLI_DISPATCH_OK, cli_dispatch_command("s"));
  TEST_ASSERT_TRUE(g_recording_print.last_line().startsWith("error\ttemperature_sample\t"));
}

// ===================== Monitor mode ==============================================

static void test_monitor_prints_a_table_per_pass_until_a_key_arrives(void) {
  g_stub_key_after_passes = 3u;
  TEST_ASSERT_EQUAL_INT(CLI_DISPATCH_OK, cli_dispatch_command("c"));

  // Three passes ran, each printing all four rows, then the key stopped it.
  TEST_ASSERT_EQUAL_UINT32(3u, g_stub_monitor_passes);
  TEST_ASSERT_EQUAL_INT(3, g_recording_print.count("blue_led"));
  TEST_ASSERT_EQUAL_INT(3, g_recording_print.count("green_led"));
  TEST_ASSERT_EQUAL_INT(6, g_recording_print.count("dark"));
  // The key and its newline were both consumed so cli_poll will not see them as a command.
  TEST_ASSERT_EQUAL_INT(0, g_stub_pending_bytes);
}

static void test_monitor_switches_adc_rate_up_then_back(void) {
  TEST_ASSERT_EQUAL_INT(CLI_DISPATCH_OK, cli_dispatch_command("c"));
  TEST_ASSERT_EQUAL_INT(2, g_stub_rate_calls);
  TEST_ASSERT_FALSE(g_stub_rate_last_fast);
}

static void test_monitor_verdicts_follow_the_target_window(void) {
  // 90% (inside the window) and 93%: blue GOOD, green HIGH.
  g_stub_monitor_sample   = {621, 223, 7570044, 7800000};
  g_stub_key_after_passes = 1u;
  TEST_ASSERT_EQUAL_INT(CLI_DISPATCH_OK, cli_dispatch_command("c"));
  String output = g_recording_print.buffer();
  TEST_ASSERT_TRUE(output.indexOf("GOOD") >= 0);
  TEST_ASSERT_TRUE(output.indexOf("HIGH") >= 0);
  TEST_ASSERT_TRUE(output.indexOf("turn RV2 down") >= 0);

  // Clipped blue and dim green.
  g_recording_print.reset();
  g_stub_monitor_sample = {621, 223, 8388606, 800000};
  g_stub_monitor_passes = 0u;
  g_stub_pending_bytes  = 0;
  TEST_ASSERT_EQUAL_INT(CLI_DISPATCH_OK, cli_dispatch_command("c"));
  output = g_recording_print.buffer();
  TEST_ASSERT_TRUE(output.indexOf("SATURATED") >= 0);
  TEST_ASSERT_TRUE(output.indexOf("turn RV1 down a lot") >= 0);
  TEST_ASSERT_TRUE(output.indexOf("LOW") >= 0);
  TEST_ASSERT_TRUE(output.indexOf("turn RV2 up") >= 0);
}

static void test_monitor_reports_sweep_error_and_restores_rate(void) {
  CliMeasurementHooks hooks = k_stub_hooks_success;
  hooks.sweep               = [](LightReadingsSweepSample* result_out) -> int {
    (void) result_out;
    return LIGHT_READINGS_ERR_TIMEOUT;
  };
  cli_test_set_measurement_hooks(&hooks);
  TEST_ASSERT_EQUAL_INT(CLI_DISPATCH_OK, cli_dispatch_command("c"));
  TEST_ASSERT_TRUE(g_recording_print.buffer().indexOf("error\tsweep\t") >= 0);
  TEST_ASSERT_EQUAL_INT(2, g_stub_rate_calls);
  TEST_ASSERT_FALSE(g_stub_rate_last_fast);
}

void setup() {
  UNITY_SETUP_SERIAL_DEFAULT();
  RUN_TEST(test_dispatch_rejects_empty_command);
  RUN_TEST(test_dispatch_rejects_unknown_command);
  RUN_TEST(test_help_lists_every_command);
  RUN_TEST(test_version_prints_firmware_name);
  RUN_TEST(test_baseline_caches_stats_with_configured_sweep_count);
  RUN_TEST(test_baseline_failure_reports_error_and_leaves_no_cache);
  RUN_TEST(test_sample_without_baseline_reports_missing);
  RUN_TEST(test_sample_emits_stats_temperature_and_ph);
  RUN_TEST(test_sample_reports_temperature_error);
  RUN_TEST(test_monitor_prints_a_table_per_pass_until_a_key_arrives);
  RUN_TEST(test_monitor_switches_adc_rate_up_then_back);
  RUN_TEST(test_monitor_verdicts_follow_the_target_window);
  RUN_TEST(test_monitor_reports_sweep_error_and_restores_rate);
  UNITY_END();
}

void loop() {
  // Unity tests run once; leave loop empty.
}
