/*+===================================================================
  File:      windSize.c

  Summary:   Implements wind size support for Macintosh Window Manager operations.

  Classes:   None.

  Functions: No externally linked function definitions.

  Origin:    Existing repository source in executor/src/windows; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986, 1989, 1990, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in WindowMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "QuickDraw.h"
#include "CQuickDraw.h"
#include "WindowMgr.h"
#include "EventMgr.h"
#include "OSEvent.h"
#include "ToolboxUtil.h"
#include "MemoryMgr.h"

#include "rsys/cquick.h"
#include "rsys/wind.h"
#include "rsys/layers.h"

static boolean_t move_overlaps_window(WindowPeek window, const Rect* source, const Rect* destination)
{
    for (WindowPeek other = ROMlib_window_first_all(); other; other = ROMlib_window_next_all(other))
        if (other != window && WINDOW_VISIBLE_X(other)) {
            const Rect* bounds = &HxX(WINDOW_STRUCT_REGION(other), rgnBBox);
            Rect intersection;
            if (SectRect(source, bounds, &intersection) || SectRect(destination, bounds, &intersection))
                return TRUE;
        }
    return FALSE;
}

/*
 * Note, the code below probably be rewritten to use XorRgn as much
 *	 as possible and probably have only one CalcVisBehind
 */

P4(PUBLIC pascal trap, void, MoveWindow, WindowPtr, wp, INTEGER, h, INTEGER, v, BOOLEAN, front)
{
    GrafPtr gp;
    RgnHandle movepart, updatepart, behindpart;
    Rect r;
    register WindowPeek w;

    TRAPBEGIN();
    w = (WindowPeek)wp;
    gp = thePort;
    if (WINDOW_VISIBLE_X(w)) {
        SetPort(MR(wmgr_port));
        ClipRect(&GD_BOUNDS(MR(TheGDevice)));
        ClipAbove(w);
        movepart = NewRgn();
        updatepart = NewRgn();
        behindpart = NewRgn();
        SectRgn(PORT_CLIP_REGION(MR(wmgr_port)), WINDOW_STRUCT_REGION(w), movepart);

#if 1
        /*
	 * CopyBits does unaligned 32-bit reads from the source, which can
	 * cause it to read beyond the framebuffer in certain circumstances.
	 * This is a cheesy way to prevent that from happening here.  A
	 * better fix would be either in CopyBits or to force an extra page
	 * after the framebuffer.
	 */
        {
            Rect tmpr;
            RgnHandle last_three_pixels;

            tmpr = GD_BOUNDS(MR(TheGDevice));
            tmpr.top = CW(CW(tmpr.bottom) - 1);
            tmpr.left = CW(CW(tmpr.right) - 3);
            last_three_pixels = NewRgn();
            RectRgn(last_three_pixels, &tmpr);
            DiffRgn(movepart, last_three_pixels, movepart);
            DisposeRgn(last_three_pixels);
        }
#endif

        CopyRgn(movepart, behindpart);
        r = HxX(WINDOW_STRUCT_REGION(w), rgnBBox);
    }
#if !defined(LETGCCWAIL)
    else {
        movepart = 0;
        updatepart = 0;
        behindpart = 0;
    }
#endif

#if 1
    /*
 * NOTE: the use of portRect below was introduced by Bill, without comment
 *       either here or in the rcslog.  But taking it out made the MSW5.1
 *	 Picture editting window come up in the wrong place.
 *	 (That could be due to other inconsistencies though, like the
 */
    h += CW(PORT_BOUNDS(w).left) - CW(PORT_RECT(w).left);
    v += CW(PORT_BOUNDS(w).top) - CW(PORT_RECT(w).top);
#else
    h += CW(PORT_BOUNDS(w).left);
    v += CW(PORT_BOUNDS(w).top);
#endif
    if (WINDOW_VISIBLE_X(w)) {
        Rect old_bounds = r;
        OffsetRect(&r, h, v);
        /* Repaint overlapping windows from their contents rather than
         carrying stale pixels from another window to the new location. */
        if ((h || v) && move_overlaps_window(w, &old_bounds, &r))
            SetEmptyRgn(movepart);
        else {
            WRAPPER_PIXMAP_FOR_COPY(wrapper);
            Rect srcr, dstr;

            OffsetRgn(movepart, h, v);
            SectRgn(movepart, PORT_CLIP_REGION(MR(wmgr_port)), movepart);
            ClipRect(&GD_BOUNDS(MR(TheGDevice)));
            WRAPPER_SET_PIXMAP_X(wrapper, GD_PMAP_X(MR(TheGDevice)));

            /* Clip the rectangles as well as the region before reading the
             framebuffer. */
            SectRect(&old_bounds, &GD_BOUNDS(MR(TheGDevice)), &srcr);
            dstr = GD_BOUNDS(MR(TheGDevice));
            OffsetRect(&dstr, h, v);
            SectRect(&dstr, &r, &dstr);
            CopyBits(wrapper, wrapper, &srcr, &dstr, srcCopy, movepart);
        }
    }
    OffsetRgn(WINDOW_STRUCT_REGION(w), h, v);
    OffsetRgn(WINDOW_CONT_REGION(w), h, v);
    OffsetRgn(WINDOW_UPDATE_REGION(w), h, v);
    OffsetRect(&PORT_BOUNDS(w), -h, -v);
    if (WINDOW_VISIBLE_X(w)) {
        ClipRect(&GD_BOUNDS(MR(TheGDevice)));
        ClipAbove(w);
        DiffRgn(WINDOW_STRUCT_REGION(w), movepart, updatepart);
        SectRgn(PORT_CLIP_REGION(MR(wmgr_port)), updatepart, updatepart);
        DiffRgn(behindpart, movepart, behindpart);
        DiffRgn(behindpart, updatepart, behindpart);
        if (!EmptyRgn(updatepart))
            PaintBehind(w, updatepart);
        PaintBehind(ROMlib_window_next_all(w), behindpart);
        CalcVisBehind(w, updatepart);
        CalcVisBehind(ROMlib_window_next_all(w), behindpart);
        CalcVisBehind(ROMlib_window_next_all(w), movepart);

        DisposeRgn(movepart);
        DisposeRgn(updatepart);
        DisposeRgn(behindpart);
    }
    if (front)
        SelectWindow((WindowPtr)w);
    SetPort(gp);
    TRAPEND();
}

