/*+===================================================================
  File:      prPrinting.c

  Summary:   Implements pr printing support for printing and print settings.

  Classes:   None.

  Functions: ROMlib_acknowledge_job_dialog, print_reinit

  Origin:    Existing repository source in executor/src/printing; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1989 - 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"
#include "rsys/hostdialog.h"
#include "QuickDraw.h"
#include "PrintMgr.h"
#include "MemoryMgr.h"
#include "FontMgr.h"
#include "ResourceMgr.h"

#include "rsys/pstuff.h"
#include "rsys/PSstrings.h"
#include "rsys/notmac.h"
#include "rsys/mman.h"
#include "rsys/print.h"
#include "rsys/string.h"
#include "rsys/cquick.h"
#include "rsys/dirtyrect.h"
#include "rsys/vdriver.h"
#include "rsys/file.h"
#include "rsys/cleanup.h"

#if defined(CYGWIN32)
#include <stdarg.h>

PUBLIC boolean_t deferred_printing_p = FALSE /* TRUE */;

#endif

PUBLIC int pageno = 0; /* This isn't really the way to do it */
PUBLIC int ROMlib_passpostscript = TRUE;
PUBLIC int ROMlib_fontsubstitution = FALSE;

PUBLIC char* ROMlib_document_paper_sizes;
PUBLIC char* ROMlib_paper_size;
PUBLIC const char* ROMlib_paper_size_name;
PUBLIC const char* ROMlib_paper_size_name_terminator;
PUBLIC int ROMlib_rotation;
PUBLIC int ROMlib_translate_x;
PUBLIC int ROMlib_translate_y;
PUBLIC int ROMlib_resolution_x = 72;
PUBLIC int ROMlib_resolution_y = 72;

PUBLIC int ROMlib_paper_x;
PUBLIC int ROMlib_paper_y;

/* This boolean is here to prevent Energy Scheming from causing trouble.
   ES calls PrPageClose twice at the end.  This fix is sub-optimal, but
   probably won't hurt anything. */

PRIVATE boolean_t page_is_open = FALSE;

#include "rsys/nextprint.h"

LONGINT pagewanted = 0;
PRIVATE int lastpagewanted = 0;

P4(PUBLIC pascal trap, void, donotPrArc, GrafVerb, verb, Rect*, r, INTEGER, starta, INTEGER, arca)
{
}

P4(PUBLIC pascal trap, void, PrArc, GrafVerb, verb, Rect*, r, INTEGER, starta, INTEGER, arca)
{
    if (pageno >= pagewanted && pageno <= lastpagewanted)
        NeXTPrArc(verb, (comRect*)r, starta, arca, (comGrafPtr)thePort);
}

P5(PUBLIC pascal trap, void, donotPrBits, BitMap*, srcbmp, Rect*, srcrp, Rect*, dstrp, INTEGER, mode, RgnHandle, mask)
{
}

P5(PUBLIC pascal trap, void, PrBits, BitMap*, srcbmp, Rect*, srcrp, Rect*, dstrp, INTEGER, mode, RgnHandle, mask)
{
    if (pageno >= pagewanted && pageno <= lastpagewanted)
        NeXTPrBits((comBitMap*)srcbmp, (comRect*)srcrp, (comRect*)dstrp, mode, (comRgnHandle)mask, (comGrafPtr)thePort);
}

P1(PUBLIC pascal trap, void, donotPrLine, Point, p)
{
}

P1(PUBLIC pascal trap, void, PrLine, Point, p)
{
    if (pageno >= pagewanted && pageno <= lastpagewanted) {
        comPoint cp;

        cp.h = p.h;
        cp.v = p.v;
        NeXTPrLine(cp, (comGrafPtr)thePort);
    }
}

P2(PUBLIC pascal trap, void, donotPrOval, GrafVerb, v, Rect*, rp)
{
}

P2(PUBLIC pascal trap, void, PrOval, GrafVerb, v, Rect*, rp)
{
    if (pageno >= pagewanted && pageno <= lastpagewanted)
        NeXTPrOval(v, (comRect*)rp, (comGrafPtr)thePort);
}

P4(PUBLIC pascal trap, void, textasPS, INTEGER, n, Ptr, textbufp, Point, num, Point, den)
{
#if 1
    if (pageno >= pagewanted && pageno <= lastpagewanted) {
        NeXTsendps(n, (comPtr)textbufp);
#if !defined(CYGWIN32)
        NeXTsendps(1, (comPtr) "\r");
#else
        NeXTsendps(2, (comPtr) "\r\n");
#endif
    }
#endif
}

