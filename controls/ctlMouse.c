/*+===================================================================
  File:      ctlMouse.c

  Summary:   Implements ctl mouse support for Macintosh Control Manager operations.

  Classes:   None.

  Functions: ROMlib_control_visible_at, find_control_helper

  Origin:    Existing repository source in executor/src/controls; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986-1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in ControlMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"

#include "WindowMgr.h"
#include "ControlMgr.h"
#include "EventMgr.h"
#include "ToolboxUtil.h"
#include "ToolboxEvent.h"
#include "OSEvent.h"
#include "ListMgr.h"

#include "rsys/ctl.h"
#include "rsys/wind.h"
#include "rsys/pstuff.h"
#include "rsys/stdfile.h"
#include "rsys/hook.h"
#include "rsys/osevent.h"

/*
 * Loser suggests that controls are tested in the opposite order that
 * they're on the linked list.  That would make sense, since it would
 * insure that when drawn under normal circumstances, the control that
 * is visible and clicked is the one that will be FindControl will
 * return.  Recursion is you friend.
 */

INTEGER
find_control_helper(Point p, ControlHandle c, HIDDEN_ControlHandle* cp)
{
    INTEGER retval;
    ControlHandle next;

    if (!c)
        retval = 0;
    else {
        next = CTL_NEXT_CONTROL(c);
        if (!(retval = find_control_helper(p, next, cp))) {
            if (CTL_VIS(c) && CTL_HILITE(c) != 255) {
                retval = TestControl(c, p);
                if (retval)
                    HIDDEN_RAW_SET(*(cp), RM(c));
            }
        }
    }
    return retval;
}

boolean_t ROMlib_control_visible_at(ControlHandle control, Point where)
{
    if (!control || !STARH(control) || !CTL_VIS(control) ||
        ROMlib_native_control_kind(control) == ROMlib_root_control_proc || !PtInRect(where, &CTL_RECT(control)))
        return FALSE;
    AuxCtlHandle aux = HIDDEN_NATIVE(*lookup_aux_ctl(control));
    while (aux && ROMlib_aux_control_extra(aux)->parent) {
        ControlHandle parent = MR(ROMlib_aux_control_extra(aux)->parent);
        aux = HIDDEN_NATIVE(*lookup_aux_ctl(parent));
        if (aux && Hx(aux, acReserved) == ROMlib_root_control_proc)
            break;
        if (!CTL_VIS(parent) || !PtInRect(where, &CTL_RECT(parent)))
            return FALSE;
    }
    return TRUE;
}

P3(PUBLIC pascal trap, INTEGER, FindControl, Point, p, /* IMI-323 */
   WindowPtr, w, HIDDEN_ControlHandle*, cp)
{
    INTEGER retval;

    HIDDEN_ControlHandle root;
    retval = 0;
    if (w && ROMlib_get_root_control(w, &root) == noErr) {
        /* Embedded foreground controls paint after their background panes. */
        for (ControlHandle c = WINDOW_CONTROL_LIST(w); c; c = CTL_NEXT_CONTROL(c))
            if (CTL_HILITE(c) != 255 && ROMlib_control_visible_at(c, p) && (retval = TestControl(c, p))) {
                HIDDEN_RAW_SET(*cp, RM(c));
                break;
            }
    }
    else if (w)
        retval = find_control_helper(p, WINDOW_CONTROL_LIST(w), cp);
    if (!retval)
        HIDDEN_RAW_SET(*(cp), 0);
    return retval;
}

typedef pascal void (*actionp)(ControlHandle c, INTEGER part);

A3(PRIVATE inline, void, CALLACTION, ControlHandle, ch, INTEGER, inpart, ProcPtr, a)
{
    ROMlib_hook(ctl_cdefnumber);
    HOOKSAVEREGS();
    if (a == (ProcPtr)P_ROMlib_mytrack)
        C_ROMlib_mytrack(ch, inpart);
    else if (a == (ProcPtr)P_ROMlib_stdftrack)
        C_ROMlib_stdftrack(ch, inpart);
    else
        CToPascalCall(a, CTOP_ROMlib_mytrack, ch, inpart);
    HOOKRESTOREREGS();
}

