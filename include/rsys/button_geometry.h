/*+===================================================================
  File:      button_geometry.h

  Summary:   ifndef ROMLIB_BUTTON_GEOMETRY_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef ROMLIB_BUTTON_GEOMETRY_H
#define ROMLIB_BUTTON_GEOMETRY_H

/* System 7 default-button corners, measured in device pixels. */
static inline int ROMlib_default_button_spans(int width, int height, int row, int spans[4])
{
    static const unsigned char outer[] = {5, 3, 2, 1, 1, 0};
    static const unsigned char inner[] = {3, 1, 1, 0};
    int edge = row < height - 1 - row ? row : height - 1 - row;
    int inset = outer[edge < 6 ? edge : 5];
    int hole;

    spans[0] = inset;
    spans[1] = width - inset;
    if (spans[0] >= spans[1])
        return 0;
    if (edge < 3)
        return 2;
    hole = 3 + inner[edge - 3 < 4 ? edge - 3 : 3];
    if (hole >= width - hole)
        return 2;
    spans[1] = hole;
    spans[2] = width - hole;
    spans[3] = width - inset;
    return 4;
}

#endif
