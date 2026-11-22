/*+===================================================================
  File:      hfs_write.h

  Summary:   Declares hfs write support for disk images package support.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in packages/disk-images/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Original libhfs integration, included after the image reader implementation. */
extern "C" {
#include "../third_party/hfsutils/hfs.h"
void romaw_hfs_medium(const char*, uint64_t, uint64_t);
int romaw_hfs_finder_info(hfsvol*, const char*, const unsigned char[32]);
}
static std::mutex hfs_library_mutex;

static std::string hfs_name(const fs::path& relative)
{
    std::string result = ":";
    for (const auto& part : relative) {
        auto text = part.u8string();
        if (text == ".")
            continue;
        check(!text.empty() && text != "..", "Invalid image pathname");
        if (std::any_of(text.begin(), text.end(), [](unsigned char c) { return c >= 128; })) {
            std::wstring_convert<std::codecvt_utf8<wchar_t>> conversion;
            text = component(conversion.from_bytes(text));
            check(text.find("%u") == std::string::npos, "This name cannot be represented in classic HFS");
        }
        if (result.size() > 1)
            result += ':';
        std::string decoded;
        for (size_t i = 0; i < text.size(); ++i) {
            unsigned char c = text[i];
            if (c == '%' && i + 2 < text.size() && isxdigit((unsigned char)text[i + 1]) &&
                isxdigit((unsigned char)text[i + 2])) {
                c = (unsigned char)std::stoul(text.substr(i + 1, 2), nullptr, 16);
                i += 2;
            }
            check(c && c != ':', "Invalid Macintosh filename");
            decoded += c;
        }
        check(decoded.size() <= 31, "HFS filenames are limited to 31 bytes");
        result += decoded;
    }
    return result;
}

static void hfs_ok(bool result)
{
    check(result, hfs_error ? hfs_error : "Cannot update HFS image");
}

struct HostForks {
    unsigned char info[32] = {};
    fs::path resource;
    uint64_t offset = 0, size = 0;
};

static HostForks host_forks(const fs::path& source)
{
    HostForks forks;
    auto parent = source.parent_path(), name = source.filename();
    auto sidecar = parent / fs::u8path("._" + name.u8string());
    if (!fs::exists(sidecar))
        sidecar = parent / ".AppleDouble" / name;
    if (!fs::exists(sidecar))
        sidecar = parent / fs::u8path("%" + name.u8string());
    if (fs::exists(sidecar)) {
        std::ifstream file(io_path(sidecar), std::ios::binary);
        uint64_t length = fs::file_size(io_path(sidecar));
        auto number = [&](unsigned size) {
            uint32_t n = 0;
            for (unsigned i = 0; i < size; ++i) {
                int c = file.get();
                check(c >= 0, "Truncated AppleDouble");
                n = n * 256 + c;
            }
            return n;
        };
        check(number(4) == 0x00051607 && number(4) == 0x20000, "Unsupported AppleDouble");
        file.seekg(24);
        unsigned count = number(2);
        check(count <= 4096 && 26 + uint64_t(count) * 12 <= length, "Invalid AppleDouble entries");
        bool info_seen = false, resource_seen = false;
        for (unsigned i = 0; i < count; ++i) {
            file.seekg(26 + i * 12);
            unsigned id = number(4), offset = number(4), size = number(4);
            check(offset <= length && size <= length - offset, "Truncated AppleDouble entry");
            if (id == 9) {
                check(!info_seen && size >= 32, "Invalid FinderInfo entry");
                info_seen = true;
                file.seekg(offset);
                file.read((char*)forks.info, 32);
                check(bool(file), "Cannot read FinderInfo");
            }
            if (id == 2) {
                check(!resource_seen, "Duplicate resource fork");
                resource_seen = true;
                forks.resource = sidecar;
                forks.offset = offset;
                forks.size = size;
            }
        }
        return forks;
    }
    auto finf = parent / ".finf" / name;
    if (fs::exists(finf)) {
        std::ifstream in(io_path(finf), std::ios::binary);
        in.read((char*)forks.info, 32);
        check(bool(in), "Truncated FinderInfo");
    }
    auto rsrc = parent / ".rsrc" / name;
    if (fs::exists(rsrc)) {
        forks.resource = rsrc;
        forks.size = fs::file_size(io_path(rsrc));
    }
    return forks;
}

