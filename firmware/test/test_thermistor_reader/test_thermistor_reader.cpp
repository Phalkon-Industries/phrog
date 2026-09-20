#include "thermistor_reader.hpp"
#include "unity_config.h"
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>
#include <unity.h>

void setUp(void) {
  thermistor_reader_reset_for_test();
}

void tearDown(void) {
  thermistor_reader_reset_for_test();
}

static void test_initialize_rejects_null_config(void) {
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_ERR_INVALID_ARG, thermistor_reader_initialize(NULL));
}

static void test_measure_requires_initialization(void) {
  float temperature_c = 0.0f;
  TEST_ASSERT_EQUAL_INT(THERMISTOR_READER_ERR_NOT_INITIALIZED, thermistor_reader_measure_celsius(&temperature_c));
}

void setup() {
  UNITY_SETUP_SERIAL_DEFAULT();
  RUN_TEST(test_initialize_rejects_null_config);
  RUN_TEST(test_measure_requires_initialization);
  UNITY_END();
}

void loop() {
  // Unity tests run once; leave loop empty.
}
