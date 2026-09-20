#include "nau7802.hpp"

#include <Arduino.h>
#include <Wire.h>
#include <string.h>

// ===================== Module State =============================================
static bool            g_initialized    = false;
static Nau7802Config   g_config         = {};
static bool            g_settings_valid = false;
static Nau7802Settings g_settings       = {};

// Wire.endTransmission codes (Arduino convention): 0 = success, 2 = address NACK.
static const uint8_t k_wire_ok           = 0u;
static const uint8_t k_wire_address_nack = 2u;

// Polling interval while waiting on a status bit over the bus; a register read already costs
// a few hundred microseconds at 100 kHz, so this only stops the loop from hammering the bus.
static const uint32_t k_status_poll_interval_us = 100u;

// Time for the LDO output to settle on its capacitors after AVDDS is switched on. The datasheet
// gives no figure; 1 ms is far longer than the 1 uF/100 nF pair needs and only runs once.
static const uint32_t k_ldo_settle_ms = 1u;

// Maps a Wire transaction status to the module's error codes.
static int wire_status_to_error(uint8_t status) {
  if (status == k_wire_ok) {
    return NAU7802_OK;
  }
  return (status == k_wire_address_nack) ? NAU7802_ERR_DEVICE_NOT_FOUND : NAU7802_ERR_COMMUNICATION;
}

// Writes one register using the single-write sequence in datasheet section 1.9.2.
static int write_register(uint8_t reg, uint8_t value) {
  g_config.wire->beginTransmission(g_config.i2c_address);
  g_config.wire->write(reg);
  g_config.wire->write(value);
  return wire_status_to_error(g_config.wire->endTransmission());
}

// Reads @p length consecutive registers with the repeated-start read sequence in section 1.9.3.
static int read_registers(uint8_t reg, uint8_t* buffer, size_t length) {
  g_config.wire->beginTransmission(g_config.i2c_address);
  g_config.wire->write(reg);
  GUARD(wire_status_to_error(g_config.wire->endTransmission(false)));

  const size_t received = g_config.wire->requestFrom(g_config.i2c_address, length);
  if (received != length) {
    return NAU7802_ERR_COMMUNICATION;
  }
  for (size_t index = 0u; index < length; ++index) {
    buffer[index] = static_cast<uint8_t>(g_config.wire->read());
  }
  return NAU7802_OK;
}

static int read_register(uint8_t reg, uint8_t* value_out) {
  return read_registers(reg, value_out, 1u);
}

// Read-modify-write of a register field so unrelated bits (and read-only status) are preserved.
static int update_register(uint8_t reg, uint8_t mask, uint8_t value) {
  uint8_t current = 0u;
  GUARD(read_register(reg, &current));
  const uint8_t next = static_cast<uint8_t>((current & static_cast<uint8_t>(~mask)) | (value & mask));
  return write_register(reg, next);
}

// Polls PU_CTRL until PUR reports the analog and digital blocks are up (section 2.1 step 3).
static int wait_for_power_up_ready(void) {
  const uint32_t start_ms = millis();
  while (true) {
    uint8_t pu_ctrl = 0u;
    GUARD(read_register(NAU7802_REG_PU_CTRL, &pu_ctrl));
    if ((pu_ctrl & NAU7802_PU_CTRL_PUR) != 0u) {
      return NAU7802_OK;
    }
    if ((millis() - start_ms) >= NAU7802_POWER_UP_TIMEOUT_MS) {
      return NAU7802_ERR_TIMEOUT;
    }
    delayMicroseconds(k_status_poll_interval_us);
  }
}

// Reports whether a conversion is waiting, via the DRDY pin when wired or the CR bit otherwise.
static int data_ready(bool* ready_out) {
  if (g_config.drdy_pin >= 0) {
    *ready_out = (digitalRead(static_cast<uint32_t>(g_config.drdy_pin)) == HIGH);
    return NAU7802_OK;
  }
  uint8_t pu_ctrl = 0u;
  GUARD(read_register(NAU7802_REG_PU_CTRL, &pu_ctrl));
  *ready_out = ((pu_ctrl & NAU7802_PU_CTRL_CR) != 0u);
  return NAU7802_OK;
}

// Waits up to @p timeout_us for data ready; micros() wrap-around is handled by the subtraction.
static int wait_for_data_ready(uint32_t timeout_us) {
  const uint32_t start_us = micros();
  while (true) {
    bool ready = false;
    GUARD(data_ready(&ready));
    if (ready) {
      return NAU7802_OK;
    }
    if ((micros() - start_us) >= timeout_us) {
      return NAU7802_ERR_TIMEOUT;
    }
    if (g_config.drdy_pin < 0) {
      delayMicroseconds(k_status_poll_interval_us);
    }
  }
}

