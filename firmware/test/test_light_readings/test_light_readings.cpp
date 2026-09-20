#include "device_setup.hpp"
#include "led_driver.hpp"
#include "light_readings.hpp"
#include "nau7802.hpp"
#include "unity_config.h"
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>
#include <Wire.h>
#include <stdio.h>
#include <unity.h>

// Codes a lit LED must add over its own dark reading: about 2 mV of the 1.5 V range.
static const int32_t k_light_margin_codes = 10000;

// 80 SPS keeps the hardware sweeps short; the board config's timeout is generous enough for it.
static const Nau7802Settings k_test_adc_settings = {
    Nau7802Gain::NAU7802_GAIN_X1,
    Nau7802SampleRate::NAU7802_SPS_80,
    Nau7802Ldo::NAU7802_LDO_3V0,
    Nau7802Channel::NAU7802_CHANNEL_1,
    false,
};

// Brings up the two drivers a sweep depends on, then the helper itself, with the board config.
static void initialize_measurement_chain(void) {
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_initialize(&g_device_led_driver_config));
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_initialize(&g_device_nau7802_config));
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_apply_settings(&k_test_adc_settings));
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_initialize(&g_device_light_readings_config));
}

static void report_sample(const char* label, const LightReadingsSweepSample& sample) {
  char message[128];
  snprintf(message, sizeof(message), "%s: dark_blue=%ld blue=%ld dark_green=%ld green=%ld%s", label,
           static_cast<long>(sample.dark_blue_code), static_cast<long>(sample.blue_code),
           static_cast<long>(sample.dark_green_code), static_cast<long>(sample.green_code),
           light_readings_last_sweep_detected_saturation() ? " SATURATED" : "");
  TEST_MESSAGE(message);
}

void setUp(void) {
  light_readings_reset_for_test();
  nau7802_reset_for_test();
  led_driver_reset_for_test();
}

void tearDown(void) {
  (void) light_readings_shutdown();
  (void) led_driver_shutdown();
  (void) nau7802_shutdown();
  light_readings_reset_for_test();
  nau7802_reset_for_test();
  led_driver_reset_for_test();
}

// ===================== Guards ===================================================

static void test_initialize_rejects_null_and_zero_timeout(void) {
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_INVALID_ARG, light_readings_initialize(NULL));
  LightReadingsConfig config = g_device_light_readings_config;
  config.adc_timeout_us      = 0u;
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_INVALID_ARG, light_readings_initialize(&config));
}

static void test_sweep_requires_initialization_and_output(void) {
  LightReadingsSweepSample sample = {};
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_NOT_INITIALIZED, light_readings_sweep(&sample));
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_initialize(&g_device_light_readings_config));
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_INVALID_ARG, light_readings_sweep(NULL));
}

static void test_sweep_n_validates_count_and_storage(void) {
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_initialize(&g_device_light_readings_config));
  LightReadingsSweepCollection collection = {0u, g_light_readings_sweep_storage};

  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_INVALID_ARG, light_readings_sweep_n(1u, NULL));
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_INVALID_ARG, light_readings_sweep_n(0u, &collection));
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_SWEEP_CAPACITY_EXCEEDED,
                        light_readings_sweep_n(LIGHT_READINGS_MAX_SWEEP_COUNT + 1u, &collection));

  LightReadingsSweepCollection no_storage = {0u, NULL};
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_INVALID_ARG, light_readings_sweep_n(1u, &no_storage));
}

static void test_sweep_propagates_missing_adc(void) {
  // LED driver up, ADC not: the sweep must fail with the ADC's own code and leave the LEDs off.
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_initialize(&g_device_led_driver_config));
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_initialize(&g_device_light_readings_config));
  LightReadingsSweepSample sample = {};
  TEST_ASSERT_EQUAL_INT(NAU7802_ERR_NOT_INITIALIZED, light_readings_sweep(&sample));
  LedDriverChannel channel = LedDriverChannel::LED_DRIVER_CHANNEL_GREEN;
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_get_channel(&channel));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(LedDriverChannel::LED_DRIVER_CHANNEL_OFF), static_cast<int>(channel));
}

// ===================== Statistics (pure) =========================================

