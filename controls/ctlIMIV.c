/*+===================================================================
  File:      ctlIMIV.c

  Summary:   Implements ctl imiv support for Macintosh Control Manager operations.

  Classes:   None.

  Functions: No externally linked function definitions.

  Origin:    Existing repository source in executor/src/controls; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in ControlMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"

#include "ControlMgr.h"

#include "rsys/cquick.h"
#include "rsys/ctl.h"
#include "rsys/wind.h"

P1(PUBLIC pascal trap, void, Draw1Control, ControlHandle, c) /* IMIV-53 */
{
    if (c && STARH(c) && CTL_VIS_X(c)) {
        CTL_CALL_EXCURSION(c, { CTLCALL(c, drawCntl, 0); });
    }
}

P2(PUBLIC pascal trap, void, UpdtControl, WindowPtr, wp, /* IMIV-53 */
   RgnHandle, rh)
{
    ControlHandle c;

    if (!ROMlib_emptyvis) {
        AuxWinHandle aux = HIDDEN_NATIVE(*lookup_aux_win(wp));
        boolean_t background_first = aux && (Hx(aux, awFlags) & ROMlib_compact_window_frame);
        for (c = WINDOW_CONTROL_LIST(wp); c && !background_first; c = CTL_NEXT_CONTROL(c))
            background_first = (ROMlib_native_control_kind(c) & ~15) == ROMlib_user_pane_proc;
        if (background_first) {
            /* Appearance windows create background panes before the controls
             drawn over them. The window list is newest first. */
            size_t count = 0;
            for (c = WINDOW_CONTROL_LIST(wp); c; c = CTL_NEXT_CONTROL(c))
                ++count;
            ControlHandle* controls = count ? malloc(count * sizeof *controls) : NULL;
            if (controls) {
                GrafPtr owner = (GrafPtr)wp;
                RgnHandle saved_clip = PORT_CLIP_REGION(owner);
                RgnHandle update_clip = NewRgn();
                if (!update_clip) {
                    free(controls);
                    return;
                }
                size_t index = 0;
                for (c = WINDOW_CONTROL_LIST(wp); c; c = CTL_NEXT_CONTROL(c))
                    controls[index++] = c;
                while (index) {
                    c = controls[--index];
                    if (RectInRgn(&CTL_RECT(c), rh)) {
                        /* Pane callbacks may leave a clip for their own view.
                         Each control still updates in the supplied region. */
                        CopyRgn(rh, update_clip);
                        FIELD_ASSIGN_RAW(PORT_CLIP_REGION_X(owner), RM(update_clip));
                        Draw1Control(c);
                    }
                }
                FIELD_ASSIGN_RAW(PORT_CLIP_REGION_X(owner), RM(saved_clip));
                DisposeRgn(update_clip);
                free(controls);
                return;
            }
        }
        for (c = WINDOW_CONTROL_LIST(wp); c; c = CTL_NEXT_CONTROL(c))
            if (RectInRgn(&CTL_RECT(c), rh))
                Draw1Control(c);
    }
}
