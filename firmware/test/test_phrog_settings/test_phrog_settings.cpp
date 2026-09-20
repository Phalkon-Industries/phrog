#include "device_setup.hpp"
#include "phrog_settings.hpp"
#include "thermistor_reader.hpp"
#include "unity_config.h"
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>
#include <string.h>
#include <unity.h>

// Two distinguishable default sets so a test can tell "loaded from flash" from "took the defaults".
static const PhrogSettings k_defaults_a = {5u, 0.0f, {0}};
static const PhrogSettings k_defaults_b = {9u, -1.5f, {0}};

static void assert_settings_equal(const PhrogSettings& expected, const PhrogSettings* actual) {
  TEST_ASSERT_NOT_NULL(actual);
  TEST_ASSERT_EQUAL_UINT16(expected.measurement_sweep_count, actual->measurement_sweep_count);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, expected.thermistor_calibration_offset_c, actual->thermistor_calibration_offset_c);
}

void setUp(void) {
  phrog_settings_deinitialize();
  TEST_ASSERT_EQUAL_INT(PHROG_SETTINGS_OK, phrog_settings_erase_for_test());
}

void tearDown(void) {
  phrog_settings_deinitialize();
  (void) phrog_settings_erase_for_test();
}

// ===================== Guards ===================================================

static void test_initialize_rejects_null_defaults(void) {
  TEST_ASSERT_EQUAL_INT(PHROG_SETTINGS_ERR_INVALID_ARG, phrog_settings_initialize(NULL));
  TEST_ASSERT_FALSE(phrog_settings_is_initialized());
}

static void test_get_and_save_require_initialization(void) {
  TEST_ASSERT_NULL(phrog_settings_get());
  TEST_ASSERT_EQUAL_INT(PHROG_SETTINGS_ERR_NOT_INITIALIZED, phrog_settings_save(&k_defaults_a));
  TEST_ASSERT_EQUAL_INT(PHROG_SETTINGS_ERR_NOT_INITIALIZED, phrog_settings_reset_to_defaults());
  TEST_ASSERT_EQUAL_INT(PHROG_SETTINGS_OK, phrog_settings_initialize(&k_defaults_a));
  TEST_ASSERT_EQUAL_INT(PHROG_SETTINGS_ERR_INVALID_ARG, phrog_settings_save(NULL));
}

// ===================== Flash round trips ==========================================

static void test_initialize_with_no_file_takes_defaults_and_creates_the_file(void) {
  TEST_ASSERT_EQUAL_INT(PHROG_SETTINGS_OK, phrog_settings_initialize(&k_defaults_a));
  TEST_ASSERT_TRUE(phrog_settings_is_initialized());
  assert_settings_equal(k_defaults_a, phrog_settings_get());

  // A second initialise with different defaults must return what was stored, not the new defaults.
  phrog_settings_deinitialize();
  TEST_ASSERT_EQUAL_INT(PHROG_SETTINGS_OK, phrog_settings_initialize(&k_defaults_b));
  assert_settings_equal(k_defaults_a, phrog_settings_get());
}

static void test_save_persists_across_reinitialization(void) {
  TEST_ASSERT_EQUAL_INT(PHROG_SETTINGS_OK, phrog_settings_initialize(&k_defaults_a));
  PhrogSettings custom = {42u, 0.75f, {0}};
  TEST_ASSERT_EQUAL_INT(PHROG_SETTINGS_OK, phrog_settings_save(&custom));
  assert_settings_equal(custom, phrog_settings_get());

  phrog_settings_deinitialize();
  TEST_ASSERT_NULL(phrog_settings_get());
  TEST_ASSERT_EQUAL_INT(PHROG_SETTINGS_OK, phrog_settings_initialize(&k_defaults_b));
  assert_settings_equal(custom, phrog_settings_get());
}

static void test_save_clamps_sweep_count(void) {
  TEST_ASSERT_EQUAL_INT(PHROG_SETTINGS_OK, phrog_settings_initialize(&k_defaults_a));
  PhrogSettings too_many = {60000u, 0.0f, {0}};
  TEST_ASSERT_EQUAL_INT(PHROG_SETTINGS_OK, phrog_settings_save(&too_many));
  TEST_ASSERT_EQUAL_UINT16(PHROG_SETTINGS_MAX_SWEEP_COUNT, phrog_settings_get()->measurement_sweep_count);
  PhrogSettings zero = {0u, 0.0f, {0}};
  TEST_ASSERT_EQUAL_INT(PHROG_SETTINGS_OK, phrog_settings_save(&zero));
  TEST_ASSERT_EQUAL_UINT16(PHROG_SETTINGS_MIN_SWEEP_COUNT, phrog_settings_get()->measurement_sweep_count);
}

