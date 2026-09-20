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

const LightReadingsConfig g_device_light_readings_config = {
    {LedDriverChannel::LED_DRIVER_CHANNEL_GREEN, 100u},  // TODO(phrog): settle time TBD on hardware.
    {LedDriverChannel::LED_DRIVER_CHANNEL_BLUE, 100u},
    100u,      // dark_settle_time_us
    1000000u,  // adc_timeout_us
};

const ThermistorReaderConfig g_device_thermistor_reader_config = {
    PIN_THERMISTOR_SIGNAL,
    PIN_THERMISTOR_DRIVE,
    10000u,    // series_resistance_ohms  TODO(phrog): confirm against schematic.
    10000.0f,  // r25_ohms
    3380.0f,   // beta_constant
    2000u,     // settle_time_us
    0.0f,      // calibration_offset_c
};

// Default settings applied when flash is empty or corrupt.
static const PhrogSettings k_default_settings = {
    {0},  // reserved
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

  // Step 6: Stage the thermistor reader on the internal SAADC.
  GUARD(thermistor_reader_initialize(&g_device_thermistor_reader_config));

  g_device_setup_ready = true;
  return PHX_OK;
}
