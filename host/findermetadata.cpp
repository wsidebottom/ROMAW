/*+===================================================================
  File:      findermetadata.cpp

  Summary:   Implements findermetadata support for host operating-system support.

  Classes:   Used: FinderItemPosition, FinderWindowState, std::runtime_error, std::string, std::vector, std::wstring

  Functions: romaw_finder_bundle_icon_id, romaw_finder_metadata, finder_image_contains

  Origin:    Existing repository source in executor/src/host; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include "rsys/findermetadata.h"
#include "finder_metadata/metadata.h"
#include "finder_metadata/icons.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <iconv.h>
#endif
namespace {
uint32_t get(const unsigned char* p)
{
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

void put(unsigned char* p, uint32_t n)
{
    p[0] = n >> 24;
    p[1] = n >> 16;
    p[2] = n >> 8;
    p[3] = n;
}

std::string utf8(const char* text, bool roman)
{
#ifdef _WIN32
    unsigned page = roman ? 10000 : CP_ACP;
    int length = MultiByteToWideChar(page, 0, text, -1, nullptr, 0);
    if (!length)
        throw std::runtime_error("Invalid metadata path");
    std::wstring wide(length, 0);
    MultiByteToWideChar(page, 0, text, -1, wide.data(), length);
    length = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (!length)
        throw std::runtime_error("Invalid metadata name");
    std::string result(length, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, result.data(), length, nullptr, nullptr);
    result.pop_back();
    return result;
#else
    if (!roman)
        return text;
    iconv_t converter = iconv_open("UTF-8", "MACINTOSH");
    if (converter == (iconv_t)-1)
        throw std::runtime_error("MacRoman conversion unavailable");
    size_t remaining = strlen(text), capacity = remaining * 4 + 1;
    std::string result(capacity, 0);
    char *input = const_cast<char*>(text), *output = result.data();
    size_t available = capacity;
    size_t status = iconv(converter, &input, &remaining, &output, &available);
    iconv_close(converter);
    if (status == (size_t)-1)
        throw std::runtime_error("Invalid MacRoman name");
    result.resize(capacity - available);
    return result;
#endif
}
}

extern "C" int romaw_finder_bundle_icon_id(const unsigned char* bytes, size_t size, unsigned creator, unsigned local,
                                           int* id)
{
    return finder_bundle_icon_id(bytes, size, creator, local, id);
}

/* The guest bridge rejects writes to image volumes before reaching this library. */
extern "C" int finder_image_contains(const char*)
{
    return 0;
}

extern "C" int romaw_finder_metadata(unsigned action, const char* path, void* data, unsigned count)
{
    try {
        if (action > 4 || (action != 4 && (!path || !data)) || count > 65536)
            return -1;
        if (action == 4) {
            finder_metadata_flush();
            return 1;
        }
        auto folder = utf8(path, false);
        auto bytes = static_cast<unsigned char*>(data);
        char error[512];
        if (action < 2) {
            FinderWindowState state = {};
            int result;
            if (action == 0)
                result = finder_window_read(folder.c_str(), &state, error, sizeof error);
            else {
                state = {int32_t(get(bytes)), int32_t(get(bytes + 4)), int32_t(get(bytes + 8)),
                         int32_t(get(bytes + 12)), int32_t(get(bytes + 16))};
                if (state.view < 0 || state.view > 3)
                    return -1;
                result = finder_window_write(folder.c_str(), &state, error, sizeof error);
            }
            if (result == 1 && action == 0) {
                put(bytes, state.x);
                put(bytes + 4, state.y);
                put(bytes + 8, state.width);
                put(bytes + 12, state.height);
                put(bytes + 16, state.view);
            }
            return result;
        }
        if (action > 3)
            return -1;
        std::vector<FinderItemPosition> items(count);
        for (unsigned i = 0; i < count; ++i) {
            auto p = bytes + i * 268;
            if (!memchr(p, 0, 256))
                return -1;
            auto name = utf8(reinterpret_cast<char*>(p), true);
            if (name.size() >= sizeof items[i].name)
                return -1;
            strcpy(items[i].name, name.c_str());
            items[i].x = int32_t(get(p + 256));
            items[i].y = int32_t(get(p + 260));
            items[i].present = get(p + 264) != 0;
        }
        if (action == 3) {
            return finder_layout_write(folder.c_str(), items.data(), items.size(), error, sizeof error);
        }
        int result = finder_layout_read(folder.c_str(), items.data(), items.size(), error, sizeof error);
        if (result == 1)
            for (unsigned i = 0; i < count; ++i) {
                auto p = bytes + i * 268;
                put(p + 256, items[i].x);
                put(p + 260, items[i].y);
                put(p + 264, items[i].present);
            }
        return result;
    }
    catch (...) {
        return -1;
    }
}
