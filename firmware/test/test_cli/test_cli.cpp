#include "cli.hpp"
#include "unity_config.h"
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>
#include <unity.h>

void setUp(void) {
  cli_test_set_output(NULL);
  cli_test_set_measurement_hooks(NULL);
}

void tearDown(void) {
  cli_test_set_output(NULL);
  cli_test_set_measurement_hooks(NULL);
}

static void test_dispatch_rejects_empty_command(void) {
  TEST_ASSERT_EQUAL_INT(CLI_DISPATCH_EMPTY_COMMAND, cli_dispatch_command(""));
  TEST_ASSERT_EQUAL_INT(CLI_DISPATCH_EMPTY_COMMAND, cli_dispatch_command(NULL));
}

static void test_dispatch_rejects_unknown_command(void) {
  TEST_ASSERT_EQUAL_INT(CLI_DISPATCH_UNKNOWN_COMMAND, cli_dispatch_command("definitely_not_a_command"));
}

void setup() {
  UNITY_SETUP_SERIAL_DEFAULT();
  RUN_TEST(test_dispatch_rejects_empty_command);
  RUN_TEST(test_dispatch_rejects_unknown_command);
  UNITY_END();
}

void loop() {
  // Unity tests run once; leave loop empty.
}
