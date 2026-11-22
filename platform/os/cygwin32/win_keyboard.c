/*+===================================================================
  File:      win_keyboard.c

  Summary:   Implements win keyboard support for host-platform configuration and integration.

  Classes:   None.

  Functions: ROMlib_set_caps_lock_off

  Origin:    Existing repository source in executor/src/config/os/cygwin32; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 2000 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#define USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES

#include "rsys/common.h"
#include "rsys/error.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "win_keyboard.h"

PUBLIC void ROMlib_set_caps_lock_off(void)
{
    if (GetKeyState(VK_CAPITAL) & 1) {
        char state[256];

        GetKeyboardState(state);
        state[VK_CAPITAL] = 0;
        SetKeyboardState(state);
    }
}
