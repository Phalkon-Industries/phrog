#ifndef NAU7802_HPP
#define NAU7802_HPP

#include "phoenix_guard.hpp"
#include <stdbool.h>
#include <stdint.h>

class TwoWire;

// ===================== Return Codes =============================================
#define NAU7802_OK PHX_OK
#define NAU7802_ERR_INVALID_ARG PHX_ERR_INVALID_ARG
#define NAU7802_ERR_NOT_INITIALIZED PHX_ERR_NOT_INITIALIZED
#define NAU7802_ERR_TIMEOUT PHX_ERR_TIMEOUT
#define NAU7802_ERR_COMMUNICATION PHX_ERR_COMMUNICATION
#define NAU7802_ERR_NOT_IMPLEMENTED PHX_ERR_NOT_IMPLEMENTED
#define NAU7802_ERR_DEVICE_NOT_FOUND (PHX_ERR_MODULE_BASE - 20)
#define NAU7802_ERR_CALIBRATION (PHX_ERR_MODULE_BASE - 21)

// ===================== Register Map (NAU7802 datasheet Rev 2.6, sections 3 and 4) =====
#define NAU7802_REG_PU_CTRL 0x00u  // Power-up control and status.
#define NAU7802_REG_CTRL1 0x01u  // DRDY polarity, LDO voltage, PGA gain.
#define NAU7802_REG_CTRL2 0x02u  // Channel select, conversion rate, calibration control.
#define NAU7802_REG_OCAL1_B2 0x03u  // Channel 1 offset calibration, 3 bytes MSB first.
#define NAU7802_REG_GCAL1_B3 0x06u  // Channel 1 gain calibration, 4 bytes MSB first.
#define NAU7802_REG_OCAL2_B2 0x0Au  // Channel 2 offset calibration, 3 bytes MSB first.
#define NAU7802_REG_GCAL2_B3 0x0Du  // Channel 2 gain calibration, 4 bytes MSB first.
#define NAU7802_REG_I2C_CTRL 0x11u  // Bus pull-ups, burnout source, temperature sensor mux.
#define NAU7802_REG_ADCO_B2 0x12u  // Conversion result bits 23..16; burst read 3 bytes from here.
#define NAU7802_REG_ADC_CTRL 0x15u  // Chopper and common-mode settings (section 4.10).
#define NAU7802_REG_PGA 0x1Bu  // LDO mode, PGA buffer and bypass (section 4.15).
#define NAU7802_REG_PWR_CTRL 0x1Cu  // Bias currents and PGA capacitor enable (section 4.16).
#define NAU7802_REG_REV_ID 0x1Fu  // Chip revision, low nibble (section 4.19).

// PU_CTRL bits (section 4.1).
#define NAU7802_PU_CTRL_AVDDS 0x80u  // 1 = AVDD from the internal LDO.
#define NAU7802_PU_CTRL_OSCS 0x40u  // 1 = external crystal; phrog uses the internal RC oscillator.
#define NAU7802_PU_CTRL_CR 0x20u  // Read-only: conversion result ready.
#define NAU7802_PU_CTRL_CS 0x10u  // 0 -> 1 starts a fresh conversion cycle.
#define NAU7802_PU_CTRL_PUR 0x08u  // Read-only: power-up complete.
#define NAU7802_PU_CTRL_PUA 0x04u  // Analog power up.
#define NAU7802_PU_CTRL_PUD 0x02u  // Digital power up.
#define NAU7802_PU_CTRL_RR 0x01u  // Register reset while held at 1.

// CTRL1 fields (section 4.2).
#define NAU7802_CTRL1_DRDYP 0x80u  // 1 = DRDY active low; phrog leaves it active high.
#define NAU7802_CTRL1_DRDY_SEL 0x40u  // 1 = DRDY outputs the clock instead of data ready.
#define NAU7802_CTRL1_VLDO_MASK 0x38u
#define NAU7802_CTRL1_VLDO_SHIFT 3u
#define NAU7802_CTRL1_PGA_MASK 0x07u

