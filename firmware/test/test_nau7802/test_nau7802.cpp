#include "device_setup.hpp"
#include "led_driver.hpp"
#include "nau7802.hpp"
#include "unity_config.h"
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>
#include <Wire.h>
#include <unity.h>

// Generous wait for one conversion at the slowest rate (10 SPS -> 100 ms) plus the six-cycle
// settling the datasheet asks for after reset or resume.
static const uint32_t k_conversion_timeout_us = 1500000u;

// Readings to throw away after a channel or LED change so the decimation filter has flushed.
static const uint32_t k_discard_count = 6u;

// Time for the LED and the photodiode amplifier to settle before the light reading.
static const uint32_t k_led_settle_ms = 50u;

// Codes a lit LED must add over dark. 24-bit single-ended full scale is 1.5 V at the 3.0 V LDO,
// about 5600 codes per millivolt, so this is roughly 2 mV: far above noise, far below any real signal.
static const int32_t k_light_margin_codes = 10000;

// Positive full scale of the 24-bit result. A lit reading at or above this means the amplifier
// output exceeds the ADC input range and the feedback trimpot (RV1 blue, RV2 green) must come down.
static const int32_t k_saturation_code = 8388600;

// Settings used by most cases: the board's LDO limit, unity gain, and a fast rate to keep the
// suite short. The 10 SPS case exists separately for the timeout test.
static const Nau7802Settings k_fast_settings = {
    Nau7802Gain::NAU7802_GAIN_X1,
    Nau7802SampleRate::NAU7802_SPS_80,
    Nau7802Ldo::NAU7802_LDO_3V0,
    Nau7802Channel::NAU7802_CHANNEL_1,
    false,
};

// Same as k_fast_settings but at the slowest rate, so a fresh conversion cannot sneak in between
// two back-to-back calls.
static const Nau7802Settings k_slow_settings = {
    Nau7802Gain::NAU7802_GAIN_X1,
    Nau7802SampleRate::NAU7802_SPS_10,
    Nau7802Ldo::NAU7802_LDO_3V0,
    Nau7802Channel::NAU7802_CHANNEL_1,
    false,
};

// Discards the filter contents so the next reading reflects the current channel and light.
static void discard_conversions(void) {
  for (uint32_t index = 0u; index < k_discard_count; ++index) {
    int32_t ignored = 0;
    TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_read_conversion(k_conversion_timeout_us, &ignored));
  }
}

// Reads one settled conversion on the requested channel with the requested LED state.
static int32_t settled_reading(Nau7802Channel channel, LedDriverChannel led) {
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_select_channel(channel));
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_set_channel(led));
  delay(k_led_settle_ms);
  discard_conversions();
  int32_t code = 0;
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_read_conversion(k_conversion_timeout_us, &code));
  return code;
}

void setUp(void) {
  nau7802_reset_for_test();
  led_driver_reset_for_test();
}

void tearDown(void) {
  (void) led_driver_shutdown();
  (void) nau7802_shutdown();
  nau7802_reset_for_test();
  led_driver_reset_for_test();
}

// ===================== Pure helpers =============================================

static void test_unpack_conversion_sign_extends_24_bit_values(void) {
  const uint8_t max_positive[NAU7802_CONVERSION_BYTES] = {0x7Fu, 0xFFu, 0xFFu};
  const uint8_t min_negative[NAU7802_CONVERSION_BYTES] = {0x80u, 0x00u, 0x00u};
  const uint8_t minus_one[NAU7802_CONVERSION_BYTES]    = {0xFFu, 0xFFu, 0xFFu};
  const uint8_t small[NAU7802_CONVERSION_BYTES]        = {0x00u, 0x01u, 0x02u};
  TEST_ASSERT_EQUAL_INT32(8388607, nau7802_unpack_conversion(max_positive));
  TEST_ASSERT_EQUAL_INT32(-8388608, nau7802_unpack_conversion(min_negative));
  TEST_ASSERT_EQUAL_INT32(-1, nau7802_unpack_conversion(minus_one));
  TEST_ASSERT_EQUAL_INT32(258, nau7802_unpack_conversion(small));
}

