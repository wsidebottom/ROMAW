/*+===================================================================
  File:      prInit.c

  Summary:   Implements pr init support for printing and print settings.

  Classes:   None.

  Functions: No externally linked function definitions.

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
#include "FileMgr.h"
#include "ToolboxUtil.h"
#include "MemoryMgr.h"
#include "AliasMgr.h"

PUBLIC INTEGER ROMlib_printresfile = -1;

P0(PUBLIC pascal trap, void, PrOpen)
{
    StringHandle h;
    INTEGER volume;
    LONGINT directory;
    SignedByte state;
    OSErr error = noErr;

    if (ROMlib_printresfile == -1) {
        h = GetString(-8192);
        if (!h || !STARH(h)) {
            PrintErr = CWC(resNotFound);
            return;
        }
        state = HGetState((Handle)h);
        HLock((Handle)h);
        if (GetHandleSize((Handle)h) < (unsigned char)STARH(h)[0] + 1)
            error = paramErr;
        else {
            error = FindFolder(kOnSystemDisk, kSystemFolderType, FALSE, &volume, &directory);
            if (error == noErr)
                ROMlib_printresfile = HOpenResFile(CW(volume), CL(directory), STARH(h), fsCurPerm);
            /* Retain older installations with Printer at the boot-volume root. */
            if (ROMlib_printresfile == -1)
                ROMlib_printresfile = OpenRFPerm(STARH(h), Cx(BootDrive), fsCurPerm);
            error = ROMlib_printresfile == -1 ? ResError() : noErr;
            if (error == noErr && ROMlib_printresfile == -1)
                error = fnfErr;
        }
        HSetState((Handle)h, state);
    }
    PrintErr = CW(error);
}

P0(PUBLIC pascal trap, void, PrClose)
{
    if (ROMlib_printresfile != -1) {
        CloseResFile(ROMlib_printresfile);
        ROMlib_printresfile = -1;
    }
    PrintErr = CWC(noErr);
}
