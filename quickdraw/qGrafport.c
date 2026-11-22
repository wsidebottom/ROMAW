/*+===================================================================
  File:      qGrafport.c

  Summary:   Implements q grafport support for Macintosh QuickDraw graphics.

  Classes:   None.

  Functions: ROMlib_QDGetPatternOrigin, ROMlib_QDSetPatternOrigin, ROMlib_fill_pat, ROMlib_initport,
             ROMlib_pattern_origin, ROMlib_register_qd_port, ROMlib_valid_qd_port

  Origin:    Existing repository source in executor/src/quickdraw; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986-1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in QuickDraw.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "QuickDraw.h"
#include "CQuickDraw.h"
#include "ToolboxEvent.h"
#include "MemoryMgr.h"
#include "WindowMgr.h"
#include "SysErr.h"

#include "rsys/mman.h"
#include "rsys/mman_private.h"
#include "rsys/cquick.h"
#include "rsys/picture.h"
#include "rsys/pattern_origin.h"
#include "rsys/wind.h"

static Point global_pattern_origin;

/* The QuickDraw port list is a system-zone Handle with a packed big-endian
   count followed by guest port pointers. CarbonLib reads it directly. */
static Handle qd_port_list(void)
{
    const uint32_t address = (uint32_t)HIDDEN_RAW(ROMlib_qd_port_list_H);
    if (!address || address == UINT32_MAX)
        return NULL;
    Handle list = HIDDEN_NATIVE(ROMlib_qd_port_list_H);
    return VALID_ADDRESS(list) && ROMlib_memtop - (uintptr_t)list >= sizeof *list ? list : NULL;
}

static unsigned qd_port_count(Handle list, Size size)
{
    uint16_t count;
    if (!list || size < (Size)sizeof count)
        return 0;
    memcpy(&count, STARH(list), sizeof count);
    return MIN((unsigned)CW(count), (size - sizeof count) / sizeof(uint32_t));
}

static unsigned qd_port_index(Handle list, unsigned count, GrafPtr port)
{
    if (!count)
        return 0;
    const uint32_t address = CL(US_TO_SYN68K(port));
    const Byte* entries = (const Byte*)STARH(list) + sizeof(uint16_t);
    for (unsigned i = 0; i < count; ++i)
        if (!memcmp(entries + i * sizeof address, &address, sizeof address))
            return i;
    return count;
}

Boolean ROMlib_register_qd_port(GrafPtr port)
{
    Handle list = qd_port_list();
    if (!list) {
        ZONE_SAVE_EXCURSION(SysZone, { list = NewHandleClear(2 + 32 * sizeof(uint32_t)); });
        if (!list)
            return FALSE;
        HIDDEN_RAW_SET(ROMlib_qd_port_list_H, CL(US_TO_SYN68K(list)));
    }
    const Size size = GetHandleSize(list);
    const unsigned count = qd_port_count(list, size);
    if (qd_port_index(list, count, port) != count)
        return TRUE;
    if (count == INT16_MAX)
        return FALSE;
    if (size < (Size)(2 + (count + 1) * sizeof(uint32_t))) {
        SetHandleSize(list, 2 + MIN(INT16_MAX, 2 * (count + 1)) * sizeof(uint32_t));
        if (MemErr != CWC(noErr))
            return FALSE;
    }
    const uint16_t updated = CW(count + 1);
    const uint32_t address = CL(US_TO_SYN68K(port));
    memcpy((Byte*)STARH(list) + sizeof updated + count * sizeof address, &address, sizeof address);
    memcpy(STARH(list), &updated, sizeof updated);
    return TRUE;
}

static void unregister_qd_port(GrafPtr port)
{
    Handle list = qd_port_list();
    if (!list)
        return;
    unsigned count = qd_port_count(list, GetHandleSize(list));
    const unsigned index = qd_port_index(list, count, port);
    if (index == count)
        return;
    --count;
    Byte* entries = (Byte*)STARH(list) + sizeof(uint16_t);
    memmove(entries + index * sizeof(uint32_t), entries + (index + 1) * sizeof(uint32_t),
            (count - index) * sizeof(uint32_t));
    const uint16_t updated = CW(count);
    memcpy(STARH(list), &updated, sizeof updated);
}

