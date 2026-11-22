/*+===================================================================
  File:      aboutbox.c

  Summary:   Draws the classic About ROMAW dialog and handles its dismissal.

  Functions: do_about_box

  Origin:    Existing repository source in executor/src/ui; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1996 - 2000 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"

#include "rsys/aboutbox.h"
#include "rsys/cquick.h"
#include "rsys/gestalt.h"
#include "rsys/mman.h"
#include "rsys/osevent.h"
#include "rsys/vdriver.h"
#include "rsys/version.h"
#include "CQuickDraw.h"
#include "ControlMgr.h"
#include "FontMgr.h"
#include "Gestalt.h"
#include "ToolboxEvent.h"

enum { ABOUT_WIDTH = 354, ABOUT_HEIGHT = 164, ABOUT_MARGIN = 16 };

StringPtr about_box_menu_name_pstr = (StringPtr) "\016About ROMAW...";

/* Keep the labels and values aligned even when memory totals change width. */
static void draw_memory_row(const char* label, const char* value, int baseline)
{
    MoveTo(ABOUT_MARGIN, baseline);
    DrawText((Ptr)label, 0, strlen(label));
    MoveTo(ABOUT_WIDTH - ABOUT_MARGIN - TextWidth((Ptr)value, 0, strlen(value)), baseline);
    DrawText((Ptr)value, 0, strlen(value));
}

static void draw_about_box(WindowPtr window)
{
    BeginUpdate(window);
    PenNormal();
    ForeColor(blackColor);
    BackColor(whiteColor);
    EraseRect(&window->portRect);
    TextFont(helvetica);
    TextSize(20);
    MoveTo(ABOUT_MARGIN, 29);
    DrawText((Ptr)ROMlib_romaw_version, 0, strlen(ROMlib_romaw_version));
    TextSize(12);
    MoveTo(ABOUT_MARGIN, 48);
    static const char copyright[] = "Copyright \251 ARDI 1986-2006. All rights reserved.";
    DrawText((Ptr)copyright, 0, sizeof copyright - 1);

    char value[64];
    LONGINT guest_ram;
    if (C_GestaltTablesOnly(gestaltLogicalRAMSize, &guest_ram) == noErr) {
        unsigned ram = CL(guest_ram);
        const unsigned mb = 1024 * 1024;
        snprintf(value, sizeof value, "%u.%02u MB", ram / mb, (ram % mb) * 100 / mb);
    }
    else
        snprintf(value, sizeof value, "Unavailable");
    draw_memory_row("Emulated RAM", value, 75);

    snprintf(value, sizeof value, "%lu KB / %u KB", FreeMemSys() / 1024UL, ROMlib_syszone_size / 1024U);
    draw_memory_row("System RAM free", value, 93);

    ZONE_SAVE_EXCURSION(ApplZone, {
        snprintf(value, sizeof value, "%lu KB / %u KB", FreeMem() / 1024UL, ROMlib_applzone_size / 1024U);
    });
    draw_memory_row("Application RAM free", value, 111);
    DrawControls(window);
    EndUpdate(window);
}

static void run_about_box(WindowPtr window, ControlHandle close)
{
    EventRecord event;
    for (;;) {
        /* Yield while the modal dialog is idle. */
        WaitNextEvent(mDownMask | keyDownMask | updateMask, &event, 1, NULL);
        switch (CW(event.what)) {
        case updateEvt:
            if (CL(event.message) == US_TO_SYN68K(window))
                draw_about_box(window);
            break;

        case keyDown: {
            unsigned key = CL(event.message) & charCodeMask;
            if (key == '\r' || key == NUMPAD_ENTER || key == 0x1B ||
                ((CW(event.modifiers) & cmdKey) && (key == 'w' || key == 'W')))
                return;
            break;
        }

        case mouseDown: {
            Point where = {CW(event.where.v), CW(event.where.h)};
            HIDDEN_WindowPtr hit;
            if (FindWindow(where, &hit) == inContent && MR(HIDDEN_RAW(hit)) == window) {
                Point local = event.where;
                GlobalToLocal(&local);
                local = SWAP_POINT(local);
                if (TestControl(close, local) == inButton && TrackControl(close, local, (ProcPtr)-1) == inButton)
                    return;
            }
            break;
        }
        }
    }
}

void do_about_box(void)
{
    static boolean_t busy;
    if (busy)
        return;

    busy = TRUE;
    ZONE_SAVE_EXCURSION(SysZone, {
        Rect bounds;
        int left = MAX(0, ((int)vdriver_width - ABOUT_WIDTH) / 2);
        int top = MAX(28, ((int)vdriver_height - ABOUT_HEIGHT) / 3);
        SetRect(&bounds, left, top, left + ABOUT_WIDTH, top + ABOUT_HEIGHT);
        WindowPtr window = (WindowPtr)NewCWindow(NULL, &bounds, (StringPtr) "\013About ROMAW", FALSE, dBoxProc,
                                                 (CWindowPtr)-1, FALSE, 0);
        if (window) {
            THEPORT_SAVE_EXCURSION(window, {
                Rect button;
                SetRect(&button, ABOUT_WIDTH - ABOUT_MARGIN - 86, ABOUT_HEIGHT - 38, ABOUT_WIDTH - ABOUT_MARGIN,
                        ABOUT_HEIGHT - ABOUT_MARGIN);
                ControlHandle close =
                    NewControl(window, &button, (StringPtr) "\005Close", TRUE, 0, 0, 1, pushButProc, 0);
                if (close) {
                    C_ShowWindow(window);
                    run_about_box(window, close);
                }
            });
            C_DisposeWindow(window);
        }
    });
    busy = FALSE;
}
