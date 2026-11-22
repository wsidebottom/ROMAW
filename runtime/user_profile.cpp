/*+===================================================================
  File:      user_profile.cpp

  Summary:   Manages the .romaw profile and imports installation settings without overwriting current data.

  Classes:   Used: statfs, std::exception, std::runtime_error, std::string, std::vector

  Functions: romaw_dialog_directory, romaw_dialog_remember, romaw_host_volume_kind,
             romaw_profile_legacy_path, romaw_profile_migrate, romaw_profile_path

  Origin:    Existing repository source in executor/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include "rsys/user_profile.h"
extern "C" {
#include "rsys/configprefs.h"
}
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <chrono>
#include <vector>
#include <cerrno>
#include <new>
#ifdef _WIN32
#include <windows.h>
#else
#include <mntent.h>
#include <sys/vfs.h>
#include <pwd.h>
#include <unistd.h>
#endif
namespace fs = std::filesystem;

static constexpr const char* profile_files[] = {"ROMAW.prefs", "ROMAW.ftm"};

extern "C" int romaw_dialog_directory(char* out, size_t capacity)
{
    if (!out || !capacity || capacity > 32768)
        return 0;
    out[0] = 0;
    try {
        char prefs[32768];
        if (!romaw_profile_path("ROMAW.prefs", prefs, sizeof prefs, nullptr, 0))
            return 0;
        std::vector<char> encoded(capacity * 2 + 1);
        size_t length = config_prefs_get(prefs, "FileDialogs", "DirectoryUTF8Hex", encoded.data(), encoded.size());
        if (!length || length % 2 || length / 2 >= capacity)
            return 0;
        auto digit = [](char c) -> int {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            return -1;
        };
        for (size_t i = 0; i < length; i += 2) {
            int high = digit(encoded[i]), low = digit(encoded[i + 1]);
            if (high < 0 || low < 0 || !(high | low)) {
                out[0] = 0;
                return 0;
            }
            out[i / 2] = char((high << 4) | low);
        }
        out[length / 2] = 0;
        return 1;
    }
    catch (const std::exception&) {
        out[0] = 0;
        return 0;
    }
}

extern "C" int romaw_dialog_remember(const char* directory)
{
    if (!directory || !*directory || strlen(directory) >= 32768)
        return 0;
    try {
        char prefs[32768];
        if (!romaw_profile_path("ROMAW.prefs", prefs, sizeof prefs, nullptr, 0))
            return 0;
        std::string encoded;
        for (const unsigned char* p = (const unsigned char*)directory; *p; ++p) {
            encoded += "0123456789abcdef"[*p >> 4];
            encoded += "0123456789abcdef"[*p & 15];
        }
        return config_prefs_set(prefs, "FileDialogs", "DirectoryUTF8Hex", encoded.c_str());
    }
    catch (const std::exception&) {
        return 0;
    }
}

static fs::path profile()
{
#ifdef _WIN32
    wchar_t home[32768];
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", home, 32768);
    if (!n || n >= 32768)
        throw std::runtime_error("USERPROFILE is unavailable");
    fs::path directory = fs::path(home) / ".romaw";
#else
    const char* home = std::getenv("HOME");
    if (!home || !*home) {
        struct passwd* account = getpwuid(getuid());
        home = account ? account->pw_dir : nullptr;
    }
    if (!home || !*home)
        throw std::runtime_error("The user's home directory is unavailable");
    fs::path directory = fs::u8path(home) / ".romaw";
#endif
    if (!directory.is_absolute())
        throw std::runtime_error("The user profile must be an absolute path");
    fs::create_directories(directory);
    fs::create_directories(directory / "cache");
    return directory;
}

static int report(char* error, size_t capacity, const std::exception& exception)
{
    if (error && capacity)
        std::snprintf(error, capacity, "%s", exception.what());
    return 0;
}

static void copy_text(const std::string& value, char* out, size_t capacity)
{
    if (!out || value.size() >= capacity)
        throw std::runtime_error("The profile path exceeds the destination buffer");
    std::memcpy(out, value.c_str(), value.size() + 1);
}

extern "C" int romaw_profile_path(const char* leaf, char* out, size_t capacity, char* error, size_t error_capacity)
{
    try {
        if (!leaf || std::strpbrk(leaf, "/\\") || !std::strcmp(leaf, ".") || !std::strcmp(leaf, "..") ||
            std::strchr(leaf, ':'))
            throw std::runtime_error("Invalid profile filename");
        fs::path directory = profile();
        copy_text((*leaf ? directory / fs::u8path(leaf) : directory).u8string(), out, capacity);
        return 1;
    }
    catch (const std::exception& e) {
        return report(error, error_capacity, e);
    }
}

extern "C" int romaw_profile_legacy_path(char* out, size_t capacity, char* error, size_t error_capacity)
{
    try {
        fs::path directory = profile();
#ifdef _WIN32
        /* Keep ordinary native launches and mounted profile paths identical.
         A short-name spelling is only necessary when the actual profile name
         cannot be represented by the legacy File Manager's code page. */
        wchar_t short_name[32768];
        const wchar_t* name = directory.c_str();
        BOOL substituted = FALSE;
        int count = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, name, -1, nullptr, 0, nullptr, &substituted);
        if (!count || substituted) {
            DWORD n = GetShortPathNameW(directory.c_str(), short_name, 32768);
            if (n && n < 32768) {
                name = short_name;
                substituted = FALSE;
                count = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, name, -1, nullptr, 0, nullptr, &substituted);
            }
        }
        if (!count || substituted)
            throw std::runtime_error("The profile path cannot be represented by the legacy File Manager");
        std::string value(count, '\0');
        WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, name, -1, value.data(), count, nullptr, &substituted);
        value.resize(count - 1);
        copy_text(value, out, capacity);
