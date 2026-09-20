#include "device_setup.hpp"
#include "thermistor_reader.hpp"
#include "unity_config.h"
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>
#include <stdio.h>
#include <unity.h>

// ===================== Hook stubs ================================================
// The stubs record the GPIO sequence and feed a chosen SAADC code so the measurement path can be
// checked without depending on the room temperature. The real-hardware case at the end uses the
// production hooks.

static const size_t k_max_recorded_calls = 32u;

struct RecordedCall {
  char     kind;  // 'm' pin_mode, 'w' digital_write, 'd' delay_us, 'r' analog_read
  int      pin;
  uint32_t value;
};

static RecordedCall g_calls[k_max_recorded_calls];
static size_t       g_call_count    = 0u;
static int32_t      g_stub_adc_code = 0;

static void record(char kind, int pin, uint32_t value) {
  if (g_call_count < k_max_recorded_calls) {
    g_calls[g_call_count].kind  = kind;
    g_calls[g_call_count].pin   = pin;
    g_calls[g_call_count].value = value;
  }
  ++g_call_count;
}

static void stub_pin_mode(int pin, uint32_t mode) {
  record('m', pin, mode);
}

static void stub_digital_write(int pin, uint32_t value) {
  record('w', pin, value);
}

static void stub_delay_us(uint32_t delay_us) {
  record('d', -1, delay_us);
}

static int32_t stub_analog_read(int pin) {
  record('r', pin, static_cast<uint32_t>(g_stub_adc_code));
  return g_stub_adc_code;
}

static void install_stubs(void) {
  thermistor_reader_set_hooks_for_test(stub_pin_mode, stub_digital_write, stub_delay_us, stub_analog_read);
}

// Counts recorded calls of one kind so tests can assert on the sequence without fragile indices.
static size_t count_calls(char kind) {
  size_t count = 0u;
  for (size_t index = 0u; (index < g_call_count) && (index < k_max_recorded_calls); ++index) {
    if (g_calls[index].kind == kind) {
      ++count;
    }
  }
  return count;
}

// Returns the value of the last recorded call of one kind, or 0xFFFFFFFF when there is none.
static uint32_t last_call_value(char kind) {
  uint32_t value = 0xFFFFFFFFu;
  for (size_t index = 0u; (index < g_call_count) && (index < k_max_recorded_calls); ++index) {
    if (g_calls[index].kind == kind) {
      value = g_calls[index].value;
    }
  }
  return value;
}

void setUp(void) {
  thermistor_reader_reset_for_test();
  g_call_count    = 0u;
  g_stub_adc_code = 0;
}

void tearDown(void) {
  thermistor_reader_reset_for_test();
}

// ===================== Guards ===================================================

static void test_initialize_rejects_null_config(void) {
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_ERR_INVALID_ARG, thermistor_reader_initialize(NULL));
}

static void test_initialize_rejects_unusable_config(void) {
  ThermistorReaderConfig config = g_device_thermistor_reader_config;
  config.signal_pin             = -1;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_ERR_INVALID_ARG, thermistor_reader_initialize(&config));

  config                        = g_device_thermistor_reader_config;
  config.series_resistance_ohms = 0u;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_ERR_INVALID_ARG, thermistor_reader_initialize(&config));

  config          = g_device_thermistor_reader_config;
  config.r25_ohms = 0.0f;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_ERR_INVALID_ARG, thermistor_reader_initialize(&config));
}

static void test_measure_requires_initialization_and_output(void) {
  float temperature_c = 0.0f;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_ERR_NOT_INITIALIZED, thermistor_reader_measure_celsius(&temperature_c));
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&g_device_thermistor_reader_config));
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_ERR_INVALID_ARG, thermistor_reader_measure_celsius(NULL));
}

// ===================== Pure conversions ==========================================

static void test_code_to_resistance_is_ratiometric(void) {
  float resistance = 0.0f;
  // Half scale means the thermistor equals the series resistor.
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_code_to_resistance(
                                                  THERMISTOR_READER_ADC_FULL_SCALE_CODE / 2, 10000u, &resistance));
  TEST_ASSERT_FLOAT_WITHIN(5.0f, 10000.0f, resistance);

  // One quarter scale: R = 10k * 0.25 / 0.75.
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_code_to_resistance(
                                                  THERMISTOR_READER_ADC_FULL_SCALE_CODE / 4, 10000u, &resistance));
  TEST_ASSERT_FLOAT_WITHIN(5.0f, 3333.3f, resistance);
}

