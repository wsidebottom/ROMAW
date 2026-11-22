/*+===================================================================
  File:      invalidation.c

  Summary:   Carbon's explicit-window invalidation over the native update regions.

  Classes:   None.

  Functions: ROMlib_carbon_inval_window_rect, ROMlib_carbon_inval_window_region, ROMlib_carbon_valid_window_rect,
             ROMlib_carbon_valid_window_region

  Origin:    Existing repository source in packages/carbonlib/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Carbon's explicit-window invalidation over the native update regions. */
#include "internal.h"
#include "rsys/wind.h"
#include "rsys/cquick.h"
#include "OSUtil.h"

static LONGINT change_update(WindowPtr window, const Rect* rectangle, RgnHandle region, BOOLEAN valid)
{
    if (!ROMlib_valid_window(window))
        return -5600; /* errInvalidWindowPtr */
    if (!rectangle && (!region || !STARH(region)))
        return paramErr;
    Rect bounds = PORT_BOUNDS(window);
    Rect snapshot;
    RgnHandle temporary;
    if (rectangle) {
        /* Allocation may move the handle containing the caller's rectangle. */
        snapshot = *rectangle;
        temporary = C_NewRgn();
        if (!temporary)
            return memFullErr;
        C_RectRgn(temporary, &snapshot);
    }
    else {
        /* HandToHand uses a native Handle*, and checks the copy allocation.
           Offset only the copy so caller-owned regions never change. */
        Handle copy = (Handle)region;
        OSErr error = HandToHand(&copy);
        if (error)
            return error;
        temporary = (RgnHandle)copy;
    }
    if (!ROMlib_valid_window(window)) {
        C_DisposeRgn(temporary);
        return -5600;
    }
    C_OffsetRgn(temporary, -CW(bounds.left), -CW(bounds.top));
    RgnHandle update = WINDOW_UPDATE_REGION(window);
    if (valid)
        C_DiffRgn(update, temporary, update);
    else
        C_UnionRgn(temporary, update, update);
    OSErr error = MemError();
    C_DisposeRgn(temporary);
    return error;
}

LONGINT ROMlib_carbon_inval_window_rect(WindowPtr window, const Rect* rectangle)
{
    if (!rectangle)
        return ROMlib_valid_window(window) ? paramErr : -5600;
    return change_update(window, rectangle, NULL, FALSE);
}

LONGINT ROMlib_carbon_inval_window_region(WindowPtr window, RgnHandle region)
{
    return change_update(window, NULL, region, FALSE);
}

LONGINT ROMlib_carbon_valid_window_rect(WindowPtr window, const Rect* rectangle)
{
    if (!rectangle)
        return ROMlib_valid_window(window) ? paramErr : -5600;
    return change_update(window, rectangle, NULL, TRUE);
}

LONGINT ROMlib_carbon_valid_window_region(WindowPtr window, RgnHandle region)
{
    return change_update(window, NULL, region, TRUE);
}