P3(PUBLIC pascal trap, void, DragWindow, WindowPtr, wp, Point, p, Rect*, rp)
{
    RgnHandle rh;
    LONGINT l;
    EventRecord ev;
    int cmddown;
    Rect r;

    THEPORT_SAVE_EXCURSION(MR(wmgr_port), {
        GetOSEvent(0, &ev);
        SetClip(MR(GrayRgn));
        cmddown = ev.modifiers & CWC(cmdKey);
        if (cmddown)
            ClipAbove((WindowPeek)wp);
        rh = NewRgn();
        CopyRgn(WINDOW_STRUCT_REGION(wp), rh);
        r = *rp;
        if (CW(r.top) < 24)
            r.top = CWC(24);
        l = DragGrayRgn(rh, p, &r, &r, noConstraint, (ProcPtr)0);
        if ((uint32)l != 0x80008000)
            MoveWindow(wp, (-CW(PORT_BOUNDS(wp).left) + LoWord(l) + CW(PORT_RECT(wp).left)),
                       (-CW(PORT_BOUNDS(wp).top) + HiWord(l) + CW(PORT_RECT(wp).top)), !cmddown);

        DisposeRgn(rh);
    });
}

#define SETUP_PORT(p)       \
    do {                    \
        SetPort(p);         \
        PenPat(gray);       \
        PenMode(notPatXor); \
    } while (FALSE)