P2(PUBLIC pascal trap, void, donotPrGetPic, Ptr, dp, INTEGER, bc)
{
    gui_abort();
}

P2(PUBLIC pascal trap, void, PrGetPic, Ptr, dp, INTEGER, bc)
{
    gui_abort();
    if (pageno >= pagewanted && pageno <= lastpagewanted)
        NeXTPrGetPic((comPtr)dp, bc, (comGrafPtr)thePort);
}

P2(PUBLIC pascal trap, void, donotPrPutPic, Ptr, sp, INTEGER, bc)
{
}

P2(PUBLIC pascal trap, void, PrPutPic, Ptr, sp, INTEGER, bc)
{
    if (pageno >= pagewanted && pageno <= lastpagewanted)
        NeXTPrPutPic((comPtr)sp, bc, (comGrafPtr)thePort);
}

P2(PUBLIC pascal trap, void, donotPrPoly, GrafVerb, verb, PolyHandle, ph)
{
}

P2(PUBLIC pascal trap, void, PrPoly, GrafVerb, verb, PolyHandle, ph)
{
    if (pageno >= pagewanted && pageno <= lastpagewanted)
        NeXTPrPoly(verb, (comPolyHandle)ph, (comGrafPtr)thePort);
}

P4(PUBLIC pascal trap, void, donotPrRRect, GrafVerb, verb, Rect*, r, INTEGER, width, INTEGER, height)
{
}

P4(PUBLIC pascal trap, void, PrRRect, GrafVerb, verb, Rect*, r, INTEGER, width, INTEGER, height)
{
    if (pageno >= pagewanted && pageno <= lastpagewanted)
        NeXTPrRRect(verb, (comRect*)r, width, height, (comGrafPtr)thePort);
}

P2(PUBLIC pascal trap, void, donotPrRect, GrafVerb, v, Rect*, rp)
{
}

P2(PUBLIC pascal trap, void, PrRect, GrafVerb, v, Rect*, rp)
{
    if (pageno >= pagewanted && pageno <= lastpagewanted)
        NeXTPrRect(v, (comRect*)rp, (comGrafPtr)thePort);
}

P2(PUBLIC pascal trap, void, donotPrRgn, GrafVerb, verb, RgnHandle, rgn)
{
}

P2(PUBLIC pascal trap, void, PrRgn, GrafVerb, verb, RgnHandle, rgn)
{
    if (pageno >= pagewanted && pageno <= lastpagewanted)
        NeXTPrRgn(verb, (comRgnHandle)rgn, (comGrafPtr)thePort);
}

P5(PUBLIC pascal trap, INTEGER, PrTxMeas, INTEGER, n, Ptr, p, Point*, nump, Point*, denp, FontInfo*, finfop)
{
    StdTxMeas(n, p, nump, denp, finfop);
    return NeXTPrTxMeas(n, (comPtr)p, (comPoint*)nump, (comPoint*)denp, (comFontInfo*)finfop, (comGrafPtr)thePort);
}

P4(PUBLIC pascal trap, void, donotPrText, INTEGER, n, Ptr, textbufp, Point, num, Point, den)
{
}

P4(PUBLIC pascal trap, void, PrText, INTEGER, n, Ptr, textbufp, Point, num, Point, den)
{
    if (pageno >= pagewanted && pageno <= lastpagewanted) {
        comPoint numc, denc;

        numc.h = num.h;
        numc.v = num.v;
        denc.h = den.h;
        denc.v = den.v;
        NeXTPrText(n, (comPtr)textbufp, numc, denc, (comGrafPtr)thePort);
    }
}

PRIVATE QDProcs prprocs;
PRIVATE QDProcs sendpsprocs;
PRIVATE boolean_t need_restore;
static TPPrPort raster_port;

#define GOOFYCAST

