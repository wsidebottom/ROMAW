/*+===================================================================
  File:      prRecords.c

  Summary:   Implements pr records support for printing and print settings.

  Classes:   None.

  Functions: ROMlib_native_print_dialog, ROMlib_set_default_resolution

  Origin:    Existing repository source in executor/src/printing; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in PrintMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "QuickDraw.h"
#include "PrintMgr.h"
#include "ResourceMgr.h"
#include "MemoryMgr.h"
#include "FileMgr.h"

#include "rsys/nextprint.h"
#include "rsys/pstuff.h"
#include "rsys/print.h"
#include "rsys/hostdialog.h"
#if defined(SDL)
#include "sdlevents.h"
#endif

static boolean_t valid_print_record(THPrint print)
{
    if (!print || !STARH(print)) {
        PrintErr = CWC(nilHandleErr);
        return FALSE;
    }
    if (GetHandleSize((Handle)print) < (Size)sizeof(TPrint)) {
        PrintErr = CWC(paramErr);
        return FALSE;
    }
    return TRUE;
}

PRIVATE void set_wDev(THPrint hPrint)
{
    HxX(hPrint, prStl.wDev) = CWC(0x307);
}

PUBLIC void ROMlib_set_default_resolution(THPrint hPrint, INTEGER vres, INTEGER hres)
{
    update_printing_globals();

    HxX(hPrint, prInfo.iVRes) = CW(vres);
    HxX(hPrint, prInfo.iHRes) = CW(hres);
    HxX(hPrint, prInfo.rPage.top) = CWC(0);
    HxX(hPrint, prInfo.rPage.left) = CWC(0);
    HxX(hPrint, prInfo.rPage.bottom) =
        CW((ROMlib_paper_y - ROMlib_print_margins[1] - ROMlib_print_margins[3]) * vres / 72);
    HxX(hPrint, prInfo.rPage.right) =
        CW((ROMlib_paper_x - ROMlib_print_margins[0] - ROMlib_print_margins[2]) * hres / 72);

    HxX(hPrint, rPaper.top) = CW((INTEGER)(-ROMlib_print_margins[1] * vres / 72));
    HxX(hPrint, rPaper.bottom) = CW((INTEGER)((ROMlib_paper_y - ROMlib_print_margins[1]) * vres / 72));
    HxX(hPrint, rPaper.left) = CW((INTEGER)(-ROMlib_print_margins[0] * hres / 72));
    HxX(hPrint, rPaper.right) = CW((INTEGER)((ROMlib_paper_x - ROMlib_print_margins[0]) * hres / 72));

    ROMlib_resolution_x = hres;
    ROMlib_resolution_y = vres;
}

P1(PUBLIC pascal trap, void, PrintDefault, THPrint, hPrint)
{
    if (!valid_print_record(hPrint))
        return;

    /* TODO:  fill this information in from the currently open
       printer resource file.  I don't know where it's kept so
       I've filled in the values by hand to be what I suspect the
       LaserWriter we're using wants */

    memset((char*)STARH(hPrint), 0, sizeof(TPrint));
    HxX(hPrint, iPrVersion) = CW(ROMlib_PrDrvrVers);

    ROMlib_set_default_resolution(hPrint, 72, 72);

    HxX(hPrint, prInfo.iDev) = 0;

    set_wDev(hPrint);
    HxX(hPrint, prStl.iPageV) = CWC(1320); /* These were switched a while back */
    HxX(hPrint, prStl.iPageH) = CWC(1020); /* but I think it was a mistake */
    HxX(hPrint, prStl.bPort) = 0;
    HxX(hPrint, prStl.feed) = 2;

    HxX(hPrint, prInfoPT.iDev) = 0;
    HxX(hPrint, prInfoPT.iVRes) = CWC(72);
    HxX(hPrint, prInfoPT.iHRes) = CWC(72);
    HxX(hPrint, prInfoPT.rPage) = HxX(hPrint, prInfo.rPage);

    HxX(hPrint, prXInfo.iRowBytes) = CW((Hx(hPrint, prXInfo.iBandH) + 7) / 8);
    /* TODO: what about the rest of prXInfo? (is zero for now) */

    HxX(hPrint, prJob.iFstPage) = CWC(1);
    HxX(hPrint, prJob.iLstPage) = CWC(9999);
    HxX(hPrint, prJob.iCopies) = CWC(1);
    HxX(hPrint, prJob.bJDocLoop) = 2; /* used to be 1, but then File Maker
					  Pro 2.1 would call PrOpenDoc
					  and PrCloseDoc once for each page */
    HxX(hPrint, prJob.fFromUsr) = 1;
}

