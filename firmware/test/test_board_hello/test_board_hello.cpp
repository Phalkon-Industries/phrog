// Board bring-up "hello world": proves the toolchain, bootloader, USB serial, and on-board LED work
// before any driver code is trusted. Keep this suite trivial; it is the first thing run on a new board.
#include "unity_config.h"
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>
#include <unity.h>

// Blink count is small so the whole suite finishes in a couple of seconds.
static const uint32_t k_blink_count     = 3u;
static const uint32_t k_blink_period_ms = 200u;

void setUp(void) {
}

void tearDown(void) {
  // Leave the LED off so the board looks idle after the suite.
  digitalWrite(LED_BUILTIN, !LED_STATE_ON);
}

// A test reaching the host at all means USB CDC and the Unity transport work.
static void test_serial_link_reports_results(void) {
  TEST_ASSERT_TRUE(true);
}

// millis() advancing proves the system tick (and therefore the clock setup) is alive.
static void test_millis_advances(void) {
  const uint32_t start_ms = millis();
  delay(10u);
  TEST_ASSERT_GREATER_THAN_UINT32(start_ms, millis());
}

// Reading the FICR device ID proves the core can touch nRF peripherals; the ID is never all zeros.
static void test_device_id_is_nonzero(void) {
  const uint32_t device_id_low  = NRF_FICR->DEVICEID[0];
  const uint32_t device_id_high = NRF_FICR->DEVICEID[1];
  TEST_ASSERT_TRUE((device_id_low != 0u) || (device_id_high != 0u));
}

// Blinking the on-board LED is the visible half of hello world; the assert checks the pin reads back.
static void test_builtin_led_blinks(void) {
  pinMode(LED_BUILTIN, OUTPUT);
  for (uint32_t blink = 0u; blink < k_blink_count; ++blink) {
    digitalWrite(LED_BUILTIN, LED_STATE_ON);
    delay(k_blink_period_ms);
    digitalWrite(LED_BUILTIN, !LED_STATE_ON);
    delay(k_blink_period_ms);
  }
  digitalWrite(LED_BUILTIN, LED_STATE_ON);
  TEST_ASSERT_EQUAL_INT(LED_STATE_ON, digitalRead(LED_BUILTIN));
}

void setup() {
  UNITY_SETUP_SERIAL_DEFAULT();
  RUN_TEST(test_serial_link_reports_results);
  RUN_TEST(test_millis_advances);
  RUN_TEST(test_device_id_is_nonzero);
  RUN_TEST(test_builtin_led_blinks);
  UNITY_END();
}

void loop() {
  // Unity tests run once; leave loop empty.
}