P3(PUBLIC pascal trap, void, PrComment, INTEGER, kind, INTEGER, size, Handle, hand)
{
    /* These pages already contain QuickDraw output. In particular, do not let
     StdComment's textbegin disable text in anticipation of a PS renderer. */
    if (raster_port)
        return;
    SignedByte state;
    INTEGER *ip, flippage, angle;
    Fixed *fp, yoffset, xoffset;

    if (pageno >= pagewanted && pageno <= lastpagewanted) {
        switch (kind) {
        case textbegin:
            do_textbegin((TTxtPicHdl)hand);
            break;
        case textend:
            do_textend();
            break;
        case textcenter:
            do_textcenter((TCenterRecHdl)hand);
            break;
        case rotatebegin:
            ip = (INTEGER*)STARH(hand);
            flippage = CW(ip[0]);
            angle = CW(ip[1]);
            ROMlib_rotatebegin(flippage, angle);
            break;
        case rotatecenter:
            fp = (Fixed*)STARH(hand);
            yoffset = CL(fp[0]);
            xoffset = CL(fp[1]);
            ROMlib_rotatecenter(Cx(thePort->pnLoc.v) + (double)yoffset / (1L << 16),
                                Cx(thePort->pnLoc.h) + (double)xoffset / (1L << 16));
            break;
        case rotateend:
            ROMlib_rotateend();
            break;
        case psbeginnosave:
            need_restore = FALSE;
            /* FALL THROUGH */
        case postscriptbegin:
            if (ROMlib_passpostscript) {
                if (kind == postscriptbegin) {
                    ROMlib_gsave();
                    need_restore = TRUE;
                }
                FIELD_ASSIGN_RAW(thePort->grafProcs, RM(&sendpsprocs));
            }
            break;
        case postscriptend:
            if (ROMlib_passpostscript) {
                if (need_restore) {
                    ROMlib_grestore();
                    need_restore = FALSE;
                }
                FIELD_ASSIGN_RAW(thePort->grafProcs, RM(&prprocs));
            }
            break;
        case postscripttextis:
            if (ROMlib_passpostscript)
                PP_RAW((PORT_GRAF_PROCS(thePort))->textProc) = RM(GOOFYCAST P_textasPS);
            break;
        case postscripthandle:
            if (pageno >= pagewanted && pageno <= lastpagewanted && ROMlib_passpostscript) {
                state = HGetState(hand);
                HLock(hand);
                NeXTsendps(size, (comPtr)STARH(hand));
                HSetState(hand, state);
            }
            break;
        case postscriptfile:
        default:
            warning_unimplemented("kind = %d", kind);
            break;
        }
    }
}

PRIVATE boolean_t printport_open_p = FALSE;

PRIVATE void ourinit(TPPrPort port, BOOLEAN preserve_font)
{
    INTEGER saved_font;

    saved_font = port->gPort.txFont;

    update_printing_globals();

    if (!printport_open_p) {
        OpenPort((GrafPtr)&printport);
        printport_open_p = TRUE;
    }
    else
        InitPort((GrafPtr)&printport);
    printport.txFont = CWC(-32768); /* force reload */
    printport.pnLoc.h = CWC(-32768);
    printport.pnLoc.v = CWC(-32768);
    OpenPort(&port->gPort);
    FIELD_ASSIGN_RAW(sendpsprocs.textProc, RM(P_donotPrText));
    FIELD_ASSIGN_RAW(sendpsprocs.lineProc, RM(P_donotPrLine));
    FIELD_ASSIGN_RAW(sendpsprocs.rectProc, RM(P_donotPrRect));
    FIELD_ASSIGN_RAW(sendpsprocs.rRectProc, RM(P_donotPrRRect));
    FIELD_ASSIGN_RAW(sendpsprocs.ovalProc, RM(P_donotPrOval));
    FIELD_ASSIGN_RAW(sendpsprocs.arcProc, RM(P_donotPrArc));
    FIELD_ASSIGN_RAW(sendpsprocs.polyProc, RM(P_donotPrPoly));
    PP_RAW(sendpsprocs.rgnProc) = RM(P_donotPrRgn);
    PP_RAW(sendpsprocs.bitsProc) = RM(P_donotPrBits);
    PP_RAW(sendpsprocs.commentProc) = RM(P_PrComment);
    PP_RAW(sendpsprocs.txMeasProc) = RM(P_PrTxMeas);
    PP_RAW(sendpsprocs.getPicProc) = RM(P_StdGetPic);
    PP_RAW(sendpsprocs.putPicProc) = RM(P_StdPutPic);
    PP_RAW(prprocs.textProc) = RM(P_PrText);
    PP_RAW(prprocs.lineProc) = RM(P_PrLine);
    PP_RAW(prprocs.rectProc) = RM(P_PrRect);
    PP_RAW(prprocs.rRectProc) = RM(P_PrRRect);
    PP_RAW(prprocs.ovalProc) = RM(P_PrOval);
    PP_RAW(prprocs.arcProc) = RM(P_PrArc);
    PP_RAW(prprocs.polyProc) = RM(P_PrPoly);
    PP_RAW(prprocs.rgnProc) = RM(P_PrRgn);
    PP_RAW(prprocs.bitsProc) = RM(P_PrBits);
    PP_RAW(prprocs.commentProc) = RM(P_PrComment);
    PP_RAW(prprocs.txMeasProc) = RM(P_PrTxMeas);
    PP_RAW(prprocs.getPicProc) = RM(P_StdGetPic);
    PP_RAW(prprocs.putPicProc) = RM(P_StdPutPic);
    port->saveprocs = prprocs;
    port->gPort.device = CWC(1);
    PP_RAW(port->gPort.grafProcs) = RM(&port->saveprocs);

    SetRect(&port->gPort.portBits.bounds, -1 * ROMlib_resolution_x / 2, -1 * ROMlib_resolution_y / 2,
            ROMlib_paper_x - (ROMlib_resolution_x / 2), ROMlib_paper_y - (ROMlib_resolution_y / 2));
    SetRect(&port->gPort.portRect, 0, 0, ROMlib_paper_x - ROMlib_print_margins[0] - ROMlib_print_margins[2],
            ROMlib_paper_y - ROMlib_print_margins[1] - ROMlib_print_margins[3]);
    HxX(PPR(port->gPort.visRgn), rgnBBox) = port->gPort.portRect;

    if (preserve_font)
        port->gPort.txFont = saved_font;

    NeXTOpenPage();
}

