#ifndef DEVICE_SETUP_HPP
#define DEVICE_SETUP_HPP

#include "cli.hpp"
#include "led_driver.hpp"
#include "light_readings.hpp"
#include "nau7802.hpp"
#include "phrog_settings.hpp"
#include "thermistor_reader.hpp"
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>
#include <Wire.h>
#include <variant.h>  // PIN_Pn_nn definitions for the nice_nano / Pro Micro variant.

// ===================== Pin Map (nRF52840 Pro Micro, see docs/pin-map.md) =====
// The nice_nano variant uses an identity map, so PIN_P1_13 is Arduino pin 45 (32 + 13).

// Thermistor divider measured on the nRF52840 internal SAADC.
#define PIN_THERMISTOR_SIGNAL PIN_P0_29  // AIN5
#define PIN_THERMISTOR_DRIVE PIN_P0_02   // Drives the divider only while sampling.

// LED drive pins (direct GPIO drive; no analog switch or digipot on phrog).
#define PIN_LED_GREEN_DRIVE PIN_P1_13
#define PIN_LED_BLUE_DRIVE PIN_P1_11

// I2C bus for the NAU7802 (SCL P1.04, SDA P1.06) is owned by Wire via PIN_WIRE_SCL / PIN_WIRE_SDA.
// DRDY goes high when a conversion is ready (CTRL1.DRDYP left at the active-high default).
#define PIN_NAU7802_DRDY PIN_P1_00

// NAU7802 has a fixed 7-bit I2C address (datasheet section 1.9.2).
#define NAU7802_I2C_ADDRESS 0x2Au

extern const LedDriverConfig        g_device_led_driver_config;
extern const Nau7802Config          g_device_nau7802_config;
extern const Nau7802Settings        g_device_nau7802_settings;
extern const LightReadingsConfig    g_device_light_readings_config;
extern const ThermistorReaderConfig g_device_thermistor_reader_config;

/**
 * @brief Bring up every board-level module in dependency order.
 *
 * Safe to call repeatedly; subsequent calls return PHX_OK without re-initialising.
 *
 * @return PHX_OK on success, or the first module error code encountered.
 */
int device_setup_initialize(void);

#endif  // DEVICE_SETUP_HPP
