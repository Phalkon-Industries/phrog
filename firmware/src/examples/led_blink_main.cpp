// Bench example: cycles the measurement LEDs off -> green -> blue every second so the LED drive
// stages can be checked by eye. Build and flash with `pio run -e led_blink -t upload`.
#ifndef PIO_UNIT_TESTING

#include "device_setup.hpp"
#include "led_driver.hpp"
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>

static const uint32_t k_step_hold_ms = 1000u;

// Order the cycle walks through; OFF sits between colours so each LED is seen alone.
static const LedDriverChannel k_cycle[] = {
    LedDriverChannel::LED_DRIVER_CHANNEL_OFF,
    LedDriverChannel::LED_DRIVER_CHANNEL_GREEN,
    LedDriverChannel::LED_DRIVER_CHANNEL_OFF,
    LedDriverChannel::LED_DRIVER_CHANNEL_BLUE,
};
static const size_t k_cycle_length = sizeof(k_cycle) / sizeof(k_cycle[0]);

static size_t g_cycle_index = 0u;

// Human-readable name for the serial log.
static const char* channel_name(LedDriverChannel channel) {
  switch (channel) {
    case LedDriverChannel::LED_DRIVER_CHANNEL_GREEN:
      return "green";
    case LedDriverChannel::LED_DRIVER_CHANNEL_BLUE:
      return "blue";
    default:
      return "off";
  }
}

void setup() {
  // Step 1: Open USB serial so the current LED state can be followed from a terminal.
  Serial.begin(115200);
  delay(100);  // Short delay to allow serial to set up.

  // Step 2: Bring up only the LED driver; the rest of the instrument is not needed for this check.
  const int return_code = led_driver_initialize(&g_device_led_driver_config);
  if (return_code != LED_DRIVER_OK) {
    Serial.print("error\tled_driver_initialize\t");
    Serial.println(return_code);
    return;
  }

  Serial.println("led_blink ready: cycling off/green/off/blue every second");
}

void loop() {
  // Step 1: Command the next LED state and report it.
  const LedDriverChannel channel     = k_cycle[g_cycle_index];
  const int              return_code = led_driver_set_channel(channel);
  Serial.print("led\t");
  Serial.print(channel_name(channel));
  if (return_code != LED_DRIVER_OK) {
    Serial.print("\terror\t");
    Serial.print(return_code);
  }
  Serial.println();

  // Step 2: Hold long enough to see it, then advance.
  delay(k_step_hold_ms);
  g_cycle_index = (g_cycle_index + 1u) % k_cycle_length;
}

#endif
