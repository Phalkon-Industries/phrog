#include "led_driver.hpp"

#include <Arduino.h>
#include <string.h>

// ===================== Module State =============================================
static bool             g_initialized = false;
static LedDriverConfig  g_config      = {};
static LedDriverChannel g_channel     = LedDriverChannel::LED_DRIVER_CHANNEL_OFF;

int led_driver_initialize(const LedDriverConfig* config) {
  // Step 1: Validate the configuration pointer.
  GUARD_NONNULL(config);

  // Step 2: Cache the pin assignments.
  memcpy(&g_config, config, sizeof(LedDriverConfig));

  // TODO(phrog): configure both drive pins as outputs parked low.
  g_channel     = LedDriverChannel::LED_DRIVER_CHANNEL_OFF;
  g_initialized = true;
  return LED_DRIVER_OK;
}

int led_driver_set_channel(LedDriverChannel channel) {
  (void) channel;
  GUARD_INITIALIZED(g_initialized);
  return LED_DRIVER_ERR_NOT_IMPLEMENTED;
}

int led_driver_get_channel(LedDriverChannel* channel_out) {
  GUARD_NONNULL(channel_out);
  GUARD_INITIALIZED(g_initialized);
  *channel_out = g_channel;
  return LED_DRIVER_OK;
}

int led_driver_shutdown(void) {
  GUARD_INITIALIZED(g_initialized);
  g_initialized = false;
  return LED_DRIVER_OK;
}

void led_driver_reset_for_test(void) {
  g_initialized = false;
  g_channel     = LedDriverChannel::LED_DRIVER_CHANNEL_OFF;
  memset(&g_config, 0, sizeof(LedDriverConfig));
}
