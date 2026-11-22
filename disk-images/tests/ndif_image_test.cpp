/*+===================================================================
  File:      ndif_image_test.cpp

  Summary:   Implements ndif image test support for disk images package support.

  Classes:   Declared: Context; Used: Context, romaw_image_item, romaw_image_set, std::map, std::string,
             std::vector

  Functions: main

  Origin:    Existing repository source in packages/disk-images/tests; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include "romaw_images.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
namespace fs = std::filesystem;
using Items = std::map<std::string, romaw_image_item>;

static Items collect(romaw_image_set* set, unsigned volume)
{
    Items result;
    std::string root = romaw_images_path(set, volume);
    std::vector<std::string> folders{root};
    for (size_t i = 0; i < folders.size(); ++i) {
        struct Context {
            Items* items;
            std::vector<std::string>* folders;
            const std::string* root;
            std::string parent;
        } context{&result, &folders, &root, folders[i]};

        assert(romaw_images_list_utf8(
            set, folders[i].c_str(),
            [](const char* path, const romaw_image_item* item, void* opaque) {
                auto& c = *static_cast<Context*>(opaque);
                std::string p = c.parent + "/" + path;
                c.items->emplace(p.substr(c.root->size()), *item);
                if (item->directory)
                    c.folders->push_back(p);
                return 1;
            },
            &context));
    }
    return result;
}

int main(int argc, char** argv)
{
    assert(argc == 3 || argc == 4);
    if (argc == 4)
        assert(romaw_images_ndif_writable(argv[1]) == atoi(argv[3]));
    char error[1024];
    auto* compressed = romaw_images_open_direct_utf8(argv[1], error, sizeof error);
    if (!compressed) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    auto* raw = romaw_images_open_direct_utf8(argv[2], error, sizeof error);
    assert(raw);
    assert(romaw_images_count(compressed) == romaw_images_count(raw));
    size_t files = 0, bytes = 0;
    for (unsigned v = 0; v < romaw_images_count(raw); ++v) {
        assert(!strcmp(romaw_images_name(compressed, v), romaw_images_name(raw, v)));
        auto a = collect(compressed, v), b = collect(raw, v);
        assert(a.size() == b.size());
        for (const auto& entry : b) {
            auto found = a.find(entry.first);
            assert(found != a.end());
            const auto &x = found->second, &y = entry.second;
            assert(x.directory == y.directory && x.data_size == y.data_size && x.resource_size == y.resource_size &&
                   !memcmp(x.finder_info, y.finder_info, 32));
            if (y.directory)
                continue;
            ++files;
            for (int resource = 0; resource < 2; ++resource) {
                unsigned char *left = nullptr, *right = nullptr;
                size_t ln = 0, rn = 0;
                std::string lp = romaw_images_path(compressed, v) + entry.first,
                            rp = romaw_images_path(raw, v) + entry.first;
                assert(romaw_images_fork_utf8(compressed, lp.c_str(), resource, &left, &ln, error, sizeof error));
                assert(romaw_images_fork_utf8(raw, rp.c_str(), resource, &right, &rn, error, sizeof error));
                assert(ln == rn && (!ln || !memcmp(left, right, ln)));
                bytes += ln;
                free(left);
                free(right);
            }
        }
        assert(romaw_images_device(compressed, v, error, sizeof error));
        std::vector<unsigned char> block(1536);
        assert(romaw_images_device_read(compressed, v, 0, block.data(), block.size()));
        assert(block[1024] == 'B' && block[1025] == 'D');
    }
    printf("NDIF comparison: %zu files, %zu fork bytes identical; classic device valid\n", files, bytes);
    romaw_images_close(compressed, 1);
    romaw_images_close(raw, 1);
}