static void test_reset_to_defaults_restores_and_persists(void) {
  TEST_ASSERT_EQUAL_INT(PHROG_SETTINGS_OK, phrog_settings_initialize(&k_defaults_a));
  PhrogSettings custom = {42u, 0.75f, {0}};
  TEST_ASSERT_EQUAL_INT(PHROG_SETTINGS_OK, phrog_settings_save(&custom));
  TEST_ASSERT_EQUAL_INT(PHROG_SETTINGS_OK, phrog_settings_reset_to_defaults());
  assert_settings_equal(k_defaults_a, phrog_settings_get());

  phrog_settings_deinitialize();
  TEST_ASSERT_EQUAL_INT(PHROG_SETTINGS_OK, phrog_settings_initialize(&k_defaults_b));
  assert_settings_equal(k_defaults_a, phrog_settings_get());
}

// ===================== Invalid files ==============================================

static void test_wrong_version_file_falls_back_to_defaults(void) {
  uint8_t image[1 + sizeof(PhrogSettings)];
  memset(image, 0, sizeof(image));
  image[0] = static_cast<uint8_t>(PHROG_SETTINGS_FORMAT_VERSION + 1u);
  memcpy(&image[1], &k_defaults_b, sizeof(PhrogSettings));
  TEST_ASSERT_EQUAL_INT(PHROG_SETTINGS_OK, phrog_settings_write_raw_for_test(image, sizeof(image)));

  TEST_ASSERT_EQUAL_INT(PHROG_SETTINGS_OK, phrog_settings_initialize(&k_defaults_a));
  assert_settings_equal(k_defaults_a, phrog_settings_get());

  // The bad file was replaced, so a re-initialise now loads the defaults from flash.
  phrog_settings_deinitialize();
  TEST_ASSERT_EQUAL_INT(PHROG_SETTINGS_OK, phrog_settings_initialize(&k_defaults_b));
  assert_settings_equal(k_defaults_a, phrog_settings_get());
}

static void test_short_file_falls_back_to_defaults(void) {
  const uint8_t truncated[3] = {PHROG_SETTINGS_FORMAT_VERSION, 0x2Au, 0x00u};
  TEST_ASSERT_EQUAL_INT(PHROG_SETTINGS_OK, phrog_settings_write_raw_for_test(truncated, sizeof(truncated)));
  TEST_ASSERT_EQUAL_INT(PHROG_SETTINGS_OK, phrog_settings_initialize(&k_defaults_a));
  assert_settings_equal(k_defaults_a, phrog_settings_get());
}

static void test_stored_sweep_count_is_clamped_on_load(void) {
  uint8_t       image[1 + sizeof(PhrogSettings)];
  PhrogSettings out_of_range = {0u, 0.0f, {0}};
  image[0]                   = PHROG_SETTINGS_FORMAT_VERSION;
  memcpy(&image[1], &out_of_range, sizeof(PhrogSettings));
  TEST_ASSERT_EQUAL_INT(PHROG_SETTINGS_OK, phrog_settings_write_raw_for_test(image, sizeof(image)));
  TEST_ASSERT_EQUAL_INT(PHROG_SETTINGS_OK, phrog_settings_initialize(&k_defaults_a));
  TEST_ASSERT_EQUAL_UINT16(PHROG_SETTINGS_MIN_SWEEP_COUNT, phrog_settings_get()->measurement_sweep_count);
}

// ===================== Board integration ==========================================
// device_setup_initialize runs once per boot, so this is the last case in the suite.

static void test_device_setup_applies_stored_thermistor_offset(void) {
  TEST_ASSERT_EQUAL_INT(PHROG_SETTINGS_OK, phrog_settings_initialize(&k_defaults_a));
  PhrogSettings custom = {5u, 1.25f, {0}};
  TEST_ASSERT_EQUAL_INT(PHROG_SETTINGS_OK, phrog_settings_save(&custom));
  phrog_settings_deinitialize();

  TEST_ASSERT_EQUAL_INT(PHX_OK, device_setup_initialize());
  ThermistorReaderConfig live_config = {};
  thermistor_reader_get_config_for_test(&live_config);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.25f, live_config.calibration_offset_c);
  TEST_ASSERT_EQUAL_UINT16(5u, phrog_settings_get()->measurement_sweep_count);
}

void setup() {
  UNITY_SETUP_SERIAL_DEFAULT();
  RUN_TEST(test_initialize_rejects_null_defaults);
  RUN_TEST(test_get_and_save_require_initialization);
  RUN_TEST(test_initialize_with_no_file_takes_defaults_and_creates_the_file);
  RUN_TEST(test_save_persists_across_reinitialization);
  RUN_TEST(test_save_clamps_sweep_count);
  RUN_TEST(test_reset_to_defaults_restores_and_persists);
  RUN_TEST(test_wrong_version_file_falls_back_to_defaults);
  RUN_TEST(test_short_file_falls_back_to_defaults);
  RUN_TEST(test_stored_sweep_count_is_clamped_on_load);
  RUN_TEST(test_device_setup_applies_stored_thermistor_offset);
  UNITY_END();
}

void loop() {
  // Unity tests run once; leave loop empty.
}
