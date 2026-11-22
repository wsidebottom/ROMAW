/*+===================================================================
  File:      ctlInit.c

  Summary:   Implements ctl init support for Macintosh Control Manager operations.

  Classes:   None.

  Functions: ROMlib_abort_custom_control, ROMlib_alloc_custom_control, ROMlib_create_root_control, ROMlib_get_root_control

  Origin:    Existing repository source in executor/src/controls; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986, 1988, 1989, 1990, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in ControlMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "QuickDraw.h"
#include "WindowMgr.h"
#include "ControlMgr.h"
#include "ToolboxUtil.h"
#include "ResourceMgr.h"
#include "MemoryMgr.h"
#include "OSUtil.h"

#include "rsys/ctl.h"
#include "rsys/wind.h"
#include "rsys/cquick.h"
#include "rsys/resource.h"

PUBLIC BOOLEAN ROMlib_cdef0_is_rectangular = FALSE;

static ControlHandle new_control(WindowPtr wst, const Rect* r, StringPtr title, BOOLEAN vis, INTEGER value, INTEGER min,
                                 INTEGER max, INTEGER procid, LONGINT rc, uint32 custom_definition)
{
    ControlHandle retval;
    GrafPtr gp;
    AuxCtlHandle tmp, auxctlhead;
    Handle temph;
    boolean_t pane = procid == ROMlib_root_control_proc;
    boolean_t live_scroll = FALSE;

    if (!wst || !r) {
        warning_unexpected("NewControl called without owner window");
        return 0;
    }
    retval = (ControlHandle)NewHandle((Size)sizeof(ControlRecord));
    if (!retval)
        return 0;
    temph = pane || custom_definition ? NULL : GetResource(TICK("CDEF"), procid >> 4);
    if (!temph && (procid & ~useWFont) >= 368 && (procid & ~useWFont) <= 370) {
        /* Appearance push button, checkbox and radio button. */
        procid -= 368;
        temph = GetResource(TICK("CDEF"), 0);
    }
    if (!temph && (procid & ~15) == 400) {
        /* The Appearance popup resource carries the same menu ID and title
         width as the classic popup control. */
        procid = 1008;
        temph = GetResource(TICK("CDEF"), procid >> 4);
        if (max == -1 && title)
            max = StringWidth(title) + 8;
    }
    if (!temph && procid == ROMlib_live_scroll_proc) {
        live_scroll = TRUE;
        procid = scrollBarProc;
        temph = GetResource(TICK("CDEF"), procid >> 4);
    }
    if (!temph)
        switch (procid & ~15) {
        case ROMlib_image_well_proc:
        case ROMlib_bevel_button_proc:
        case ROMlib_slider_proc:
        case ROMlib_tabs_proc:
        case ROMlib_separator_proc:
        case ROMlib_group_box_proc:
        case ROMlib_user_pane_proc:
        case ROMlib_edit_text_proc:
        case ROMlib_static_text_proc:
            pane = TRUE;
            break;
        }
    FIELD_ASSIGN_RAW(PP_RAW(HxX(retval, contrlDefProc)), RM(temph));
    if (!pane && !temph && !custom_definition) {
        DisposHandle((Handle)retval);
        return 0;
    }

    if (temph && (procid >> 4) == 0) {
        INTEGER id;
        ResType type;
        Str255 name;

        GetResInfo(temph, &id, &type, name);
        ROMlib_cdef0_is_rectangular = (EqualString(name, (StringPtr) "\023rectangular buttons", FALSE, FALSE));
    }

    if (!title)
        title = (StringPtr) ""; /* nothing ("\p" == "") */
    gp = thePort;
    SetPort((GrafPtr)wst);

    tmp = MR(AuxCtlHead);
    auxctlhead = (AuxCtlHandle)NewHandleClear(sizeof(AuxCtlRec) + sizeof(ROMlib_aux_control_data));
    if (!auxctlhead) {
        DisposHandle((Handle)retval);
        SetPort(gp);
        return NULL;
    }
    HIDDEN_RAW_SET(AuxCtlHead_H, RM(auxctlhead));

    HASSIGN_6(auxctlhead, acNext, RM(tmp), acOwner, RM(retval), acCTable, (CCTabHandle)RM(GetResource(TICK("cctb"), 0)),
              acFlags, CW(procid & 0x0F), acReserved,
              CL(live_scroll ? ROMlib_live_scroll_proc
                 : pane      ? procid
                             : 0),
              acRefCon, CLC(0));
    ROMlib_aux_control_extra(auxctlhead)->custom_definition = custom_definition;
    str255assign(CTL_TITLE(retval), title);

    if (!PP_RAW(HxX(auxctlhead, acCTable))) {
        warning_unexpected("no 'cctb', 0, probably old system file ");
        FIELD_ASSIGN_RAW(PP_RAW(HxX(auxctlhead, acCTable)), (CCTabHandle)RM(default_ctl_ctab));
    }

    HASSIGN_11(retval, contrlRect, *r, nextControl, WINDOW_CONTROL_LIST_X(wst), contrlOwner, RM(wst), contrlVis,
               vis ? 255 : 0, contrlHilite, 0, contrlValue, CW(value), contrlMin, CW(min), contrlMax, CW(max),
               contrlData, CLC_NULL, contrlAction, CLC_NULL, contrlRfCon, CL(rc));

    FIELD_ASSIGN_RAW(WINDOW_CONTROL_LIST_X(wst), RM(retval));

    if (!custom_definition) {
        CTL_CALL_EXCURSION(retval, {
            CTLCALL(retval, initCntl, 0);
            if (WINDOW_VISIBLE_X(wst) && vis)
                CTLCALL(retval, drawCntl, 0);
        });
    }

    SetPort(gp);
    return retval;
}

