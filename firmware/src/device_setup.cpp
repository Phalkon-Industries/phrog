#include "device_setup.hpp"

const LedDriverConfig g_device_led_driver_config = {
    PIN_LED_GREEN_DRIVE,
    PIN_LED_BLUE_DRIVE,
};

const Nau7802Config g_device_nau7802_config = {
    &Wire,
    NAU7802_I2C_ADDRESS,
    PIN_NAU7802_DRDY,
};

// DVDD is 3.3 V so the LDO tops out at 3.0 V; unity gain keeps the 0..1.5 V single-ended range.
// 10 SPS gives the best 50/60 Hz rejection; light_readings may trade that for speed later.
const Nau7802Settings g_device_nau7802_settings = {
    Nau7802Gain::NAU7802_GAIN_X1,
    Nau7802SampleRate::NAU7802_SPS_10,
    Nau7802Ldo::NAU7802_LDO_3V0,
    Nau7802Channel::NAU7802_CHANNEL_1,
    false,  // pga_bypass
};

// The LED and amplifier settle electrically in well under a millisecond; the real settling is the
// NAU7802 decimation filter, handled by discarding conversions after every LED or channel change.
// The timeout covers the four-conversion restart the chip performs after a channel switch at 10 SPS.
const LightReadingsConfig g_device_light_readings_config = {
    {LedDriverChannel::LED_DRIVER_CHANNEL_GREEN, 5000u},  // settle_time_us
    {LedDriverChannel::LED_DRIVER_CHANNEL_BLUE, 5000u},
    5000u,     // dark_settle_time_us
    4u,        // settle_conversions
    1500000u,  // adc_timeout_us
};

// R6 is the 10 k series resistor on the netlist; the thermistor is the Phoenix sample part (10 k NTC).
const ThermistorReaderConfig g_device_thermistor_reader_config = {
    PIN_THERMISTOR_SIGNAL,
    PIN_THERMISTOR_DRIVE,
    10000u,    // series_resistance_ohms
    10000.0f,  // r25_ohms
    2000u,     // settle_time_us
    0.0f,      // calibration_offset_c
};

// Default settings applied when flash is empty or corrupt: five sweeps per measurement and no
// thermistor correction until a unit has been checked against a reference thermometer.
static const PhrogSettings k_default_settings = {
    5u,    // measurement_sweep_count
    0.0f,  // thermistor_calibration_offset_c
    {0},   // reserved
};

int device_setup_initialize(void) {
  static bool g_device_setup_ready = false;

  if (g_device_setup_ready) {
    return PHX_OK;
  }

  // Step 1: Initialize the I2C bus shared by the NAU7802.
  Wire.begin();

  // Step 2: Initialize settings storage so calibration data is available before measurements.
  GUARD(phrog_settings_initialize(&k_default_settings));

  // Step 3: Bring up the NAU7802 ADC that samples the photodiode signal and program the board's
  // conversion settings, which also runs the internal offset calibration.
  GUARD(nau7802_initialize(&g_device_nau7802_config));
  GUARD(nau7802_apply_settings(&g_device_nau7802_settings));

  // Step 4: Initialize the LED driver pins in a known-off state.
  GUARD(led_driver_initialize(&g_device_led_driver_config));

  // Step 5: Bring the light readings helper online so sweeps can run immediately.
  GUARD(light_readings_initialize(&g_device_light_readings_config));

  // Step 6: Stage the thermistor reader on the internal SAADC, applying this unit's stored
  // calibration offset on top of the board constants.
  ThermistorReaderConfig thermistor_config = g_device_thermistor_reader_config;
  const PhrogSettings*   settings          = phrog_settings_get();
  if (settings != NULL) {
    thermistor_config.calibration_offset_c = settings->thermistor_calibration_offset_c;
  }
  GUARD(thermistor_reader_initialize(&thermistor_config));

  g_device_setup_ready = true;
  return PHX_OK;
}
