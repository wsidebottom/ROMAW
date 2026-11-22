/*+===================================================================
  File:      qGWorld.c

  Summary:   Implements q gworld support for Macintosh QuickDraw graphics.

  Classes:   None.

  Functions: ROMlib_InitGWorlds, ROMlib_new_gworld, ROMlib_new_gworld_from_ptr, delete_gw_info, lookup_gw_info_by_gw,
             lookup_gw_info_by_gw_gd_pixmap, lookup_gw_info_by_gw_pixmap, lookup_gw_info_by_gw_pixmap_baseaddr

  Origin:    Existing repository source in executor/src/quickdraw; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1994 - 1998 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"
#include "QuickDraw.h"
#include "CQuickDraw.h"
#include "MemoryMgr.h"
#include "OSUtil.h"

#include "rsys/cquick.h"
#include "rsys/gworld.h"
#include "rsys/host.h"
#include "rsys/mman.h"
#include "rsys/qcolor.h"

gw_info_t *gw_info_head, *gw_info_free;

void ROMlib_InitGWorlds(void)
{
    gw_info_head = NULL;
    gw_info_free = NULL;
}

#define LOOKUP_GW_INFO_BY_EXPR(expr, value)      \
    {                                            \
        gw_info_t* t;                            \
                                                 \
        for (t = gw_info_head; t; t = t->next) { \
            if (expr == value) {                 \
                return t;                        \
            }                                    \
        }                                        \
        return NULL;                             \
    }

gw_info_t* lookup_gw_info_by_gw(GWorldPtr gw)
{
    LOOKUP_GW_INFO_BY_EXPR(t->gw, gw);
}

gw_info_t* lookup_gw_info_by_gw_pixmap(PixMapHandle gw_pixmap)
{
    LOOKUP_GW_INFO_BY_EXPR(t->gw_pixmap, gw_pixmap);
}

gw_info_t* lookup_gw_info_by_gw_pixmap_baseaddr(void* baseaddr)
{
    LOOKUP_GW_INFO_BY_EXPR(PIXMAP_BASEADDR(t->gw_pixmap), baseaddr);
}

gw_info_t* lookup_gw_info_by_gw_gd_pixmap(PixMapHandle gw_gd_pixmap)
{
    LOOKUP_GW_INFO_BY_EXPR(t->gw_gd_pixmap, gw_gd_pixmap);
}

static void install_new_gw_info(GWorldPtr graphics_world, GDHandle graphics_world_device, GWorldFlags flags,
                                int gd_allocated_p)
{
    gw_info_t* new_gw_info;

    if (gw_info_free) {
        new_gw_info = gw_info_free;
        gw_info_free = gw_info_free->next;
    }
    else {
        new_gw_info = (gw_info_t*)NewPtr(sizeof *new_gw_info);
    }

    new_gw_info->next = gw_info_head;
    gw_info_head = new_gw_info;

    new_gw_info->gw = graphics_world;
    new_gw_info->gw_pixmap = CPORT_PIXMAP(graphics_world);
    new_gw_info->gw_gd = graphics_world_device;
    new_gw_info->gw_gd_pixmap = GD_PMAP(graphics_world_device);
    new_gw_info->flags = flags;
    new_gw_info->pixel_lock_count = 0;
    new_gw_info->gd_allocated_p = gd_allocated_p;
}

void delete_gw_info(gw_info_t* gw_info)
{
    gw_info_t* t;

    if (gw_info == gw_info_head) {
        gw_info_head = gw_info->next;
    }
    else {
        for (t = gw_info_head; t; t = t->next) {
            if (t->next == gw_info) {
                t->next = t->next->next;
                break;
            }
        }
        if (t == NULL)
            gui_fatal("gw_info `%p' not in info chain", gw_info);
    }

    gw_info->next = gw_info_free;
    gw_info_free = gw_info;
}

PRIVATE int gworld_row_bytes(const Rect* bounds, INTEGER depth, boolean_t padded)
{
    int bytes = (RECT_WIDTH(bounds) * depth + 7) / 8;
    if (!padded)
        return (bytes + 3) & ~3;
    /* Some PowerPC clients request narrower worlds and repack their locked
     pixels to the next 16-byte boundary, including already aligned rows. */
    int aligned = (bytes + 16) & ~15;
    return aligned < 0x4000 ? aligned : (bytes + 3) & ~3;
}

