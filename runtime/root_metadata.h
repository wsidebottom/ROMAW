/*+===================================================================
  File:      root_metadata.h

  Summary:   Declares root metadata support for Macintosh startup, traps and runtime services.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Private implementation, included after the profile path helpers. */
#include <memory>

namespace {
using RootBytes = std::vector<unsigned char>;

static RootBytes read_root_metadata(const fs::path& path)
{
    if (!fs::is_regular_file(path) || fs::file_size(path) > 64 * 1024 * 1024)
        throw std::runtime_error("Invalid root metadata file");
    RootBytes bytes(static_cast<size_t>(fs::file_size(path)));
    std::ifstream input(path, std::ios::binary);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
        throw std::runtime_error("Cannot read root metadata");
    return bytes;
}

static bool root_appledouble(const RootBytes& bytes)
{
    auto word = [&bytes](size_t p) {
        return uint32_t(bytes[p]) << 24 | uint32_t(bytes[p + 1]) << 16 | uint32_t(bytes[p + 2]) << 8 | bytes[p + 3];
    };
    if (bytes.size() < 26 || word(0) != 0x00051607 || (word(4) != 0x00020000 && word(4) != 0x00010000))
        return false;
    size_t count = size_t(bytes[24]) * 256 + bytes[25];
    if (count > (bytes.size() - 26) / 12)
        return false;
    size_t header_end = 26 + count * 12;
    bool finder_info = false;
    for (size_t p = 26; count; --count, p += 12) {
        size_t start = word(p + 4), length = word(p + 8);
        if (length && (start < header_end || start > bytes.size() || length > bytes.size() - start))
            return false;
        if (word(p) == 9 && length >= 32)
            finder_info = true;
    }
    return finder_info;
}

/* Imports preserve existing user records; volume-state updates replace them.
   An exclusive temporary directory also works on profiles without hard links. */
static void publish_root_metadata(const fs::path& destination, const RootBytes& bytes, bool replace = false)
{
    if (!replace && fs::exists(destination))
        return;
    fs::path temporary;
    for (unsigned i = 0; i < 1024; ++i) {
        auto candidate = destination.parent_path() / (".import-" + std::to_string(i));
        if (fs::create_directory(candidate)) {
            temporary = candidate;
            break;
        }
    }
    if (temporary.empty())
        throw std::runtime_error("Cannot reserve root metadata import");
    auto snapshot = temporary / "record";
    try {
        std::ofstream output(snapshot, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        output.close();
        if (!output)
            throw std::runtime_error("Cannot write root metadata import");
#ifdef _WIN32
        if (!MoveFileExW(snapshot.c_str(), destination.c_str(),
                         MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0)) &&
            (replace || (GetLastError() != ERROR_ALREADY_EXISTS && GetLastError() != ERROR_FILE_EXISTS)))
            throw std::runtime_error("Cannot publish root metadata import");
#else
        if (replace ? rename(snapshot.c_str(), destination.c_str()) != 0
                    : link(snapshot.c_str(), destination.c_str()) != 0 && errno != EEXIST)
            throw std::runtime_error("Cannot publish root metadata import");
#endif
        std::error_code ignored;
        fs::remove(snapshot, ignored);
        fs::remove(temporary, ignored);
    }
    catch (...) {
        std::error_code ignored;
        fs::remove(snapshot, ignored);
        fs::remove(temporary, ignored);
        throw;
    }
}

static bool import_root_metadata(const fs::path& source, const fs::path& backup, const fs::path& current)
{
    std::error_code error;
    if (!fs::exists(source, error)) {
        if (error)
            throw fs::filesystem_error("Cannot inspect legacy root metadata", source, error);
        return false;
    }
    if (!fs::is_regular_file(source))
        return false;
    auto bytes = read_root_metadata(source);
    /* Never move an unrelated or damaged file solely because of its name. */
    if (!root_appledouble(bytes))
        return false;
    publish_root_metadata(backup, bytes);
    if (read_root_metadata(backup) != bytes)
        return true; // Retain conflicting originals; current metadata takes priority.
    if (!current.empty())
        publish_root_metadata(current, bytes);
    /* Retain the source if another writer changed it during the import. */
    if (read_root_metadata(source) == bytes)
        fs::remove(source, error); // Read-only drives can keep the old copy.
    return true;
}
} // namespace

