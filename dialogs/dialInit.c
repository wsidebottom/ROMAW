/*+===================================================================
  File:      dialInit.c

  Summary:   Implements dial init support for Macintosh Dialog Manager operations.

  Classes:   None.

  Functions: C_ROMlib_mysound, SetDAFont

  Origin:    Existing repository source in executor/src/dialogs; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986, 1988, 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in DialogMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"

#include "DialogMgr.h"
#include "FontMgr.h"
#include "OSUtil.h"
#include "MemoryMgr.h"

#include "rsys/itm.h"
#include "rsys/mman.h"

P1(PUBLIC, pascal void, ROMlib_mysound, INTEGER, i)
{
    while (i--)
        SysBeep(5);
}

P1(PUBLIC pascal trap, void, ErrorSound, ProcPtr, sp) /* IMI-411 */
{
    HIDDEN_RAW_SET(DABeeper_H, RM(sp));
}

P1(PUBLIC pascal trap, void, InitDialogs, ProcPtr, rp) /* IMI-411 */
{
    Ptr nothing = (Ptr) "";

    ZONE_SAVE_EXCURSION(SysZone, {
        DlgFont = CWC(systemFont);
        HIDDEN_RAW_SET(ResumeProc_H, RM(rp));
        ErrorSound((ProcPtr)P_ROMlib_mysound);
        for (unsigned i = 0; i < 4; ++i) {
            Handle string = NULL;
            PtrToHand(nothing, &string, 1);
            HIDDEN_RAW_SET(DAStrings_H[i], RM(string));
        }
    });
}

A1(PUBLIC, void, SetDAFont, INTEGER, i) /* IMI-412 */
{
    DlgFont = CW(i);
}