// ===================== Guards ===================================================

static void test_initialize_rejects_null_config_and_null_bus(void) {
  TEST_ASSERT_EQUAL_INT(NAU7802_ERR_INVALID_ARG, nau7802_initialize(NULL));
  const Nau7802Config no_bus = {NULL, NAU7802_I2C_ADDRESS, -1};
  TEST_ASSERT_EQUAL_INT(NAU7802_ERR_INVALID_ARG, nau7802_initialize(&no_bus));
}

static void test_calls_require_initialization(void) {
  int32_t code  = 0;
  uint8_t value = 0u;
  TEST_ASSERT_EQUAL_INT(NAU7802_ERR_NOT_INITIALIZED, nau7802_read_conversion(1000u, &code));
  TEST_ASSERT_EQUAL_INT(NAU7802_ERR_NOT_INITIALIZED, nau7802_apply_settings(&k_fast_settings));
  TEST_ASSERT_EQUAL_INT(NAU7802_ERR_NOT_INITIALIZED, nau7802_select_channel(Nau7802Channel::NAU7802_CHANNEL_2));
  TEST_ASSERT_EQUAL_INT(NAU7802_ERR_NOT_INITIALIZED, nau7802_calibrate_internal_offset());
  TEST_ASSERT_EQUAL_INT(NAU7802_ERR_NOT_INITIALIZED, nau7802_enter_standby());
  TEST_ASSERT_EQUAL_INT(NAU7802_ERR_NOT_INITIALIZED, nau7802_resume());
  TEST_ASSERT_EQUAL_INT(NAU7802_ERR_NOT_INITIALIZED, nau7802_shutdown());
  TEST_ASSERT_EQUAL_INT(NAU7802_ERR_NOT_INITIALIZED, nau7802_read_register_for_test(NAU7802_REG_REV_ID, &value));
}

static void test_read_conversion_rejects_null_output(void) {
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_initialize(&g_device_nau7802_config));
  TEST_ASSERT_EQUAL_INT(NAU7802_ERR_INVALID_ARG, nau7802_read_conversion(1000u, NULL));
}

// ===================== Bring-up ==================================================

static void test_initialize_reports_missing_device_on_wrong_address(void) {
  const Nau7802Config wrong_address = {&Wire, static_cast<uint8_t>(NAU7802_I2C_ADDRESS + 1u), -1};
  TEST_ASSERT_EQUAL_INT(NAU7802_ERR_DEVICE_NOT_FOUND, nau7802_initialize(&wrong_address));
  int32_t code = 0;
  TEST_ASSERT_EQUAL_INT(NAU7802_ERR_NOT_INITIALIZED, nau7802_read_conversion(1000u, &code));
}

static void test_initialize_powers_up_on_internal_ldo(void) {
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_initialize(&g_device_nau7802_config));

  uint8_t pu_ctrl = 0u;
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_read_register_for_test(NAU7802_REG_PU_CTRL, &pu_ctrl));
  const uint8_t expected = NAU7802_PU_CTRL_AVDDS | NAU7802_PU_CTRL_PUR | NAU7802_PU_CTRL_PUA | NAU7802_PU_CTRL_PUD;
  TEST_ASSERT_EQUAL_HEX8(expected, pu_ctrl & (expected | NAU7802_PU_CTRL_RR | NAU7802_PU_CTRL_OSCS));

  uint8_t adc_ctrl = 0u;
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_read_register_for_test(NAU7802_REG_ADC_CTRL, &adc_ctrl));
  TEST_ASSERT_EQUAL_HEX8(NAU7802_ADC_CTRL_RECOMMENDED, adc_ctrl & NAU7802_ADC_CTRL_RECOMMENDED);

  uint8_t rev_id = 0u;
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_read_register_for_test(NAU7802_REG_REV_ID, &rev_id));
  TEST_ASSERT_EQUAL_HEX8(NAU7802_REV_ID_EXPECTED, rev_id & NAU7802_REV_ID_MASK);
}

