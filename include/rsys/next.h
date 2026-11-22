/*+===================================================================
  File:      next.h

  Summary:   Supplies guest A5 save and restore macros for print callbacks.

  Classes:   None.

  Functions: SETUPA5 and RESTOREA5 callback context macros.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_RSYS_NEXT_H_)
#define _RSYS_NEXT_H_

/*
 * Copyright 1991 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: next.h 63 2004-12-24 18:19:43Z ctm $
 */

#define SETUPA5     \
    LONGINT savea5; \
    savea5 = EM_A5; \
    EM_A5 = CL((LONGINT)CurrentA5)

#define RESTOREA5 EM_A5 = savea5

#endif
