/*+===================================================================
  File:      virtual_hfs.h

  Summary:   Declares virtual hfs support for disk images package support.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in packages/disk-images/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A sparse RAM catalog for classic File Manager access to modern containers. */
#include "hfs_medium.h"

static std::string device_name(const Volume& volume, unsigned index)
{
    std::string name;
    try {
        name = hfs_name(fs::u8path(volume.name)).substr(1);
    }
    catch (...) {
        name = "Image " + std::to_string(index + 1);
    }
    if (name.size() > 27)
        name.resize(27);
    return name;
}

extern "C" int romaw_images_target_utf8(romaw_image_set* set, const char* path, char* source, size_t source_capacity,
                                        char* guest, size_t guest_capacity)
{
    try {
        if (!set || !path || !source || !guest)
            return 0;
        std::lock_guard<std::mutex> lock(set->mutex);
        auto requested = catalog_path(fs::u8path(path));
        if (!set->items.count(requested))
            return 0;
        for (unsigned i = 0; i < set->volumes.size(); ++i) {
            auto relative = requested.lexically_relative(catalog_path(fs::u8path(set->volumes[i].path)));
            if (relative.empty() || *relative.begin() == "..")
                continue;
            auto name = device_name(set->volumes[i], i);
            std::wstring wide;
            for (unsigned char c : name)
                wide += c < 128 ? c : roman[c - 128].value;
            auto target = "image:" + component(wide) + "/";
            if (relative != ".") {
                auto alias = set->guest_paths.find(requested);
                target +=
                    alias == set->guest_paths.end() ? relative.generic_u8string() : alias->second.generic_u8string();
            }
            auto original = set->source.u8string();
            if (original.size() >= source_capacity || target.size() >= guest_capacity)
                return 0;
            memcpy(source, original.c_str(), original.size() + 1);
            memcpy(guest, target.c_str(), target.size() + 1);
            return 1;
        }
    }
    catch (...) {
    }
    return 0;
}

extern "C" int romaw_hfs_allocate(hfsfile*, unsigned long, int (*)(void*, uint64_t, uint64_t, size_t), void*);

/* Classic names retain their spelling where possible. Longer/Unicode names
   and HFS collation collisions receive deterministic, unique sibling aliases. */
static fs::path guest_component(const fs::path& source, std::set<std::string>& used, unsigned sequence)
{
    std::string name;
    try {
        name = hfs_name(source).substr(1);
    }
    catch (...) {
        name = source.u8string();
        for (char& c : name)
            if ((unsigned char)c >= 128 || c == ':' || !c)
                c = '_';
    }
    auto key = [](const std::string& name) {
        std::string value;
        for (unsigned char c : name)
            value += (char)hfs_charorder[c];
        return value;
    };
    if (name.empty() || name.size() > 31 || used.count(key(name))) {
        char suffix[16];
        do {
            snprintf(suffix, sizeof suffix, "~%08X", sequence++);
            name = name.substr(0, 22) + suffix;
        } while (used.count(key(name)));
    }
    used.insert(key(name));
    std::wstring wide;
    for (unsigned char c : name)
        wide += c < 128 ? c : roman[c - 128].value;
    return fs::u8path(component(wide));
}

struct DirectDevice {
    struct Range {
        uint64_t offset, size;
        romaw_image_set::Item* item;
        bool resource;
    };

    uint64_t size = 0;
    std::map<uint64_t, std::array<unsigned char, 512>> metadata;
    std::map<uint64_t, Range> ranges;
    std::map<uint32_t, romaw_image_set::Item*> forks;

    static int io(void* opaque, uint64_t offset, void* buffer, size_t count, int writing)
    {
        try {
            auto& device = *static_cast<DirectDevice*>(opaque);
            check(offset <= device.size && count <= device.size - offset, "Image block outside volume");
            auto* bytes = static_cast<unsigned char*>(buffer);
            if (!writing)
                memset(buffer, 0, count);
            for (size_t at = 0; at < count;) {
                uint64_t block = (offset + at) / 512;
                unsigned in_block = (unsigned)((offset + at) % 512);
                size_t n = std::min<size_t>(512 - in_block, count - at);
                if (writing) {
                    check(device.metadata.size() < 262144, "Virtual HFS catalog exceeds memory limit");
                    memcpy(device.metadata[block].data() + in_block, bytes + at, n);
                }
                else if (auto record = device.metadata.find(block); record != device.metadata.end())
                    memcpy(bytes + at, record->second.data() + in_block, n);
                else {
                    auto range = device.ranges.upper_bound(offset + at);
                    if (range != device.ranges.begin()) {
                        --range;
                        uint64_t relative = offset + at - range->first;
                        if (relative < range->second.size) {
                            auto& item = range->second;
                            size_t take = (size_t)std::min<uint64_t>(count - at, item.size - relative);
                            direct_range(*item.item, item.resource, item.offset + relative, bytes + at, take);
                            n = take;
                        }
                    }
                }
                at += n;
            }
            return 1;
        }
        catch (...) {
            return 0;
        }
    }
};

