#ifndef PHROG_SETTINGS_HPP
#define PHROG_SETTINGS_HPP

#include "phoenix_guard.hpp"
#include <stdint.h>

// ===================== Return Codes =============================================
#define PHROG_SETTINGS_OK PHX_OK
#define PHROG_SETTINGS_ERR_INVALID_ARG PHX_ERR_INVALID_ARG
#define PHROG_SETTINGS_ERR_NOT_INITIALIZED PHX_ERR_NOT_INITIALIZED
#define PHROG_SETTINGS_ERR_STORAGE PHX_ERR_COMMUNICATION

// ===================== Settings Version =========================================
// Increment when PhrogSettings changes in a way that requires migration; stored files with a
// different version are discarded and replaced with defaults.
#define PHROG_SETTINGS_FORMAT_VERSION 1u

/**
 * @brief Persistent device settings stored in internal flash (LittleFS, as on phoenix).
 *
 * phrog has no digipots, so the phoenix wiper codes do not carry over. Fields will be added
 * as calibration work lands; reserved bytes keep the on-flash layout stable meanwhile.
 */
struct PhrogSettings {
  uint8_t reserved[8]; /**< Reserved for future fields; zeroed on creation. */
};

/**
 * @brief Initialise the settings module and load from flash, persisting @p defaults if none exist.
 *
 * @param defaults Pointer to default settings to use when flash is empty or invalid.
 * @return PHROG_SETTINGS_OK on success or a negative error code.
 */
int phrog_settings_initialize(const PhrogSettings* defaults);

/** @brief Report whether the settings module has been initialised. */
bool phrog_settings_is_initialized(void);

/**
 * @brief Get a const pointer to the cached settings in RAM, or NULL when not initialised.
 */
const PhrogSettings* phrog_settings_get(void);

/**
 * @brief Update settings in RAM and persist to flash.
 *
 * @param settings Pointer to the new settings to save.
 * @return PHROG_SETTINGS_OK on success or a negative error code.
 */
int phrog_settings_save(const PhrogSettings* settings);

/**
 * @brief Reset settings to the defaults passed at initialisation and persist them.
 *
 * @return PHROG_SETTINGS_OK on success or a negative error code.
 */
int phrog_settings_reset_to_defaults(void);

/** @brief Clear the cache and mark the module uninitialised (tests and controlled shutdown). */
void phrog_settings_deinitialize(void);

#endif  // PHROG_SETTINGS_HPP
