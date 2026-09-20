#include "thermistor_reader.hpp"

#include <Arduino.h>
#include <string.h>

// ===================== Module State =============================================
static bool                   g_initialized     = false;
static ThermistorReaderConfig g_config          = {};
static float                  g_last_resistance = 0.0f;

static thermistor_reader_pin_mode_fn_t      g_pin_mode      = NULL;
static thermistor_reader_digital_write_fn_t g_digital_write = NULL;
static thermistor_reader_delay_us_fn_t      g_delay_us      = NULL;
static thermistor_reader_analog_read_fn_t   g_analog_read   = NULL;

// Production GPIO hooks wrap the Arduino API so tests can swap in stubs without hardware.
static void default_pin_mode(int pin, uint32_t mode) {
  pinMode(static_cast<uint32_t>(pin), mode);
}

static void default_digital_write(int pin, uint32_t value) {
  digitalWrite(static_cast<uint32_t>(pin), value);
}

static void default_delay_us(uint32_t delay_us) {
  delayMicroseconds(delay_us);
}

static int32_t default_analog_read(int pin) {
  return static_cast<int32_t>(analogRead(static_cast<uint32_t>(pin)));
}

// Installs the production hooks so a fresh initialise never inherits test stubs.
static void apply_default_hooks(void) {
  g_pin_mode      = default_pin_mode;
  g_digital_write = default_digital_write;
  g_delay_us      = default_delay_us;
  g_analog_read   = default_analog_read;
}

int thermistor_reader_initialize(const ThermistorReaderConfig* config) {
  // Step 1: Validate the configuration pointer.
  GUARD_NONNULL(config);

  // Step 2: Cache the configuration and install production hooks.
  memcpy(&g_config, config, sizeof(ThermistorReaderConfig));
  apply_default_hooks();

  // TODO(phrog): configure the drive pin as an output parked low and set SAADC resolution/reference.
  g_initialized = true;
  return THERMISTOR_READER_OK;
}

int thermistor_reader_measure_celsius(float* temperature_c_out) {
  GUARD_NONNULL(temperature_c_out);
  GUARD_INITIALIZED(g_initialized);
  return THERMISTOR_READER_ERR_NOT_IMPLEMENTED;
}

void thermistor_reader_reset_for_test(void) {
  g_initialized     = false;
  g_last_resistance = 0.0f;
  memset(&g_config, 0, sizeof(ThermistorReaderConfig));
  apply_default_hooks();
}

void thermistor_reader_get_config_for_test(ThermistorReaderConfig* config_out) {
  if (config_out == NULL) {
    return;
  }
  memcpy(config_out, &g_config, sizeof(ThermistorReaderConfig));
}

void thermistor_reader_set_hooks_for_test(thermistor_reader_pin_mode_fn_t      pin_mode,
                                          thermistor_reader_digital_write_fn_t digital_write,
                                          thermistor_reader_delay_us_fn_t      delay_us,
                                          thermistor_reader_analog_read_fn_t   analog_read) {
  if (pin_mode != NULL) {
    g_pin_mode = pin_mode;
  }
  if (digital_write != NULL) {
    g_digital_write = digital_write;
  }
  if (delay_us != NULL) {
    g_delay_us = delay_us;
  }
  if (analog_read != NULL) {
    g_analog_read = analog_read;
  }
}

int thermistor_reader_get_last_resistance_for_test(float* resistance_out) {
  GUARD_NONNULL(resistance_out);
  GUARD_INITIALIZED(g_initialized);
  *resistance_out = g_last_resistance;
  return THERMISTOR_READER_OK;
}
