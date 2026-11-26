/*+===================================================================
  File:      configprefs.h

  Summary:   Declares configprefs support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#ifndef RSYS_CONFIGPREFS_H
#define RSYS_CONFIGPREFS_H
#include <stdio.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define CONFIG_PREFS_SECTION_SIZE 800
int config_prefs_validate_line(const char* line);
const char* config_prefs_parameter_help(void);
int config_prefs_seed(const char* path, const char* master);
int config_prefs_remove_startup(const char* path);
void config_prefs_app_section(uint32_t type, uint32_t creator, char section[CONFIG_PREFS_SECTION_SIZE]);
int config_prefs_visit_applications(const char* path, int (*visit)(const char*, void*), void* context);
size_t config_prefs_get(const char* path, const char* section, const char* key, char* value, size_t capacity);
int config_prefs_set(const char* path, const char* section, const char* key, const char* value);
/* Publish a group of values together, preserving all unrelated settings. */
int config_prefs_set_many(const char* path, const char* section, const char* const* keys, const char* const* values,
                          size_t count);
FILE* config_prefs_open(const char* path, const char* section);
/* Windows host drives default to DOS. The mapped Apps tree is independent
     of its containing drive. An empty setting requests first-start setup. */
int config_drive_prefs_load(const char* path, const char* apps);
int config_drive_prefs_save(const char* path, uint32_t drives);
uint32_t config_drive_prefs_mask(void);
int config_drive_sidecars(const char* path);
#define CONFIG_DRIVE_DIRECTORY_LIMIT 256
size_t config_drive_directory_count(void);
const char* config_drive_directory(size_t index);
int config_drive_prefs_save_all(const char* path, uint32_t drives, const char* const* directories, size_t count);

enum ConfigDriveFormat {
    CONFIG_DRIVE_UNDECLARED,
    CONFIG_DRIVE_WINDOWS,
    CONFIG_DRIVE_OSX,
    CONFIG_DRIVE_NETATALK,
    CONFIG_DRIVE_BASILISKII,
    CONFIG_DRIVE_ROMAW
};

/* Stable UI order: Windows, OSX, Netatalk, BasiliskII, ROMAW. */
const char* config_drive_format_name(int format);
int config_drive_format(const char* path);
/* Register the native Finder's PC-only desktop root before starting workers. */
int config_drive_set_windows_desktop(const char* path);
int config_drive_detect_format(const char* directory);
int config_drive_prefs_save_formats(const char* path, const int formats[26], const char* const* directories,
                                    size_t count);
size_t config_drive_label(const char* root, char* label, size_t capacity);
int config_drive_rename(const char* root, const char* label);
#ifdef _WIN32
int ROMlib_win_drive_preferences(const char* path);
int ROMlib_win_default_preferences(void* owner, const char* path);
#endif
int config_prefs_store(const char* path, const char* section, FILE* script);
/* Atomically replace an owned INI section without application-key filtering. */
int config_prefs_store_ini(const char* path, const char* section, FILE* script);
#ifdef __cplusplus
}
#endif
#endif