P1(PUBLIC pascal trap, BOOLEAN, PrValidate, THPrint, hPrint) /* IMII-158 */
{
    if (!valid_print_record(hPrint))
        return FALSE;

    /* TODO: figure out what are problem areas for us and adjust
	     accordingly */

    set_wDev(hPrint);

    if (!HxX(hPrint, prInfo.iVRes) || !HxX(hPrint, prInfo.iHRes))
        PrintDefault(hPrint);

    {
        int first, last;

        first = Hx(hPrint, prJob.iFstPage);
        last = Hx(hPrint, prJob.iLstPage);

        if (first < 1 || first > last) {
            HxX(hPrint, prJob.iFstPage) = CWC(1);
            HxX(hPrint, prJob.iLstPage) = CWC(9999);
        }
    }
    {
        int copies;

        copies = Hx(hPrint, prJob.iCopies);

        if (copies < 1 || copies > 99)
            HxX(hPrint, prJob.iCopies) = CWC(1);
    }

    HxX(hPrint, prJob.bJDocLoop) = 2;
    return FALSE;
}

BOOLEAN
ROMlib_native_print_dialog(THPrint print, boolean_t page_setup)
{
    host_print_settings settings = {0};
    int result;
    if (!valid_print_record(print))
        return FALSE;
    update_printing_globals();
    settings.width = ROMlib_paper_x;
    settings.height = ROMlib_paper_y;
    settings.first = Hx(print, prJob.iFstPage);
    settings.last = Hx(print, prJob.iLstPage);
    settings.copies = Hx(print, prJob.iCopies);
    settings.left = ROMlib_print_margins[0];
    settings.top = ROMlib_print_margins[1];
    settings.right = ROMlib_print_margins[2];
    settings.bottom = ROMlib_print_margins[3];
    result = host_print_dialog(page_setup, &settings);
#if defined(SDL)
    sdl_preferences_input_reset();
#endif
    if (result <= 0) {
        PrintErr = CW(result < 0 ? -1 : 0);
        if (result < 0)
            host_dialog_error("The host print dialog could not be opened.");
        return FALSE;
    }
    if (settings.width < 73 || settings.height < 73 || settings.width > 32000 || settings.height > 32000 ||
        settings.first < 1 || settings.last < settings.first || settings.last > 32767 || settings.copies < 1 ||
        settings.copies > 32767 || settings.left < 0 || settings.top < 0 || settings.right < 0 || settings.bottom < 0 ||
        (long long)settings.left + settings.right >= settings.width ||
        (long long)settings.top + settings.bottom >= settings.height) {
        PrintErr = CWC(paramErr);
        return FALSE;
    }
    ROMlib_print_margins[0] = settings.left;
    ROMlib_print_margins[1] = settings.top;
    ROMlib_print_margins[2] = settings.right;
    ROMlib_print_margins[3] = settings.bottom;
    ROMlib_native_paper_width = settings.width;
    ROMlib_native_paper_height = settings.height;
    ROMlib_paper_orientation = settings.width > settings.height ? "Landscape" : "Portrait";
    ROMlib_set_default_resolution(print, 72, 72);
    HxX(print, prInfoPT) = HxX(print, prInfo);
    if (!page_setup) {
        HxX(print, prJob.iFstPage) = CW(settings.first);
        HxX(print, prJob.iLstPage) = CW(settings.last);
        /* The native spooler applies copies; the generated PS contains one. */
        HxX(print, prJob.iCopies) = CWC(1);
        ROMlib_acknowledge_job_dialog(print);
    }
    C_PrValidate(print);
    PrintErr = CWC(noErr);
    return TRUE;
}

P1(PUBLIC pascal trap, BOOLEAN, PrStlDialog, THPrint, hPrint)
{
    BOOLEAN retval;

    retval = C_PrDlgMain(hPrint, (ProcPtr)P_PrStlInit);
    return retval;
}

P1(PUBLIC pascal trap, BOOLEAN, PrJobDialog, THPrint, hPrint)
{
    return C_PrDlgMain(hPrint, (ProcPtr)P_PrJobInit);
}

P2(PUBLIC pascal trap, void, PrJobMerge, THPrint, hPrintSrc, THPrint, hPrintDst)
{
    if (!valid_print_record(hPrintSrc) || !valid_print_record(hPrintDst))
        return;

    /* Imaging With QuickDraw, Printing Manager: validate both records and
       transfer job settings while retaining each document's page setup. */
    C_PrValidate(hPrintSrc);
    C_PrValidate(hPrintDst);
    HxX(hPrintDst, prJob) = HxX(hPrintSrc, prJob);
    PrintErr = CWC(noErr);
}
