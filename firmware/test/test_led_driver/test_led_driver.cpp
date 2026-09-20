#include "device_setup.hpp"
#include "led_driver.hpp"
#include "unity_config.h"
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>
#include <nrf_gpio.h>
#include <unity.h>

// Long enough for a human to see each LED on the bench without dragging the suite out.
static const uint32_t k_visible_hold_ms = 500u;

// Reports whether the core has configured an Arduino pin as a GPIO output.
static bool pin_is_output(int pin) {
  const uint32_t nrf_pin = g_ADigitalPinMap[pin];
  return nrf_gpio_pin_dir_get(nrf_pin) == NRF_GPIO_PIN_DIR_OUTPUT;
}

// digitalRead returns the OUT register for output pins in the Adafruit core, so it reflects the
// commanded drive level rather than the pin voltage.
static int pin_level(int pin) {
  return digitalRead(static_cast<uint32_t>(pin));
}

void setUp(void) {
  led_driver_reset_for_test();
}

void tearDown(void) {
  // Never leave an LED lit between cases; the drive transistors sink ~18 mA continuously.
  (void) led_driver_shutdown();
  led_driver_reset_for_test();
}

static void test_initialize_rejects_null_config(void) {
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_ERR_INVALID_ARG, led_driver_initialize(NULL));
}

static void test_initialize_parks_both_pins_low_as_outputs(void) {
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_initialize(&g_device_led_driver_config));
  TEST_ASSERT_TRUE(pin_is_output(g_device_led_driver_config.green_drive_pin));
  TEST_ASSERT_TRUE(pin_is_output(g_device_led_driver_config.blue_drive_pin));
  TEST_ASSERT_EQUAL_INT(LOW, pin_level(g_device_led_driver_config.green_drive_pin));
  TEST_ASSERT_EQUAL_INT(LOW, pin_level(g_device_led_driver_config.blue_drive_pin));
}

static void test_set_channel_requires_initialization(void) {
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_ERR_NOT_INITIALIZED,
                        led_driver_set_channel(LedDriverChannel::LED_DRIVER_CHANNEL_GREEN));
}

static void test_set_channel_green_drives_green_high_and_blue_low(void) {
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_initialize(&g_device_led_driver_config));
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_set_channel(LedDriverChannel::LED_DRIVER_CHANNEL_GREEN));
  TEST_ASSERT_EQUAL_INT(HIGH, pin_level(g_device_led_driver_config.green_drive_pin));
  TEST_ASSERT_EQUAL_INT(LOW, pin_level(g_device_led_driver_config.blue_drive_pin));
}

static void test_set_channel_blue_drives_blue_high_and_green_low(void) {
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_initialize(&g_device_led_driver_config));
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_set_channel(LedDriverChannel::LED_DRIVER_CHANNEL_GREEN));
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_set_channel(LedDriverChannel::LED_DRIVER_CHANNEL_BLUE));
  TEST_ASSERT_EQUAL_INT(HIGH, pin_level(g_device_led_driver_config.blue_drive_pin));
  TEST_ASSERT_EQUAL_INT(LOW, pin_level(g_device_led_driver_config.green_drive_pin));
}

static void test_set_channel_off_drives_both_low(void) {
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_initialize(&g_device_led_driver_config));
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_set_channel(LedDriverChannel::LED_DRIVER_CHANNEL_BLUE));
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_set_channel(LedDriverChannel::LED_DRIVER_CHANNEL_OFF));
  TEST_ASSERT_EQUAL_INT(LOW, pin_level(g_device_led_driver_config.green_drive_pin));
  TEST_ASSERT_EQUAL_INT(LOW, pin_level(g_device_led_driver_config.blue_drive_pin));
}

