/*+===================================================================
  File:      text_scale_test.c

  Summary:   Preserve one-pixel strokes that nearest-neighbor font reduction loses.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Preserve one-pixel strokes that nearest-neighbor font reduction loses. */
#include <assert.h>
#include <stdio.h>
#include "rsys/textscale.h"

static unsigned bit(const unsigned char* p, int pitch, int x, int y)
{
    return !!(p[y * pitch + x / 8] & (128 >> (x % 8)));
}

int main(void)
{
    unsigned char source[17 * 8], guarded[17 * 8 + 2];
    int sw, sh, x, y, offset;
    for (sw = 1; sw <= 31; ++sw)
        for (sh = 1; sh <= 17; ++sh)
            for (offset = 0; offset < 8; ++offset) {
                int pitch = (sw + 7) / 8;
                for (x = 0; x < (int)sizeof source; ++x)
                    source[x] = (unsigned char)(x * 137 + sw * 29 + sh);
                memset(guarded, 0xCD, sizeof guarded);
                ROMlib_reduce_text_bitmap(source, 8, offset, sw, sh, guarded + 1, pitch, sw, sh);
                assert(guarded[0] == 0xCD && guarded[pitch * sh + 1] == 0xCD);
                for (y = 0; y < sh; ++y)
                    for (x = 0; x < sw; ++x)
                        assert(bit(guarded + 1, pitch, x, y) == bit(source, 8, x + offset, y));
                for (y = 0; y < sh; ++y)
                    for (x = sw; x < pitch * 8; ++x)
                        assert(!bit(guarded + 1, pitch, x, y));
            }
    memset(source, 0, sizeof source);
    source[3] = source[7] = source[11] = 128;
    ROMlib_reduce_text_bitmap(source, 1, 0, 1, 15, guarded, 1, 1, 11);
    for (y = 0; y < 11; ++y)
        assert(bit(guarded, 1, 0, y) == (y == 2 || y == 5 || y == 8));
    memset(source, 0, sizeof source);
    source[0] = 0x11;
    source[1] = 0x10;
    ROMlib_reduce_text_bitmap(source, 2, 0, 15, 1, guarded, 2, 11, 1);
    for (x = 0; x < 11; ++x)
        assert(bit(guarded, 2, x, 0) == (x == 2 || x == 5 || x == 8));
    for (sw = 1; sw <= 31; ++sw)
        for (sh = 1; sh <= 17; ++sh) {
            int dw = (sw + 1) / 2, dh = (sh + 1) / 2, pitch = (dw + 7) / 8;
            memset(source, 0xFF, sizeof source);
            memset(guarded, 0xCD, sizeof guarded);
            ROMlib_reduce_text_bitmap(source, 8, 3, sw, sh, guarded + 1, pitch, dw, dh);
            assert(guarded[0] == 0xCD && guarded[pitch * dh + 1] == 0xCD);
            for (y = 0; y < dh; ++y)
                for (x = 0; x < dw; ++x)
                    assert(bit(guarded + 1, pitch, x, y));
            memset(source, 0, sizeof source);
            ROMlib_reduce_text_bitmap(source, 8, 3, sw, sh, guarded + 1, pitch, dw, dh);
            for (x = 0; x < pitch * dh; ++x)
                assert(!guarded[x + 1]);
        }
    puts(
        "PASS: reduced text preserves thin strokes; identity, unaligned sources, solid/empty images, padding and guards");
    return 0;
}
