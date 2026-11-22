/*+===================================================================
  File:      label_menu_test.cpp

  Summary:   Render the Label painter and compare reference geometry/glyphs with Rez-configured swatch colors.

  Classes:   Used: std::array, std::filesystem::path, std::ofstream

  Functions: icon, main

  Origin:    Existing repository source in executor/tests/native-finder; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

// Render the production Label painter into test bitmaps and compare reference
// glyph/swatch pixels without automating a desktop window.

#include "finder/classic.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdint>
#include "assets.inc"
#include "label_art.inc"
namespace classic = finder::classic;

HICON
icon(const unsigned char* rgba)
{
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 32;
    info.bmiHeader.biHeight = -32;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color)
        return nullptr;
    auto* out = static_cast<unsigned char*>(bits);
    std::array<unsigned char, 128> mask{};
    for (unsigned i = 0; i < 1024; ++i) {
        out[i * 4] = rgba[i * 4 + 2] * rgba[i * 4 + 3] / 255;
        out[i * 4 + 1] = rgba[i * 4 + 1] * rgba[i * 4 + 3] / 255;
        out[i * 4 + 2] = rgba[i * 4] * rgba[i * 4 + 3] / 255;
        out[i * 4 + 3] = rgba[i * 4 + 3];
        if (!rgba[i * 4 + 3])
            mask[i / 8] |= 0x80 >> (i % 8);
    }
    HBITMAP mono = CreateBitmap(32, 32, 1, 1, mask.data());
    ICONINFO image{};
    image.fIcon = TRUE;
    image.hbmColor = color;
    image.hbmMask = mono;
    HICON result = CreateIconIndirect(&image);
    DeleteObject(color);
    DeleteObject(mono);
    return result;
}

int main()
{
    classic::initialize(font_chicago, sizeof font_chicago, font_geneva, sizeof font_geneva);
    const unsigned char* data[] = {label_1_rgba, label_2_rgba, label_3_rgba, label_4_rgba,
                                   label_5_rgba, label_6_rgba, label_7_rgba};
    HICON icons[7]{};
    for (unsigned i = 0; i < 7; ++i)
        icons[i] = icon(data[i]);
    classic::artwork(nullptr, nullptr, nullptr, nullptr, icons);
    void* memory = nullptr;
    HBITMAP bitmap = classic::label_menu_bitmap(&memory);
    if (!bitmap || !memory)
        return 2;
    auto* pixels = static_cast<unsigned char*>(memory);
    auto hash = [&](RECT r) {
        uint32_t value = 2166136261u;
        for (int y = r.top; y < r.bottom; ++y)
            for (int x = r.left; x < r.right; ++x) {
                auto* p = pixels + 4 * (y * 145 + x);
                value = (value ^ uint32_t(p[0] < 128 && p[1] < 128 && p[2] < 128)) * 16777619u;
            }
        return value;
    };
    // Glyph masks and coordinates come from the supplied classic Finder reference;
    // thresholding removes its cursor halo without changing the black glyphs.
    // None also excludes the four pointer pixels above its lowercase n.
    const uint32_t expected[] = {0x89666720, 0x89efb0de, 0xdaaee233, 0xa37ee976,
                                 0xe8b94385, 0x615f2e2c, 0x4a523bce, 0x6a971e86};
    bool ok = true;
    for (unsigned i = 0; i < 8; ++i) {
        RECT region = i ? RECT{43, 31 + int(i - 1) * 16, 141, 47 + int(i - 1) * 16} : RECT{15, 1, 100, 17};
        auto actual = hash(region);
        std::cout << "Label " << i << " glyph mask: " << std::hex << actual << " expected " << expected[i] << std::dec
                  << '\n';
        ok &= actual == expected[i];
    }
    const unsigned char reference_colors[7][3] = {{102, 51, 0},  {0, 102, 0}, {0, 0, 204},  {0, 153, 255},
                                                  {255, 0, 153}, {221, 0, 0}, {255, 102, 0}};
    for (unsigned i = 0; i < 7; ++i)
        for (unsigned y = 33 + i * 16; y < 45 + i * 16; ++y)
            for (unsigned x = 15; x < 39; ++x) {
                auto* p = pixels + 4 * (y * 145 + x);
                ok &= p[2] == label_menu_rgb[i + 1][0] && p[1] == label_menu_rgb[i + 1][1] &&
                      p[0] == label_menu_rgb[i + 1][2];
            }
    ok &= hash({3, 3, 14, 13}) == 0xcd2e1097u;
    std::cout << "Checkmark mask: " << std::hex << hash({3, 3, 14, 13}) << std::dec << '\n';
    // Normalize only the configured swatch colors to the reference palette;
    // compare every other RGB pixel, including text, borders and shadows.
    uint32_t body = 2166136261u;
    for (unsigned y = 48; y < 145; ++y)
        for (unsigned x = 0; x < 144; ++x) {
            auto* pixel = pixels + 4 * (y * 145 + x);
            unsigned label = (y - 33) / 16;
            bool swatch = label < 7 && y < 45 + label * 16 && x >= 15 && x < 39;
            for (int channel = 2; channel >= 0; --channel) {
                auto value = swatch ? reference_colors[label][2 - channel] : pixel[channel];
                body = (body ^ value) * 16777619u;
            }
        }
    std::cout << "Lower menu RGB: " << std::hex << body << std::dec << '\n';
    ok &= body == 0x11d7274du;
    wchar_t module[32768];
    GetModuleFileNameW(nullptr, module, 32768);
    auto path = std::filesystem::path(module).parent_path() / L"label-menu.bmp";
    BITMAPFILEHEADER file{};
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER);
    file.bfSize = file.bfOffBits + 145 * 145 * 4;
    BITMAPINFOHEADER header{};
    header.biSize = sizeof(header);
    header.biWidth = 145;
    header.biHeight = -145;
    header.biPlanes = 1;
    header.biBitCount = 32;
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<char*>(&file), sizeof(file));
    output.write(reinterpret_cast<char*>(&header), sizeof(header));
    output.write(reinterpret_cast<char*>(pixels), 145 * 145 * 4);
    output.close();
    DeleteObject(bitmap);
    for (auto value : icons)
        DestroyIcon(value);
    classic::finish();
    if (!ok) {
        std::cerr << "FAIL: Label menu differs from reference; render saved to " << path << '\n';
        return 2;
    }
    std::cout << "PASS: reference Label menu glyph positions and full-size swatch pixels\n";
}
