/*+===================================================================
  File:      qCRegular.c

  Summary:   Implements q cregular support for Macintosh QuickDraw graphics.

  Classes:   None.

  Functions: No externally linked function definitions.

  Origin:    Existing repository source in executor/src/quickdraw; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1994 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in QuickDraw.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "QuickDraw.h"
#include "CQuickDraw.h"

#include "rsys/quick.h"
#include "rsys/cquick.h"

#define FillCxxx(CALLxxx)                                                       \
    do {                                                                        \
        if (CGrafPort_p(thePort)) {                                             \
            PixPatHandle orig_fill_pixpat_x;                                    \
                                                                                \
            PenMode(patCopy);                                                   \
            FIELD_ASSIGN_RAW(orig_fill_pixpat_x, CPORT_FILL_PIXPAT_X(thePort)); \
            /* ROMlib_fill_pixpat (pixpat); */                                  \
            FIELD_ASSIGN_RAW(CPORT_FILL_PIXPAT_X(thePort), RM(pixpat));         \
            CALLxxx;                                                            \
            FIELD_ASSIGN_RAW(CPORT_FILL_PIXPAT_X(thePort), orig_fill_pixpat_x); \
        }                                                                       \
        else {                                                                  \
            PATASSIGN(PORT_FILL_PAT(thePort), PIXPAT_1DATA(pixpat));            \
            CALLxxx;                                                            \
        }                                                                       \
    } while (FALSE)

P2(PUBLIC pascal trap, void, FillCRect, Rect*, r, PixPatHandle, pixpat)
{
    /* #warning "restore settings after FillCxxx?" */
    FillCxxx(CALLRECT(fill, r));
}

P4(PUBLIC pascal trap, void, FillCRoundRect, const Rect*, r, short, ovalWidth, short, ovalHeight, PixPatHandle, pixpat)
{
    FillCxxx(CALLRRECT(fill, (Rect*)r, ovalWidth, ovalHeight));
}

P2(PUBLIC pascal trap, void, FillCOval, const Rect*, r, PixPatHandle, pixpat)
{
    FillCxxx(CALLOVAL(fill, (Rect*)r));
}

P4(PUBLIC pascal trap, void, FillCArc, const Rect*, r, short, startAngle, short, arcAngle, PixPatHandle, pixpat)
{
    FillCxxx(CALLARC(fill, (Rect*)r, startAngle, arcAngle));
}

P2(PUBLIC pascal trap, void, FillCPoly, PolyHandle, poly, PixPatHandle, pixpat)
{
    FillCxxx(CALLPOLY(fill, poly));
}

P2(PUBLIC pascal trap, void, FillCRgn, RgnHandle, rgn, PixPatHandle, pixpat)
{
    FillCxxx(CALLRGN(fill, rgn));
}
