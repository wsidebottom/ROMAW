/*+===================================================================
  File:      aboutbox.c

  Summary:   Draws the classic About ROMAW dialog and handles its dismissal.

  Classes:   None.

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
#include "classic-ui/host_graphics.h"

StringPtr about_box_menu_name_pstr = (StringPtr) "\016About ROMAW...";

/* Keep the labels and values aligned even when memory totals change width. */
static void draw_memory_row(const char* label, const char* value, int left, int baseline)
{
    MoveTo(left + ROMAW_ABOUT_MARGIN, baseline);
    DrawText((Ptr)label, 0, strlen(label));
    MoveTo(left + ROMAW_ABOUT_WIDTH - ROMAW_ABOUT_MARGIN - TextWidth((Ptr)value, 0, strlen(value)), baseline);
    DrawText((Ptr)value, 0, strlen(value));
}

void ROMlib_draw_about_contents(int left, int top)
{
    TextFont(helvetica);
    TextSize(20);
    MoveTo(left + ROMAW_ABOUT_MARGIN, top + 29);
    DrawText((Ptr)ROMlib_romaw_version, 0, strlen(ROMlib_romaw_version));
    TextSize(12);
    MoveTo(left + ROMAW_ABOUT_MARGIN, top + 48);

    char value[64];
    LONGINT guest_ram;
    if (C_GestaltTablesOnly(gestaltLogicalRAMSize, &guest_ram) == noErr) {
        unsigned ram = CL(guest_ram);
        const unsigned mb = 1024 * 1024;
        snprintf(value, sizeof value, "%u.%02u MB", ram / mb, (ram % mb) * 100 / mb);
    }
    else
        snprintf(value, sizeof value, "Unavailable");
    draw_memory_row("Emulated RAM", value, left, top + 75);

    snprintf(value, sizeof value, "%lu KB / %u KB", FreeMemSys() / 1024UL, ROMlib_syszone_size / 1024U);
    draw_memory_row("System RAM free", value, left, top + 93);

    ZONE_SAVE_EXCURSION(ApplZone, {
        snprintf(value, sizeof value, "%lu KB / %u KB", FreeMem() / 1024UL, ROMlib_applzone_size / 1024U);
    });
    draw_memory_row("Application RAM free", value, left, top + 111);
}

static void draw_about_box(WindowPtr window)
{
    BeginUpdate(window);
    PenNormal();
    ForeColor(blackColor);
    BackColor(whiteColor);
    EraseRect(&window->portRect);
    ROMlib_draw_about_contents(0, 0);
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
        int left = MAX(0, ((int)vdriver_width - ROMAW_ABOUT_WIDTH) / 2);
        int top = MAX(28, ((int)vdriver_height - ROMAW_ABOUT_HEIGHT) / 3);
        SetRect(&bounds, left, top, left + ROMAW_ABOUT_WIDTH, top + ROMAW_ABOUT_HEIGHT);
        WindowPtr window = (WindowPtr)NewCWindow(NULL, &bounds, (StringPtr) "\013About ROMAW", FALSE, dBoxProc,
                                                 (CWindowPtr)-1, FALSE, 0);
        if (window) {
            THEPORT_SAVE_EXCURSION(window, {
                Rect button;
                int right = ROMAW_ABOUT_WIDTH - ROMAW_ABOUT_MARGIN;
                int bottom = ROMAW_ABOUT_HEIGHT - ROMAW_ABOUT_MARGIN;
                SetRect(&button, right - ROMAW_ABOUT_BUTTON_WIDTH, bottom - ROMAW_ABOUT_BUTTON_HEIGHT, right, bottom);
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