static void test_initialize_is_repeatable(void) {
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_initialize(&g_device_nau7802_config));
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_shutdown());
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_initialize(&g_device_nau7802_config));
  int32_t code = 0;
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_read_conversion(k_conversion_timeout_us, &code));
}

// ===================== Settings ==================================================

static void test_apply_settings_programs_ctrl_registers_and_calibrates(void) {
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_initialize(&g_device_nau7802_config));
  const Nau7802Settings settings = {
      Nau7802Gain::NAU7802_GAIN_X2,
      Nau7802SampleRate::NAU7802_SPS_40,
      Nau7802Ldo::NAU7802_LDO_3V0,
      Nau7802Channel::NAU7802_CHANNEL_2,
      false,
  };
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_apply_settings(&settings));

  uint8_t ctrl1 = 0u;
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_read_register_for_test(NAU7802_REG_CTRL1, &ctrl1));
  const uint8_t expected_ctrl1 =
      static_cast<uint8_t>((static_cast<uint8_t>(Nau7802Ldo::NAU7802_LDO_3V0) << NAU7802_CTRL1_VLDO_SHIFT) |
                           static_cast<uint8_t>(Nau7802Gain::NAU7802_GAIN_X2));
  TEST_ASSERT_EQUAL_HEX8(expected_ctrl1, ctrl1 & (NAU7802_CTRL1_VLDO_MASK | NAU7802_CTRL1_PGA_MASK));
  TEST_ASSERT_EQUAL_HEX8(0u, ctrl1 & (NAU7802_CTRL1_DRDYP | NAU7802_CTRL1_DRDY_SEL));

  uint8_t ctrl2 = 0u;
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_read_register_for_test(NAU7802_REG_CTRL2, &ctrl2));
  const uint8_t expected_ctrl2 = static_cast<uint8_t>(
      NAU7802_CTRL2_CHS | (static_cast<uint8_t>(Nau7802SampleRate::NAU7802_SPS_40) << NAU7802_CTRL2_CRS_SHIFT));
  TEST_ASSERT_EQUAL_HEX8(expected_ctrl2, ctrl2 & (NAU7802_CTRL2_CHS | NAU7802_CTRL2_CRS_MASK));
  TEST_ASSERT_EQUAL_HEX8(0u, ctrl2 & (NAU7802_CTRL2_CAL_ERR | NAU7802_CTRL2_CALS));

  uint8_t pga = 0u;
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_read_register_for_test(NAU7802_REG_PGA, &pga));
  TEST_ASSERT_EQUAL_HEX8(0u, pga & NAU7802_PGA_BYPASS_ENABLE);
}

static void test_apply_settings_can_enable_pga_bypass(void) {
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_initialize(&g_device_nau7802_config));
  Nau7802Settings settings = k_fast_settings;
  settings.pga_bypass      = true;
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_apply_settings(&settings));

  uint8_t pga = 0u;
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_read_register_for_test(NAU7802_REG_PGA, &pga));
  TEST_ASSERT_EQUAL_HEX8(NAU7802_PGA_BYPASS_ENABLE, pga & NAU7802_PGA_BYPASS_ENABLE);

  // Switching bypass back off must clear the bit again rather than leave it latched.
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_apply_settings(&k_fast_settings));
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_read_register_for_test(NAU7802_REG_PGA, &pga));
  TEST_ASSERT_EQUAL_HEX8(0u, pga & NAU7802_PGA_BYPASS_ENABLE);
}

static void test_apply_settings_rejects_null(void) {
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_initialize(&g_device_nau7802_config));
  TEST_ASSERT_EQUAL_INT(NAU7802_ERR_INVALID_ARG, nau7802_apply_settings(NULL));
}

