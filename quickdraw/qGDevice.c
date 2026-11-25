/*+===================================================================
  File:      qGDevice.c

  Summary:   Implements q gdevice support for Macintosh QuickDraw graphics.

  Classes:   None.

  Functions: ROMlib_device_loop, ROMlib_resize_screen, gd_allocate_main_device, gd_set_bpp

  Origin:    Existing repository source in executor/src/quickdraw; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1994, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"
#include "QuickDraw.h"
#include "CQuickDraw.h"
#include "MemoryMgr.h"
#include "WindowMgr.h"
#include "MenuMgr.h"
#include "SysErr.h"

#include "rsys/cquick.h"
#include "rsys/wind.h"
#include "rsys/layers.h"
#include "rsys/host.h"
#include "rsys/vdriver.h"
#include "rsys/blockinterrupts.h"
#include "rsys/flags.h"
#include "rsys/autorefresh.h"
#include "rsys/redrawscreen.h"
#include "rsys/romaw.h"
#include "rsys/dirtyrect.h"

/*
 * determined experimentally -- this fix is needed for Energy Scheming because
 * they put a bogus test for mode >= 8 in their code to detect color.  NOTE:
 * the value that will get stored is most definitely not 8 or anything near
 * 8, since 8bpp will be translated to 0x83.
 */

PRIVATE LONGINT mode_from_bpp(int bpp)
{
    LONGINT retval;
    int log2;

    if (bpp >= 1 && bpp <= 32)
        log2 = ROMlib_log2[bpp];
    else
        log2 = -1;

    if (log2 != -1)
        retval = 0x80 | ROMlib_log2[bpp];
    else {
        warning_unexpected("bpp = %d", bpp);
        retval = 0x86; /* ??? */
    }
    return retval;
}

void gd_allocate_main_device(void)
{
    GDHandle graphics_device;

    if (vdriver_fbuf == NULL)
        gui_fatal("vdriver not initialized, unable to allocate `MainDevice'");

    ZONE_SAVE_EXCURSION(SysZone, {
        PixMapHandle gd_pixmap;
        Rect* gd_rect;

        graphics_device = NewGDevice(/* no driver */ 0, mode_from_bpp(vdriver_bpp));

        /* we are the main device, since there are currently no others */
        HIDDEN_RAW_SET(TheGDevice_H, HIDDEN_RAW_SET(MainDevice_H, RM(graphics_device)));

        /* set gd flags reflective of the main device */
        GD_FLAGS_X (graphics_device) |= CWC (  (1 << mainScreen)
					    | (1 << screenDevice)
					    | (1 << screenActive)
					      /* PacMan Deluxe avoids
						 GDevices with noDriver
						 set.  Looking around
						 Apple's site shows that
						 people set this bit when
						 they're creating offscreen
						 gDevices.  It's not clear
						 whether or not we should
						 be setting this bit.
					    | (1 << noDriver) */ );

        gd_set_bpp(graphics_device, !vdriver_grayscale_p, vdriver_fixed_clut_p, vdriver_bpp);

        gd_pixmap = GD_PMAP(graphics_device);
        PIXMAP_SET_ROWBYTES_X(gd_pixmap, CW(vdriver_row_bytes));
        FIELD_ASSIGN_RAW(PIXMAP_BASEADDR_X(gd_pixmap), (Ptr)RM(vdriver_fbuf));

        gd_rect = &GD_RECT(graphics_device);
        gd_rect->top = gd_rect->left = CWC(0);
        gd_rect->bottom = CW(vdriver_height);
        gd_rect->right = CW(vdriver_width);
        PIXMAP_BOUNDS(gd_pixmap) = *gd_rect;

        /* add ourselves to the device list */
        FIELD_ASSIGN_RAW(GD_NEXT_GD_X(graphics_device), DeviceList);
        HIDDEN_RAW_SET(DeviceList_H, RM(graphics_device));

        /* Assure that we're using the correct colors. */
        vdriver_set_colors(0, 1 << vdriver_bpp, CTAB_TABLE(PIXMAP_TABLE(GD_PMAP(graphics_device))));
    });
}

