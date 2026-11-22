/*+===================================================================
  File:      systemnotify.h

  Summary:   ifndef RSYS_SYSTEMNOTIFY_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef RSYS_SYSTEMNOTIFY_H
#define RSYS_SYSTEMNOTIFY_H
/* Private AA77 notification operations used by CarbonLib's 68K INIT. */
extern int32 ROMlib_system_notify_register(ProcPtr callback, uint32 kind, uint32 context);
extern int32 ROMlib_system_notify(uint32 kind, const void* data);
extern void ROMlib_system_notify_reset(void);
#endif
