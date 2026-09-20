#include "thermistor_reader.hpp"

#include <Arduino.h>
#include <math.h>
#include <string.h>

// ===================== Module State =============================================
static bool                   g_initialized     = false;
static ThermistorReaderConfig g_config          = {};
static float                  g_last_resistance = 0.0f;

static thermistor_reader_pin_mode_fn_t      g_pin_mode      = NULL;
static thermistor_reader_digital_write_fn_t g_digital_write = NULL;
static thermistor_reader_delay_us_fn_t      g_delay_us      = NULL;
static thermistor_reader_analog_read_fn_t   g_analog_read   = NULL;

static const float k_kelvin_offset = 273.15f;

// ===================== Steinhart-Hart Table =====================================
// Ported verbatim from Phoenix (lib/thermistor_reader): piecewise coefficients for the 10 k NTC
// sample thermistor, one row per R/R25 band. A row applies when ratio >= ratio_min_inclusive.
// The bands meet at 0 C (3.274), 50 C (0.36036), and 100 C (0.06831).
struct SteinhartHartSegment {
  float ratio_min_inclusive;
  float coefficient_a;
  float coefficient_b;
  float coefficient_c;
  float coefficient_d;
};

static const SteinhartHartSegment k_sample_thermistor_segments[] = {
    {3.274f, 3.3538646e-03f, 2.5654090e-04f, 1.9243889e-06f, 1.0969244e-07f},
    {0.36036f, 3.3540154e-03f, 2.5627725e-04f, 2.0829210e-06f, 7.3003206e-08f},
    {0.06831f, 3.3539264e-03f, 2.5609446e-04f, 1.9621987e-06f, 4.6045930e-08f},
    {0.0f, 3.3368620e-03f, 2.4057263e-04f, -2.6687093e-06f, -4.0719355e-07f},
};
static const size_t k_segment_count = sizeof(k_sample_thermistor_segments) / sizeof(k_sample_thermistor_segments[0]);

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

// The SAADC reference and resolution are core-wide state that other modules may change (the board
// power test uses the 3.0 V internal reference), so they are re-asserted before every measurement.
static void configure_saadc(void) {
  analogReadResolution(THERMISTOR_READER_ADC_RESOLUTION_BITS);
  analogReference(AR_VDD4);
}

// Drives the divider only while sampling; a -1 drive pin means it is permanently powered.
static void set_drive(bool energised) {
  if (g_config.drive_pin < 0) {
    return;
  }
  g_digital_write(g_config.drive_pin, energised ? HIGH : LOW);
}

int thermistor_reader_initialize(const ThermistorReaderConfig* config) {
  // Step 1: Validate the configuration before caching it.
  GUARD_NONNULL(config);
  if ((config->signal_pin < 0) || (config->series_resistance_ohms == 0u) || (config->r25_ohms <= 0.0f)) {
    return THERMISTOR_READER_ERR_INVALID_ARG;
  }

  // Step 2: Cache the configuration. Production hooks are installed only on the very first call;
  // afterwards thermistor_reader_reset_for_test owns them, so stubs installed before initialise
  // survive and can observe the pin setup below.
  memcpy(&g_config, config, sizeof(ThermistorReaderConfig));
  if (g_pin_mode == NULL) {
    apply_default_hooks();
  }

  // Step 3: Park the drive pin low as an output so the divider draws nothing between reads.
  if (g_config.drive_pin >= 0) {
    g_pin_mode(g_config.drive_pin, OUTPUT);
    g_digital_write(g_config.drive_pin, LOW);
  }

  g_last_resistance = 0.0f;
  g_initialized     = true;
  return THERMISTOR_READER_OK;
}

int thermistor_reader_measure_celsius(float* temperature_c_out) {
  GUARD_NONNULL(temperature_c_out);
  GUARD_INITIALIZED(g_initialized);

  // Step 1: Energise the divider and let the node settle through the series resistor.
  configure_saadc();
  set_drive(true);
  if (g_config.settle_time_us > 0u) {
    g_delay_us(g_config.settle_time_us);
  }

  // Step 2: Average several conversions; the SAADC is noisy enough at 14 bits to be worth it.
  int32_t code_sum = 0;
  for (uint32_t sample = 0u; sample < THERMISTOR_READER_SAMPLE_COUNT; ++sample) {
    code_sum += g_analog_read(g_config.signal_pin);
  }
  const int32_t code = code_sum / static_cast<int32_t>(THERMISTOR_READER_SAMPLE_COUNT);

  // Step 3: De-energise before any conversion can fail so the thermistor never self-heats.
  set_drive(false);

  // Step 4: Convert code -> resistance -> temperature.
  float resistance_ohms = 0.0f;
  GUARD(thermistor_reader_code_to_resistance(code, g_config.series_resistance_ohms, &resistance_ohms));
  g_last_resistance = resistance_ohms;
  return thermistor_reader_convert_steinhart_hart_temperature(resistance_ohms, g_config.r25_ohms,
                                                              g_config.calibration_offset_c, temperature_c_out);
}

int thermistor_reader_code_to_resistance(int32_t code, uint32_t series_resistance_ohms, float* resistance_out) {
  GUARD_NONNULL(resistance_out);

  // Code 0 is a shorted thermistor and full scale an open one; both are outside the model and the
  // open case would divide by zero.
  if ((code <= 0) || (code >= THERMISTOR_READER_ADC_FULL_SCALE_CODE)) {
    return THERMISTOR_READER_ERR_COMPUTE_FAILURE;
  }

  // V_signal / V_drive = R_t / (R_s + R_t)  =>  R_t = R_s * ratio / (1 - ratio).
  const float ratio = static_cast<float>(code) / static_cast<float>(THERMISTOR_READER_ADC_FULL_SCALE_CODE);
  *resistance_out   = static_cast<float>(series_resistance_ohms) * ratio / (1.0f - ratio);
  return THERMISTOR_READER_OK;
}

int thermistor_reader_convert_steinhart_hart_temperature(float resistance_ohms, float r25_ohms,
                                                         float calibration_offset_c, float* temperature_c_out) {
  GUARD_NONNULL(temperature_c_out);
  if ((resistance_ohms <= 0.0f) || (r25_ohms <= 0.0f)) {
    return THERMISTOR_READER_ERR_COMPUTE_FAILURE;
  }

  // Step 1: Pick the coefficient band from the resistance ratio.
  const float                 ratio           = resistance_ohms / r25_ohms;
  const SteinhartHartSegment* matched_segment = NULL;
  for (size_t index = 0u; index < k_segment_count; ++index) {
    if (ratio >= k_sample_thermistor_segments[index].ratio_min_inclusive) {
      matched_segment = &k_sample_thermistor_segments[index];
      break;
    }
  }
  if (matched_segment == NULL) {
    return THERMISTOR_READER_ERR_COMPUTE_FAILURE;
  }

  // Step 2: 1/T = A + B ln(r) + C ln(r)^2 + D ln(r)^3, in kelvin.
  const float ln_ratio    = logf(ratio);
  const float ln_ratio_sq = ln_ratio * ln_ratio;
  const float inverse_t   = matched_segment->coefficient_a + (matched_segment->coefficient_b * ln_ratio) +
                          (matched_segment->coefficient_c * ln_ratio_sq) +
                          (matched_segment->coefficient_d * ln_ratio_sq * ln_ratio);
  if (inverse_t <= 0.0f) {
    return THERMISTOR_READER_ERR_COMPUTE_FAILURE;
  }

  *temperature_c_out = (1.0f / inverse_t) - k_kelvin_offset + calibration_offset_c;
  return THERMISTOR_READER_OK;
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
