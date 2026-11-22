/*+===================================================================
  File:      windInit.c

  Summary:   Implements wind init support for Macintosh Window Manager operations.

  Classes:   None.

  Functions: ROMlib_new_window_common

  Origin:    Existing repository source in executor/src/windows; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986-1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in WindowMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "QuickDraw.h"
#include "CQuickDraw.h"
#include "WindowMgr.h"
#include "ToolboxUtil.h"
#include "ResourceMgr.h"
#include "FontMgr.h"
#include "MemoryMgr.h"
#include "SegmentLdr.h"
#include "OSUtil.h"
#include "OSEvent.h"
#include "ControlMgr.h"
#include "MenuMgr.h"
#include "SysErr.h"
#include "DialogMgr.h"

#include "rsys/cquick.h"
#include "rsys/wind.h"
#include "rsys/systemwindow.h"
#include "rsys/layers.h"
#include "rsys/menu.h"
#include "rsys/resource.h"
#include "rsys/system_error.h"
#include "rsys/dirtyrect.h"
#include "rsys/vdriver.h"

#include "rsys/prefs.h"
#include "rsys/flags.h"

#include "rsys/segment.h"
#include "rsys/file.h"
#include "rsys/romaw.h"
#include "rsys/options.h"
#include "rsys/launch.h"

PUBLIC BOOLEAN ROMlib_dirtyvariant = FALSE;

boolean_t system_file_version_skew_p;

static void exit_romaw(void)
{
    ROMlib_exit = TRUE;
    ExitToShell();
}

PRIVATE char* reinstall = "System and %System";

P0(PUBLIC pascal trap, void, InitWindows)
{
    PatHandle ph;
    PixPatHandle new_ph;
    RgnHandle mrgn, corners;
    Rect mbar_rect;

    fprintf(stderr, "trace: InitWindows entered\n");
    fflush(stderr);
    HIDDEN_RAW_SET(AuxWinHead_H, RM(default_aux_win));
    HIDDEN_RAW_SET(SaveVisRgn_H, NULL);

    THEPORT_SAVE_EXCURSION(thePort, {
        fprintf(stderr, "trace: InitWindows InitPort begin\n");
        fflush(stderr);
        /* FIXME: is this a memory leak, to just call InitPort () again? */
        InitPort(MR(WMgrPort));
        InitCPort(MR(WMgrCPort));
        fprintf(stderr, "trace: InitWindows InitPort complete\n");
        fflush(stderr);

        ph = GetPattern(deskPatID);
        fprintf(stderr, "trace: InitWindows GetPattern complete ph=%p\n", (void*)ph);
        fflush(stderr);
        if (ph == NULL) {
            fprintf(stderr,
                    "Can't open System file.\n"
                    "This is very bad.  You will have to reinstall %s"
                    " before\n"
                    "ROMAW will work again.\n",
                    reinstall);
            ROMlib_exit = TRUE;
            ExitToShell();
        }
        new_ph = GetPixPat(deskPatID);
        if (new_ph)
            HIDDEN_RAW_SET(DeskCPat_H, RM(new_ph));
        else
            USE_DESKCPAT_VAR &= ~USE_DESKCPAT_BIT;
        fprintf(stderr, "trace: InitWindows InitPalettes begin\n");
        fflush(stderr);
        InitPalettes();
        fprintf(stderr, "trace: InitWindows InitPalettes complete\n");
        fflush(stderr);
        fprintf(stderr, "trace: InitWindows InitMenus begin\n");
        fflush(stderr);
        InitMenus();
        fprintf(stderr, "trace: InitWindows InitMenus complete\n");
        fflush(stderr);
        PATASSIGN(DeskPattern, STARH(ph));
        HIDDEN_RAW_SET(GrayRgn_H, RM(NewRgn()));
        fprintf(stderr, "trace: InitWindows GrayRgn created\n");
        fflush(stderr);

        OpenRgn();
        fprintf(stderr, "trace: InitWindows OpenRgn complete\n");
        fflush(stderr);
        if (ROMlib_creator && !(ROMlib_options & ROMLIB_RECT_SCREEN_BIT))
            FrameRoundRect(&GD_BOUNDS(MR(TheGDevice)), 16, 16);
        else
            FrameRect(&GD_BOUNDS(MR(TheGDevice)));
        fprintf(stderr, "trace: InitWindows frame complete\n");
        fflush(stderr);
        CloseRgn(MR(GrayRgn));
        fprintf(stderr, "trace: InitWindows CloseRgn complete\n");
        fflush(stderr);
        mrgn = NewRgn();
        fprintf(stderr, "trace: InitWindows mrgn created\n");
        fflush(stderr);
        SetRect(&mbar_rect, 0, 0, CW(GD_BOUNDS(MR(TheGDevice)).right), CW(MBarHeight));
        SetRectRgn(mrgn, CW(mbar_rect.left), CW(mbar_rect.top), CW(mbar_rect.right), CW(mbar_rect.bottom));
        fprintf(stderr, "trace: InitWindows SetRectRgn mrgn complete\n");
        fflush(stderr);
        SectRgn(MR(GrayRgn), mrgn, mrgn);
        fprintf(stderr, "trace: InitWindows SectRgn complete\n");
        fflush(stderr);
        corners = NewRgn();
        fprintf(stderr, "trace: InitWindows corners created\n");
        fflush(stderr);
        SetRectRgn(corners, 0, 0, CW(GD_BOUNDS(MR(TheGDevice)).right), CW(GD_BOUNDS(MR(TheGDevice)).bottom));
        fprintf(stderr, "trace: InitWindows SetRectRgn corners complete\n");
        fflush(stderr);
        DiffRgn(corners, MR(GrayRgn), corners);
        fprintf(stderr, "trace: InitWindows DiffRgn corners complete\n");
        fflush(stderr);
        PaintRgn(corners);
        fprintf(stderr, "trace: InitWindows PaintRgn corners complete\n");
        fflush(stderr);
        CopyRgn(MR(GrayRgn), PORT_VIS_REGION(MR(wmgr_port)));
        fprintf(stderr, "trace: InitWindows CopyRgn vis complete\n");
        fflush(stderr);
        DiffRgn(MR(GrayRgn), mrgn, MR(GrayRgn));
        fprintf(stderr, "trace: InitWindows DiffRgn gray complete\n");
        fflush(stderr);
        EraseRect(&mbar_rect);
        if ((USE_DESKCPAT_VAR & USE_DESKCPAT_BIT) && PIXMAP_PIXEL_SIZE(GD_PMAP(MR(MainDevice))) > 2)
            FillCRgn(MR(GrayRgn), MR(DeskCPat));
        else
            FillRgn(MR(GrayRgn), DeskPattern);
        dirty_rect_update_screen();
        vdriver_flush_display();
        fprintf(stderr, "trace: InitWindows early menu/desktop paint complete\n");
        fflush(stderr);
        DisposeRgn(mrgn);
        DisposeRgn(corners);
        fprintf(stderr, "trace: InitWindows dispose regions complete\n");
        fflush(stderr);
        CopyRgn(MR(GrayRgn), PORT_CLIP_REGION(MR(wmgr_port)));
        fprintf(stderr, "trace: InitWindows CopyRgn clip complete\n");
        fflush(stderr);
        HIDDEN_RAW_SET(WindowList_H, NULL);
        SaveUpdate = -1;
        PaintWhite = -1;
        HIDDEN_RAW_SET(DeskHook_H, NULL);
        HIDDEN_RAW_SET(GhostWindow_H, NULL);
        PATASSIGN(DragPattern, gray);
        fprintf(stderr, "trace: InitWindows excursion complete\n");
        fflush(stderr);
    });

    /* since there is no `InitControls ()', we do this here */
    fprintf(stderr, "trace: InitWindows ctl_color_init begin\n");
    fflush(stderr);
    ctl_color_init();
    fprintf(stderr, "trace: InitWindows ctl_color_init complete\n");
    fflush(stderr);

    WWExist = EXIST_YES;

    {
        static boolean_t issued_system_file_version_skew_warning_p = FALSE;

        if (system_file_version_skew_p && !issued_system_file_version_skew_warning_p) {
            system_error("\
The system file you have installed appears to be too old. \
ROMAW may die without warning because of this mismatch",
                         0, "Continue", "Exit", NULL, NULL, exit_romaw, NULL);
        }
        issued_system_file_version_skew_warning_p = TRUE;
    }

    switch (ROMlib_launch_failure) {
    case launch_no_failure:
        break;
    case launch_no_supported_fragment:
        system_error("This application has no supported executable fragment. "
                     "ROMAW requires a CODE 0 entry point or a supported "
                     "PowerPC or CFM-68K application fragment.",
                     0, "OK", NULL, NULL, NULL, NULL, NULL);
        break;
    case launch_damaged:
        system_error("That application appears damaged (lacks CODE and cfrg).", 0, "OK", NULL, NULL, NULL, NULL, NULL);
        break;

    case launch_compressed_ge7:
        system_error("That application has a compressed CODE 0.  "
                     "It is probably unusable under ROMAW but "
                     "might work in System 6 mode",
                     0, "OK", NULL, NULL, NULL, NULL, NULL);
        break;
    case launch_compressed_lt7:
        system_error("That application has a compressed CODE 0.  "
                     "It will not run under this version of ROMAW.",
                     0, "OK", NULL, NULL, NULL, NULL, NULL);
        break;

    default:
        warning_unexpected("%d", ROMlib_launch_failure);
        break;
    }
    ROMlib_launch_failure = launch_no_failure;

    if (!size_info.application_p)
        return;

    /* only issue warnings once */
    size_info.application_p = FALSE;

    if ((!size_info.size_resource_present_p || (size_info.size_flags & SZis32BitCompatible) != SZis32BitCompatible) &&
        !ROMlib_nowarn32 && MMU32Bit) {
        system_error("This application doesn't claim to be \"32 bit clean\".  "
                     "It is quite possible that this program will not work "
                     "under ROMAW.",
                     0, "Continue", "Restart", NULL, NULL, C_ExitToShell, NULL);
    }

    if (size_info.size_resource_present_p && ROMlib_applzone_size < size_info.preferred_size) {
        char msg_buf[1024];
        int applzone_in_k, preferred_size_in_k;

        applzone_in_k = ROMlib_applzone_size / 1024;
        preferred_size_in_k = (size_info.preferred_size + 1023) / 1024;

        sprintf(msg_buf,
                "This application prefers `%dk' of memory, "
                "but only '%dk' of memory is available in the application "
                "zone.  You should exit ROMAW and run it again "
                "with \"-applzone %dk\".",
                preferred_size_in_k, applzone_in_k, preferred_size_in_k);

        system_error(msg_buf, 0, "Continue", "Exit", NULL, NULL, exit_romaw, NULL);
    }
}

