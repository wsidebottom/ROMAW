/*+===================================================================
  File:      x86_64.h

  Summary:   Declares x86 64 support for host-platform configuration and integration.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/config/arch/x86_64; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__arch_x86_64_h__)
#define __arch_x86_64_h__

#define LITTLEENDIAN
#define SYN68K

/* TODO: only do these if the compiler supports it, check to see if we can
         get a better swap16 w/o using builtin_bswap32 */

#define swap16(v) ((uint16_t)(__builtin_bswap32((int32_t)(v)) >> 16))

#define swap32(v) ((uint32_t)__builtin_bswap32((int32_t)(v)))

#endif /* !defined (__arch_x86_64_h__) */
