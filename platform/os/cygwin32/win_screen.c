/*+===================================================================
  File:      win_screen.c

  Summary:   Implements win screen support for host-platform configuration and integration.

  Classes:   None.

  Functions: ROMlib_recenter_window, os_current_screen_height, os_current_screen_width, os_maximum_window_height,
             os_maximum_window_width

  Origin:    Existing repository source in executor/src/config/os/cygwin32; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1999 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#define USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES

#include "rsys/common.h"
#include "rsys/error.h"

#include <windows.h>

extern HWND Win_Window;

PUBLIC int os_current_screen_width(void)
{
    int retval;

    retval = GetSystemMetrics(SM_CXSCREEN);
    return retval;
}

PUBLIC int os_current_screen_height(void)
{
    int retval;

    retval = GetSystemMetrics(SM_CYSCREEN);
    return retval;
}

PUBLIC int os_maximum_window_height(void)
{
    int retval;

    if (0)
        retval = os_current_screen_height();
    else
        retval = GetSystemMetrics(SM_CYFULLSCREEN);
    return retval;
}

PUBLIC int os_maximum_window_width(void)
{
    int retval;

    if (0)
        retval = os_current_screen_width();
    else
        retval = GetSystemMetrics(SM_CXFULLSCREEN);
    return retval;
}

PUBLIC void ROMlib_recenter_window(void)
{
    if (1) {
        RECT bounds;

        if (GetWindowRect(Win_Window, &bounds)) {
            int width, height, caption_height;
            int x, y;

            caption_height = GetSystemMetrics(SM_CYCAPTION);
            width = bounds.right - bounds.left;
            height = bounds.bottom - bounds.top;
            x = (GetSystemMetrics(SM_CXFULLSCREEN) - width) / 2;
            y = (GetSystemMetrics(SM_CYFULLSCREEN) - height) / 2;

            SetWindowPos(Win_Window, NULL, x, y + caption_height, width, height,
                         (SWP_NOCOPYBITS | SWP_NOZORDER | SWP_SHOWWINDOW));
        }
    }
}
