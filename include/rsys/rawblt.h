/*+===================================================================
  File:      rawblt.h

  Summary:   Declares rawblt support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_RSYS_RAWBLT_H_)
#define _RSYS_RAWBLT_H_

typedef struct {
    const void* label;
    int32 offset;
    int32 arg;
} blt_section_t;

#define MAX_BLT_SECTIONS 1024

#endif /* !_RSYS_RAWBLT_H_ */
