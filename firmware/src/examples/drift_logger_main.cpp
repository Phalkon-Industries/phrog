// Bench example: logs every production sweep as CSV so LED warm-up drift can be measured. Uses the
// same light_readings sweep and NAU7802 settings as the CLI's `b` and `s`, so the LED duty cycle
// matches a real measurement. Flash with `pio run -e drift_logger -t upload`.
//
// Starts paused with the LEDs off. Send `r` to sweep continuously and `p` to pause; while paused a
// temperature-only row is logged every two seconds so the cool-down is visible. Lines starting
// with '#' are comments; everything else is one CSV row per sweep or pause tick.
#ifndef PIO_UNIT_TESTING

#include "device_setup.hpp"
#include "led_driver.hpp"
#include "light_readings.hpp"
#include "thermistor_reader.hpp"
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>
#include <stdio.h>

static const uint32_t k_paused_log_interval_ms = 2000u;

static bool     g_ready           = false;
static bool     g_running         = false;
static uint32_t g_last_paused_log = 0u;

// Sample thermistor and nRF52840 die temperature; the thermistor reports NAN on error.
static void read_temperatures(float* thermistor_c_out, float* die_c_out) {
  if (thermistor_reader_measure_celsius(thermistor_c_out) != PHX_OK) {
    *thermistor_c_out = NAN;
  }
  *die_c_out = readCPUTemperature();
}

static void log_sweep(void) {
  LightReadingsSweepSample sample      = {};
  const uint32_t           start_ms    = millis();
  const int                return_code = light_readings_sweep(&sample);
  if (return_code != PHX_OK) {
    Serial.print("# error\tsweep\t");
    Serial.println(return_code);
    return;
  }
  float thermistor_c = 0.0f;
  float die_c        = 0.0f;
  read_temperatures(&thermistor_c, &die_c);

  char line[128];
  snprintf(line, sizeof(line), "%lu,run,%ld,%ld,%ld,%ld,%d,%.3f,%.2f", static_cast<unsigned long>(start_ms),
           static_cast<long>(sample.dark_blue_code), static_cast<long>(sample.blue_code),
           static_cast<long>(sample.dark_green_code), static_cast<long>(sample.green_code),
           light_readings_last_sweep_detected_saturation() ? 1 : 0, static_cast<double>(thermistor_c),
           static_cast<double>(die_c));
  Serial.println(line);
}

static void log_paused(void) {
  float thermistor_c = 0.0f;
  float die_c        = 0.0f;
  read_temperatures(&thermistor_c, &die_c);
  char line[96];
  snprintf(line, sizeof(line), "%lu,paused,,,,,,%.3f,%.2f", static_cast<unsigned long>(millis()),
           static_cast<double>(thermistor_c), static_cast<double>(die_c));
  Serial.println(line);
}

static void handle_input(void) {
  while (Serial.available() > 0) {
    const int incoming = Serial.read();
    if (incoming == 'r') {
      g_running = true;
      Serial.println("# running");
    }
    else if (incoming == 'p') {
      g_running = false;
      (void) led_driver_set_channel(LedDriverChannel::LED_DRIVER_CHANNEL_OFF);
      Serial.println("# paused");
    }
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }
  const int return_code = device_setup_initialize();
  if (return_code != PHX_OK) {
    Serial.print("# error\tdevice_setup_initialize\t");
    Serial.println(return_code);
    return;
  }
  Serial.println("# drift_logger ready; r = run, p = pause");
  Serial.println("ms,state,dark_blue,blue,dark_green,green,saturated,thermistor_c,die_c");
  g_ready = true;
}

void loop() {
  if (!g_ready) {
    return;
  }
  handle_input();
  if (g_running) {
    log_sweep();
  }
  else if ((millis() - g_last_paused_log) >= k_paused_log_interval_ms) {
    g_last_paused_log = millis();
    log_paused();
  }
}

#endif