// CTRL2 fields (section 4.3).
#define NAU7802_CTRL2_CHS 0x80u  // 1 = channel 2 (VIN2P/VIN2N), 0 = channel 1.
#define NAU7802_CTRL2_CRS_MASK 0x70u
#define NAU7802_CTRL2_CRS_SHIFT 4u
#define NAU7802_CTRL2_CAL_ERR 0x08u  // Read-only: last calibration failed.
#define NAU7802_CTRL2_CALS 0x04u  // Write 1 to start calibration; reads 1 while running.
#define NAU7802_CTRL2_CALMOD_MASK 0x03u
#define NAU7802_CTRL2_CALMOD_OFFSET_INTERNAL 0x00u

// ADC_CTRL (section 4.10): REG_CHPS = 11 is the datasheet's recommended value.
#define NAU7802_ADC_CTRL_RECOMMENDED 0x30u

// PGA register bits (section 4.15).
#define NAU7802_PGA_RD_OTP_SEL 0x80u
#define NAU7802_PGA_LDOMODE 0x40u
#define NAU7802_PGA_BUFFER_ENABLE 0x20u
#define NAU7802_PGA_BYPASS_ENABLE 0x10u

// REV_ID (section 4.19): the revision nibble reads 0xF on every NAU7802.
#define NAU7802_REV_ID_MASK 0x0Fu
#define NAU7802_REV_ID_EXPECTED 0x0Fu

// ===================== Timing =================================================
#define NAU7802_POWER_UP_TIMEOUT_MS 10u  // PUR typically sets ~200 us after PUD (section 2.1).
#define NAU7802_CALIBRATION_TIMEOUT_MS 3000u  // Internal calibration takes a handful of conversions.
#define NAU7802_CONVERSION_BYTES 3u

/**
 * @brief PGA gain options; values are the CTRL1.PGA register codes (datasheet section 4.2).
 */
enum class Nau7802Gain : uint8_t {
  NAU7802_GAIN_X1   = 0u,
  NAU7802_GAIN_X2   = 1u,
  NAU7802_GAIN_X4   = 2u,
  NAU7802_GAIN_X8   = 3u,
  NAU7802_GAIN_X16  = 4u,
  NAU7802_GAIN_X32  = 5u,
  NAU7802_GAIN_X64  = 6u,
  NAU7802_GAIN_X128 = 7u,
};

/**
 * @brief Conversion rate options; values are the CTRL2.CRS register codes (datasheet section 4.3).
 */
enum class Nau7802SampleRate : uint8_t {
  NAU7802_SPS_10  = 0u,
  NAU7802_SPS_20  = 1u,
  NAU7802_SPS_40  = 2u,
  NAU7802_SPS_80  = 3u,
  NAU7802_SPS_320 = 7u,
};

/**
 * @brief Internal LDO output voltage; values are the CTRL1.VLDO register codes (datasheet section 4.2).
 *
 * DVDD must be at least 0.3 V above the LDO output, so with phrog's 3.3 V DVDD the highest usable
 * setting is 3.0 V.
 */
enum class Nau7802Ldo : uint8_t {
  NAU7802_LDO_4V5 = 0u,
  NAU7802_LDO_4V2 = 1u,
  NAU7802_LDO_3V9 = 2u,
  NAU7802_LDO_3V6 = 3u,
  NAU7802_LDO_3V3 = 4u,
  NAU7802_LDO_3V0 = 5u,
  NAU7802_LDO_2V7 = 6u,
  NAU7802_LDO_2V4 = 7u,
};

/**
 * @brief Differential input channel select (CTRL2.CHS). On phrog channel 1 is the blue photodiode
 * and channel 2 the green one, both single-ended against ground.
 */
enum class Nau7802Channel : uint8_t {
  NAU7802_CHANNEL_1 = 0u,
  NAU7802_CHANNEL_2 = 1u,
};

/**
 * @brief Bus resources required to talk to the NAU7802.
 */
struct Nau7802Config {
  TwoWire* wire;        /**< I2C bus instance; must already be started by the caller. */
  uint8_t  i2c_address; /**< 7-bit device address (fixed at 0x2A). */
  int      drdy_pin;    /**< GPIO wired to DRDY, or -1 to poll the PU_CTRL.CR bit instead. */
};

/**
 * @brief Working conversion settings applied by nau7802_apply_settings.
 */
