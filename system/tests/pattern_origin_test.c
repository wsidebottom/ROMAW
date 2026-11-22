/*+===================================================================
  File:      pattern_origin_test.c

  Summary:   Exercises portable pattern blits across negative port origins on x64.

  Classes:   None.

  Functions: main.

  Origin:    Original regression fixture for the Resplendence resource-view crash.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/
#define NDEBUG 1
#include "../../quickdraw/xdblt.c"
#include "../../quickdraw/rawpatblt.c"
#undef main
#undef NDEBUG
#include <assert.h>

int main(void)
{
    uint32 pixels[8][16];
    for (int top = -3; top <= 3; ++top) {
        memset(pixels, 0xa5, sizeof pixels);
        INTEGER region[] = {CW(top), 1, 7, RGNSTOP, CW(top + 3), RGNSTOP, RGNSTOPX};
        /* Logical row zero is inside the allocation, even when drawing starts
           at a negative logical coordinate. Preserve untouched guard rows. */
        xdblt_dst_baseaddr = &pixels[3 - top][0];
        xdblt_dst_row_bytes = sizeof pixels[0];
        xdblt_log2_bpp = 5;
        xdblt_x_offset = 0;
        xdblt_pattern_value = 0x12345678;
        xdblt_stub_table = xdblt_short_narrow_stubs[XDBLT_COPY];
        xdblt_rgn_start = region;
        xdblt_canon_pattern();
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 16; ++x)
                assert(pixels[y][x] == (y >= 3 && y < 6 && x >= 1 && x < 7 ? 0x12345678 : 0xa5a5a5a5));
    }
    puts("Pattern blits: negative, zero and positive row origins preserve guards");
    return 0;
}
