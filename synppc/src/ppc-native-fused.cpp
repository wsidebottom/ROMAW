/*+===================================================================
  File:      ppc-native-fused.cpp

  Summary:   Supplies relocation-free integer instruction templates for native fused arithmetic.

  Classes:   Uses ppc_native_fused_state and ppc_fused_result.

  Functions: Build-time native_fused_s28 through native_fused_d31 templates.

  Origin:    Original native lowering using the exact synppc fused integer algorithm.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ppc-fused-integer.h"
#include "ppc-native-fused-state.h"

namespace {
__attribute__((always_inline)) inline uint32_t narrow(uint64_t bits, unsigned mode)
{
    uint32_t sign = (bits >> 32) & 0x80000000;
    unsigned exponent = (bits >> 52) & 0x7ff;
    uint64_t significand = (bits & UINT64_C(0x000fffffffffffff)) | (exponent ? UINT64_C(0x0010000000000000) : 0);
    if (!significand)
        return sign;
    int target = (int)(exponent ? exponent : 1) - 1023 + 127;
    unsigned shift = target > 0 ? 29 : 30 - target;
    uint64_t rounded = shift < 64 ? significand >> shift : 0;
    bool discarded = shift < 64 ? !!(significand & ((UINT64_C(1) << shift) - 1)) : !!significand;
    bool half = shift <= 64 && !!(significand & (UINT64_C(1) << (shift - 1)));
    bool lower = shift <= 64 && !!(significand & ((UINT64_C(1) << (shift - 1)) - 1));
    rounded +=
        mode == 0 ? half && (lower || (rounded & 1)) : discarded && ((mode == 2 && !sign) || (mode == 3 && sign));
    if (rounded >= 0x1000000)
        rounded >>= 1, ++target;
    if (target >= 255) {
        bool infinity = mode == 0 || (mode == 2 && !sign) || (mode == 3 && sign);
        return sign | (infinity ? 0x7f800000 : 0x7f7fffff);
    }
    if (target <= 0)
        target = rounded >= 0x800000 ? 1 : 0;
    return sign | (uint32_t)target << 23 | ((uint32_t)rounded & 0x7fffff);
}

__attribute__((always_inline)) inline uint64_t expand(uint32_t bits)
{
    uint64_t sign = (uint64_t)(bits & 0x80000000) << 32;
    unsigned exponent = (bits >> 23) & 255, fraction = bits & 0x7fffff;
    if (!exponent && !fraction)
        return sign;
    if (exponent == 255)
        return sign | UINT64_C(0x7ff0000000000000) | (uint64_t)fraction << 29;
    if (!exponent) {
        unsigned shift = __builtin_clz(fraction) - 8;
        fraction <<= shift;
        return sign | (uint64_t)(897 - shift) << 52 | (uint64_t)(fraction & 0x7fffff) << 29;
    }
    return sign | (uint64_t)(exponent + 896) << 52 | (uint64_t)fraction << 29;
}

template <bool Single, unsigned Operation>
__attribute__((always_inline)) inline void fused(ppc_native_fused_state* state)
{
    const uint64_t sign = UINT64_C(0x8000000000000000), infinity = UINT64_C(0x7ff0000000000000);
    const uint64_t minimum = Single ? UINT64_C(0x3810000000000000) : UINT64_C(0x0010000000000000);
    uint64_t a = state->a, b = state->b, c = state->c;
    uint32_t initial = state->fpscr;
    if (initial & 4) {
        if ((a & ~sign) < minimum)
            a &= sign;
        if ((b & ~sign) < minimum)
            b &= sign;
        if ((c & ~sign) < minimum)
            c &= sign;
    }
    if (!(Operation & 1))
        b ^= sign;
    unsigned rounding = initial & 3;
    ppc_fused_result value;
    if (Single)
        value = romaw_ppc_fused_detail::calculate<8, 23, 9>(narrow(a, rounding), narrow(c, rounding),
                                                            narrow(b, rounding), rounding, initial);
    else
        value = romaw_ppc_fused_detail::calculate<11, 52, 66>(a, c, b, rounding, initial);
    uint64_t bits = Single ? expand(value.bits) : value.bits;
    if (Operation >= 30 && (bits & ~sign) <= infinity)
        bits ^= sign;
    uint32_t raised =
        (value.flags & 1 ? 0x02000000 : 0) | (value.flags & 2 ? 0x08000000 : 0) | (value.flags & 4 ? 0x10000000 : 0);
    bool incremented = value.incremented;
    bool scaled = ((raised & 0x08000000) && (initial & 0x20)) || ((raised & 0x10000000) && (initial & 0x40));
    uint64_t magnitude = bits & ~sign;
    if ((initial & 4) && !(initial & 0x20) && !scaled &&
        ((raised & 0x08000000) || (magnitude && magnitude < minimum))) {
        bits &= sign;
        raised |= 0x0a000000;
        incremented = false;
        magnitude = 0;
    }
    uint32_t status = initial & ~0x0007f000u;
    if (raised & 0x02000000)
        status |= 0x20000;
    if (incremented)
        status |= 0x40000;
    if (raised & ~status)
        status |= 0x80000000;
    status |= raised;
    status = (status & ~0x60000000u) | ((status & 0x01f80700) ? 0x20000000 : 0);
    if ((status >> 25) & (status >> 3) & 31)
        status |= 0x40000000;
    bool negative = bits & sign;
    uint32_t classification = magnitude > infinity    ? 0x11000
                              : magnitude == infinity ? (negative ? 0x9000 : 0x5000)
                              : !magnitude            ? (negative ? 0x12000 : 0x2000)
                                           : (negative ? 0x8000 : 0x4000) | (magnitude < minimum ? 0x10000 : 0);
    state->fpscr = status | classification;
    state->result = bits;
}
}

// Each section is copied into the generated block. The generator rejects calls,
// external references and relocations; these functions are never linked as helpers.
#define NATIVE_FUSED(Name, Single, Operation)                                                                    \
    extern "C" __attribute__((section(".text." #Name), used, noinline)) void Name(ppc_native_fused_state* state) \
    {                                                                                                            \
        fused<Single, Operation>(state);                                                                         \
    }
NATIVE_FUSED(native_fused_s28, true, 28)
NATIVE_FUSED(native_fused_s29, true, 29)
NATIVE_FUSED(native_fused_s30, true, 30)
NATIVE_FUSED(native_fused_s31, true, 31)
NATIVE_FUSED(native_fused_d28, false, 28)
NATIVE_FUSED(native_fused_d29, false, 29)
NATIVE_FUSED(native_fused_d30, false, 30)
NATIVE_FUSED(native_fused_d31, false, 31)
