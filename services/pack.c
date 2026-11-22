/*+===================================================================
  File:      pack.c

  Summary:   Implements pack support for Macintosh Toolbox services.

  Classes:   None.

  Functions: No externally linked function definitions.

  Origin:    Existing repository source in executor/src/services; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986, 1988, 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in Package.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "Package.h"

P1(PUBLIC pascal trap, void, InitPack, INTEGER, packid)
{ /* NOP */
}

P0(PUBLIC pascal trap, void, InitAllPacks)
{ /* NOP */
}
