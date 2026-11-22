/*+===================================================================
  File:      dialDispatch.c

  Summary:   Implements dial dispatch support for Macintosh Dialog Manager operations.

  Classes:   None.

  Functions: C_GetStdFilterProc, C_SetDialogDefaultItem, C_SetDialogCancelItem, C_SetDialogTracksCursor

  Origin:    Existing repository source in executor/src/dialogs; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"
#include "EventMgr.h"
#include "rsys/pstuff.h"

#include "DialogMgr.h"
#include "rsys/wind.h"
#include "FileMgr.h"

/* traps from the DialogDispatch trap */

P1(PUBLIC pascal trap, OSErr, GetStdFilterProc, HIDDEN_ProcPtr*, proc)
{
    if (!proc)
        return paramErr;
    /* The output belongs to guest memory, including guest stack locals.
       A native ProcPtr is eight bytes on Windows x64. */
    HIDDEN_RAW_SET(*proc, RM(P_ROMlib_myfilt));
    return noErr;
}

P2(PUBLIC pascal trap, OSErr, SetDialogDefaultItem, DialogPtr, dialog, int16, new_item)
{
    DialogPeek dp;

    dp = (DialogPeek)dialog;
    if (!dp) {
        warning_unexpected("SetDialogDefaultItem called with null dialog");
        return noErr;
    }

    dp->aDefItem = CW(new_item);
    warning_unimplemented("no specs");
    return noErr;
}

static ROMlib_window_metadata* dialog_options(DialogPtr dialog)
{
    ROMlib_window_metadata* metadata = dialog ? ROMlib_window_get_metadata(dialog) : NULL;
    return metadata && (WINDOW_KIND(dialog) == dialogKind || WINDOW_KIND(dialog) < 0) ? metadata : NULL;
}

P2(PUBLIC pascal trap, OSErr, SetDialogCancelItem, DialogPtr, dialog, int16, new_item)
{
    ROMlib_window_metadata* metadata = dialog_options(dialog);
    if (!metadata || new_item < 0 || new_item > CountDITL(dialog))
        return paramErr;
    metadata->dialog_options = (metadata->dialog_options & ROMLIB_DIALOG_TRACKS_CURSOR) | new_item;
    return noErr;
}

P2(PUBLIC pascal trap, OSErr, SetDialogTracksCursor, DialogPtr, dialog, Boolean, tracks)
{
    ROMlib_window_metadata* metadata = dialog_options(dialog);
    if (!metadata)
        return paramErr;
    metadata->dialog_options =
        (metadata->dialog_options & ROMLIB_DIALOG_CANCEL_ITEM) | (tracks ? ROMLIB_DIALOG_TRACKS_CURSOR : 0);
    return noErr;
}