PRIVATE void resize_screen_port(GrafPtr port, Ptr old_base)
{
    Rect* bounds;

    if (!port || PP_RAW(PORT_BASEADDR_X(port)) != old_base)
        return;
    if (CGrafPort_p(port))
        FIELD_ASSIGN_RAW(PIXMAP_BASEADDR_X(CPORT_PIXMAP(port)), (Ptr)RM(vdriver_fbuf));
    else
        FIELD_ASSIGN_RAW(PORT_BITS(port).baseAddr, (Ptr)RM(vdriver_fbuf));
    bounds = &PORT_BOUNDS(port);
    bounds->right = CW(CW(bounds->left) + vdriver_width);
    bounds->bottom = CW(CW(bounds->top) + vdriver_height);
    if (CGrafPort_p(port))
        PIXMAP_SET_ROWBYTES_X(CPORT_PIXMAP(port), CW(vdriver_row_bytes));
    else
        PORT_BITS(port).rowBytes = screenBitsX.rowBytes;
}

/* Called at an application event boundary, never from the SDL event filter. */
boolean_t ROMlib_resize_screen(int width, int height)
{
    GDHandle device;
    PixMapHandle pixmap;
    Ptr old_base;
    WindowPeek window;
    RgnHandle screen_region;
    Rect bounds;
    int old_width = vdriver_width, old_height = vdriver_height;
    virtual_int_state_t int_state;

    if (width == old_width && height == old_height)
        return TRUE;
    /* A new application's ports do not exist yet.  Set the device geometry
     before InitGraf copies it, without walking the previous app's ports. */
    if (MainDevice && QDExist == EXIST_NO && WWExist == EXIST_NO) {
        int_state = block_virtual_ints();
        if (!vdriver_set_mode(width, height, vdriver_bpp, vdriver_grayscale_p)) {
            restore_virtual_ints(int_state);
            return FALSE;
        }
        device = MR(MainDevice);
        pixmap = GD_PMAP(device);
        C_SetRect(&bounds, 0, 0, vdriver_width, vdriver_height);
        GD_RECT(device) = bounds;
        PIXMAP_BOUNDS(pixmap) = bounds;
        FIELD_ASSIGN_RAW(PIXMAP_BASEADDR_X(pixmap), (Ptr)RM(vdriver_fbuf));
        PIXMAP_SET_ROWBYTES_X(pixmap, CW(vdriver_row_bytes));
        vdriver_set_colors(0, 1 << vdriver_bpp, CTAB_TABLE(PIXMAP_TABLE(pixmap)));
        /* InitGraf will populate the application's A5-relative globals. */
        HIDDEN_RAW_SET(ScrnBase_H, PP_RAW(PIXMAP_BASEADDR_X(pixmap)));
        ScreenRow = CW(vdriver_row_bytes / vdriver_bpp);
        CrsrPin = bounds;
        restore_virtual_ints(int_state);
        return TRUE;
    }
    if (!MainDevice || QDExist != EXIST_YES || WWExist != EXIST_YES)
        return FALSE;
    int_state = block_virtual_ints();
    HideCursor();
    dirty_rect_update_screen();
    device = MR(MainDevice);
    pixmap = GD_PMAP(device);
    FIELD_ASSIGN_RAW(old_base, PIXMAP_BASEADDR_X(pixmap));
    if (!vdriver_set_mode(width, height, vdriver_bpp, vdriver_grayscale_p)) {
        ShowCursor();
        restore_virtual_ints(int_state);
        return FALSE;
    }

    SetRect(&bounds, 0, 0, vdriver_width, vdriver_height);
    GD_RECT(device) = PIXMAP_BOUNDS(pixmap) = bounds;
    FIELD_ASSIGN_RAW(PIXMAP_BASEADDR_X(pixmap), (Ptr)RM(vdriver_fbuf));
    PIXMAP_SET_ROWBYTES_X(pixmap, CW(vdriver_row_bytes));
    FIELD_ASSIGN_RAW(screenBitsX.baseAddr, PIXMAP_BASEADDR_X(pixmap));
    screenBitsX.bounds = bounds;
    screenBitsX.rowBytes = CW(vdriver_row_bytes / vdriver_bpp);
    HIDDEN_RAW_SET(ScrnBase_H, PP_RAW(screenBitsX.baseAddr));
    ScreenRow = screenBitsX.rowBytes;
    CrsrPin = bounds;
    MouseLocation.h = CW(MAX(0, MIN(CW(MouseLocation.h), vdriver_width - 1)));
    MouseLocation.v = CW(MAX(0, MIN(CW(MouseLocation.v), vdriver_height - 1)));
    MouseLocation2 = MTemp = MouseLocation;

    resize_screen_port(thePort, old_base);
    resize_screen_port(MR(WMgrPort), old_base);
    resize_screen_port(MR(wmgr_port), old_base);
    PORT_RECT(MR(WMgrPort)) = PORT_RECT(MR(wmgr_port)) = bounds;
    for (window = ROMlib_window_first_all(); window; window = ROMlib_window_next_all(window))
        resize_screen_port(WINDOW_PORT(window), old_base);

    SetRectRgn(MR(GrayRgn), 0, CW(MBarHeight), vdriver_width, vdriver_height);
    RectRgn(PORT_VIS_REGION(MR(WMgrPort)), &bounds);
    RectRgn(PORT_VIS_REGION(MR(wmgr_port)), &bounds);
    CopyRgn(MR(GrayRgn), PORT_CLIP_REGION(MR(WMgrPort)));
    CopyRgn(MR(GrayRgn), PORT_CLIP_REGION(MR(wmgr_port)));
    for (window = ROMlib_window_first_all(); window; window = ROMlib_window_next_all(window))
        CalcVis(window);

    screen_region = NewRgn();
    RectRgn(screen_region, &bounds);
    THEGDEVICE_SAVE_EXCURSION(device, {
        vdriver_set_colors(0, 1 << vdriver_bpp, CTAB_TABLE(PIXMAP_TABLE(pixmap)));
        PaintBehind(ROMlib_window_first_all(), screen_region);
        DrawMenuBar();
    });
    DisposeRgn(screen_region);
    note_romaw_changed_screen(0, vdriver_height);
    cursor_reset_current_cursor();
    ShowCursor();
    vdriver_update_screen(0, 0, vdriver_height, vdriver_width, FALSE);
    restore_virtual_ints(int_state);
    fprintf(stderr, "trace: screen resized %dx%d -> %dx%d rowBytes=%d\n", old_width, old_height, vdriver_width,
            vdriver_height, vdriver_row_bytes);
    return TRUE;
}