P1(PUBLIC pascal trap, void, GetWMgrPort, HIDDEN_GrafPtr*, wp)
{
    HIDDEN_RAW_SET(*(wp), WMgrPort);
}

P1(PUBLIC pascal trap, void, GetCWMgrPort, HIDDEN_CGrafPtr*, wp)
{
    HIDDEN_RAW_SET(*(wp), WMgrCPort);
}

P1(PUBLIC pascal trap, void, SetDeskCPat, PixPatHandle, ph)
{
    PatHandle bw_ph;

    if (ph) {
        HIDDEN_RAW_SET(DeskCPat_H, RM(ph));
        USE_DESKCPAT_VAR |= USE_DESKCPAT_BIT;
    }
    else {
        bw_ph = GetPattern(deskPatID);
        PATASSIGN(DeskPattern, STARH(bw_ph));
        USE_DESKCPAT_VAR &= ~USE_DESKCPAT_BIT;
    }
    PaintOne((WindowPeek)0, MR(GrayRgn));
}

void ROMlib_new_window_common(WindowPeek w, int allocated_p, int cwindow_p, Rect* bounds, StringPtr title,
                              BOOLEAN visible_p, INTEGER proc_id, WindowPtr behind, BOOLEAN wind_close_flag,
                              LONGINT ref_con)
{
    WindowPeek t_w;
    AuxWinHandle t_aux_w;
    GrafPtr save_port;

    save_port = thePort;
    if (!title)
        title = (StringPtr) ""; /* thank MS Word for pointing this out */
    if (!behind) {
        FIELD_ASSIGN_RAW(WINDOW_NEXT_WINDOW_X(w), CLC(0));
        if (WindowList) {
            for (t_w = MR(WindowList); PP_RAW(WINDOW_NEXT_WINDOW_X(t_w)); t_w = WINDOW_NEXT_WINDOW(t_w))
                ;
            FIELD_ASSIGN_RAW(WINDOW_NEXT_WINDOW_X(t_w), RM(w));
        }
        else {
            HIDDEN_RAW_SET(WindowList_H, RM(w));
            if (visible_p) {
                /* notify the palette manager that the `FrontWindow ()'
		 may have changed */
                pm_front_window_maybe_changed_hook();
            }
        }
    }
    else if (behind == (WindowPtr)-1L) {
        FIELD_ASSIGN_RAW(WINDOW_NEXT_WINDOW_X(w), WindowList);
        HIDDEN_RAW_SET(WindowList_H, (WindowPeek)RM(w));
        if (visible_p) {
            /* notify the palette manager that the `FrontWindow ()' may have
	     changed */
            pm_front_window_maybe_changed_hook();
        }
    }
    else {
        PP_RAW(WINDOW_NEXT_WINDOW_X(w)) = PP_RAW(WINDOW_NEXT_WINDOW_X(behind));
        FIELD_ASSIGN_RAW(WINDOW_NEXT_WINDOW_X(behind), (WindowPeek)RM(w));
    }
    WINDOW_KIND_X(w) = CWC(userKind);
    WINDOW_VISIBLE_X(w) = visible_p;
    for (t_w = MR(WindowList); t_w && (!WINDOW_VISIBLE(t_w) || ROMlib_system_window_p((WindowPtr)t_w));
         t_w = WINDOW_NEXT_WINDOW(t_w))
        ;
    WINDOW_HILITED_X(w) = visible_p && (t_w == w || ROMlib_system_window_p((WindowPtr)w));
    if (WINDOW_HILITED_X(w) && !ROMlib_system_window_p((WindowPtr)w)) {
        HIDDEN_RAW_SET(CurActivate_H, (WindowPtr)RM(w));
        for (t_w = WINDOW_NEXT_WINDOW(t_w); t_w && !WINDOW_HILITED_X(t_w); t_w = WINDOW_NEXT_WINDOW(t_w))
            ;
    }
    else
        t_w = 0; /* t_w will be used later */
    WINDOW_GO_AWAY_FLAG_X(w) = wind_close_flag;
    WINDOW_SPARE_FLAG_X(w) = 0;
    FIELD_ASSIGN_RAW(/* will be used zoombox (wNew) */
                     WINDOW_DATA_X(w), 0);
    FIELD_ASSIGN_RAW(WINDOW_STRUCT_REGION_X(w), RM(NewRgn()));
    FIELD_ASSIGN_RAW(WINDOW_CONT_REGION_X(w), RM(NewRgn()));
    FIELD_ASSIGN_RAW(WINDOW_UPDATE_REGION_X(w), RM(NewRgn()));
    FIELD_ASSIGN_RAW(WINDOW_DEF_PROC_X(w), RM(GetResource(TICK("WDEF"), proc_id >> 4)));
    uint32 frame_flags = 0;
    if (!PP_RAW(WINDOW_DEF_PROC_X(w))) {
        /* Appearance document and top-title utility variants use different
         grow and zoom bits than the classic WDEF. */
        if (proc_id >= 1024 && proc_id <= 1031)
            proc_id = (proc_id & 1 ? documentProc : noGrowDocProc) | (proc_id & 6 ? ZOOMBIT : 0);
        else if (proc_id >= 1057 && proc_id <= 1071 && (proc_id & 1)) {
            frame_flags = ROMlib_compact_window_frame;
            proc_id = (proc_id & 2 ? documentProc : noGrowDocProc) | (proc_id & 12 ? ZOOMBIT : 0);
        }
        else if (proc_id == 1040)
            proc_id = plainDBox;
        else if (proc_id == 1043 || proc_id == 1045) {
            frame_flags = ROMlib_appearance_dialog_frame;
            proc_id = movableDBoxProc;
        }
        PP_RAW(WINDOW_DEF_PROC_X(w)) = RM(GetResource(TICK("WDEF"), 0));
        if (!PP_RAW(WINDOW_DEF_PROC_X(w))) {
            if (allocated_p)
                DisposPtr((Ptr)w);
            /* fatal_error ("no window (?)"); */
            gui_fatal("Unable to find WDEF.");
        }
    }

    t_aux_w = (AuxWinHandle)NewHandleClear(ROMLIB_AUX_WINDOW_SIZE);
    PP_RAW(HxX(t_aux_w, awNext)) = AuxWinHead;
    PP_RAW(HxX(t_aux_w, awOwner)) = (WindowPtr)RM(w);
    PP_RAW(HxX(t_aux_w, awCTable)) = (CTabHandle)RM(GetResource(TICK("wctb"), 0));
    PP_RAW(HxX(t_aux_w, dialogCItem)) = 0;
    HxX(t_aux_w, awFlags) = CL(((proc_id & 0xF) << 24) | frame_flags);
    PP_RAW(HxX(t_aux_w, awReserved)) = 0;
    HxX(t_aux_w, awRefCon) = 0;
    HIDDEN_RAW_SET(AuxWinHead_H, RM(t_aux_w));

    {
        Handle t;

        PtrToHand((Ptr)title, &t, (LONGINT)title[0] + 1);
        PP_RAW(WINDOW_TITLE_X(w)) = (StringHandle)RM(t);
    }

    if (cwindow_p)
        OpenCPort((CGrafPtr)w);
    else
        OpenPort((GrafPtr)w);
    OffsetRect(&PORT_BOUNDS(w), -CW(bounds->left), -CW(bounds->top));
    PORT_RECT(w) = *bounds;
    OffsetRect(&PORT_RECT(w), -CW(bounds->left), -CW(bounds->top));
    LOCK_HANDLE_EXCURSION_1(WINDOW_TITLE(w), { WINDOW_TITLE_WIDTH_X(w) = CW(StringWidth(STARH(WINDOW_TITLE(w)))); });

    TextFont(applFont);
    PP_RAW(WINDOW_CONTROL_LIST_X(w)) = CWC(0);
    PP_RAW(WINDOW_PIC_X(w)) = CWC(0);
    WINDOW_REF_CON_X(w) = CL(ref_con);
    WINDCALL((WindowPtr)w, wNew, 0);
    if (WINDOW_VISIBLE_X(w)) {
        THEPORT_SAVE_EXCURSION(MR(wmgr_port), {
            WINDCALL((WindowPtr)w, wCalcRgns, 0);
            SetClip(WINDOW_STRUCT_REGION(w));
            ClipAbove(w);
            PenPat(black);
            WINDCALL((WindowPtr)w, wDraw, 0);
            CalcVis(w);
            EraseRgn(WINDOW_CONT_REGION(w));
            CopyRgn(WINDOW_CONT_REGION(w), WINDOW_UPDATE_REGION(w));
            if (ROMlib_window_next_all(w))
                CalcVisBehind(ROMlib_window_next_all(w), WINDOW_STRUCT_REGION(w));
        });
    }
    else
        SetEmptyRgn(PORT_VIS_REGION(w));
    if (t_w) {
        HiliteWindow((WindowPtr)t_w, FALSE);
        HIDDEN_RAW_SET(CurDeactive_H, (WindowPtr)RM(t_w));
    }

    SetPort(save_port);
}