#define RESTORE_PORT(p)   \
    do {                  \
        PenPat(black);    \
        PenMode(patCopy); \
        SetPort(p);       \
    } while (FALSE)

P3(PUBLIC pascal trap, LONGINT, GrowWindow, WindowPtr, w, Point, startp, Rect*, rp)
{
    EventRecord ev;
    GrafPtr gp;
    Rect r, initial;

#if 0
    r.left   = CW (- CW (PORT_BOUNDS (w).left));
    r.top    = CW (- CW (PORT_BOUNDS (w).top));
    r.right  = CW (CW (r.left) + RECT_WIDTH (&PORT_RECT (w)));
    r.bottom = CW (CW (r.top)  + RECT_HEIGHT (&PORT_RECT (w)));
#else
    r.left = CW(CW(PORT_RECT(w).left) - CW(PORT_BOUNDS(w).left));
    r.top = CW(CW(PORT_RECT(w).top) - CW(PORT_BOUNDS(w).top));
    r.right = CW(CW(PORT_RECT(w).right) - CW(PORT_BOUNDS(w).left));
    r.bottom = CW(CW(PORT_RECT(w).bottom) - CW(PORT_BOUNDS(w).top));
#endif

    initial = r;

    gp = thePort;
    SETUP_PORT(MR((GrafPtr)WMgrPort));
    SETUP_PORT(MR(wmgr_port));
    ClipRect(&GD_BOUNDS(MR(TheGDevice)));
    ClipAbove((WindowPeek)w);
    WINDCALL((WindowPtr)w, wGrow, (intptr_t)&r);
    for (;;) {
        BOOLEAN released = GetOSEvent(mUpMask, &ev);
        /* Limits describe sizes, not mouse coordinates. Retain the offset
         between the initial click and the lower-right corner, so a fixed
         width does not grow by the grow-box inset on every vertical drag. */
        int width = RECT_WIDTH(&initial) + CW(ev.where.h) - startp.h;
        int height = RECT_HEIGHT(&initial) + CW(ev.where.v) - startp.v;
        width = MAX(CW(rp->left), MIN(width, CW(rp->right) - 1));
        height = MAX(CW(rp->top), MIN(height, CW(rp->bottom) - 1));
        int right = MIN(32767, CW(initial.left) + width);
        int bottom = MIN(32767, CW(initial.top) + height);
        if (CW(r.right) != right || CW(r.bottom) != bottom) {
            WINDCALL((WindowPtr)w, wGrow, (intptr_t)&r);
            r.right = CW(right);
            r.bottom = CW(bottom);
            WINDCALL((WindowPtr)w, wGrow, (intptr_t)&r);
        }
        /* Include the release position even if no intermediate event was
         observed during a short drag. */
        if (released)
            break;
        CALLDRAGHOOK();
    }
    WINDCALL((WindowPtr)w, wGrow, (intptr_t)&r);
    RESTORE_PORT(MR((GrafPtr)WMgrPort));
    RESTORE_PORT(gp);
    if (r.right != initial.right || r.bottom != initial.bottom)
        /*-->*/ return (((LONGINT)(CW(r.bottom) - CW(r.top)) << 16) | (unsigned short)(CW(r.right) - CW(r.left)));
    else
        return (0L);
}

/* #### speedup? bag saveold, drawnew */

P4(PUBLIC pascal trap, void, SizeWindow, WindowPtr, w, INTEGER, width, INTEGER, height, BOOLEAN, flag)
{
    if (width || height) {
        if (WINDOW_VISIBLE_X(w))
            SaveOld((WindowPeek)w);

        PORT_RECT(w).right = CW(CW(PORT_RECT(w).left) + width);
        PORT_RECT(w).bottom = CW(CW(PORT_RECT(w).top) + height);

        THEPORT_SAVE_EXCURSION(MR(wmgr_port), {
            WINDCALL(w, wCalcRgns, 0);
            if (WINDOW_VISIBLE_X(w))
                DrawNew((WindowPeek)w, flag);
        });
    }
}
