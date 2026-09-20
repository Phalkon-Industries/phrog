#include "led_driver.hpp"
#include "unity_config.h"
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>
#include <unity.h>

void setUp(void) {
  led_driver_reset_for_test();
}

void tearDown(void) {
  led_driver_reset_for_test();
}

static void test_initialize_rejects_null_config(void) {
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_ERR_INVALID_ARG, led_driver_initialize(NULL));
}

static void test_set_channel_requires_initialization(void) {
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_ERR_NOT_INITIALIZED,
                        led_driver_set_channel(LedDriverChannel::LED_DRIVER_CHANNEL_GREEN));
}

void setup() {
  UNITY_SETUP_SERIAL_DEFAULT();
  RUN_TEST(test_initialize_rejects_null_config);
  RUN_TEST(test_set_channel_requires_initialization);
  UNITY_END();
}

void loop() {
  // Unity tests run once; leave loop empty.
}
