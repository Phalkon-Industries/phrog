#include "phrog_settings.hpp"
#include "unity_config.h"
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>
#include <unity.h>

void setUp(void) {
  phrog_settings_deinitialize();
}

void tearDown(void) {
  phrog_settings_deinitialize();
}

static void test_initialize_rejects_null_defaults(void) {
  TEST_ASSERT_EQUAL_INT(PHROG_SETTINGS_ERR_INVALID_ARG, phrog_settings_initialize(NULL));
  TEST_ASSERT_FALSE(phrog_settings_is_initialized());
}

static void test_get_returns_null_before_initialization(void) {
  TEST_ASSERT_NULL(phrog_settings_get());
}

void setup() {
  UNITY_SETUP_SERIAL_DEFAULT();
  RUN_TEST(test_initialize_rejects_null_defaults);
  RUN_TEST(test_get_returns_null_before_initialization);
  UNITY_END();
}

void loop() {
  // Unity tests run once; leave loop empty.
}
