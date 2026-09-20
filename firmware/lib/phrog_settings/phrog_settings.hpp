#ifndef PHROG_SETTINGS_HPP
#define PHROG_SETTINGS_HPP

#include "phoenix_guard.hpp"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ===================== Return Codes =============================================
#define PHROG_SETTINGS_OK PHX_OK
#define PHROG_SETTINGS_ERR_INVALID_ARG PHX_ERR_INVALID_ARG
#define PHROG_SETTINGS_ERR_NOT_INITIALIZED PHX_ERR_NOT_INITIALIZED
#define PHROG_SETTINGS_ERR_STORAGE PHX_ERR_COMMUNICATION

// ===================== Settings Version =========================================
// Increment when PhrogSettings changes layout. A stored file with a different version is
// discarded and replaced with the defaults, which is the whole migration strategy for now.
#define PHROG_SETTINGS_FORMAT_VERSION 1u

// Bounds for the sweep count so a corrupt or hand-edited value cannot stall a measurement.
#define PHROG_SETTINGS_MIN_SWEEP_COUNT 1u
#define PHROG_SETTINGS_MAX_SWEEP_COUNT 500u

/**
 * @brief Persistent per-unit settings stored in internal flash (LittleFS on the 28 KB the Adafruit
 * core reserves between the application image and the bootloader; survives reflashing).
 *
 * Fields are things that belong to this particular unit and must outlive a power cycle. Per-session
 * choices belong in CLI arguments, tuning constants in device_setup. Reserved bytes keep the
 * on-flash layout stable while calibration fields (per-LED extinction coefficients, for example)
 * are still being worked out; consuming them does not need a version bump, changing existing
 * fields does.
 */
struct PhrogSettings {
  uint16_t measurement_sweep_count;         /**< Sweeps averaged per baseline or sample. */
  float    thermistor_calibration_offset_c; /**< Added to every sample temperature. */
  uint8_t  reserved[24];                    /**< Zeroed on creation; for future calibration fields. */
};

/**
 * @brief Mount the internal filesystem and load settings, persisting @p defaults when the file is
 * missing, short, or from another format version.
 *
 * @param defaults Settings to use when nothing valid is stored.
 * @return PHROG_SETTINGS_OK, PHROG_SETTINGS_ERR_INVALID_ARG on NULL, or PHROG_SETTINGS_ERR_STORAGE
 *         when the filesystem cannot be mounted or the defaults cannot be written.
 */
int phrog_settings_initialize(const PhrogSettings* defaults);

/** @brief Report whether the settings module has been initialised. */
bool phrog_settings_is_initialized(void);

/**
 * @brief Get a const pointer to the cached settings in RAM, or NULL when not initialised.
 *
 * Reads never touch flash; the pointer stays valid until phrog_settings_deinitialize.
 */
const PhrogSettings* phrog_settings_get(void);

/**
 * @brief Update settings in RAM and persist them to flash.
 *
 * @param settings New settings; the sweep count is clamped into its valid range before saving.
 * @return PHROG_SETTINGS_OK or a negative error code; the RAM copy is updated only on success.
 */
int phrog_settings_save(const PhrogSettings* settings);

/**
 * @brief Reset settings to the defaults passed at initialisation and persist them.
 *
 * @return PHROG_SETTINGS_OK or a negative error code.
 */
int phrog_settings_reset_to_defaults(void);

/** @brief Clear the cache and mark the module uninitialised; flash is left untouched. */
void phrog_settings_deinitialize(void);

/**
 * @brief Delete the settings file so the next initialise takes the defaults path (test helper).
 *
 * @return PHROG_SETTINGS_OK or PHROG_SETTINGS_ERR_STORAGE.
 */
int phrog_settings_erase_for_test(void);

/**
 * @brief Overwrite the settings file with arbitrary bytes to simulate corruption (test helper).
 *
 * @param bytes Raw file contents.
 * @param length Number of bytes to write.
 * @return PHROG_SETTINGS_OK or PHROG_SETTINGS_ERR_STORAGE.
 */
int phrog_settings_write_raw_for_test(const uint8_t* bytes, size_t length);

#endif  // PHROG_SETTINGS_HPP
