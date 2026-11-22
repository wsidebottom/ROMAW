/*+===================================================================
  File:      toolbox_link_stubs.c

  Summary:   Fail fast for Toolbox services outside the standalone Carbon fixture.

  Classes:   None.

  Functions: C_GetEnvirons, Gestalt, C_GetPixBaseAddr, ROMlib_kchr_ptr,
             C_KeyTrans, C_FontScript, Delay, C_SectRgn, C_InsetRgn.

  Origin:    Standalone Carbon regression fixture for Executor Toolbox dependencies.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/

#include "rsys/common.h"
#include "rsys/everything.h"
#include "rsys/osevent.h"
#include "Gestalt.h"
#include <stdio.h>
#include <stdlib.h>

// These services have real implementations in ROMAW. The standalone fixture
// does not exercise them; an unexpected call must never silently succeed.
static _Noreturn void unexpected_toolbox_call(const char* name)
{
    fprintf(stderr, "Unexpected Toolbox service in Carbon fixture: %s\n", name);
    abort();
}

LONGINT C_GetEnvirons(INTEGER verb)
{
    unexpected_toolbox_call("GetEnvirons");
}

OSErrRET Gestalt(OSType selector, LONGINT* response)
{
    unexpected_toolbox_call("Gestalt");
}

Ptr C_GetPixBaseAddr(PixMapHandle pixmap)
{
    unexpected_toolbox_call("GetPixBaseAddr");
}

Ptr ROMlib_kchr_ptr(void)
{
    unexpected_toolbox_call("ROMlib_kchr_ptr");
}

LONGINT C_KeyTrans(Ptr map, unsigned short code, LONGINT* state)
{
    unexpected_toolbox_call("KeyTrans");
}

INTEGER C_FontScript(void)
{
    unexpected_toolbox_call("FontScript");
}

void Delay(LONGINT ticks, LONGINT* final_ticks)
{
    unexpected_toolbox_call("Delay");
}

void C_SectRgn(RgnHandle first, RgnHandle second, RgnHandle destination)
{
    unexpected_toolbox_call("SectRgn");
}

void C_InsetRgn(RgnHandle region, INTEGER horizontal, INTEGER vertical)
{
    unexpected_toolbox_call("InsetRgn");
}
