/*+===================================================================
  File:      win_queue.h

  Summary:   Declares win queue support for host-platform configuration and integration.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/config/os/cygwin32; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__WIN_QUEUE_H__)
#define __WIN_QUEUE_H__

extern void win_queue(volatile uint8* pendingp);
extern void set_timer_driven_events(boolean_t value);

#endif
