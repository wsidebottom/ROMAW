/*+===================================================================
  File:      desk.c

  Summary:   Implements desk support for ROMAW application appearance and help.

  Classes:   None.

  Functions: No externally linked function definitions.

  Origin:    Existing repository source in executor/src/ui; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986-1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in DeskMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "rsys/desk.h"
#include "rsys/systemwindow.h"
#include "QuickDraw.h"
#include "CQuickDraw.h"
#include "EventMgr.h"
#include "WindowMgr.h"
#include "DeviceMgr.h"
#include "DeskMgr.h"
#include "MenuMgr.h"
#include "QuickDraw.h"
#include "OSEvent.h"
#include "ToolboxEvent.h"
#include "OSUtil.h"

#include "rsys/cquick.h"
#include "rsys/wind.h"
#include "rsys/hook.h"
#include "rsys/aboutbox.h"

static void accessory_event(INTEGER refnum, EventRecord* event)
{
    /* Control copies all 22 parameter bytes, even for the one-pointer accEvent. */
    LONGINT parameters[6] = {(LONGINT)RM(event)};
    Control(refnum, accEvent, (Ptr)parameters);
}

P1(PUBLIC pascal trap, INTEGER, OpenDeskAcc, Str255, acc) /* IMI-440 */
{
    INTEGER retval;
    DCtlHandle dctlh;
    WindowPtr wp;

    if (EqualString(acc, about_box_menu_name_pstr, TRUE, TRUE)) {
        do_about_box();
        retval = 0;
        goto done;
    }

    if (ROMlib_open_desk_file(acc, &retval))
        goto done;

    if (OpenDriver(acc, &retval) == noErr) {
        retval = CW(retval);
        dctlh = GetDCtlEntry(retval);
        if (dctlh) {
            wp = HxP(dctlh, dCtlWindow);
            if (wp) {
                ShowWindow(wp);
                SelectWindow(wp);
            }
        }
    }

done:

    SEvtEnb = TRUE;
    return retval;
}

P1(PUBLIC pascal trap, void, CloseDeskAcc, INTEGER, rn)
{
    CloseDriver(rn);
}

P2(PUBLIC pascal trap, void, SystemClick, EventRecord*, evp, WindowPtr, wp)
{
    Point p;
    LONGINT pointaslong, val;
    Rect bounds;

    if (wp) {
        p.h = CW(evp->where.h);
        p.v = CW(evp->where.v);
        if (PtInRgn(p, WINDOW_STRUCT_REGION(wp))) {
            pointaslong = ((LONGINT)p.v << 16) | (unsigned short)p.h;
            val = WINDCALL((WindowPtr)wp, wHit, pointaslong);
            switch (val) {
            case wInContent:
                if (WINDOW_HILITED_X(wp)) {
                    accessory_event(WINDOW_KIND(wp), evp);
                }
                else
                    SelectWindow(wp);
                break;
            case wInDrag:
                bounds.top = CW(CW(MBarHeight) + 4);
                bounds.left = CW(CW(GD_BOUNDS(MR(TheGDevice)).left) + 4);
                bounds.bottom = CW(CW(GD_BOUNDS(MR(TheGDevice)).bottom) - 4);
                bounds.right = CW(CW(GD_BOUNDS(MR(TheGDevice)).right) - 4);
                DragWindow(wp, p, &bounds);
                break;
            case wInGoAway:
                if (TrackGoAway(wp, p))
                    CloseDeskAcc(WINDOW_KIND(wp));
                break;
            }
        }
        else {
            if (DeskHook) {
                ROMlib_hook(desk_deskhooknumber);
                EM_D0 = -1;
                EM_A0 = (LONGINT)(long)US_TO_SYN68K(evp);
                CALL_EMULATOR((syn68k_addr_t)(long)CL((long)DeskHook));
            }
        }
    }
}