static void test_set_channel_rejects_invalid_channel_without_touching_pins(void) {
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_initialize(&g_device_led_driver_config));
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_set_channel(LedDriverChannel::LED_DRIVER_CHANNEL_GREEN));

  const LedDriverChannel bogus_channel = static_cast<LedDriverChannel>(99u);
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_ERR_INVALID_ARG, led_driver_set_channel(bogus_channel));

  // The previously commanded state must survive a rejected request.
  TEST_ASSERT_EQUAL_INT(HIGH, pin_level(g_device_led_driver_config.green_drive_pin));
  TEST_ASSERT_EQUAL_INT(LOW, pin_level(g_device_led_driver_config.blue_drive_pin));
  LedDriverChannel reported = LedDriverChannel::LED_DRIVER_CHANNEL_OFF;
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_get_channel(&reported));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(LedDriverChannel::LED_DRIVER_CHANNEL_GREEN), static_cast<int>(reported));
}

static void test_get_channel_reports_last_command(void) {
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_initialize(&g_device_led_driver_config));
  LedDriverChannel reported = LedDriverChannel::LED_DRIVER_CHANNEL_GREEN;

  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_get_channel(&reported));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(LedDriverChannel::LED_DRIVER_CHANNEL_OFF), static_cast<int>(reported));

  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_set_channel(LedDriverChannel::LED_DRIVER_CHANNEL_BLUE));
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_get_channel(&reported));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(LedDriverChannel::LED_DRIVER_CHANNEL_BLUE), static_cast<int>(reported));
}

static void test_get_channel_rejects_null_and_requires_initialization(void) {
  LedDriverChannel reported = LedDriverChannel::LED_DRIVER_CHANNEL_OFF;
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_ERR_INVALID_ARG, led_driver_get_channel(NULL));
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_ERR_NOT_INITIALIZED, led_driver_get_channel(&reported));
}

static void test_shutdown_turns_leds_off_and_requires_reinitialization(void) {
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_ERR_NOT_INITIALIZED, led_driver_shutdown());
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_initialize(&g_device_led_driver_config));
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_set_channel(LedDriverChannel::LED_DRIVER_CHANNEL_GREEN));

  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_shutdown());
  TEST_ASSERT_EQUAL_INT(LOW, pin_level(g_device_led_driver_config.green_drive_pin));
  TEST_ASSERT_EQUAL_INT(LOW, pin_level(g_device_led_driver_config.blue_drive_pin));
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_ERR_NOT_INITIALIZED,
                        led_driver_set_channel(LedDriverChannel::LED_DRIVER_CHANNEL_BLUE));
}

// Bench check: green then blue should each light for half a second. The assert only covers the
// final off state; whether the LEDs actually lit is for the person watching the board.
static void test_leds_visibly_light_green_then_blue(void) {
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_initialize(&g_device_led_driver_config));
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_set_channel(LedDriverChannel::LED_DRIVER_CHANNEL_GREEN));
  delay(k_visible_hold_ms);
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_set_channel(LedDriverChannel::LED_DRIVER_CHANNEL_BLUE));
  delay(k_visible_hold_ms);
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_set_channel(LedDriverChannel::LED_DRIVER_CHANNEL_OFF));
  TEST_ASSERT_EQUAL_INT(LOW, pin_level(g_device_led_driver_config.green_drive_pin));
  TEST_ASSERT_EQUAL_INT(LOW, pin_level(g_device_led_driver_config.blue_drive_pin));
}

void setup() {
  UNITY_SETUP_SERIAL_DEFAULT();
  RUN_TEST(test_initialize_rejects_null_config);
  RUN_TEST(test_initialize_parks_both_pins_low_as_outputs);
  RUN_TEST(test_set_channel_requires_initialization);
  RUN_TEST(test_set_channel_green_drives_green_high_and_blue_low);
  RUN_TEST(test_set_channel_blue_drives_blue_high_and_green_low);
  RUN_TEST(test_set_channel_off_drives_both_low);
  RUN_TEST(test_set_channel_rejects_invalid_channel_without_touching_pins);
  RUN_TEST(test_get_channel_reports_last_command);
  RUN_TEST(test_get_channel_rejects_null_and_requires_initialization);
  RUN_TEST(test_shutdown_turns_leds_off_and_requires_reinitialization);
  RUN_TEST(test_leds_visibly_light_green_then_blue);
  UNITY_END();
}

void loop() {
  // Unity tests run once; leave loop empty.
}