extern "C" char* romaw_root_metadata_path(const char* root)
{
    try {
        if (!root || !*root || !fs::path(root).is_absolute()) {
            errno = EINVAL;
            return nullptr;
        }
        char profile_path[32768], error[1024];
        if (!romaw_profile_legacy_path(profile_path, sizeof profile_path, error, sizeof error))
            throw std::runtime_error(error);
        auto normalized = fs::path(root).lexically_normal().generic_u8string();
        while (normalized.size() > 1 && normalized.back() == '/')
            normalized.pop_back();
#ifdef _WIN32
        for (char& c : normalized)
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
#endif
        auto directory = fs::path(profile_path) / "volume-metadata";
        std::string component;
        if (normalized.size() <= 32) {
            for (unsigned char c : normalized) {
                component += "0123456789abcdef"[c >> 4];
                component += "0123456789abcdef"[c & 15];
            }
        }
        else {
            // Keep long UNC/mount names within Windows filename limits.
            // The exact identity below detects collisions before any import.
            uint64_t hashes[] = {14695981039346656037ULL, 7809847782465536322ULL};
            for (unsigned char c : normalized)
                for (auto& hash : hashes)
                    hash = (hash ^ c) * 1099511628211ULL;
            component = "h-";
            for (auto hash : hashes)
                for (int shift = 60; shift >= 0; shift -= 4)
                    component += "0123456789abcdef"[(hash >> shift) & 15];
        }
        directory /= component;
        auto current = directory / "root.appledouble";
        std::string path = current.string();
        // Reserve the caller's return value before changing any legacy files.
        char* result = static_cast<char*>(std::malloc(path.size() + 1));
        if (!result)
            throw std::bad_alloc();
        std::memcpy(result, path.c_str(), path.size() + 1);
        try {
            fs::create_directories(directory);
            RootBytes identity(normalized.begin(), normalized.end());
            publish_root_metadata(directory / "volume.path", identity);
            if (read_root_metadata(directory / "volume.path") != identity)
                throw std::runtime_error("Root metadata volume identity mismatch");
            auto source = fs::path(root) / "%%2F";
            auto backup = directory / "legacy-root.appledouble";
            bool legacy = import_root_metadata(source, backup, current);
            if (legacy || (!fs::exists(source) && fs::exists(backup) && root_appledouble(read_root_metadata(backup))))
                import_root_metadata(fs::path(root) / "._%%2F", directory / "legacy-sidecar.appledouble", {});
        }
        catch (...) {
            std::free(result);
            throw;
        }
        return result;
    }
    catch (const std::bad_alloc&) {
        errno = ENOMEM;
        return nullptr;
    }
    catch (const std::exception&) {
        errno = EIO;
        return nullptr;
    }
}

extern "C" int romaw_volume_finder_info(const char* root, void* bytes, int write)
{
    if (!bytes || (write != 0 && write != 1)) {
        errno = EINVAL;
        return -1;
    }
    std::unique_ptr<char, decltype(&std::free)> metadata(romaw_root_metadata_path(root), &std::free);
    if (!metadata)
        return -1;
    try {
        auto path = fs::path(metadata.get()).parent_path() / "volume.finderinfo";
        if (write) {
            auto data = static_cast<const unsigned char*>(bytes);
            publish_root_metadata(path, RootBytes(data, data + 32), true);
        }
        else {
            if (!fs::exists(path))
                return 0;
            auto data = read_root_metadata(path);
            if (data.size() != 32)
                throw std::runtime_error("Invalid volume Finder information");
            std::memcpy(bytes, data.data(), 32);
        }
        return 1;
    }
    catch (const std::bad_alloc&) {
        errno = ENOMEM;
        return -1;
    }
    catch (const std::exception&) {
        errno = EIO;
        return -1;
    }
}