PUBLIC QDErr ROMlib_new_gworld(HIDDEN_GWorldPtr* graphics_world_out, INTEGER depth, Rect* bounds, CTabHandle ctab,
                               GDHandle gw_gd, GWorldFlags flags, boolean_t padded)
{
    PixMapHandle gw_pixmap, gd_pixmap;
    GWorldPtr graphics_world;
    GrafPtr save_portX;
    int gd_allocated_p = FALSE;

    if (!graphics_world_out || !bounds)
        return paramErr;
    /* Accept full-color requests expressed as 24 color bits, using the
       existing QuickDraw 32-bit RGB storage rather than packed RGB bytes. */
    if (depth == 24)
        depth = 32;
    /* Applications may probe unsupported depths before falling back to a
       supported format. Reject these before allocating a partial world. */
    if (depth != 0 && depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16 && depth != 32)
        return cDepthErr;

    save_portX = thePortX;

    if (!depth) {
        GDHandle max_gd;

        max_gd = GetMaxDevice(bounds);
        if (max_gd == NULL)
            return paramErr;
        gw_gd = max_gd;
        depth = PIXMAP_PIXEL_SIZE(GD_PMAP(max_gd));
        if (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16 && depth != 32)
            return cDepthErr;

        if (bounds->top || bounds->left) {
            Rect* temp_bounds = alloca(sizeof *temp_bounds);

            temp_bounds->bottom = CW(RECT_HEIGHT(bounds));
            temp_bounds->right = CW(RECT_WIDTH(bounds));

            temp_bounds->top = temp_bounds->left = CWC(0);

            bounds = temp_bounds;
        }
    }

#if defined(BASIC_QD_ONLY)
    gui_assert(depth == 1 && gw_gd == NULL);
#endif /* BASIC_QD_ONLY */

    /* tests show that if we allocate the gdevice, then the gdevice's
     baseAddr is the graphics world's locked handle.

     but if the user passes the gdevice in, the pixmap is allocated
     its own handle */

    if (!(flags & noNewDevice)) {
        Handle gd_pixmap_baseaddr;
        int row_bytes;

        gw_gd = NewGDevice(/* dummy */ 1,
                           /* don't initialize */ -1);
        gd_allocated_p = TRUE;
        /* set the various fields of the newly created graphics
	 device */
        GD_RECT(gw_gd) = *bounds;

        gd_pixmap = GD_PMAP(gw_gd);

        row_bytes = gworld_row_bytes(bounds, depth, padded);
        PIXMAP_SET_ROWBYTES_X(gd_pixmap, CW(row_bytes));

        gd_pixmap_baseaddr = NewHandle(row_bytes * RECT_HEIGHT(bounds));
        if (MemError() != noErr)
            return MemError();

        FIELD_ASSIGN_RAW(PIXMAP_BASEADDR_X(gd_pixmap), (Ptr)RM(gd_pixmap_baseaddr));

        PIXMAP_BOUNDS(gd_pixmap) = *bounds;
        if (depth > 8) {
            PIXMAP_PIXEL_TYPE_X(gd_pixmap) = CWC(RGBDirect);
            PIXMAP_CMP_COUNT_X(gd_pixmap) = CWC(3);
            switch (depth) {
            case 16:
                PIXMAP_CMP_SIZE_X(gd_pixmap) = CWC(5);
                break;
            case 32:
                PIXMAP_CMP_SIZE_X(gd_pixmap) = CWC(8);
                break;
            default:
                gui_fatal("unknown pixel size");
            }
        }
        else {
            PIXMAP_PIXEL_TYPE_X(gd_pixmap) = CWC(0);
            PIXMAP_CMP_COUNT_X(gd_pixmap) = CWC(1);
            PIXMAP_CMP_SIZE_X(gd_pixmap) = CW(depth);
        }
        PIXMAP_PIXEL_SIZE_X(gd_pixmap) = CW(depth);
        if (ctab == NULL && depth <= 8) {
            int ctab_max_elt = (1 << depth) - 1;

            ctab = PIXMAP_TABLE(gd_pixmap);

            SetHandleSize((Handle)ctab, CTAB_STORAGE_FOR_SIZE(ctab_max_elt));
            CTAB_SIZE_X(ctab) = CW(ctab_max_elt);
            CTAB_SEED_X(ctab) = CL((int32)depth);
            CTAB_FLAGS_X(ctab) = CTAB_GDEVICE_BIT_X;
            memcpy(CTAB_TABLE(ctab), default_ctab_colors[ROMlib_log2[depth]], sizeof(ColorSpec) << depth);
        }
        else if (ctab && depth <= 8) {
            ROMlib_copy_ctab(ctab, PIXMAP_TABLE(gd_pixmap));
        }

        /* #### if color table in the main device matches `ctab', then
	 we should copy the inverse color table from the main device */
        MakeITable(PIXMAP_TABLE(gd_pixmap), GD_ITABLE(gw_gd), GD_RES_PREF(gw_gd));
    }
    else {
        if (!gw_gd)
            return paramErr;

        gd_pixmap = GD_PMAP(gw_gd);
        FIELD_ASSIGN_RAW(depth, PIXMAP_PIXEL_SIZE(gd_pixmap));
    }

    THEGDEVICE_SAVE_EXCURSION(gw_gd, {
        /* allocate and initialize a new graphics world structure */
        graphics_world = (GWorldPtr)NewPtr(sizeof(GWorld));
        OpenCPort(GW_CPORT(graphics_world));

        gw_pixmap = CPORT_PIXMAP(graphics_world);

        if (!gd_allocated_p) {
            Handle gw_pixmap_baseaddr;
            int row_bytes;

            /* #### the `OpenCPort ()' initialization of `gw' sets the
	      default depth of `gw_pixmap' to that of the gworld,
	      which may not match `depth' */
            row_bytes = gworld_row_bytes(bounds, depth, padded);

            PIXMAP_SET_ROWBYTES_X(gw_pixmap, CW(row_bytes));

            gw_pixmap_baseaddr = NewHandle(row_bytes * RECT_HEIGHT(bounds));
            if (MemError() != noErr)
                /* ### err, i'm not sure what the mac does when this
		allocation fails */
                gw_pixmap_baseaddr = NewHandle(0);
            FIELD_ASSIGN_RAW(PIXMAP_BASEADDR_X(gw_pixmap), (Ptr)RM(gw_pixmap_baseaddr));
            if (PIXMAP_VERSION_X(gw_pixmap) == CWC(GWORLD_EXTERNAL_PIXMAP_VERSION))
                PIXMAP_VERSION_X(gw_pixmap) = 0;

            PORT_RECT(graphics_world) = PIXMAP_BOUNDS(gw_pixmap) = *bounds;
        }

        /* mark as a GWorld and not a CGrafPort */
        CPORT_VERSION_X(graphics_world) |= GW_FLAG_BIT_X;

        install_new_gw_info(graphics_world, gw_gd, 0, gd_allocated_p);

        if (flags & pixelsLocked)
            LockPixels(gw_pixmap);
    });

    HIDDEN_RAW_SET(*graphics_world_out, RM(graphics_world));

    SET_THEPORT_RAW(save_portX);
    return noErr;
}

