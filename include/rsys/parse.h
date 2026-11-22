/*+===================================================================
  File:      parse.h

  Summary:   Declares parse support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_RSYS_PARSE_H_)
#define _RSYS_PARSE_H_

/*
 * Copyright 1995 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: parse.h 63 2004-12-24 18:19:43Z ctm $
 */

#include "rsys/options.h"

extern int yyparse(void); /* ick -- that's what yacc produces */
extern int ROMlib_AppleChar;
extern int ROMlib_right_button_modifier;
extern void ROMlib_HideScreen(void);
extern void ROMlib_SetTitle(char* name);
extern void ROMlib_SetLocation(pair_t* pairsp);
extern void ROMlib_ShowScreen(void);
extern void ROMlib_SetSize(pair_t* pairsp, pair_t* pairs2p);
extern char* ROMlib_GetTitle(void);
extern void ROMlib_FreeTitle(char* title);

#endif