P2(PUBLIC pascal trap, GDHandle, NewGDevice, INTEGER, ref_num, LONGINT, mode)
{
    GDHandle this;
    Handle h;
    PixMapHandle pmh;

    ZONE_SAVE_EXCURSION(SysZone, {
        this = (GDHandle)NewHandle((Size)sizeof(GDevice));

        if (this == NULL)
            return this;

        /* initialize fields; for some of these, i'm not sure what
	  the value should be */
        GD_ID_X(this) = CWC(0); /* ??? */

        /* CLUT graphics device by default */
        GD_TYPE_X(this) = CWC(clutType);

        /* how do i allocate a new inverse color table? */
        h = RM(NewHandle(0));
        FIELD_ASSIGN_RAW(GD_ITABLE_X(this), (ITabHandle)h);
        GD_RES_PREF_X(this) = CWC(DEFAULT_ITABLE_RESOLUTION);

        FIELD_ASSIGN_RAW(GD_SEARCH_PROC_X(this), RM(NULL));
        FIELD_ASSIGN_RAW(GD_COMP_PROC_X(this), RM(NULL));

        GD_FLAGS_X(this) = CWC(0);
        /* mode_from_bpp (1)  indicates b/w hardware */
        if (mode != mode_from_bpp(1))
            GD_FLAGS_X(this) |= CWC(1 << gdDevType);

        pmh = RM(NewPixMap());
        FIELD_ASSIGN_RAW(GD_PMAP_X(this), pmh);
        CTAB_FLAGS_X(PIXMAP_TABLE(GD_PMAP(this))) |= CTAB_GDEVICE_BIT_X;

        GD_REF_CON_X(this) = CWC(0);      /* ??? */
        GD_REF_NUM_X(this) = CW(ref_num); /* ??? */
        GD_MODE_X(this) = CL(mode);       /* ??? */

        FIELD_ASSIGN_RAW(GD_NEXT_GD_X(this), RM(NULL));

        GD_RECT(this).top = CWC(0);
        GD_RECT(this).left = CWC(0);
        GD_RECT(this).bottom = CWC(0);
        GD_RECT(this).right = CWC(0);

        /* handle to cursor's expanded data/mask? */
        GD_CCBYTES_X(this) = CWC(0);
        GD_CCDEPTH_X(this) = CWC(0);
        FIELD_ASSIGN_RAW(GD_CCXDATA_X(this), RM(NULL));
        FIELD_ASSIGN_RAW(GD_CCXMASK_X(this), RM(NULL));

        GD_RESERVED_X(this) = CWC(0);

        /* if mode is -1, this is a user created gdevice,
	  and InitGDevice () should not be called (IMV-122) */
        if (mode != -1)
            InitGDevice(ref_num, mode, this);
    });

    return this;
}

