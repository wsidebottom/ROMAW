/*+===================================================================
  File:      qBit.c

  Summary:   Implements q bit support for Macintosh QuickDraw graphics.

  Classes:   None.

  Functions: No externally linked function definitions.

  Origin:    Existing repository source in executor/src/quickdraw; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986, 1988, 1989, 1990, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in QuickDraw.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "QuickDraw.h"
#include "CQuickDraw.h"
#include "WindowMgr.h"
#include "MemoryMgr.h"
#include "OSUtil.h"

#include "rsys/cquick.h"
#include "rsys/stdbits.h"
#include "rsys/picture.h"
#include "rsys/mman.h"

P6(PUBLIC pascal trap, void, CopyBits, BitMap*, src_bitmap, BitMap*, dst_bitmap, const Rect*, src_rect, const Rect*,
   dst_rect, INTEGER, mode, RgnHandle, mask)
{
    if (ROMlib_text_output_disabled_p)
        /*-->*/ return;

#define StdBits_TOOLTRAP_NUMBER (0xEB)

#warning ctm hack below (mode = srcCopy) -- might not be best solution

    ZONE_SAVE_EXCURSION(SysZone, {
        int dst_is_theport_p;

        /* use `->portBits' instead of `PORT_BITS ()', because here we want
                     those actual bytes, not the logical structure that is the
                     portBits */
        dst_is_theport_p = (!memcmp(dst_bitmap, &thePort->portBits, sizeof(BitMap)) ||
                            (IS_PIXMAP_PTR_P(dst_bitmap) && CGrafPort_p(thePort) &&
                             !memcmp(dst_bitmap, STARH(CPORT_PIXMAP(theCPort)), sizeof(BitMap))));
        if (dst_is_theport_p) {
            if (!CGrafPort_p(thePort) && mode >= blend) {
                warning_unexpected("mode = %d", mode);
                mode = srcCopy;
            }

            CALLBITS(src_bitmap, src_rect, dst_rect, mode, mask);
        }
        else {
            RgnHandle savevisr;
            RgnHandle saveclipr;
            RgnHandle big_region;
            QDProcsPtr gp;

            /* save away thePort fields */
            /* this saves the port bits (in the case of a GrafPort), and
                             the portPixMap in the case of a CGrafPort (among other
                             things */
            FIELD_ASSIGN_RAW(savevisr, PORT_VIS_REGION_X(thePort));
            FIELD_ASSIGN_RAW(saveclipr, PORT_CLIP_REGION_X(thePort));

            big_region = NewRgn();
            SetRectRgn(big_region, -32767, -32767, 32767, 32767);
            FIELD_ASSIGN_RAW(PORT_VIS_REGION_X(thePort), RM(big_region));
            FIELD_ASSIGN_RAW(PORT_CLIP_REGION_X(thePort), RM(big_region));

            /* give a warning of _StdBits or thePort's bitsProc has
                             been patched out */
            gp = PORT_GRAF_PROCS(thePort);
            if (gp && PPR(gp->bitsProc) != P_StdBits)
                warning_unexpected("thePort bitsProc patched out!");

            if (tooltraptable[StdBits_TOOLTRAP_NUMBER] != toolstuff[StdBits_TOOLTRAP_NUMBER].orig)
                warning_unexpected("_StdBits patched out!");

            ROMlib_bogo_stdbits(src_bitmap, dst_bitmap, src_rect, dst_rect, mode, mask);

            /* restore fields */
            FIELD_ASSIGN_RAW(PORT_VIS_REGION_X(thePort), savevisr);
            FIELD_ASSIGN_RAW(PORT_CLIP_REGION_X(thePort), saveclipr);
            DisposeRgn(big_region);
        }
    });
    /* cruft */
}

