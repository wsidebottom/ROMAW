/*+===================================================================
  File:      common.h

  Summary:   Declares common support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_COMMON_H_)
#define _COMMON_H_

#if !defined(COMPILE_FOR_BUILD)
#include "config.h"
#endif

#if defined(_WIN32) && !defined(WIN32)
#define WIN32 /* evil hackage needed to make SDL happy */
#endif

#if defined(COMPILE_FOR_BUILD)
#include "build-os-config.h"
#include "build-arch-config.h"
#else /* !COMPILE_FOR_BUILD */
#include "host-os-config.h"
#include "host-arch-config.h"
#endif /* !COMPILE_FOR_BUILD */

#if !defined(CYGWIN32) && defined(USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES)
#undef USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES
#endif

#if !defined(COMPILE_FOR_BUILD) && (!defined(_WIN32) || !defined(__x86_64__))
#error "ROMAW requires a Windows x64 host"
#endif

#include "rsys/cruft.h"
#include "rsys/macros.h"
#include "rsys/types.h"
#include "rsys/mactype.h"
#include "rsys/byteswap.h"

#if !defined USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES
#include <syn68k_public.h>
#include "MacTypes.h"
#include "rsys/trapdecl.h"
#include "rsys/stubify.h"
#include "rsys/slash.h"
#include "rsys/error.h"
#include "rsys/lowglobals.h"
#endif

#if !defined(COMPILE_FOR_BUILD)
#include "front-end-config.h"
#endif

typedef enum { get, put } getorput_t;

typedef enum { original_sf, new_sf, new_custom_sf } sf_flavor_t;

#endif /* !_COMMON_H_ */