P8(PUBLIC pascal trap, WindowPtr, NewWindow, Ptr, window_storage, Rect*, bounds, StringPtr, title, BOOLEAN, visible_p,
   INTEGER, proc_id, WindowPtr, behind, BOOLEAN, wind_close_flag, LONGINT, ref_con)
{
    WindowPeek w;
    int allocated_p = 0;

    if (!window_storage) {
        allocated_p = 1;
        /* Hack for Dark Castle Demo.  They call NewWindow and expect us to
	 create the storage.  Immediately after calling NewWindow they set
	 the windowKind field to dialogKind.  Later they call UpdateDialog
	 on this window and we die a horrible death since we try to refer
	 to a field that isn't present.  I don't know how they get away with
	 it on the Mac, but I doubt that this particular hack will hurt us
	 elsewhere.  At some point we should find out why it works on the
	 Mac and then get rid of this evil hack.  ctm 97/06/01 */
        {
            int size;

#define DARK_CASTLE_HACK
#if defined(DARK_CASTLE_HACK)
#warning DARK_CASTLE_HACK
            if (strncmp((char*)title + 1, "Modal", 5) == 0)
                size = sizeof(DialogRecord);
            else
                size = sizeof *w;
            w = (WindowPeek)_NewPtr_flags(size, FALSE, TRUE);
#else
            w = (WindowPeek)NewPtr(sizeof *w);
#endif
        }
    }
    else
        w = (WindowPeek)window_storage;

    ROMlib_new_window_common(w, allocated_p, 0, bounds, title, visible_p, proc_id, behind, wind_close_flag, ref_con);
    return (WindowPtr)w;
}

