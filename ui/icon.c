/*+===================================================================
  File:      icon.c

  Summary:   Implements icon support for ROMAW application appearance and help.

  Classes:   None.

  Functions: ROMlib_read_file_icon, ROMlib_read_icon_data, ROMlib_set_application_icon

  Origin:    Existing repository source in executor/src/ui; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1995, 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Suites retain ROMAW's private layout. Cache loading, mask alignment and
   hit testing follow SuperMario IconUtils.c (FromSuite, PerformAlignment and
   HitTestIconGeneric); this is an adaptation, not the Apple private ABI. */

#include "rsys/common.h"

#include "QuickDraw.h"
#include "CQuickDraw.h"
#include "Iconutil.h"

#include "rsys/cquick.h"
#include "rsys/resource.h"
#include "rsys/mman.h"
#include "rsys/icon.h"
#include "rsys/icon_color.h"
#include "rsys/file.h"
#include "rsys/fileicon.h"
#include "rsys/hfs.h"
#include "rsys/device.h"
#include "../finder-metadata/include/finder_metadata/icons.h"

/* Keep large source artwork outside the guest heap. A small guest transfer
   buffer lets both host directories and mounted HFS images use the same path. */
int ROMlib_read_icon_data(FSSpec* spec, FinderIcon* icon, unsigned char* rgba)
{
    if (!spec || (!icon && !rgba))
        return 0;
    HParamBlockRec pb = {0};
    PP_ASSIGN(pb.ioParam.ioNamePtr, (StringPtr)spec->name);
    pb.ioParam.ioVRefNum = spec->vRefNum;
    pb.ioParam.ioPermssn = fsRdPerm;
    pb.fileParam.ioDirID = spec->parID;
    if (PBOpenDF(&pb, FALSE) != noErr)
        return 0;
    INTEGER file = CW(pb.ioParam.ioRefNum);
    LONGINT length = 0;
    unsigned char* bytes = NULL;
    Ptr transfer = NULL;
    int valid = GetEOF(file, &length) == noErr && length >= 6 && length <= FINDER_ICON_FILE_LIMIT;
    if (valid) {
        bytes = malloc(length);
        transfer = NewPtrSys(32768);
        valid = bytes && transfer;
    }
    for (LONGINT offset = 0; valid && offset < length;) {
        LONGINT count = MIN(32768, length - offset), requested = count;
        valid = FSRead(file, &count, transfer) == noErr && count == requested;
        if (valid) {
            memcpy(bytes + offset, transfer, count);
            offset += count;
        }
    }
    FSClose(file);
    if (transfer)
        DisposPtr(transfer);
    if (valid && icon)
        valid = finder_parse_icon_file(bytes, length, icon);
    if (valid && rgba)
        valid = finder_decode_icon_file(bytes, length, 32, rgba);
    free(bytes);
    return valid;
}

#if defined(SDL) && defined(_WIN32)
#include "rsys/appicon.h"

/* Use the application's bundle mapping, rather than assuming icon ID 128. */
void ROMlib_set_application_icon(unsigned long creator)
{
    Handle h, mono, color;
    short id = 128;
    unsigned local_id = 0;
    int i, j, depth = 1, found = FALSE;
    unsigned char mask[256], pixels[1024], rgba[32 * 32 * 4];

    for (i = 1; i <= Count1Resources(TICK("FREF")); ++i) {
        h = Get1IndResource(TICK("FREF"), i);
        if (h && STARH(h) && GetHandleSize(h) >= 6) {
            const unsigned char* p = (unsigned char*)STARH(h);
            if (memcmp(p, "APPL", 4) == 0) {
                local_id = ROMlib_icon_word(p + 4);
                break;
            }
        }
    }
    for (i = 1; i <= Count1Resources(TICK("BNDL")); ++i) {
        h = Get1IndResource(TICK("BNDL"), i);
        if (h && STARH(h) && GetHandleSize(h) >= 8 &&
            ROMlib_bundle_icon_id((unsigned char*)STARH(h), GetHandleSize(h), creator, local_id, &id)) {
            found = TRUE;
            break;
        }
    }
    mono = Get1Resource(TICK("ICN#"), id);
    if (!mono && !found)
        mono = Get1IndResource(TICK("ICN#"), 1);
    if (!mono || !STARH(mono) || GetHandleSize(mono) < (Size)sizeof mask) {
        ROMlib_sdl_set_application_icon(NULL, creator);
        return;
    }
    memcpy(mask, STARH(mono), sizeof mask);
    if (!found) {
        ResType type;
        Str255 name;
        GetResInfo(mono, &id, &type, name);
        id = CW(id);
    }
    color = Get1Resource(TICK("icl8"), id);
    if (color && STARH(color) && GetHandleSize(color) >= 1024)
        depth = 8;
    else {
        color = Get1Resource(TICK("icl4"), id);
        if (color && STARH(color) && GetHandleSize(color) >= 512)
            depth = 4;
    }
    if (depth != 1)
        memcpy(pixels, STARH(color), depth == 8 ? 1024 : 512);
    for (i = 0; i < 1024; ++i) {
        unsigned bit = 0x80 >> (i & 7);
        if (depth == 1)
            for (j = 0; j < 3; ++j)
                rgba[4 * i + j] = mask[i >> 3] & bit ? 0 : 255;
        else {
            unsigned index = depth == 8 ? pixels[i] : (pixels[i >> 1] >> ((i & 1) ? 0 : 4)) & 15;
            const RGBColor* rgb = depth == 8 ? &ctab_8bpp_values[index].rgb : &ctab_4bpp_values[index].rgb;
            rgba[4 * i] = CW(rgb->red) >> 8;
            rgba[4 * i + 1] = CW(rgb->green) >> 8;
            rgba[4 * i + 2] = CW(rgb->blue) >> 8;
        }
        rgba[4 * i + 3] = mask[128 + (i >> 3)] & bit ? 255 : 0;
    }
    ROMlib_sdl_set_application_icon(rgba, creator);
}
#endif

