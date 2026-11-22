/*+===================================================================
  File:      byteorder.h

  Summary:   ifndef _byteorder_h_

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in syn68k/src/syngen/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#ifndef _byteorder_h_
#define _byteorder_h_

#include "defopcode.h"
#include "syn68k_private.h"

typedef struct {
    const char* operand_decls;
    int num_bitfields;
    BitfieldInfo bitfield[8];
} OperandInfo;

extern int compute_operand_info(OperandInfo* op, const ParsedOpcodeInfo* p, const CCVariant* v);

#endif /* Not _byteorder_h_ */
