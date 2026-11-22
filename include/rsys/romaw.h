/*+===================================================================
  File:      romaw.h

  Summary:   Declares romaw support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__RSYS_ROMAW__)
#define __RSYS_ROMAW__

/*
 * Copyright 1995 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: executor.h 63 2004-12-24 18:19:43Z ctm $
 */

extern LONGINT debugnumber;
extern LONGINT ROMlib_creator;
extern syn68k_addr_t alinehandler(syn68k_addr_t pc, void* ignored);
extern void filltables(void);
extern void romaw_main(void);

#endif
