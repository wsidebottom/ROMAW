/*+===================================================================
  File:      windColor.c

  Summary:   Implements wind color support for Macintosh Window Manager operations.

  Classes:   None.

  Functions: lookup_aux_win, ROMlib_window_change_owners, ROMlib_window_dispose_owned_colors,
             ROMlib_window_get_metadata, wind_color_init

  Origin:    Existing repository source in executor/src/windows; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1994, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* color window manager functions; introduced in IM-V or beyond */

#include "rsys/common.h"
#include "QuickDraw.h"
#include "CQuickDraw.h"
#include "WindowMgr.h"
#include "MemoryMgr.h"

#include "rsys/cquick.h"
#include "rsys/layers.h"
#include "rsys/wind.h"

/* return a pointer to the auxilary window record
   associated with `w' */

AuxWinHandle default_aux_win = NULL;

const RGBColor ROMlib_theme_background_rgb = {CWC(0xdddd), CWC(0xdddd), CWC(0xdddd)};

#define BLACK_RGB \
    {             \
        CWC(0),   \
        CWC(0),   \
        CWC(0),   \
    }
#define WHITE_RGB    \
    {                \
        CWC(0xFFFF), \
        CWC(0xFFFF), \
        CWC(0xFFFF), \
    }

/* bw window colortable */
const ColorSpec default_bw_win_ctab[] = {
    {CWC(wContentColor), WHITE_RGB},    {CWC(wFrameColor), BLACK_RGB},    {CWC(wTextColor), BLACK_RGB},
    {CWC(wHiliteColor), BLACK_RGB},     {CWC(wTitleBarColor), WHITE_RGB}, {CWC(wHiliteColorLight), WHITE_RGB},
    {CWC(wHiliteColorDark), BLACK_RGB}, {CWC(wTitleBarLight), WHITE_RGB}, {CWC(wTitleBarDark), BLACK_RGB},
    {CWC(wDialogLight), BLACK_RGB},     {CWC(wDialogDark), BLACK_RGB},    {CWC(wTingeLight), BLACK_RGB},
    {CWC(wTingeDark), BLACK_RGB},
};

#define LT_BLUISH_RGB {CWC(0xCCCC), CWC(0xCCCC), CWC(0xFFFF)}
#define DK_BLUISH_RGB {CWC(0x3333), CWC(0x3333), CWC(0x6666)}
/* the default `bluish' window color table */
const ColorSpec default_color_win_ctab[] = {
    {CWC(wContentColor), WHITE_RGB},    {CWC(wFrameColor), BLACK_RGB},    {CWC(wTextColor), BLACK_RGB},
    {CWC(wHiliteColor), BLACK_RGB},     {CWC(wTitleBarColor), WHITE_RGB}, {CWC(wHiliteColorLight), WHITE_RGB},
    {CWC(wHiliteColorDark), BLACK_RGB}, {CWC(wTitleBarLight), WHITE_RGB}, {CWC(wTitleBarDark), BLACK_RGB},
    {CWC(wDialogLight), LT_BLUISH_RGB}, {CWC(wDialogDark), BLACK_RGB},    {CWC(wTingeLight), LT_BLUISH_RGB},
    {CWC(wTingeDark), DK_BLUISH_RGB},
};
#undef DK_BLUISH_RGB
#undef LT_BLUISH_RGB

#define GRAY_RGB {CWC(0x8888), CWC(0x8888), CWC(0x8888)}
/* stolen from the default colortable excel
   tries to install */
const ColorSpec default_system6_color_win_ctab[] = {
    {CWC(wContentColor), WHITE_RGB}, {CWC(wFrameColor), BLACK_RGB},    {CWC(wTextColor), BLACK_RGB},
    {CWC(wHiliteColor), BLACK_RGB},  {CWC(wTitleBarColor), WHITE_RGB},
};
#undef GRAY_RGB
#undef WHITE_RGB
#undef BLACK_RGB

void wind_color_init(void)
{
    /* initalize the default window colortable */
    ZONE_SAVE_EXCURSION(SysZone, {
        default_aux_win = (AuxWinHandle)NewHandle(sizeof(AuxWinRec));
        PP_RAW(HxX(default_aux_win, awNext)) = 0;
        PP_RAW(HxX(default_aux_win, awOwner)) = 0;
        PP_RAW(HxX(default_aux_win, awCTable)) = (CTabHandle)RM(GetResource(TICK("wctb"), 0));
        PP_RAW(HxX(default_aux_win, dialogCItem)) = 0;
        HxX(default_aux_win, awFlags) = 0;
        PP_RAW(HxX(default_aux_win, awReserved)) = 0;
        HxX(default_aux_win, awRefCon) = 0;
    });
}

HIDDEN_AuxWinHandle* lookup_aux_win(WindowPtr w)
{
    HIDDEN_AuxWinHandle* t;

    for (t = &AuxWinHead_H; HIDDEN_RAW(*t) && HxP(HIDDEN_NATIVE(*t), awOwner) != w;
         t = (HIDDEN_AuxWinHandle*)&HxX(HIDDEN_NATIVE(*t), awNext))
        ;
    return t;
}

PUBLIC OSErr ROMlib_window_change_owners(WindowPtr window, int change, uint32* owners)
{
    AuxWinHandle aux = HIDDEN_NATIVE(*lookup_aux_win(window));
    uint32 count = 1;
    unsigned char* storage = aux && GetHandleSize((Handle)aux) >= (Size)(sizeof(AuxWinRec) + sizeof(uint32))
                                 ? (unsigned char*)(STARH(aux) + 1)
                                 : NULL;
    if (storage) {
        uint32 stored;
        memcpy(&stored, storage, sizeof stored);
        if (stored)
            count = CL(stored);
    }
    if (change > 0 && !storage)
        return memFullErr;
    if (change > 0 && count == UINT32_MAX)
        return paramErr;
    count = change < 0 ? count - 1 : change > 0 ? count + 1 : count;
    if (storage && change && count) {
        const uint32 stored = CL(count);
        memcpy(storage, &stored, sizeof stored);
    }
    *owners = count;
    return noErr;
}