static int live_qd_handle(Handle handle, Size minimum)
{
    return handle && HandleZone(handle) && GetHandleSize(handle) >= minimum && MemErr == CWC(noErr);
}

Boolean ROMlib_valid_qd_port(GrafPtr port)
{
    Handle list = qd_port_list();
    if (!list || !port)
        return FALSE;
    const OSErr saved = MemErr;
    const unsigned count = qd_port_count(list, GetHandleSize(list));
    int valid = qd_port_index(list, count, port) != count &&
                live_qd_handle((Handle)PORT_VIS_REGION(port), sizeof(Region)) &&
                live_qd_handle((Handle)PORT_CLIP_REGION(port), sizeof(Region));
    if (valid && CGrafPort_p(port))
        valid = live_qd_handle((Handle)CPORT_PIXMAP(port), sizeof(PixMap)) &&
                live_qd_handle((Handle)CPORT_GRAFVARS(port), sizeof(GrafVars)) &&
                live_qd_handle((Handle)CPORT_BK_PIXPAT(port), sizeof(PixPat)) &&
                live_qd_handle((Handle)CPORT_PEN_PIXPAT(port), sizeof(PixPat)) &&
                live_qd_handle((Handle)CPORT_FILL_PIXPAT(port), sizeof(PixPat));
    MemErr = saved;
    return valid;
}

/* System 9 NQD uses the shared QD origin unless GrafVars flag 0x4000
   enables an extended per-port Point at offset 0x2e. Preserve old GrafVars
   layouts and never read or write their nonexistent extension. */
static Point* port_pattern_origin(void)
{
    if (thePort && CGrafPort_p(thePort)) {
        GrafVarsHandle vars = CPORT_GRAFVARS(theCPort);
        if (vars && STARH(vars) && (Hx(vars, pmFlags) & 0x4000)) {
            INTEGER saved = MemErr;
            Size size = GetHandleSize((Handle)vars);
            MemErr = saved;
            if (size >= 0x32)
                return (Point*)((char*)STARH(vars) + 0x2e);
        }
    }
    return &global_pattern_origin;
}

Point ROMlib_pattern_origin(void)
{
    Point* origin = port_pattern_origin();
    return (Point){CW(origin->v), CW(origin->h)};
}

void ROMlib_QDGetPatternOrigin(Point* origin)
{
    ROMlib_qd_error = origin ? noErr : paramErr;
    if (origin && thePort)
        *origin = *port_pattern_origin();
}

void ROMlib_QDSetPatternOrigin(Point origin)
{
    ROMlib_qd_error = noErr;
    if (thePort)
        *port_pattern_origin() = (Point){CW(origin.v), CW(origin.h)};
}

