/*+===================================================================
  File:      win_ntinc.h

  Summary:   Declares win ntinc support for host-platform configuration and integration.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/config/os/cygwin32; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#define IN

#define CTL_CODE(device_type, function, method, access) \
    (((device_type) << 16) | ((function) << 2) | ((method) << 0) | ((access) << 14))

#define METHOD_BUFFERED 0
#define FILE_ANY_ACCESS 0
