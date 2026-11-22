/*+===================================================================
  File:      map.h

  Summary:   Declares map support for host-platform configuration and integration.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/config/front-ends/sdl; original attribution and provenance
             comments are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if defined(CYGWIN32)
extern void ROMlib_try_to_map(uint8* first_byte_unavailable);

#endif
