/*+===================================================================
  File:      systemwindow.h

  Summary:   ifndef RSYS_SYSTEMWINDOW_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef RSYS_SYSTEMWINDOW_H
#define RSYS_SYSTEMWINDOW_H
#include "WindowMgr.h"
#include "EventMgr.h"
extern WindowPtr ROMlib_system_window_new(Rect*, StringPtr, BOOLEAN, INTEGER, WindowPtr, BOOLEAN, LONGINT, ProcPtr,
                                          RgnHandle);
extern OSErr ROMlib_system_window_dispose(WindowPtr);
extern BOOLEAN ROMlib_system_window_p(WindowPtr);
extern BOOLEAN ROMlib_system_window_event(EventRecord*);
extern void ROMlib_system_window_forget(WindowPtr);
extern void ROMlib_system_window_reset(void);
#endif
