/*+===================================================================
  File:      macro.h

  Summary:   ifndef _macro_h_

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in syn68k/src/syngen/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#ifndef _macro_h_
#define _macro_h_

#include "list.h"
#include "hash.h"

#define MAX_MACRO_EXPANSIONS 64

typedef struct MacroStruct {
    List* expr;
    struct MacroStruct* next;
} Macro;

extern BOOL macro_sub(List* ls, const SymbolTable* sym, BOOL flag_err);

#endif /* Not _macro_h_ */
