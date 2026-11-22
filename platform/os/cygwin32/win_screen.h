/*+===================================================================
  File:      win_screen.h

  Summary:   Declares win screen support for host-platform configuration and integration.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/config/os/cygwin32; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__WIN_SCREEN_H__)
#define __WIN_SCREEN_H__

extern int os_current_screen_width(void);
extern int os_current_screen_height(void);
extern int os_maximum_window_height(void);
extern int os_maximum_window_width(void);
extern void ROMlib_recenter_window(void);

#endif