P8(PUBLIC pascal trap, CWindowPtr, NewCWindow, Ptr, window_storage, Rect*, bounds, StringPtr, title, BOOLEAN, visible_p,
   INTEGER, proc_id, CWindowPtr, behind, BOOLEAN, wind_close_flag, LONGINT, ref_con)
{
    WindowPeek w;
    int allocated_p = 0;

    if (!window_storage) {
        allocated_p = 1;
        w = (WindowPeek)NewPtr(sizeof *w);
    }
    else
        w = (WindowPeek)window_storage;

    ROMlib_new_window_common(w, allocated_p, 1, bounds, title, visible_p, proc_id, (WindowPtr)behind, wind_close_flag,
                             ref_con);
    return (CWindowPtr)w;
}

typedef windrestype* windrestypeptr;
MAKE_HIDDEN(windrestypeptr);
typedef HIDDEN_windrestypeptr* windrestypehand;

P3(PUBLIC pascal trap, CWindowPtr, GetNewCWindow, INTEGER, window_id, Ptr, window_storage, CWindowPtr, behind)
{

    CWindowPtr new_cwin;
    windrestypehand win_res;
    Handle win_ctab_res;
    PaletteHandle palette;

    win_res = (windrestypehand)ROMlib_getrestid(TICK("WIND"), window_id);
    if (win_res == NULL)
        return (CWindowPtr)NULL;

    new_cwin = NewCWindow(window_storage, &HxX(win_res, _wrect), (StringPtr)((char*)&HxX(win_res, _wrect) + 18),
                          Hx(win_res, _wvisible) != 0, Hx(win_res, _wprocid), behind, Hx(win_res, _wgoaway) != 0,
                          CL(*(LONGINT*)((char*)&HxX(win_res, _wrect) + 14)));

    win_ctab_res = ROMlib_getrestid(TICK("wctb"), window_id);
    if (win_ctab_res != NULL) {
        THEPORT_SAVE_EXCURSION(thePort, { SetWinColor((WindowPtr)new_cwin, (CTabHandle)win_ctab_res); });
    }

    /* if this is a color window we must check if a palette
     corresponding to this window id exists */

    palette = GetNewPalette(window_id);
    if (palette)
        NSetPalette((WindowPtr)new_cwin, palette, pmAllUpdates);

    return new_cwin;
}

