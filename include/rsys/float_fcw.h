/*+===================================================================
  File:      float_fcw.h

  Summary:   Declares float fcw support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_romlib_float_fcw_h_)
#define _romlib_float_fcw_h_

/*
 * Copyright 2000 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: float_fcw.h 63 2004-12-24 18:19:43Z ctm $
 */

extern uint32 ROMlib_get_fcw_fsw(void);
extern void ROMlib_set_fcw_fsw(uint32 fcwfsw);
extern void ROMlib_compare_fcw_fsw(uint32 fcwfsw, const char* func, int line);

#endif
