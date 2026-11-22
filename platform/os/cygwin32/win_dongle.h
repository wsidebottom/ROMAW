/*+===================================================================
  File:      win_dongle.h

  Summary:   Declares win dongle support for host-platform configuration and integration.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/config/os/cygwin32; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__WIN_DONGLE_H__)
#define __WIN_DONGLE_H__

#define SENTINEL_DLL "sp32w.dll"
#define SENTINEL_STRING "ashi"
#define SENTINEL_FAMILY "AH"

#define HASP_DLL "haspms32.dll"

extern int dongle_query(uint32* valuep);

extern char* ROMlib_DongleFamily;

#endif
