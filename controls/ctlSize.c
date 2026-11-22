/*+===================================================================
  File:      ctlSize.c

  Summary:   Implements ctl size support for Macintosh Control Manager operations.

  Classes:   None.

  Functions: No externally linked function definitions.

  Origin:    Existing repository source in executor/src/controls; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986, 1988, 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in ControlMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"

#include "WindowMgr.h"
#include "ControlMgr.h"
#include "EventMgr.h"
#include "ToolboxUtil.h"

#include "rsys/ctl.h"

P3(PUBLIC pascal trap, void, MoveControl, ControlHandle, c, /* IMI-325 */
   INTEGER, h, INTEGER, v)
{
    if (!c || !STARH(c)) {
        warning_unexpected("MoveControl called with null control");
        return;
    }
    if (Hx(c, contrlVis)) {
        HideControl(c);
        HxX(c, contrlRect.right) = CW(Hx(c, contrlRect.right) + h - Hx(c, contrlRect.left));
        HxX(c, contrlRect.bottom) = CW(Hx(c, contrlRect.bottom) + v - Hx(c, contrlRect.top));
        HxX(c, contrlRect.left) = CW(h);
        HxX(c, contrlRect.top) = CW(v);
        ShowControl(c);
    }
    else {
        HxX(c, contrlRect.right) = CW(Hx(c, contrlRect.right) + h - Hx(c, contrlRect.left));
        HxX(c, contrlRect.bottom) = CW(Hx(c, contrlRect.bottom) + v - Hx(c, contrlRect.top));
        HxX(c, contrlRect.left) = CW(h);
        HxX(c, contrlRect.top) = CW(v);
    }
}

P5(PUBLIC pascal trap, void, DragControl, ControlHandle, c, /* IMI-325 */
   Point, p, Rect*, limit, Rect*, slop, INTEGER, axis)
{
    RgnHandle rh;
    LONGINT l;

    CTL_CALL_EXCURSION(c, {
        if (!(CTLCALL(c, dragCntl, 0) & 0xf000)) {
            rh = NewRgn();
            CTLCALL(c, calcCntlRgn, (intptr_t)rh);
            l = DragGrayRgn(rh, p, limit, slop, axis, (ProcPtr)0);
            if ((uint32)l != 0x80008000)
                MoveControl(c, Hx(c, contrlRect.left) + LoWord(l), Hx(c, contrlRect.top) + HiWord(l));

            DisposeRgn(rh);
        }
    });
}

P3(PUBLIC pascal trap, void, SizeControl, ControlHandle, c, /* IMI-326 */
   INTEGER, width, INTEGER, height)
{
    if (!c || !STARH(c)) {
        warning_unexpected("SizeControl called with null control");
        return;
    }
    if (Hx(c, contrlVis)) {
        HideControl(c);
        HxX(c, contrlRect.right) = CW(Hx(c, contrlRect.left) + width);
        HxX(c, contrlRect.bottom) = CW(Hx(c, contrlRect.top) + height);
        ShowControl(c);
    }
    else {
        HxX(c, contrlRect.right) = CW(Hx(c, contrlRect.left) + width);
        HxX(c, contrlRect.bottom) = CW(Hx(c, contrlRect.top) + height);
    }
}
