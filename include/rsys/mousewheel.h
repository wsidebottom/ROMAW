/*+===================================================================
  File:      mousewheel.h

  Summary:   Declares mousewheel support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#ifndef RSYS_MOUSEWHEEL_H
#define RSYS_MOUSEWHEEL_H

/* Return a point inside a standard scrollbar arrow, never its thumb or track.
 * Coordinates here are host-endian integers; callers handle guest byte order. */
static int ROMlib_wheel_arrow(int left, int top, int right, int bottom, int horizontal, int direction, int* x, int* y)
{
    int width = right - left, height = bottom - top;
    if (width < 3 || height < 3 || (horizontal ? width <= height * 2 : height <= width * 2))
        return 0;
    *x = horizontal ? (direction < 0 ? left + height / 2 : right - height / 2 - 1) : left + width / 2;
    *y = horizontal ? top + height / 2 : (direction < 0 ? top + width / 2 : bottom - width / 2 - 1);
    return 1;
}
#endif