void gd_set_bpp(GDHandle gd, boolean_t color_p, boolean_t fixed_p, int bpp)
{
    PixMapHandle gd_pixmap;
    boolean_t main_device_p = (gd == MR(MainDevice));

    /* set the color bit, all other flag bits should be the same */
    if (color_p)
        GD_FLAGS_X(gd) |= CWC(1 << gdDevType);
    else
        GD_FLAGS_X(gd) &= CWC(~(1 << gdDevType));

    GD_TYPE_X(gd) = (bpp > 8 ? directType : (fixed_p ? CWC(fixedType) : CWC(clutType)));

    gd_pixmap = GD_PMAP(gd);
    pixmap_set_pixel_fields(STARH(gd_pixmap), bpp);

    if (bpp <= 8) {
        CTabHandle gd_color_table;

        gd_color_table = PIXMAP_TABLE(gd_pixmap);

        if (fixed_p) {
            if (!main_device_p)
                gui_fatal("unable to set bpp of gd not the screen");
            else {
                CTabHandle gd_color_table;

                gd_color_table = PIXMAP_TABLE(gd_pixmap);
                SetHandleSize((Handle)gd_color_table, CTAB_STORAGE_FOR_SIZE((1 << bpp) - 1));
                CTAB_SIZE_X(gd_color_table) = CW((1 << bpp) - 1);
                vdriver_get_colors(0, 1 << bpp, CTAB_TABLE(gd_color_table));

                CTAB_SEED_X(gd_color_table) = CL(GetCTSeed());
            }
        }
        else {
            CTabHandle temp_color_table;

            temp_color_table = GetCTable(color_p ? bpp : (bpp + 32));
            if (temp_color_table == NULL)
                gui_fatal("unable to get color table `%d'", color_p ? bpp : (bpp + 32));
            ROMlib_copy_ctab(temp_color_table, gd_color_table);
            DisposCTable(temp_color_table);
        }

        CTAB_FLAGS_X(gd_color_table) = CTAB_GDEVICE_BIT_X;
        MakeITable(gd_color_table, GD_ITABLE(gd), GD_RES_PREF(gd));

        if (main_device_p && !fixed_p)
            vdriver_set_colors(0, 1 << bpp, CTAB_TABLE(gd_color_table));
    }
}

/* it seems that `gd_ref_num' describes which device to initialize,
   and `mode' tells it what mode to start it in

   i'm not sure how this relates to NeXT/vga video hardware, for now,
   we do nothing */
