/*+===================================================================
  File:      desktop.h

  Summary:   Internal entry and thread-affinity checks for the built-in Finder.

  Classes:   None.

  Functions: Interface declarations for the internal desktop entry point.

  Origin:    Internal Finder startup separated from the existing Executor runtime
             native application bridge.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/

#ifndef ROMAW_RSYS_DESKTOP_H
#define ROMAW_RSYS_DESKTOP_H

void ROMlib_desktop_set_entry(int (*entry)(void));
int ROMlib_desktop_active(void);
int ROMlib_desktop_is_ui_thread(void);
int ROMlib_desktop_run(void);

#endif
