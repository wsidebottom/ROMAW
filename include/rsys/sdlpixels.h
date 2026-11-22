/*+===================================================================
  File:      sdlpixels.h

  Summary:   Expand Macintosh packed pixels into SDL's byte-sized palette indices. ifndef RSYS_SDLPIXELS_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Expand Macintosh packed pixels into SDL's byte-sized palette indices. */
#ifndef RSYS_SDLPIXELS_H
#define RSYS_SDLPIXELS_H

#include <string.h>
#include "rsys/displayscale.h"

static inline void ROMlib_sdl_copy_scaled_row(unsigned char* dst, const unsigned char* src, int left, int right,
                                              int origin, int bpp, int scale)
{
    for (int x = left; x < right; ++x) {
        int guest = origin + ROMlib_scale_guest(x, scale);
        dst[x - left] =
            bpp == 8 ? src[guest] : (src[(guest * bpp) >> 3] >> (8 - bpp - ((guest * bpp) & 7))) & ((1 << bpp) - 1);
    }
}

/* A cropped host surface starts at zero even for unaligned guest pixels. */
static inline void ROMlib_sdl_copy_cropped_row(unsigned char* dst, const unsigned char* src, int left, int right,
                                               int bpp)
{
    if (bpp == 8)
        memcpy(dst, src + left, right - left);
    else
        for (int x = left; x < right; ++x)
            dst[x - left] = (src[(x * bpp) >> 3] >> (8 - bpp - ((x * bpp) & 7))) & ((1 << bpp) - 1);
}

static inline void ROMlib_sdl_copy_indexed_row(unsigned char* dst, const unsigned char* src, int left, int right,
                                               int bpp)
{
    int x;

    if (bpp == 8)
        memcpy(dst + left, src + left, right - left);
    else
        for (x = left; x < right; ++x)
            dst[x] = (src[(x * bpp) >> 3] >> (8 - bpp - ((x * bpp) & 7))) & ((1 << bpp) - 1);
}

#endif
