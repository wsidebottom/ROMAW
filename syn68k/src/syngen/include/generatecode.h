/*+===================================================================
  File:      generatecode.h

  Summary:   ifndef _generatecode_h_

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in syn68k/src/syngen/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#ifndef _generatecode_h_
#define _generatecode_h_

#include "syn68k_private.h"
#include "defopcode.h"

extern void generate_c_code(const ParsedOpcodeInfo* info, const CCVariant* var, int m68kop, int synop, SymbolTable* sym,
                            const char* operand_decls, const OpcodeMappingInfo* map);

#endif /* Not _generatecode_h_ */
