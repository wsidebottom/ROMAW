/*+===================================================================
  File:      archive.cpp

  Summary:   Encode and verify StuffIt archives through an owned C++ wrapper.

  Classes:   Entry, OwnedBytes; std::unique_ptr.

  Functions: romaw::stuffit::encode.

  Origin:    ROMAW adapter for benletchford/stuffit-rs, using its published C ABI.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/
#include "romaw_stuffit.hpp"
#include "stuffit_ffi.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>

namespace romaw::stuffit {
namespace {
void check(bool success)
{
    if (!success)
        throw std::runtime_error(std::string("StuffIt: ") + stuffit_last_error());
}

struct OwnedBytes {
    StuffitOwnedBytes value{};

    ~OwnedBytes()
    {
        stuffit_owned_bytes_free(value);
    }
};
}

Bytes encode(const std::vector<Entry>& entries, const std::function<bool()>& cancelled)
{
    auto poll = [&] {
        if (cancelled && cancelled())
            throw std::runtime_error("StuffIt creation cancelled");
    };
    poll();
    if (stuffit_ffi_abi_version() != STUFFIT_FFI_ABI_VERSION)
        throw std::runtime_error("Incompatible StuffIt library ABI");
    if (entries.empty() || entries.size() > 65535)
        throw std::runtime_error("StuffIt requires 1 to 65535 entries");
    std::unique_ptr<StuffitWriter, decltype(&stuffit_writer_free)> writer(stuffit_writer_new(), stuffit_writer_free);
    check(bool(writer));
    std::map<std::string, const Entry*> expected;
    constexpr size_t limit = 256 * 1024 * 1024;
    size_t total = 0;
    for (const auto& entry : entries) {
        poll();
        if (entry.name.empty() || entry.name.find('\0') != std::string::npos ||
            !expected.emplace(entry.name, &entry).second)
            throw std::runtime_error("Invalid or duplicate StuffIt entry name");
        if (std::count(entry.name.begin(), entry.name.end(), '/') > 128)
            throw std::runtime_error("StuffIt folder nesting exceeds 128 levels");
        if (entry.folder && (!entry.data.empty() || !entry.resource.empty()))
            throw std::runtime_error("StuffIt folders cannot contain file forks");
        for (size_t bytes : {entry.name.size(), entry.data.size(), entry.resource.size(), size_t(256)}) {
            if (bytes > limit - total)
                throw std::runtime_error("StuffIt transfer exceeds the 256 MiB archive limit");
            total += bytes;
        }
        StuffitNewEntry value{};
        value.name = entry.name.c_str();
        value.data_fork = {entry.data.data(), entry.data.size()};
        value.resource_fork = {entry.resource.data(), entry.resource.size()};
        std::copy(entry.type.begin(), entry.type.end(), value.file_type);
        std::copy(entry.creator.begin(), entry.creator.end(), value.creator);
        value.creation_date = entry.created;
        value.modification_date = entry.modified;
        value.finder_flags = entry.flags;
        value.is_folder = entry.folder;
        check(stuffit_writer_add_entry(writer.get(), &value) == 0);
    }
    OwnedBytes output;
    check(stuffit_writer_finish(writer.get(), 13, &output.value) == 0);
    poll();
    std::unique_ptr<StuffitArchive, decltype(&stuffit_archive_free)> archive(
        stuffit_archive_parse_bytes(output.value.ptr, output.value.len), stuffit_archive_free);
    check(bool(archive));
    if (stuffit_archive_entry_count(archive.get()) != expected.size())
        throw std::runtime_error("StuffIt verification lost entries");
    for (size_t i = 0; i < entries.size(); ++i) {
        poll();
        StuffitEntryInfo info{};
        check(stuffit_archive_entry_info(archive.get(), i, &info) == 0);
        auto found = expected.find(info.name);
        if (found == expected.end())
            throw std::runtime_error("StuffIt verification changed an entry name");
        const auto& entry = *found->second;
        if (bool(info.is_folder) != entry.folder || info.finder_flags != entry.flags ||
            info.creation_date != entry.created || info.modification_date != entry.modified ||
            !std::equal(entry.type.begin(), entry.type.end(), info.file_type) ||
            !std::equal(entry.creator.begin(), entry.creator.end(), info.creator))
            throw std::runtime_error("StuffIt verification changed Finder metadata");
        for (int fork : {STUFFIT_FORK_DATA, STUFFIT_FORK_RESOURCE}) {
            StuffitBytes bytes{};
            check(stuffit_archive_entry_fork(archive.get(), i, fork, &bytes) == 0);
            const auto& original = fork == STUFFIT_FORK_DATA ? entry.data : entry.resource;
            if (bytes.len != original.size() || (bytes.len && std::memcmp(bytes.ptr, original.data(), bytes.len)))
                throw std::runtime_error("StuffIt verification changed a fork");
        }
        expected.erase(found);
    }
    poll();
    return Bytes(output.value.ptr, output.value.ptr + output.value.len);
}
}
