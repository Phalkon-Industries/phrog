// Board characterisation for the battery sense (P0.04) and the module's 3.3 V cutoff (P0.13).
//
// The module is a no-name nRF52840 Pro Micro clone, so the nice!nano documentation and the
// variant notes are both treated as claims to be checked, not facts. This suite measures what
// the pins actually do and prints the numbers. The asserts only cover conditions the rest of
// the firmware relies on (a live 3.3 V rail with I2C pull-ups at the power-on pin state, and a
// sense pin that is not over-voltage). The P0.13 sweep is information only because phrog does
// not use that pin and its behaviour varies between board implementations.
#include "device_setup.hpp"
#include "unity_config.h"
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>
#include <stdio.h>
#include <unity.h>

// SAADC configuration: 12-bit codes against the 3.0 V internal reference (0.6 V ref, 1/5 gain).
static const int      k_adc_resolution_bits = 12;
static const uint32_t k_adc_full_scale_code = 4095u;
static const uint32_t k_adc_full_scale_mv   = 3000u;
static const uint32_t k_adc_sample_count    = 8u;

// nice!nano v2 documents a 2 M / 806 k divider on P0.04, so VBAT = Vpin * 2806 / 2000. The
// clone may differ; the ratio is only used to print a "if the divider matches" estimate.
static const uint32_t k_nice_nano_divider_full_ohms   = 2806000u;
static const uint32_t k_nice_nano_divider_output_ohms = 2000000u;

// Time for a switched rail to collapse or recover before we look at it.
static const uint32_t k_rail_settle_ms = 100u;

static char g_message[96];

// Averages several SAADC samples so a single noisy conversion cannot skew the report.
static uint32_t read_vbat_code_average(void) {
  uint32_t code_sum = 0u;
  for (uint32_t sample = 0u; sample < k_adc_sample_count; ++sample) {
    code_sum += static_cast<uint32_t>(analogRead(PIN_VBAT));
  }
  return code_sum / k_adc_sample_count;
}

// Converts an averaged SAADC code into millivolts at the pin.
static uint32_t code_to_pin_mv(uint32_t code) {
  return (code * k_adc_full_scale_mv) / k_adc_full_scale_code;
}

// Reads the I2C lines as plain inputs; the 10 k pull-ups to +3.3 V make them a rail detector.
static bool i2c_pullups_high(void) {
  pinMode(PIN_WIRE_SDA, INPUT);
  pinMode(PIN_WIRE_SCL, INPUT);
  return (digitalRead(PIN_WIRE_SDA) == HIGH) && (digitalRead(PIN_WIRE_SCL) == HIGH);
}

// Prints one labelled measurement line of rail state plus battery-sense voltage.
static void report_state(const char* label) {
  const uint32_t code             = read_vbat_code_average();
  const uint32_t pin_mv           = code_to_pin_mv(code);
  const uint32_t vbat_estimate_mv = (pin_mv * k_nice_nano_divider_full_ohms) / k_nice_nano_divider_output_ohms;
  snprintf(g_message, sizeof(g_message), "%s: 3v3_rail=%s vbat_code=%lu pin_mv=%lu vbat_est_mv=%lu", label,
           i2c_pullups_high() ? "present" : "ABSENT", static_cast<unsigned long>(code),
           static_cast<unsigned long>(pin_mv), static_cast<unsigned long>(vbat_estimate_mv));
  TEST_MESSAGE(g_message);
}

void setUp(void) {
  analogReadResolution(k_adc_resolution_bits);
  analogReference(AR_INTERNAL_3_0);
}

void tearDown(void) {
  // Leave P0.13 floating exactly as it is at power-on so later suites see the board as shipped.
  pinMode(PIN_EXT_VCC_CUTOFF, INPUT);
}

// The whole instrument hangs off the 3.3 V rail, so it must be up with P0.13 untouched.
static void test_3v3_rail_present_with_p0_13_floating(void) {
  pinMode(PIN_EXT_VCC_CUTOFF, INPUT);
  delay(k_rail_settle_ms);
  report_state("p0_13_floating");
  TEST_ASSERT_TRUE_MESSAGE(i2c_pullups_high(), "SDA/SCL not high: 3.3 V rail or I2C pull-ups missing");
}

// A divider output can never reach the 3.0 V reference; a railed reading means P0.04 is tied
// straight to the battery, which would exceed the SAADC absolute maximum.
static void test_vbat_pin_is_not_railed(void) {
  const uint32_t code = read_vbat_code_average();
  snprintf(g_message, sizeof(g_message), "vbat_code=%lu (full scale %lu)", static_cast<unsigned long>(code),
           static_cast<unsigned long>(k_adc_full_scale_code));
  TEST_MESSAGE(g_message);
  TEST_ASSERT_LESS_THAN_UINT32(k_adc_full_scale_code, code);
}

// Information only: drives P0.13 low, then high, then lets it float, and reports the rail and
// sense pin each time. phrog has no plans to use P0.13 and its behaviour depends on which clone
// the module is, so this test never fails on the outcome; read the :INFO: lines instead.
static void test_p0_13_effect_on_rail_and_vbat_info(void) {
  pinMode(PIN_EXT_VCC_CUTOFF, OUTPUT);

  // Step 1: Drive low and report.
  digitalWrite(PIN_EXT_VCC_CUTOFF, LOW);
  delay(k_rail_settle_ms);
  report_state("p0_13_low");
  const bool rail_with_low = i2c_pullups_high();

  // Step 2: Drive high and report.
  digitalWrite(PIN_EXT_VCC_CUTOFF, HIGH);
  delay(k_rail_settle_ms);
  report_state("p0_13_high");
  const bool rail_with_high = i2c_pullups_high();

  // Step 3: Summarise which level, if any, switches the rail off.
  if (rail_with_low && rail_with_high) {
    TEST_MESSAGE("P0.13 rail switch: NONE (rail up at both levels)");
  }
  else if (!rail_with_low && rail_with_high) {
    TEST_MESSAGE("P0.13 rail switch: LOW cuts the 3.3 V rail");
  }
  else if (rail_with_low && !rail_with_high) {
    TEST_MESSAGE("P0.13 rail switch: HIGH cuts the 3.3 V rail");
  }
  else {
    TEST_MESSAGE("P0.13 rail switch: rail down at both levels (check the rail detector)");
  }

  // Step 4: Float the pin as at power-on so later suites see the board as shipped, and report.
  pinMode(PIN_EXT_VCC_CUTOFF, INPUT);
  delay(k_rail_settle_ms);
  report_state("p0_13_floating_again");
}

void setup() {
  UNITY_SETUP_SERIAL_DEFAULT();
  RUN_TEST(test_3v3_rail_present_with_p0_13_floating);
  RUN_TEST(test_vbat_pin_is_not_railed);
  RUN_TEST(test_p0_13_effect_on_rail_and_vbat_info);
  UNITY_END();
}

void loop() {
  // Unity tests run once; leave loop empty.
}
