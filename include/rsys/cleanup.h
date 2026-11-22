/*+===================================================================
  File:      cleanup.h

  Summary:   Declares cleanup support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__RSYS_CLEANUP__)
#define __RSYS_CLEANUP__

/*
 * Copyright 1998 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: cleanup.h 63 2004-12-24 18:19:43Z ctm $
 */

#if defined(CYGWIN32)

#define CLEANUP_BATCH_FILE_NAME "+/cleanup.bat"

extern void add_to_cleanup(const char* s, ...);
extern void call_cleanup_bat(void);

#endif

#endif /* !defined(__RSYS_CLEANUP__) */