static void test_code_to_resistance_rejects_rails_and_null(void) {
  float resistance = 0.0f;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_ERR_INVALID_ARG, thermistor_reader_code_to_resistance(100, 10000u, NULL));
  // Code 0 is a shorted thermistor; full scale is an open one (division by zero).
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_ERR_COMPUTE_FAILURE,
                        thermistor_reader_code_to_resistance(0, 10000u, &resistance));
  TEST_ASSERT_EQUAL_INT(
      THERMISTOR_READER_ERR_COMPUTE_FAILURE,
      thermistor_reader_code_to_resistance(THERMISTOR_READER_ADC_FULL_SCALE_CODE, 10000u, &resistance));
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_ERR_COMPUTE_FAILURE,
                        thermistor_reader_code_to_resistance(-5, 10000u, &resistance));
}

static void test_steinhart_hart_matches_reference_points(void) {
  // Reference points evaluated from the Phoenix coefficient table: the segment boundaries land on
  // 0, 50, and 100 C and R = R25 is 25 C by definition.
  float temperature_c = 0.0f;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK,
                        thermistor_reader_convert_steinhart_hart_temperature(10000.0f, 10000.0f, 0.0f, &temperature_c));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 25.0f, temperature_c);

  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK,
                        thermistor_reader_convert_steinhart_hart_temperature(32740.0f, 10000.0f, 0.0f, &temperature_c));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.0f, temperature_c);

  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK,
                        thermistor_reader_convert_steinhart_hart_temperature(3603.6f, 10000.0f, 0.0f, &temperature_c));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 50.0f, temperature_c);

  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK,
                        thermistor_reader_convert_steinhart_hart_temperature(683.1f, 10000.0f, 0.0f, &temperature_c));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 100.0f, temperature_c);

  // Mid-segment values from the same table.
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK,
                        thermistor_reader_convert_steinhart_hart_temperature(5000.0f, 10000.0f, 0.0f, &temperature_c));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 41.58f, temperature_c);
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK,
                        thermistor_reader_convert_steinhart_hart_temperature(20000.0f, 10000.0f, 0.0f, &temperature_c));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 9.92f, temperature_c);
}

static void test_steinhart_hart_applies_offset_and_rejects_bad_inputs(void) {
  float temperature_c = 0.0f;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_convert_steinhart_hart_temperature(
                                                  10000.0f, 10000.0f, -0.75f, &temperature_c));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 24.25f, temperature_c);

  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_ERR_INVALID_ARG,
                        thermistor_reader_convert_steinhart_hart_temperature(10000.0f, 10000.0f, 0.0f, NULL));
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_ERR_COMPUTE_FAILURE,
                        thermistor_reader_convert_steinhart_hart_temperature(0.0f, 10000.0f, 0.0f, &temperature_c));
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_ERR_COMPUTE_FAILURE,
                        thermistor_reader_convert_steinhart_hart_temperature(10000.0f, 0.0f, 0.0f, &temperature_c));
}

// ===================== Measurement sequence (stubbed GPIO/ADC) ====================

static void test_initialize_parks_drive_pin_low_as_output(void) {
  install_stubs();
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&g_device_thermistor_reader_config));

  TEST_ASSERT_EQUAL_UINT(1u, count_calls('m'));
  TEST_ASSERT_EQUAL_INT(PIN_THERMISTOR_DRIVE, g_calls[0].pin);
  TEST_ASSERT_EQUAL_UINT32(OUTPUT, g_calls[0].value);
  TEST_ASSERT_EQUAL_UINT(1u, count_calls('w'));
  TEST_ASSERT_EQUAL_UINT32(LOW, last_call_value('w'));
}

