#include "phrog_settings.hpp"

#include <string.h>

// ===================== Module State =============================================
static bool          g_initialized     = false;
static PhrogSettings g_cached_settings = {};
static PhrogSettings g_defaults        = {};

int phrog_settings_initialize(const PhrogSettings* defaults) {
  // Step 1: Validate the defaults pointer.
  GUARD_NONNULL(defaults);

  // Step 2: Store defaults for reset_to_defaults.
  memcpy(&g_defaults, defaults, sizeof(PhrogSettings));

  // TODO(phrog): mount InternalFS and load /phrog_settings.dat, mirroring phoenix_settings.cpp.
  memcpy(&g_cached_settings, defaults, sizeof(PhrogSettings));
  g_initialized = true;
  return PHROG_SETTINGS_OK;
}

bool phrog_settings_is_initialized(void) {
  return g_initialized;
}

const PhrogSettings* phrog_settings_get(void) {
  if (!g_initialized) {
    return NULL;
  }
  return &g_cached_settings;
}

int phrog_settings_save(const PhrogSettings* settings) {
  GUARD_NONNULL(settings);
  GUARD_INITIALIZED(g_initialized);
  memcpy(&g_cached_settings, settings, sizeof(PhrogSettings));
  // TODO(phrog): persist to flash.
  return PHROG_SETTINGS_OK;
}

int phrog_settings_reset_to_defaults(void) {
  GUARD_INITIALIZED(g_initialized);
  memcpy(&g_cached_settings, &g_defaults, sizeof(PhrogSettings));
  // TODO(phrog): persist to flash.
  return PHROG_SETTINGS_OK;
}

void phrog_settings_deinitialize(void) {
  memset(&g_cached_settings, 0, sizeof(PhrogSettings));
  g_initialized = false;
}
