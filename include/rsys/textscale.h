/*+===================================================================
  File:      textscale.h

  Summary:   ifndef ROMLIB_TEXTSCALE_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef ROMLIB_TEXTSCALE_H
#define ROMLIB_TEXTSCALE_H
#include <stdint.h>
#include <string.h>

/* Area coverage keeps thin glyph strokes when a bitmap font is reduced.
   Coordinates are relative to the source rectangle, except for src_left. */
static inline void ROMlib_reduce_text_bitmap(const unsigned char* source, int source_pitch, int src_left,
                                             int source_width, int source_height, unsigned char* destination,
                                             int destination_pitch, int width, int height)
{
    int x, y;
    uint64_t area = (uint64_t)source_width * source_height;
    memset(destination, 0, (size_t)destination_pitch * height);
    for (y = 0; y < height; ++y)
        for (x = 0; x < width; ++x) {
            uint64_t left = (uint64_t)x * source_width;
            uint64_t right = left + source_width;
            uint64_t top = (uint64_t)y * source_height;
            uint64_t bottom = top + source_height;
            uint64_t ink = 0;
            int sx, sy;
            for (sy = top / height; (uint64_t)sy * height < bottom; ++sy) {
                uint64_t y0 = top > (uint64_t)sy * height ? top : (uint64_t)sy * height;
                uint64_t y1 = bottom < (uint64_t)(sy + 1) * height ? bottom : (uint64_t)(sy + 1) * height;
                for (sx = left / width; (uint64_t)sx * width < right; ++sx)
                    if (source[sy * source_pitch + (src_left + sx) / 8] & (128 >> ((src_left + sx) % 8))) {
                        uint64_t x0 = left > (uint64_t)sx * width ? left : (uint64_t)sx * width;
                        uint64_t x1 = right < (uint64_t)(sx + 1) * width ? right : (uint64_t)(sx + 1) * width;
                        ink += (x1 - x0) * (y1 - y0);
                    }
            }
            if (ink * 2 >= area)
                destination[y * destination_pitch + x / 8] |= 128 >> (x % 8);
        }
}
#endif
