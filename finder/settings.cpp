/*+===================================================================
  File:      settings.cpp

  Summary:   Native state persistence and compatibility migration. Directory migration preserves file bytes; old
             absolute values and path-hash keys are translated on read so saved windows and icon positions remain
             usable.

  Classes:   Used: std::error_code, std::vector, std::wstring

  Functions: finder::remove_startup_preferences, finder::settings_directory, finder::settings_path_key,
             finder::settings_path_value, finder::settings_value

  Origin:    Existing repository source in executor/src/finder; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

// Native state persistence and compatibility migration. Directory migration
// preserves file bytes; old absolute values and path-hash keys are translated
// on read so saved windows and icon positions remain usable.

#include "finder/settings.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <sstream>
#include <cstdint>
#include <vector>

namespace finder {
namespace fs = std::filesystem;

namespace {
// The old roots remain lookup keys for paths recorded before the move.
std::array<fs::path, 2> previous_roots;

// Translate a rooted storage path without rewriting unrelated text or sibling
// prefixes; preserve the original suffix spelling.
std::wstring remap_path(const std::wstring& value, const fs::path& from, const fs::path& to)
{
    auto prefix = from.lexically_normal().make_preferred().wstring();
    auto normalized = value;
    std::replace(normalized.begin(), normalized.end(), L'/', L'\\');
    if (normalized.size() < prefix.size() ||
        CompareStringOrdinal(normalized.data(), int(prefix.size()), prefix.data(), int(prefix.size()), TRUE) !=
            CSTR_EQUAL ||
        (normalized.size() != prefix.size() && normalized[prefix.size()] != L'\\'))
        return value;
    return to.wstring() + value.substr(prefix.size());
}
}

// Move one existing directory as a unit on the first launch that uses .romaw.
fs::path settings_directory(const fs::path& profile, const fs::path& local)
{
    auto current = profile / L"Finder";
    previous_roots = {};
    if (!local.empty())
        previous_roots = {local / L"Finder", local / L"Finder64"};
    if (!fs::exists(current)) {
        fs::create_directories(profile);
        for (const auto& source : previous_roots)
            if (!source.empty() && fs::is_directory(source)) {
                std::error_code error;
                fs::rename(source, current, error);
                if (error && !fs::is_directory(current))
                    throw fs::filesystem_error("Cannot move Finder settings", source, current, error);
                break;
            }
    }
    fs::create_directories(current);
    return current;
}

// Best-effort removal of retired global startup keys before emulator
// initialization, without creating a missing preference file.
void remove_startup_preferences(const fs::path& path)
{
    std::error_code error;
    if (fs::is_regular_file(path, error))
        for (const auto* key : {L"Startup", L"Browser"})
            WritePrivateProfileStringW(L"ROMAW", key, nullptr, path.c_str());
}

// Preserve the historical seed and UTF-16 hashing units: these decimal keys
// are part of the on-disk settings format.
std::wstring settings_path_key(const fs::path& path)
{
    uint64_t hash = 1469598103934665603ULL;
    for (wchar_t c : path.wstring()) {
        hash ^= uint16_t(c);
        hash *= 1099511628211ULL;
    }
    return std::to_wstring(hash);
}

// Read a Windows INI value and translate old storage-root paths in memory,
// leaving the serialized file unchanged.
std::wstring settings_value(const fs::path& ini, const wchar_t* section, const std::wstring& key,
                            const wchar_t* fallback)
{
    std::vector<wchar_t> buffer(32768);
    GetPrivateProfileStringW(section, key.c_str(), fallback, buffer.data(), DWORD(buffer.size()), ini.c_str());
    auto directory = ini.parent_path();
    for (const auto& old : previous_roots)
        if (!old.empty()) {
            auto mapped = remap_path(buffer.data(), old, directory);
            if (mapped != buffer.data())
                return mapped;
        }
    return buffer.data();
}

// Prefer current path-keyed settings, then retry the old storage-path key
// after migration. External paths need no fallback.
std::wstring settings_path_value(const fs::path& ini, const wchar_t* section, const fs::path& path)
{
    auto value = settings_value(ini, section, settings_path_key(path));
    if (!value.empty())
        return value;
    auto directory = ini.parent_path();
    for (const auto& old : previous_roots)
        if (!old.empty()) {
            auto old_path = remap_path(path.wstring(), directory, old);
            if (old_path != path.wstring()) {
                value = settings_value(ini, section, settings_path_key(old_path));
                if (!value.empty())
                    break;
            }
        }
    return value;
}

// WINDOWPLACEMENT keeps the restored rectangle independently of the visible
// maximized/minimized frame, so neither state destroys the user's normal size.
void save_host_window(HWND window, const fs::path& ini)
{
    WINDOWPLACEMENT placement{};
    placement.length = sizeof placement;
    if (!GetWindowPlacement(window, &placement))
        return;
    const auto& r = placement.rcNormalPosition;
    auto geometry = std::to_wstring(r.left) + L"," + std::to_wstring(r.top) + L"," + std::to_wstring(r.right - r.left) +
                    L"," + std::to_wstring(r.bottom - r.top);
    WritePrivateProfileStringW(L"HostWindow", L"Geometry", geometry.c_str(), ini.c_str());
    bool maximized = placement.showCmd == SW_SHOWMAXIMIZED || (placement.flags & WPF_RESTORETOMAXIMIZED);
    WritePrivateProfileStringW(L"HostWindow", L"Maximized", maximized ? L"1" : L"0", ini.c_str());
}

bool restore_host_window(HWND window, const fs::path& ini)
{
    auto geometry = settings_value(ini, L"HostWindow", L"Geometry");
    if (geometry.empty())
        geometry = settings_path_value(ini, L"WindowsNative", fs::path(L"Desktop"));
    std::wistringstream input(geometry);
    long long x, y, width, height;
    wchar_t a, b, c;
    if (!(input >> x >> a >> y >> b >> width >> c >> height) || a != L',' || b != L',' || c != L',' ||
        !(input >> std::ws).eof() || x < -32768 || x > 32767 || y < -32768 || y > 32767 || width < 320 ||
        width > 32768 || height < 240 || height > 32768)
        return false;
    RECT r{LONG(x), LONG(y), LONG(x + width), LONG(y + height)};
    // Reconnect to an available monitor if a previous display was removed.
    // Preserve valid placements that deliberately span more than one display.
    MONITORINFO monitor{};
    monitor.cbSize = sizeof monitor;
    if (!MonitorFromRect(&r, MONITOR_DEFAULTTONULL) &&
        GetMonitorInfoW(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), &monitor)) {
        const auto& work = monitor.rcWork;
        width = std::min<long long>(width, work.right - work.left);
        height = std::min<long long>(height, work.bottom - work.top);
        x = std::clamp<long long>(x, work.left, work.right - width);
        y = std::clamp<long long>(y, work.top, work.bottom - height);
        r = {LONG(x), LONG(y), LONG(x + width), LONG(y + height)};
    }
    WINDOWPLACEMENT placement{};
    placement.length = sizeof placement;
    placement.rcNormalPosition = r;
    placement.showCmd =
        GetPrivateProfileIntW(L"HostWindow", L"Maximized", 0, ini.c_str()) ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    return SetWindowPlacement(window, &placement) != FALSE;
}

}