P1(PUBLIC pascal trap, BOOLEAN, SystemEdit, INTEGER, editcmd)
{
    WindowPeek wp;
    BOOLEAN retval;

    wp = (WindowPeek)FrontWindow();
    if (!wp)
        retval = FALSE;
    else if ((retval = WINDOW_KIND(wp) < 0))
        Control(WINDOW_KIND(wp), editcmd + accUndo, (Ptr)0);
    return retval;
}

#define rntodctlh(rn) (STARH(&MR(UTableBase)[-((rn) + 1)]))
#define itorn(i) ((-i) - 1)

P0(PUBLIC pascal trap, void, SystemTask)
{
    DCtlHandle dctlh;
    INTEGER i;

    for (i = 0; i < CW(UnitNtryCnt); ++i) {
        dctlh = STARH(&MR(UTableBase)[i]);
        if (dctlh && (HxX(dctlh, dCtlFlags) & CWC(NEEDTIMEBIT)) && TickCount() >= Hx(dctlh, dCtlCurTicks)) {
            Control(itorn(i), accRun, (Ptr)0);
            HxX(dctlh, dCtlCurTicks) = CL(Hx(dctlh, dCtlCurTicks) + Hx(dctlh, dCtlDelay));
        }
    }
}

P1(PUBLIC pascal trap, BOOLEAN, SystemEvent, EventRecord*, evp)
{
    BOOLEAN retval;
    WindowPeek wp;
    INTEGER rn;
    DCtlHandle dctlh;

    /* A standalone accessory has no desktop Edit menu. Handle its standard
     shortcuts before SystemEvent consumes the key for the RAM driver. */
    if (ROMlib_desk_host_active && (evp->what == CWC(keyDown) || evp->what == CWC(autoKey)) &&
        (CW(evp->modifiers) & cmdKey)) {
        int command = -1;
        switch (CL(evp->message) & 255) {
        case 'w':
        case 'q':
            ROMlib_desk_host_close = TRUE;
            return TRUE;
        case 'z':
            command = undoCmd;
            break;
        case 'x':
            command = cutCmd;
            break;
        case 'c':
            command = copyCmd;
            break;
        case 'v':
            command = pasteCmd;
            break;
        }
        if (command >= 0 && SystemEdit(command))
            return TRUE;
    }

    if (ROMlib_system_window_event(evp))
        return TRUE;
    if (SEvtEnb) {
        wp = 0;
        switch (evp->what) {
        default:
        case CWC(nullEvent):
        case CWC(mouseDown):
        case CWC(networkEvt):
        case CWC(driverEvt):
        case CWC(app1Evt):
        case CWC(app2Evt):
        case CWC(app3Evt):
        case CWC(app4Evt):
            break;
        case CWC(mouseUp):
        case CWC(keyDown):
        case CWC(keyUp):
        case CWC(autoKey):
            wp = (WindowPeek)FrontWindow();
            break;
        case CWC(updateEvt):
        case CWC(activateEvt):
            wp = (WindowPeek)MR(evp->message);
            break;
        case CWC(diskEvt):
            /* NOTE:  I think the code around toolevent.c:277 should
		      really be here.  I'm not going to get all excited
		      about it right now though. */
            break;
        }
        if (wp) {
            rn = WINDOW_KIND(wp);
            if ((retval = rn < 0)) {
                dctlh = rntodctlh(rn);
                if (Hx(dctlh, dCtlEMask) & (1 << CW(evp->what))) {
                    accessory_event(rn, evp);
                }
            }
        }
        else
            retval = FALSE;
    }
    else
        retval = FALSE;
    return retval;
}

P1(PUBLIC pascal trap, void, SystemMenu, LONGINT, menu)
{
    INTEGER i;
    DCtlHandle dctlh;

    for (i = 0; i < CW(UnitNtryCnt); ++i) {
        dctlh = STARH(&MR(UTableBase)[i]);
        if (HxX(dctlh, dCtlMenu) == MBarEnable) {
            LONGINT parameters[6] = {CL(menu)};
            Control(itorn(i), accMenu, (Ptr)parameters);
            /*-->*/ break;
        }
    }
}
