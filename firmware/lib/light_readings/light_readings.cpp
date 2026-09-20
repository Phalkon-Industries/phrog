#include "light_readings.hpp"

#include "nau7802.hpp"
#include <string.h>

// ===================== Module State =============================================
static bool                g_initialized = false;
static LightReadingsConfig g_config      = {};

int light_readings_initialize(const LightReadingsConfig* config) {
  // Step 1: Validate the configuration pointer.
  GUARD_NONNULL(config);

  // Step 2: Cache the configuration for sweeps.
  memcpy(&g_config, config, sizeof(LightReadingsConfig));

  g_initialized = true;
  return LIGHT_READINGS_OK;
}

int light_readings_sweep(LightReadingsSweepSample* result_out) {
  GUARD_NONNULL(result_out);
  GUARD_INITIALIZED(g_initialized);
  return LIGHT_READINGS_ERR_NOT_IMPLEMENTED;
}

int light_readings_sweep_n(uint32_t sweep_count, LightReadingsSweepCollection* results_out) {
  GUARD_NONNULL(results_out);
  GUARD_INITIALIZED(g_initialized);
  if ((sweep_count == 0u) || (sweep_count > LIGHT_READINGS_MAX_SWEEP_COUNT)) {
    return LIGHT_READINGS_ERR_SWEEP_CAPACITY_EXCEEDED;
  }
  return LIGHT_READINGS_ERR_NOT_IMPLEMENTED;
}

int light_readings_compute_sweep_stats(const LightReadingsSweepCollection* sweep_collection,
                                       LightReadingsSweepStats*            stats_out) {
  GUARD_NONNULL(sweep_collection);
  GUARD_NONNULL(stats_out);
  return LIGHT_READINGS_ERR_NOT_IMPLEMENTED;
}

int light_readings_shutdown(void) {
  GUARD_INITIALIZED(g_initialized);
  g_initialized = false;
  return LIGHT_READINGS_OK;
}

void light_readings_reset_for_test(void) {
  g_initialized = false;
  memset(&g_config, 0, sizeof(LightReadingsConfig));
}

void light_readings_get_config_for_test(LightReadingsConfig* config_out) {
  if (config_out == NULL) {
    return;
  }
  memcpy(config_out, &g_config, sizeof(LightReadingsConfig));
}