static ROMlib_window_metadata* auxiliary_metadata(AuxWinHandle aux)
{
    if (!aux || GetHandleSize((Handle)aux) < (Size)ROMLIB_AUX_WINDOW_SIZE)
        return NULL;
    return (ROMlib_window_metadata*)((Byte*)(STARH(aux) + 1) + sizeof(uint32));
}

ROMlib_window_metadata* ROMlib_window_get_metadata(WindowPtr window)
{
    void* layer = ROMlib_layer_parent(window);
    if (!layer)
        return NULL;
    void* old_layer = ROMlib_layer_swap(layer);
    AuxWinHandle aux = HIDDEN_NATIVE(*lookup_aux_win(window));
    ROMlib_layer_swap(old_layer);
    return auxiliary_metadata(aux);
}

void ROMlib_window_dispose_owned_colors(AuxWinHandle aux)
{
    ROMlib_window_metadata* metadata = auxiliary_metadata(aux);
    if (!metadata || !metadata->owned_content_colors)
        return;
    const uint32 owned = metadata->owned_content_colors;
    metadata->owned_content_colors = 0;
    CTabHandle current = HxP(aux, awCTable);
    if (current && US_TO_SYN68K(current) == owned)
        DisposHandle((Handle)current);
}

P2(PUBLIC pascal trap, void, SetWinColor, WindowPtr, w, CTabHandle, new_w_ctab)
{
    if (w) {
        AuxWinHandle aux_w;

        aux_w = HIDDEN_NATIVE(*lookup_aux_win(w));

        if (!aux_w) {
            AuxWinHandle t_aux_w;

            t_aux_w = AuxWinHead;
            aux_w = (AuxWinHandle)NewHandleClear(ROMLIB_AUX_WINDOW_SIZE);
            HIDDEN_RAW_SET(AuxWinHead_H, RM(aux_w));
            FIELD_ASSIGN_RAW(PP_RAW(HxX(aux_w, awNext)), t_aux_w);
            FIELD_ASSIGN_RAW(PP_RAW(HxX(aux_w, awOwner)), (WindowPtr)RM(w));
            FIELD_ASSIGN_RAW(/* FIXME: copy? */
                             PP_RAW(HxX(aux_w, awCTable)), RM(new_w_ctab));
            FIELD_ASSIGN_RAW(PP_RAW(HxX(aux_w, dialogCItem)), 0);
            HxX(aux_w, awFlags) = /* CL (proc_id & 0x0F) */ 0;
            PP_RAW(HxX(aux_w, awReserved)) = 0;
            HxX(aux_w, awRefCon) = 0;
        }
        else {
            if (HxP(aux_w, awCTable) != new_w_ctab)
                ROMlib_window_dispose_owned_colors(aux_w);
            PP_RAW(HxX(aux_w, awCTable)) = RM(new_w_ctab);
        }

        if (CGrafPort_p(w)) {
            ColorSpec* w_ctab_table;
            RGBColor* color;

            w_ctab_table = CTAB_TABLE(new_w_ctab);
            color = &w_ctab_table[wContentColor].rgb;

            CPORT_RGB_BK_COLOR(w) = *color;

            /* pick the best color and store it into window's port's
	     bkColor field */
            PORT_BK_COLOR_X(w) = CL(Color2Index(color));

            if (WINDOW_VISIBLE_X(w))
                THEPORT_SAVE_EXCURSION(w, {
                    RgnHandle t;
                    t = NewRgn();

                    CopyRgn(WINDOW_CONT_REGION(w), t);
                    OffsetRgn(t, CW(PORT_BOUNDS(w).left), CW(PORT_BOUNDS(w).top));
                    EraseRgn(t);
                    DisposeRgn(t);
                });
        }

        if (WINDOW_VISIBLE_X(w)) {
            /* set the port here, don't just save it while drawing the
	     window
	     FIXME: i'm not sure to what extent this should be the
	     case, at the bare minimum at least here */
            SetPort(MR(wmgr_port));

            SetClip(WINDOW_STRUCT_REGION(w));
            ClipAbove((WindowPeek)w);
            WINDCALL(w, wDraw, 0);
        }
    }
    else {
        /* modify the default color window table */
        abort();
    }
}

/* NOTE: all documentation on this function is either vague to the
   point of certain mis-interpretation, or plain wrong.

   it's second argument is `AuxWinHandle *' (or `AuxWinHndl *' in
   Think C speak), and returns the aux window entry for the given
   window; if NULL is passed, an handle to a `AuxWinRec' is placed in
   the aux_w_out argument which contains the default window color
   table

   they named it correctly, but noone got the documentation right */

P2(PUBLIC pascal trap, BOOLEAN, GetAuxWin, WindowPtr, w, HIDDEN_AuxWinHandle*, aux_win_out)
{
    if (!w) {
        /* return default window color table */
        HIDDEN_RAW_SET(*(aux_win_out), RM(default_aux_win));
        return TRUE;
    }
    else {
        AuxWinHandle t;

        t = HIDDEN_RAW(*lookup_aux_win(w));
        if (t) {
            /* return this window's color table */
            HIDDEN_RAW_SET(*(aux_win_out), t);
            return TRUE;
        }
        else {
            /* return default window color table */
            HIDDEN_RAW_SET(*(aux_win_out), RM(default_aux_win));
            return FALSE;
        }
    }
}
