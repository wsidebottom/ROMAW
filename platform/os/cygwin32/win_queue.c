/*+===================================================================
  File:      win_queue.c

  Summary:   Implements win queue support for host-platform configuration and integration.

  Classes:   None.

  Functions: set_timer_driven_events, win_queue

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

PRIVATE boolean_t timer_driven_events_p = TRUE;

PUBLIC void win_queue(volatile uint8* pendingp)
{
    if (timer_driven_events_p)
        *pendingp = TRUE;
}

PUBLIC void set_timer_driven_events(boolean_t value)
{
    timer_driven_events_p = value;
}
