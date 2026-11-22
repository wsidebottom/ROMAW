/*+===================================================================
  File:      ctlSet.c

  Summary:   Implements ctl set support for Macintosh Control Manager operations.

  Classes:   None.

  Functions: No externally linked function definitions.

  Origin:    Existing repository source in executor/src/controls; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986-1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in ControlMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"

#include "WindowMgr.h"
#include "ControlMgr.h"

#include "rsys/ctl.h"

P2(PUBLIC pascal trap, void, SetCtlValue, ControlHandle, c, /* IMI-326 */
   INTEGER, v)
{
    if (!c || !STARH(c)) {
        warning_unexpected("SetCtlValue called with null control");
        EM_D0 = 0;
        return;
    }
    CTL_CALL_EXCURSION(c, {
        SetPort(HxP(c, contrlOwner));
        if ((ROMlib_native_control_kind(c) & ~15) == ROMlib_bevel_button_proc)
            CTL_VALUE_X(c) = CW(v != 0);
        else if (v < Hx(c, contrlMin))
            HxX(c, contrlValue) = HxX(c, contrlMin);
        else if (v > Hx(c, contrlMax))
            HxX(c, contrlValue) = HxX(c, contrlMax);
        else
            HxX(c, contrlValue) = CW(v);
        CTLCALL(c, drawCntl, ALLINDICATORS);
    });

    EM_D0 = 0;
}

P1(PUBLIC pascal trap, INTEGER, GetCtlValue, /* IMI-326 */
   ControlHandle, c)
{
    if (!c || !STARH(c))
        return 0;
    return Hx(c, contrlValue);
}

P2(PUBLIC pascal trap, void, SetCtlMin, ControlHandle, c, /* IMI-326 */
   INTEGER, v)
{
    if (!c || !STARH(c)) {
        warning_unexpected("SetCtlMin called with null control");
        return;
    }
    CTL_CALL_EXCURSION(c, {
        SetPort(HxP(c, contrlOwner));
        HxX(c, contrlMin) = CW(v);
        if (Hx(c, contrlValue) < v)
            HxX(c, contrlValue) = CW(v);
        CTLCALL(c, drawCntl, ALLINDICATORS);
    });
}

P1(PUBLIC pascal trap, INTEGER, GetCtlMin, ControlHandle, c) /* IMI-327 */
{
    if (!c || !STARH(c))
        return 0;
    return Hx(c, contrlMin);
}

P2(PUBLIC pascal trap, void, SetCtlMax, ControlHandle, c, /* IMI-327 */
   INTEGER, v)
{
    if (!c || !STARH(c)) {
        warning_unexpected("SetCtlMax called with null control");
        return;
    }
    CTL_CALL_EXCURSION(c, {
        /* #### TEST ON MAC MacBreadboard's behaviour suggests that
	   this code is needed. */
        if (v < Hx(c, contrlMin))
            v = Hx(c, contrlMin);

        HxX(c, contrlMax) = CW(v);
        if (Hx(c, contrlValue) > v)
            HxX(c, contrlValue) = CW(v);
        CTLCALL(c, drawCntl, ALLINDICATORS);
    });
}

P1(PUBLIC pascal trap, INTEGER, GetCtlMax, ControlHandle, c) /* IMI-327 */
{
    if (!c || !STARH(c))
        return 0;
    return Hx(c, contrlMax);
}
