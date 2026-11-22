/*+===================================================================
  File:      stuffit.cpp

  Summary:   Encode privately staged Macintosh files and folders without discarding metadata.

  Classes:   romaw::stuffit::Entry; CopyJob.

  Functions: finder::write_stuffit_folder, finder::write_macbinary_file,
             finder::read_macbinary_file, finder::lookup_host_type.

  Origin:    ROMAW integration with the pinned stuffit-rs C ABI and C++ bridge.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/
#include "finder/stuffit.h"
#include "romaw_stuffit.hpp"
#include "romaw_archives.hpp"
#include "rsys/filetypemap.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace finder {
bool lookup_host_type(const fs::path& path, uint32_t& type, uint32_t& creator, bool& strip)
{
    FileTypeMapping mapping{};
    if (!filetype_map_lookup_path(path.extension().u8string().c_str(), &mapping))
        return false;
    type = mapping.type;
    creator = mapping.creator;
    strip = mapping.strip != 0;
    return true;
}

bool read_macbinary_file(const fs::path& source, ImportedFile& file, bool data)
{
    std::ifstream stream(source, std::ios::binary);
    Bytes header(128);
    stream.read(reinterpret_cast<char*>(header.data()), header.size());
    if (stream.gcount() != 128 || header[0] || !header[1] || header[1] > 63 || header[74] || header[82])
        return false; // Ordinary .bin data is still eligible for FTM mapping.
    auto info = romaw::archives::inspect_macbinary(header, fs::file_size(source));
    auto read = [&](uint64_t offset, size_t count) {
        Bytes bytes(count);
        stream.seekg(offset);
        if (count && !stream.read(reinterpret_cast<char*>(bytes.data()), count))
            throw std::runtime_error("The MacBinary source changed or became unavailable");
        return bytes;
    };
    file.name = fs::u8path(info.name);
    file.data_offset = info.data_offset;
    file.data_size = info.data_size;
    file.resource_offset = info.resource_offset;
    file.resource_size = info.resource_size;
    auto secondary = read(128, info.secondary_size);
    file.prefix = header;
    file.prefix.insert(file.prefix.end(), secondary.begin(), secondary.end());
    file.metadata =
        romaw::archives::macbinary_metadata(info, secondary, read(info.resource_offset, info.resource_size));
    file.finder_info = romaw::archives::metadata_part(file.metadata, 9);
    if (data)
        file.data = read(info.data_offset, info.data_size);
    return true;
}

void write_macbinary_file(const fs::path& staged, const fs::path& name, const fs::path& output, CopyJob& job)
{
    romaw::archives::Item item{name.filename().u8string(), false, read_bytes(staged, romaw::archives::byte_limit), {}};
    auto companions = mac_companions(staged);
    if (!companions.empty()) {
        if (companions.size() != 1)
            throw std::runtime_error("MacBinary needs normalized Macintosh metadata");
        item.metadata = read_bytes(companions.front(), romaw::archives::byte_limit);
    }
    auto bytes = romaw::archives::macbinary(item, [&] { return job.stop.load(); });
    if (job.stop)
        throw std::runtime_error("MacBinary creation cancelled");
    std::ofstream stream(output, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    stream.close();
    if (!stream || read_bytes(output, bytes.size()) != bytes)
        throw std::runtime_error("Cannot verify the staged MacBinary archive");
}

void write_stuffit_folder(const fs::path& staged, const fs::path& name, const fs::path& output, CopyJob& job, bool zip)
{
    std::vector<romaw::stuffit::Entry> entries;
    size_t total = 0;
    constexpr size_t limit = 256 * 1024 * 1024;
    auto add = [&](const fs::path& path, const fs::path& archive_name) {
        if (job.stop)
            throw std::runtime_error("StuffIt creation cancelled");
        romaw::stuffit::Entry entry;
        entry.name = archive_name.generic_u8string();
        entry.folder = fs::is_directory(path);
        WIN32_FILE_ATTRIBUTE_DATA attributes{};
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes))
            throw std::system_error(GetLastError(), std::system_category());
        auto mac_time = [](FILETIME time) {
            uint64_t seconds = (uint64_t(time.dwHighDateTime) << 32 | time.dwLowDateTime) / 10000000;
            constexpr uint64_t epoch = 9561628800ULL; // 1601 to 1904.
            return seconds <= epoch ? uint32_t(0) : uint32_t(std::min(seconds - epoch, uint64_t(UINT32_MAX)));
        };
        entry.created = mac_time(attributes.ftCreationTime);
        entry.modified = mac_time(attributes.ftLastWriteTime);
        // Preserve canonical AppleDouble files as archive entries as well as
        // populating native StuffIt fields. The engine does not expose all
        // FinderInfo bytes, comments or extended AppleDouble attributes.
        auto info = mac_file(path);
        entry.flags = info.flags;
        for (unsigned i = 0; i < 4; ++i) {
            entry.type[i] = info.type >> (24 - i * 8);
            entry.creator[i] = info.creator >> (24 - i * 8);
        }
        entry.resource = std::move(info.fork);
        auto account = [&](uint64_t bytes) {
            if (bytes > limit - total)
                throw std::runtime_error("StuffIt transfer exceeds the 256 MiB archive limit");
            total += size_t(bytes);
        };
        account(entry.name.size() + 256);
        account(entry.resource.size());
        if (!entry.folder) {
            account(fs::file_size(path));
            entry.data = read_bytes(path, limit);
        }
        else if (!entry.resource.empty())
            throw std::runtime_error("A folder resource fork cannot be represented by StuffIt");
        entries.push_back(std::move(entry));
    };
    add(staged, name.filename());
    for (const auto& item : fs::recursive_directory_iterator(staged))
        add(item.path(), name.filename() / item.path().lexically_relative(staged));
    for (const auto& companion : mac_companions(staged))
        add(companion, L"._" + name.filename().wstring());
    Bytes bytes;
    if (zip) {
        std::vector<romaw::archives::Item> logical;
        for (const auto& entry : entries) {
            logical.push_back({entry.name, entry.folder, entry.data, {}});
        }
        bytes =
            romaw::archives::zip(romaw::archives::canonicalize(std::move(logical)), [&] { return job.stop.load(); });
    }
    else
        bytes = romaw::stuffit::encode(entries, [&] { return job.stop.load(); });
    if (job.stop)
        throw std::runtime_error("StuffIt creation cancelled");
    std::ofstream stream(output, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    stream.close();
    if (!stream || read_bytes(output, bytes.size()) != bytes)
        throw std::runtime_error("Cannot verify the staged StuffIt archive");
}
}
