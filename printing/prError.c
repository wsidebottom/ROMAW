/*+===================================================================
  File:      prError.c

  Summary:   Implements pr error support for printing and print settings.

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

P0(PUBLIC pascal trap, INTEGER, PrError)
{
    return CW(PrintErr);
}

P1(PUBLIC pascal trap, void, PrSetError, INTEGER, iErr)
{
    PrintErr = CW(iErr);
}