P1(PUBLIC pascal trap, void, InitGraf, Ptr, gp)
{
    PixMapHandle main_gd_pixmap;

    global_pattern_origin = (Point){0, 0};
    Handle ports = qd_port_list();
    if (ports && GetHandleSize(ports) >= (Size)sizeof(uint16_t))
        memset(STARH(ports), 0, sizeof(uint16_t));

    HIDDEN_RAW_SET(*(HIDDEN_Ptr*)SYN68K_TO_US(a5), RM(gp));

    main_gd_pixmap = GD_PMAP(MR(MainDevice));

    FIELD_ASSIGN_RAW(/* screenBitsX flag bits must not be set */
                     screenBitsX.baseAddr, PIXMAP_BASEADDR_X(main_gd_pixmap));
    screenBitsX.rowBytes = CW(PIXMAP_ROWBYTES(main_gd_pixmap) / PIXMAP_PIXEL_SIZE(main_gd_pixmap));
    screenBitsX.bounds = PIXMAP_BOUNDS(main_gd_pixmap);

#define patinit(d, s) (*(LONGINT*)d = CLC(s), *((LONGINT*)d + 1) = CLC(s))
    ZONE_SAVE_EXCURSION(SysZone, {
        patinit(white, 0x00000000);
        patinit(black, 0xffffffff);
        patinit(gray, 0xaa55aa55);
        patinit(ltGray, 0x88228822);
        patinit(dkGray, 0x77dd77dd);

        HIDDEN_RAW_SET(WMgrPort_H, RM((WindowPtr)NewPtr(sizeof(GrafPort))));
        OpenPort(MR(WMgrPort));

        HIDDEN_RAW_SET(WMgrCPort_H, (CWindowPtr)RM(NewPtr(sizeof(CGrafPort))));
        OpenCPort(MR(WMgrCPort));

        SET_THEPORT_RAW((GrafPtr)WMgrCPort);
        HIDDEN_RAW_SET(ScrnBase_H, PP_RAW(screenBitsX.baseAddr));

        StuffHex((Ptr)arrowX.data, (StringPtr)("\100000040006000700078007c007e007f"
                                               "007f807c006c0046000600030003000000"));
        StuffHex((Ptr)arrowX.mask, (StringPtr)("\100c000e000f000f800fc00fe00ff00ff"
                                               "80ffc0ffe0fe00ef00cf00878007800380"));
        arrowX.hotSpot.h = arrowX.hotSpot.v = CWC(1);
        CrsrState = 0;

        RndSeed = CL(TickCount());
        ScrVRes = ScrHRes = CWC(72);
        ScreenRow = screenBitsX.rowBytes;
        randSeedX = CLC(1);
        QDExist = EXIST_YES;
    });
}

/*
 * ROMlib_initport does everything except play with cliprgn and visrgn.
 * It exists so that SysError can call it and maniuplate clip and
 * vis without using the memory manager.
 */

A1(PUBLIC, void, ROMlib_initport, GrafPtr, p) /* INTERNAL */
{
    /* this is a grafport, not a cgrafport, so the flag bits must not be
     set.  when we get here, it is likely they they contain garbage,
     so initialize them before using any accesssor macros */
    p->portBits.rowBytes = CWC(0);

    PORT_DEVICE_X(p) = 0;
    PORT_BITS(p) = screenBitsX;
    PORT_RECT(p) = screenBitsX.bounds;
    PATASSIGN(PORT_BK_PAT(p), white);
    PATASSIGN(PORT_FILL_PAT(p), black);
    PATASSIGN(PORT_PEN_PAT(p), black);
    PORT_PEN_LOC(p).h = PORT_PEN_LOC(p).v = CWC(0);
    PORT_PEN_SIZE(p).h = PORT_PEN_SIZE(p).v = CWC(1);
    PORT_PEN_MODE_X(p) = CWC(patCopy);
    PORT_PEN_VIS_X(p) = CWC(0);
    PORT_TX_FONT_X(p) = CWC(0);
    /* txFace is a Style (signed char); don't swap */
    PORT_TX_FACE_X(p) = 0;
    *((char*)&p->txFace + 1) = 0; /* Excel & tests show we need to do this. */
    PORT_TX_MODE_X(p) = CWC(srcOr);
    PORT_TX_SIZE_X(p) = CWC(0);
    PORT_SP_EXTRA_X(p) = CWC(0);
    PORT_FG_COLOR_X(p) = CLC(blackColor);
    PORT_BK_COLOR_X(p) = CLC(whiteColor);
    PORT_COLR_BIT_X(p) = CWC(0);
    PORT_PAT_STRETCH_X(p) = CWC(0);
    FIELD_ASSIGN_RAW(PORT_PIC_SAVE_X(p), CLC(0));
    FIELD_ASSIGN_RAW(PORT_REGION_SAVE_X(p), CLC(0));
    FIELD_ASSIGN_RAW(PORT_POLY_SAVE_X(p), CLC(0));
    FIELD_ASSIGN_RAW(PORT_GRAF_PROCS_X(p), CLC(0));
}

