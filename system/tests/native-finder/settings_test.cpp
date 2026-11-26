/*+===================================================================
  File:      settings_test.cpp

  Summary:   Exercise migration and obsolete-key cleanup under a unique temporary root. The actual user settings,
             Desktop and Trash are never modified by this fixture.

  Classes:   Used: std::ifstream, std::ofstream, std::string, std::wstring

  Functions: main

  Origin:    Existing repository source in executor/tests/native-finder; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

// Exercise migration and obsolete-key cleanup under a unique temporary root.
// The actual user settings, Desktop and Trash are never modified by this
// fixture.

#include "finder/settings.h"
#include <windows.h>
#include <objbase.h>
#include <cassert>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using namespace finder;

static void put(const fs::path& ini, const wchar_t* section, const std::wstring& key, const std::wstring& value)
{
    assert(WritePrivateProfileStringW(section, key.c_str(), value.c_str(), ini.c_str()));
}

static std::string bytes(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

int main()
{
    wchar_t temporary[32768], id[64];
    assert(GetTempPathW(std::size(temporary), temporary));
    GUID guid;
    assert(SUCCEEDED(CoCreateGuid(&guid)));
    assert(StringFromGUID2(guid, id, std::size(id)));
    auto root = fs::path(temporary) / (L"Finder-settings-" + std::wstring(id));
    auto old = root / L"Finder64";
    auto profile = root / L".romaw";
    auto current = profile / L"Finder";
    auto nested = fs::path(L"Desktop/Nested \u65e5\u672c");
    fs::create_directories(old / nested);
    fs::create_directories(old / L"Trash");
    std::ofstream(old / nested / L"File") << "desktop data";
    std::ofstream(old / L"Trash/Item") << "trash data";
    auto ini = old / L"settings.ini";
    // Preserve Unicode paths through the actual Windows INI APIs.
    std::ofstream(ini, std::ios::binary).write("\xff\xfe", 2);
    put(ini, L"Session", L"Count", L"2");
    put(ini, L"Session", L"Window1", (old / nested).wstring());
    put(ini, L"Session", L"Window2", (root / L"Finder64-backup").wstring());
    put(ini, L"WindowsNative", settings_path_key(old / nested), L"10,20,640,480");
    put(ini, L"PositionsNative", settings_path_key(old / nested / L"File"), L"72,52");
    put(ini, L"PutAway", settings_path_key(old / L"Trash/Item"), (old / nested / L"Original").wstring());
    put(ini, L"Labels", L"1", L"Cool project");
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, ini.c_str());
    auto original = bytes(ini);
    assert(settings_directory(profile, root) == current);
    ini = current / L"settings.ini";
    assert(!fs::exists(old) && bytes(ini) == original);
    assert(bytes(current / nested / L"File") == "desktop data");
    assert(bytes(current / L"Trash/Item") == "trash data");
    assert(settings_value(ini, L"Session", L"Count") == L"2");
    assert(settings_value(ini, L"Session", L"Window1") == (current / nested).wstring());
    assert(settings_value(ini, L"Session", L"Window2") == (root / L"Finder64-backup").wstring());
    assert(settings_value(ini, L"Labels", L"1") == L"Cool project");
    assert(settings_path_value(ini, L"WindowsNative", current / nested) == L"10,20,640,480");
    assert(settings_path_value(ini, L"PositionsNative", current / nested / L"File") == L"72,52");
    assert(settings_path_value(ini, L"PutAway", current / L"Trash/Item") == (current / nested / L"Original").wstring());
    put(ini, L"WindowsNative", settings_path_key(current / nested), L"30,40,800,600");
    assert(settings_path_value(ini, L"WindowsNative", current / nested) == L"30,40,800,600");
    assert(settings_directory(profile, root) == current);
    // Existing current settings take precedence without changing either tree.
    fs::create_directory(old);
    std::ofstream(old / L"settings.ini") << "legacy settings";
    auto saved = bytes(ini);
    assert(settings_directory(profile, root) == current && bytes(ini) == saved);
    assert(bytes(old / L"settings.ini") == "legacy settings");
    auto blocked = root / L"Blocked";
    fs::create_directories(blocked / L"Finder64");
    fs::create_directory(blocked / L".romaw");
    std::ofstream(blocked / L"Finder64/settings.ini") << "preserved";
    std::ofstream(blocked / L".romaw/Finder") << "existing file";
    bool failed = false;
    try {
        settings_directory(blocked / L".romaw", blocked);
    }
    catch (const fs::filesystem_error&) {
        failed = true;
    }
    assert(failed && bytes(blocked / L"Finder64/settings.ini") == "preserved");
    assert(bytes(blocked / L".romaw/Finder") == "existing file");
    assert(settings_directory(root / L"Fresh", {}) == root / L"Fresh/Finder");
    auto local = root / L"Local";
    auto previous = local / L"Finder";
    auto migrated = root / L"Moved";
    fs::create_directories(previous / L"Shortcut Icons");
    std::ofstream(previous / L"Shortcut Icons/Application.ico") << "icon data";
    std::ofstream(previous / L"settings.ini", std::ios::binary).write("\xff\xfe", 2);
    put(previous / L"settings.ini", L"Session", L"Window1", (previous / nested).wstring());
    assert(settings_directory(migrated, local) == migrated / L"Finder");
    assert(!fs::exists(previous));
    assert(bytes(migrated / L"Finder/Shortcut Icons/Application.ico") == "icon data");
    assert(settings_value(migrated / L"Finder/settings.ini", L"Session", L"Window1") ==
           (migrated / L"Finder" / nested).wstring());
    auto runtime_prefs = root / L"ROMAW.prefs";
    std::ofstream(runtime_prefs)
        << "[ROMAW]\r\nStartup=ROMAW Finder\r\nBrowser=old app\r\nOther=keep\r\n[Elsewhere]\r\nStartup=unchanged\r\n";
    remove_startup_preferences(runtime_prefs);
    auto cleaned = bytes(runtime_prefs);
    assert(cleaned.find("ROMAW Finder") == std::string::npos);
    assert(cleaned.find("Browser=") == std::string::npos);
    assert(cleaned.find("Other=keep") != std::string::npos);
    assert(cleaned.find("Startup=unchanged") != std::string::npos);
    remove_startup_preferences(root / L"missing.ini");
    assert(!fs::exists(root / L"missing.ini"));
    // Exercise real Windows placement without injecting input or changing the
    // user's settings. Maximized/minimized frames retain a separate normal size.
    HWND host = CreateWindowExW(0, L"STATIC", L"Finder placement fixture", WS_OVERLAPPEDWINDOW, 120, 120, 640, 480,
                                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    assert(host);
    auto host_ini = root / L"host.ini";
    save_host_window(host, host_ini);
    WINDOWPLACEMENT original_placement{};
    original_placement.length = sizeof original_placement;
    assert(GetWindowPlacement(host, &original_placement));
    SetWindowPos(host, nullptr, 200, 180, 800, 600, SWP_NOZORDER | SWP_NOACTIVATE);
    assert(restore_host_window(host, host_ini));
    WINDOWPLACEMENT restored{};
    restored.length = sizeof restored;
    assert(GetWindowPlacement(host, &restored));
    assert(EqualRect(&original_placement.rcNormalPosition, &restored.rcNormalPosition));
    ShowWindow(host, SW_SHOWMAXIMIZED);
    save_host_window(host, host_ini);
    ShowWindow(host, SW_RESTORE);
    assert(restore_host_window(host, host_ini) && IsZoomed(host));
    ShowWindow(host, SW_MINIMIZE);
    save_host_window(host, host_ini);
    assert(restore_host_window(host, host_ini) && IsZoomed(host));
    ShowWindow(host, SW_RESTORE);
    ShowWindow(host, SW_MINIMIZE);
    save_host_window(host, host_ini);
    assert(restore_host_window(host, host_ini) && !IsIconic(host) && !IsZoomed(host));
    assert(GetWindowPlacement(host, &restored));
    assert(EqualRect(&original_placement.rcNormalPosition, &restored.rcNormalPosition));
    for (const auto* invalid :
         {L"", L"1,2,0,480", L"1,2,640,-1", L"1,2,640,480junk", L"9999999999999999999999999,2,640,480"}) {
        put(host_ini, L"HostWindow", L"Geometry", invalid);
        assert(!restore_host_window(host, host_ini));
    }
    assert(WritePrivateProfileStringW(L"HostWindow", nullptr, nullptr, host_ini.c_str()));
    put(host_ini, L"WindowsNative", settings_path_key(fs::path(L"Desktop")), L"120,120,640,480");
    assert(restore_host_window(host, host_ini));
    assert(GetWindowPlacement(host, &restored));
    assert(EqualRect(&original_placement.rcNormalPosition, &restored.rcNormalPosition));
    put(host_ini, L"HostWindow", L"Geometry", L"30000,30000,640,480");
    assert(restore_host_window(host, host_ini));
    RECT visible{};
    assert(GetWindowRect(host, &visible));
    MONITORINFO monitor{};
    monitor.cbSize = sizeof monitor;
    assert(GetMonitorInfoW(MonitorFromWindow(host, MONITOR_DEFAULTTONEAREST), &monitor));
    RECT intersection{};
    assert(IntersectRect(&intersection, &visible, &monitor.rcWork));
    DestroyWindow(host);
    fs::remove_all(root);
    std::cout
        << "PASS: settings migration preserves INI bytes, Unicode Desktop/Trash data, sessions, geometry, icon positions and Put Away; retries and conflicts preserve data; host geometry, maximize/minimize, legacy placement and disconnected monitors\n";
}
