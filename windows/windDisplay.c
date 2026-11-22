/*+===================================================================
  File:      windDisplay.c

  Summary:   Implements wind display support for Macintosh Window Manager operations.

  Classes:   None.

  Functions: ROMlib_firstvisible, ROMlib_refresh_grow_icons

  Origin:    Existing repository source in executor/src/windows; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986, 1989, 1990, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in WindowMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "WindowMgr.h"
#include "OSUtil.h"
#include "MemoryMgr.h"

/* get {C}PORT_... accessors */
#include "rsys/cquick.h"
#include "rsys/wind.h"
#include "rsys/systemwindow.h"
#include "rsys/layers.h"
#include "rsys/glue.h"
#include "rsys/dirtyrect.h"
#include "rsys/vdriver.h"

P2(PUBLIC pascal trap, void, SetWTitle, WindowPtr, w, StringPtr, t)
{
    if (!w)
        return;
    PtrToXHand((Ptr)t, (Handle)WINDOW_TITLE(w), (LONGINT)t[0] + 1);

    THEPORT_SAVE_EXCURSION(MR(wmgr_port), {
        WINDOW_TITLE_WIDTH_X(w) = CW(StringWidth(t));

        if (WINDOW_VISIBLE_X(w)) {
            SetClip(WINDOW_STRUCT_REGION(w));
            ClipAbove((WindowPeek)w);
            WINDCALL(w, wDraw, 0);
        }
    });
}

P2(PUBLIC pascal trap, void, GetWTitle, WindowPtr, w, StringPtr, t)
{
    if (!w)
        /*-->*/ return;
    str255assign(t, STARH(WINDOW_TITLE(w)));
}

A1(PUBLIC, WindowPeek, ROMlib_firstvisible, WindowPtr, w) /* INTERNAL */
{
    WindowPeek wp;

    for (wp = (WindowPeek)w;
         wp && (!WINDOW_VISIBLE_X(wp) || ROMlib_system_window_p((WindowPtr)wp) || wp == (WindowPeek)MR(GhostWindow));
         wp = WINDOW_NEXT_WINDOW(wp))
        ;
    return wp;
}

P0(PUBLIC pascal trap, WindowPtr, FrontWindow)
{
    WindowPtr retval;

    retval = (WindowPtr)ROMlib_firstvisible((WindowPtr)MR(WindowList));
    return retval;
}

P2(PUBLIC pascal trap, void, HiliteWindow, WindowPtr, w, BOOLEAN, flag)
{
    if (!w)
        return;
    fprintf(stderr, "trace: HiliteWindow w=%p flag=%d hilited=%d\n", (void*)w, flag, WINDOW_HILITED_X(w));
    fflush(stderr);
    THEPORT_SAVE_EXCURSION(MR(wmgr_port), {
        SetClip(WINDOW_STRUCT_REGION(w));
        ClipAbove((WindowPeek)w);
        if (flag && !WINDOW_HILITED_X(w)) {
            WINDOW_HILITED_X(w) = TRUE;
            WINDCALL(w, wDraw, 0);
        }
        else if (!flag && WINDOW_HILITED_X(w)) {
            WINDOW_HILITED_X(w) = FALSE;
            WINDCALL(w, wDraw, 0);
        }
    });
}

static void bring_to_front_in_layer(WindowPtr w)
{
    WindowPeek wp;
    RgnHandle hidden;

    if (MR(WindowList) != (WindowPeek)w)
        THEPORT_SAVE_EXCURSION(MR(wmgr_port), {
            SetClip(MR(GrayRgn));
            for (wp = MR(WindowList); wp && WINDOW_NEXT_WINDOW(wp) != (WindowPeek)w; wp = WINDOW_NEXT_WINDOW(wp))
                ;
            if (wp) {
                FIELD_ASSIGN_RAW(WINDOW_NEXT_WINDOW_X(wp), WINDOW_NEXT_WINDOW_X(w));
                FIELD_ASSIGN_RAW(WINDOW_NEXT_WINDOW_X(w), WindowList);
                HIDDEN_RAW_SET(WindowList_H, RM((WindowPeek)w));
                if (WINDOW_VISIBLE_X(w)) {
                    /* notify the palette manager that the `FrontWindow ()'
		    may have changed */
                    pm_front_window_maybe_changed_hook();

                    hidden = NewRgn();
                    CopyRgn(PORT_VIS_REGION(w), hidden);
                    OffsetRgn(hidden, -Cx(PORT_BOUNDS(w).left), -Cx(PORT_BOUNDS(w).top));
                    XorRgn(WINDOW_STRUCT_REGION(w), hidden, hidden);
                    PaintOne((WindowPeek)w, hidden);
                    CalcVisBehind((WindowPeek)w, hidden);
                    DisposeRgn(hidden);
                }
            }
        });
}

