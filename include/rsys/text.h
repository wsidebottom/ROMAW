/*+===================================================================
  File:      text.h

  Summary:   Declares text support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_TEXT_H_)
#define _TEXT_H_

/*
 * Copyright 1997 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: text.h 63 2004-12-24 18:19:43Z ctm $
 */

extern boolean_t disable_text_printing(void);
extern void set_text_printing(boolean_t state);
extern INTEGER ROMlib_wordb(char* p);
extern int ROMlib_forward_del_p;

#endif
