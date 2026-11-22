/*+===================================================================
  File:      prLowLevel.c

  Summary:   Implements pr low level support for printing and print settings.

  Classes:   None.

  Functions: update_printing_globals

  Origin:    Existing repository source in executor/src/printing; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/*
 * Copyright 1989 - 1997 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in PrintMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "QuickDraw.h"
#include "PrintMgr.h"
#include "MemoryMgr.h"
#include "OSUtil.h"
#include "BinaryDecimal.h"
#include "ControlMgr.h"
#include "ResourceMgr.h"
#include "rsys/nextprint.h"
#include "rsys/pstuff.h"
#include "rsys/print.h"
#include "rsys/hook.h"
#include "rsys/ctl.h"
#include "rsys/notmac.h"
#include <ctype.h>
#include "MenuMgr.h"
#include "rsys/vdriver.h"
#include "rsys/system_error.h"
#include "rsys/options.h"
#include "rsys/string.h"
#include "rsys/osevent.h"

#if defined(CYGWIN32)
#include "win_print.h"
#endif

PUBLIC uint32 ROMlib_PrDrvrVers = 70;

P0(PUBLIC pascal trap, void, PrDrvrOpen) /* TODO */
{
    warning_unimplemented(NULL_STRING);
}

P0(PUBLIC pascal trap, void, PrDrvrClose) /* TODO */
{
    warning_unimplemented(NULL_STRING);
}

P4(PUBLIC pascal trap, void, PrCtlCall, INTEGER, iWhichCtl, LONGINT, lParam1, LONGINT, lParam2, LONGINT,
   lParam3) /* TODO */
{
    warning_unimplemented(NULL_STRING);
}

P0(PUBLIC pascal trap, Handle, PrDrvrDCE) /* TODO */
{
    return (Handle)0;
}

P0(PUBLIC pascal trap, INTEGER, PrDrvrVers)
{
    INTEGER retval;

    retval = ROMlib_PrDrvrVers;
    return retval;
}

PUBLIC int ROMlib_print_margins[4] = {36, 36, 36, 36};
PUBLIC int ROMlib_native_paper_width, ROMlib_native_paper_height;
PUBLIC const char* ROMlib_paper_orientation = "Portrait";

PUBLIC char* ROMlib_spool_file;

PUBLIC void update_printing_globals(void)
{
    if (ROMlib_native_paper_width > 0) {
        ROMlib_paper_x = ROMlib_native_paper_width;
        ROMlib_paper_y = ROMlib_native_paper_height;
    }
    else {
        /* Safe defaults before the host page-setup dialog supplies dimensions. */
        ROMlib_paper_x = 612;
        ROMlib_paper_y = 792;
    }

    if (!ROMlib_native_paper_width && (!ROMlib_paper_orientation || strcmp(ROMlib_paper_orientation, "Portrait") ==
                                                                        0) == (ROMlib_paper_x > ROMlib_paper_y)) {
        int temporary = ROMlib_paper_x;
        ROMlib_paper_x = ROMlib_paper_y;
        ROMlib_paper_y = temporary;
    }

    ROMlib_document_paper_sizes = "%%DocumentPaperSizes: ";
    ROMlib_paper_size = "%%PaperSize: ";
    ROMlib_paper_size_name = "Letter";
    ROMlib_paper_size_name_terminator = "\n";

    if (ROMlib_native_paper_width) {
        ROMlib_rotation = 0;
        ROMlib_translate_x = ROMlib_print_margins[0];
        ROMlib_translate_y = ROMlib_paper_y - ROMlib_print_margins[1];
    }
    else if (ROMlib_paper_x < ROMlib_paper_y) {
        ROMlib_rotation = 0;
        ROMlib_translate_x = 0;
        ROMlib_translate_y = ROMlib_paper_y;
    }
    else {
        ROMlib_rotation = -90;
        ROMlib_translate_x = -ROMlib_paper_x;
        ROMlib_translate_y = ROMlib_paper_y;
    }
}

