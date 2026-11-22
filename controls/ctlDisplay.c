/*+===================================================================
  File:      ctlDisplay.c

  Summary:   Implements ctl display support for Macintosh Control Manager operations.

  Classes:   None.

  Functions: ROMlib_dialog_edit_control, ROMlib_dialog_edit_control_item, ROMlib_get_dialog_control

  Origin:    Existing repository source in executor/src/controls; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986, 1988, 1989, 1990, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in ControlMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "WindowMgr.h"
#include "ControlMgr.h"
#include "DialogMgr.h"

#include "rsys/ctl.h"
#include "rsys/wind.h"

OSErr ROMlib_get_dialog_control(WindowPtr window, INTEGER item, HIDDEN_ControlHandle* control)
{
    if (!control)
        return paramErr;
    HIDDEN_RAW_SET(*control, 0);
    if (!ROMlib_valid_window(window) || WINDOW_KIND(window) != dialogKind)
        return paramErr;
    DialogPtr dialog = (DialogPtr)window;
    if (item < 1 || item > CountDITL(dialog))
        return paramErr;

    INTEGER type;
    HIDDEN_Handle handle;
    Rect bounds;
    GetDItem(dialog, item, &type, &handle, &bounds);
    type = CW(type);
    if (type & ctrlItem) {
        HIDDEN_RAW_SET(*control, HIDDEN_RAW(handle));
        return noErr;
    }
    if (!HIDDEN_RAW(handle))
        return paramErr;
    Handle text = HIDDEN_NATIVE(handle);
    ControlHandle result;
    if ((type & ~itemDisable) == editText)
        result = ROMlib_dialog_edit_control(window, item, text);
    else if ((type & ~itemDisable) == statText) {
        Size length = GetHandleSize(text);
        if (length < 0 || length > 255 || !STARH(text))
            return paramErr;
        Str255 title;
        title[0] = length;
        memcpy(title + 1, STARH(text), length);
        result = NewControl(window, &bounds, title, CW(bounds.left) < 8192, 0, 0, 0, ROMlib_static_text_proc, 0);
        if (result) {
            SetDItem(dialog, item, ctrlItem | resCtrl | (type & itemDisable), (Handle)result, &bounds);
            DisposHandle(text);
        }
    }
    else
        return paramErr;
    if (!result)
        return memFullErr;
    HIDDEN_RAW_SET(*control, RM(result));
    return noErr;
}

ControlHandle ROMlib_dialog_edit_control(WindowPtr window, INTEGER item, Handle text)
{
    for (ControlHandle control = WINDOW_CONTROL_LIST(window); control; control = CTL_NEXT_CONTROL(control)) {
        AuxCtlHandle aux = HIDDEN_NATIVE(*lookup_aux_ctl(control));
        if (aux && ROMlib_aux_control_extra(aux)->dialog_edit_item == item &&
            ROMlib_aux_control_extra(aux)->dialog_edit_text == (uint32)(uintptr_t)RM(text))
            return control;
    }

    /* The dialog item still owns the text, drawing and hit testing. */
    Rect empty = {0};
    ControlHandle control = NewControl(window, &empty, NULL, FALSE, 0, 0, 0, ROMlib_user_pane_proc, 0);
    if (control) {
        AuxCtlHandle aux = HIDDEN_NATIVE(*lookup_aux_ctl(control));
        ROMlib_aux_control_extra(aux)->dialog_edit_item = item;
        ROMlib_aux_control_extra(aux)->dialog_edit_text = (uint32)(uintptr_t)RM(text);
    }
    return control;
}

INTEGER
ROMlib_dialog_edit_control_item(ControlHandle control)
{
    AuxCtlHandle aux = HIDDEN_NATIVE(*lookup_aux_ctl(control));
    INTEGER type, item;
    HIDDEN_Handle text;
    Rect bounds;

    if (!aux || !(item = ROMlib_aux_control_extra(aux)->dialog_edit_item))
        return 0;
    DialogPtr dialog = (DialogPtr)CTL_OWNER(control);
    if (item > CountDITL(dialog))
        return 0;
    GetDItem(dialog, item, &type, &text, &bounds);
    if ((CW(type) & ~itemDisable) != editText ||
        (uint32)(uintptr_t)HIDDEN_RAW(text) != ROMlib_aux_control_extra(aux)->dialog_edit_text)
        return 0;
    return item;
}

