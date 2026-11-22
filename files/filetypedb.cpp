/*+===================================================================
  File:      filetypedb.cpp

  Summary:   Implements filetypedb support for Macintosh File Manager and host-file operations.

  Classes:   Declared: DatabaseLock; Used: DatabaseLock, FileTypeMapping, std::error_code, std::ifstream,
             std::istringstream, std::lock_guard, std::mutex, std::ofstream, std::runtime_error, std::string,
             std::vector, std::wstring

  Functions: filetype_map_lookup_path, filetype_map_application_path, filetype_map_extension_path, filetype_map_extensions_path,
             filetype_map_register_application

  Origin:    Existing repository source in executor/src/files; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include "rsys/filetypemap.h"
#include "rsys/archive_types.h"
#include "rsys/user_profile.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <mutex>
#include <map>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#endif
namespace fs = std::filesystem;

namespace {
std::mutex database_mutex;
constexpr char association_description[] = "Macintosh application association";

// Index display descriptions once per profile revision. Finder asks for Kind
// for every row even in icon view, so parsing the FTM for each file is costly.
struct KindDescriptions {
    fs::path path;
    fs::file_time_type modified;
    uintmax_t size = 0;
    using Identity = std::pair<uint32_t, uint32_t>;

    struct Description {
        Identity identity;
        std::string text;
    };

    std::map<std::string, Description> extensions;
    std::map<Identity, std::string> identities;
};

std::string folded_extension(std::string extension)
{
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](char c) { return c >= 'A' && c <= 'Z' ? char(c + 'a' - 'A') : c; });
    return extension;
}

struct DatabaseLock {
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE;

    explicit DatabaseLock(const fs::path& path)
    {
        for (int i = 0; i < 40; ++i) {
            file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_HIDDEN, nullptr);
            if (file != INVALID_HANDLE_VALUE)
                return;
            if (GetLastError() != ERROR_SHARING_VIOLATION)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    }

    bool valid() const
    {
        return file != INVALID_HANDLE_VALUE;
    }

    ~DatabaseLock()
    {
        if (valid())
            CloseHandle(file);
    }
#else
    int file = -1;

    explicit DatabaseLock(const fs::path& path)
    {
        file = open(path.c_str(), O_RDWR | O_CREAT, 0600);
        if (file >= 0 && flock(file, LOCK_EX)) {
            close(file);
            file = -1;
        }
    }

    bool valid() const
    {
        return file >= 0;
    }

    ~DatabaseLock()
    {
        if (valid())
            close(file);
    }
#endif
};
}

// Get Info uses the same directional rules and last enabled match as imports.
// Read afresh so edits to the per-user FTM are visible without restarting Finder.
extern "C" int filetype_map_lookup_path(const char* extension, FileTypeMapping* mapping)
{
    if (!extension || !*extension || !mapping)
        return 0;
    if (romaw_archive_extension(extension))
        return filetype_map_lookup("", extension, 0, 0, 1, mapping);
    try {
        char profile[32768];
        if (!romaw_profile_path("ROMAW.ftm", profile, sizeof profile, nullptr, 0))
            return 0;
        auto path = fs::u8path(profile);
        if (!fs::exists(path) || fs::file_size(path) > 8 * 1024 * 1024)
            return 0;
        std::ifstream input(path, std::ios::binary);
        if (!input)
            return 0;
        std::string text(std::istreambuf_iterator<char>(input), {});
        FileTypeMapping found{};
        if (input.bad() || !filetype_map_lookup(text.c_str(), extension, 0, 0, 1, &found))
            return 0;
        *mapping = found;
        return 1;
    }
    catch (...) {
        return 0;
    }
}

extern "C" int filetype_map_description_path(const char* extension, uint32_t type, uint32_t creator, char* out,
                                             size_t capacity)
{
    if (!out || !capacity)
        return 0;
    out[0] = 0;
    try {
        char profile[32768];
        if (!romaw_profile_path("ROMAW.ftm", profile, sizeof profile, nullptr, 0))
            return 0;
        auto path = fs::u8path(profile);
        auto modified = fs::last_write_time(path);
        auto size = fs::file_size(path);
        if (size > 8 * 1024 * 1024)
            return 0;
        static thread_local KindDescriptions cache;
        if (cache.path != path || cache.modified != modified || cache.size != size) {
            std::ifstream input(path, std::ios::binary);
            if (!input)
                return 0;
            KindDescriptions updated{path, modified, size, {}, {}};
            std::string line;
            while (std::getline(input, line)) {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                FileTypeMapping mapping;
                if (!filetype_map_parse(line.c_str(), &mapping) || !mapping.description[0] ||
                    !std::strcmp(mapping.description, association_description))
                    continue;
                if (mapping.extension[0])
                    updated.extensions[folded_extension(mapping.extension)] = {{mapping.type, mapping.creator},
                                                                               mapping.description};
                if (mapping.type)
                    updated.identities[{mapping.type, mapping.creator}] = mapping.description;
            }
            if (input.bad())
                return 0;
            cache = std::move(updated);
        }

        const std::string* description = nullptr;
        if (extension && *extension) {
            auto match = cache.extensions.find(folded_extension(extension + (*extension == '.')));
            if (match != cache.extensions.end() &&
                (!type || match->second.identity == KindDescriptions::Identity{type, creator}))
                description = &match->second.text;
        }
        if (!description && type) {
            auto match = cache.identities.find({type, creator});
            // The starter FTM uses '????' for types with any creator.
            if (match == cache.identities.end())
                match = cache.identities.find({type, 0x3f3f3f3f});
            if (match != cache.identities.end())
                description = &match->second;
        }
        if (!description || description->size() >= capacity)
            return 0;
        std::memcpy(out, description->c_str(), description->size() + 1);
        return 1;
    }
    catch (...) {
        return 0;
    }
}

extern "C" int filetype_map_extension_path(uint32_t type, uint32_t creator, char* extension, size_t capacity)
{
    if (!extension || !capacity)
        return 0;
    extension[0] = 0;
    try {
        char profile[32768];
        if (!romaw_profile_path("ROMAW.ftm", profile, sizeof profile, nullptr, 0))
            return 0;
        auto path = fs::u8path(profile);
        if (!fs::exists(path) || fs::file_size(path) > 8 * 1024 * 1024)
            return 0;
        std::ifstream input(path, std::ios::binary);
        if (!input)
            return 0;
        std::string text(std::istreambuf_iterator<char>(input), {});
        return !input.bad() && filetype_map_extension(text.c_str(), type, creator, extension, capacity);
    }
    catch (...) {
        return 0;
    }
}

extern "C" int filetype_map_extensions_path(const uint32_t* types, size_t count, uint32_t creator, char* out,
                                            size_t capacity)
{
    if (!out || !capacity)
        return 0;
    out[0] = 0;
    try {
        char profile[32768];
        if (!romaw_profile_path("ROMAW.ftm", profile, sizeof profile, nullptr, 0))
            return 0;
        auto path = fs::u8path(profile);
        if (!fs::exists(path) || fs::file_size(path) > 8 * 1024 * 1024)
            return 0;
        std::ifstream input(path, std::ios::binary);
        if (!input)
            return 0;
        std::string text(std::istreambuf_iterator<char>(input), {});
        return !input.bad() && filetype_map_extensions(text.c_str(), types, count, creator, out, capacity);
    }
    catch (...) {
        return 0;
    }
}

extern "C" int filetype_map_application_path(uint32_t type, uint32_t creator, char* application, size_t capacity,
                                             int native_path)
{
    if (!application || !capacity)
        return 0;
    application[0] = 0;
    try {
        char profile[32768], path[4096];
        if (!romaw_profile_path("ROMAW.ftm", profile, sizeof profile, nullptr, 0))
            return 0;
        auto file = fs::u8path(profile);
        if (!fs::exists(file) || fs::file_size(file) > 8 * 1024 * 1024)
            return 0;
        std::ifstream input(file, std::ios::binary);
        if (!input)
            return 0;
        std::string text(std::istreambuf_iterator<char>(input), {});
        if (input.bad() || !filetype_map_application(text.c_str(), type, creator, path, sizeof path) ||
            !fs::is_regular_file(fs::u8path(path)))
            return 0;
#ifdef _WIN32
        if (native_path) {
            auto wide = fs::u8path(path).wstring();
            BOOL lost = FALSE;
            int n = WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, application, capacity, nullptr, &lost);
            if (!n || lost) {
                application[0] = 0;
                return 0;
            }
            return 1;
        }
#else
        (void)native_path;
#endif
        if (std::strlen(path) >= capacity)
            return 0;
        std::strcpy(application, path);
        return 1;
    }
    catch (...) {
        return 0;
    }
}

extern "C" int filetype_map_register_application(const char* application, uint32_t creator, const uint32_t* types,
                                                 size_t count, int native_path)
{
    if (!application || !*application || !types || !count || count > 1024)
        return 0;
    std::lock_guard<std::mutex> guard(database_mutex);
    fs::path temporary;
    try {
        std::string name = application;
#ifdef _WIN32
        if (native_path) {
            int n = MultiByteToWideChar(CP_ACP, 0, application, -1, nullptr, 0);
            if (n <= 1)
                return 0;
            std::wstring wide(n, 0);
            MultiByteToWideChar(CP_ACP, 0, application, -1, wide.data(), n);
            wide.pop_back();
            name = fs::path(wide).u8string();
        }
#else
        (void)native_path;
#endif
        auto app = fs::absolute(fs::u8path(name));
        name = app.u8string();
        if (name.size() >= sizeof(FileTypeMapping{}.application) || !fs::is_regular_file(app))
            return 0;
        char profile[32768];
        if (!romaw_profile_path("ROMAW.ftm", profile, sizeof profile, nullptr, 0))
            return 0;
        fs::path path = fs::u8path(profile);
        fs::create_directories(path.parent_path());
        DatabaseLock lock(path.parent_path() / ".ROMAW.ftm.lock");
        if (!lock.valid())
            return 0;
        std::string original;
        if (fs::exists(path)) {
            if (fs::file_size(path) > 8 * 1024 * 1024)
                return 0;
            std::ifstream input(path, std::ios::binary);
            if (!input)
                return 0;
            original.assign(std::istreambuf_iterator<char>(input), {});
            if (input.bad())
                return 0;
        }
        std::vector<bool> found(count, false);
        std::istringstream lines(original);
        std::string line, text;
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            FileTypeMapping mapping;
            if (filetype_map_parse(line.c_str(), &mapping) && mapping.creator == creator)
                for (size_t i = 0; i < count; ++i)
                    if (mapping.type == types[i] && !romaw_archive_type(types[i]) &&
                        !romaw_archive_extension(mapping.extension)) {
                        found[i] = true;
                        if (name != mapping.application) {
                            std::strcpy(mapping.application, name.c_str());
                            char formatted[FILETYPE_MAP_LINE_CAP];
                            if (!filetype_map_format(&mapping, formatted, sizeof formatted))
                                return 0;
                            line = formatted;
                        }
                    }
            text += line + "\r\n";
        }
        for (size_t i = 0; i < count; ++i)
            if (!found[i] && !romaw_archive_type(types[i])) {
                FileTypeMapping mapping{};
                mapping.type = types[i];
                mapping.creator = creator;
                std::strcpy(mapping.application, name.c_str());
                std::strcpy(mapping.description, association_description);
                char formatted[FILETYPE_MAP_LINE_CAP];
                if (!filetype_map_format(&mapping, formatted, sizeof formatted))
                    return 0;
                text += std::string(formatted) + "\r\n";
                for (size_t j = i + 1; j < count; ++j)
                    if (types[j] == types[i])
                        found[j] = true;
            }
        if (text == original)
            return 1;
        if (text.size() > 8 * 1024 * 1024)
            return 0;
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        temporary = path.parent_path() / (".ROMAW.ftm-" + std::to_string(stamp));
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(text.data(), text.size());
        output.close();
        if (!output)
            throw std::runtime_error("Cannot save file type database");
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot publish file type database");
#else
        fs::rename(temporary, path);
#endif
        return 1;
    }
    catch (...) {
        if (!temporary.empty()) {
            std::error_code ignored;
            fs::remove(temporary, ignored);
        }
        return 0;
    }
}
