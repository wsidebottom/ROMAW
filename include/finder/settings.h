/*+===================================================================
  File:      settings.h

  Summary:   ifndef FINDER_SETTINGS_H

  Classes:   Used: std::filesystem::path, std::wstring

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/finder; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef FINDER_SETTINGS_H
#define FINDER_SETTINGS_H
#include <windows.h>
#include <filesystem>
#include <string>

namespace finder {
// Persist the host's normal rectangle, even while maximized or minimized.
void save_host_window(HWND window, const std::filesystem::path& ini);
// Restore host placement, falling back to the historical Desktop geometry.
bool restore_host_window(HWND window, const std::filesystem::path& ini);
// Move legacy storage only when current storage is absent; never merge trees.
std::filesystem::path settings_directory(const std::filesystem::path& profile, const std::filesystem::path& local);
// Best-effort removal of retired global keys; a missing file stays missing.
void remove_startup_preferences(const std::filesystem::path& path);
// This historical UTF-16 hash is a persistence format; changing it loses keys.
std::wstring settings_path_key(const std::filesystem::path& path);
// Translate legacy storage paths on read without rewriting the INI bytes.
std::wstring settings_value(const std::filesystem::path& ini, const wchar_t* section, const std::wstring& key,
                            const wchar_t* fallback = L"");
// Prefer the current key, then fall back to the old storage-path hash.
std::wstring settings_path_value(const std::filesystem::path& ini, const wchar_t* section,
                                 const std::filesystem::path& path);
}
#endif