extern "C" int romaw_images_host_info_utf8(const char* path, romaw_image_item* item, char* resource_path,
                                           size_t resource_capacity, uint64_t* resource_offset, char* error,
                                           size_t capacity)
{
    try {
        check(path && item && resource_path && resource_capacity && resource_offset, "Missing host copy source");
        auto source = fs::u8path(path);
        auto status = fs::symlink_status(source);
        check(!fs::is_symlink(status) && (fs::is_directory(status) || fs::is_regular_file(status)),
              "Choose a regular source item");
        auto forks = host_forks(source);
        memset(item, 0, sizeof *item);
        item->directory = fs::is_directory(status);
        item->data_size = item->directory ? 0 : fs::file_size(source);
        item->resource_size = item->directory ? 0 : forks.size;
        memcpy(item->finder_info, forks.info, 32);
        auto resource = forks.resource.u8string();
        check(resource.size() < resource_capacity, "Resource path is too long");
        memcpy(resource_path, resource.c_str(), resource.size() + 1);
        *resource_offset = forks.offset;
        return 1;
    }
    catch (const std::exception& e) {
        if (error && capacity)
            snprintf(error, capacity, "%s", e.what());
        return 0;
    }
}

static void import_hfs(hfsvol* volume, const std::string& target, const fs::path& source, unsigned depth,
                       unsigned& count)
{
    check(++count <= 100000 && depth <= 256, "Image import exceeds catalog limits");
    check(!fs::is_symlink(source), "Symbolic links cannot be copied into HFS images");
    auto forks = host_forks(source);
    if (fs::is_directory(source)) {
        hfs_ok(hfs_mkdir(volume, target.c_str()) == 0);
        for (const auto& entry : fs::directory_iterator(source)) {
            auto name = entry.path().filename().u8string();
            if (name == ".AppleDouble" || name == ".finf" || name == ".rsrc" || name == ".DS_Store" ||
                name.compare(0, 2, "._") == 0)
                continue;
            import_hfs(volume, target + hfs_name(entry.path().filename()), entry.path(), depth + 1, count);
        }
    }
    else {
        check(fs::is_regular_file(source), "Choose a regular source file");
        char type[5] = {}, creator[5] = {};
        memcpy(type, forks.info, 4);
        memcpy(creator, forks.info + 4, 4);
        hfsfile* file = hfs_create(volume, target.c_str(), type, creator);
        hfs_ok(file != nullptr);
        std::unique_ptr<hfsfile, decltype(&hfs_close)> owner(file, hfs_close);
        for (unsigned resource = 0; resource < 2; ++resource) {
            uint64_t length = resource ? forks.size : fs::file_size(io_path(source));
            check(length <= 0x7fffffff, "HFS fork exceeds supported size");
            if (!length)
                continue;
            hfs_ok(hfs_setfork(file, resource) == 0);
            std::ifstream in(io_path(resource ? forks.resource : source), std::ios::binary);
            in.seekg(resource ? forks.offset : 0);
            for (uint64_t at = 0; at < length;) {
                char buffer[65536];
                size_t n = (size_t)std::min<uint64_t>(sizeof buffer, length - at);
                in.read(buffer, n);
                check(bool(in), "Cannot read source fork");
                hfs_ok(hfs_write(file, buffer, (unsigned long)n) == n);
                at += n;
            }
        }
        hfs_ok(hfs_close(owner.release()) == 0);
    }
    hfs_ok(romaw_hfs_finder_info(volume, target.c_str(), forks.info) == 0);
}