P1(PUBLIC pascal trap, void, BringToFront, WindowPtr, w)
{
    void *parent = ROMlib_layer_parent(w), *old = NULL;
    if (parent)
        old = ROMlib_layer_swap(parent);
    bring_to_front_in_layer(w);
    if (old)
        ROMlib_layer_set(old);
}

P1(PUBLIC pascal trap, void, SelectWindow, WindowPtr, w)
{
    WindowPtr cactive;

    if (ROMlib_system_window_p(w)) {
        C_BringToFront(w);
        C_HiliteWindow(w, TRUE);
        return;
    }
    cactive = FrontWindow();
    if (cactive != w) {
        HiliteWindow(cactive, FALSE);
        HIDDEN_RAW_SET(CurDeactive_H, RM(cactive));
        HIDDEN_RAW_SET(CurActivate_H, RM(w));
    }
    BringToFront(w);
    HiliteWindow(w, TRUE);
}

static void show_hide_in_layer(WindowPtr w, BOOLEAN flag)
{
    fprintf(stderr, "trace: ShowHide w=%p flag=%d visible=%d kind=%d\n", (void*)w, flag, w ? WINDOW_VISIBLE_X(w) : 0,
            w ? CW(WINDOW_KIND_X(w)) : 0);
    fflush(stderr);
    if (flag && !WINDOW_VISIBLE_X(w)) {
        WINDOW_VISIBLE_X(w) = TRUE;
        /* notify the palette manager that the `FrontWindow ()' may have
	 changed */
        pm_front_window_maybe_changed_hook();
        THEPORT_SAVE_EXCURSION(MR(wmgr_port), {
            /*
 * Energy Scheming suggests that this test is incorrect.  They change portRect
 * and then call ShowHide twice (once to hide, once to show) to see their
 * changes.  With this test in place, their code fails.
 *
 *	   if (EmptyRgn (WINDOW_STRUCT_REGION (w)))
 */
            fprintf(stderr, "trace: ShowHide wCalcRgns begin\n");
            fflush(stderr);
            WINDCALL(w, wCalcRgns, 0);
            fprintf(stderr, "trace: ShowHide wCalcRgns complete\n");
            fflush(stderr);
            fprintf(stderr, "trace: ShowHide SetClip begin\n");
            fflush(stderr);
            SetClip(WINDOW_STRUCT_REGION(w));
            fprintf(stderr, "trace: ShowHide ClipAbove begin\n");
            fflush(stderr);
            ClipAbove((WindowPeek)w);
            fprintf(stderr, "trace: ShowHide CalcVisBehind begin\n");
            fflush(stderr);
            CalcVisBehind((WindowPeek)w, PORT_CLIP_REGION(MR(wmgr_port)));
            fprintf(stderr, "trace: ShowHide wDraw begin\n");
            fflush(stderr);
            WINDCALL(w, wDraw, 0);
            fprintf(stderr, "trace: ShowHide wDraw complete\n");
            fflush(stderr);
            CopyRgn(WINDOW_CONT_REGION(w), WINDOW_UPDATE_REGION(w));
            fprintf(stderr, "trace: ShowHide CopyRgn update complete\n");
            fflush(stderr);

            RGBColor* colors = validate_colors_for_window((GrafPtr)w);
            RGBBackColor(&colors[wContentColor]);
            fprintf(stderr, "trace: ShowHide FillRgn begin\n");
            fflush(stderr);
            FillRgn(WINDOW_CONT_REGION(w), white);
            dirty_rect_update_screen();
            vdriver_flush_display();
            fprintf(stderr, "trace: ShowHide FillRgn complete\n");
            fflush(stderr);
            RGBBackColor(&ROMlib_white_rgb_color);
        });
    }
    else if (!flag && WINDOW_VISIBLE_X(w)) {
        WINDOW_VISIBLE_X(w) = FALSE;
        /* notify the palette manager that the `FrontWindow ()' may have
	 changed */
        pm_front_window_maybe_changed_hook();
        THEPORT_SAVE_EXCURSION(MR(wmgr_port), {
            SetClip(MR(GrayRgn));
            SetEmptyRgn(PORT_VIS_REGION(w));
            PaintBehind(ROMlib_window_next_all(w), WINDOW_STRUCT_REGION(w));
            CalcVisBehind(ROMlib_window_next_all(w), WINDOW_STRUCT_REGION(w));
        });
    }
}

P2(PUBLIC pascal trap, void, ShowHide, WindowPtr, w, BOOLEAN, flag)
{
    void *parent, *old = NULL;
    if (!w)
        return;
    parent = ROMlib_layer_parent(w);
    if (parent)
        old = ROMlib_layer_swap(parent);
    show_hide_in_layer(w, flag);
    if (old)
        ROMlib_layer_set(old);
}

