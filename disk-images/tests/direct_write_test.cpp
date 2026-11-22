/*+===================================================================
  File:      direct_write_test.cpp

  Summary:   Implements direct write test support for disk images package support.

  Classes:   Used: romaw_image_item, romaw_image_set, std::ifstream, std::ofstream, std::vector

  Functions: main

  Origin:    Existing repository source in packages/disk-images/tests; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "romaw_images.h"
#include "../src/hfs_medium.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
extern "C" {
#include "../third_party/hfsutils/hfs.h"
void romaw_hfs_medium(const char*, uint64_t, uint64_t);
}
namespace fs = std::filesystem;

static void number(std::ofstream& file, unsigned value)
{
    for (int i = 24; i >= 0; i -= 8)
        file.put((char)(value >> i));
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    auto work = fs::u8path(argv[1]);
    fs::create_directories(work);
    auto image = work / "Writable.hfv";
    {
        std::ofstream file(image, std::ios::binary);
        std::vector<char> zeros(1440 * 1024);
        file.write(zeros.data(), zeros.size());
    }
    auto source = image.u8string();
    romaw_hfs_medium(source.c_str(), 0, 1440 * 1024);
    assert(!hfs_format(source.c_str(), 0, 0, "Direct Test", 0, nullptr));
    char error[1024] = {};
    auto* set = romaw_images_open_direct_utf8(source.c_str(), error, sizeof error);
    if (!set)
        fprintf(stderr, "open: %s\n", error);
    assert(set && romaw_images_count(set) == 1);
    auto root = fs::u8path(romaw_images_path(set, 0));
    assert(!fs::exists(root));
    auto folder = (root / "Folder").u8string();
    if (!romaw_images_mutate_utf8(set, folder.c_str(), 0, nullptr, error, sizeof error)) {
        fprintf(stderr, "mkdir: %s\n", error);
        return 1;
    }
    auto input = work / "Hello";
    {
        std::ofstream file(input, std::ios::binary);
        file << "Image mount test";
    }
    unsigned char info[32];
    for (unsigned i = 0; i < 32; ++i)
        info[i] = i * 7;
    memcpy(info, "TEXTttxt", 8);
    info[8] = info[9] = 0;
    {
        std::ofstream file(work / "._Hello", std::ios::binary);
        number(file, 0x51607);
        number(file, 0x20000);
        for (unsigned i = 0; i < 16; ++i)
            file.put(0);
        file.put(0);
        file.put(2);
        number(file, 9);
        number(file, 50);
        number(file, 32);
        number(file, 2);
        number(file, 82);
        number(file, 4);
        file.write((const char*)info, 32);
        file << "RSRC";
    }
    auto target = (root / "Folder" / "Hello").u8string();
    auto host = input.u8string();
    if (!romaw_images_mutate_utf8(set, target.c_str(), 3, host.c_str(), error, sizeof error)) {
        fprintf(stderr, "import: %s\n", error);
        return 1;
    }
    romaw_image_item item;
    assert(romaw_images_stat_utf8(set, target.c_str(), &item));
    assert(item.data_size == 16 && item.resource_size == 4 && !memcmp(item.finder_info, info, 32));
    unsigned char* bytes = nullptr;
    size_t size = 0;
    assert(romaw_images_fork_utf8(set, target.c_str(), 0, &bytes, &size, error, sizeof error));
    assert(size == 16 && !memcmp(bytes, "Image mount test", 16));
    free(bytes);
    assert(romaw_images_fork_utf8(set, target.c_str(), 1, &bytes, &size, error, sizeof error));
    assert(size == 4 && !memcmp(bytes, "RSRC", 4));
    free(bytes);
    if (!romaw_images_device(set, 0, error, sizeof error)) {
        fprintf(stderr, "virtual device: %s\n", error);
        return 1;
    }
    romaw_hfs_ram(
        0x7fffffff,
        [](void* opaque, uint64_t offset, void* buffer, size_t count, int writing) -> int {
            return !writing &&
                   romaw_images_device_read(static_cast<romaw_image_set*>(opaque), 0, offset, buffer, count);
        },
        set);
    auto* virtual_volume = hfs_mount("Virtual test", 0, HFS_MODE_RDONLY);
    assert(virtual_volume);
    auto* virtual_file = hfs_open(virtual_volume, ":Folder:Hello");
    assert(virtual_file);
    char content[32] = {};
    assert(hfs_read(virtual_file, content, sizeof content) == 16);
    assert(!memcmp(content, "Image mount test", 16));
    assert(!hfs_setfork(virtual_file, 1));
    assert(hfs_read(virtual_file, content, sizeof content) == 4);
    assert(!memcmp(content, "RSRC", 4));
    assert(!hfs_close(virtual_file));
    assert(!hfs_umount(virtual_volume));
    auto renamed = (root / "Folder" / "Renamed").u8string();
    assert(romaw_images_mutate_utf8(set, target.c_str(), 1, renamed.c_str(), error, sizeof error));
    assert(!romaw_images_stat_utf8(set, target.c_str(), &item));
    assert(romaw_images_mutate_utf8(set, renamed.c_str(), 2, nullptr, error, sizeof error));
    assert(!romaw_images_stat_utf8(set, renamed.c_str(), &item));
    auto trashed = (root / "Trash" / "Renamed").u8string();
    assert(romaw_images_stat_utf8(set, trashed.c_str(), &item));
    auto copy = (work / "Copied").u8string();
    assert(romaw_images_export_utf8(set, trashed.c_str(), copy.c_str(), error, sizeof error));
    assert(fs::file_size(fs::u8path(copy)) == 16);
    assert(!fs::exists(root));
#ifdef _WIN32
    assert(SetFileAttributesW(image.c_str(), FILE_ATTRIBUTE_READONLY));
#else
    fs::permissions(image, fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read);
#endif
    auto denied = (root / "Denied").u8string();
    assert(!romaw_images_mutate_utf8(set, denied.c_str(), 0, nullptr, error, sizeof error));
#ifdef _WIN32
    assert(SetFileAttributesW(image.c_str(), FILE_ATTRIBUTE_NORMAL));
#else
    fs::permissions(image, fs::perms::owner_write, fs::perm_options::add);
#endif
    romaw_images_close(set, 1);
    assert(!fs::exists(root));
    std::ifstream raw_input(image, std::ios::binary);
    std::vector<unsigned char> raw((std::istreambuf_iterator<char>(raw_input)), {});
    auto put16 = [](unsigned char* p, unsigned value) {
        p[0] = value >> 8;
        p[1] = value;
    };
    auto put32 = [&](unsigned char* p, unsigned value) {
        put16(p, value >> 16);
        put16(p + 2, value);
    };
    for (unsigned diskcopy = 0; diskcopy < 2; ++diskcopy) {
        std::vector<unsigned char> prefix(diskcopy ? 84 : 4096);
        if (diskcopy) {
            prefix[82] = 1;
            put32(&prefix[64], (unsigned)raw.size());
        }
        else {
            put16(prefix.data(), 0x4552);
            put16(prefix.data() + 2, 512);
            put32(prefix.data() + 4, (unsigned)(prefix.size() + raw.size()) / 512);
            put16(&prefix[512], 0x504d);
            put32(&prefix[516], 1);
            put32(&prefix[520], 8);
            put32(&prefix[524], (unsigned)raw.size() / 512);
            memcpy(&prefix[560], "Apple_HFS", 10);
        }
        auto wrapped = work / (diskcopy ? "DiskCopy.img" : "Partitioned.hda");
        {
            std::ofstream file(wrapped, std::ios::binary);
            file.write((const char*)prefix.data(), prefix.size());
            file.write((const char*)raw.data(), raw.size());
        }
        auto text = wrapped.u8string();
        set = romaw_images_open_direct_utf8(text.c_str(), error, sizeof error);
        assert(set && romaw_images_count(set) == 1);
        auto child = (fs::u8path(romaw_images_path(set, 0)) / "Wrapped").u8string();
        if (!romaw_images_mutate_utf8(set, child.c_str(), 0, nullptr, error, sizeof error)) {
            fprintf(stderr, "wrapped mkdir: %s\n", error);
            return 1;
        }
        assert(romaw_images_stat_utf8(set, child.c_str(), &item));
        romaw_images_close(set, 1);
        std::ifstream file(wrapped, std::ios::binary);
        std::vector<unsigned char> result((std::istreambuf_iterator<char>(file)), {});
        assert(result.size() == prefix.size() + raw.size());
        if (!diskcopy)
            assert(std::equal(prefix.begin(), prefix.end(), result.begin()));
        else {
            uint32_t expected = 0;
            for (size_t at = 84; at < result.size(); at += 2) {
                expected += (unsigned)result[at] * 256 + result[at + 1];
                expected = expected / 2 + (expected & 1 ? 0x80000000u : 0);
            }
            uint32_t actual = 0;
            for (unsigned i = 72; i < 76; ++i)
                actual = actual * 256 + result[i];
            assert(actual == expected);
        }
    }
    auto classic = work / "ClassicWritable.hfv";
    {
        std::ofstream file(classic, std::ios::binary);
        file.write((const char*)raw.data(), raw.size());
    }
    auto classic_source = classic.u8string();
    romaw_hfs_medium(classic_source.c_str(), 0, raw.size());
    assert(!hfs_format(classic_source.c_str(), 0, 0, "Mount Test", 0, nullptr));
    auto* volume = hfs_mount(classic_source.c_str(), 0, HFS_MODE_RDWR);
    assert(volume && !hfs_mkdir(volume, ":Folder"));
    auto* file = hfs_create(volume, ":Hello", "TEXT", "ttxt");
    assert(file && hfs_write(file, "Image mount test", 16) == 16);
    assert(!hfs_close(file) && !hfs_umount(volume));
    puts("Direct HFS import, both forks, FinderInfo, rename, Trash, export and host lock passed");
}
