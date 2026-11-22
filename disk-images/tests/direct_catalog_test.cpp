/*+===================================================================
  File:      direct_catalog_test.cpp

  Summary:   Validate direct catalogs, classic aliases and source-backed fork reads.

  Classes:   Catalog.

  Functions: main.

  Origin:    Executor direct-image regression fixture; inputs remain local assets.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "romaw_images.h"
#include "../src/hfs_medium.h"
extern "C" {
#include "../third_party/hfsutils/hfs.h"
}
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
namespace fs = std::filesystem;

struct Catalog {
    std::vector<std::pair<std::string, romaw_image_item>> entries;
    std::string parent;
};

static std::string classic_path(const char* target)
{
    const char* p = strchr(target, '/');
    assert(p);
    std::string result = ":";
    for (++p; *p; ++p) {
        if (*p == '/')
            result += ':';
        else if (*p == '%' && p[1] && p[2] && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
            result += (char)std::stoul(std::string(p + 1, 2), nullptr, 16);
            p += 2;
        }
        else
            result += *p;
    }
    return result;
}

int main(int argc, char** argv)
{
    assert(argc >= 2);
    bool allow_unavailable = !strcmp(argv[1], "--allow-unavailable");
    for (int argument = allow_unavailable ? 2 : 1; argument < argc; ++argument) {
        char error[2048] = {};
        auto before = fs::last_write_time(fs::u8path(argv[argument]));
        auto size = fs::file_size(fs::u8path(argv[argument]));
        auto* set = romaw_images_open_direct_utf8(argv[argument], error, sizeof error);
        if (!set) {
            fprintf(stderr, "%s: %s\n", argv[argument], error);
            return 1;
        }
        const char* warning = romaw_images_warning(set);
        if (allow_unavailable)
            assert(*warning && strstr(warning, "incomplete"));
        else
            assert(!*warning);
        for (unsigned volume = 0; volume < romaw_images_count(set); ++volume) {
            if (!romaw_images_device(set, volume, error, sizeof error)) {
                fprintf(stderr, "%s: %s\n", argv[argument], error);
                return 1;
            }

            struct Device {
                romaw_image_set* set;
                unsigned volume;
            } device{set, volume};

            romaw_hfs_ram(
                INT32_MAX,
                [](void* opaque, uint64_t offset, void* bytes, size_t size, int writing) -> int {
                    auto& device = *static_cast<Device*>(opaque);
                    return !writing && romaw_images_device_read(device.set, device.volume, offset, bytes, size);
                },
                &device);
            auto* mounted = hfs_mount("Direct catalog test", 0, HFS_MODE_RDONLY);
            assert(mounted);
            Catalog catalog;
            std::string root = romaw_images_path(set, volume);
            catalog.entries.emplace_back(root, romaw_image_item{1, 0, 0, {}});
            unsigned sampled = 0, files = 0, unavailable = 0;
            uint64_t total = 0;
            for (size_t i = 0; i < catalog.entries.size(); ++i) {
                auto path = catalog.entries[i].first;
                auto item = catalog.entries[i].second;
                if (item.directory) {
                    catalog.parent = path;
                    assert(romaw_images_list_utf8(
                        set, path.c_str(),
                        [](const char* name, const romaw_image_item* item, void* opaque) {
                            auto& catalog = *static_cast<Catalog*>(opaque);
                            catalog.entries.emplace_back(catalog.parent + "/" + name, *item);
                            return 1;
                        },
                        &catalog));
                }
                else {
                    ++files;
                    total += item.data_size + item.resource_size;
                }
                if (path == root)
                    continue;
                char source[32768], target[32768];
                assert(romaw_images_target_utf8(set, path.c_str(), source, sizeof source, target, sizeof target));
                auto guest = classic_path(target);
                hfsdirent entry;
                if (hfs_stat(mounted, guest.c_str(), &entry)) {
                    fprintf(stderr, "Missing projected entry: %s -> %s (%s)\n", path.c_str(), guest.c_str(), hfs_error);
                    return 1;
                }
                assert(bool(entry.flags & HFS_ISDIR) == bool(item.directory));
                if (item.directory)
                    continue;
                assert(entry.u.file.dsize == item.data_size && entry.u.file.rsize == item.resource_size);
                /* Cover both early and late catalog IDs without reading all
                   source content merely to validate a metadata projection. */
                if (sampled >= 12 && files % 997)
                    continue;
                for (int resource = 0; resource < 2; ++resource) {
                    uint64_t length = resource ? item.resource_size : item.data_size;
                    if (!length || length > 1024 * 1024)
                        continue;
                    unsigned char* expected = nullptr;
                    size_t expected_size = 0;
                    if (!romaw_images_fork_utf8(set, path.c_str(), resource, &expected, &expected_size, error,
                                                sizeof error)) {
                        if (!allow_unavailable) {
                            fprintf(stderr, "Cannot read source fork: %s (%d): %s\n", path.c_str(), resource, error);
                            return 1;
                        }
                        unsigned char byte;
                        assert(romaw_images_device_fork_read(set, volume, (uint32_t)entry.cnid, resource, length - 1,
                                                             &byte, 1) == -1);
                        ++unavailable;
                        continue;
                    }
                    if (getenv("IMAGE_TEST_SAMPLES") && sampled < 12) {
                        printf("SAMPLE|%s|%d|%llu|", guest.c_str(), resource, (unsigned long long)length);
                        for (size_t n = 0; n < std::min<size_t>(64, expected_size); ++n)
                            printf("%02x", expected[n]);
                        printf("\n");
                    }
                    assert(expected_size == length);
                    std::vector<unsigned char> bytes((size_t)length);
                    int external = romaw_images_device_fork_read(set, volume, (uint32_t)entry.cnid, resource, 0,
                                                                 bytes.data(), bytes.size());
                    if (!external) {
                        auto* file = hfs_open(mounted, guest.c_str());
                        assert(file && !hfs_setfork(file, resource));
                        assert(hfs_read(file, bytes.data(), (unsigned long)bytes.size()) == bytes.size());
                        assert(!hfs_close(file));
                    }
                    else {
                        assert(external == 1);
                        assert(romaw_images_device_fork_read(set, volume, (uint32_t)entry.cnid, resource, length,
                                                             bytes.data(), 1) == -1);
                    }
                    assert(!memcmp(bytes.data(), expected, bytes.size()));
                    free(expected);
                    ++sampled;
                }
            }
            assert(!hfs_umount(mounted));
            assert(!fs::exists(fs::u8path(root)));
            printf("PASS|%s|%s|%zu entries|%llu fork bytes|%u source comparisons|%u unavailable samples\n",
                   argv[argument], romaw_images_name(set, volume), catalog.entries.size(), (unsigned long long)total,
                   sampled, unavailable);
            fflush(stdout);
        }
        romaw_images_close(set, 1);
        assert(fs::last_write_time(fs::u8path(argv[argument])) == before);
        assert(fs::file_size(fs::u8path(argv[argument])) == size);
    }
}
