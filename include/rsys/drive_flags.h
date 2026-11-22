/*+===================================================================
  File:      drive_flags.h

  Summary:   Declares drive flags support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__rsys_drive_flags__)
#define __rsys_drive_flags__

typedef unsigned char drive_flags_t;

enum { DRIVE_FLAGS_LOCKED = 1, DRIVE_FLAGS_FIXED = 2, DRIVE_FLAGS_FLOPPY = 4 };

#endif /* !defined(__rsys_drive_flags__) */