#include "projected_catalog.h"

extern "C" int romaw_images_guest_name_utf8(romaw_image_set* set, const char* path, char name[32])
{
    try {
        check(set && path && name, "Invalid image name");
        std::lock_guard<std::mutex> lock(set->mutex);
        auto found = set->guest_paths.find(catalog_path(fs::u8path(path)));
        check(found != set->guest_paths.end(), "Missing projected image name");
        auto result = hfs_name(found->second.filename()).substr(1);
        check(!result.empty() && result.size() <= 31, "Invalid projected image name");
        memcpy(name, result.c_str(), result.size() + 1);
        return 1;
    }
    catch (...) {
        return 0;
    }
}

extern "C" int romaw_images_guest_path_utf8(romaw_image_set* set, unsigned volume, const char* guest, char* path,
                                            size_t capacity, char* error, size_t error_capacity)
{
    try {
        check(set && guest && guest[0] == ':' && path && capacity, "Invalid image path");
        check(romaw_images_device(set, volume, error, error_capacity), "Cannot project image directory");
        std::lock_guard<std::mutex> lock(set->mutex);
        fs::path wanted = ".";
        std::string input = guest;
        for (size_t at = 1; at < input.size();) {
            size_t end = input.find(':', at);
            if (end == std::string::npos)
                end = input.size();
            std::wstring name;
            for (size_t i = at; i < end; ++i) {
                unsigned char c = input[i];
                name += c < 128 ? c : roman[c - 128].value;
            }
            wanted /= fs::u8path(component(name));
            at = end + 1;
        }
        wanted = wanted.lexically_normal();
        auto root = catalog_path(fs::u8path(set->volumes[volume].path));
        for (const auto& entry : set->guest_paths) {
            auto relative = entry.first.lexically_relative(root);
            if (relative.empty() || *relative.begin() == ".." || entry.second != wanted)
                continue;
            auto result = entry.first.u8string();
            check(result.size() < capacity, "Image path is too long");
            memcpy(path, result.c_str(), result.size() + 1);
            return 1;
        }
        throw std::runtime_error("The selected image item is no longer available");
    }
    catch (const std::exception& e) {
        if (error && error_capacity)
            snprintf(error, error_capacity, "%s", e.what());
        return 0;
    }
}