P9(PUBLIC pascal trap, ControlHandle, NewControl, WindowPtr, wst, Rect*, r, StringPtr, title, BOOLEAN, vis, INTEGER,
   value, INTEGER, min, INTEGER, max, INTEGER, procid, LONGINT, rc) /* IMI-319 */
{
    return new_control(wst, r, title, vis, value, min, max, procid, rc, 0);
}

PUBLIC ControlHandle ROMlib_alloc_custom_control(WindowPtr window, const Rect* bounds, uint32 definition)
{
    return new_control(window, bounds, NULL, FALSE, 0, 0, 0, 0, 0, definition);
}

enum { errNoRootControl = -30586, errRootAlreadyExists = -30587 };

PUBLIC OSErr ROMlib_get_root_control(WindowPtr window, HIDDEN_ControlHandle* root)
{
    if (!root || !ROMlib_valid_window(window))
        return paramErr;
    HIDDEN_RAW_SET(*root, 0);
    for (ControlHandle control = WINDOW_CONTROL_LIST(window); control; control = CTL_NEXT_CONTROL(control)) {
        AuxCtlHandle aux = HIDDEN_NATIVE(*lookup_aux_ctl(control));
        if (aux && Hx(aux, acReserved) == ROMlib_root_control_proc) {
            HIDDEN_RAW_SET(*root, RM(control));
            return noErr;
        }
    }
    return errNoRootControl;
}

PUBLIC OSErr ROMlib_create_root_control(WindowPtr window, HIDDEN_ControlHandle* root)
{
    OSErr error = ROMlib_get_root_control(window, root);
    if (error != errNoRootControl)
        return error == noErr ? errRootAlreadyExists : error;
    ControlHandle control = NewControl(window, &PORT_RECT(window), NULL, FALSE, 0, 0, 0, ROMlib_root_control_proc, 0);
    if (!control)
        return memFullErr;
    HIDDEN_RAW_SET(*root, RM(control));
    return noErr;
}

P2(PUBLIC pascal trap, ControlHandle, GetNewControl, /* IMI-321 */
   INTEGER, cid, WindowPtr, wst)
{
    typedef contrlrestype* wp;
    MAKE_HIDDEN(wp);
    HIDDEN_wp* wh;
    ControlHandle retval;
    Handle ctab_res_h;

    wh = (HIDDEN_wp*)GetResource(TICK("CNTL"), cid);
    if (!wh)
        return 0;
    if (!HIDDEN_RAW((*wh)))
        LoadResource((Handle)wh);
    ctab_res_h = ROMlib_getrestid(TICK("cctb"), cid);
    retval = NewControl(wst, &(HxX(wh, _crect)), (StringPtr)((char*)&HxX(wh, _crect) + 22), /* _ctitle */
                        Hx(wh, _cvisible) ? 255 : 0, Hx(wh, _cvalue), Hx(wh, _cmin), Hx(wh, _cmax), Hx(wh, _cprocid),
                        CL(*(LONGINT*)((char*)&HxX(wh, _crect) + 18))); /* _crefcon */
    if (retval && ctab_res_h)
        SetCtlColor(retval, (CCTabHandle)ctab_res_h);
    return retval;
}