static void test_select_channel_only_touches_chs_bit(void) {
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_initialize(&g_device_nau7802_config));
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_apply_settings(&k_fast_settings));

  uint8_t before = 0u;
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_read_register_for_test(NAU7802_REG_CTRL2, &before));

  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_select_channel(Nau7802Channel::NAU7802_CHANNEL_2));
  uint8_t after = 0u;
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_read_register_for_test(NAU7802_REG_CTRL2, &after));
  TEST_ASSERT_EQUAL_HEX8(NAU7802_CTRL2_CHS, after & NAU7802_CTRL2_CHS);
  TEST_ASSERT_EQUAL_HEX8(before & NAU7802_CTRL2_CRS_MASK, after & NAU7802_CTRL2_CRS_MASK);

  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_select_channel(Nau7802Channel::NAU7802_CHANNEL_1));
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_read_register_for_test(NAU7802_REG_CTRL2, &after));
  TEST_ASSERT_EQUAL_HEX8(0u, after & NAU7802_CTRL2_CHS);
}

// ===================== Conversions ===============================================

static void test_read_conversion_returns_in_range_codes_via_drdy_pin(void) {
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_initialize(&g_device_nau7802_config));
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_apply_settings(&k_fast_settings));
  for (uint32_t index = 0u; index < k_discard_count; ++index) {
    int32_t code = 0x7FFFFFFF;
    TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_read_conversion(k_conversion_timeout_us, &code));
    TEST_ASSERT_TRUE((code >= -8388608) && (code <= 8388607));
  }
}

static void test_read_conversion_works_when_polling_the_status_bit(void) {
  Nau7802Config polled = g_device_nau7802_config;
  polled.drdy_pin      = -1;
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_initialize(&polled));
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_apply_settings(&k_fast_settings));
  int32_t code = 0;
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_read_conversion(k_conversion_timeout_us, &code));
  TEST_ASSERT_TRUE((code >= -8388608) && (code <= 8388607));
}

static void test_read_conversion_times_out_when_nothing_is_ready(void) {
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_initialize(&g_device_nau7802_config));
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_apply_settings(&k_slow_settings));

  // Reading a result clears data-ready; at 10 SPS the next one is 100 ms away, so an immediate
  // second read with no wait must time out rather than hand back the same sample.
  int32_t code = 0;
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_read_conversion(k_conversion_timeout_us, &code));
  TEST_ASSERT_EQUAL_INT(NAU7802_ERR_TIMEOUT, nau7802_read_conversion(0u, &code));
}

static void test_standby_stops_conversions_and_resume_restarts_them(void) {
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_initialize(&g_device_nau7802_config));
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_apply_settings(&k_fast_settings));

  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_enter_standby());
  uint8_t pu_ctrl = 0u;
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_read_register_for_test(NAU7802_REG_PU_CTRL, &pu_ctrl));
  TEST_ASSERT_EQUAL_HEX8(0u, pu_ctrl & (NAU7802_PU_CTRL_PUA | NAU7802_PU_CTRL_PUD));

  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_resume());
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_read_register_for_test(NAU7802_REG_PU_CTRL, &pu_ctrl));
  const uint8_t powered = NAU7802_PU_CTRL_PUR | NAU7802_PU_CTRL_PUA | NAU7802_PU_CTRL_PUD;
  TEST_ASSERT_EQUAL_HEX8(powered, pu_ctrl & powered);

  int32_t code = 0;
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_read_conversion(k_conversion_timeout_us, &code));
}

static void test_shutdown_leaves_device_in_standby(void) {
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_initialize(&g_device_nau7802_config));
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_shutdown());
  int32_t code = 0;
  TEST_ASSERT_EQUAL_INT(NAU7802_ERR_NOT_INITIALIZED, nau7802_read_conversion(1000u, &code));

  // Re-initialise only to look at the register; the analog side must have been powered down.
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_initialize(&g_device_nau7802_config));
}