int nau7802_initialize(const Nau7802Config* config) {
  // Step 1: Validate inputs before touching the bus.
  GUARD_NONNULL(config);
  GUARD_NONNULL(config->wire);

  // Step 2: Cache the bus configuration for later transactions and stage the DRDY input.
  memcpy(&g_config, config, sizeof(Nau7802Config));
  if (g_config.drdy_pin >= 0) {
    pinMode(static_cast<uint32_t>(g_config.drdy_pin), INPUT);
  }

  // Step 3: Pulse the register reset (section 2.1 steps 1-2). The first write doubles as the
  // presence check: an address NACK means nothing is at 0x2A.
  GUARD(write_register(NAU7802_REG_PU_CTRL, NAU7802_PU_CTRL_RR));
  GUARD(write_register(NAU7802_REG_PU_CTRL, NAU7802_PU_CTRL_PUD));

  // Step 4: Wait for power-up ready, then confirm the revision nibble so a different part that
  // happens to share the address is not mistaken for an NAU7802.
  GUARD(wait_for_power_up_ready());
  uint8_t rev_id = 0u;
  GUARD(read_register(NAU7802_REG_REV_ID, &rev_id));
  if ((rev_id & NAU7802_REV_ID_MASK) != NAU7802_REV_ID_EXPECTED) {
    return NAU7802_ERR_DEVICE_NOT_FOUND;
  }

  // Step 5: Apply the datasheet's recommended ADC chopper setting (section 2.1 step 4b).
  GUARD(write_register(NAU7802_REG_ADC_CTRL, NAU7802_ADC_CTRL_RECOMMENDED));

  // Step 6: Select the internal LDO for AVDD (phrog leaves the AVDD pin on decoupling only) and
  // power the analog block; PUD stays set. Give the LDO a moment to settle on its capacitors.
  GUARD(write_register(NAU7802_REG_PU_CTRL, NAU7802_PU_CTRL_AVDDS | NAU7802_PU_CTRL_PUA | NAU7802_PU_CTRL_PUD));
  delay(k_ldo_settle_ms);
  GUARD(wait_for_power_up_ready());

  g_settings_valid = false;
  g_initialized    = true;
  return NAU7802_OK;
}

int nau7802_apply_settings(const Nau7802Settings* settings) {
  GUARD_NONNULL(settings);
  GUARD_INITIALIZED(g_initialized);

  // Step 1: LDO voltage and PGA gain live in CTRL1; keep DRDY at its active-high default.
  const uint8_t ctrl1 = static_cast<uint8_t>((static_cast<uint8_t>(settings->ldo) << NAU7802_CTRL1_VLDO_SHIFT) |
                                             (static_cast<uint8_t>(settings->gain) & NAU7802_CTRL1_PGA_MASK));
  GUARD(write_register(NAU7802_REG_CTRL1, ctrl1));

  // Step 2: Channel and conversion rate live in CTRL2; calibration bits are left untouched.
  const uint8_t ctrl2 = static_cast<uint8_t>(
      ((settings->channel == Nau7802Channel::NAU7802_CHANNEL_2) ? NAU7802_CTRL2_CHS : 0u) |
      ((static_cast<uint8_t>(settings->sample_rate) << NAU7802_CTRL2_CRS_SHIFT) & NAU7802_CTRL2_CRS_MASK));
  GUARD(update_register(NAU7802_REG_CTRL2, NAU7802_CTRL2_CHS | NAU7802_CTRL2_CRS_MASK, ctrl2));

  // Step 3: PGA bypass is the only PGA register bit phrog changes (datasheet section 2.3).
  GUARD(update_register(NAU7802_REG_PGA, NAU7802_PGA_BYPASS_ENABLE,
                        settings->pga_bypass ? NAU7802_PGA_BYPASS_ENABLE : 0u));

  // Step 4: Gain, rate, and channel changes all invalidate the offset trim (section 1.12).
  GUARD(nau7802_calibrate_internal_offset());

  // Step 5: Remember what is now programmed so callers can restore it later.
  memcpy(&g_settings, settings, sizeof(Nau7802Settings));
  g_settings_valid = true;
  return NAU7802_OK;
}

