/*+===================================================================
  File:      windUpdate.c

  Summary:   Implements wind update support for Macintosh Window Manager operations.

  Classes:   None.

  Functions: ROMlib_invalidate_update_vis, ROMlib_invalidate_window_contents, ROMlib_offset_update_vis

  Origin:    Existing repository source in executor/src/windows; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986, 1989, 1990, 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in WindowMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "WindowMgr.h"

#include "rsys/cquick.h"
#include "rsys/wind.h"
#include "rsys/layers.h"

static void invalidate_window_content(WindowPeek w, RgnHandle damage, RgnHandle content)
{
    if (WINDOW_VISIBLE_X(w)) {
        /* contRgn may alias a visRgn restricted by BeginUpdate. */
        Rect bounds = PORT_RECT(w);
        OffsetRect(&bounds, -CW(PORT_BOUNDS(w).left), -CW(PORT_BOUNDS(w).top));
        RectRgn(content, &bounds);
        SectRgn(content, damage, content);
        UnionRgn(WINDOW_UPDATE_REGION(w), content, WINDOW_UPDATE_REGION(w));
    }
}

/* SuperMario LayerMgr.c, PaintTheDesk accumulates every painted desktop area
   into its owner's update region. Finder's desktop is a window with an empty
   structure region, so ordinary ClipAbove does not exclude its content. */
void ROMlib_invalidate_window_contents(RgnHandle damage)
{
    RgnHandle content = NewRgn();
    if (!content)
        return;
    for (WindowPeek w = ROMlib_window_first_all(); w; w = ROMlib_window_next_all(w))
        invalidate_window_content(w, damage, content);
    /* Finder temporarily unlinks its windows while extracting application
     icons. Their palette associations still own live window references. */
    for (WindowPtr w = ROMlib_palette_unlisted_window_after(NULL); w; w = ROMlib_palette_unlisted_window_after(w))
        invalidate_window_content((WindowPeek)w, damage, content);
    DisposeRgn(content);
}

P1(PUBLIC pascal trap, void, InvalRect, Rect*, r)
{
    if (thePort) {
        RgnHandle rh;
        Rect r_copy;

        r_copy = *r; /* Just in case NewRgn moves memory */
        rh = NewRgn();
        RectRgn(rh, &r_copy);
        OffsetRgn(rh, -CW(PORT_BOUNDS(thePort).left), -CW(PORT_BOUNDS(thePort).top));
        UnionRgn(rh, WINDOW_UPDATE_REGION(thePort), WINDOW_UPDATE_REGION(thePort));
        DisposeRgn(rh);
    }
}

P1(PUBLIC pascal trap, void, InvalRgn, RgnHandle, r)
{
    GrafPtr current_port;
    RgnHandle update_rgn;
    int top, left;

    current_port = thePort;
    top = CW(PORT_BOUNDS(current_port).top);
    left = CW(PORT_BOUNDS(current_port).left);

    OffsetRgn(r, -left, -top);

    update_rgn = WINDOW_UPDATE_REGION(current_port);
    UnionRgn(r, update_rgn, update_rgn);

    OffsetRgn(r, left, top);
}

P1(PUBLIC pascal trap, void, ValidRect, Rect*, r)
{
    RgnHandle rh;

    rh = NewRgn();
    RectRgn(rh, r);
    OffsetRgn(rh, -CW(PORT_BOUNDS(thePort).left), -CW(PORT_BOUNDS(thePort).top));
    DiffRgn(WINDOW_UPDATE_REGION(thePort), rh, WINDOW_UPDATE_REGION(thePort));
    DisposeRgn(rh);
}

P1(PUBLIC pascal trap, void, ValidRgn, RgnHandle, r)
{
    OffsetRgn(r, -CW(PORT_BOUNDS(thePort).left), -CW(PORT_BOUNDS(thePort).top));
    DiffRgn(WINDOW_UPDATE_REGION(thePort), r, WINDOW_UPDATE_REGION(thePort));
    OffsetRgn(r, CW(PORT_BOUNDS(thePort).left), CW(PORT_BOUNDS(thePort).top));
}

PUBLIC int ROMlib_emptyvis = 0;

/* SaveVisRgn belongs to the current window update, not to arbitrary ports
   used for offscreen rendering while that update is in progress. */
static GrafPtr update_port;

void ROMlib_invalidate_update_vis(GrafPtr port)
{
    /* LayerMgr.c UpdatedVisRgn discards an update snapshot when the window
       manager recalculates visibility while that update is in progress. */
    if (port == update_port) {
        update_port = NULL;
        if (SaveVisRgn)
            SetEmptyRgn(MR(SaveVisRgn));
        ROMlib_emptyvis = 0;
    }
}

void ROMlib_offset_update_vis(GrafPtr port, INTEGER dh, INTEGER dv)
{
    if (port == update_port && SaveVisRgn)
        OffsetRgn(MR(SaveVisRgn), dh, dv);
}

P1(PUBLIC pascal trap, void, BeginUpdate, WindowPtr, w)
{
    /* #warning Should SaveVisRgn ever become 0? */
    if (!SaveVisRgn)
        HIDDEN_RAW_SET(SaveVisRgn_H, (RgnHandle)RM(NewHandle(0)));

    update_port = (GrafPtr)w;
    CopyRgn(PORT_VIS_REGION(w), MR(SaveVisRgn));

    if (EmptyRgn(WINDOW_UPDATE_REGION(w)))
        ROMlib_emptyvis = 1;
#if 0
  else /* our ROMlib_emptyvis hack below doesn't work if there are some
	  routines that write to the screen and ignore it */
#endif
    {
        CopyRgn(WINDOW_UPDATE_REGION(w), PORT_VIS_REGION(w));
        OffsetRgn(PORT_VIS_REGION(w), CW(PORT_BOUNDS(w).left), CW(PORT_BOUNDS(w).top));
        SectRgn(PORT_VIS_REGION(w), MR(SaveVisRgn), PORT_VIS_REGION(w));
        SetEmptyRgn(WINDOW_UPDATE_REGION(w));
        ROMlib_emptyvis = EmptyRgn(PORT_VIS_REGION(w));
    }
}

P1(PUBLIC pascal trap, void, EndUpdate, WindowPtr, w)
{
    ROMlib_emptyvis = 0;
    /* WindowMgr.a EndUpdate only restores a matching, still-valid snapshot. */
    if (!w || update_port != (GrafPtr)w) {
        CalcVis((WindowPeek)w);
        return;
    }
    CopyRgn(MR(SaveVisRgn), PORT_VIS_REGION(w));
    SetEmptyRgn(MR(SaveVisRgn));
    update_port = NULL;
}