P3(PUBLIC pascal trap, void, InitGDevice, INTEGER, gd_ref_num, LONGINT, mode, GDHandle, gdh)
{
}

P3(PUBLIC pascal trap, void, SetDeviceAttribute, GDHandle, gdh, INTEGER, attribute, BOOLEAN, value)
{
    if (value)
        GD_FLAGS_X(gdh) |= CW(1 << attribute);
    else
        GD_FLAGS_X(gdh) &= CW(~(1 << attribute));
}

P1(PUBLIC pascal trap, void, SetGDevice, GDHandle, gdh)
{
    if (TheGDevice != RM(gdh)) {
        HIDDEN_RAW_SET(TheGDevice_H, RM(gdh));
        ROMlib_invalidate_conversion_tables();
    }
}

P1(PUBLIC pascal trap, void, DisposGDevice, GDHandle, gdh)
{
    DisposHandle((Handle)GD_ITABLE(gdh));
    DisposPixMap(GD_PMAP(gdh));

    /* FIXME: do other cleanup */
    DisposHandle((Handle)gdh);
}

P0(PUBLIC pascal trap, GDHandle, GetGDevice)
{
    return MR(TheGDevice);
}

P0(PUBLIC pascal trap, GDHandle, GetDeviceList)
{
    return MR(DeviceList);
}

P0(PUBLIC pascal trap, GDHandle, GetMainDevice)
{
    GDHandle retval;

    retval = MR(MainDevice);

    /* Unfortunately, Realmz winds up dereferencing non-existent
     memory unless noDriver is set, but PacMan Deluxe will have
     trouble if that bit is set. */

    if (ROMlib_creator == TICK("RLMZ"))
        GD_FLAGS_X(retval) |= CWC(1 << noDriver);
    else
        GD_FLAGS_X(retval) &= ~CWC(1 << noDriver);

    return retval;
}

P1(PUBLIC pascal trap, GDHandle, GetMaxDevice, Rect*, globalRect)
{
    /* FIXME:
     currently we have only a single device, so that has
     the max pixel depth for any given region (tho we would
     probably see if it intersects the main screen and return
     NULL otherwise */
    return MR(MainDevice);
}

P1(PUBLIC pascal trap, GDHandle, GetNextDevice, GDHandle, cur_device)
{
    return GD_NEXT_GD(cur_device);
}

OSErr ROMlib_device_loop(RgnHandle rgn, ROMlib_device_draw_proc draw, void* data, DeviceLoopFlags flags)
{
    /* SuperMario QuickDraw/DeviceLoop.a primes an offscreen loop with the
       current GDevice, not DeviceList. A single device calls the drawing proc
       directly without replacing visRgn. Otherwise offscreen controls can be
       clipped to the physical screen and receive the wrong pixel depth. */
    GDHandle first = MR(TheGDevice) != MR(MainDevice) ? MR(TheGDevice) : MR(DeviceList);
    if (first && !GD_NEXT_GD(first)) {
        draw(PIXMAP_PIXEL_SIZE(GD_PMAP(first)), GD_FLAGS(first), first, data);
        return noErr;
    }

    GrafPtr port = thePort;
    RgnHandle saved_vis_raw = PP_RAW(PORT_VIS_REGION_X(port));
    RgnHandle saved_vis = PORT_VIS_REGION(port);
    RgnHandle sect_rgn = NewRgn();
    if (!sect_rgn)
        return memFullErr;
    RgnHandle gd_rect_rgn = NewRgn();
    if (!gd_rect_rgn) {
        DisposeRgn(sect_rgn);
        return memFullErr;
    }

    /* Loop over all GDevices, looking for active screens. */
    for (GDHandle gd = first; gd; gd = GD_NEXT_GD(gd))
        if ((GD_FLAGS_X(gd) & CWC((1 << screenDevice) | (1 << screenActive))) ==
            CWC((1 << screenDevice) | (1 << screenActive))) {
            /* NOTE: I'm blowing off some flags that come into play when
	 * you have multiple screens.  I don't think anything terribly
	 * bad would happen even if we had multiple screens, but we
	 * don't.  We can worry about it later.
	 */

            if (!(flags & allDevices)) {
                Rect gd_rect, *port_bounds;

                /* Map the GDevice rect into thePort local coordinates. */
                gd_rect = GD_RECT(gd);
                port_bounds = &(PORT_BOUNDS(port));
                OffsetRect(&gd_rect, CW(port_bounds->left), CW(port_bounds->top));

                /* Intersect GDevice rect with the specified region. */
                RectRgn(gd_rect_rgn, &gd_rect);
                SectRgn(gd_rect_rgn, rgn, sect_rgn);

                SectRgn(sect_rgn, saved_vis, sect_rgn);

                /* Save it away in thePort. */
                FIELD_ASSIGN_RAW(PORT_VIS_REGION_X(port), RM(sect_rgn));
            }

            if ((flags & allDevices) || !EmptyRgn(sect_rgn))
                draw(PIXMAP_PIXEL_SIZE(GD_PMAP(gd)), GD_FLAGS(gd), gd, data);
        }

    FIELD_ASSIGN_RAW(PORT_VIS_REGION_X(port), saved_vis_raw);

    DisposeRgn(gd_rect_rgn);
    DisposeRgn(sect_rgn);
    return noErr;
}