/* Kept as Toolbox callback ABI entry points. Native panels have no guest
   DialogRecord, DITL, item procedure or modal event filter. */
P2(PUBLIC pascal, void, ROMlib_myjobproc, DialogPtr, dp, INTEGER, itemno)
{
}

P2(PUBLIC pascal, void, ROMlib_mystlproc, DialogPtr, dp, INTEGER, itemno)
{
}

P3(PUBLIC, pascal BOOLEAN, ROMlib_stlfilterproc, DialogPeek, dp, EventRecord*, evt, INTEGER*, item)
{
    return FALSE;
}

P3(PUBLIC, pascal BOOLEAN, ROMlib_numsonlyfilterproc, DialogPeek, dp, EventRecord*, evt, INTEGER*, item)
{
    return FALSE;
}

P2(PUBLIC, pascal void, ROMlib_circle_ok, DialogPeek, dp, INTEGER, item)
{
}

P2(PUBLIC, pascal void, ROMlib_orientation, DialogPeek, dp, INTEGER, item)
{
}

P1(PUBLIC pascal trap, TPPrDlg, PrJobInit, THPrint, hPrint)
{
    return NULL;
}

P1(PUBLIC pascal trap, TPPrDlg, PrStlInit, THPrint, hPrint)
{
    return NULL;
}

P2(PUBLIC pascal trap, BOOLEAN, PrDlgMain, THPrint, hPrint, ProcPtr, initfptr)
{
    return ROMlib_native_print_dialog(hPrint, initfptr == (ProcPtr)P_PrStlInit);
}

P1(PUBLIC pascal trap, void, PrGeneral, Ptr, pData) /* IMV-410 */
{
    TGnlData* tgp;

    tgp = (TGnlData*)pData;

    ((TGnlData*)pData)->iError = CWC(OpNotImpl);
    switch (CW(tgp->iOpCode)) {
    case GetRslData: {
        TGetRslBlk* resolp;

        resolp = (TGetRslBlk*)pData;
        resolp->iError = CWC(noErr);
        resolp->iRgType = CWC(1);
        resolp->xRslRg.iMin = CWC(0);
        resolp->xRslRg.iMax = CWC(0);
        resolp->yRslRg.iMin = CWC(0);
        resolp->yRslRg.iMax = CWC(0);
        resolp->rgRslRec[0].iXRsl = CWC(72);
        resolp->rgRslRec[0].iYRsl = CWC(72);
        if (ROMlib_optional_res_x <= 0 || ROMlib_optional_res_y <= 0)
            resolp->iRslRecCnt = CWC(1);
        else {
            resolp->iRslRecCnt = CWC(2);
            resolp->rgRslRec[1].iXRsl = CW(ROMlib_optional_res_x);
            resolp->rgRslRec[1].iYRsl = CW(ROMlib_optional_res_y);
        }
    } break;
    case SetRsl: {
        TSetRslBlk* resolp;

        resolp = (TSetRslBlk*)pData;
        if (!((resolp->iXRsl == CWC(72) && resolp->iYRsl == CWC(72)) ||
              (resolp->iXRsl == CW(ROMlib_optional_res_x) && resolp->iYRsl == CW(ROMlib_optional_res_y))))
            resolp->iError = CWC(NoSuchRsl);
        else {
            resolp->iError = CWC(noErr);
            ROMlib_set_default_resolution(PPR(resolp->hPrint), CW(resolp->iYRsl), CW(resolp->iXRsl));
        }
    } break;
    case DraftBits:
        warning_unimplemented("DraftBits");
        break;
    case NoDraftBits:
        warning_unimplemented("NoDraftBits");
        break;
    case GetRotn:
        warning_unimplemented("GetRotn");
        break;
    default:
        warning_unimplemented(NULL_STRING);
        break;
    }
}
