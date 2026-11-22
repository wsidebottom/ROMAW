/*+===================================================================
  File:      resMisc.c

  Summary:   Implements res misc support for classic Macintosh resources.

  Classes:   None.

  Functions: ROMlib_setreserr

  Origin:    Existing repository source in executor/src/resources; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986, 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in ResourceMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "ResourceMgr.h"
#include "rsys/resource.h"
#include "rsys/hook.h"

A1(PUBLIC, INTEGER, ROMlib_setreserr, INTEGER, reserr) /* INTERNAL */
{
    ResErr = CW(reserr);
    if (ResErr != noErr && ResErrProc) {
        ROMlib_hook(res_reserrprocnumber);

        EM_D0 = (unsigned short)reserr; /* TODO: is unsigned short
							 correct? */
        CALL_EMULATOR((syn68k_addr_t)CL((long)ResErrProc));
    }
    return CW(ResErr);
}

P0(PUBLIC pascal trap, INTEGER, ResError)
{
    return CW(ResErr);
}

/* System 7 retained these obsolete traps for MacWrite 2.2. They consume their
   parameters and report failure without changing any resources or invoking
   ResErrProc. Its shared clear-and-return path leaves Z set and preserves X. */
static void obsolete_reference_error(INTEGER error)
{
    ResErr = CW(error);
    cpu_state.ccnz = 0;
    cpu_state.ccn = 0;
    cpu_state.ccv = 0;
    cpu_state.ccc = 0;
}

P3(PUBLIC pascal trap, void, AddReference, Handle, resource, INTEGER, id, StringPtr, name)
{
    obsolete_reference_error(addRefFailed);
}

P1(PUBLIC pascal trap, void, RmveReference, Handle, resource)
{
    obsolete_reference_error(rmvRefFailed);
}

P1(PUBLIC pascal trap, INTEGER, GetResFileAttrs, INTEGER, rn)
{
    resmaphand map;

    ROMlib_setreserr(noErr);
    map = ROMlib_resource_file_map(rn);
    if (!map) {
        ROMlib_setreserr(resFNotFound);
        return (0);
    }
    else
        return (Hx(map, resfatr));
}

P2(PUBLIC pascal trap, void, SetResFileAttrs, INTEGER, rn, INTEGER, attrs)
{
    resmaphand map;

    ROMlib_setreserr(noErr);
    map = ROMlib_resource_file_map(rn);
    if (!map)
        return; /* don't set reserr.  I kid you not, see I-127 */
    else
        HxX(map, resfatr) = CW(attrs);
}
