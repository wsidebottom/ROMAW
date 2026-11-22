/*+===================================================================
  File:      Components.h

  Summary:   Declares components support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__COMPONENTS__)
#define __COMPONENTS__

/*
 * Copyright 1998 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: Components.h 63 2004-12-24 18:19:43Z ctm $
 */

typedef struct ComponentRecord {
    LONGINT data[1];
} ComponentRecord;

typedef ComponentRecord* Component;

typedef struct ComponentInstanceRecord {
    LONGINT data[1];
} ComponentInstanceRecord;

typedef ComponentInstanceRecord* ComponentInstance;

#endif
