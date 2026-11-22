/*+===================================================================
  File:      pictheader.h

  Summary:   Picture size compatibility, not validation of the opcode stream. ifndef RSYS_PICTHEADER_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Picture size compatibility, not validation of the opcode stream. */
#ifndef RSYS_PICTHEADER_H
#define RSYS_PICTHEADER_H
#include <stddef.h>
#include <stdint.h>

static inline int ROMlib_pict_header_valid(const unsigned char* bytes, size_t size)
{
    unsigned declared;
    if (!bytes || size < 10)
        return 0;
    /* Apple Inside Macintosh: Imaging With QuickDraw, Picture record.
     picSize is meaningful only for version 1; version 2 pictures can exceed
     the 16-bit field. Their actual extent comes from GetHandleSize.
     https://leopard-adc.pepas.com/documentation/mac/QuickDraw/QuickDraw-337.html */
    if (size >= 14 && bytes[10] == 0 && bytes[11] == 0x11 && bytes[12] == 2 && bytes[13] == 0xFF)
        return 1;
    declared = ((unsigned)bytes[0] << 8) | bytes[1];
    /* A zero size is also used with custom getPicProc streaming callbacks. */
    return declared == 0 || (declared >= 10 && declared <= size);
}
#endif