P6(PUBLIC pascal trap, QDErr, NewGWorld, HIDDEN_GWorldPtr*, graphics_world_out, INTEGER, depth, Rect*, bounds,
   CTabHandle, ctab, GDHandle, gw_gd, GWorldFlags, flags)
{
    return ROMlib_new_gworld(graphics_world_out, depth, bounds, ctab, gw_gd, flags, FALSE);
}

static void attach_gworld_pixels(PixMapHandle pixels, const Rect* bounds, Ptr base, INTEGER row_bytes)
{
    FIELD_ASSIGN_RAW(PIXMAP_BASEADDR_X(pixels), RM(base));
    PIXMAP_SET_ROWBYTES_X(pixels, CW(row_bytes));
    PIXMAP_BOUNDS(pixels) = *bounds;
    PIXMAP_VERSION_X(pixels) = CWC(GWORLD_EXTERNAL_PIXMAP_VERSION);
}

QDErr ROMlib_new_gworld_from_ptr(HIDDEN_GWorldPtr* output, INTEGER depth, Rect* bounds, CTabHandle colors,
                                 GDHandle device, GWorldFlags flags, Ptr base, INTEGER row_bytes)
{
    Rect seed = {.bottom = CWC(1), .right = CWC(1)};
    HIDDEN_GWorldPtr world_handle;
    const QDErr status = ROMlib_new_gworld(&world_handle, depth, &seed, colors, device, flags & ~pixelsLocked, FALSE);
    if (status != noErr)
        return status;

    GWorldPtr world = HIDDEN_NATIVE(world_handle);
    gw_info_t* info = lookup_gw_info_by_gw(world);
    DisposHandle((Handle)PIXMAP_BASEADDR(info->gw_pixmap));
    attach_gworld_pixels(info->gw_pixmap, bounds, base, row_bytes);
    PORT_RECT(world) = *bounds;
    RectRgn(PORT_VIS_REGION(world), bounds);
    if (info->gd_allocated_p) {
        attach_gworld_pixels(info->gw_gd_pixmap, bounds, base, row_bytes);
        GD_RECT(info->gw_gd) = *bounds;
    }
    if (flags & pixelsLocked)
        LockPixels(info->gw_pixmap);
    *output = world_handle;
    return noErr;
}

