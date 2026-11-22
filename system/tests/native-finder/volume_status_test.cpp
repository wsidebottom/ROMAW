/*+===================================================================
  File:      volume_status_test.cpp

  Summary:   Verify the resource padlock and item-count layout without painting the user's desktop or mounting drives.

  Classes:   None.

  Functions: main.

  Origin:    Executor native Finder status-strip regression fixture.

  Copyright and Legal notices: License reference: COPYING.
===================================================================+*/

#include "finder/classic.h"
#include "assets.inc"
#include "volume_status.inc"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

#undef assert
#define assert(condition)                                               \
    do {                                                                \
        if (!(condition)) {                                             \
            std::cerr << __LINE__ << ": " << #condition << " failed\n"; \
            std::exit(2);                                               \
        }                                                               \
    } while (false)

int main()
{
    static_assert(sizeof volume_lock_rgba == 16 * 16 * 4);
    finder::classic::initialize(font_chicago, sizeof font_chicago, font_geneva, sizeof font_geneva);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = 16;
    info.bmiHeader.biHeight = -16;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bytes;
    HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bytes, nullptr, 0);
    assert(color);
    auto rgba = volume_lock_rgba;
    auto bgra = static_cast<unsigned char*>(bytes);
    unsigned char mask[32]{};
    unsigned ink = 0;
    for (unsigned i = 0; i < 256; ++i) {
        unsigned alpha = rgba[i * 4 + 3];
        bgra[i * 4] = rgba[i * 4 + 2] * alpha / 255;
        bgra[i * 4 + 1] = rgba[i * 4 + 1] * alpha / 255;
        bgra[i * 4 + 2] = rgba[i * 4] * alpha / 255;
        bgra[i * 4 + 3] = alpha;
        if (!alpha)
            mask[i / 8] |= 0x80 >> (i % 8);
        else
            ++ink;
    }
    assert(ink && ink < 256); // The padded SICN must not become an opaque square.
    HBITMAP mono = CreateBitmap(16, 16, 1, 1, mask);
    ICONINFO icon{};
    icon.fIcon = TRUE;
    icon.hbmColor = color;
    icon.hbmMask = mono;
    HICON lock = CreateIconIndirect(&icon);
    assert(lock);
    DeleteObject(color);
    DeleteObject(mono);

    constexpr int width = 160, height = 22;
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    HBITMAP canvas = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bytes, nullptr, 0);
    HDC dc = CreateCompatibleDC(nullptr);
    assert(canvas && dc);
    auto previous = SelectObject(dc, canvas);
    auto pixels = static_cast<unsigned*>(bytes);
    constexpr unsigned marker = 0x00345678;
    RECT bounds{4, 2, 154, 18};
    auto reset = [&] { std::fill_n(pixels, width * height, marker); };
    reset();
    SetTextColor(dc, RGB(12, 34, 56));
    SetBkMode(dc, OPAQUE);
    finder::classic::draw_folder_status(dc, bounds, L"13 items");
    GdiFlush();
    assert(GetTextColor(dc) == RGB(12, 34, 56) && GetBkMode(dc) == OPAQUE);
    std::vector<unsigned> unlocked(pixels, pixels + width * height);
    reset();
    finder::classic::draw_folder_status(dc, bounds, L"13 items", lock);
    GdiFlush();
    unsigned matched = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            unsigned pixel = pixels[y * width + x] & 0xffffff;
            if (x < bounds.left || x >= bounds.right || y < bounds.top || y >= bounds.bottom) {
                assert(pixel == marker); // Border/separator rows and adjacent fields stay untouched.
                continue;
            }
            int local_x = x - bounds.left, local_y = y - bounds.top;
            if (local_x < 14) {
                unsigned index = (local_y * 16 + local_x + 2) * 4;
                unsigned alpha = rgba[index + 3];
                unsigned expected = 0;
                for (unsigned channel = 0; channel < 3; ++channel)
                    expected |= ((rgba[index + channel] * alpha + 255 * (255 - alpha)) / 255) << (16 - 8 * channel);
                assert(pixel == expected);
                matched += alpha != 0;
            }
            if (local_x >= 16)
                assert(pixel == (unlocked[y * width + x - 16] & 0xffffff)); // Count shifts exactly one icon cell.
        }
    assert(matched == ink);
    reset();
    RECT narrow{4, 2, 24, 18};
    finder::classic::draw_folder_status(dc, narrow, L"13 items", lock);
    GdiFlush();
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (x < narrow.left || x >= narrow.right || y < narrow.top || y >= narrow.bottom)
                assert((pixels[y * width + x] & 0xffffff) == marker);
    reset();
    finder::classic::draw_folder_status(dc, bounds, L"13 items");
    GdiFlush();
    for (size_t i = 0; i < unlocked.size(); ++i)
        assert((pixels[i] & 0xffffff) == (unlocked[i] & 0xffffff)); // Writable state restores ordinary placement.
    SelectObject(dc, previous);
    DeleteObject(canvas);
    DeleteDC(dc);
    DestroyIcon(lock);
    finder::classic::finish();
    std::cout
        << "PASS: resource-exact 16 px volume lock, transparent padding, locked/writable item-count layout, clipping and preserved separators/DC state\n";
}
