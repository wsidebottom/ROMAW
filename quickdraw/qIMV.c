/*+===================================================================
  File:      qIMV.c

  Summary:   Implements q imv support for Macintosh QuickDraw graphics.

  Classes:   None.

  Functions: default_search_proc_stub

  Origin:    Existing repository source in executor/src/quickdraw; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1989, 1990, 1994, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in QuickDraw.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "QuickDraw.h"
#include "MemoryMgr.h"

#include "rsys/pstuff.h"
#include "rsys/cquick.h"
#include "rsys/tempalloc.h"

/* these stubs are here to make my Pic V2 code work */

P1(PUBLIC pascal trap, void, CharExtra, Fixed, Extra) /* IMV-77 */
{
    /* TODO */
    /* #warning CharExtra not implemented */
    warning_unimplemented(NULL_STRING);
}

P1(PUBLIC pascal trap, void, SetStdCProcs, CQDProcs*, cProcs) /* IMV-77 */
{
    FIELD_ASSIGN_RAW(cProcs->textProc, RM((Ptr)P_StdText));
    FIELD_ASSIGN_RAW(cProcs->lineProc, RM((Ptr)P_StdLine));
    FIELD_ASSIGN_RAW(cProcs->rectProc, RM((Ptr)P_StdRect));
    FIELD_ASSIGN_RAW(cProcs->rRectProc, RM((Ptr)P_StdRRect));
    FIELD_ASSIGN_RAW(cProcs->ovalProc, RM((Ptr)P_StdOval));
    FIELD_ASSIGN_RAW(cProcs->arcProc, RM((Ptr)P_StdArc));
    FIELD_ASSIGN_RAW(cProcs->polyProc, RM((Ptr)P_StdPoly));
    FIELD_ASSIGN_RAW(cProcs->rgnProc, RM((Ptr)P_StdRgn));
    FIELD_ASSIGN_RAW(cProcs->bitsProc, RM((Ptr)P_StdBits));
    FIELD_ASSIGN_RAW(cProcs->commentProc, RM((Ptr)P_StdComment));
    PP_RAW(cProcs->txMeasProc) = RM((Ptr)P_StdTxMeas);
    PP_RAW(cProcs->getPicProc) = RM((Ptr)P_StdGetPic);
    PP_RAW(cProcs->putPicProc) = RM((Ptr)P_StdPutPic);
    PP_RAW(cProcs->opcodeProc) = RM((Ptr)0) /* ??? */;
    PP_RAW(cProcs->newProc1Proc) = RM((Ptr)0) /* ??? */;
    PP_RAW(cProcs->newProc2Proc) = RM((Ptr)0) /* ??? */;
    PP_RAW(cProcs->newProc3Proc) = RM((Ptr)0) /* ??? */;
    PP_RAW(cProcs->newProc4Proc) = RM((Ptr)0) /* ??? */;
    PP_RAW(cProcs->newProc5Proc) = RM((Ptr)0) /* ??? */;
    PP_RAW(cProcs->newProc6Proc) = RM((Ptr)0) /* ??? */;
}

P3(PUBLIC pascal trap, void, GetCPixel, INTEGER, h, INTEGER, v, RGBColor*, pixelp)
{
    GrafPtr port = thePort;
    CTabHandle ctab;
    ColorSpec* cspec;
    const rgb_spec_t* spec = NULL;
    const uint8 *base, *sample;
    Rect bounds;
    int row_bytes, x;
    int pixval;
    int bpp;

    if (!pixelp || !port)
        return;

    if (CGrafPort_p(port)) {
        PixMapHandle port_pm = CPORT_PIXMAP(port);

        bpp = PIXMAP_PIXEL_SIZE(port_pm);
        ctab = PIXMAP_TABLE(port_pm);
        bounds = PIXMAP_BOUNDS(port_pm);
        row_bytes = PIXMAP_ROWBYTES(port_pm);
        base = (const uint8*)C_GetPixBaseAddr(port_pm);
        spec = pixmap_rgb_spec(STARH(port_pm));
    }
    else {
        bpp = 1;
        ctab = validate_relative_bw_ctab();
        bounds = PORT_BITS(port).bounds;
        row_bytes = BITMAP_ROWBYTES(&PORT_BITS(port));
        base = (const uint8*)PPR(PORT_BITS(port).baseAddr);
    }
    if (!base || h < CW(bounds.left) || h >= CW(bounds.right) || v < CW(bounds.top) || v >= CW(bounds.bottom))
        return;

    /* Reading a pixel must not apply the port's foreground/background mapping,
     transfer mode or clipping, as CopyBits would. */
    x = h - CW(bounds.left);
    sample = base + (v - CW(bounds.top)) * row_bytes + x * bpp / 8;

    if (bpp > 8) {
        uint32 pixel = 0;

        if (bpp == 16) {
            uint16 value;
            memcpy(&value, sample, sizeof value);
            pixel = value;
        }
        else
            memcpy(&pixel, sample, sizeof pixel);
        spec->pixel_to_rgbcolor(spec, pixel, pixelp);
    }
    else {
        /* extract the pixel */
        pixval = (*sample >> (8 - bpp - (x * bpp & 7))) & ((1 << bpp) - 1);

        /* Convert that pixel to an RGB value. */
        cspec = CTAB_TABLE(ctab);
        if (CTAB_FLAGS_X(ctab) & CTAB_GDEVICE_BIT_X)
            *pixelp = cspec[pixval].rgb;
        else {
            int16 swapped_pixval;
            int i;

            /* non-device color tables aren't guaranteed to be sorted, so we
	     need to hunt for an entry with the specified value */
            swapped_pixval = CW(pixval);
            for (i = CTAB_SIZE(ctab); i >= 0; i--)
                if (cspec[i].value == swapped_pixval)
                    break;
            if (i >= 0)
                *pixelp = cspec[i].rgb;
            else {
                warning_unexpected("Failed to find match in GetCPixel.");
                pixelp->red = pixelp->green = pixelp->blue = CWC(0);
            }
        }
    }
}