static Boolean gworld_external_pixels(PixMapHandle pixels)
{
    return PIXMAP_VERSION_X(pixels) == CWC(GWORLD_EXTERNAL_PIXMAP_VERSION);
}

P1(PUBLIC pascal trap, Boolean, LockPixels, PixMapHandle, pixels)
{
    gw_info_t* gw_info = lookup_gw_info_by_gw_pixmap(pixels);
    Handle pixels_baseaddr_h;

    if (gw_info) {
        if (gw_info->flags & pixelsLocked) {
            gw_info->pixel_lock_count++;
            return TRUE;
        }

        gw_info->flags |= pixelsLocked;
        gui_assert(!gw_info->pixel_lock_count);
        gw_info->pixel_lock_count = 1;

        if (!gworld_external_pixels(pixels)) {
            pixels_baseaddr_h = (Handle)PIXMAP_BASEADDR(pixels);
            /* lock the baseaddr handle memory */
            HSetState(pixels_baseaddr_h, HGetState(pixels_baseaddr_h) | LOCKBIT);
            FIELD_ASSIGN_RAW(PIXMAP_BASEADDR_X(pixels), HIDDEN_RAW(*(pixels_baseaddr_h)));
        }

        HSetState((Handle)pixels, HGetState((Handle)pixels) | LOCKBIT);
    }

    return TRUE;
}

P1(PUBLIC pascal trap, void, UnlockPixels, PixMapHandle, pixels)
{
    gw_info_t* gw_info = lookup_gw_info_by_gw_pixmap(pixels);
    Handle pixels_baseaddr_h;
    Ptr pixels_baseaddr;

    if (gw_info) {
        if (!(gw_info->flags & pixelsLocked)) {
            warning_unexpected("attemp to unlock unlocked pixels");
            return;
        }

        gw_info->pixel_lock_count--;
        if (gw_info->flags & pixelsLocked && !gw_info->pixel_lock_count) {
            gw_info->flags &= ~pixelsLocked;

            if (!gworld_external_pixels(pixels)) {
                pixels_baseaddr = PIXMAP_BASEADDR(pixels);
                pixels_baseaddr_h = RecoverHandle(pixels_baseaddr);
                HSetState(pixels_baseaddr_h, HGetState(pixels_baseaddr_h) & ~LOCKBIT);
                FIELD_ASSIGN_RAW(PIXMAP_BASEADDR_X(pixels), (Ptr)RM(pixels_baseaddr_h));
            }

            HSetState((Handle)pixels, HGetState((Handle)pixels) & ~LOCKBIT);
        }
    }
}

