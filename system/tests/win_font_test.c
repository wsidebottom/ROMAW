/*+===================================================================
  File:      win_font_test.c

  Summary:   Implements win font test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "rsys/winfont.h"

int main(int argc, char** argv)
{
    int positions[11], pitch, i, x, y, ink = 0;
    unsigned char* mask;
    DWORD resources = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    for (i = 0; i < 11; ++i)
        positions[i] = i * 66;
    assert(argc == 2);
    ROMlib_win_font_set_directory(argv[1]);
    mask = ROMlib_win_text_mask("Times", 104, 1, (const unsigned char*)"Try MyABCD", 10, positions, 2, 110, 703, 145,
                                &pitch);
    assert(mask && pitch == 88);
    for (y = 0; y < 145; ++y) {
        for (x = 0; x < 703; ++x)
            ink += !!(mask[y * pitch + x / 8] & (0x80 >> (x & 7)));
        assert(!(mask[y * pitch + 87] & 1));
    }
    assert(ink > 1000);
    free(mask);
    positions[1] = 100;
    mask = ROMlib_win_text_mask("Helvetica", 48, 0, (const unsigned char*)"II", 2, positions, 0, 50, 137, 65, &pitch);
    assert(mask && pitch == 20);
    for (y = 0; y < 65; ++y) {
        for (x = 30; x < 100; ++x)
            assert(!(mask[y * pitch + x / 8] & (0x80 >> (x & 7))));
        assert(!(mask[y * pitch + 17] & 0x7f));
        assert(!mask[y * pitch + 18] && !mask[y * pitch + 19]);
    }
    free(mask);
    for (i = 0; i < 11; ++i)
        positions[i] = i * 66;
    for (i = 0; i < 96; ++i) {
        const unsigned char text[] = {'A', 0x80, 0x8e};
        mask = ROMlib_win_text_mask(i & 1 ? "Times" : "Courier", 20 + i, i & 7, text, 3, positions, -2, 120, 201, 145,
                                    &pitch);
        assert(mask);
        free(mask);
    }
    assert(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= resources + 16);
    pitch = 123;
    assert(!ROMlib_win_text_mask("Custom", 48, 0, (const unsigned char*)"A", 1, positions, 0, 50, 100, 65, &pitch));
    assert(!ROMlib_win_text_mask("Times", 48, 8, (const unsigned char*)"A", 1, positions, 0, 50, 100, 65, &pitch));
    assert(!ROMlib_win_text_mask("Times", 513, 0, (const unsigned char*)"A", 1, positions, 0, 50, 100, 65, &pitch));
    assert(!ROMlib_win_text_mask("Times", 48, 0, (const unsigned char*)"A", 1025, positions, 0, 50, 100, 65, &pitch));
    assert(!ROMlib_win_text_mask("Times", 48, 0, (const unsigned char*)"A", 1, positions, 0, 50, 8193, 65, &pitch));
    assert(!ROMlib_win_text_mask("Times", 48, 0, (const unsigned char*)"A", 1, NULL, 0, 50, 100, 65, &pitch));
    assert(pitch == 123);
    assert(!ROMlib_win_text_mask("Times", 48, 0, (const unsigned char*)"\001", 1, positions, 0, 50, 100, 65, &pitch));
    ROMlib_win_font_shutdown();
    ROMlib_win_font_shutdown();
    assert(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= resources);
    puts("Windows scalable font mask tests passed");
    return 0;
}
