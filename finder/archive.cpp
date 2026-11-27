/*+===================================================================
  File:      archive.cpp

  Summary:   Stage verified archives and publish only to compatible host folders or mounted images.

  Classes:   ArchiveSource, ArchiveDestination, TransferStaging; CopyJob.

  Functions: finder::archive_destination_allowed, finder::extract_archives.

  Origin:    ROMAW archive codecs and existing transactional host/image copy services.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/
#include "finder/archive.h"
#include "finder/image_copy.h"
#include "romaw_archives.hpp"
#include "rsys/configprefs.h"
#include "rsys/mac_storage.h"
#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>
#include <system_error>

namespace finder {
namespace {
Bytes source_bytes(const ArchiveSource& source)
{
    if (!source.volume)
        return read_bytes(source.path, romaw::archives::byte_limit);
    FinderImageEntry entry{};
    if (ROMlib_finder_image_info(source.volume, source.parent, source.name.c_str(), &entry) || entry.directory ||
        entry.id != source.id || entry.data_size > romaw::archives::byte_limit)
        throw std::runtime_error("The image archive is unavailable or too large");
    int16_t file;
    if (ROMlib_finder_image_open_source_fork(source.volume, source.parent, source.name.c_str(), 0, &file))
        throw std::runtime_error("Cannot open the archive's data fork");
    Bytes bytes;
    try {
        bytes.resize(entry.data_size);
        for (size_t offset = 0; offset < bytes.size();) {
            unsigned size = unsigned(std::min(bytes.size() - offset, size_t(64 * 1024)));
            if (ROMlib_finder_image_read(file, bytes.data() + offset, size))
                throw std::runtime_error("Cannot read the archive's data fork");
            offset += size;
        }
    }
    catch (...) {
        ROMlib_finder_image_close_fork(file);
        throw;
    }
    if (ROMlib_finder_image_close_fork(file))
        throw std::runtime_error("Cannot close the archive's data fork");
    return bytes;
}
}

bool archive_destination_allowed(const ArchiveDestination& destination)
{
    if (destination.volume)
        return !image_copy_private(destination.volume, destination.directory) &&
               !ROMlib_finder_image_writable(destination.volume);
    std::error_code error;
    auto resolved = fs::canonical(destination.path, error);
    if (error || resolved.empty() || !fs::is_directory(resolved, error) || error)
        return false;
    auto path = resolved.u8string();
    int format = config_drive_format(path.c_str());
    return format > CONFIG_DRIVE_WINDOWS || (format == CONFIG_DRIVE_UNDECLARED && config_drive_sidecars(path.c_str()));
}

std::shared_ptr<CopyJob> extract_archives(Copies& copies, const std::vector<ArchiveSource>& sources,
                                          const ArchiveDestination& destination, std::wstring& formats)
{
    if (!archive_destination_allowed(destination))
        throw std::runtime_error("Archives can only be extracted to a writable Mac-compatible drive or image");
    std::vector<romaw::archives::Item> items;
    std::set<romaw::archives::Format> detected;
    size_t total = 0;
    for (const auto& source : sources) {
        romaw::archives::Format format;
        auto expanded = romaw::archives::expand(source_bytes(source), {}, &format);
        detected.insert(format);
        for (auto& item : expanded) {
            for (size_t size : {item.data.size(), item.metadata.size(), item.name.size(), size_t(256)}) {
                if (size > romaw::archives::byte_limit - total)
                    throw std::runtime_error("Extraction exceeds 256 MiB");
                total += size;
            }
            items.push_back(std::move(item));
        }
    }
    using Format = romaw::archives::Format;
    for (auto format : detected) {
        if (!formats.empty())
            formats += L", ";
        switch (format) {
        case Format::zip: formats += L"ZIP"; break;
        case Format::stuffit: formats += L"StuffIt"; break;
        case Format::stuffit5: formats += L"StuffIt 5"; break;
        case Format::macbinary: formats += L"MacBinary"; break;
        case Format::binhex: formats += L"BinHex"; break;
        }
    }
    romaw::archives::validate(items);
    // Resolve UTF-8 and Windows case equivalence before any staging output.
    std::vector<fs::path> names;
    for (const auto& item : items) {
        auto name = fs::u8path(item.name);
        for (const auto& existing : names)
            if (CompareStringOrdinal(existing.c_str(), -1, name.c_str(), -1, TRUE) == CSTR_EQUAL)
                throw std::runtime_error("Archive names collide on the destination filesystem");
        names.push_back(std::move(name));
        if (destination.volume && !item.metadata.empty()) {
            unsigned count = 0;
            if (!romaw_mac_storage_table(item.metadata.data(), item.metadata.size(), &count))
                throw std::runtime_error("Invalid archived metadata");
            for (unsigned i = 0; i < count; ++i) {
                ROMAWMacStorageEntry entry{};
                if (!romaw_mac_storage_entry(item.metadata.data(), item.metadata.size(), item.metadata.size(), i,
                                             &entry))
                    throw std::runtime_error("Invalid archived metadata");
                if ((entry.id != 9 && entry.id != 2) || (entry.id == 9 && entry.length > 32))
                    throw std::runtime_error("This image cannot represent the archive's extended AppleDouble metadata");
            }
        }
    }
    auto staging = std::make_shared<TransferStaging>();
    auto write = [](const fs::path& path, const Bytes& bytes) {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        out.close();
        if (!out || read_bytes(path, bytes.size()) != bytes)
            throw std::runtime_error("Cannot verify staged extraction");
    };
    for (size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        auto path = staging->path / names[i];
        fs::create_directories(item.directory ? path : path.parent_path());
        if (!item.directory)
            write(path, item.data);
        if (!item.metadata.empty())
            write(path.parent_path() / (L"._" + path.filename().wstring()), item.metadata);
    }
    std::vector<fs::path> roots;
    for (const auto& item : fs::directory_iterator(staging->path))
        if (item.path().filename().wstring().rfind(L"._", 0) != 0)
            roots.push_back(item.path());
    // Recheck policy immediately before starting a real host/image transaction.
    if (!archive_destination_allowed(destination))
        throw std::runtime_error("The extraction destination changed");
    auto job = destination.volume
                   ? copy_into_image(copies, roots, {}, destination.volume, destination.directory, destination.path)
                   : copies.start(roots, destination.path, TransferKind::copy);
    if (job)
        job->source_owner = staging;
    return job;
}
}