typedef struct {
    DeviceLoopDrawingProcPtr procedure;
    LONGINT user_data;
} pascal_device_draw;

static void draw_pascal_device(INTEGER depth, INTEGER flags, GDHandle device, void* context)
{
    const pascal_device_draw* draw = context;
    CToPascalCall(draw->procedure, CTOP_DeviceLoopDrawingProcTemplate, depth, flags, device, draw->user_data);
}

P4(PUBLIC pascal trap, void, DeviceLoop, RgnHandle, rgn, DeviceLoopDrawingProcPtr, drawing_proc, LONGINT, user_data,
   DeviceLoopFlags, flags)
{
    pascal_device_draw draw = {drawing_proc, user_data};
    ROMlib_device_loop(rgn, draw_pascal_device, &draw, flags);
}

P4(PUBLIC pascal trap, void, DeviceLoopDrawingProcTemplate, INTEGER, depth, INTEGER, deviceFlags, GDHandle,
   targetDevice, LONGINT, userData)
{
    /* Never used: only a dummy function to generate ctopflags! */
}

P2(PUBLIC pascal trap, BOOLEAN, TestDeviceAttribute, GDHandle, gdh, INTEGER, attribute)
{
    BOOLEAN retval;

    retval = (GD_FLAGS(gdh) & (1 << attribute)) != 0;
    return retval;
}

P2(PUBLIC pascal trap, void, ScreenRes, INTEGER*, h_res, INTEGER*, v_res)
{
    *h_res = PIXMAP_HRES_X(GD_PMAP(MR(MainDevice)));
    *v_res = PIXMAP_VRES_X(GD_PMAP(MR(MainDevice)));
}

P4(PUBLIC pascal trap, INTEGER, HasDepth, GDHandle, gdh, INTEGER, bpp, INTEGER, which_flags, INTEGER, flags)
{
    flags &= ~1;
    which_flags &= ~1;

    if (gdh != MR(MainDevice) || bpp == 0)
        return FALSE;

    return (vdriver_acceptable_mode_p(
        0, 0, bpp,
        ((which_flags & (1 << gdDevType)) ? (flags & (1 << gdDevType)) == (1 << gdDevType) : vdriver_grayscale_p),
        FALSE));
}

