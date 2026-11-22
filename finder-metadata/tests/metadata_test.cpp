/*+===================================================================
  File:      metadata_test.cpp

  Summary:   Implements metadata test support for finder metadata package support.

  Classes:   Used: FinderItemPosition, FinderWindowState, std::ifstream, std::ofstream, std::string, std::vector

  Functions: main

  Origin:    Existing repository source in packages/finder-metadata/tests; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             finder-metadata/LICENSE.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include "finder_metadata/metadata.h"
#include "rsys/configprefs.h"
#include "rsys/user_profile.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <cstring>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cstdlib>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace fs = std::filesystem;

int main(int argc, char** argv)
{
    if (argc == 3 && !strcmp(argv[1], "--roundtrip")) {
        std::string folder = (fs::u8path(argv[2]) / "Folder").u8string();
        FinderWindowState state;
        char error[512];
        if (finder_window_read(folder.c_str(), &state, error, sizeof error) != 1) {
            fprintf(stderr, "%s\n", error);
            return 1;
        }
        printf("%d,%d,%d,%d,%d\n", state.x, state.y, state.width, state.height, state.view);
        if (finder_window_write(folder.c_str(), &state, error, sizeof error) != 1) {
            fprintf(stderr, "%s\n", error);
            return 1;
        }
        return 0;
    }
    auto root =
        argc > 1
            ? fs::u8path(argv[1])
            : fs::temp_directory_path() /
                  ("finder-metadata-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "Folder");
#ifdef _WIN32
    assert(SetEnvironmentVariableW(L"USERPROFILE", root.c_str()));
#else
    assert(!setenv("HOME", root.c_str(), 1));
#endif
    std::string folder = (root / "Folder").u8string(), parent = root.u8string();
    char error[512] = {};
    FinderWindowState state{41, 52, 640, 400, 2}, read{};
    assert(finder_window_read(folder.c_str(), &read, error, sizeof error) == 0);
    assert(finder_window_write(folder.c_str(), &state, error, sizeof error) == 1);
    assert(finder_window_read(folder.c_str(), &read, error, sizeof error) == 1);
    assert(read.x == 41 && read.y == 52 && read.width == 640 && read.height == 400 && read.view == 2);
    assert(finder_window_read((folder + "/./").c_str(), &read, error, sizeof error) == 1);
    assert(read.x == 41 && read.height == 400);
    auto small = state;
    small.view = 3;
    assert(finder_window_write(folder.c_str(), &small, error, sizeof error) == 1);
    assert(finder_window_read(folder.c_str(), &read, error, sizeof error) == 1 && read.view == 3);
    small.view = 1;
    assert(finder_window_write(folder.c_str(), &small, error, sizeof error) == 1);
    assert(finder_window_read(folder.c_str(), &read, error, sizeof error) == 1 && read.view == 1);
    small.view = 0;
    assert(finder_window_write(folder.c_str(), &small, error, sizeof error) == 1);
    assert(finder_window_read(folder.c_str(), &read, error, sizeof error) == 1 && read.view == 0);
    assert(finder_window_write(folder.c_str(), &state, error, sizeof error) == 1);
    for (int i = 0; i < 250; ++i) {
        int x = i * 2, y = i * 3;
        auto name = "Icon " + std::to_string(i);
        assert(finder_icon_position(parent.c_str(), name.c_str(), &x, &y, 1, error, sizeof error) == 1);
    }
    int x = 0, y = 0;
    assert(finder_icon_position(parent.c_str(), "Icon 249", &x, &y, 0, error, sizeof error) == 1 && x == 498 &&
           y == 747);
    int stale_x = 1, stale_y = 2;
    assert(finder_icon_position(parent.c_str(), "Renamed", &stale_x, &stale_y, 1, error, sizeof error) == 1);
    assert(finder_metadata_rename(parent.c_str(), "Icon 249", "Renamed", error, sizeof error) == 1);
    assert(finder_icon_position(parent.c_str(), "Icon 249", &x, &y, 0, error, sizeof error) == 0);
    assert(finder_icon_position(parent.c_str(), "Renamed", &x, &y, 0, error, sizeof error) == 1 && x == 498 &&
           y == 747);
    assert(finder_window_read(folder.c_str(), &read, error, sizeof error) == 1);
    // Malformed inputs are never replaced by a fresh database.
    fs::create_directory(root / "Broken");
    std::ofstream(root / "Broken" / ".DS_Store") << "broken";
    assert(finder_icon_position((root / "Broken").u8string().c_str(), "item", &x, &y, 1, error, sizeof error) == -1);
    assert(fs::file_size(root / "Broken" / ".DS_Store") == 6);
    auto attack = root / "Malformed";
    fs::create_directory(attack);
    assert(finder_icon_position(attack.u8string().c_str(), "A", &x, &y, 1, error, sizeof error) == 1);
    auto read_bytes = [](const fs::path& file) {
        std::ifstream input(file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), {});
    };
    auto original = read_bytes(attack / ".DS_Store");
#ifdef _WIN32
    assert(SetFileAttributesW((attack / ".DS_Store").c_str(), FILE_ATTRIBUTE_NORMAL));
#endif
    const char tag[] = {0, 0, 0, 1, 0, 'A', 'I', 'l', 'o', 'c', 'b', 'l', 'o', 'b'};
    auto tag_at = original.find(std::string(tag, sizeof tag));
    assert(tag_at != std::string::npos);
    for (int kind = 0; kind < 4; ++kind) {
        auto bad = original;
        auto put = [&](size_t at, uint32_t value) {
            for (int shift = 24; shift >= 0; shift -= 8)
                bad[at++] = value >> shift;
        };
        if (kind == 0)
            put(tag_at + 14, 0xffffffff);
        if (kind == 1) {
            bad.replace(tag_at + 10, 4, "ustr");
            put(tag_at + 14, 0x80000000);
        }
        if (kind == 2) {
            put(8, 0xfffffffe);
            put(16, 0xfffffffe);
        }
        if (kind == 3)
            put(tag_at - 4, 0xffffffff);
        std::ofstream file(attack / ".DS_Store", std::ios::binary);
        assert(file);
        file.write(bad.data(), bad.size());
        file.close();
        assert(file);
        assert(finder_icon_position(attack.u8string().c_str(), "A", &x, &y, 1, error, sizeof error) == -1);
        assert(read_bytes(attack / ".DS_Store") == bad);
        assert(!fs::exists(attack / ".DS_Store.romaw-tmp"));
    }
    const char* paths[] = {"Folder % with spaces", "Unicode \xc3\xa9\n[NotASection]"};
    FinderWindowState states[2] = {{1, 2, 640, 400, 0}, {3, 4, 800, 600, 2}};
    assert(finder_native_session_write(paths, states, 2));
    char prefs[32768], value[64];
    assert(romaw_profile_path("ROMAW.prefs", prefs, sizeof prefs, nullptr, 0));
    assert(config_prefs_set(prefs, "Unrelated", "Keep", "unchanged"));
    std::vector<std::string> restored;
    assert(finder_native_session_read(
        [](const char* path, void* context) {
            ((std::vector<std::string>*)context)->push_back(path);
            return 1;
        },
        &restored));
    assert(restored.size() == 2 && restored[0] == paths[0] && restored[1] == paths[1]);
    assert(finder_native_session_window(paths[1], &read));
    assert(read.x == 3 && read.y == 4 && read.width == 800 && read.height == 600 && read.view == 2);
    assert(finder_native_session_write(nullptr, nullptr, 0));
    restored.clear();
    assert(finder_native_session_read(
        [](const char* path, void* context) {
            ((std::vector<std::string>*)context)->push_back(path);
            return 1;
        },
        &restored));
    assert(restored.empty());
    assert(config_prefs_get(prefs, "Unrelated", "Keep", value, sizeof value));
    assert(!strcmp(value, "unchanged"));
    FILE* bad = tmpfile();
    assert(bad);
    fputs("[Injected]\nBad=1\n", bad);
    assert(!config_prefs_store_ini(prefs, "Finder.OpenWindows", bad));
    fclose(bad);
    assert(config_prefs_get(prefs, "Unrelated", "Keep", value, sizeof value));
    assert(!strcmp(value, "unchanged"));
    FinderItemPosition item = {"Queued", 200, 300, 1};
    finder_layout_schedule(parent.c_str(), &item, 1);
    finder_window_schedule(folder.c_str(), &state);
    finder_metadata_flush();
    item.present = 0;
    assert(finder_layout_read(parent.c_str(), &item, 1, error, sizeof error) == 1);
    assert(item.present && item.x == 200 && item.y == 300);
    for (const char* format : {".AppleDouble", ".finf", ".rsrc"}) {
        auto legacy = root / (std::string("Legacy") + format);
        auto child = legacy / "Child";
        fs::create_directories(legacy / format);
        fs::create_directories(child / format);
        auto name = child.u8string();
        assert(finder_window_write(name.c_str(), &state, error, sizeof error) == 1);
        assert(finder_window_read(name.c_str(), &read, error, sizeof error) == 1);
        assert(read.x == state.x && read.width == state.width);
        FinderItemPosition saved = {"Before", 81, 92, 1};
        assert(finder_layout_write(name.c_str(), &saved, 1, error, sizeof error) == 1);
        assert(finder_metadata_rename(name.c_str(), "Before", "After", error, sizeof error) == 1);
        int x = 0, y = 0;
        assert(finder_icon_position(name.c_str(), "After", &x, &y, 0, error, sizeof error) == 1);
        assert(x == 81 && y == 92);
        finder_window_schedule(name.c_str(), &state);
        finder_layout_schedule(name.c_str(), &saved, 1);
        finder_metadata_flush();
        assert(!fs::exists(legacy / ".DS_Store"));
        assert(!fs::exists(child / ".DS_Store"));
        // Existing stores are usable even on a legacy-format folder.
        fs::copy_file(root / ".DS_Store", child / ".DS_Store");
        assert(finder_layout_write(name.c_str(), &saved, 1, error, sizeof error) == 1);
        saved.present = 0;
        assert(finder_layout_read(name.c_str(), &saved, 1, error, sizeof error) == 1 && saved.present);
    }
    // A genuine OS X companion wins over a legacy metadata directory.
    auto mixed = root / "Mixed";
    fs::create_directories(mixed / ".AppleDouble");
    unsigned char apple[26] = {0, 5, 22, 7, 0, 2, 0, 0};
    std::ofstream(mixed / "._File", std::ios::binary).write(reinterpret_cast<char*>(apple), sizeof apple);
    std::string mixed_name = mixed.u8string();
    assert(finder_layout_write(mixed_name.c_str(), &item, 1, error, sizeof error) == 1);
    assert(fs::exists(mixed / ".DS_Store"));
    if (argc == 1)
        fs::remove_all(root);
}
