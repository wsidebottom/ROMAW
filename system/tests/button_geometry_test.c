/*+===================================================================
  File:      button_geometry_test.c

  Summary:   Implements button geometry test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rsys/button_geometry.h"

int main(void)
{
    /* Upper-left border pixels from the supplied System 7 example.
     The normal button face and label are excluded. */
    static const char* corner[] = {
        ".....########", "...##########", "..###########", ".#####.......",
        ".###.........", "####.........", "###..........",
    };
    for (int y = 0; y < 28; y++) {
        char pixels[67];
        int spans[4];
        int count = ROMlib_default_button_spans(67, 28, y, spans);
        memset(pixels, '.', sizeof pixels);
        for (int i = 0; i < count; i += 2) {
            assert(spans[i] >= 0 && spans[i + 1] <= 67);
            assert(spans[i] < spans[i + 1]);
            memset(pixels + spans[i], '#', spans[i + 1] - spans[i]);
        }
        int edge = y < 27 - y ? y : 27 - y;
        if (edge < 7)
            assert(!memcmp(pixels, corner[edge], 13));
        for (int x = 0; x < 67; x++)
            assert(pixels[x] == pixels[66 - x]);
    }
    puts("Default-button outline corner pixels passed");
    return 0;
}