P1(PUBLIC pascal trap, void, HideWindow, WindowPtr, w)
{
    WindowPeek nextvis;

    if (!w)
        return;
    if (WINDOW_VISIBLE_X(w)) {
        if (w == FrontWindow()) {
            nextvis = ROMlib_firstvisible((WindowPtr)WINDOW_NEXT_WINDOW(w));
            if (nextvis)
                SelectWindow((WindowPtr)nextvis);
            else
                HIDDEN_RAW_SET(CurDeactive_H, RM(w));
            WINDOW_HILITED_X(w) = FALSE;
        }
        ShowHide(w, FALSE);
    }
}

P1(PUBLIC pascal trap, void, ShowWindow, WindowPtr, w)
{
    WindowPeek t;

    fprintf(stderr, "trace: ShowWindow w=%p visible=%d front=%p\n", (void*)w, w ? WINDOW_VISIBLE_X(w) : 0,
            (void*)FrontWindow());
    fflush(stderr);
    if (!w)
        /*-->*/ return;
    if (!WINDOW_VISIBLE_X(w)) {
        TRAPBEGIN();
        ShowHide(w, TRUE);
        if (FrontWindow() == w && !WINDOW_HILITED_X(w)) {
            HiliteWindow(w, TRUE);
            HIDDEN_RAW_SET(CurActivate_H, RM(w));
            for (t = WINDOW_NEXT_WINDOW(w); t && !WINDOW_HILITED_X(t); t = WINDOW_NEXT_WINDOW(t))
                ;
            HiliteWindow((WindowPtr)t, FALSE);
            HIDDEN_RAW_SET(CurDeactive_H, (WindowPtr)RM(t));
        }
        TRAPEND();
    }
}

static void send_behind_in_layer(WindowPtr w, WindowPtr behind)
{
    HIDDEN_WindowPeek* wpp;
    WindowPeek oldfront, newfront, oldbehind;
    RgnHandle temprgn;
    Rect r;

    oldfront = (WindowPeek)FrontWindow();

    /* NOTE: the following nasty code is to make our SendBehind behave like the Mac
	 send behind, which is that it looks for a new FrontWindow with the
	 WindowList as it would be if w were totally removed from the list
   BEGINNING of nasty code */

    if (oldfront == (WindowPeek)w && (newfront = ROMlib_firstvisible((WindowPtr)WINDOW_NEXT_WINDOW(oldfront))))
        SelectWindow((WindowPtr)newfront);

    /* END of nasty code */

    if ((!WINDOW_NEXT_WINDOW(w) && !behind) || WINDOW_NEXT_WINDOW(w) == (WindowPeek)w)
        /*-->*/ return;
    for (wpp = (HIDDEN_WindowPeek*)&WindowList_H; HIDDEN_RAW((*wpp)) && STARH(wpp) != (WindowPeek)w;
         wpp = (HIDDEN_WindowPeek*)&PP_RAW(WINDOW_NEXT_WINDOW_X(STARH(wpp))))
        ;
    if (!HIDDEN_RAW((*wpp)))
        /*-->*/ return;
    HIDDEN_RAW_SET((*wpp), PP_RAW(WINDOW_NEXT_WINDOW_X(w)));
    oldbehind = STARH(wpp);
    if (behind) {
        PP_RAW(WINDOW_NEXT_WINDOW_X(w)) = PP_RAW(WINDOW_NEXT_WINDOW_X(behind));
        FIELD_ASSIGN_RAW(WINDOW_NEXT_WINDOW_X(behind), (WindowPeek)RM(w));
    }
    else {
#define SEND_BEHIND
#if defined(SEND_BEHIND)
        if (!HIDDEN_RAW((*wpp))) /* what if 'w' is the only window? */
            wpp = (HIDDEN_WindowPeek*)&WindowList_H;
        for (; PP_RAW(WINDOW_NEXT_WINDOW_X(STARH(wpp)));
             wpp = (HIDDEN_WindowPeek*)&PP_RAW(WINDOW_NEXT_WINDOW_X(STARH(wpp))))
            ;
        if (STARH(wpp) != (WindowPeek)w)
            FIELD_ASSIGN_RAW(WINDOW_NEXT_WINDOW_X(STARH(wpp)), (WindowPeek)RM(w));
#endif /* SEND_BEHIND */
        PP_RAW(WINDOW_NEXT_WINDOW_X(w)) = 0;
    }
    CalcVis((WindowPeek)w);
    temprgn = NewRgn();
    CopyRgn(PORT_VIS_REGION(w), temprgn);
    OffsetRgn(temprgn, -Cx(PORT_BOUNDS(w).left), -Cx(PORT_BOUNDS(w).top));
    XorRgn(WINDOW_STRUCT_REGION(w), temprgn, temprgn);
    CalcVisBehind(oldbehind, temprgn);
    PaintBehind(oldbehind, temprgn);
    DisposeRgn(temprgn);
#if 0
    newfront = (WindowPeek) FrontWindow ();
    if (oldfront != newfront) {
	HIDDEN_RAW_SET (CurDeactive_H, CL((WindowPtr) oldfront));
	HiliteWindow((WindowPtr) oldfront, FALSE);
	HIDDEN_RAW_SET (CurActivate_H, CL((WindowPtr) newfront));
	HiliteWindow((WindowPtr) newfront, TRUE);
    }
#else /* 0 */
    if (oldfront == (WindowPeek)w && MR(WindowList) != (WindowPeek)w) {
        THEPORT_SAVE_EXCURSION(w, {
            r = HxX(PORT_VIS_REGION(w), rgnBBox);
            EraseRect(&r); /* ick! The bad Mac made me do it */
            InvalRect(&r);
        });
    }
#endif /* 0 */
}

