// Bench example: streams NAU7802 codes for each photodiode channel in the dark and under its LED so
// the amplifier trimpots (RV1 blue, RV2 green) can be set while watching the numbers. Flash with
// `pio run -e adc_monitor -t upload` and open a 115200 baud terminal.
//
// Each line is tab separated: channel, led, code, percent of positive full scale, and a SATURATED
// marker when the amplifier output exceeds the ADC input range. Aim for lit readings well inside
// full scale (roughly 50-80 %) so the signal has headroom as the sample absorbs light.
#ifndef PIO_UNIT_TESTING

#include "device_setup.hpp"
#include "led_driver.hpp"
#include "nau7802.hpp"
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>

static const uint32_t k_conversion_timeout_us = 500000u;
static const uint32_t k_discard_count         = 6u;  // Flush the filter after each channel/LED change.
static const uint32_t k_led_settle_ms         = 50u;
static const int32_t  k_full_scale_code       = 8388607;
static const int32_t  k_saturation_code       = 8388600;

// 80 SPS keeps the display responsive while the trimpots are being turned.
static const Nau7802Settings k_monitor_settings = {
    Nau7802Gain::NAU7802_GAIN_X1,
    Nau7802SampleRate::NAU7802_SPS_80,
    Nau7802Ldo::NAU7802_LDO_3V0,
    Nau7802Channel::NAU7802_CHANNEL_1,
    false,
};

struct MonitorStep {
  Nau7802Channel   channel;
  LedDriverChannel led;
  const char*      label;
};

// Channel 1 is the blue photodiode, channel 2 the green one (docs/pin-map.md).
static const MonitorStep k_steps[] = {
    {Nau7802Channel::NAU7802_CHANNEL_1, LedDriverChannel::LED_DRIVER_CHANNEL_OFF, "blue_pd\tdark"},
    {Nau7802Channel::NAU7802_CHANNEL_1, LedDriverChannel::LED_DRIVER_CHANNEL_BLUE, "blue_pd\tblue_led"},
    {Nau7802Channel::NAU7802_CHANNEL_2, LedDriverChannel::LED_DRIVER_CHANNEL_OFF, "green_pd\tdark"},
    {Nau7802Channel::NAU7802_CHANNEL_2, LedDriverChannel::LED_DRIVER_CHANNEL_GREEN, "green_pd\tgreen_led"},
};
static const size_t k_step_count = sizeof(k_steps) / sizeof(k_steps[0]);

static bool g_ready = false;

// Prints a labelled error line in the same tab-separated style as the CLI.
static void report_error(const char* label, int return_code) {
  Serial.print("error\t");
  Serial.print(label);
  Serial.print("\t");
  Serial.println(return_code);
}

// Reads one settled code for a step, returning false and reporting if the ADC misbehaves.
static bool settled_code(const MonitorStep& step, int32_t* code_out) {
  int return_code = nau7802_select_channel(step.channel);
  if (return_code != NAU7802_OK) {
    report_error("select_channel", return_code);
    return false;
  }
  return_code = led_driver_set_channel(step.led);
  if (return_code != LED_DRIVER_OK) {
    report_error("set_channel", return_code);
    return false;
  }
  delay(k_led_settle_ms);
  for (uint32_t index = 0u; index <= k_discard_count; ++index) {
    return_code = nau7802_read_conversion(k_conversion_timeout_us, code_out);
    if (return_code != NAU7802_OK) {
      report_error("read_conversion", return_code);
      return false;
    }
  }
  return true;
}

void setup() {
  // Step 1: Open USB serial so the readings can be followed from a terminal.
  Serial.begin(115200);
  delay(100);  // Short delay to allow serial to set up.

  // Step 2: Bring up only what the monitor needs: I2C, the ADC, and the LED driver.
  Wire.begin();
  int return_code = led_driver_initialize(&g_device_led_driver_config);
  if (return_code != LED_DRIVER_OK) {
    report_error("led_driver_initialize", return_code);
    return;
  }
  return_code = nau7802_initialize(&g_device_nau7802_config);
  if (return_code != NAU7802_OK) {
    report_error("nau7802_initialize", return_code);
    return;
  }
  return_code = nau7802_apply_settings(&k_monitor_settings);
  if (return_code != NAU7802_OK) {
    report_error("nau7802_apply_settings", return_code);
    return;
  }

  g_ready = true;
  Serial.println("adc_monitor ready: photodiode\tled\tcode\tpercent_fs");
}

void loop() {
  if (!g_ready) {
    delay(1000u);
    return;
  }

  // Step 1: Walk each channel through dark and lit and print one line per state.
  for (size_t index = 0u; index < k_step_count; ++index) {
    int32_t code = 0;
    if (!settled_code(k_steps[index], &code)) {
      continue;
    }
    const int32_t percent = static_cast<int32_t>((static_cast<int64_t>(code) * 100) / k_full_scale_code);
    Serial.print(k_steps[index].label);
    Serial.print("\t");
    Serial.print(code);
    Serial.print("\t");
    Serial.print(percent);
    Serial.println((code >= k_saturation_code) ? "\tSATURATED" : "");
  }

  // Step 2: Leave the LEDs off between passes and separate the passes in the log.
  (void) led_driver_set_channel(LedDriverChannel::LED_DRIVER_CHANNEL_OFF);
  Serial.println();
}

#endif