P2(PUBLIC pascal trap, void, SetCtlColor, ControlHandle, ctl, CCTabHandle, ctab)
{
    AuxCtlHandle aux_c;

    if (ctl) {
        aux_c = HIDDEN_NATIVE(*lookup_aux_ctl(ctl));
        if (!aux_c) {
            AuxCtlHandle t_aux_c;

            /* allocate one */
            t_aux_c = MR(AuxCtlHead);
            aux_c = (AuxCtlHandle)NewHandleClear(sizeof(AuxCtlRec) + sizeof(ROMlib_aux_control_data));
            if (!aux_c)
                return;
            HIDDEN_RAW_SET(AuxCtlHead_H, RM(aux_c));
            FIELD_ASSIGN_RAW(PP_RAW(HxX(aux_c, acNext)), RM(t_aux_c));
            FIELD_ASSIGN_RAW(PP_RAW(HxX(aux_c, acOwner)), RM(ctl));

            FIELD_ASSIGN_RAW(PP_RAW(HxX(aux_c, acCTable)), RM(ctab));

            HxX(aux_c, acFlags) = 0;
            HxX(aux_c, acReserved) = 0;
            HxX(aux_c, acRefCon) = 0;
        }
        else
            PP_RAW(HxX(aux_c, acCTable)) = RM(ctab);

        if (CTL_VIS(ctl))
            Draw1Control(ctl);
    }
    else {
        abort();
    }
}

static void dispose_single_control(ControlHandle c, boolean_t notify)
{
    MAKE_HIDDEN(ControlHandle);
    HIDDEN_ControlHandle* t;
    MAKE_HIDDEN(AuxCtlHandle);
    HIDDEN_AuxCtlHandle* auxhp;
    AuxCtlHandle saveauxh;

    if (notify)
        HideControl(c);
    ROMlib_forget_control_font_style(c);
    ROMlib_detach_control(c);
    const uint32 supervisor = US_TO_SYN68K(c);
    for (ControlHandle candidate = WINDOW_CONTROL_LIST(CTL_OWNER(c)); candidate;
         candidate = CTL_NEXT_CONTROL(candidate)) {
        AuxCtlHandle aux = HIDDEN_NATIVE(*lookup_aux_ctl(candidate));
        if (aux && ROMlib_aux_control_extra(aux)->supervisor == supervisor)
            ROMlib_aux_control_extra(aux)->supervisor = 0;
    }
    if (notify)
        CTL_CALL_EXCURSION(c, { CTLCALL(c, dispCntl, 0); });

    for (t = (HIDDEN_ControlHandle*)&(PP_RAW(((WindowPeek)(HxP(c, contrlOwner)))->controlList));
         HIDDEN_RAW((*t)) && STARH(t) != c; t = (HIDDEN_ControlHandle*)&(PP_RAW((STARH(STARH(t)))->nextControl)))
        ;
    if (HIDDEN_RAW((*t)))
        FIELD_ASSIGN_RAW(*t, PP_RAW(HxX(c, nextControl)));
    for (auxhp = (HIDDEN_AuxCtlHandle*)&AuxCtlHead_H;
         HIDDEN_RAW((*auxhp)) && PP_RAW((STARH(STARH(auxhp)))->acOwner) != c;
         auxhp = (HIDDEN_AuxCtlHandle*)&PP_RAW((STARH(STARH(auxhp)))->acNext))
        ;
    if (HIDDEN_RAW((*auxhp))) {
        saveauxh = STARH(auxhp);
        FIELD_ASSIGN_RAW(*auxhp, STARH(STARH(auxhp))->acNext);
        DisposHandle((Handle)saveauxh);
    }

    DisposHandle((Handle)c);
}

static void dispose_control(ControlHandle c, boolean_t notify)
{
    if (!c || !STARH(c))
        return;
    ControlHandle current = c;
    for (;;) {
        AuxCtlHandle aux = HIDDEN_NATIVE(*lookup_aux_ctl(current));
        uint32 child = aux ? ROMlib_aux_control_extra(aux)->first_child : 0;
        if (child) {
            current = MR(child);
            continue;
        }
        const int finished = current == c;
        uint32 parent = aux ? ROMlib_aux_control_extra(aux)->parent : 0;
        dispose_single_control(current, !finished || notify);
        if (finished || !parent)
            break;
        current = MR(parent);
    }
}

PUBLIC void ROMlib_abort_custom_control(ControlHandle control)
{
    dispose_control(control, FALSE);
}

P1(PUBLIC pascal trap, void, DisposeControl, ControlHandle, c) /* IMI-321 */
{
    dispose_control(c, TRUE);
}

P1(PUBLIC pascal trap, void, KillControls, WindowPtr, w) /* IMI-321 */
{
    ControlHandle control = WINDOW_CONTROL_LIST(w);
    while (control) {
        ControlHandle next = CTL_NEXT_CONTROL(control);
        DisposeControl(control);
        if (!next)
            break;
        /* Disposing an embedded subtree can also dispose the saved successor. */
        for (control = WINDOW_CONTROL_LIST(w); control && control != next; control = CTL_NEXT_CONTROL(control))
            ;
        if (!control)
            control = WINDOW_CONTROL_LIST(w);
    }
}