static void test_compute_sweep_stats_requires_arguments(void) {
  LightReadingsSweepCollection collection = {0u, g_light_readings_sweep_storage};
  LightReadingsSweepStats      stats      = {};
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_INVALID_ARG, light_readings_compute_sweep_stats(NULL, &stats));
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_INVALID_ARG, light_readings_compute_sweep_stats(&collection, NULL));
  LightReadingsSweepCollection no_storage = {2u, NULL};
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_INVALID_ARG, light_readings_compute_sweep_stats(&no_storage, &stats));
}

static void test_compute_sweep_stats_handles_empty_collection(void) {
  LightReadingsSweepCollection collection = {0u, NULL};
  LightReadingsSweepStats      stats      = {};
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_compute_sweep_stats(&collection, &stats));
  TEST_ASSERT_EQUAL_UINT32(0u, stats.sweep_count);
  TEST_ASSERT_FALSE(stats.dark_blue.has_samples);
  TEST_ASSERT_FALSE(stats.green.has_samples);
  TEST_ASSERT_EQUAL_UINT32(0u, stats.blue.sample_count);
  TEST_ASSERT_EQUAL_INT32(0, stats.dark_green.min_value);
  TEST_ASSERT_EQUAL_INT32(0, stats.dark_green.max_value);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, static_cast<float>(stats.blue.mean));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, static_cast<float>(stats.blue.standard_deviation));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, static_cast<float>(stats.blue.drift_slope));
}

static void test_compute_sweep_stats_calculates_metrics(void) {
  // Same vectors Phoenix uses: a 1000/2000/3000 ramp on every field, with the green field flat.
  LightReadingsSweepSample samples[3] = {
      {1000, 10, 100, 500},
      {2000, 20, 200, 500},
      {3000, 30, 300, 500},
  };
  LightReadingsSweepCollection collection = {3u, samples};
  LightReadingsSweepStats      stats      = {};
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_compute_sweep_stats(&collection, &stats));

  TEST_ASSERT_EQUAL_UINT32(3u, stats.sweep_count);
  TEST_ASSERT_TRUE(stats.dark_blue.has_samples);
  TEST_ASSERT_EQUAL_UINT32(3u, stats.dark_blue.sample_count);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2000.0f, static_cast<float>(stats.dark_blue.mean));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1000.0f, static_cast<float>(stats.dark_blue.standard_deviation));
  TEST_ASSERT_EQUAL_INT32(1000, stats.dark_blue.min_value);
  TEST_ASSERT_EQUAL_INT32(3000, stats.dark_blue.max_value);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1000.0f, static_cast<float>(stats.dark_blue.drift_slope));

  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 20.0f, static_cast<float>(stats.dark_green.mean));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 10.0f, static_cast<float>(stats.dark_green.drift_slope));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 200.0f, static_cast<float>(stats.blue.mean));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 100.0f, static_cast<float>(stats.blue.standard_deviation));

  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 500.0f, static_cast<float>(stats.green.mean));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, static_cast<float>(stats.green.standard_deviation));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, static_cast<float>(stats.green.drift_slope));
  TEST_ASSERT_EQUAL_INT32(500, stats.green.min_value);
  TEST_ASSERT_EQUAL_INT32(500, stats.green.max_value);
}

static void test_compute_sweep_stats_single_sample_has_zero_spread(void) {
  LightReadingsSweepSample     samples[1] = {{-5, 7, 123456, 654321}};
  LightReadingsSweepCollection collection = {1u, samples};
  LightReadingsSweepStats      stats      = {};
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_compute_sweep_stats(&collection, &stats));
  TEST_ASSERT_TRUE(stats.blue.has_samples);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 123456.0f, static_cast<float>(stats.blue.mean));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, static_cast<float>(stats.blue.standard_deviation));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, static_cast<float>(stats.blue.drift_slope));
  TEST_ASSERT_EQUAL_INT32(-5, stats.dark_blue.min_value);
  TEST_ASSERT_EQUAL_INT32(-5, stats.dark_blue.max_value);
}

// ===================== Hardware sweeps ============================================

