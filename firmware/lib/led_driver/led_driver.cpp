#include "led_driver.hpp"

#include <Arduino.h>
#include <string.h>

// ===================== Module State =============================================
static bool             g_initialized = false;
static LedDriverConfig  g_config      = {};
static LedDriverChannel g_channel     = LedDriverChannel::LED_DRIVER_CHANNEL_OFF;

// Each LED is an NPN current sink whose base is driven from the GPIO, so HIGH lights the LED.
static const uint32_t k_led_on_level  = HIGH;
static const uint32_t k_led_off_level = LOW;

// Writes both drive pins for a channel, turning the LED that is going off out first so the two
// LEDs never overlap even for the few cycles between the writes.
static void drive_channel(LedDriverChannel channel) {
  const uint32_t green_level =
      (channel == LedDriverChannel::LED_DRIVER_CHANNEL_GREEN) ? k_led_on_level : k_led_off_level;
  const uint32_t blue_level = (channel == LedDriverChannel::LED_DRIVER_CHANNEL_BLUE) ? k_led_on_level : k_led_off_level;

  if (green_level == k_led_off_level) {
    digitalWrite(static_cast<uint32_t>(g_config.green_drive_pin), green_level);
    digitalWrite(static_cast<uint32_t>(g_config.blue_drive_pin), blue_level);
  }
  else {
    digitalWrite(static_cast<uint32_t>(g_config.blue_drive_pin), blue_level);
    digitalWrite(static_cast<uint32_t>(g_config.green_drive_pin), green_level);
  }
}

int led_driver_initialize(const LedDriverConfig* config) {
  // Step 1: Validate the configuration pointer.
  GUARD_NONNULL(config);

  // Step 2: Cache the pin assignments.
  memcpy(&g_config, config, sizeof(LedDriverConfig));

  // Step 3: Configure both drive pins as outputs and park them low immediately so neither LED
  // flickers on while the pin direction changes.
  pinMode(static_cast<uint32_t>(g_config.green_drive_pin), OUTPUT);
  digitalWrite(static_cast<uint32_t>(g_config.green_drive_pin), k_led_off_level);
  pinMode(static_cast<uint32_t>(g_config.blue_drive_pin), OUTPUT);
  digitalWrite(static_cast<uint32_t>(g_config.blue_drive_pin), k_led_off_level);

  g_channel     = LedDriverChannel::LED_DRIVER_CHANNEL_OFF;
  g_initialized = true;
  return LED_DRIVER_OK;
}

int led_driver_set_channel(LedDriverChannel channel) {
  GUARD_INITIALIZED(g_initialized);

  // Step 1: Reject anything outside the enum before touching hardware so a bad request leaves the
  // previously commanded state intact.
  if ((channel != LedDriverChannel::LED_DRIVER_CHANNEL_OFF) &&
      (channel != LedDriverChannel::LED_DRIVER_CHANNEL_GREEN) &&
      (channel != LedDriverChannel::LED_DRIVER_CHANNEL_BLUE)) {
    return LED_DRIVER_ERR_INVALID_ARG;
  }

  // Step 2: Drive the pins and record the command.
  drive_channel(channel);
  g_channel = channel;
  return LED_DRIVER_OK;
}

int led_driver_get_channel(LedDriverChannel* channel_out) {
  GUARD_NONNULL(channel_out);
  GUARD_INITIALIZED(g_initialized);
  *channel_out = g_channel;
  return LED_DRIVER_OK;
}

int led_driver_shutdown(void) {
  GUARD_INITIALIZED(g_initialized);

  // Step 1: Park both LEDs off before forgetting the configuration.
  drive_channel(LedDriverChannel::LED_DRIVER_CHANNEL_OFF);
  g_channel     = LedDriverChannel::LED_DRIVER_CHANNEL_OFF;
  g_initialized = false;
  return LED_DRIVER_OK;
}

void led_driver_reset_for_test(void) {
  g_initialized = false;
  g_channel     = LedDriverChannel::LED_DRIVER_CHANNEL_OFF;
  memset(&g_config, 0, sizeof(LedDriverConfig));
}