static void track_live_scrollbar(ControlHandle control, Point start, ProcPtr action)
{
    thumbstr thumb;
    EventRecord event;
    BOOLEAN released;

    thumb._tlimit.left = CW(start.h);
    thumb._tlimit.top = CW(start.v);
    CTLCALL(control, thumbCntl, (intptr_t)&thumb);
    boolean_t vertical = CW(thumb._taxis) == vAxisOnly;
    int first = vertical ? CW(thumb._tlimit.top) : CW(thumb._tlimit.left);
    int last = (vertical ? CW(thumb._tlimit.bottom) : CW(thumb._tlimit.right)) - 1;
    int previous = vertical ? start.v : start.h;

    do {
        released = GetOSEvent(mUpMask, &event);
        GlobalToLocal(&event.where);
        Point where = {CW(event.where.v), CW(event.where.h)};
        int position = vertical ? where.v : where.h;
        if (last > first && position != previous && PtInRect(where, &thumb._tslop)) {
            int travel = last - first;
            int offset = MAX(0, MIN(travel, position - first));
            AuxCtlHandle aux = HIDDEN_NATIVE(*lookup_aux_ctl(control));
            int32 limits[4];
            ROMlib_control_read_range(control, aux, limits);
            const int32 minimum = limits[control_minimum];
            const int64 range = (int64)limits[control_maximum] - minimum;
            const int32 value = minimum + ((int64)offset * range + travel / 2) / travel;
            if (value != limits[control_value]) {
                ROMlib_control_store_value(control, aux, value);
                Draw1Control(control);
                if (action)
                    CALLACTION(control, inThumb, action);
            }
            previous = position;
        }
    } while (!released && !(event.modifiers & CWC(btnState)));
}

