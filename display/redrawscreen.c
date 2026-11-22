/*+===================================================================
  File:      redrawscreen.c

  Summary:   Implements redrawscreen support for display refresh and pixel conversion.

  Classes:   None.

  Functions: redraw_screen

  Origin:    Existing repository source in executor/src/display; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"
#include "SysErr.h"
#include "WindowMgr.h"
#include "MenuMgr.h"
#include "rsys/redrawscreen.h"
#include "rsys/cquick.h"
#include "rsys/vdriver.h"

void redraw_screen(void)
{
    THEGDEVICE_SAVE_EXCURSION(MR(MainDevice), {
        vdriver_set_colors(0, 1 << vdriver_bpp, CTAB_TABLE(PIXMAP_TABLE(GD_PMAP(MR(MainDevice)))));

        if (WWExist == EXIST_YES) {
            WindowPeek frontp;
            frontp = (WindowPeek)FrontWindow();
            if (frontp) {
                RgnHandle screen_rgn;
                Rect b;

                b = PIXMAP_BOUNDS(GD_PMAP(MR(MainDevice)));
                screen_rgn = NewRgn();
                RectRgn(screen_rgn, &b);
                PaintBehind(frontp, screen_rgn);
                DisposeRgn(screen_rgn);
            }
        }

        DrawMenuBar();
    });
}