P6(PUBLIC pascal trap, GWorldFlags, UpdateGWorld, HIDDEN_GWorldPtr*, graphics_world, INTEGER, depth, Rect*, bounds,
   CTabHandle, ctab, GDHandle, a_gdevice, GWorldFlags, flags)
{
    gw_info_t* gw_info;
    GWorldPtr gw;
    PixMapHandle gw_pmap;
    CTabHandle gw_pmap_ctab;
    GDHandle gw_gd;
    GDHandle max_gd = NULL;
    QDErr err;
    GWorldFlags retval = 0;

    if (depth == 24)
        depth = 32;

    gw_info = lookup_gw_info_by_gw(HIDDEN_NATIVE(*graphics_world));

    gw = gw_info->gw;
    gw_pmap = gw_info->gw_pixmap;
    gw_gd = gw_info->gw_gd;
    gw_pmap_ctab = PIXMAP_TABLE(gw_pmap);

    if (flags & ~(keepLocal | clipPix | stretchPix | ditherPix))
        return paramErr;

    if (!depth) {
        PixMapHandle max_gd_pixmap;

        max_gd = GetMaxDevice(bounds);
        max_gd_pixmap = GD_PMAP(max_gd);

        ctab = PIXMAP_TABLE(max_gd_pixmap);
        depth = PIXMAP_PIXEL_SIZE(GD_PMAP(max_gd));
        gw_gd = max_gd;

        /* #### unclear if this is correct, but it is what `NewGWorld ()'
	 does */
        if (bounds->top || bounds->left) {
            Rect* temp_bounds = alloca(sizeof *temp_bounds);

            *temp_bounds = *bounds;
            OffsetRect(temp_bounds, -CW(temp_bounds->left), -CW(temp_bounds->top));
            bounds = temp_bounds;
        }
    }

    if (ctab && CTAB_SEED_X(ctab) != CTAB_SEED_X(gw_pmap_ctab))
        retval |= mapPix;

    if (depth != PIXMAP_PIXEL_SIZE(gw_pmap))
        retval |= newDepth;

    if (memcmp(bounds, &PIXMAP_BOUNDS(gw_pmap), sizeof *bounds)) {
        if (flags & clipPix)
            retval |= clipPix;
        else if (flags & stretchPix)
            retval |= stretchPix;
        else
            retval |= reallocPix;
    }
    if (retval & (mapPix | newDepth | clipPix | stretchPix | reallocPix)) {
        GWorldPtr gw_ret;
        HIDDEN_GWorldPtr gw_ret_x;
        PixMapHandle gw_pmap_ret;
        gw_info_t* gw_info_ret;
        Rect src_rect, dst_rect;

        err = NewGWorld(&gw_ret_x, depth, bounds, ctab, gw_gd, flags);
        if (err != noErr)
            return err;

        gw_ret = HIDDEN_NATIVE(gw_ret_x);
        gw_info_ret = lookup_gw_info_by_gw(gw_ret);
        gw_pmap_ret = CPORT_PIXMAP(gw_ret);

        if (PIXMAP_ROWBYTES_X(gw_pmap_ret) != PIXMAP_ROWBYTES_X(gw_pmap))
            retval |= newRowBytes;

        LockPixels(gw_pmap_ret);

        if (flags & (stretchPix | clipPix)) {
            if (flags & stretchPix) {
                src_rect = PIXMAP_BOUNDS(gw_pmap);
                dst_rect = PIXMAP_BOUNDS(gw_pmap_ret);
            }
            else /* if (flags & clipPix) */
            {
                Rect *gw_bounds_ret, *gw_bounds;
                Rect temp_rect;

                gw_bounds_ret = &PIXMAP_BOUNDS(gw_pmap_ret);
                gw_bounds = &PIXMAP_BOUNDS(gw_pmap);

                src_rect = *gw_bounds;
                dst_rect = *gw_bounds_ret;

                OffsetRect(&src_rect, -CW(src_rect.left), -CW(src_rect.top));
                OffsetRect(&dst_rect, -CW(dst_rect.left), -CW(dst_rect.top));

                SectRect(&src_rect, &dst_rect, &temp_rect);
                src_rect = dst_rect = temp_rect;

                OffsetRect(&src_rect, CW(gw_bounds->left), CW(gw_bounds->top));
                OffsetRect(&dst_rect, CW(gw_bounds_ret->left), CW(gw_bounds_ret->top));
            }

            CopyBits(PORT_BITS_FOR_COPY(gw), PORT_BITS_FOR_COPY(gw_ret), &src_rect, &dst_rect, srcCopy, NULL);
        }
        if (gw_info->flags & pixelsLocked) {
            gw_info_ret->flags |= pixelsLocked;
            gw_info_ret->pixel_lock_count = gw_info->pixel_lock_count;
        }
        else
            UnlockPixels(gw_pmap_ret);

        DisposeGWorld(gw);
        *graphics_world = gw_ret_x;
    }

    return retval;
}