P3(PUBLIC pascal trap, void, SetCPixel, INTEGER, h, INTEGER, v, RGBColor*, pixelp)
{
    Rect temp_rect;

    GrafPtr port;
    boolean_t cgrafport_p;

    RGBColor save_fg_rgb;
    int32 save_fg;

    temp_rect.top = CW(v);
    temp_rect.bottom = CW(v + 1);
    temp_rect.left = CW(h);
    temp_rect.right = CW(h + 1);

    port = thePort;
    cgrafport_p = CGrafPort_p(port);

    save_fg = PORT_FG_COLOR_X(port);
    if (cgrafport_p)
        save_fg_rgb = CPORT_RGB_FG_COLOR(port);

    RGBForeColor(pixelp);
    PenMode(patCopy);

    FillRect(&temp_rect, black);

    PORT_FG_COLOR_X(port) = save_fg;
    if (cgrafport_p)
        CPORT_RGB_FG_COLOR(port) = save_fg_rgb;
}

static int8 default_search_proc(RGBColor* rgb, int32* pixel)
{
    MatchRec* mr;

    mr = (MatchRec*)GD_REF_CON(MR(TheGDevice));

    if (mr->red == rgb->red && mr->green == rgb->green && mr->blue == rgb->blue)
        *pixel = CL(mr->matchData);
    else
        *pixel = CL(!mr->matchData);
    return TRUE;
}

syn68k_addr_t default_search_proc_stub(syn68k_addr_t dummy_addr, void* dummy)
{
    void *arg1, *arg2;
    syn68k_addr_t retval;
    int8 result;

    retval = POPADDR();
    arg2 = (void*)POPADDR();
    arg1 = (void*)POPADDR();

    result = default_search_proc(arg1, arg2);
    WRITEUB(EM_A7, result);

    return retval;
}

P8(PUBLIC pascal trap, void, SeedCFill, BitMap*, srcbp, BitMap*, dstbp, Rect*, srcrp, Rect*, dstrp, int16, seedh, int16,
   seedv, ProcPtr, matchprocp, int32, matchdata)
{
    MatchRec mr;
    LONGINT save_ref_con;
    Handle save_pic_handle;
    QDProcsPtr save_graf_procs;
    GDHandle gdev;
    RGBColor pixel;
    BitMap temp_bitmap1, temp_bitmap2;
    Rect temp_rect;
    int row_words;
    int width, height;
    void* t;

    TEMP_ALLOC_DECL(temp_bitmap1_bits);
    TEMP_ALLOC_DECL(temp_bitmap2_bits);

    gdev = MR(TheGDevice);

    if (!matchprocp) {
        matchprocp = (ProcPtr)SYN68K_TO_US(callback_install(default_search_proc_stub, NULL));
        mr.matchData = 0;
    }
    else {
        mr.matchData = CL(matchdata);
    }

    GetCPixel(seedh, seedv, &pixel);

    mr.red = pixel.red;
    mr.green = pixel.green;
    mr.blue = pixel.blue;
    save_ref_con = GD_REF_CON_X(gdev);
    GD_REF_CON_X(gdev) = (int32)RM(&mr);

    save_pic_handle = PP_RAW(PORT_PIC_SAVE_X(thePort));
    save_graf_procs = PP_RAW(PORT_GRAF_PROCS_X(thePort));

    PP_RAW(PORT_PIC_SAVE_X(thePort)) = CLC(0);
    PP_RAW(PORT_GRAF_PROCS_X(thePort)) = CLC(0);
    FIELD_ASSIGN_RAW(GD_SEARCH_PROC_X(gdev), CLC(0));
    AddSearch(matchprocp);

    width = RECT_WIDTH(srcrp);
    height = RECT_HEIGHT(srcrp);

    temp_rect.top = temp_rect.left = CWC(0);
    temp_rect.right = CW(width);
    temp_rect.bottom = CW(height);

    row_words = (width + 15) / 16;
    temp_bitmap1.rowBytes = CW(row_words * 2);
    TEMP_ALLOC_ALLOCATE(t, temp_bitmap1_bits, row_words * 2 * height);
    PP_RAW(temp_bitmap1.baseAddr) = RM(t);
    memset(PPR(temp_bitmap1.baseAddr), '\377', row_words * 2 * height);
    temp_bitmap1.bounds = temp_rect;

    CopyBits(srcbp, &temp_bitmap1, srcrp, &temp_rect, srcCopy, NULL);

    DelSearch(matchprocp);
    GD_REF_CON_X(gdev) = save_ref_con;

    temp_bitmap2 = temp_bitmap1;
    TEMP_ALLOC_ALLOCATE(t, temp_bitmap2_bits, row_words * 2 * height);
    PP_RAW(temp_bitmap2.baseAddr) = RM(t);

    SeedFill(PPR(temp_bitmap1.baseAddr), PPR(temp_bitmap2.baseAddr), row_words * 2, row_words * 2, height, row_words,
             seedh, seedv);

    CopyBits(&temp_bitmap2, dstbp, &temp_rect, dstrp, srcCopy, NULL);

    PP_RAW(PORT_PIC_SAVE_X(thePort)) = save_pic_handle;
    PP_RAW(PORT_GRAF_PROCS_X(thePort)) = save_graf_procs;

    TEMP_ALLOC_FREE(temp_bitmap1_bits);
    TEMP_ALLOC_FREE(temp_bitmap2_bits);
}