static void test_sweep_lights_each_photodiode_and_parks_leds(void) {
  initialize_measurement_chain();
  LightReadingsSweepSample sample = {};
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_sweep(&sample));
  report_sample("sweep", sample);

  TEST_ASSERT_GREATER_THAN_INT32(sample.dark_blue_code + k_light_margin_codes, sample.blue_code);
  TEST_ASSERT_GREATER_THAN_INT32(sample.dark_green_code + k_light_margin_codes, sample.green_code);

  LedDriverChannel channel = LedDriverChannel::LED_DRIVER_CHANNEL_GREEN;
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_get_channel(&channel));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(LedDriverChannel::LED_DRIVER_CHANNEL_OFF), static_cast<int>(channel));
}

static void test_sweep_saturation_flag_matches_codes(void) {
  initialize_measurement_chain();
  LightReadingsSweepSample sample = {};
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_sweep(&sample));
  const bool any_clipped = (sample.dark_blue_code >= LIGHT_READINGS_SATURATION_POSITIVE_CODE) ||
                           (sample.dark_green_code >= LIGHT_READINGS_SATURATION_POSITIVE_CODE) ||
                           (sample.blue_code >= LIGHT_READINGS_SATURATION_POSITIVE_CODE) ||
                           (sample.green_code >= LIGHT_READINGS_SATURATION_POSITIVE_CODE) ||
                           (sample.dark_blue_code <= LIGHT_READINGS_SATURATION_NEGATIVE_CODE) ||
                           (sample.dark_green_code <= LIGHT_READINGS_SATURATION_NEGATIVE_CODE) ||
                           (sample.blue_code <= LIGHT_READINGS_SATURATION_NEGATIVE_CODE) ||
                           (sample.green_code <= LIGHT_READINGS_SATURATION_NEGATIVE_CODE);
  TEST_ASSERT_EQUAL(any_clipped, light_readings_last_sweep_detected_saturation());
}

static void test_sweep_n_fills_collection_and_feeds_stats(void) {
  initialize_measurement_chain();
  LightReadingsSweepCollection collection = {0u, g_light_readings_sweep_storage};
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_sweep_n(3u, &collection));
  TEST_ASSERT_EQUAL_UINT32(3u, collection.sweep_count);
  report_sample("sweep_n[2]", collection.sweeps[2]);

  LightReadingsSweepStats stats = {};
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_compute_sweep_stats(&collection, &stats));
  TEST_ASSERT_EQUAL_UINT32(3u, stats.sweep_count);
  TEST_ASSERT_EQUAL_UINT32(3u, stats.blue.sample_count);
  TEST_ASSERT_EQUAL_UINT32(3u, stats.green.sample_count);
  TEST_ASSERT_TRUE(stats.blue.mean > stats.dark_blue.mean);
  TEST_ASSERT_TRUE(stats.green.mean > stats.dark_green.mean);
}

static void test_shutdown_parks_leds_and_requires_reinitialization(void) {
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_NOT_INITIALIZED, light_readings_shutdown());
  initialize_measurement_chain();
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_set_channel(LedDriverChannel::LED_DRIVER_CHANNEL_BLUE));

  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_OK, light_readings_shutdown());
  LedDriverChannel channel = LedDriverChannel::LED_DRIVER_CHANNEL_BLUE;
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_get_channel(&channel));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(LedDriverChannel::LED_DRIVER_CHANNEL_OFF), static_cast<int>(channel));

  LightReadingsSweepSample sample = {};
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_NOT_INITIALIZED, light_readings_sweep(&sample));
}

void setup() {
  UNITY_SETUP_SERIAL_DEFAULT();
  Wire.begin();
  RUN_TEST(test_initialize_rejects_null_and_zero_timeout);
  RUN_TEST(test_sweep_requires_initialization_and_output);
  RUN_TEST(test_sweep_n_validates_count_and_storage);
  RUN_TEST(test_sweep_propagates_missing_adc);
  RUN_TEST(test_compute_sweep_stats_requires_arguments);
  RUN_TEST(test_compute_sweep_stats_handles_empty_collection);
  RUN_TEST(test_compute_sweep_stats_calculates_metrics);
  RUN_TEST(test_compute_sweep_stats_single_sample_has_zero_spread);
  RUN_TEST(test_sweep_lights_each_photodiode_and_parks_leds);
  RUN_TEST(test_sweep_saturation_flag_matches_codes);
  RUN_TEST(test_sweep_n_fills_collection_and_feeds_stats);
  RUN_TEST(test_shutdown_parks_leds_and_requires_reinitialization);
  UNITY_END();
}

void loop() {
  // Unity tests run once; leave loop empty.
}