P1(PUBLIC pascal trap, void, DisposeGWorld, GWorldPtr, graphics_world)
{
    gw_info_t* gw_info;

    if ((GrafPtr)graphics_world == thePort)
        SET_THEPORT_RAW(WMgrPort);
    /* FIXME: set the gdevice to something sane as well? */

    gw_info = lookup_gw_info_by_gw(graphics_world);

    if (!gw_info) {
        warning_unexpected("no gw_info, no deallocation done");
        return;
    }

    /* dispose the fbuf */
    if (!gworld_external_pixels(gw_info->gw_pixmap)) {
        Ptr base = PIXMAP_BASEADDR(gw_info->gw_pixmap);
        DisposHandle(gw_info->flags & pixelsLocked ? RecoverHandle(base) : (Handle)base);
    }

    if (gw_info->gd_allocated_p) {
        DisposGDevice(gw_info->gw_gd);
    }

    ClosePort((GrafPtr)graphics_world);
    DisposPtr((Ptr)graphics_world);

    delete_gw_info(gw_info);
}

P2(PUBLIC pascal trap, void, GetGWorld, HIDDEN_CGrafPtr*, port, HIDDEN_GDHandle*, graphics_device)
{
    HIDDEN_RAW_SET(*port, theCPortX);
    HIDDEN_RAW_SET(*graphics_device, TheGDevice);
}

P2(PUBLIC pascal trap, void, SetGWorld, CGrafPtr, port, GDHandle, graphics_device)
{
    if (port && GRAPHICS_WORLD_P(port)) {
        gw_info_t* gw_info;

        gw_info = lookup_gw_info_by_gw((GWorldPtr)port);
        gui_assert(gw_info);

        HIDDEN_RAW_SET(TheGDevice_H, RM(gw_info->gw_gd));
    }
    else {
        if (graphics_device)
            HIDDEN_RAW_SET(TheGDevice_H, RM(graphics_device));
        else {
            PixMapHandle gd_pixmap;

            gd_pixmap = GD_PMAP(MR(MainDevice));

            if (port && (PP_RAW(PIXMAP_BASEADDR_X(gd_pixmap)) == PP_RAW(PORT_BASEADDR_X(port))))
                HIDDEN_RAW_SET(TheGDevice_H, MainDevice);
        }
    }

    SET_THEPORT_RAW(RM(port));
}

P1(PUBLIC pascal trap, void, AllowPurgePixels, PixMapHandle, pixels)
{
    gw_info_t* gw_info;
    Handle pixels_baseaddr_h;

    gw_info = lookup_gw_info_by_gw_pixmap(pixels);
    if (gw_info && !gworld_external_pixels(pixels)) {
        pixels_baseaddr_h = (Handle)PIXMAP_BASEADDR(pixels);
        if (gw_info->flags & pixelsLocked)
            pixels_baseaddr_h = RecoverHandle((Ptr)pixels_baseaddr_h);
        HPurge(pixels_baseaddr_h);
        gw_info->flags |= pixelsPurgeable;
    }
}

P1(PUBLIC pascal trap, void, NoPurgePixels, PixMapHandle, pixels)
{
    gw_info_t* gw_info;
    Handle pixels_baseaddr_h;

    gw_info = lookup_gw_info_by_gw_pixmap(pixels);
    if (gw_info && !gworld_external_pixels(pixels)) {
        pixels_baseaddr_h = (Handle)PIXMAP_BASEADDR(pixels);
        if (gw_info->flags & pixelsLocked)
            pixels_baseaddr_h = RecoverHandle((Ptr)pixels_baseaddr_h);
        HNoPurge(pixels_baseaddr_h);
        gw_info->flags &= ~pixelsPurgeable;
    }
}

#define FLAGMASK (pixelsPurgeable | pixelsLocked | keepLocal)

P1(PUBLIC pascal trap, GWorldFlags, GetPixelsState, PixMapHandle, pixels)
{
    gw_info_t* gw_info;

    gw_info = lookup_gw_info_by_gw_pixmap(pixels);

    return gw_info ? (gw_info->flags & FLAGMASK) : 0;
}

