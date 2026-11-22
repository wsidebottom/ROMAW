/*+===================================================================
  File:      win_beep.c

  Summary:   Implements win beep support for host-platform configuration and integration.

  Classes:   None.

  Functions: host_beep_at_user

  Origin:    Existing repository source in executor/src/config/os/cygwin32; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1998 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#define USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES

#include "rsys/common.h"

#include <windows.h>

PUBLIC void host_beep_at_user()
{
    MessageBeep(MB_OK);
}
