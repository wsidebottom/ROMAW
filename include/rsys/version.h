/*+===================================================================
  File:      version.h

  Summary:   Declares version support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_RSYS_VERSION_H_)
#define _RSYS_VERSION_H_

/* $Id: version.h 94 2005-05-25 15:53:40Z ctm $ */

#include "generated/build_version.h"

#define SYSTEM_VERSION_ENCODING(m, n, x, b) ((m) * 1000000 + (n) * 10000 + ((x) ? ((x) - 'a' + 1) * 100 : 0) + (b))

#if !defined(ROMAW_VERSION_NUMERIC)
#define ROMAW_VERSION_NUMERIC SYSTEM_VERSION_ENCODING(3, 0, 0, 0)
#endif

#define MINIMUM_SYSTEM_FILE_NEEDED SYSTEM_VERSION_ENCODING(2, 0, 0, 3)

#define MAJOR_REVISION 2
#define MINOR_REVISION 1

extern const char ROMlib_romaw_version[];
extern char* ROMlib_romaw_full_name;

#define PLATFORM_MOD 10   /* last decimal digit of serial number is platform */
#define COMPOSITE_MOD 100 /* 2nd to last digit is price class */

#if defined(CYGWIN32)
#define VERSION_SIG PLATFORM_CYGWIN32
#elif defined(LINUX)
#define VERSION_SIG PLATFORM_LINUX
#else
#error "Need VERSION_SIG #define"
#endif

enum { CLASS_COMMERCIAL = 0, CLASS_EDUCATIONAL = 10, CLASS_STUDENT = 20 };

enum { PLATFORM_CYGWIN32 = 1, PLATFORM_LINUX = 3, PLATFORM_ANY = 4 };

enum {
    STUDENT_LINUX = CLASS_STUDENT + PLATFORM_LINUX,
};

#define ROMAW_NAME "ROMAW"

extern void ROMlib_set_system_version(uint32 version);

#endif /* !_RSYS_VERSION_H_ */
