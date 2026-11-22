/*+===================================================================
  File:      ppc-fused.h

  Summary:   Declares ppc fused support for synppc PowerPC execution and fragment loading.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in synppc/src; original attribution and provenance comments are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ROMAW_PPC_FUSED_H
#define ROMAW_PPC_FUSED_H
#include <stdint.h>

struct ppc_fused_result {
    uint64_t bits;
    /* IEEE inexact/underflow/overflow/zero-divide/invalid bits 0..4. */
    unsigned flags;
    bool incremented;
};

/* Exact a*b+c with FPSCR RN and OE/UE, in native binary32/binary64 bits. */
ppc_fused_result romaw_ppc_fused_status(bool single, unsigned rounding, uint64_t a, uint64_t b, uint64_t c,
                                        unsigned enables);
/* IEEE a*b+c bits, rounded once with the current FE mode. */
uint64_t romaw_ppc_fused_bits(bool single, uint64_t a, uint64_t b, uint64_t c);
#endif