P3(PUBLIC pascal trap, INTEGER, TrackControl, /* IMI-323 */
   ControlHandle, c, Point, p, ProcPtr, a)
{
    INTEGER partstart, inpart;
    EventRecord ev;
    thumbstr thumb;
    RgnHandle rh;
    LONGINT l;
    Point whereunswapped;

    int retval;

    CTL_CALL_EXCURSION(c, {
        partstart = inpart = TestControl(c, p);

        /* Super-Dice-It hack: It appears that Super Dice It 1.1
	  calls TrackControl with p in Global coordinates.  Without
	  this mod, we get hosed */
        if (!partstart) {
            GetMouse(&p);
            p.h = CW(p.h);
            p.v = CW(p.v);
            partstart = inpart = TestControl(c, p);
        }

        if (CTL_ACTION_AS_LONG(c) == -1L) {
            /* if we don't draw before/after, then jim's demo cdefs
	      don't update pop up menu bars */
            CTLCALL(c, drawCntl, 0);
            /* this is not how IMI says to do it, but it makes
	      Microsoft Word work. */
            CTLCALL(c, autoTrack, inpart);
            CTLCALL(c, drawCntl, 0);

            /* NOTE 1: force a return of inpart */
            partstart = inpart;
            goto done;
        }

        if (a == (ProcPtr)-1L)
            a = CTL_ACTION(c);

        int kind = ROMlib_native_control_kind(c);
        if (partstart == inThumb && kind == ROMlib_live_scroll_proc) {
            track_live_scrollbar(c, p, a);
            goto done;
        }
        if (partstart == inThumb && (kind & ~15) == ROMlib_slider_proc) {
            Rect bounds = CTL_RECT(c);
            int width = CW(bounds.right) - CW(bounds.left);
            int height = CW(bounds.bottom) - CW(bounds.top);
            boolean_t vertical = height > width;
            int travel = (vertical ? height : width) - ROMlib_slider_thumb_length;
            do {
                int offset = vertical ? p.v - CW(bounds.top) : p.h - CW(bounds.left);
                AuxCtlHandle aux = HIDDEN_NATIVE(*lookup_aux_ctl(c));
                int32 limits[4];
                ROMlib_control_read_range(c, aux, limits);
                const int32 minimum = limits[control_minimum];
                const int64 range = (int64)limits[control_maximum] - minimum;
                int position = offset - ROMlib_slider_thumb_length / 2;
                if (position < 0)
                    position = 0;
                if (position > travel)
                    position = travel;
                if (vertical != !!(kind & 4))
                    position = travel - position;
                const int32 value = travel > 0 ? minimum + range * position / travel : minimum;
                if (value != limits[control_value]) {
                    ROMlib_control_store_value(c, aux, value);
                    Draw1Control(c);
                    if (a && (kind & 1))
                        CALLACTION(c, inThumb, a);
                }
                if (!StillDown())
                    break;
                GetMouse(&p);
                p.v = CW(p.v);
                p.h = CW(p.h);
            } while (TRUE);
            GetOSEvent(mUpMask, &ev);
            goto done;
        }

        if (partstart > 128) {
            /* indicator */
            /* The code used to & the result of CTLCALL with 0xf000
	      for no apparent reason.  Taking it out fixed a bug in
	      Quicken. */
            if (!CTLCALL(c, dragCntl, partstart)) {
                thumb._tlimit.left = CW(p.h);
                thumb._tlimit.top = CW(p.v);
                CTLCALL(c, thumbCntl, (intptr_t)&thumb);
                rh = NewRgn();

                CTLCALL(c, calcThumbRgn, (intptr_t)rh);

                PATASSIGN(DragPattern, ltGray);
                l = DragTheRgn(rh, p, &thumb._tlimit, &thumb._tslop, CW(thumb._taxis), a);
                if ((uint32)l != 0x80008000) {
                    CTLCALL(c, posCntl, l);
                    inpart = partstart;
                }
                DisposeRgn(rh);
            }
        }
        else {
            /* not an indicator */
            HxX(c, contrlHilite) = partstart;
            CTLCALL(c, drawCntl, partstart);
            /* Wheel clicks are already released. Deliver one scroll action,
             rather than skipping the callback or depending on polling speed. */
            if (ROMlib_take_wheel_click(p) && a && inpart)
                CALLACTION(c, inpart, a);
            /* CALLACTION can remove mouse up events which is why the
	      following line is not a GetOSEvent call. */
            while (!OSEventAvail(mUpMask, &ev) && StillDown()) {
                GlobalToLocal(&ev.where);
                whereunswapped.h = CW(ev.where.h);
                whereunswapped.v = CW(ev.where.v);
                inpart = TestControl(c, whereunswapped);
                if (inpart && inpart != partstart)
                    inpart = 0;
                if (inpart != U(HxX(c, contrlHilite))) {
                    HxX(c, contrlHilite) = inpart;
                    CTLCALL(c, drawCntl, partstart);
                }
                if (a && inpart)
                    CALLACTION(c, inpart, a);
            }
            GetOSEvent(mUpMask, &ev);
            GlobalToLocal(&ev.where);
            whereunswapped.h = CW(ev.where.h);
            whereunswapped.v = CW(ev.where.v);
            if (HxX(c, contrlHilite)) {
                HxX(c, contrlHilite) = 0;
                CTLCALL(c, drawCntl, partstart);
            }
            inpart = TestControl(c, whereunswapped);
        }
    done:
        if (partstart && inpart == partstart && (ROMlib_native_control_kind(c) & ~15) == ROMlib_tabs_proc)
            SetCtlValue(c, partstart);
        if (partstart == inButton && inpart == partstart && ROMlib_native_control_kind(c) == ROMlib_image_well_proc + 1)
            SetCtlValue(c, 1);
        if (partstart == inButton && inpart == partstart &&
            (ROMlib_native_control_kind(c) & ~15) == ROMlib_bevel_button_proc) {
            if (CTL_MIN(c) & 0x0100)
                SetCtlValue(c, !CTL_VALUE(c));
            else if (CTL_MIN(c) & 0x0200)
                SetCtlValue(c, 1);
        }
    });

    retval = (partstart == inpart ? inpart : 0); /* DON'T CHANGE THIS
						  line w/o looking to Note 1
						  above */
    return retval;
}

P2(PUBLIC pascal trap, INTEGER, TestControl, /* IMI-325 */
   ControlHandle, c, Point, p)
{
    int16 retval;

    CTL_CALL_EXCURSION(c, {
        if (Hx(c, contrlVis) && U(Hx(c, contrlHilite)) != 255)
            retval = CTLCALL(c, testCntl, ((LONGINT)p.v << 16) | (unsigned short)p.h);
        else
            retval = 0;
    });
    return retval;
}
