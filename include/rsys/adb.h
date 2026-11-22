/*+===================================================================
  File:      adb.h

  Summary:   Declares adb support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_RSYS_ADB_H_)
#define _RSYS_ADB_H_

/*
 * Copyright 1995 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: adb.h 63 2004-12-24 18:19:43Z ctm $
 */

#include "ADB.h"

extern void C_adb_service_stub(void);
extern void adb_apeiron_hack(boolean_t deltas_p, ...);
extern void reset_adb_vector(void);

#endif
