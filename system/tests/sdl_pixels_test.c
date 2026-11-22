/*+===================================================================
  File:      sdl_pixels_test.c

  Summary:   Check all clipping ranges against known palette indices at each depth.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Check all clipping ranges against known palette indices at each depth. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rsys/sdlpixels.h"

static void check_depth(const unsigned char* packed, const unsigned char* expected, int width, int bpp)
{
    unsigned char guarded[24];
    int left, right, x;

    for (left = 0; left <= width; ++left)
        for (right = left; right <= width; ++right) {
            memset(guarded, 0xCC, sizeof guarded);
            ROMlib_sdl_copy_indexed_row(guarded + 2, packed, left, right, bpp);
            for (x = 0; x < (int)sizeof guarded; ++x)
                assert(guarded[x] == (x >= left + 2 && x < right + 2 ? expected[x - 2] : 0xCC));
            memset(guarded, 0xCC, sizeof guarded);
            ROMlib_sdl_copy_cropped_row(guarded + 2, packed, left, right, bpp);
            for (x = 0; x < (int)sizeof guarded; ++x)
                assert(guarded[x] == (x >= 2 && x < right - left + 2 ? expected[x - 2 + left] : 0xCC));
        }
}

int main(void)
{
    static const unsigned char mono[] = {0xA5, 0x5A};
    static const unsigned char mono_pixels[] = {1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0};
    static const unsigned char two[] = {0x1B, 0xE4};
    static const unsigned char two_pixels[] = {0, 1, 2, 3, 3, 2, 1, 0};
    static const unsigned char four[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    static const unsigned char four_pixels[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    static const unsigned char eight[] = {7, 3, 254, 0, 99};

    check_depth(mono, mono_pixels, 16, 1);
    check_depth(two, two_pixels, 8, 2);
    check_depth(four, four_pixels, 16, 4);
    check_depth(eight, eight, 5, 8);
    int percent = 777;
    assert(ROMlib_scale_parse("1.5x", &percent) && percent == 150);
    assert(ROMlib_scale_parse("0.25", &percent) && percent == 25);
    assert(ROMlib_scale_parse("16x", &percent) && percent == 1600);
    for (const char** p = (const char*[]){"", "0", "-1", "1.234", "17", "nan", "1e2", "99999999999999999999", NULL}; *p;
         ++p) {
        percent = 777;
        assert(!ROMlib_scale_parse(*p, &percent) && percent == 777);
    }
    const int scales[] = {25, 75, 100, 125, 150, 200, 400, 800, 1600};
    const unsigned char* packed[] = {mono, two, four, eight};
    const unsigned char* pixels[] = {mono_pixels, two_pixels, four_pixels, eight};
    const int depths[] = {1, 2, 4, 8}, widths[] = {16, 8, 16, 5};
    for (unsigned s = 0; s < sizeof scales / sizeof scales[0]; ++s)
        for (unsigned d = 0; d < 4; ++d)
            for (int origin = 0; origin < widths[d]; ++origin) {
                int width = ROMlib_scale_edge(widths[d] - origin, scales[s]);
                unsigned char guarded[512];
                for (int split = 0; split <= width; ++split) {
                    memset(guarded, 0xcc, sizeof guarded);
                    ROMlib_sdl_copy_scaled_row(guarded + 1, packed[d], 0, split, origin, depths[d], scales[s]);
                    ROMlib_sdl_copy_scaled_row(guarded + 1 + split, packed[d], split, width, origin, depths[d],
                                               scales[s]);
                    for (int x = 0; x < width; ++x)
                        assert(guarded[1 + x] == pixels[d][origin + x * 100 / scales[s]]);
                    assert(guarded[0] == 0xcc && guarded[1 + width] == 0xcc);
                }
            }
    puts("SDL packed-pixel tests passed (1, 2, 4 and 8 bpp)");
    return 0;
}