P2(PUBLIC pascal trap, void, SetPixelsState, PixMapHandle, pixels, GWorldFlags, state)
{
    gw_info_t* gw_info;

    if (state & pixelsPurgeable)
        AllowPurgePixels(pixels);
    else
        NoPurgePixels(pixels);

    if (state & pixelsLocked)
        LockPixels(pixels);
    else
        UnlockPixels(pixels);

    gw_info = lookup_gw_info_by_gw_pixmap(pixels);
    if (gw_info) {
        if (state & keepLocal)
            gw_info->flags |= keepLocal;
        else
            gw_info->flags &= ~keepLocal;
    }
}

P1(PUBLIC pascal trap, Ptr, GetPixBaseAddr, PixMapHandle, pixels)
{
    gw_info_t* gw_info = lookup_gw_info_by_gw_pixmap(pixels);

    if (gworld_external_pixels(pixels))
        return PIXMAP_BASEADDR(pixels);
    if (gw_info == NULL) {
        gw_info = lookup_gw_info_by_gw_gd_pixmap(pixels);

        if (gw_info == NULL
            /* don't dereference the screen baseaddr! */
            || gw_info->gw_gd == MR(MainDevice))
            return PIXMAP_BASEADDR(pixels);
        else
            return STARH((Handle)PIXMAP_BASEADDR(pixels));
    }
    else if (gw_info->flags & pixelsLocked)
        return PIXMAP_BASEADDR(pixels);
    else
        return STARH((Handle)PIXMAP_BASEADDR(pixels));
}

static QDErr make_screen_buffer(Rect* bounds, Boolean purgeable, HIDDEN_GDHandle* device_out,
                                HIDDEN_PixMapHandle* pixmap_out, Boolean temporary)
{
    if (!bounds || !device_out || !pixmap_out)
        return paramErr;
    HIDDEN_RAW_SET(*device_out, 0);
    HIDDEN_RAW_SET(*pixmap_out, 0);
    const int width = RECT_WIDTH(bounds), height = RECT_HEIGHT(bounds);
    if (width <= 0 || height <= 0 || width > INT16_MAX || height > INT16_MAX)
        return paramErr;

    GDHandle device = GetMaxDevice(bounds);
    if (!device)
        return paramErr;
    const PixMapHandle screen = GD_PMAP(device);
    const int rowbytes = gworld_row_bytes(bounds, PIXMAP_PIXEL_SIZE(screen), FALSE);
    if (rowbytes <= 0 || rowbytes >= 0x4000)
        return cDepthErr;

    PixMapHandle pixels = NewPixMap();
    if (!pixels)
        return memFullErr;
    Handle colors = (Handle)PIXMAP_TABLE(screen);
    QDErr status = HandToHand(&colors);
    if (status) {
        DisposPixMap(pixels);
        return status;
    }
    DisposCTable(PIXMAP_TABLE(pixels));
    FIELD_ASSIGN_RAW(PIXMAP_TABLE_X(pixels), RM(colors));
    PIXMAP_PIXEL_SIZE_X(pixels) = PIXMAP_PIXEL_SIZE_X(screen);
    PIXMAP_PIXEL_TYPE_X(pixels) = PIXMAP_PIXEL_TYPE_X(screen);
    PIXMAP_CMP_COUNT_X(pixels) = PIXMAP_CMP_COUNT_X(screen);
    PIXMAP_CMP_SIZE_X(pixels) = PIXMAP_CMP_SIZE_X(screen);
    PIXMAP_BOUNDS(pixels) = (Rect){0, 0, CW(height), CW(width)};
    PIXMAP_SET_ROWBYTES_X(pixels, CW(rowbytes));

    OSErr temp_error;
    Handle storage = temporary ? TempNewHandle(rowbytes * height, &temp_error) : NewHandle(rowbytes * height);
    if (!storage) {
        status = temporary ? CW(temp_error) : MemError();
        DisposPixMap(pixels);
        return status ? status : memFullErr;
    }

    gw_info_t* info = gw_info_free;
    if (info)
        gw_info_free = info->next;
    else
        info = (gw_info_t*)NewPtr(sizeof *info);
    if (!info) {
        DisposHandle(storage);
        DisposPixMap(pixels);
        return memFullErr;
    }
    *info = (gw_info_t){.gw_pixmap = pixels, .gw_gd = device, .next = gw_info_head};
    gw_info_head = info;
    FIELD_ASSIGN_RAW(PIXMAP_BASEADDR_X(pixels), RM(storage));
    if (purgeable)
        AllowPurgePixels(pixels);
    HIDDEN_RAW_SET(*device_out, RM(device));
    HIDDEN_RAW_SET(*pixmap_out, RM(pixels));
    return noErr;
}