P7(PUBLIC pascal trap, void, CalcCMask, BitMap*, srcbp, BitMap*, dstbp, Rect*, srcrp, Rect*, dstrp, RGBColor*, seedrgbp,
   ProcPtr, matchprocp, int32, matchdata)
{
    MatchRec mr;
    LONGINT save_ref_con;
    Handle save_pic_handle;
    QDProcsPtr save_graf_procs;
    GDHandle gdev;
    BitMap temp_bitmap1, temp_bitmap2;
    Rect temp_rect;
    int row_words;
    int width, height;
    void* t;

    TEMP_ALLOC_DECL(temp_bitmap1_bits);
    TEMP_ALLOC_DECL(temp_bitmap2_bits);

    gdev = MR(TheGDevice);

    if (!matchprocp) {
        matchprocp = (ProcPtr)SYN68K_TO_US(callback_install(default_search_proc_stub, NULL));
        mr.matchData = 1;
    }
    else {
        mr.matchData = CL(matchdata);
    }

    mr.red = seedrgbp->red;
    mr.green = seedrgbp->green;
    mr.blue = seedrgbp->blue;
    save_ref_con = GD_REF_CON_X(gdev);
    GD_REF_CON_X(gdev) = (int32)RM(&mr);

    save_pic_handle = PP_RAW(PORT_PIC_SAVE_X(thePort));
    save_graf_procs = PP_RAW(PORT_GRAF_PROCS_X(thePort));

    PP_RAW(PORT_PIC_SAVE_X(thePort)) = CLC(0);
    PP_RAW(PORT_GRAF_PROCS_X(thePort)) = CLC(0);
    FIELD_ASSIGN_RAW(GD_SEARCH_PROC_X(gdev), CLC(0));
    AddSearch(matchprocp);

    width = RECT_WIDTH(srcrp);
    height = RECT_HEIGHT(srcrp);

    temp_rect.top = temp_rect.left = CWC(0);
    temp_rect.right = CW(width);
    temp_rect.bottom = CW(height);

    row_words = (width + 15) / 16;
    temp_bitmap1.rowBytes = CW(row_words * 2);
    TEMP_ALLOC_ALLOCATE(t, temp_bitmap1_bits, row_words * 2 * height);
    PP_RAW(temp_bitmap1.baseAddr) = RM(t);
    memset(PPR(temp_bitmap1.baseAddr), '\377', row_words * 2 * height);
    temp_bitmap1.bounds = temp_rect;

    CopyBits(srcbp, &temp_bitmap1, srcrp, &temp_rect, srcCopy, NULL);

    DelSearch(matchprocp);
    GD_REF_CON_X(gdev) = save_ref_con;

    temp_bitmap2 = temp_bitmap1;
    TEMP_ALLOC_ALLOCATE(t, temp_bitmap2_bits, row_words * 2 * height);
    PP_RAW(temp_bitmap2.baseAddr) = RM(t);

    CalcMask(PPR(temp_bitmap1.baseAddr), PPR(temp_bitmap2.baseAddr), row_words * 2, row_words * 2, height, row_words);

    CopyBits(&temp_bitmap2, dstbp, &temp_rect, dstrp, srcCopy, NULL);

    PP_RAW(PORT_PIC_SAVE_X(thePort)) = save_pic_handle;
    PP_RAW(PORT_GRAF_PROCS_X(thePort)) = save_graf_procs;

    TEMP_ALLOC_FREE(temp_bitmap1_bits);
    TEMP_ALLOC_FREE(temp_bitmap2_bits);
}