#include <stdio.h>

PRIVATE boolean_t need_pclose;

#if defined(LINUX)
PRIVATE void (*old_pipe_signal)(int);
#endif

PRIVATE FILE* open_ps_file(boolean_t* need_pclosep)
{
    *need_pclosep = FALSE;
    if (ROMlib_spool_file) {
        unlink(ROMlib_spool_file);
        free(ROMlib_spool_file);
        ROMlib_spool_file = NULL;
    }
    return host_print_spool(&ROMlib_spool_file);
}

PRIVATE boolean_t already_open = FALSE;

#if defined(QUESTIONABLE_FIX_FOR_LOGBOOK_THAT_BREAKS_PRINTING_UNDER_TESTGEN)
PRIVATE Byte save_FractEnable;
#endif

PRIVATE THPrint last_thprint;
PRIVATE uint32 job_dialog_count;
PRIVATE uint32 job_dialog_desired = 1;

PRIVATE boolean_t call_job_dialog_if_needed(THPrint thprint)
{
    if (thprint != last_thprint || job_dialog_count < job_dialog_desired) {
        if (!PrJobDialog(thprint))
            return FALSE;
        last_thprint = thprint;
        job_dialog_count = job_dialog_desired;
    }
    ++job_dialog_desired;
    return TRUE;
}

PUBLIC void ROMlib_acknowledge_job_dialog(THPrint thprint)
{
    last_thprint = thprint;
    ++job_dialog_count;
}

/* Draw into an ordinary QuickDraw bitmap, then submit it to the native
   printer. No PostScript interpreter or printer-specific guest driver. */
static Ptr raster_pixels;
static Size raster_size;
static int raster_width, raster_height, raster_stride;
static Rect raster_paper, raster_page;
static GrafPtr raster_saved_port;

static void raster_port_init(TPPrPort port, int preserve_font)
{
    INTEGER font = preserve_font ? port->gPort.txFont : 0;
    InitPort(&port->gPort);
    PP_RAW(port->gPort.portBits.baseAddr) = RM(raster_pixels);
    port->gPort.portBits.rowBytes = CW(raster_stride);
    port->gPort.portBits.bounds = raster_paper;
    port->gPort.portRect = raster_page;
    SetStdProcs(&port->saveprocs);
    PP_RAW(port->saveprocs.commentProc) = RM(P_PrComment);
    PP_RAW(port->gPort.grafProcs) = RM(&port->saveprocs);
    RectRgn(PPR(port->gPort.visRgn), &raster_page);
    RectRgn(PPR(port->gPort.clipRgn), &raster_page);
    if (preserve_font)
        port->gPort.txFont = font;
}

