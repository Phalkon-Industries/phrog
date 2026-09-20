#ifndef NAU7802_HPP
#define NAU7802_HPP

#include "phoenix_guard.hpp"
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

// ===================== Register Map (NAU7802 datasheet Rev 2.6, section 10) =====
// TODO(phrog): populate register addresses and bit masks from the datasheet before implementing.

/**
 * @brief PGA gain options (CTRL1.GAINS, datasheet table 10-4).
 */
enum class Nau7802Gain : uint8_t {
  NAU7802_GAIN_X1 = 0u,
  NAU7802_GAIN_X2,
  NAU7802_GAIN_X4,
  NAU7802_GAIN_X8,
  NAU7802_GAIN_X16,
  NAU7802_GAIN_X32,
  NAU7802_GAIN_X64,
  NAU7802_GAIN_X128,
};

/**
 * @brief Conversion rate options (CTRL2.CRS, datasheet table 10-7).
 */
enum class Nau7802SampleRate : uint8_t {
  NAU7802_SPS_10 = 0u,
  NAU7802_SPS_20,
  NAU7802_SPS_40,
  NAU7802_SPS_80,
  NAU7802_SPS_320,
};

/**
 * @brief Internal LDO output voltage (CTRL1.VLDO, datasheet table 10-3).
 */
enum class Nau7802Ldo : uint8_t {
  NAU7802_LDO_2V4 = 0u,
  NAU7802_LDO_2V7,
  NAU7802_LDO_3V0,
  NAU7802_LDO_3V3,
  NAU7802_LDO_3V6,
  NAU7802_LDO_3V9,
  NAU7802_LDO_4V2,
  NAU7802_LDO_4V5,
};

/**
 * @brief Differential input channel select (CTRL2.CHS).
 */
enum class Nau7802Channel : uint8_t {
  NAU7802_CHANNEL_1 = 0u,
  NAU7802_CHANNEL_2,
};

/**
 * @brief Bus resources required to talk to the NAU7802.
 */
struct Nau7802Config {
  TwoWire* wire;        /**< I2C bus instance; must already be started by the caller. */
  uint8_t  i2c_address; /**< 7-bit device address (fixed at 0x2A). */
};

/**
 * @brief Working conversion settings applied after initialisation.
 */
struct Nau7802Settings {
  Nau7802Gain       gain;
  Nau7802SampleRate sample_rate;
  Nau7802Ldo        ldo;
  Nau7802Channel    channel;
};

/**
 * @brief Prepare the driver, reset the device, and verify it responds on the bus.
 *
 * @param config Pointer to bus configuration.
 * @return NAU7802_OK on success, NAU7802_ERR_INVALID_ARG on NULL config, or a bus/device error.
 */
int nau7802_initialize(const Nau7802Config* config);

/**
 * @brief Program gain, sample rate, LDO, and channel selection.
 *
 * @param settings Pointer to the settings to apply.
 * @return NAU7802_OK on success or a negative error code.
 */
int nau7802_apply_settings(const Nau7802Settings* settings);

/**
 * @brief Block until a conversion is ready and return the signed 24-bit result.
 *
 * @param timeout_us Maximum time to wait for the data-ready flag, in microseconds.
 * @param code_out Destination for the sign-extended conversion code.
 * @return NAU7802_OK on success, NAU7802_ERR_TIMEOUT when no sample arrives, or a bus error.
 */
int nau7802_read_conversion(uint32_t timeout_us, int32_t* code_out);

/**
 * @brief Place the device in its low-power state (PU_CTRL.PUA/PUD cleared).
 *
 * @return NAU7802_OK on success or a negative error code.
 */
int nau7802_enter_standby(void);

/**
 * @brief Release the driver and mark it uninitialised.
 *
 * @return NAU7802_OK on success, NAU7802_ERR_NOT_INITIALIZED if never initialised.
 */
int nau7802_shutdown(void);

/** @brief Reset internal state between Unity test cases. */
void nau7802_reset_for_test(void);

#endif  // NAU7802_HPP
