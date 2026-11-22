/*+===================================================================
  File:      win_stat.h

  Summary:   Declares win stat support for host-platform configuration and integration.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/config/os/cygwin32; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__STAT_FUN_H__)
#define __STAT_FUN_H__

extern uint32 ino_from_name(const char* name);

#endif