P4(PUBLIC pascal trap, void, ScrollRect, Rect*, rp, INTEGER, dh, INTEGER, dv, RgnHandle, updatergn)
{
    Rect srcr, dstr;
    RgnHandle temp, temp2, updatergn2, srcregion;
    RGBColor bk_rgb, fg_rgb;
    int32 bk_color, fg_color;
    int cgrafport_p;
    PAUSEDECL;

    TRAPBEGIN();
    PAUSERECORDING;

    cgrafport_p = CGrafPort_p(thePort);
    if (cgrafport_p) {
        fg_rgb = CPORT_RGB_FG_COLOR(thePort);
        bk_rgb = CPORT_RGB_BK_COLOR(thePort);
    }
    fg_color = PORT_FG_COLOR_X(thePort);
    bk_color = PORT_BK_COLOR_X(thePort);

    RGBForeColor(&ROMlib_black_rgb_color);
    RGBBackColor(&ROMlib_white_rgb_color);

    SectRect(rp, &HxX(PORT_VIS_REGION(thePort), rgnBBox), rp);
    SectRect(rp, &HxX(PORT_CLIP_REGION(thePort), rgnBBox), rp);
    SectRect(rp, &PORT_RECT(thePort), rp);
    SectRect(rp, &PORT_BOUNDS(thePort), rp);
    srcregion = NewRgn();
    CopyRgn(PORT_VIS_REGION(thePort), srcregion);
    SectRgn(PORT_CLIP_REGION(thePort), srcregion, srcregion);
    OffsetRgn(srcregion, dh, dv);

    RectRgn(temp2 = NewRgn(), rp);
    DiffRgn(temp2, srcregion, temp2);

    srcr = *rp;
#if 0
  OffsetRect(&srcr, -CW (PORT_BOUNDS (thePort).left),
	     -CW (PORT_BOUNDS (thePort).top));   /* loc to glob */
#endif
    dstr = srcr;
    updatergn2 = NewRgn();
    RectRgn(updatergn2, &srcr);
    if (dh > 0) {
        dstr.left = CW(CW(dstr.left) + dh);
        srcr.right = CW(CW(srcr.right) - dh);
    }
    else {
        srcr.left = CW(CW(srcr.left) - dh);
        dstr.right = CW(CW(dstr.right) + dh);
    }
    if (dv > 0) {
        dstr.top = CW(CW(dstr.top) + dv);
        srcr.bottom = CW(CW(srcr.bottom) - dv);
    }
    else {
        srcr.top = CW(CW(srcr.top) - dv);
        dstr.bottom = CW(CW(dstr.bottom) + dv);
    }
    RectRgn(temp = NewRgn(), &dstr);
    DiffRgn(updatergn2, temp, updatergn2);
#if 0
  OffsetRgn(updatergn2,
	    CW (PORT_BOUNDS (thePort).left),
	    CW (PORT_BOUNDS (thePort).top)); /* glob to loc */
#endif
    UnionRgn(updatergn2, temp2, updatergn2);

    /* I guess here we should clip to portRect, visRgn and clipRgn */
    CopyRgn(PORT_VIS_REGION(thePort), temp);
    SectRgn(temp, PORT_CLIP_REGION(thePort), temp);
    SectRgn(temp, srcregion, temp);

    CopyBits(PORT_BITS_FOR_COPY(thePort), PORT_BITS_FOR_COPY(thePort), &srcr, &dstr, srcCopy, temp);

    if (cgrafport_p) {
        CPORT_RGB_FG_COLOR(thePort) = fg_rgb;
        CPORT_RGB_BK_COLOR(thePort) = bk_rgb;
    }
    PORT_FG_COLOR_X(thePort) = fg_color;
    PORT_BK_COLOR_X(thePort) = bk_color;

    EraseRgn(updatergn2);
    if (updatergn)
        CopyRgn(updatergn2, updatergn);
    DisposeRgn(temp);
    DisposeRgn(temp2);
    DisposeRgn(srcregion);
    DisposeRgn(updatergn2);

    RESUMERECORDING;
    TRAPEND();
}