extern "C" int romaw_images_device(romaw_image_set* set, unsigned volume, char* error, size_t capacity)
{
    std::string context = "format";
    try {
        check(set && set->direct && volume < set->volumes.size(), "Missing image volume");
        std::lock_guard<std::mutex> lock(set->mutex);
        if (set->devices.count(volume))
            return 1;
        std::lock_guard<std::mutex> library_lock(hfs_library_mutex);
        auto root = catalog_path(fs::u8path(set->volumes[volume].path));
        std::vector<std::pair<fs::path, romaw_image_set::Item*>> items;
        for (auto& item : set->items) {
            auto relative = item.first.lexically_relative(root);
            if (!relative.empty() && *relative.begin() != "..")
                items.emplace_back(relative, &item.second);
        }
        auto device = std::make_shared<DirectDevice>();
        uint64_t metadata = 8 * 1024 * 1024 + items.size() * 1024;
        device->size = metadata;
        for (unsigned pass = 0; pass < 64; ++pass) {
            if (device->size > 0x7fff0000u) {
                /* Only catalog blocks need classic disk addresses. File reads
                   use CNID/fork offsets directly, independent of total size. */
                device->size = (metadata + 511) / 512 * 512;
                break;
            }
            uint64_t block = (1 + ((device->size / 512 - 6) >> 16)) * 512;
            uint64_t needed = metadata;
            for (auto& item : items)
                for (uint64_t bytes : {item.second->info.data_size, item.second->info.resource_size})
                    needed += ((bytes + block - 1) / block) * block;
            if (needed <= device->size)
                break;
            check(pass != 63, "Cannot size virtual HFS volume");
            device->size = ((needed + 1024 * 1024 + 511) / 512) * 512;
        }
        uint64_t content_bytes = 0;
        for (auto& item : items)
            content_bytes += item.second->info.data_size + item.second->info.resource_size;
        bool external = content_bytes > device->size - metadata;
        check(device->size <= 0x7fff0000u, "Image catalog exceeds classic volume size limit");
        std::string name = device_name(set->volumes[volume], volume);
        if (external) {
            projected_catalog(*set, *device, root, items, name);
            set->devices.emplace(volume, std::move(device));
            return 1;
        }
        romaw_hfs_ram(device->size, DirectDevice::io, device.get());
        hfs_ok(hfs_format("ROMAW virtual image", 0, 0, name.c_str(), 0, nullptr) == 0);
        hfsvol* mounted = hfs_mount("ROMAW virtual image", 0, HFS_MODE_RDWR);
        hfs_ok(mounted != nullptr);
        std::unique_ptr<hfsvol, decltype(&hfs_umount)> owner(mounted, hfs_umount);
        std::map<fs::path, fs::path> aliases{{".", "."}};
        std::map<fs::path, std::set<std::string>> siblings;
        unsigned sequence = 0;
        for (auto& entry : items) {
            fs::path alias = ".";
            if (entry.first != ".") {
                auto parent = entry.first.parent_path();
                if (parent.empty())
                    parent = ".";
                check(aliases.count(parent), "Missing projected image parent");
                alias = (aliases.at(parent) / guest_component(entry.first.filename(), siblings[parent], ++sequence))
                            .lexically_normal();
            }
            aliases.emplace(entry.first, alias);
            set->guest_paths[(root / entry.first).lexically_normal()] = alias;
            auto target = hfs_name(alias);
            auto& item = *entry.second;
            context = target + " (directory)";
            if (item.info.directory) {
                if (entry.first != ".")
                    hfs_ok(hfs_mkdir(mounted, target.c_str()) == 0);
            }
            else {
                char type[5] = {}, creator[5] = {};
                memcpy(type, item.info.finder_info, 4);
                memcpy(creator, item.info.finder_info + 4, 4);
                context = target + " (create)";
                hfsfile* file = hfs_create(mounted, target.c_str(), type, creator);
                hfs_ok(file != nullptr);
                std::unique_ptr<hfsfile, decltype(&hfs_close)> file_owner(file, hfs_close);
                for (unsigned resource = 0; resource < 2; ++resource) {
                    hfs_ok(hfs_setfork(file, resource) == 0);
                    uint64_t length = resource ? item.info.resource_size : item.info.data_size;
                    check(length <= 0x7fffffff, "Image fork exceeds classic File Manager size limit");

                    struct Map {
                        DirectDevice* device;
                        romaw_image_set::Item* item;
                        bool resource;
                    } map{device.get(), &item, resource != 0};

                    hfs_ok(romaw_hfs_allocate(
                               file, (unsigned long)length,
                               [](void* opaque, uint64_t physical, uint64_t logical, size_t size) {
                                   auto& map = *static_cast<Map*>(opaque);
                                   try {
                                       return map.device->ranges
                                                      .emplace(physical, DirectDevice::Range{logical, size, map.item,
                                                                                             map.resource})
                                                      .second
                                                  ? 1
                                                  : 0;
                                   }
                                   catch (...) {
                                       return 0;
                                   }
                               },
                               &map) == 0);
                }
                hfs_ok(hfs_close(file_owner.release()) == 0);
            }
            context = target + " (Finder info)";
            hfs_ok(romaw_hfs_finder_info(mounted, target.c_str(), item.info.finder_info) == 0);
        }
        hfs_ok(hfs_umount(owner.release()) == 0);
        set->devices.emplace(volume, std::move(device));
        return 1;
    }
    catch (const std::exception& e) {
        if (error && capacity)
            std::snprintf(error, capacity, "%s: %s", context.c_str(), e.what());
        return 0;
    }
    catch (...) {
        return 0;
    }
}

extern "C" int romaw_images_device_read(romaw_image_set* set, unsigned volume, uint64_t offset, void* bytes,
                                        size_t size)
{
    if (!set || (!bytes && size))
        return 0;
    std::lock_guard<std::mutex> lock(set->mutex);
    auto device = set->devices.find(volume);
    return device != set->devices.end() && DirectDevice::io(device->second.get(), offset, bytes, size, 0);
}

/* 0 means this CNID uses ordinary disk extents; -1 is a bounded read failure. */
extern "C" int romaw_images_device_fork_read(romaw_image_set* set, unsigned volume, uint32_t id, int resource,
                                             uint64_t offset, void* bytes, size_t size)
{
    if (!set || (!bytes && size))
        return -1;
    std::lock_guard<std::mutex> lock(set->mutex);
    auto device = set->devices.find(volume);
    if (device == set->devices.end())
        return 0;
    auto file = device->second->forks.find(id);
    if (file == device->second->forks.end())
        return 0;
    try {
        auto length = resource ? file->second->info.resource_size : file->second->info.data_size;
        check(offset <= length && size <= length - offset, "External image fork read outside bounds");
        if (size)
            direct_range(*file->second, resource != 0, offset, bytes, size);
        return 1;
    }
    catch (...) {
        return -1;
    }
}