// ===================== Loop closure through the optics ===========================
// These are the first tests that go GPIO -> transistor -> LED -> photodiode -> amplifier -> ADC.
// Channel 1 is the blue photodiode and channel 2 the green one (see docs/pin-map.md).

static void test_blue_led_raises_channel_1_above_dark(void) {
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_initialize(&g_device_led_driver_config));
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_initialize(&g_device_nau7802_config));
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_apply_settings(&k_fast_settings));

  const int32_t dark = settled_reading(Nau7802Channel::NAU7802_CHANNEL_1, LedDriverChannel::LED_DRIVER_CHANNEL_OFF);
  const int32_t lit  = settled_reading(Nau7802Channel::NAU7802_CHANNEL_1, LedDriverChannel::LED_DRIVER_CHANNEL_BLUE);
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_set_channel(LedDriverChannel::LED_DRIVER_CHANNEL_OFF));

  char message[96];
  snprintf(message, sizeof(message), "blue: dark=%ld lit=%ld%s", static_cast<long>(dark), static_cast<long>(lit),
           (lit >= k_saturation_code) ? " SATURATED: turn the blue trimpot down" : "");
  TEST_MESSAGE(message);
  TEST_ASSERT_GREATER_THAN_INT32(dark + k_light_margin_codes, lit);
}

static void test_green_led_raises_channel_2_above_dark(void) {
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_initialize(&g_device_led_driver_config));
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_initialize(&g_device_nau7802_config));
  TEST_ASSERT_EQUAL_INT(NAU7802_OK, nau7802_apply_settings(&k_fast_settings));

  const int32_t dark = settled_reading(Nau7802Channel::NAU7802_CHANNEL_2, LedDriverChannel::LED_DRIVER_CHANNEL_OFF);
  const int32_t lit  = settled_reading(Nau7802Channel::NAU7802_CHANNEL_2, LedDriverChannel::LED_DRIVER_CHANNEL_GREEN);
  TEST_ASSERT_EQUAL_INT(LED_DRIVER_OK, led_driver_set_channel(LedDriverChannel::LED_DRIVER_CHANNEL_OFF));

  char message[96];
  snprintf(message, sizeof(message), "green: dark=%ld lit=%ld%s", static_cast<long>(dark), static_cast<long>(lit),
           (lit >= k_saturation_code) ? " SATURATED: turn the green trimpot down" : "");
  TEST_MESSAGE(message);
  TEST_ASSERT_GREATER_THAN_INT32(dark + k_light_margin_codes, lit);
}

void setup() {
  UNITY_SETUP_SERIAL_DEFAULT();
  Wire.begin();
  RUN_TEST(test_unpack_conversion_sign_extends_24_bit_values);
  RUN_TEST(test_initialize_rejects_null_config_and_null_bus);
  RUN_TEST(test_calls_require_initialization);
  RUN_TEST(test_read_conversion_rejects_null_output);
  RUN_TEST(test_initialize_reports_missing_device_on_wrong_address);
  RUN_TEST(test_initialize_powers_up_on_internal_ldo);
  RUN_TEST(test_initialize_is_repeatable);
  RUN_TEST(test_apply_settings_programs_ctrl_registers_and_calibrates);
  RUN_TEST(test_apply_settings_can_enable_pga_bypass);
  RUN_TEST(test_apply_settings_rejects_null);
  RUN_TEST(test_select_channel_only_touches_chs_bit);
  RUN_TEST(test_read_conversion_returns_in_range_codes_via_drdy_pin);
  RUN_TEST(test_read_conversion_works_when_polling_the_status_bit);
  RUN_TEST(test_read_conversion_times_out_when_nothing_is_ready);
  RUN_TEST(test_standby_stops_conversions_and_resume_restarts_them);
  RUN_TEST(test_shutdown_leaves_device_in_standby);
  RUN_TEST(test_blue_led_raises_channel_1_above_dark);
  RUN_TEST(test_green_led_raises_channel_2_above_dark);
  UNITY_END();
}

void loop() {
  // Unity tests run once; leave loop empty.
}