int nau7802_get_settings(Nau7802Settings* settings_out) {
  GUARD_NONNULL(settings_out);
  GUARD_INITIALIZED(g_initialized && g_settings_valid);
  memcpy(settings_out, &g_settings, sizeof(Nau7802Settings));
  return NAU7802_OK;
}

int nau7802_select_channel(Nau7802Channel channel) {
  GUARD_INITIALIZED(g_initialized);
  const uint8_t chs = (channel == Nau7802Channel::NAU7802_CHANNEL_2) ? NAU7802_CTRL2_CHS : 0u;
  return update_register(NAU7802_REG_CTRL2, NAU7802_CTRL2_CHS, chs);
}

int nau7802_calibrate_internal_offset(void) {
  GUARD_INITIALIZED(g_initialized);

  // Step 1: Select internal offset mode and start; CALS reads 1 until the device finishes.
  GUARD(update_register(NAU7802_REG_CTRL2, NAU7802_CTRL2_CALMOD_MASK | NAU7802_CTRL2_CALS,
                        NAU7802_CTRL2_CALMOD_OFFSET_INTERNAL | NAU7802_CTRL2_CALS));

  // Step 2: Wait for CALS to clear, then check CAL_ERR (section 1.12.3).
  const uint32_t start_ms = millis();
  while (true) {
    uint8_t ctrl2 = 0u;
    GUARD(read_register(NAU7802_REG_CTRL2, &ctrl2));
    if ((ctrl2 & NAU7802_CTRL2_CALS) == 0u) {
      return ((ctrl2 & NAU7802_CTRL2_CAL_ERR) != 0u) ? NAU7802_ERR_CALIBRATION : NAU7802_OK;
    }
    if ((millis() - start_ms) >= NAU7802_CALIBRATION_TIMEOUT_MS) {
      return NAU7802_ERR_TIMEOUT;
    }
    delay(1u);  // Calibration spans several conversions, so millisecond polling is plenty.
  }
}

int nau7802_read_conversion(uint32_t timeout_us, int32_t* code_out) {
  GUARD_NONNULL(code_out);
  GUARD_INITIALIZED(g_initialized);

  // Step 1: Never read ADCO before data ready, or the device re-latches the previous result.
  GUARD(wait_for_data_ready(timeout_us));

  // Step 2: Burst-read the three result bytes so they come from the same conversion (section 4.9).
  uint8_t bytes[NAU7802_CONVERSION_BYTES] = {0u, 0u, 0u};
  GUARD(read_registers(NAU7802_REG_ADCO_B2, bytes, NAU7802_CONVERSION_BYTES));
  *code_out = nau7802_unpack_conversion(bytes);
  return NAU7802_OK;
}

int nau7802_enter_standby(void) {
  GUARD_INITIALIZED(g_initialized);
  // Clearing PUA and PUD shuts the analog block down while keeping every register (section 1.14).
  return update_register(NAU7802_REG_PU_CTRL, NAU7802_PU_CTRL_PUA | NAU7802_PU_CTRL_PUD, 0u);
}

int nau7802_resume(void) {
  GUARD_INITIALIZED(g_initialized);
  GUARD(update_register(NAU7802_REG_PU_CTRL, NAU7802_PU_CTRL_PUA | NAU7802_PU_CTRL_PUD,
                        NAU7802_PU_CTRL_PUA | NAU7802_PU_CTRL_PUD));
  return wait_for_power_up_ready();
}

int nau7802_shutdown(void) {
  GUARD_INITIALIZED(g_initialized);
  // Best effort: the driver forgets the device even if the standby write fails.
  (void) nau7802_enter_standby();
  g_initialized = false;
  return NAU7802_OK;
}

int32_t nau7802_unpack_conversion(const uint8_t bytes[NAU7802_CONVERSION_BYTES]) {
  // Assemble the 24-bit value in the top of a 32-bit word and let the arithmetic shift sign-extend.
  const uint32_t raw = (static_cast<uint32_t>(bytes[0]) << 24u) | (static_cast<uint32_t>(bytes[1]) << 16u) |
                       (static_cast<uint32_t>(bytes[2]) << 8u);
  return static_cast<int32_t>(raw) >> 8;
}

void nau7802_reset_for_test(void) {
  g_initialized    = false;
  g_settings_valid = false;
  memset(&g_config, 0, sizeof(Nau7802Config));
  memset(&g_settings, 0, sizeof(Nau7802Settings));
}

int nau7802_read_register_for_test(uint8_t reg, uint8_t* value_out) {
  GUARD_NONNULL(value_out);
  GUARD_INITIALIZED(g_initialized);
  return read_register(reg, value_out);
}