P1(PUBLIC pascal trap, void, SetPort, GrafPtr, p)
{
    if (p == NULL)
        warning_unexpected("SetPort(NULL_STRING)");
    SET_THEPORT_RAW(RM(p));
}

P1(PUBLIC pascal trap, void, InitPort, GrafPtr, p)
{
    ROMlib_initport(p);
    SetEmptyRgn(PORT_VIS_REGION(p));
    SetEmptyRgn(PORT_CLIP_REGION(p));
    HxX(PORT_VIS_REGION(p), rgnBBox) = screenBitsX.bounds;

    SetRect(&HxX(PORT_CLIP_REGION(p), rgnBBox), -32767, -32767, 32767, 32767);
    SetPort(p);
}

P1(PUBLIC pascal trap, void, OpenPort, GrafPtr, p)
{
    RgnHandle visible = NewRgn();
    RgnHandle clip = visible ? NewRgn() : NULL;
    if (!clip || !ROMlib_register_qd_port(p)) {
        if (clip)
            DisposeRgn(clip);
        if (visible)
            DisposeRgn(visible);
        MemErr = CWC(memFullErr);
        return;
    }
    FIELD_ASSIGN_RAW(PORT_VIS_REGION_X(p), RM(visible));
    FIELD_ASSIGN_RAW(PORT_CLIP_REGION_X(p), RM(clip));
    InitPort(p);
}

/*
 * "Five of a Kind" calls CloseWindow (x) followed by ClosePort (x).
 * That used to kill us, but checking MemErr gets around that problem.
 * I'm not sure how the Mac gets around it.
 */

P1(PUBLIC pascal trap, void, ClosePort, GrafPtr, p)
{
    unregister_qd_port(p);
    DisposeRgn(PORT_VIS_REGION(p));
    if (MemErr == CWC(noErr)) {
        DisposeRgn(PORT_CLIP_REGION(p));

        if (MemErr == CWC(noErr)) {
            if (CGrafPort_p(p)) {
                DisposPixPat(CPORT_BK_PIXPAT(p));
                DisposPixPat(CPORT_PEN_PIXPAT(p));
                DisposPixPat(CPORT_FILL_PIXPAT(p));
                DisposHandle((Handle)CPORT_PIXMAP(p)); /* NOT DisposPixMap */

                DisposHandle((Handle)CPORT_GRAFVARS(p));
            }
        }
    }
}

P1(PUBLIC pascal trap, void, GetPort, HIDDEN_GrafPtr*, pp)
{
    HIDDEN_RAW_SET((*pp), thePortX);
}

P1(PUBLIC pascal trap, void, GrafDevice, INTEGER, d)
{
    PORT_DEVICE_X(thePort) = Cx(d);
}

P1(PUBLIC pascal trap, void, SetPortBits, BitMap*, bm)
{
    if (!CGrafPort_p(thePort))
        PORT_BITS(thePort) = *bm;
}

P2(PUBLIC pascal trap, void, PortSize, INTEGER, w, INTEGER, h)
{
    PORT_RECT(thePort).bottom = CW(CW(PORT_RECT(thePort).top) + h);
    PORT_RECT(thePort).right = CW(CW(PORT_RECT(thePort).left) + w);
}

P2(PUBLIC pascal trap, void, MovePortTo, INTEGER, lg, INTEGER, tg)
{
    GrafPtr current_port;
    Rect *port_bounds, *port_rect;
    int w, h;

    current_port = thePort;
    port_bounds = &PORT_BOUNDS(current_port);
    port_rect = &PORT_RECT(current_port);
    lg = CW(port_rect->left) - lg;
    tg = CW(port_rect->top) - tg;
    w = RECT_WIDTH(port_bounds);
    h = RECT_HEIGHT(port_bounds);
    SetRect(port_bounds, lg, tg, lg + w, tg + h);
}