static void test_measure_drives_settles_samples_then_parks(void) {
  install_stubs();
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&g_device_thermistor_reader_config));
  g_call_count    = 0u;
  g_stub_adc_code = THERMISTOR_READER_ADC_FULL_SCALE_CODE / 2;  // R = R25 -> 25 C

  float temperature_c = 0.0f;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_measure_celsius(&temperature_c));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 25.0f, temperature_c);

  // Sequence: drive high, settle, N reads on the signal pin, drive low.
  TEST_ASSERT_EQUAL_INT('w', g_calls[0].kind);
  TEST_ASSERT_EQUAL_INT(PIN_THERMISTOR_DRIVE, g_calls[0].pin);
  TEST_ASSERT_EQUAL_UINT32(HIGH, g_calls[0].value);
  TEST_ASSERT_EQUAL_INT('d', g_calls[1].kind);
  TEST_ASSERT_EQUAL_UINT32(g_device_thermistor_reader_config.settle_time_us, g_calls[1].value);
  TEST_ASSERT_EQUAL_UINT(THERMISTOR_READER_SAMPLE_COUNT, count_calls('r'));
  TEST_ASSERT_EQUAL_INT(PIN_THERMISTOR_SIGNAL, g_calls[2].pin);
  TEST_ASSERT_EQUAL_INT('w', g_calls[g_call_count - 1u].kind);
  TEST_ASSERT_EQUAL_UINT32(LOW, g_calls[g_call_count - 1u].value);

  float resistance = 0.0f;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_get_last_resistance_for_test(&resistance));
  TEST_ASSERT_FLOAT_WITHIN(5.0f, 10000.0f, resistance);
}

static void test_measure_reports_open_thermistor_and_still_parks_drive(void) {
  install_stubs();
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&g_device_thermistor_reader_config));
  g_call_count    = 0u;
  g_stub_adc_code = THERMISTOR_READER_ADC_FULL_SCALE_CODE;  // Signal pin sits at the drive rail.

  float temperature_c = 0.0f;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_ERR_COMPUTE_FAILURE, thermistor_reader_measure_celsius(&temperature_c));
  TEST_ASSERT_EQUAL_INT('w', g_calls[g_call_count - 1u].kind);
  TEST_ASSERT_EQUAL_UINT32(LOW, g_calls[g_call_count - 1u].value);
}

static void test_measure_without_drive_pin_skips_gpio(void) {
  install_stubs();
  ThermistorReaderConfig config = g_device_thermistor_reader_config;
  config.drive_pin              = -1;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&config));
  g_call_count    = 0u;
  g_stub_adc_code = THERMISTOR_READER_ADC_FULL_SCALE_CODE / 2;

  float temperature_c = 0.0f;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_measure_celsius(&temperature_c));
  TEST_ASSERT_EQUAL_UINT(0u, count_calls('w'));
  TEST_ASSERT_EQUAL_UINT(THERMISTOR_READER_SAMPLE_COUNT, count_calls('r'));
}

// ===================== Real hardware ==============================================

static void test_board_thermistor_reads_a_plausible_room_temperature(void) {
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_OK, thermistor_reader_initialize(&g_device_thermistor_reader_config));

  float     temperature_c = 0.0f;
  const int return_code   = thermistor_reader_measure_celsius(&temperature_c);
  float     resistance    = 0.0f;
  (void) thermistor_reader_get_last_resistance_for_test(&resistance);

  char message[96];
  snprintf(message, sizeof(message), "thermistor: rc=%d resistance=%ld ohm temperature_x100=%ld", return_code,
           static_cast<long>(resistance), static_cast<long>(temperature_c * 100.0f));
  TEST_MESSAGE(message);

  TEST_ASSERT_EQUAL_INT_MESSAGE(THERMISTOR_READER_OK, return_code, "divider open or shorted: is TH1 fitted?");
  TEST_ASSERT_FLOAT_WITHIN(20.0f, 25.0f, temperature_c);  // Anything a bench could plausibly be.

  // The drive pin must be back at rest so the thermistor is not self-heating between reads.
  TEST_ASSERT_EQUAL_INT(LOW, digitalRead(PIN_THERMISTOR_DRIVE));
}

void setup() {
  UNITY_SETUP_SERIAL_DEFAULT();
  RUN_TEST(test_initialize_rejects_null_config);
  RUN_TEST(test_initialize_rejects_unusable_config);
  RUN_TEST(test_measure_requires_initialization_and_output);
  RUN_TEST(test_code_to_resistance_is_ratiometric);
  RUN_TEST(test_code_to_resistance_rejects_rails_and_null);
  RUN_TEST(test_steinhart_hart_matches_reference_points);
  RUN_TEST(test_steinhart_hart_applies_offset_and_rejects_bad_inputs);
  RUN_TEST(test_initialize_parks_drive_pin_low_as_output);
  RUN_TEST(test_measure_drives_settles_samples_then_parks);
  RUN_TEST(test_measure_reports_open_thermistor_and_still_parks_drive);
  RUN_TEST(test_measure_without_drive_pin_skips_gpio);
  RUN_TEST(test_board_thermistor_reads_a_plausible_room_temperature);
  UNITY_END();
}

void loop() {
  // Unity tests run once; leave loop empty.
}
