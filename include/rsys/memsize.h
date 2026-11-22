/*+===================================================================
  File:      memsize.h

  Summary:   Declares memsize support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_RSYS_MEMSIZE_H_)
#define _RSYS_MEMSIZE_H_

#define MIN_APPLZONE_SIZE (512 * 1024)
#define ZONE_ALIGN_SIZE 8192
#define MAX_APPLZONE_SIZE (2047 * 1024 * 1024)
#define DEFAULT_APPLZONE_SIZE (2 * 1024 * 1024)

#define MIN_SYSZONE_SIZE (128 * 1024)
#define MAX_SYSZONE_SIZE (2047 * 1024 * 1024)

#define DEFAULT_SYSZONE_SIZE (512 * 1024)

#define MIN_STACK_SIZE (64 * 1024)
#define MAX_STACK_SIZE (2047 * 1024 * 1024)
#define DEFAULT_STACK_SIZE (256 * 1024)

#endif /* !_RSYS_MEMSIZE_H_ */
