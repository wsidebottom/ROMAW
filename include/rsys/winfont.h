/*+===================================================================
  File:      winfont.h

  Summary:   ifndef RSYS_WINFONT_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef RSYS_WINFONT_H
#define RSYS_WINFONT_H

void ROMlib_win_font_set_directory(const char* installation);

/* Render a MacRoman run using the supplied TrueType outlines, with caller-supplied
   glyph origins so Macintosh text measurement and placement remain unchanged.
   The returned monochrome mask is allocated with malloc; the caller frees it. */
unsigned char* ROMlib_win_text_mask(const char* family, int size, unsigned style, const unsigned char* text, int count,
                                    const int* positions, int x, int baseline, int width, int height, int* pitch);
void ROMlib_win_font_shutdown(void);

#endif
