#include "phrog_settings.hpp"

#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <string.h>

using namespace Adafruit_LittleFS_Namespace;

// ===================== File Storage =============================================
// One file in the core's internal LittleFS: a version byte followed by the raw struct. The
// version lets a layout change be detected and the old file discarded instead of misread.
static const char* const k_settings_file_path = "/phrog_settings.dat";

struct SettingsFileHeader {
  uint8_t version;
};

// ===================== Module State =============================================
static bool          g_initialized      = false;
static bool          g_filesystem_ready = false;
static PhrogSettings g_cached_settings  = {};
static PhrogSettings g_defaults         = {};

// Mounts the internal filesystem once; InternalFS.begin is safe to repeat but slow.
static bool ensure_filesystem(void) {
  if (!g_filesystem_ready) {
    g_filesystem_ready = InternalFS.begin();
  }
  return g_filesystem_ready;
}

// Keeps a stored or requested sweep count inside the range the measurement code can handle.
static void clamp_settings(PhrogSettings* settings) {
  if (settings->measurement_sweep_count < PHROG_SETTINGS_MIN_SWEEP_COUNT) {
    settings->measurement_sweep_count = PHROG_SETTINGS_MIN_SWEEP_COUNT;
  }
  if (settings->measurement_sweep_count > PHROG_SETTINGS_MAX_SWEEP_COUNT) {
    settings->measurement_sweep_count = PHROG_SETTINGS_MAX_SWEEP_COUNT;
  }
}

// Reads the file into @p settings; false for missing, short, or wrong-version files.
static bool load_from_flash(PhrogSettings* settings) {
  if (!InternalFS.exists(k_settings_file_path)) {
    return false;
  }

  File file(InternalFS);
  if (!file.open(k_settings_file_path, FILE_O_READ)) {
    return false;
  }

  // Step 1: The header must be present and match this build's format.
  SettingsFileHeader header  = {};
  bool               success = (file.read(&header, sizeof(header)) == static_cast<int>(sizeof(header))) &&
                 (header.version == PHROG_SETTINGS_FORMAT_VERSION);

  // Step 2: The payload must be complete; a short read means an interrupted or foreign file.
  if (success) {
    success = (file.read(settings, sizeof(PhrogSettings)) == static_cast<int>(sizeof(PhrogSettings)));
  }
  file.close();
  return success;
}

// Writes @p length bytes as the whole file, replacing any previous contents.
static bool write_file(const uint8_t* bytes, size_t length) {
  // Step 1: Remove the old file so the write starts from an empty one rather than appending.
  if (InternalFS.exists(k_settings_file_path)) {
    InternalFS.remove(k_settings_file_path);
  }

  File file(InternalFS);
  if (!file.open(k_settings_file_path, FILE_O_WRITE)) {
    return false;
  }
  const bool success = (file.write(bytes, length) == length);
  file.close();
  return success;
}

// Serialises header plus struct and writes them as one file.
static bool save_to_flash(const PhrogSettings* settings) {
  uint8_t            image[sizeof(SettingsFileHeader) + sizeof(PhrogSettings)];
  SettingsFileHeader header = {PHROG_SETTINGS_FORMAT_VERSION};
  memcpy(image, &header, sizeof(header));
  memcpy(image + sizeof(header), settings, sizeof(PhrogSettings));
  return write_file(image, sizeof(image));
}

int phrog_settings_initialize(const PhrogSettings* defaults) {
  // Step 1: Keep the defaults for reset_to_defaults and for a missing or invalid file.
  GUARD_NONNULL(defaults);
  memcpy(&g_defaults, defaults, sizeof(PhrogSettings));
  clamp_settings(&g_defaults);

  // Step 2: Mount the internal filesystem.
  if (!ensure_filesystem()) {
    return PHROG_SETTINGS_ERR_STORAGE;
  }

  // Step 3: Prefer what is stored; fall back to the defaults and persist them so the file exists.
  if (load_from_flash(&g_cached_settings)) {
    clamp_settings(&g_cached_settings);
  }
  else {
    memcpy(&g_cached_settings, &g_defaults, sizeof(PhrogSettings));
    if (!save_to_flash(&g_cached_settings)) {
      return PHROG_SETTINGS_ERR_STORAGE;
    }
  }

  g_initialized = true;
  return PHROG_SETTINGS_OK;
}

bool phrog_settings_is_initialized(void) {
  return g_initialized;
}

const PhrogSettings* phrog_settings_get(void) {
  return g_initialized ? &g_cached_settings : NULL;
}

int phrog_settings_save(const PhrogSettings* settings) {
  GUARD_NONNULL(settings);
  GUARD_INITIALIZED(g_initialized);

  // Write the clamped copy first so RAM never holds something flash does not.
  PhrogSettings clamped = *settings;
  clamp_settings(&clamped);
  if (!save_to_flash(&clamped)) {
    return PHROG_SETTINGS_ERR_STORAGE;
  }
  memcpy(&g_cached_settings, &clamped, sizeof(PhrogSettings));
  return PHROG_SETTINGS_OK;
}

int phrog_settings_reset_to_defaults(void) {
  GUARD_INITIALIZED(g_initialized);
  return phrog_settings_save(&g_defaults);
}

void phrog_settings_deinitialize(void) {
  memset(&g_cached_settings, 0, sizeof(PhrogSettings));
  g_initialized = false;
}

int phrog_settings_erase_for_test(void) {
  if (!ensure_filesystem()) {
    return PHROG_SETTINGS_ERR_STORAGE;
  }
  if (InternalFS.exists(k_settings_file_path) && !InternalFS.remove(k_settings_file_path)) {
    return PHROG_SETTINGS_ERR_STORAGE;
  }
  return PHROG_SETTINGS_OK;
}

int phrog_settings_write_raw_for_test(const uint8_t* bytes, size_t length) {
  GUARD_NONNULL(bytes);
  if (!ensure_filesystem()) {
    return PHROG_SETTINGS_ERR_STORAGE;
  }
  return write_file(bytes, length) ? PHROG_SETTINGS_OK : PHROG_SETTINGS_ERR_STORAGE;
}
