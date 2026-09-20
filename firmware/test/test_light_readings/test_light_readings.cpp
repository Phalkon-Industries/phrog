#include "light_readings.hpp"
#include "unity_config.h"
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>
#include <unity.h>

void setUp(void) {
  light_readings_reset_for_test();
}

void tearDown(void) {
  light_readings_reset_for_test();
}

static void test_initialize_rejects_null_config(void) {
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_INVALID_ARG, light_readings_initialize(NULL));
}

static void test_sweep_requires_initialization(void) {
  LightReadingsSweepSample sample = {};
  TEST_ASSERT_EQUAL_INT(LIGHT_READINGS_ERR_NOT_INITIALIZED, light_readings_sweep(&sample));
}

void setup() {
  UNITY_SETUP_SERIAL_DEFAULT();
  RUN_TEST(test_initialize_rejects_null_config);
  RUN_TEST(test_sweep_requires_initialization);
  UNITY_END();
}

void loop() {
  // Unity tests run once; leave loop empty.
}