struct Nau7802Settings {
  Nau7802Gain       gain;
  Nau7802SampleRate sample_rate;
  Nau7802Ldo        ldo;
  Nau7802Channel    channel;
  bool              pga_bypass; /**< True routes the input straight to the ADC (datasheet section 2.3). */
};

/**
 * @brief Reset the device, power it up on the internal LDO, and verify it answers on the bus.
 *
 * Leaves the device converting with power-on defaults (4.5 V LDO code, gain x1, 10 SPS, channel 1);
 * call nau7802_apply_settings before trusting any reading.
 *
 * @param config Pointer to bus configuration.
 * @return NAU7802_OK on success, NAU7802_ERR_INVALID_ARG on NULL config or bus,
 *         NAU7802_ERR_DEVICE_NOT_FOUND when nothing acknowledges the address or the revision
 *         nibble is wrong, NAU7802_ERR_TIMEOUT if power-up ready never sets, or a bus error.
 */
int nau7802_initialize(const Nau7802Config* config);

/**
 * @brief Program gain, sample rate, LDO, channel, and PGA bypass, then run an internal offset
 * calibration as the datasheet recommends after any gain, rate, or channel change.
 *
 * @param settings Pointer to the settings to apply.
 * @return NAU7802_OK on success, NAU7802_ERR_CALIBRATION if the device reports a calibration
 *         error, or a negative error code.
 */
int nau7802_apply_settings(const Nau7802Settings* settings);

/**
 * @brief Switch the input multiplexer without touching any other setting.
 *
 * The first conversions after a switch still carry the old channel through the filter; callers
 * should discard several readings (the datasheet advises six after reset or resume).
 *
 * @param channel Channel to route to the PGA.
 * @return NAU7802_OK on success or a negative error code.
 */
int nau7802_select_channel(Nau7802Channel channel);

/**
 * @brief Run the device's internal offset calibration (CTRL2.CALMOD = 00) and wait for it.
 *
 * @return NAU7802_OK on success, NAU7802_ERR_CALIBRATION when CAL_ERR is set afterwards,
 *         NAU7802_ERR_TIMEOUT if CALS never clears, or a bus error.
 */
int nau7802_calibrate_internal_offset(void);

/**
 * @brief Block until a conversion is ready and return the signed 24-bit result.
 *
 * Reading the result clears data-ready, so back-to-back calls return distinct conversions.
 *
 * @param timeout_us Maximum time to wait for data ready, in microseconds.
 * @param code_out Destination for the sign-extended conversion code.
 * @return NAU7802_OK on success, NAU7802_ERR_TIMEOUT when no sample arrives, or a bus error.
 */
int nau7802_read_conversion(uint32_t timeout_us, int32_t* code_out);

/**
 * @brief Place the device in its low-power state (PU_CTRL.PUA and PUD cleared).
 *
 * Register contents survive standby; use nau7802_resume to continue converting.
 *
 * @return NAU7802_OK on success or a negative error code.
 */
int nau7802_enter_standby(void);

/**
 * @brief Leave standby and wait for power-up ready.
 *
 * @return NAU7802_OK on success, NAU7802_ERR_TIMEOUT if PUR never sets, or a bus error.
 */
int nau7802_resume(void);

/**
 * @brief Put the device in standby and mark the driver uninitialised.
 *
 * @return NAU7802_OK on success, NAU7802_ERR_NOT_INITIALIZED if never initialised.
 */
int nau7802_shutdown(void);

/**
 * @brief Sign-extend a big-endian 24-bit conversion into a 32-bit integer.
 *
 * @param bytes Three bytes as read from ADCO_B2, ADCO_B1, ADCO_B0.
 * @return The signed conversion code.
 */
int32_t nau7802_unpack_conversion(const uint8_t bytes[NAU7802_CONVERSION_BYTES]);

/** @brief Reset internal state between Unity test cases without touching the device. */
void nau7802_reset_for_test(void);

/**
 * @brief Read one device register so tests can verify what the driver programmed.
 *
 * @param reg Register address.
 * @param value_out Destination for the byte read.
 * @return NAU7802_OK on success or a negative error code.
 */
int nau7802_read_register_for_test(uint8_t reg, uint8_t* value_out);

#endif  // NAU7802_HPP
