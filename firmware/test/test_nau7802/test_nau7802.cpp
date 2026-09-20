#include "nau7802.hpp"
#include "unity_config.h"
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>
#include <unity.h>

void setUp(void) {
  nau7802_reset_for_test();
}

void tearDown(void) {
  nau7802_reset_for_test();
}

static void test_initialize_rejects_null_config(void) {
  TEST_ASSERT_EQUAL_INT(NAU7802_ERR_INVALID_ARG, nau7802_initialize(NULL));
}

static void test_read_conversion_requires_initialization(void) {
  int32_t code = 0;
  TEST_ASSERT_EQUAL_INT(NAU7802_ERR_NOT_INITIALIZED, nau7802_read_conversion(1000u, &code));
}

void setup() {
  UNITY_SETUP_SERIAL_DEFAULT();
  RUN_TEST(test_initialize_rejects_null_config);
  RUN_TEST(test_read_conversion_requires_initialization);
  UNITY_END();
}

void loop() {
  // Unity tests run once; leave loop empty.
}