P3(PUBLIC pascal trap, WindowPtr, GetNewWindow, INTEGER, wid, Ptr, wst, WindowPtr, behind)
{
    windrestypehand wh;
    WindowPtr tp;

    wh = (windrestypehand)GetResource(TICK("WIND"), wid);
    if (!wh)
        return (0);
    if (!HIDDEN_RAW((*wh)))
        LoadResource((Handle)wh);
    tp = NewWindow(wst, &(HxX(wh, _wrect)), (StringPtr)((char*)&HxX(wh, _wrect) + 18), Hx(wh, _wvisible) != 0,
                   Hx(wh, _wprocid), (WindowPtr)behind, Hx(wh, _wgoaway) != 0,
                   CL(*(LONGINT*)((char*)&HxX(wh, _wrect) + 14)));
    return (tp);
}

/*
 * NOTE below:  On the Mac+ if after you close a window, the top most
 *		window is non-visible, it will shuffle things.
 */

static void close_window_in_layer(WindowPtr w)
{
    WindowPeek wptmp;
    WindowPeek behind_all = ROMlib_window_next_all((WindowPeek)w);
    GrafPtr savgp;
    MAKE_HIDDEN(AuxWinHandle);
    AuxWinHandle saveauxh;
    HIDDEN_AuxWinHandle* auxhp;
    ControlHandle c, t;

    if (FrontWindow() == w) {
        wptmp = ROMlib_firstvisible((WindowPtr)WINDOW_NEXT_WINDOW(w));
        if (wptmp) {
            HiliteWindow((WindowPtr)wptmp, TRUE);
            HIDDEN_RAW_SET(CurActivate_H, (WindowPtr)RM(wptmp));
        }
    }
    if (MR(WindowList) == (WindowPeek)w) {
        HIDDEN_RAW_SET(WindowList_H, PP_RAW(WINDOW_NEXT_WINDOW_X(w)));
        wptmp = MR(WindowList);
    }
    else {
        for (wptmp = MR(WindowList); wptmp && WINDOW_NEXT_WINDOW(wptmp) != (WindowPeek)w;
             wptmp = WINDOW_NEXT_WINDOW(wptmp))
            ;
        if (wptmp)
            PP_RAW(WINDOW_NEXT_WINDOW_X(wptmp)) = PP_RAW(WINDOW_NEXT_WINDOW_X(w));
    }

    /* notify the palette manager this window has been deleted */
    pm_window_closed(w);

    /* notify the palette manager that the `FrontWindow ()' may have
       changed */
    pm_front_window_maybe_changed_hook();

    /* NOTE: tests have shown that the behaviour implemented below is
       indeed what a Mac+ does */

    /* NOTE: we can't use THEPORT_SAVE_EXCURSION here, becuase of this
       odd behavior */
    savgp = thePort == (GrafPtr)w ? (GrafPtr)MR(wmgr_port) : thePort;
    SetPort(MR(wmgr_port));
    SetClip(MR(GrayRgn));
    PaintBehind(behind_all, WINDOW_STRUCT_REGION(w));
    if (behind_all)
        CalcVisBehind(behind_all, WINDOW_STRUCT_REGION(w));

    DisposeRgn(WINDOW_STRUCT_REGION(w));
    DisposeRgn(WINDOW_CONT_REGION(w));
    DisposeRgn(WINDOW_UPDATE_REGION(w));
    DisposHandle((Handle)WINDOW_TITLE(w));
    for (auxhp = (HIDDEN_AuxWinHandle*)&AuxWinHead_H; HIDDEN_RAW((*auxhp)) && PPR(STARH(STARH(auxhp))->awOwner) != w;
         auxhp = (HIDDEN_AuxWinHandle*)&STARH(STARH(auxhp))->awNext)
        ;
    if (HIDDEN_RAW((*auxhp))) {
        saveauxh = STARH(auxhp);
        HIDDEN_RAW_SET((*auxhp), PP_RAW(STARH(STARH(auxhp))->awNext));
        ROMlib_window_dispose_owned_colors(saveauxh);
        DisposHandle((Handle)saveauxh);
    }

#if defined(NOTAGOODIDEA)
#warning "what the hell does this mean?! DANGER WILL ROBINSON!"
    Cx (*(Ptr*)Cx)(((WindowPeek)w)->windowDefProc) = 0;
    DisposHandle(Cx(((WindowPeek)w)->windowDefProc));
#endif /* NOTAGOODIDEA */

/*
 * TODO: Fix this.  Tests on the mac show that KillControls is called,
 * but just replacing the for loop causes many apps to die.  It could
 * be because some window information that DisposeControl wants is
 * destroyed already, or it could be DisposeControl or KillControl
 * makes some FALSE assumptions.  More tests need to be written.
 */
#if 1
    for (c = WINDOW_CONTROL_LIST(w); c;) {
        t = c;
        c = HxP(c, nextControl);
#if 0
	DisposHandle(Hx(t, contrlDefProc));
#endif /* 0 */
        DisposHandle((Handle)t);
    }
#else  /* 0 */
    KillControls(w);
#endif /* 0 */

    if (PP_RAW(WINDOW_PIC_X(w)))
        KillPicture(WINDOW_PIC(w));
    ClosePort((GrafPtr)w);
    SetPort(savgp);
    if (MR(CurActivate) == w)
        HIDDEN_RAW_SET(CurActivate_H, 0);
    if (MR(CurDeactive) == w)
        HIDDEN_RAW_SET(CurDeactive_H, 0);
    WINDCALL((WindowPtr)w, wDispose, 0);
}

P1(PUBLIC pascal trap, void, CloseWindow, WindowPtr, w)
{
    void *parent = ROMlib_layer_parent(w), *old = NULL;
    if (parent)
        old = ROMlib_layer_swap(parent);
    close_window_in_layer(w);
    if (old)
        ROMlib_layer_set(old);
}

P1(PUBLIC pascal trap, void, DisposeWindow, WindowPtr, w)
{
    CloseWindow(w);
    ROMlib_system_window_forget(w);
    DisposPtr((Ptr)w);
}