#define ICON_RETURN_ERROR(error)                                     \
    ({                                                               \
        OSErr _error_ = (error);                                     \
                                                                     \
        if (_error_ != noErr)                                        \
            warning_unexpected("error `%s', `%d'", #error, _error_); \
        return _error_;                                              \
    })

#define _GetIconSuite(icon_suite, res_id, selector)                \
    ({                                                             \
        Handle _icon_suite_ = NULL;                                \
        OSErr _err_;                                               \
                                                                   \
        _err_ = GetIconSuite(&_icon_suite_, (res_id), (selector)); \
                                                                   \
        *(icon_suite) = MR(_icon_suite_);                          \
                                                                   \
        _err_;                                                     \
    })

P4(PUBLIC pascal trap, OSErr, PlotIconID, const Rect*, rect, IconAlignmentType, align, IconTransformType, transform,
   short, res_id)
{
    Handle icon_suite;
    OSErr err;

    err = _GetIconSuite(&icon_suite, res_id, svAllAvailableData);
    if (err != noErr)
        ICON_RETURN_ERROR(err);

    err = PlotIconSuite(rect, align, transform, icon_suite);
    DisposeIconSuite(icon_suite, FALSE);
    ICON_RETURN_ERROR(err);
}

static OSErr suite_from_method(IconGetterProcPtr method, void* data, Handle* suite);

P5(PUBLIC pascal trap, OSErr, PlotIconMethod, const Rect*, rect, IconAlignmentType, align, IconTransformType, transform,
   IconGetterProcPtr, method, void*, data)
{
    Handle suite;
    OSErr error = suite_from_method(method, data, &suite);
    if (!error) {
        error = PlotIconSuite(rect, align, transform, suite);
        DisposeIconSuite(suite, FALSE);
    }
    ICON_RETURN_ERROR(error);
}

P2(PUBLIC pascal trap, void, PlotIcon, const Rect*, rect, Handle, icon)
{
    if (icon == NULL)
        return;

    if (!HIDDEN_RAW((*icon)))
        LoadResource(icon);

    LOCK_HANDLE_EXCURSION_1(icon, {
        BitMap bm;

        FIELD_ASSIGN_RAW(bm.baseAddr, (Ptr)HIDDEN_RAW(*(icon)));
        bm.rowBytes = CWC(4);
        bm.bounds.left = bm.bounds.top = 0;
        if (GetHandleSize(icon) == 2 * 16) {
            bm.rowBytes = CWC(2);
            bm.bounds.bottom = CWC(16);
        }
        else {
            bm.rowBytes = CWC(4);
            bm.bounds.bottom = CWC(32);
        }
        bm.bounds.right = bm.bounds.bottom;
        CopyBits(&bm, PORT_BITS_FOR_COPY(thePort), &bm.bounds, rect, srcCopy, NULL);
    });
}

P4(PUBLIC pascal trap, OSErr, PlotIconHandle, const Rect*, rect, IconAlignmentType, align, IconTransformType, transform,
   Handle, icon)
{
    /* #### change plotting routines to respect alignment and transform */
    if (align != atNone)
        warning_unimplemented("unhandled icon alignment `%d'", align);
    if (transform != ttNone)
        warning_unimplemented("unhandled icon transform `%d'", transform);

    PlotIcon(rect, icon);

    ICON_RETURN_ERROR(noErr);
}

P2(PUBLIC pascal trap, void, PlotCIcon, const Rect*, rect, CIconHandle, icon)
{
    /* when plotting, `ignore' the current fg/bk colors */
    GrafPtr current_port;
    RGBColor bk_rgb, fg_rgb;
    int32 bk_color, fg_color;
    int cgrafport_p;

    if (!icon)
        return;

    current_port = thePort;

    cgrafport_p = CGrafPort_p(current_port);
    if (cgrafport_p) {
        fg_rgb = CPORT_RGB_FG_COLOR(current_port);
        bk_rgb = CPORT_RGB_BK_COLOR(current_port);
    }
    fg_color = PORT_FG_COLOR_X(current_port);
    FIELD_ASSIGN_RAW(bk_color, PORT_BK_COLOR_X(current_port));

    RGBForeColor(&ROMlib_black_rgb_color);
    RGBBackColor(&ROMlib_white_rgb_color);

    LOCK_HANDLE_EXCURSION_1(icon, {
        PixMapHandle gd_pixmap;

        BitMap* mask_bm;
        BitMap* icon_bm;

        icon_bm = &CICON_BMAP(icon);

        mask_bm = &CICON_MASK(icon);
        FIELD_ASSIGN_RAW(BITMAP_BASEADDR_X(mask_bm), RM((Ptr)CICON_MASK_DATA(icon)));

        gd_pixmap = GD_PMAP(MR(MainDevice));

        if ((PP_RAW(PORT_BASEADDR_X(current_port)) == PP_RAW(PIXMAP_BASEADDR_X(gd_pixmap)) &&
             PIXMAP_PIXEL_SIZE(gd_pixmap) > 2) ||
            (CGrafPort_p(current_port) && PIXMAP_PIXEL_SIZE(CPORT_PIXMAP(current_port)) > 2) ||
            !BITMAP_ROWBYTES_X(icon_bm)) {
            Handle icon_data;

            icon_data = CICON_DATA(icon);
            LOCK_HANDLE_EXCURSION_1(icon_data, {
                PixMap* icon_pm;

                icon_pm = &CICON_PMAP(icon);
                FIELD_ASSIGN_RAW(BITMAP_BASEADDR_X(icon_pm), (Ptr)HIDDEN_RAW(*(icon_data)));

                CopyMask((BitMap*)icon_pm, mask_bm, PORT_BITS_FOR_COPY(current_port), &BITMAP_BOUNDS(icon_pm),
                         &BITMAP_BOUNDS(mask_bm),
                         /* #### fix up the need for this cast */
                         (Rect*)rect);
            });
        }
        else {
            Rect* icon_bm_bounds;
            Ptr bm_baseaddr;
            int height;
            int mask_data_size;

            icon_bm_bounds = &BITMAP_BOUNDS(icon_bm);

            height = RECT_HEIGHT(icon_bm_bounds);
            mask_data_size = BITMAP_ROWBYTES(icon_bm) * height;
            bm_baseaddr = (Ptr)((char*)CICON_MASK_DATA(icon) + mask_data_size);

            FIELD_ASSIGN_RAW(BITMAP_BASEADDR_X(icon_bm), RM(bm_baseaddr));
            CopyMask(icon_bm, mask_bm, PORT_BITS_FOR_COPY(current_port), icon_bm_bounds, &BITMAP_BOUNDS(mask_bm),
                     /* #### fix up the need for this cast */
                     (Rect*)rect);
        }
    });

    if (cgrafport_p) {
        CPORT_RGB_FG_COLOR(current_port) = fg_rgb;
        CPORT_RGB_BK_COLOR(current_port) = bk_rgb;
    }
    PORT_FG_COLOR_X(current_port) = fg_color;
    PORT_BK_COLOR_X(current_port) = bk_color;
}

P4(PUBLIC pascal trap, OSErr, PlotCIconHandle, const Rect*, rect, IconAlignmentType, align, IconTransformType,
   transform, CIconHandle, icon)
{
    /* #### change plotting routines to respect alignment and transform */
    if (align != atNone)
        warning_unimplemented("unhandled icon alignment `%d'", align);
    if (transform != ttNone)
        warning_unimplemented("unhandled icon transform `%d'", transform);

    PlotCIcon(rect, icon);

    ICON_RETURN_ERROR(noErr);
}

P4(PUBLIC pascal trap, OSErr, PlotSICNHandle, const Rect*, rect, IconAlignmentType, align, IconTransformType, transform,
   Handle, icon)
{
    /* #### change plotting routines to respect alignment and transform */
    if (align != atNone)
        warning_unimplemented("unhandled icon alignment `%d'", align);
    if (transform != ttNone)
        warning_unimplemented("unhandled icon transform `%d'", transform);

    PlotIcon(rect, icon);

    ICON_RETURN_ERROR(noErr);
}

P1(PUBLIC pascal trap, Handle, GetIcon, short, icon_id)
{
    return ROMlib_getrestid(TICK("ICON"), icon_id);
}

P1(PUBLIC pascal trap, CIconHandle, GetCIcon, short, icon_id)
{
    CIconHandle cicon_handle;
    CIconHandle cicon_res_handle;
    CIconPtr cicon_res;
    int height;
    int mask_data_size;
    int bmap_data_size;
    int new_size;

    cicon_res_handle = (CIconHandle)ROMlib_getrestid(TICK("cicn"), icon_id);
    if (cicon_res_handle == NULL)
        return NULL;

    cicon_res = STARH(cicon_res_handle);
    height = RECT_HEIGHT(&cicon_res->iconPMap.bounds);
    mask_data_size = CW(cicon_res->iconMask.rowBytes) * height;
    bmap_data_size = CW(cicon_res->iconBMap.rowBytes) * height;
    new_size = sizeof(CIcon) - sizeof(INTEGER) + mask_data_size + bmap_data_size;

    FIELD_ASSIGN_RAW(cicon_handle, (CIconHandle)NewHandle(new_size));
    LOCK_HANDLE_EXCURSION_2(cicon_handle, cicon_res_handle, {
        CTabPtr tmp_ctab;
        int mask_data_offset;
        int bmap_data_offset;
        int pmap_ctab_offset;
        int pmap_ctab_size;
        int pmap_data_offset;
        int pmap_data_size;
        CIconPtr cicon;

        cicon = STARH(cicon_handle);
        cicon_res = STARH(cicon_res_handle);

        BlockMove((Ptr)cicon_res, (Ptr)cicon, new_size);

        mask_data_offset = 0;

        bmap_data_offset = mask_data_size;

        pmap_ctab_offset = bmap_data_offset + bmap_data_size;
        tmp_ctab = (CTabPtr)((char*)&cicon_res->iconMaskData + pmap_ctab_offset);
        pmap_ctab_size = sizeof(ColorTable) + (CW(tmp_ctab->ctSize) * sizeof(ColorSpec));

        pmap_data_offset = pmap_ctab_offset + pmap_ctab_size;
        pmap_data_size = (CW(cicon->iconPMap.rowBytes) & ROWBYTES_VALUE_BITS) * height;

        FIELD_ASSIGN_RAW(cicon->iconMask.baseAddr, CLC_NULL);

        FIELD_ASSIGN_RAW(cicon->iconBMap.baseAddr, CLC_NULL);

        {
            CTabHandle color_table;

            color_table = (CTabHandle)NewHandle(pmap_ctab_size);
            BlockMove((Ptr)&cicon_res->iconMaskData + pmap_ctab_offset, (Ptr)STARH(color_table), pmap_ctab_size);
            CTAB_SEED_X(color_table) = CL(GetCTSeed());
            FIELD_ASSIGN_RAW(cicon->iconPMap.pmTable, RM(color_table));

            FIELD_ASSIGN_RAW(cicon->iconPMap.baseAddr, CLC_NULL);
            PP_ASSIGN(cicon->iconData, NewHandle(pmap_data_size));
            BlockMove((Ptr)&cicon_res->iconMaskData + pmap_data_offset, (Ptr)STARH(PPR(cicon->iconData)),
                      pmap_data_size);
        }
    });

    return cicon_handle;
}

P1(PUBLIC pascal trap, void, DisposeCIcon, CIconHandle, icon)
{
    DisposHandle(CICON_DATA(icon));
    DisposHandle((Handle)PPR(CICON_PMAP(icon).pmTable));
    DisposHandle((Handle)icon);
}

int ROMlib_read_file_icon(FSSpec* spec, uint32 creator, uint32 type, boolean_t custom, FinderIcon* icon)
{
    if (!spec || !icon)
        return 0;
    const int16 reference = CW(spec->vRefNum);
    const VCBExtra* volume =
        (const VCBExtra*)(reference < 0 ? ROMlib_vcbbyvrn(reference) : ROMlib_findvcb(reference, NULL, NULL, TRUE));
    const bool custom_icons_disabled = volume && volume->custom_icons_disabled;
    custom = custom && !custom_icons_disabled;
    INTEGER saved_error = ResErr;
    INTEGER saved = C_CurResFile();
    Handle previous_top = MR(TopMapHndl);
    BOOLEAN loading = ResLoad;
    memset(icon, 0, sizeof *icon);
    C_SetResLoad(FALSE);
    INTEGER file = C_FSpOpenResFile(spec, fsRdPerm);
    C_SetResLoad(TRUE);
    if (file != -1) {
        C_UseResFile(file);
        int id = -16455, found = custom;
        int bundles = custom ? 0 : C_Count1Resources(TICK("BNDL"));
        int refs = custom ? 0 : C_Count1Resources(TICK("FREF"));
        for (int i = 1; i <= refs && !found; ++i) {
            Handle ref = C_Get1IndResource(TICK("FREF"), i);
            if (!ref || !STARH(ref) || GetHandleSize(ref) < 6)
                continue;
            unsigned char* bytes = (unsigned char*)STARH(ref);
            uint32 code;
            memcpy(&code, bytes, 4);
            if (CL(code) != type)
                continue;
            unsigned local = bytes[4] * 256u + bytes[5];
            for (int j = 1; j <= bundles && !found; ++j) {
                Handle bundle = C_Get1IndResource(TICK("BNDL"), j);
                if (bundle && STARH(bundle))
                    found = finder_bundle_icon_id((unsigned char*)STARH(bundle), GetHandleSize(bundle), creator, local,
                                                  &id);
            }
        }
        if (!found && !bundles && C_Count1Resources(TICK("ICN#")) == 1) {
            Handle first = C_Get1IndResource(TICK("ICN#"), 1);
            INTEGER resource_id;
            ResType resource_type;
            Str255 name;
            C_GetResInfo(first, &resource_id, &resource_type, name);
            id = CW(resource_id);
            found = !custom_icons_disabled || id != -16455;
        }
        if (found) {
            unsigned char* data[] = {icon->mono, icon->small, icon->color4, icon->color8, icon->small4, icon->small8};
            for (unsigned i = 0; i < 6; ++i) {
                Handle resource = C_Get1Resource(finder_icon_types[i], id);
                if (resource && STARH(resource) && GetHandleSize(resource) >= finder_icon_sizes[i]) {
                    memcpy(data[i], STARH(resource), finder_icon_sizes[i]);
                    icon->present |= 1u << i;
                }
            }
            if (custom) {
                Handle icns = C_Get1Resource(TICK("icns"), id);
                if (icns && !(icon->present & 3))
                    finder_parse_icns_icon((unsigned char*)STARH(icns), GetHandleSize(icns), icon);
            }
        }
        /* OpenResFile can reuse the running application's map. */
        if (MR(TopMapHndl) != previous_top)
            C_CloseResFile(file);
    }
    C_UseResFile(saved);
    C_SetResLoad(loading);
    ResErr = saved_error;
    return !!(icon->present & 3);
}

#define large_bw_icon 0
#define small_bw_icon 3

static int icon_for_log2_bpp[] = {
    0, 0, 1, 2, 2, 2,
};

static int bpp_for_icon[] = {
    1,
    4,
    8,
};

static int restype_for_icon[] = {
    large1BitMask, large4BitData, large8BitData, small1BitMask, small4BitData, small8BitData,
};

/* Callback handles are borrowed. Only the temporary suite belongs to us. */
static OSErr suite_from_method(IconGetterProcPtr method, void* data, Handle* suite)
{
    if (!method || !suite)
        return paramErr;
    *suite = NULL;
    OSErr error = NewIconSuite(suite);
    if (error)
        return error;
    *suite = MR(*suite);
    for (unsigned i = 0; i < N_SUITE_ICONS; ++i) {
        Handle icon = (Handle)CToPascalCall(method, 5 | (4 << 3) | (5 << 6), restype_for_icon[i], data);
        if (icon)
            AddIconToSuite(icon, *suite, restype_for_icon[i]);
    }
    return noErr;
}

static int mask_for_icon[] = {
    svLarge1Bit, svLarge4Bit, svLarge8Bit, svSmall1Bit, svSmall4Bit, svSmall8Bit,
};

static int restype_to_index(ResType type)
{
    int i;

    for (i = 0; i < N_SUITE_ICONS; i++) {
        if (type == restype_for_icon[i])
            return i;
    }

    return -1;
}

/* A Toolbox Handle output occupies four guest bytes, even on x64. */
static void store_icon_handle(Handle* output, Handle icon)
{
    uint32 encoded = (uint32)(uintptr_t)RM(icon);
    memcpy(output, &encoded, sizeof encoded);
}

P3(PUBLIC pascal trap, OSErr, GetIconSuite, Handle*, icon_suite_return, short, res_id, IconSelectorValue, selector)
{
    Handle icon_suite, *icons;
    int i;

    icon_suite = NewHandleClear(sizeof(cotton_suite_layout_t));
    if (MemErr != CWC(noErr))
        ICON_RETURN_ERROR(memFullErr);

    ((cotton_suite_layout_t*)STARH(icon_suite))->signature = CLC(ICON_SUITE_SIGNATURE);
    LOCK_HANDLE_EXCURSION_1(icon_suite, {
        icons = (Handle*)STARH(icon_suite);

        for (i = 0; i < N_SUITE_ICONS; i++) {
            if (selector & mask_for_icon[i]) {
                Handle icon;

                icon = GetResource(restype_for_icon[i], res_id);
                if (icon != NULL)
                    icons[i] = icon;
            }
        }
    });

    store_icon_handle(icon_suite_return, icon_suite);

    ICON_RETURN_ERROR(noErr);
}

P1(PUBLIC pascal trap, OSErr, NewIconSuite, Handle*, icon_suite_return)
{
    Handle icon_suite;

    icon_suite = NewHandleClear(sizeof(cotton_suite_layout_t));
    if (MemErr != CWC(noErr))
        ICON_RETURN_ERROR(memFullErr);

    ((cotton_suite_layout_t*)STARH(icon_suite))->signature = CLC(ICON_SUITE_SIGNATURE);
    store_icon_handle(icon_suite_return, icon_suite);

    ICON_RETURN_ERROR(noErr);
}

P3(PUBLIC pascal trap, OSErr, AddIconToSuite, Handle, icon_data, Handle, icon_suite, ResType, type)
{
    Handle* icons;
    int index = restype_to_index(type);

    if (index < 0 || !icon_suite || !STARH(icon_suite))
        ICON_RETURN_ERROR(paramErr);
    icons = (Handle*)STARH(icon_suite);
    icons[index] = icon_data;

    ICON_RETURN_ERROR(noErr);
}

P3(PUBLIC pascal trap, OSErr, GetIconFromSuite, Handle*, icon_data_return, Handle, icon_suite, ResType, type)
{
    Handle *icons, icon_data;
    int index = restype_to_index(type);

    if (index < 0 || !icon_data_return || !icon_suite || !STARH(icon_suite))
        ICON_RETURN_ERROR(paramErr);
    icons = (Handle*)STARH(icon_suite);
    icon_data = icons[index];

    if (icon_data == NULL)
        ICON_RETURN_ERROR(paramErr);

    store_icon_handle(icon_data_return, icon_data);
    ICON_RETURN_ERROR(noErr);
}

static boolean_t icon_cache_p(Handle suite)
{
    return suite && STARH(suite) && GetHandleSize(suite) >= (Size)sizeof(cotton_cache_layout_t) &&
           ((cotton_cache_layout_t*)STARH(suite))->suite.signature == CLC(ICON_CACHE_SIGNATURE);
}

/* SuperMario FromSuite stores successful getter results. Reacquire the suite
   after the callback, which may allocate and move unlocked handle storage. */
static Handle suite_icon(Handle suite, unsigned index)
{
    Handle icon = ((cotton_suite_layout_t*)STARH(suite))->icons[index];
    if (!icon && icon_cache_p(suite)) {
        cotton_cache_layout_t* cache = (cotton_cache_layout_t*)STARH(suite);
        ProcPtr procedure = MR((ProcPtr)(uintptr_t)cache->procedure);
        void* data = MR((Ptr)(uintptr_t)cache->data);
        if (procedure) {
            icon = (Handle)CToPascalCall(procedure, 5 | (4 << 3) | (5 << 6), restype_for_icon[index], data);
            ((cotton_suite_layout_t*)STARH(suite))->icons[index] = icon;
        }
    }
    return icon;
}

static OSErr find_best_icon(boolean_t small_p, int bpp, Handle icon_suite_h, Handle* icon_data_return,
                            Handle* icon_mask_return, boolean_t* small_return_p, int* icon_bpp_return)
{
    Handle icon_data = NULL, icon_mask = NULL;
    int best_icon = -1;

    if (!icon_suite_h || !STARH(icon_suite_h) || GetHandleSize(icon_suite_h) < (Size)sizeof(cotton_suite_layout_t))
        ICON_RETURN_ERROR(paramErr);
    for (unsigned attempt = 0; attempt < 2; ++attempt, small_p = !small_p) {
        unsigned first = small_p ? small_bw_icon : large_bw_icon;
        Size plane = small_p ? 32 : 128;
        icon_mask = suite_icon(icon_suite_h, first);
        if (!icon_mask)
            continue;
        if (!STARH(icon_mask))
            LoadResource(icon_mask);
        if (!STARH(icon_mask) || GetHandleSize(icon_mask) < plane * 2)
            continue;

        SignedByte mask_state = HGetState(icon_mask);
        HLock(icon_mask);
        for (best_icon = icon_for_log2_bpp[ROMlib_log2[bpp]]; best_icon >= 0; --best_icon) {
            icon_data = suite_icon(icon_suite_h, first + best_icon);
            if (!icon_data)
                continue;
            if (!STARH(icon_data))
                LoadResource(icon_data);
            if (STARH(icon_data) && GetHandleSize(icon_data) >= plane * bpp_for_icon[best_icon])
                break;
        }
        HSetState(icon_mask, mask_state);
        if (best_icon >= 0)
            break;
    }
    if (best_icon < 0)
        ICON_RETURN_ERROR(noMaskFoundErr);

    *small_return_p = small_p;
    *icon_bpp_return = bpp_for_icon[best_icon];
    *icon_mask_return = icon_mask;
    *icon_data_return = icon_data;

    /* The host disk driver supplies classic monochrome controls, but its
     built-in icon family also contains color. Explicit suite data wins. */
    if (!small_p && best_icon == 0 && bpp >= 4) {
        int depth = bpp;
        SignedByte state = HGetState(icon_mask);
        HLock(icon_mask);
        Handle color = ROMlib_host_disk_color_icon(icon_mask, &depth);
        HSetState(icon_mask, state);
        if (color) {
            *icon_data_return = color;
            *icon_bpp_return = depth;
        }
    }

    ICON_RETURN_ERROR(noErr);
}

static int icon_port_depth(GrafPtr port)
{
    if (CGrafPort_p(port))
        return PIXMAP_PIXEL_SIZE(CPORT_PIXMAP(port));
    /* Like PlotCIcon, retain colour when an old-style port draws directly
     onto the screen. A separate one-bit bitmap remains monochrome. */
    if (MainDevice) {
        PixMapHandle screen = GD_PMAP(MR(MainDevice));
        if (screen && PP_RAW(PORT_BASEADDR_X(port)) == PP_RAW(PIXMAP_BASEADDR_X(screen)))
            return PIXMAP_PIXEL_SIZE(screen);
    }
    return 1;
}

/* Align the visible mask boundary, not its transparent 16/32px padding.
   SuperMario PerformAlignment maps that boundary into the requested rectangle
   before shifting the whole destination. Scan while locked, then release the
   borrowed storage before calling Toolbox geometry functions. */
static OSErr align_icon_rect(Handle mask, int size, const Rect* requested, IconAlignmentType align, Rect* output)
{
    *output = *requested;
    if (align == atNone)
        return noErr;
    Size plane = size * size / 8;
    if (!mask || !STARH(mask) || GetHandleSize(mask) < plane * 2)
        return paramErr;
    int left = size, top = size, right = 0, bottom = 0;
    LOCK_HANDLE_EXCURSION_1(mask, {
        const unsigned char* bits = (unsigned char*)STARH(mask) + plane;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
                if (bits[y * (size / 8) + x / 8] & (0x80 >> (x & 7))) {
                    left = MIN(left, x);
                    top = MIN(top, y);
                    right = MAX(right, x + 1);
                    bottom = MAX(bottom, y + 1);
                }
    });
    if (left >= right)
        return noErr;
    Rect source, boundary;
    SetRect(&source, 0, 0, size, size);
    SetRect(&boundary, left, top, right, bottom);
    MapRect(&boundary, &source, (Rect*)requested);
    int dx = 0, dy = 0;
    switch (align & 12) {
    case atHorizontalCenter:
        dx = (CW(requested->left) + CW(requested->right) - CW(boundary.left) - CW(boundary.right)) / 2;
        break;
    case atLeft:
        dx = CW(requested->left) - CW(boundary.left);
        break;
    case atRight:
        dx = CW(requested->right) - CW(boundary.right);
        break;
    }
    switch (align & 3) {
    case atVerticalCenter:
        dy = (CW(requested->top) + CW(requested->bottom) - CW(boundary.top) - CW(boundary.bottom)) / 2;
        break;
    case atTop:
        dy = CW(requested->top) - CW(boundary.top);
        break;
    case atBottom:
        dy = CW(requested->bottom) - CW(boundary.bottom);
        break;
    }
    OffsetRect(output, dx, dy);
    return noErr;
}

P4(PUBLIC pascal trap, OSErr, PlotIconSuite, const Rect*, rect, IconAlignmentType, align, IconTransformType, transform,
   Handle, icon_suite)
{
    GrafPtr current_port;
    int port_bpp, icon_bpp;
    boolean_t little_rect_p, little_icon_p;
    Handle icon_data, icon_mask;
    OSErr err;
    unsigned label = ((unsigned)transform >> 8) & 7;
    Handle expanded = NULL;
    SignedByte mask_state;

    if (!rect || !icon_suite || !STARH(icon_suite) || GetHandleSize(icon_suite) < (Size)sizeof(cotton_suite_layout_t))
        ICON_RETURN_ERROR(paramErr);

    if (transform & ~0x0700)
        warning_unimplemented("unhandled icon transform `%d'", transform);

    if (!label)
        label = (unsigned)C_GetSuiteLabel(icon_suite) & 7;

    current_port = thePort;
    little_rect_p = (RECT_WIDTH(rect) < 32 && RECT_HEIGHT(rect) < 32);
    port_bpp = icon_port_depth(current_port);

    err = find_best_icon(little_rect_p, port_bpp, icon_suite, &icon_data, &icon_mask, &little_icon_p, &icon_bpp);
    if (err != noErr)
        ICON_RETURN_ERROR(err);
    Rect aligned;
    err = align_icon_rect(icon_mask, little_icon_p ? 16 : 32, rect, align, &aligned);
    if (err)
        ICON_RETURN_ERROR(err);
    rect = &aligned;

    /* Expansion and palette allocation must not purge the borrowed mask. */
    mask_state = HGetState(icon_mask);
    HLock(icon_mask);

    /* A monochrome family still acquires its label on a color screen. Expand
     its image only; the original mask and suite remain untouched. */
    if (label && port_bpp >= 4 && icon_bpp == 1) {
        unsigned pixels = little_icon_p ? 256 : 1024;
        LOCK_HANDLE_EXCURSION_1(icon_data, {
            expanded = NewHandle(pixels / 2);
            if (expanded) {
                const unsigned char* source = (unsigned char*)STARH(icon_data);
                unsigned char* destination = (unsigned char*)STARH(expanded);
                for (unsigned i = 0; i < pixels; i += 2)
                    destination[i / 2] = ((source[i / 8] & (0x80 >> (i & 7))) ? 0xf0 : 0) |
                                         ((source[(i + 1) / 8] & (0x80 >> ((i + 1) & 7))) ? 15 : 0);
            }
        });
        if (!expanded) {
            HSetState(icon_mask, mask_state);
            ICON_RETURN_ERROR(memFullErr);
        }
        icon_data = expanded;
        icon_bpp = 4;
    }

    /* plot our icon */

    LOCK_HANDLE_EXCURSION_2(icon_data, icon_mask, {
        PixMap icon_pm;
        BitMap mask_bm;
        CTabHandle color_table;
        Rect icon_rect;
        int icon_size;

        color_table = GetCTable(icon_bpp);

        if (color_table && label && port_bpp >= 4) {
            RGBColor rgb;
            if (C_GetLabel(label, &rgb, NULL) == noErr) {
                uint16_t label_rgb[3];
                label_rgb[0] = CW(rgb.red);
                label_rgb[1] = CW(rgb.green);
                label_rgb[2] = CW(rgb.blue);
                for (unsigned i = 0; i <= (unsigned)CTAB_SIZE(color_table); ++i) {
                    RGBColor* entry = &CTAB_TABLE(color_table)[i].rgb;
                    uint16_t color[3];
                    color[0] = CW(entry->red);
                    color[1] = CW(entry->green);
                    color[2] = CW(entry->blue);
                    if (port_bpp < 8 && icon_bpp == 8) {
                        if (i == 255)
                            memcpy(color, label_rgb, sizeof color);
                    }
                    else
                        ROMlib_icon_label_rgb(color, label_rgb, icon_bpp, i);
                    entry->red = CW(color[0]);
                    entry->green = CW(color[1]);
                    entry->blue = CW(color[2]);
                }
                CTAB_SEED_X(color_table) = CL(GetCTSeed());
            }
        }

        memset(&icon_pm, '\000', sizeof icon_pm);
        memset(&icon_rect, '\000', sizeof icon_rect);

        icon_size = (little_icon_p ? 16 : 32);
        icon_rect.bottom = icon_rect.right = CW(icon_size);

        FIELD_ASSIGN_RAW(icon_pm.baseAddr, HIDDEN_RAW(*(icon_data)));
        icon_pm.rowBytes = CW((icon_size * icon_bpp / 8) | PIXMAP_DEFAULT_ROW_BYTES);
        icon_pm.bounds = icon_rect;
        icon_pm.pixelSize = icon_pm.cmpSize = CW(icon_bpp);
        icon_pm.cmpCount = CWC(1);
        FIELD_ASSIGN_RAW(icon_pm.pmTable, RM(color_table));

        PP_ASSIGN(mask_bm.baseAddr, (char*)STARH(icon_mask) + icon_size * icon_size / 8);
        mask_bm.rowBytes = CW(icon_size / 8);
        mask_bm.bounds = icon_rect;

        CopyMask((BitMap*)&icon_pm, &mask_bm, PORT_BITS_FOR_COPY(current_port), &icon_pm.bounds, &mask_bm.bounds,
                 /* #### fix up the need for this cast */
                 (Rect*)rect);

        DisposCTable(color_table);
    });

    if (expanded)
        DisposHandle(expanded);
    HSetState(icon_mask, mask_state);

    ICON_RETURN_ERROR(noErr);
}

P4(PUBLIC pascal trap, OSErr, ForEachIconDo, Handle, suite, IconSelectorValue, selector, IconActionProcPtr, action,
   void*, data)
{
    int i;
    if (!suite || !STARH(suite) || !action || GetHandleSize(suite) < (Size)sizeof(cotton_suite_layout_t))
        ICON_RETURN_ERROR(paramErr);
    for (i = 0; i < N_SUITE_ICONS; ++i)
        if (selector & mask_for_icon[i]) {
            Handle* icons = (Handle*)STARH(suite);
            if (icons[i]) {
                /* Suites contain host handles, but the callback's Handle*
               refers to a big-endian guest handle and may replace it. */
                Handle guest_icon = RM(icons[i]);
                OSErr err = CToPascalCall(action, CTOP_IconActionProcTemplate, restype_for_icon[i], &guest_icon, data);
                icons = (Handle*)STARH(suite);
                icons[i] = MR(guest_icon);
                if (err != noErr)
                    ICON_RETURN_ERROR(err);
            }
        }
    ICON_RETURN_ERROR(noErr);
}

P1(PUBLIC pascal trap, short, GetSuiteLabel, Handle, suite)
{
    short retval;
    cotton_suite_layout_t* suitep;

    suitep = (cotton_suite_layout_t*)STARH(suite);
    retval = CW(suitep->label);
    return retval;
}

P2(PUBLIC pascal trap, OSErr, SetSuiteLabel, Handle, suite, short, label)
{
    OSErr retval;
    cotton_suite_layout_t* suitep;

    suitep = (cotton_suite_layout_t*)STARH(suite);
    suitep->label = CW(label);
    retval = noErr;

    return retval;
}

typedef struct {
    RGBColor rgb_color;
    char* string;
} label_info_t;

/* System 7 label numbering and RGB defaults from IconUtils.r. */
PRIVATE const label_info_t labels[8] = {
    {{0, 0, 0}, "\004None"},
    {{CWC(0x5600), CWC(0x2c9d), CWC(0x0524)}, "\011Project 2"},
    {{CWC(0x0000), CWC(0x64af), CWC(0x11b0)}, "\011Project 1"},
    {{CWC(0x0000), CWC(0x0000), CWC(0xd400)}, "\010Personal"},
    {{CWC(0x0241), CWC(0xab54), CWC(0xeaff)}, "\004Cool"},
    {{CWC(0xf2d7), CWC(0x0856), CWC(0x84ec)}, "\013In Progress"},
    {{CWC(0xdd6b), CWC(0x08c2), CWC(0x06a2)}, "\003Hot"},
    {{CWC(0xffff), CWC(0x648a), CWC(0x028c)}, "\011Essential"},
};

P3(PUBLIC pascal trap, OSErr, GetLabel, short, label, RGBColor*, label_color, Str255, label_string)
{
    if ((unsigned)label >= 8)
        ICON_RETURN_ERROR(paramErr);
    if (label_color)
        *label_color = labels[label].rgb_color;
    if (label_string)
        memcpy(label_string, labels[label].string, labels[label].string[0] + 1);
    ICON_RETURN_ERROR(noErr);
}

P2(PUBLIC pascal trap, OSErr, DisposeIconSuite, Handle, suite, Boolean, dispose_data_p)
{
    if (!suite || !STARH(suite) || GetHandleSize(suite) < (Size)sizeof(cotton_suite_layout_t) ||
        (((cotton_suite_layout_t*)STARH(suite))->signature != CLC(ICON_SUITE_SIGNATURE) && !icon_cache_p(suite)))
        ICON_RETURN_ERROR(paramErr);

    /* A stale suite handle can already name a different live heap object.
     Invalidate the identity before freeing any children, including callbacks
     or later cleanup attempts which may still retain this handle. */
    ((cotton_suite_layout_t*)STARH(suite))->signature = 0;
    if (dispose_data_p) {
        LOCK_HANDLE_EXCURSION_1(suite, {
            Handle* icons;
            int i;

            icons = (Handle*)STARH(suite);
            for (i = 0; i < N_SUITE_ICONS; i++) {
                Handle icon;
                SignedByte icon_state;

                icon = icons[i];
                if (icon) {
                    icon_state = HGetState(icon);
                    if (icon_state & RSRCBIT)
                        ;
                    else
                        DisposHandle(icons[i]);
                }
            }
        });
    }

    DisposHandle(suite);

    ICON_RETURN_ERROR(noErr);
}

P4(PUBLIC pascal trap, OSErr, IconSuiteToRgn, RgnHandle, rgn, const Rect*, rect, IconAlignmentType, align, Handle,
   suite)
{
    Handle image, mask;
    boolean_t small;
    int depth;
    if (!rgn || !rect || !suite)
        ICON_RETURN_ERROR(paramErr);
    OSErr error =
        find_best_icon(RECT_WIDTH(rect) < 32 && RECT_HEIGHT(rect) < 32, 1, suite, &image, &mask, &small, &depth);
    if (!error) {
        int size = small ? 16 : 32;
        Size plane = size * size / 8;
        if (GetHandleSize(mask) < plane * 2)
            error = paramErr;
        else {
            Rect aligned;
            error = align_icon_rect(mask, size, rect, align, &aligned);
            if (error)
                ICON_RETURN_ERROR(error);
            LOCK_HANDLE_EXCURSION_1(mask, {
                BitMap bitmap = {0};
                FIELD_ASSIGN_RAW(bitmap.baseAddr, RM((Ptr)STARH(mask) + plane));
                bitmap.rowBytes = CW(size / 8);
                SetRect(&bitmap.bounds, 0, 0, size, size);
                error = BitMapToRegion(rgn, &bitmap);
                if (!error)
                    MapRgn(rgn, &bitmap.bounds, &aligned);
            });
        }
    }
    ICON_RETURN_ERROR(error);
}

P4(PUBLIC pascal trap, OSErr, IconIDToRgn, RgnHandle, rgn, const Rect*, rect, IconAlignmentType, align, short, icon_id)
{
    if (!rgn || !rect)
        ICON_RETURN_ERROR(paramErr);
    Handle suite;
    OSErr error = _GetIconSuite(&suite, icon_id, svAllAvailableData);
    if (!error) {
        error = IconSuiteToRgn(rgn, rect, align, suite);
        DisposeIconSuite(suite, FALSE);
    }
    ICON_RETURN_ERROR(error);
}

P5(PUBLIC pascal trap, OSErr, IconMethodToRgn, RgnHandle, rgn, const Rect*, rect, IconAlignmentType, align,
   IconGetterProcPtr, method, void*, data)
{
    Handle suite;
    OSErr error = suite_from_method(method, data, &suite);
    if (!error) {
        error = IconSuiteToRgn(rgn, rect, align, suite);
        DisposeIconSuite(suite, FALSE);
    }
    ICON_RETURN_ERROR(error);
}

P4(PUBLIC pascal trap, Boolean, PtInIconSuite, Point, test_pt, const Rect*, rect, IconAlignmentType, align, Handle,
   suite)
{
    RgnHandle region = NewRgn();
    if (!region)
        return FALSE;
    Boolean result = !IconSuiteToRgn(region, rect, align, suite) && PtInRgn(test_pt, region);
    DisposeRgn(region);
    return result;
}

P4(PUBLIC pascal trap, Boolean, PtInIconID, Point, test_pt, const Rect*, rect, IconAlignmentType, align, short, icon_id)
{
    RgnHandle region = NewRgn();
    if (!region)
        return FALSE;
    Boolean result = !IconIDToRgn(region, rect, align, icon_id) && PtInRgn(test_pt, region);
    DisposeRgn(region);
    return result;
}

P5(PUBLIC pascal trap, Boolean, PtInIconMethod, Point, test_pt, const Rect*, rect, IconAlignmentType, align,
   IconGetterProcPtr, method, void*, data)
{
    Handle suite;
    if (suite_from_method(method, data, &suite))
        return FALSE;
    Boolean result = PtInIconSuite(test_pt, rect, align, suite);
    DisposeIconSuite(suite, FALSE);
    return result;
}

P4(PUBLIC pascal trap, Boolean, RectInIconSuite, const Rect*, test_rect, const Rect*, rect, IconAlignmentType, align,
   Handle, suite)
{
    if (!test_rect)
        return FALSE;
    RgnHandle region = NewRgn();
    if (!region)
        return FALSE;
    Boolean result = !IconSuiteToRgn(region, rect, align, suite) && RectInRgn((Rect*)test_rect, region);
    DisposeRgn(region);
    return result;
}

P4(PUBLIC pascal trap, Boolean, RectInIconID, const Rect*, test_rect, const Rect*, rect, IconAlignmentType, align,
   short, icon_id)
{
    if (!test_rect)
        return FALSE;
    RgnHandle region = NewRgn();
    if (!region)
        return FALSE;
    Boolean result = !IconIDToRgn(region, rect, align, icon_id) && RectInRgn((Rect*)test_rect, region);
    DisposeRgn(region);
    return result;
}

P5(PUBLIC pascal trap, Boolean, RectInIconMethod, const Rect*, test_rect, const Rect*, rect, IconAlignmentType, align,
   IconGetterProcPtr, method, void*, data)
{
    if (!test_rect)
        return FALSE;
    Handle suite;
    if (suite_from_method(method, data, &suite))
        return FALSE;
    Boolean result = RectInIconSuite(test_rect, rect, align, suite);
    DisposeIconSuite(suite, FALSE);
    return result;
}

P3(PUBLIC pascal trap, OSErr, MakeIconCache, Handle*, cache, IconGetterProcPtr, make_icon, void*, data)
{
    if (!cache)
        ICON_RETURN_ERROR(paramErr);
    Handle result = NewHandleClear(sizeof(cotton_cache_layout_t));
    if (!result)
        ICON_RETURN_ERROR(memFullErr);
    cotton_cache_layout_t* record = (cotton_cache_layout_t*)STARH(result);
    record->suite.signature = CLC(ICON_CACHE_SIGNATURE);
    record->data = (uint32)(uintptr_t)RM(data);
    record->procedure = (uint32)(uintptr_t)RM(make_icon);
    store_icon_handle(cache, result);
    ICON_RETURN_ERROR(noErr);
}

P4(PUBLIC pascal trap, OSErr, LoadIconCache, const Rect*, rect, IconAlignmentType, align, IconTransformType, transform,
   Handle, cache)
{
    if (!rect || !icon_cache_p(cache))
        ICON_RETURN_ERROR(paramErr);
    GrafPtr port = thePort;
    Handle image, mask;
    boolean_t small;
    int depth;
    OSErr error = find_best_icon(RECT_WIDTH(rect) < 32 && RECT_HEIGHT(rect) < 32, icon_port_depth(port), cache, &image,
                                 &mask, &small, &depth);
    /* This is the same selection as plotting, with no drawing side effects. */
    ICON_RETURN_ERROR(error);
}

P2(PUBLIC pascal trap, OSErr, GetIconCacheData, Handle, cache, void**, data)
{
    if (!data || !icon_cache_p(cache))
        ICON_RETURN_ERROR(paramErr);
    memcpy(data, &((cotton_cache_layout_t*)STARH(cache))->data, sizeof(uint32));
    ICON_RETURN_ERROR(noErr);
}

P2(PUBLIC pascal trap, OSErr, SetIconCacheData, Handle, cache, void*, data)
{
    if (!icon_cache_p(cache))
        ICON_RETURN_ERROR(paramErr);
    ((cotton_cache_layout_t*)STARH(cache))->data = (uint32)(uintptr_t)RM(data);
    ICON_RETURN_ERROR(noErr);
}

P2(PUBLIC pascal trap, OSErr, GetIconCacheProc, Handle, cache, IconGetterProcPtr*, proc)
{
    if (!proc || !icon_cache_p(cache))
        ICON_RETURN_ERROR(paramErr);
    memcpy(proc, &((cotton_cache_layout_t*)STARH(cache))->procedure, sizeof(uint32));
    ICON_RETURN_ERROR(noErr);
}

P2(PUBLIC pascal trap, OSErr, SetIconCacheProc, Handle, cache, IconGetterProcPtr, proc)
{
    if (!icon_cache_p(cache))
        ICON_RETURN_ERROR(paramErr);
    ((cotton_cache_layout_t*)STARH(cache))->procedure = (uint32)(uintptr_t)RM(proc);
    ICON_RETURN_ERROR(noErr);
}
