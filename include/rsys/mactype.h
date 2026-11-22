/*+===================================================================
  File:      mactype.h

  Summary:   Declares mactype support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_MACTYPE_H_)
#define _MACTYPE_H_

/*
 * Copyright 1986, 1989, 1990, 1995 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: mactype.h 63 2004-12-24 18:19:43Z ctm $
 */

typedef int16 INTEGER;
typedef int32 LONGINT;
typedef uint32 ULONGINT;

#if !defined(USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES)
typedef int8 BOOLEAN;
typedef int16 CHAR; /* very important not to use this as char */
#endif

typedef struct {
    int32 l PACKED;
} HIDDEN_LONGINT;

typedef struct {
    uint32 u PACKED;
} HIDDEN_ULONGINT;

#endif /* _MACTYPE_H_ */
