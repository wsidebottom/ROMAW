/*+===================================================================
  File:      shutdown.c

  Summary:   shutdown.c; ShutDown Manager routines

  Classes:   None.

  Functions: No externally linked function definitions.

  Origin:    Existing repository source in executor/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986, 1988, 1989, 1990, 1995 by Abacus Research and
   Development, Inc.  All rights reserved.

   shutdown.c; ShutDown Manager routines */

#include "rsys/common.h"

#include "ShutDown.h"
#include "SegmentLdr.h"

#include "rsys/segment.h"

P0(PUBLIC pascal trap, void, ShutDwnPower)
{
    ROMlib_exit = TRUE;
    ExitToShell();
}

P0(PUBLIC pascal trap, void, ShutDwnStart)
{
    ExitToShell();
}

P2(PUBLIC pascal trap, void, ShutDwnInstall, ProcPtr, shutdown_proc, int16, flags)
{
    /* #warning "ShutDwnInstall unimplemented" */
    warning_unimplemented(NULL_STRING);
}

P1(PUBLIC pascal trap, void, ShutDwnRemove, ProcPtr, shutdown_proc)
{
    /* #warning "ShutDwnRemove unimplemented" */
    warning_unimplemented(NULL_STRING);
}