extern "C" int romaw_images_mutate_utf8(romaw_image_set* set, const char* path, unsigned operation, const char* value,
                                        char* error, size_t capacity)
{
    bool attempted = false;
    try {
        check(set && set->direct && path && operation <= 3, "Invalid image operation");
        std::lock_guard<std::mutex> lock(set->mutex);
        std::lock_guard<std::mutex> writer_lock(hfs_library_mutex);
        auto requested = catalog_path(fs::u8path(path));
        const Volume* volume = nullptr;
        fs::path relative;
        for (const auto& candidate : set->volumes) {
            auto part = requested.lexically_relative(catalog_path(fs::u8path(candidate.path.c_str())));
            if (!part.empty() && *part.begin() != "..") {
                volume = &candidate;
                relative = part;
                break;
            }
        }
        check(volume && relative != ".", "Image volume roots cannot be changed");
        check(volume->classic && volume->writable_container, "This image format does not support in-place writes");
        auto target = hfs_name(relative);
        std::string source = set->source.u8string();
        romaw_hfs_medium(source.c_str(), volume->offset, volume->length);
        hfsvol* mounted = hfs_mount(source.c_str(), 0, HFS_MODE_RDWR);
        hfs_ok(mounted != nullptr);
        attempted = true;
        std::unique_ptr<hfsvol, decltype(&hfs_umount)> owner(mounted, hfs_umount);
        if (operation == 0)
            hfs_ok(hfs_mkdir(mounted, target.c_str()) == 0);
        else if (operation == 1) {
            check(value && *value, "Missing image rename destination");
            auto dest = catalog_path(fs::u8path(value)).lexically_relative(catalog_path(fs::u8path(volume->path)));
            check(!dest.empty() && *dest.begin() != "..", "Cannot rename across image volumes");
            hfs_ok(hfs_rename(mounted, target.c_str(), hfs_name(dest).c_str()) == 0);
        }
        else if (operation == 2) {
            check(relative.begin()->u8string() != "Trash", "Cannot move the image Trash into itself");
            hfsdirent trash;
            if (hfs_stat(mounted, ":Trash", &trash)) {
                hfs_ok(hfs_mkdir(mounted, ":Trash") == 0);
                unsigned char info[32] = {};
                info[8] = 0x40;
                hfs_ok(romaw_hfs_finder_info(mounted, ":Trash", info) == 0);
            }
            std::string name = hfs_name(relative.filename()).substr(1), dest;
            for (unsigned i = 0; i < 100000; ++i) {
                auto suffix = i ? " " + std::to_string(i) : "";
                dest = ":Trash:" + name.substr(0, 31 - suffix.size()) + suffix;
                hfsdirent existing;
                if (hfs_stat(mounted, dest.c_str(), &existing))
                    break;
            }
            hfs_ok(hfs_rename(mounted, target.c_str(), dest.c_str()) == 0);
        }
        else {
            check(value && *value, "Missing image import source");
            unsigned count = 0;
            import_hfs(mounted, target, fs::u8path(value), 0, count);
        }
        hfs_ok(hfs_flush(mounted) == 0);
        hfs_ok(hfs_umount(owner.release()) == 0);
        romaw_image_set updated;
        updated.direct = updated.readonly = true;
        updated.root = set->root;
        updated.source = set->source;
        CMyComPtr<IInStream> input = new Input(set->source);
        check(prepare_stream(updated, input, 0), "Cannot refresh changed image catalog");
        set->items.swap(updated.items);
        set->volumes.swap(updated.volumes);
        set->containers.swap(updated.containers);
        set->bytes = updated.bytes;
        set->devices.clear();
        return 1;
    }
    catch (const std::exception& e) {
        std::string message = e.what();
        if (attempted)
            try {
                std::lock_guard<std::mutex> lock(set->mutex);
                romaw_image_set updated;
                updated.direct = updated.readonly = true;
                updated.root = set->root;
                updated.source = set->source;
                CMyComPtr<IInStream> input = new Input(set->source);
                check(prepare_stream(updated, input, 0), "Cannot refresh changed image catalog");
                set->items.swap(updated.items);
                set->volumes.swap(updated.volumes);
                set->containers.swap(updated.containers);
                set->bytes = updated.bytes;
                set->devices.clear();
            }
            catch (...) {
                message += "; remount the image to refresh its catalog";
            }
        if (error && capacity)
            std::snprintf(error, capacity, "%s", message.c_str());
        return 0;
    }
    catch (...) {
        return 0;
    }
}