P3(PUBLIC pascal trap, TPPrPort, PrOpenDoc, THPrint, hPrint, TPPrPort, port, Ptr, pIOBuf)
{
    if (raster_port || !hPrint || !STARH(hPrint) || GetHandleSize((Handle)hPrint) < sizeof(TPrint)) {
        PrintErr = CWC(paramErr);
        return NULL;
    }
    if (!call_job_dialog_if_needed(hPrint))
        return NULL;
    raster_paper = HxX(hPrint, rPaper);
    raster_page = HxX(hPrint, prInfo.rPage);
    raster_width = CW(raster_paper.right) - CW(raster_paper.left);
    raster_height = CW(raster_paper.bottom) - CW(raster_paper.top);
    if (raster_width <= 0 || raster_width > 32767 || raster_height <= 0 || raster_height > 32767) {
        PrintErr = CWC(paramErr);
        return NULL;
    }
    raster_stride = (raster_width + 31) / 32 * 4;
    raster_size = (Size)raster_stride * raster_height;
    raster_pixels = NewPtrSysClear(raster_size);
    if (!raster_pixels) {
        PrintErr = CWC(memFullErr);
        return NULL;
    }
    BOOLEAN owned = !port;
    if (!port)
        port = (TPPrPort)NewPtrClear(sizeof *port);
    if (!port) {
        DisposPtr(raster_pixels);
        raster_pixels = NULL;
        PrintErr = CWC(memFullErr);
        return NULL;
    }
    RgnHandle visible = NewRgn(), clip = NewRgn();
    int error = (!visible || !clip) ? memFullErr : noErr;
    if (!error && host_raster_begin(ROMlib_paper_x, ROMlib_paper_y) < 0)
        error = -1;
    if (error) {
        if (visible)
            DisposeRgn(visible);
        if (clip)
            DisposeRgn(clip);
        DisposPtr(raster_pixels);
        raster_pixels = NULL;
        if (owned)
            DisposPtr((Ptr)port);
        PrintErr = CW(error);
        return NULL;
    }
    raster_saved_port = thePort;
    PP_RAW(port->gPort.visRgn) = RM(visible);
    PP_RAW(port->gPort.clipRgn) = RM(clip);
    port->fOurPtr = owned;
    raster_port_init(port, FALSE);
    raster_port = port;
    pagewanted = Hx(hPrint, prJob.iFstPage);
    lastpagewanted = Hx(hPrint, prJob.iLstPage);
    pageno = 0;
    page_is_open = FALSE;
    PrintErr = CWC(noErr);
    return port;
}

P2(PUBLIC pascal trap, void, PrOpenPage, TPPrPort, port, TPRect, pPageFrame)
{
    if (!port || port != raster_port || page_is_open) {
        PrintErr = CWC(paramErr);
        return;
    }
    ++pageno;
    raster_port_init(port, TRUE);
    memset(raster_pixels, 0, raster_size);
    page_is_open = TRUE;
}

P1(PUBLIC pascal trap, void, PrClosePage, TPPrPort, port)
{
    if (port != raster_port || !page_is_open)
        return;
    if (!PrintErr && pageno >= pagewanted && pageno <= lastpagewanted &&
        host_raster_page((unsigned char*)raster_pixels, raster_width, raster_height, raster_stride) < 0)
        PrintErr = CWC(-1);
    page_is_open = FALSE;
}

P1(PUBLIC pascal trap, void, PrCloseDoc, TPPrPort, port)
{
    if (!port || port != raster_port) {
        PrintErr = CWC(paramErr);
        return;
    }
    if (page_is_open)
        C_PrClosePage(port);
    if (host_raster_end(!!PrintErr) < 0)
        PrintErr = CWC(-1);
    ClosePort(&port->gPort);
    SetPort(raster_saved_port);
    if (port->fOurPtr)
        DisposPtr((Ptr)port);
    DisposPtr(raster_pixels);
    raster_pixels = NULL;
    raster_port = NULL;
    page_is_open = FALSE;
}

P5(PUBLIC pascal trap, void, PrPicFile, THPrint, hPrint, TPPrPort, pPrPort, Ptr, pIOBuf, Ptr, pDevBuf, TPrStatus*,
   prStatus) /* TODO */
{
    warning_unimplemented(NULL_STRING);
}

PUBLIC void print_reinit(void)
{
    host_raster_end(1);
    raster_pixels = NULL;
    raster_port = NULL;
    printport_open_p = FALSE;
    last_thprint = NULL;
    job_dialog_count = 0;
    job_dialog_desired = 1;
    if (ROMlib_printfile)
        fclose(ROMlib_printfile);
    ROMlib_printfile = NULL;
    if (ROMlib_spool_file)
        unlink(ROMlib_spool_file);
    free(ROMlib_spool_file);
    ROMlib_spool_file = NULL;
    already_open = page_is_open = FALSE;
    host_print_reset();
}
