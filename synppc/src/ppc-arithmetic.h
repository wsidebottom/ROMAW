/*+===================================================================
  File:      ppc-arithmetic.h

  Summary:   Declares ppc arithmetic support for synppc PowerPC execution and fragment loading.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in synppc/src; original attribution and provenance comments are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ROMAW_PPC_ARITHMETIC_H
#define ROMAW_PPC_ARITHMETIC_H
#include <stdint.h>

enum ppc_fp_operation { ppc_fp_add, ppc_fp_subtract, ppc_fp_multiply, ppc_fp_divide, ppc_fp_sqrt, ppc_fp_round_single };

struct ppc_fp_result {
    uint64_t bits;
    /* IEEE inexact, underflow, overflow, zero divide, invalid: bits 0..4. */
    unsigned flags;
};

/* Native binary32/binary64 bits. Round-single takes binary64 and returns
   binary32. The rounding selector uses the PowerPC FPSCR RN encoding. */
ppc_fp_result romaw_ppc_arithmetic(ppc_fp_operation operation, bool single, unsigned rounding, uint64_t a, uint64_t b);
uint64_t romaw_ppc_expand_single(uint32_t value);
/* Update ordinary arithmetic value, FPSCR and FPRF. Return 0 for unsupported,
   1 for a result to commit, or 2 when VE/ZE inhibits the destination. */
int romaw_ppc_arithmetic_value(uint32_t opcode, uint64_t a, uint64_t b, uint64_t c, uint32_t* fpscr, uint64_t* output);
int romaw_ppc_fused_value(uint32_t opcode, uint64_t a, uint64_t b, uint64_t c, uint32_t* fpscr, uint64_t* output);
/* Call for a handled ordinary or fused arithmetic instruction. Update invalid
   and zero-divide causes and return whether its result may be committed. */
bool romaw_ppc_arithmetic_exceptions(uint32_t opcode, uint64_t a, uint64_t b, uint64_t c, uint32_t* fpscr);

struct ppc_integer_result {
    uint32_t bits;
    unsigned flags;
    bool incremented;
};

ppc_integer_result romaw_ppc_integer(uint64_t value, unsigned rounding);
/* Update conversion status and return whether the destination is written.
   Invalid conversions with VE set leave the destination unchanged. */
bool romaw_ppc_integer_value(uint64_t value, unsigned rounding, uint32_t* fpscr, uint64_t* output);
unsigned romaw_ppc_compare_value(uint64_t a, uint64_t b, bool ordered, uint32_t* fpscr);
/* Software FPSCR control writes: derive VX/FEX and set FX only when a sticky
   exception cause changes from zero to one. Other explicitly set bits persist. */
uint32_t romaw_ppc_record_fpscr(uint32_t status, uint32_t raised);
/* Reciprocal estimates. Return 0 unsupported, 1 commit, 2 inhibited. */
int romaw_ppc_estimate_value(uint32_t opcode, uint64_t operand, uint32_t* fpscr, uint64_t* output);
#endif