P2(PUBLIC pascal trap, void, SetOrigin, INTEGER, h, INTEGER, v)
{
    int32 dh, dv;

    dh = h - Cx(PORT_RECT(thePort).left);
    dv = v - Cx(PORT_RECT(thePort).top);
    PIC_SAVE_EXCURSION({
        int16 swappeddh;
        int16 swappeddv;

        PICOP(OP_Origin);

        swappeddh = CW(dh);
        PICWRITE(&swappeddh, sizeof swappeddh);
        swappeddv = CW(dv);
        PICWRITE(&swappeddv, sizeof swappeddv);
    });
    OffsetRect(&PORT_BOUNDS(thePort), dh, dv);
    OffsetRect(&PORT_RECT(thePort), dh, dv);
    OffsetRgn(PORT_VIS_REGION(thePort), dh, dv);
    ROMlib_offset_update_vis(thePort, dh, dv);
}

P1(PUBLIC pascal trap, void, SetClip, RgnHandle, r)
{
    /* A missing saved region must not replace the current clip or crash the
       host. REALbasic can reach this cleanup path without acquiring a clip. */
    if (!r)
        return;
    CopyRgn(r, PORT_CLIP_REGION(thePort));
}

P1(PUBLIC pascal trap, void, GetClip, RgnHandle, r)
{
    CopyRgn(PORT_CLIP_REGION(thePort), r);
}

P1(PUBLIC pascal trap, void, ClipRect, Rect*, r)
{
    Rect r_copy;

    /* We copy r to a local, in case it points to memory that might move
   * during the RectRgn.
   */
    r_copy = *r;
    RectRgn(PORT_CLIP_REGION(thePort), &r_copy);
}

P1(PUBLIC pascal trap, void, BackPat, Pattern, pp)
{
    if (CGrafPort_p(thePort)) {
        PixPatHandle old_bk;

        old_bk = CPORT_BK_PIXPAT(theCPort);
        if (old_bk && PIXPAT_TYPE_X(old_bk) == pixpat_type_orig)
            PATASSIGN(PIXPAT_1DATA(old_bk), pp);
        else {
            PixPatHandle new_bk = NewPixPat();

            PIXPAT_TYPE_X(new_bk) = CWC(0);
            PATASSIGN(PIXPAT_1DATA(new_bk), pp);
            BackPixPat(new_bk);
        }
        /*
 * On a Mac, PIXPAT_1DATA is *not* updated here, instead a new pixpat
 * is created and the other fields are set up to point to this guy.
 */

        /* #warning BackPat not currently implemented properly ... */
    }
    else
        PATASSIGN(PORT_BK_PAT(thePort), pp);
}

A1(PUBLIC, void, ROMlib_fill_pat, Pattern, pp) /* INTERNAL */
{
    /* A required Pattern argument can refer to Macintosh address zero.
     The trap bridge translates that value to NULL; restore the low-memory
     address rather than dereferencing a host null pointer. */
    if (!pp)
        pp = (Ptr)SYN68K_TO_US(0);

    if (CGrafPort_p(thePort)) {
        PixPatHandle old_fill;

        old_fill = CPORT_FILL_PIXPAT(theCPort);
        if (PIXPAT_TYPE_X(old_fill) == CWC(pixpat_type_orig))
            PATASSIGN(PIXPAT_1DATA(old_fill), pp);
        else {
            PixPatHandle new_fill = NewPixPat();

            PIXPAT_TYPE_X(new_fill) = CWC(0);
            PATASSIGN(PIXPAT_1DATA(new_fill), pp);

            ROMlib_fill_pixpat(new_fill);
        }
        /*
 * It's not clear what we're supposed to be doing here.
 */
        /* #warning ROMlib_fill_pat not currently implemented properly... */
    }
    else
        PATASSIGN(PORT_FILL_PAT(thePort), pp);
}