P2(PUBLIC pascal trap, void, SetCTitle, ControlHandle, c, /* IMI-321 */
   StringPtr, t)
{
    if (c) {
        CTL_CALL_EXCURSION(c, {
            RgnHandle rh;

            rh = NewRgn();
            CTLCALL(c, calcCntlRgn, (intptr_t)rh);
            EraseRgn(rh);
            str255assign(HxX(c, contrlTitle), t);
            CTLCALL(c, drawCntl, ENTIRECONTROL);
            DisposeRgn(rh);
        });
    }
}

P2(PUBLIC pascal trap, void, GetCTitle, ControlHandle, c, /* IMI-321 */
   StringPtr, t)
{
    if (c)
        str255assign(t, HxX(c, contrlTitle));
}

P1(PUBLIC pascal trap, void, HideControl, ControlHandle, c) /* IMI-322 */
{
    if (c) {
        INTEGER item = ROMlib_dialog_edit_control_item(c);
        if (item) {
            HideDItem((DialogPtr)CTL_OWNER(c), item);
            return;
        }
        CTL_CALL_EXCURSION(c, {
            RgnHandle rh;

            if (c && CTL_VIS(c)) {
                rh = NewRgn();
                /* #### warning; should this be called with the control
		  owner as the current port, or no? */
                CTLCALL(c, calcCntlRgn, (intptr_t)rh);

                /* The following code is a hack that works around a problem
		  due to the different meanings of the fgColor and bgColor
		  fields of a grafport vs. a cgrafport.  In a grafport, those
		  fields contain magic numbers, but in a cgrafport they
		  contain colortable indices.  This means that if a grafport
		  is turned into a cgrafport, the old values may cause
		  trouble.  This makes me wonder whether or not our use of
		  them as indices is really correct.  W/o the following fix,
		  LogDig will erase magenta when you open a file and then
		  use Cmd-= to create a check-box, if you check and un-check
		  the check-box. */

                if (CGrafPort_p(thePort)) {
                    RGBForeColor(&CPORT_RGB_FG_COLOR(thePort));
                    RGBBackColor(&CPORT_RGB_BK_COLOR(thePort));
                }

                EraseRgn(rh);
                InvalRgn(rh);
                DisposeRgn(rh);
                CTL_VIS(c) = FALSE;
            }
        });
    }
}

/* It appears that ShowControl does not erase anything before calling
   drawCntl.  This is based on Loser running on the Mac, vs. running
   under ROMAW.  The EraseRgn used to be an EraseRect that was put
   into ctlDisplay a long time ago, and with no fanfare.  Perhaps
   there is more going on here; I don't know. */

P1(PUBLIC pascal trap, void, ShowControl, ControlHandle, c) /* IMI-322 */
{
    if (c) {
        INTEGER item = ROMlib_dialog_edit_control_item(c);
        if (item) {
            ShowDItem((DialogPtr)CTL_OWNER(c), item);
            return;
        }
        CTL_CALL_EXCURSION(c, {
            if (c && !CTL_VIS(c)) {
                HxX(c, contrlVis) = 255;
                CTLCALL(c, drawCntl, 0);
            }
        });
    }
}

P2(PUBLIC pascal trap, void, HiliteControl, ControlHandle, c, /* IMI-322 */
   INTEGER, state)
{
    if (c && HIDDEN_RAW(*(c))) {
        if (CTL_VIS(c)) {
            int16 oldh;

            oldh = CTL_HILITE(c);
            if (oldh != state) {
                CTL_CALL_EXCURSION(c, {
                    HxX(c, contrlHilite) = state;
                    if (oldh == INACTIVE || state == INACTIVE)
                        CTLCALL(c, drawCntl, 0);
                    else
                        CTLCALL(c, drawCntl, state);
                });
            }
        }
        else
            CTL_HILITE_X(c) = state;
    }
}

P1(PUBLIC pascal trap, void, DrawControls, WindowPtr, w) /* IMI-322 */
{
    ControlHandle c;

    if (!ROMlib_emptyvis) {
        for (c = WINDOW_CONTROL_LIST(w); c; c = CTL_NEXT_CONTROL(c))
            Draw1Control(c);
    }
}
