/*+===================================================================
  File:      ppc-fused.cpp

  Summary:   Implements ppc fused support for synppc PowerPC execution and fragment loading.

  Classes:   Uses ppc_fused_result and the shared integer magnitude template.

  Functions: romaw_ppc_fused_bits, romaw_ppc_fused_status

  Origin:    Existing repository source in synppc/src; original attribution and provenance comments are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Original exact fused arithmetic. Fixed integers cover the full exponent
   span, avoiding host intermediate rounding and allocation. */
#include "ppc-fused.h"
#include <fenv.h>
#include "ppc-fused-integer.h"
using romaw_ppc_fused_detail::calculate;

ppc_fused_result romaw_ppc_fused_status(bool single, unsigned rounding, uint64_t a, uint64_t b, uint64_t c,
                                        unsigned enables)
{
    return single ? calculate<8, 23, 9>(a, b, c, rounding, enables) : calculate<11, 52, 66>(a, b, c, rounding, enables);
}

uint64_t romaw_ppc_fused_bits(bool single, uint64_t a, uint64_t b, uint64_t c)
{
    int mode = fegetround();
    unsigned rounding = mode == FE_TOWARDZERO ? 1 : mode == FE_UPWARD ? 2 : mode == FE_DOWNWARD ? 3 : 0;
    ppc_fused_result result = romaw_ppc_fused_status(single, rounding, a, b, c, 0);
    int flags = (result.flags & 1 ? FE_INEXACT : 0) | (result.flags & 2 ? FE_UNDERFLOW : 0) |
                (result.flags & 4 ? FE_OVERFLOW : 0) | (result.flags & 16 ? FE_INVALID : 0);
    if (flags)
        feraiseexcept(flags);
    return result.bits;
}