static boolean_t grow_icons_pending, refreshing_grow_icons;

P1(PUBLIC pascal trap, void, DrawGrowIcon, WindowPtr, w)
{
    if (w && !ROMlib_emptyvis && WINDOW_VISIBLE(w)) {
        if (!refreshing_grow_icons) {
            AuxWinHandle aux = HIDDEN_NATIVE(*lookup_aux_win(w));
            if (aux) {
                HxX(aux, awFlags) |= CLC(ROMlib_pending_grow_icon);
                grow_icons_pending = TRUE;
            }
        }
        THEPORT_SAVE_EXCURSION(MR(wmgr_port), {
            SetClip(PORT_CLIP_REGION(w));
            OffsetRgn(WINDOW_STRUCT_REGION(w), CW(PORT_BOUNDS(w).left), CW(PORT_BOUNDS(w).top));
            SectRgn(PORT_CLIP_REGION(thePort), WINDOW_STRUCT_REGION(w), PORT_CLIP_REGION(thePort));
            OffsetRgn(WINDOW_STRUCT_REGION(w), -CW(PORT_BOUNDS(w).left), -CW(PORT_BOUNDS(w).top));
            OffsetRgn(PORT_CLIP_REGION(thePort), -CW(PORT_BOUNDS(w).left), -CW(PORT_BOUNDS(w).top));
            ClipAbove((WindowPeek)w);
            WINDCALL(w, wDrawGIcon, 0);
        });
    }
}

void ROMlib_refresh_grow_icons(void)
{
    if (!grow_icons_pending || refreshing_grow_icons)
        return;
    grow_icons_pending = FALSE;
    refreshing_grow_icons = TRUE;
    /* Some clients draw their size box before repainting the background.
       Finish each explicit request once, after application painting returns
       to the event loop. Do not schedule another redraw from this pass. */
    for (WindowPeek w = ROMlib_window_first_all(); w; w = ROMlib_window_next_all(w)) {
        AuxWinHandle aux = HIDDEN_NATIVE(*lookup_aux_win((WindowPtr)w));
        if (aux && (Hx(aux, awFlags) & ROMlib_pending_grow_icon)) {
            HxX(aux, awFlags) &= ~CLC(ROMlib_pending_grow_icon);
            /* The application may have clipped its original request to the
               size box. A later full-port clip must not add scrollbar dividers. */
            RgnHandle corner_clip = NewRgn();
            if (corner_clip) {
                RgnHandle saved_clip = PORT_CLIP_REGION(w);
                Rect corner = PORT_RECT(w);
                corner.left = CW(CW(corner.right) - 15);
                corner.top = CW(CW(corner.bottom) - 15);
                RectRgn(corner_clip, &corner);
                SectRgn(corner_clip, saved_clip, corner_clip);
                FIELD_ASSIGN_RAW(PORT_CLIP_REGION_X(w), RM(corner_clip));
                C_DrawGrowIcon((WindowPtr)w);
                FIELD_ASSIGN_RAW(PORT_CLIP_REGION_X(w), RM(saved_clip));
                DisposeRgn(corner_clip);
            }
        }
    }
    refreshing_grow_icons = FALSE;
}

P2(PUBLIC pascal trap, void, SendBehind, WindowPtr, w, WindowPtr, behind)
{
    void *parent = ROMlib_layer_parent(w), *old = NULL;
    if (parent)
        old = ROMlib_layer_swap(parent);
    if (!behind || !parent || ROMlib_layer_parent(behind) == parent)
        send_behind_in_layer(w, behind);
    if (old)
        ROMlib_layer_set(old);
}
