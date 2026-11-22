/*+===================================================================
  File:      hfs_io.cpp

  Summary:   Implements hfs io support for disk images package support.

  Classes:   Declared: HfsMedium; Used: HfsMedium

  Functions: romaw_hfs_medium, romaw_hfs_ram, os_close, os_open, os_read, os_same, os_seek, os_write

  Origin:    Existing repository source in packages/disk-images/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Bounded, file-only libhfs medium adapter. No physical devices are opened. */
#include <cstdio>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <memory>
#include "romaw_images.h"
#include "hfs_medium.h"
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#endif
extern "C" {
#include "../third_party/hfsutils/hfs.h"
}

struct HfsMedium {
    FILE* file;
    uint64_t base, size, position;
    bool dirty;
    romaw_hfs_io io;
    void* opaque;
};

static thread_local const char* source_path;
static thread_local uint64_t source_base, source_size;
static thread_local romaw_hfs_io source_io;
static thread_local void* source_opaque;

extern "C" void romaw_hfs_ram(uint64_t size, romaw_hfs_io io, void* opaque)
{
    source_path = nullptr;
    source_base = 0;
    source_size = size;
    source_io = io;
    source_opaque = opaque;
}

extern "C" void romaw_hfs_medium(const char* path, uint64_t base, uint64_t size)
{
    source_path = path;
    source_base = base;
    source_size = size;
    source_io = nullptr;
    source_opaque = nullptr;
}

extern "C" int os_open(void** private_data, const char*, int mode)
{
    try {
        if (source_io) {
            auto* medium = new (std::nothrow) HfsMedium{nullptr, 0, source_size, 0, false, source_io, source_opaque};
            if (!medium) {
                errno = ENOMEM;
                return -1;
            }
            *private_data = medium;
            return 0;
        }
        if (!source_path) {
            errno = EINVAL;
            return -1;
        }
        if (mode && (std::filesystem::status(std::filesystem::u8path(source_path)).permissions() &
                     (std::filesystem::perms::owner_write | std::filesystem::perms::group_write |
                      std::filesystem::perms::others_write)) == std::filesystem::perms::none) {
            errno = EACCES;
            return -1;
        }
        FILE* file = nullptr;
#ifdef _WIN32
        auto path = std::filesystem::u8path(source_path);
        HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | (mode ? GENERIC_WRITE : 0), FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            errno = EACCES;
            return -1;
        }
        int fd = _open_osfhandle((intptr_t)handle, _O_BINARY | (mode ? _O_RDWR : _O_RDONLY));
        if (fd < 0) {
            CloseHandle(handle);
            return -1;
        }
        file = _fdopen(fd, mode ? "r+b" : "rb");
        if (!file) {
            _close(fd);
            return -1;
        }
#else
        file = fopen(source_path, mode ? "r+b" : "rb");
        if (file && flock(fileno(file), (mode ? LOCK_EX : LOCK_SH) | LOCK_NB)) {
            fclose(file);
            file = nullptr;
            errno = EACCES;
        }
#endif
        if (!file)
            return -1;
        auto* medium = new (std::nothrow) HfsMedium{file, source_base, source_size, 0, false, nullptr, nullptr};
        if (!medium) {
            fclose(file);
            errno = ENOMEM;
            return -1;
        }
        *private_data = medium;
        return 0;
    }
    catch (...) {
        errno = ENOMEM;
        return -1;
    }
}

extern "C" int os_close(void** private_data)
{
    auto* medium = static_cast<HfsMedium*>(*private_data);
    *private_data = nullptr;
    if (!medium)
        return 0;
    int result = 0;
    if (medium->file && medium->dirty && medium->base == 84 && !romaw_images_dc42_sync(medium->file))
        result = -1;
    if (medium->file && fclose(medium->file))
        result = -1;
    delete medium;
    return result;
}

extern "C" int os_same(void**, const char*)
{
    return 0;
}

extern "C" unsigned long os_seek(void** private_data, unsigned long block)
{
    auto* medium = static_cast<HfsMedium*>(*private_data);
    uint64_t offset = block == (unsigned long)-1 ? medium->size : uint64_t(block) * 512;
    if (offset > medium->size) {
        errno = EINVAL;
        return (unsigned long)-1;
    }
    if (medium->io) {
        medium->position = offset;
        return (unsigned long)(offset / 512);
    }
#ifdef _WIN32
    int result = _fseeki64(medium->file, medium->base + offset, SEEK_SET);
#else
    int result = fseeko(medium->file, medium->base + offset, SEEK_SET);
#endif
    if (result)
        return (unsigned long)-1;
    medium->position = offset;
    return (unsigned long)(offset / 512);
}

extern "C" unsigned long os_read(void** private_data, void* buffer, unsigned long blocks)
{
    auto* medium = static_cast<HfsMedium*>(*private_data);
    if (blocks > (medium->size - medium->position) / 512) {
        errno = EIO;
        return (unsigned long)-1;
    }
    if (medium->io) {
        if (!medium->io(medium->opaque, medium->position, buffer, size_t(blocks) * 512, 0)) {
            errno = EIO;
            return (unsigned long)-1;
        }
        medium->position += uint64_t(blocks) * 512;
        return blocks;
    }
    size_t done = fread(buffer, 512, blocks, medium->file);
    medium->position += done * 512;
    return ferror(medium->file) ? (unsigned long)-1 : (unsigned long)done;
}

extern "C" unsigned long os_write(void** private_data, const void* buffer, unsigned long blocks)
{
    auto* medium = static_cast<HfsMedium*>(*private_data);
    if (blocks > (medium->size - medium->position) / 512) {
        errno = EIO;
        return (unsigned long)-1;
    }
    if (medium->io) {
        if (!medium->io(medium->opaque, medium->position, const_cast<void*>(buffer), size_t(blocks) * 512, 1)) {
            errno = EIO;
            return (unsigned long)-1;
        }
        medium->position += uint64_t(blocks) * 512;
        return blocks;
    }
    size_t done = fwrite(buffer, 512, blocks, medium->file);
    medium->dirty |= done != 0;
    medium->position += done * 512;
    return ferror(medium->file) ? (unsigned long)-1 : (unsigned long)done;
}