P4(PUBLIC pascal trap, OSErr, SetDepth, GDHandle, gdh, INTEGER, bpp, INTEGER, which_flags, INTEGER, flags)
{
    PixMapHandle gd_pixmap;
    WindowPeek tw;
    virtual_int_state_t int_state;

    if (gdh != MR(MainDevice)) {
        warning_unexpected("Setting the depth of a device not the screen; "
                           "this violates bogus assumptions in SetDepth.");
    }

    gd_pixmap = GD_PMAP(gdh);

    if (bpp == PIXMAP_PIXEL_SIZE(gd_pixmap))
        return noErr;

    int_state = block_virtual_ints();
    HideCursor();

    note_romaw_changed_screen(0, vdriver_height);

    if (bpp == 0 ||
        !vdriver_set_mode(0, 0, bpp,
                          ((which_flags & (1 << gdDevType)) ? !(flags & (1 << gdDevType)) : vdriver_grayscale_p))) {
        /* IMV says this returns non-zero in error case; not positive
	 cDepthErr is correct; need to verify */
        ShowCursor();
        restore_virtual_ints(int_state);
        return cDepthErr;
    }

    if (vdriver_fbuf == NULL)
        gui_fatal("vdriver not initialized, unable to change bpp");
    gd_set_bpp(gdh, !vdriver_grayscale_p, vdriver_fixed_clut_p, bpp);

    PIXMAP_SET_ROWBYTES_X(gd_pixmap, CW(vdriver_row_bytes));
    screenBitsX.rowBytes = CW(vdriver_row_bytes);

    cursor_reset_current_cursor();

    ShowCursor();
    restore_virtual_ints(int_state);

    /* FIXME: assuming (1) all windows are on the current
     graphics device, and (2) the rowbytes and baseaddr
     of the gdevice cannot change */
    /* set the pixel size, rowbytes, etc
     of windows and the window manager color graphics port */

    if (WWExist == EXIST_YES) {
        for (tw = ROMlib_window_first_all(); tw; tw = ROMlib_window_next_all(tw)) {
            GrafPtr gp;

            gp = WINDOW_PORT(tw);

            if (CGrafPort_p(gp)) {
                PixMapHandle window_pixmap;

                window_pixmap = CPORT_PIXMAP(gp);
                PIXMAP_PIXEL_SIZE_X(window_pixmap) = PIXMAP_PIXEL_SIZE_X(gd_pixmap);
                PIXMAP_CMP_SIZE_X(window_pixmap) = PIXMAP_PIXEL_SIZE_X(gd_pixmap);
                PIXMAP_SET_ROWBYTES_X(window_pixmap, PIXMAP_ROWBYTES_X(gd_pixmap));

                ROMlib_copy_ctab(PIXMAP_TABLE(gd_pixmap), PIXMAP_TABLE(window_pixmap));
                THEPORT_SAVE_EXCURSION(gp, {
                    RGBForeColor(&CPORT_RGB_FG_COLOR(gp));
                    RGBBackColor(&CPORT_RGB_BK_COLOR(gp));
                });
            }
            else {
                BITMAP_SET_ROWBYTES_X(&PORT_BITS(gp), PIXMAP_ROWBYTES_X(gd_pixmap));
            }
        }

        /* do the same for the WMgrCPort */
        {
            PixMapHandle wmgr_cport_pixmap;

            wmgr_cport_pixmap = CPORT_PIXMAP(MR(WMgrCPort));

            PIXMAP_PIXEL_SIZE_X(wmgr_cport_pixmap) = PIXMAP_PIXEL_SIZE_X(gd_pixmap);
            PIXMAP_CMP_SIZE_X(wmgr_cport_pixmap) = PIXMAP_PIXEL_SIZE_X(gd_pixmap);
            PIXMAP_SET_ROWBYTES_X(wmgr_cport_pixmap, PIXMAP_ROWBYTES_X(gd_pixmap));

            ROMlib_copy_ctab(PIXMAP_TABLE(gd_pixmap), PIXMAP_TABLE(wmgr_cport_pixmap));
        }

        THEPORT_SAVE_EXCURSION(MR(wmgr_port), {
            RGBForeColor(&ROMlib_black_rgb_color);
            RGBBackColor(&ROMlib_white_rgb_color);
        });
    }

    /* Redraw the screen if that's what changed. */
    if (gdh == MR(MainDevice))
        redraw_screen();

    return noErr;
}
