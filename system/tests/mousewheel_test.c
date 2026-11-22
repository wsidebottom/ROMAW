/*+===================================================================
  File:      mousewheel_test.c

  Summary:   Implements mousewheel test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include "rsys/mousewheel.h"

int main(void)
{
    int x = -1, y = -1;
    assert(ROMlib_wheel_arrow(100, 20, 116, 220, 0, -1, &x, &y));
    assert(x == 108 && y == 28);
    assert(ROMlib_wheel_arrow(100, 20, 116, 220, 0, 1, &x, &y));
    assert(x == 108 && y == 211);
    assert(ROMlib_wheel_arrow(-200, -30, 200, -14, 1, -1, &x, &y));
    assert(x == -192 && y == -22);
    assert(ROMlib_wheel_arrow(-200, -30, 200, -14, 1, 1, &x, &y));
    assert(x == 191 && y == -22);
    assert(!ROMlib_wheel_arrow(0, 0, 100, 20, 0, 1, &x, &y));
    assert(!ROMlib_wheel_arrow(0, 0, 20, 100, 1, 1, &x, &y));
    assert(!ROMlib_wheel_arrow(0, 0, 20, 20, 1, 1, &x, &y));
    assert(!ROMlib_wheel_arrow(10, 10, 0, 0, 0, 1, &x, &y));
    assert(!ROMlib_wheel_arrow(0, 0, 1, 200, 0, 1, &x, &y));
    /* Every accepted arrow is strictly inside the control and on its end square. */
    for (int width = 3; width <= 32; ++width)
        for (int length = 3; length <= 512; ++length)
            for (int direction = -1; direction <= 1; direction += 2)
                if (ROMlib_wheel_arrow(0, 0, width, length, 0, direction, &x, &y)) {
                    assert(x > 0 && x < width - 1 && y > 0 && y < length - 1);
                    assert(direction < 0 ? y < width : y >= length - width);
                }
    return 0;
}
