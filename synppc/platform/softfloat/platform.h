/*+===================================================================
  File:      platform.h

  Summary:   Declares platform support for synppc PowerPC execution and fragment loading.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in synppc/platform/softfloat; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* ROMAW's compiler configuration; the vendored SoftFloat sources are
   unchanged. Use portable 64-bit primitives on the supported x64 hosts. */
#if !defined(__BYTE_ORDER__)
#error "SoftFloat requires a compiler-provided host byte order"
#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define LITTLEENDIAN 1
#endif

#ifdef __GNUC_STDC_INLINE__
#define INLINE inline
#else
#define INLINE extern inline
#endif
#define SOFTFLOAT_BUILTIN_CLZ 1
#include "opts-GCC.h"
