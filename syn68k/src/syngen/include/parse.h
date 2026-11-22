/*+===================================================================
  File:      parse.h

  Summary:   ifndef _parse_h_

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in syn68k/src/syngen/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#ifndef _parse_h_
#define _parse_h_

#include "token.h"
#include "list.h"
#include "hash.h"

extern int parse_all_expressions(SymbolTable* sym);
extern List* string_to_list(const char* string, const char* include_dirs[]);

#endif /* Not _parse_h_ */
