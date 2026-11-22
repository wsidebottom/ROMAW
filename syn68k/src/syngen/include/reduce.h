/*+===================================================================
  File:      reduce.h

  Summary:   ifndef _reduce_h_

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in syn68k/src/syngen/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#ifndef _reduce_h_
#define _reduce_h_

#include "list.h"
#include "hash.h"

extern List* reduce_list(List* ls, const SymbolTable* sym, int macro_level);
extern List* reduce(List* ls, const SymbolTable* sym, int macro_level);

#endif /* Not _reduce_h_ */