P4(PUBLIC pascal trap, QDErr, NewScreenBuffer, Rect*, global_rect, Boolean, purgeable_p, GDHandle*, graphics_device,
   PixMapHandle*, offscreen_pixmap)
{
    return make_screen_buffer(global_rect, purgeable_p, (HIDDEN_GDHandle*)graphics_device,
                              (HIDDEN_PixMapHandle*)offscreen_pixmap, FALSE);
}

P1(PUBLIC pascal trap, void, DisposeScreenBuffer, PixMapHandle, pixels)
{
    gw_info_t* info = lookup_gw_info_by_gw_pixmap(pixels);
    if (!info || info->gw)
        return;
    Ptr address = PIXMAP_BASEADDR(pixels);
    Handle storage = info->flags & pixelsLocked ? RecoverHandle(address) : (Handle)address;
    if (storage)
        DisposHandle(storage);
    delete_gw_info(info);
    DisposPixMap(pixels);
}

P1(PUBLIC pascal trap, GDHandle, GetGWorldDevice, GWorldPtr, graphics_world)
{
    gw_info_t* gw_info = lookup_gw_info_by_gw(graphics_world);

    return gw_info->gw_gd;
}

P1(PUBLIC pascal trap, Boolean, PixMap32Bit, PixMapHandle, pixels)
{
    warning_unimplemented("poorly implemented");
    return TRUE;
    /* #warning "Haven't verified that TRUE is better than 0xff or 0 in PixMap32Bit" */
}

P1(PUBLIC pascal trap, PixMapHandle, GetGWorldPixMap, GWorldPtr, port)
{
    gw_info_t* gw_info = lookup_gw_info_by_gw(port);

    if (gw_info)
        return gw_info->gw_pixmap;
    else
        /* ultima (and others) call `GetGWorldPixMap ()' with a non-gworld
       port */
        return CPORT_PIXMAP(port);
}

P4(PUBLIC pascal trap, QDErr, NewTempScreenBuffer, Rect*, global_rect, Boolean, purgeable_p, GDHandle*, graphics_device,
   PixMapHandle*, offscreen_pixmap)
{
    return make_screen_buffer(global_rect, purgeable_p, (HIDDEN_GDHandle*)graphics_device,
                              (HIDDEN_PixMapHandle*)offscreen_pixmap, TRUE);
}

P0(PUBLIC pascal trap, LONGINT, OffscreenVersion)
{
    /* #warning OffscreenVersion not properly implemented */
    warning_unimplemented("poorly implemented");
    return 0x130;
}

P1(PUBLIC pascal trap, void, GDeviceChanged, GDHandle, graphics_device)
{
    /* #warning GDeviceChanged not implemented */
    ROMlib_invalidate_conversion_tables();
    warning_unimplemented(NULL_STRING);
}

P1(PUBLIC pascal trap, void, PortChanged, GrafPtr, port)
{
    /* #warning "PortChanged not implemented; worked around for hypercard" */
    ROMlib_invalidate_conversion_tables();
    warning_unimplemented(NULL_STRING);
}

P1(PUBLIC pascal trap, void, PixPatChanged, PixPatHandle, pixpat)
{
    /* #warning "PixPatChanged not implemented" */
    ROMlib_invalidate_conversion_tables();
    warning_unimplemented(NULL_STRING);
}

P1(PUBLIC pascal trap, void, CTabChanged, CTabHandle, ctab)
{
    /* #warning "CTabChanged not implemented" */
    ROMlib_invalidate_conversion_tables();
    warning_unimplemented(NULL_STRING);
}

P1(PUBLIC pascal trap, Boolean, QDDone, GrafPtr, port)
{
    /* #warning "QDDone not implemented" */
    warning_unimplemented(NULL_STRING);
    return TRUE;
}
