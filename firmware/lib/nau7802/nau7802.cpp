#include "nau7802.hpp"

#include <Wire.h>
#include <string.h>

// ===================== Module State =============================================
static bool          g_initialized = false;
static Nau7802Config g_config      = {};

int nau7802_initialize(const Nau7802Config* config) {
  // Step 1: Validate inputs before touching the bus.
  GUARD_NONNULL(config);
  GUARD_NONNULL(config->wire);

  // Step 2: Cache the bus configuration for later transactions.
  memcpy(&g_config, config, sizeof(Nau7802Config));

  // TODO(phrog): reset the device, wait for power-up ready, and verify the revision register.
  g_initialized = true;
  return NAU7802_OK;
}

int nau7802_apply_settings(const Nau7802Settings* settings) {
  GUARD_NONNULL(settings);
  GUARD_INITIALIZED(g_initialized);
  return NAU7802_ERR_NOT_IMPLEMENTED;
}

int nau7802_read_conversion(uint32_t timeout_us, int32_t* code_out) {
  (void) timeout_us;
  GUARD_NONNULL(code_out);
  GUARD_INITIALIZED(g_initialized);
  return NAU7802_ERR_NOT_IMPLEMENTED;
}

int nau7802_enter_standby(void) {
  GUARD_INITIALIZED(g_initialized);
  return NAU7802_ERR_NOT_IMPLEMENTED;
}

int nau7802_shutdown(void) {
  GUARD_INITIALIZED(g_initialized);
  g_initialized = false;
  return NAU7802_OK;
}

void nau7802_reset_for_test(void) {
  g_initialized = false;
  memset(&g_config, 0, sizeof(Nau7802Config));
}
