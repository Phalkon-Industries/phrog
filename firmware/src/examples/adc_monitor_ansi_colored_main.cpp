// Bench example (ANSI colour edition): streams NAU7802 codes for each photodiode channel in the
// dark and under its LED so the amplifier trimpots (RV1 blue, RV2 green) can be set while watching
// the numbers. Flash with `pio run -e adc_monitor_ansi_colored -t upload` and watch with
// `pio device monitor -e adc_monitor_ansi_colored`, which passes the escape codes through.
//
// Every pass measures all four states first, then redraws one fixed-width table in place, so the
// terminal shows a steady display whose numbers change rather than a scrolling log. Lit rows get
// LOW / GOOD / HIGH / SATURATED against the target window below plus which way to turn the pot;
// dark rows only report. Tune with the sample holder empty. Adding water raises transmission, so
// expect to re-check the window once the optics are final.
//
// The in-place redraw and the coloured verdict column use ANSI escape sequences, which Windows
// Terminal, the VS Code terminal, and PuTTY render. For a terminal that shows stray "[" characters
// instead, use the plain adc_monitor example, which prints the same table without any escapes.
#ifndef PIO_UNIT_TESTING

#include "device_setup.hpp"
#include "led_driver.hpp"
#include "nau7802.hpp"
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

static const uint32_t k_conversion_timeout_us = 500000u;
static const uint32_t k_discard_count         = 6u;  // Flush the filter after each channel/LED change.
static const uint32_t k_led_settle_ms         = 50u;
static const int32_t  k_full_scale_code       = 8388607;
static const int32_t  k_saturation_code       = 8388600;

// Target window for a lit reading, as percent of positive full scale.
static const int32_t k_good_min_percent = 70;
static const int32_t k_good_max_percent = 90;

// SGR colour codes for the verdict column.
static const char* const k_color_green  = "\x1b[32m";
static const char* const k_color_yellow = "\x1b[33m";
static const char* const k_color_red    = "\x1b[31m";
static const char* const k_color_reset  = "\x1b[0m";

// One table row per step plus the header; the redraw moves the cursor back up this many lines.
// Line capacity leaves room for the colour escapes on top of the visible 70-odd characters.
static const size_t k_table_lines     = 5u;
static const size_t k_line_capacity   = 96u;
static const size_t k_block_capacity  = k_table_lines * k_line_capacity;
static const size_t k_verdict_width   = 10u;
static const size_t k_verdict_padding = k_verdict_width + 1u;

// Picks the verdict colour: green for on target, yellow for adjust, red for clipping or errors.
static const char* verdict_color(const char* verdict) {
  if (verdict[0] == '\0') {
    return "";
  }
  if (strcmp(verdict, "GOOD") == 0) {
    return k_color_green;
  }
  if ((strcmp(verdict, "LOW") == 0) || (strcmp(verdict, "HIGH") == 0)) {
    return k_color_yellow;
  }
  return k_color_red;
}

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
  const char*      photodiode;
  const char*      led_label;
  const char*      trimpot;  // NULL for dark steps, which get no verdict.
};

// Channel 1 is the blue photodiode, channel 2 the green one (docs/pin-map.md).
static const MonitorStep k_steps[] = {
    {Nau7802Channel::NAU7802_CHANNEL_1, LedDriverChannel::LED_DRIVER_CHANNEL_OFF, "blue_pd", "dark", NULL},
    {Nau7802Channel::NAU7802_CHANNEL_1, LedDriverChannel::LED_DRIVER_CHANNEL_BLUE, "blue_pd", "blue_led", "RV1"},
    {Nau7802Channel::NAU7802_CHANNEL_2, LedDriverChannel::LED_DRIVER_CHANNEL_OFF, "green_pd", "dark", NULL},
    {Nau7802Channel::NAU7802_CHANNEL_2, LedDriverChannel::LED_DRIVER_CHANNEL_GREEN, "green_pd", "green_led", "RV2"},
};
static const size_t k_step_count = sizeof(k_steps) / sizeof(k_steps[0]);

static bool g_ready = false;

// Chooses the verdict word and the trimpot advice for a lit reading; dark rows get blanks.
static void choose_verdict(int32_t code, int32_t percent, const char* trimpot, const char** verdict_out, char* advice,
                           size_t advice_capacity) {
  advice[0] = '\0';
  if (trimpot == NULL) {
    *verdict_out = "";
  }
  else if (code >= k_saturation_code) {
    *verdict_out = "SATURATED";
    snprintf(advice, advice_capacity, "turn %s down a lot", trimpot);
  }
  else if (percent > k_good_max_percent) {
    *verdict_out = "HIGH";
    snprintf(advice, advice_capacity, "turn %s down", trimpot);
  }
  else if (percent < k_good_min_percent) {
    *verdict_out = "LOW";
    snprintf(advice, advice_capacity, "turn %s up", trimpot);
  }
  else {
    *verdict_out = "GOOD";
  }
}

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
  Serial.print("adc_monitor ready: lit target ");
  Serial.print(k_good_min_percent);
  Serial.print("-");
  Serial.print(k_good_max_percent);
  Serial.println(" % of full scale with the holder empty");
  Serial.println();
}

void loop() {
  if (!g_ready) {
    delay(1000u);
    return;
  }

  static bool g_table_drawn = false;
  static char g_block[k_block_capacity];

  // Step 1: Take every reading before printing anything so the table updates as one unit.
  int32_t codes[k_step_count];
  bool    valid[k_step_count];
  for (size_t index = 0u; index < k_step_count; ++index) {
    codes[index] = 0;
    valid[index] = settled_code(k_steps[index], &codes[index]);
  }
  (void) led_driver_set_channel(LedDriverChannel::LED_DRIVER_CHANNEL_OFF);

  // Step 2: Lay the table out in a buffer with fixed column widths so nothing shifts between passes.
  size_t written = 0u;
  written += snprintf(&g_block[written], k_block_capacity - written, "%-10s %-10s %10s %6s  %-10s %-20s\n",
                      "photodiode", "led", "code", "pct_fs", "verdict", "advice");
  for (size_t index = 0u; index < k_step_count; ++index) {
    const char* verdict    = "ERROR";
    char        advice[24] = "see error line above";
    long        code       = 0;
    long        percent    = 0;
    if (valid[index]) {
      code    = static_cast<long>(codes[index]);
      percent = static_cast<long>((static_cast<int64_t>(codes[index]) * 100) / k_full_scale_code);
      choose_verdict(codes[index], static_cast<int32_t>(percent), k_steps[index].trimpot, &verdict, advice,
                     sizeof(advice));
    }
    // Pad the verdict before colouring it so the escape bytes do not count toward the column width.
    char padded_verdict[k_verdict_padding];
    snprintf(padded_verdict, sizeof(padded_verdict), "%-10s", verdict);
    written += snprintf(&g_block[written], k_block_capacity - written, "%-10s %-10s %10ld %5ld%%  %s%s%s %-20s\n",
                        k_steps[index].photodiode, k_steps[index].led_label, code, percent, verdict_color(verdict),
                        padded_verdict, k_color_reset, advice);
  }

  // Step 3: After the first pass, move the cursor back to the top of the table so it redraws in place.
  if (g_table_drawn) {
    Serial.print("\x1b[");
    Serial.print(k_table_lines);
    Serial.print("A");
  }
  Serial.write(reinterpret_cast<const uint8_t*>(g_block), written);
  g_table_drawn = true;
}

#endif