#else
        copy_text(directory.u8string(), out, capacity);
#endif
        return 1;
    }
    catch (const std::exception& e) {
        return report(error, error_capacity, e);
    }
}

#include "root_metadata.h"

extern "C" int romaw_profile_migrate(const char* installation, char* error, size_t capacity)
{
    fs::path staging;
    try {
        auto directory = profile();
        auto source = fs::u8path(installation);
        for (const auto* file : profile_files) {
            // Existing user settings win over installation files and first-start templates.
            for (const auto& name : {std::string(file), std::string(file) + ".master"}) {
                auto from = source / name, to = directory / file;
                if (fs::exists(to))
                    break;
                if (fs::exists(from))
                    fs::copy_file(from, to, fs::copy_options::skip_existing);
            }
        }
        auto apps = directory / "Apps", old = source / "Apps";
        if (fs::is_symlink(fs::symlink_status(apps)))
            throw std::runtime_error("The profile Apps directory must not be a symbolic link");
        if (fs::is_directory(old) && (!fs::exists(apps) || fs::is_empty(apps)) &&
            fs::weakly_canonical(old) != fs::weakly_canonical(apps)) {
            for (unsigned n = 0; n < 10000; ++n) {
                staging = directory / (".Apps-migration-" + std::to_string(n));
                if (fs::create_directory(staging))
                    break;
                staging.clear();
            }
            if (staging.empty())
                throw std::runtime_error("Cannot reserve an Apps migration directory");
            for (const auto& entry : fs::directory_iterator(old))
                fs::copy(entry.path(), staging / entry.path().filename(),
                         fs::copy_options::recursive | fs::copy_options::copy_symlinks);
            if (fs::exists(apps))
                fs::remove(apps); /* Only an empty directory can be removed here. */
            fs::rename(staging, apps);
            staging.clear();
        }
        fs::create_directories(apps);
        return 1;
    }
    catch (const std::exception& e) {
        /* The installed source is never removed. A partial staging copy is
         retained for inspection/recovery and is never used as the Apps tree. */
        return report(error, capacity, e);
    }
}

extern "C" int romaw_host_volume_kind(const char* path)
{
    try {
#ifdef _WIN32
        auto native = fs::u8path(path).wstring();
        wchar_t root[32768];
        // GetDriveType does not enumerate directory contents or wake a volume.
        if (native.size() >= 3 && native[1] == L':') {
            root[0] = native[0];
            root[1] = L':';
            root[2] = L'\\';
            root[3] = 0;
        }
        else if (!GetVolumePathNameW(native.c_str(), root, 32768))
            return 1;
        UINT type = GetDriveTypeW(root);
        if (type == DRIVE_REMOTE)
            return 2;
        if (type == DRIVE_CDROM)
            return 4;
        if (type == DRIVE_REMOVABLE && (root[0] == L'A' || root[0] == L'B' || root[0] == L'a' || root[0] == L'b'))
            return 3;
#else
        struct statfs info;
        if (!statfs(path, &info)) {
            unsigned long type = info.f_type;
            if (type == 0x6969 || type == 0xff534d42 || type == 0xfe534d42 || type == 0x517b)
                return 2;
            if (type == 0x9660 || type == 0x15013346)
                return 4;
        }
        FILE* mounts = setmntent("/proc/self/mounts", "r");
        if (mounts) {
            struct mntent record, *mount;
            char buffer[8192];
            int floppy = 0;
            while ((mount = getmntent_r(mounts, &record, buffer, sizeof buffer)))
                if (!strcmp(path, mount->mnt_dir) && !strncmp(mount->mnt_fsname, "/dev/fd", 7))
                    floppy = 1;
            endmntent(mounts);
            if (floppy)
                return 3;
        }
#endif
    }
    catch (const std::exception&) {
    }
    return 1;
}
