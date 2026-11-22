/*+===================================================================
  File:      ppc-fused-integer.h

  Summary:   Exact integer fused arithmetic shared by interpreted and emitted native operations.

  Classes:   magnitude.

  Functions: Internal calculate template and magnitude operations.

  Origin:    Extracted unchanged integer algorithm from synppc/src/ppc-fused.cpp.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ROMAW_PPC_FUSED_INTEGER_H
#define ROMAW_PPC_FUSED_INTEGER_H
#include "ppc-fused.h"
#include <algorithm>

namespace romaw_ppc_fused_detail {
template <unsigned N> struct magnitude {
    uint64_t words[N] = {};

    __attribute__((always_inline)) inline void insert(uint64_t value, unsigned shift)
    {
        unsigned index = shift / 64, bit = shift % 64;
        words[index] |= value << bit;
        if (bit && index + 1 < N)
            words[index + 1] |= value >> (64 - bit);
    }

    __attribute__((always_inline)) inline int compare(const magnitude& other) const
    {
        for (unsigned i = N; i-- > 0;)
            if (words[i] != other.words[i])
                return words[i] > other.words[i] ? 1 : -1;
        return 0;
    }

    __attribute__((always_inline)) inline void combine(const magnitude& other, bool subtract)
    {
        uint64_t carry = 0;
        for (unsigned i = 0; i < N; ++i) {
            uint64_t a = words[i], b = other.words[i];
            uint64_t interim = subtract ? a - b : a + b;
            words[i] = subtract ? interim - carry : interim + carry;
            carry = subtract ? (a < b || interim < carry) : (interim < a || words[i] < interim);
        }
    }

    __attribute__((always_inline)) inline int highest() const
    {
        for (unsigned i = N; i-- > 0;)
            if (words[i])
                return i * 64 + 63 - __builtin_clzll(words[i]);
        return -1;
    }

    __attribute__((always_inline)) inline uint64_t shifted(unsigned shift) const
    {
        unsigned index = shift / 64, bit = shift % 64;
        uint64_t result = words[index] >> bit;
        if (bit && index + 1 < N)
            result |= words[index + 1] << (64 - bit);
        return result;
    }

    __attribute__((always_inline)) inline bool below(unsigned limit) const
    {
        unsigned index = limit / 64, bit = limit % 64;
        for (unsigned i = 0; i < index; ++i)
            if (words[i])
                return true;
        return bit && (words[index] & ((UINT64_C(1) << bit) - 1));
    }
};

template <unsigned E, unsigned F, unsigned N>
__attribute__((always_inline)) inline ppc_fused_result calculate(uint64_t a, uint64_t b, uint64_t c, unsigned mode,
                                                                 unsigned enables)
{
    constexpr uint64_t fraction = (UINT64_C(1) << F) - 1;
    constexpr uint64_t hidden = UINT64_C(1) << F;
    constexpr uint64_t sign_bit = UINT64_C(1) << (E + F);
    constexpr unsigned infinity = (1u << E) - 1;
    constexpr int bias = (1u << (E - 1)) - 1;
    constexpr uint64_t quiet = hidden >> 1, nan = (uint64_t)infinity << F | quiet;
    constexpr int minimum = 1 - bias - (int)F, base = 2 * minimum;
    const uint64_t operands[] = {a, c, b}; /* FRA, FRB, FRC preference. */
    uint64_t selected = 0;
    bool signaling = false;
    for (uint64_t operand : operands)
        if (((operand >> F) & infinity) == infinity && (operand & fraction)) {
            signaling |= !(operand & quiet);
            if (!selected)
                selected = operand | quiet;
        }
    if (selected) {
        return {selected, signaling ? 16u : 0u, false};
    }
    unsigned ae = (a >> F) & infinity, be = (b >> F) & infinity;
    unsigned ce = (c >> F) & infinity;
    uint64_t am = (a & fraction) | (ae ? hidden : 0);
    uint64_t bm = (b & fraction) | (be ? hidden : 0);
    uint64_t cm = (c & fraction) | (ce ? hidden : 0);
    bool product_sign = !!((a ^ b) & sign_bit), addend_sign = !!(c & sign_bit);
    if (ae == infinity || be == infinity) {
        if (!am || !bm || (ce == infinity && product_sign != addend_sign)) {
            return {nan, 16, false};
        }
        return {(product_sign ? sign_bit : 0) | (uint64_t)infinity << F, 0, false};
    }
    if (ce == infinity)
        return {c, 0, false};
    int as = ae ? (int)ae - bias - (int)F : minimum;
    int bs = be ? (int)be - bias - (int)F : minimum;
    int cs = ce ? (int)ce - bias - (int)F : minimum;
    /* Exact 53x53 multiplication using 32-bit halves; no __int128 dependency. */
    uint64_t low = (uint64_t)(uint32_t)am * (uint32_t)bm;
    uint64_t cross = (am >> 32) * (uint32_t)bm + (bm >> 32) * (uint32_t)am;
    uint64_t high = (am >> 32) * (bm >> 32) + (cross >> 32), saved = low;
    low += cross << 32;
    high += low < saved;
    magnitude<N> product, addend;
    product.insert(low, as + bs - base);
    product.insert(high, as + bs - base + 64);
    addend.insert(cm, cs - base);
    bool sign = product_sign;
    if (product_sign == addend_sign)
        product.combine(addend, false);
    else if (product.compare(addend) >= 0)
        product.combine(addend, true);
    else {
        addend.combine(product, true);
        product = addend;
        sign = addend_sign;
    }
    int leading = product.highest();
    if (leading < 0) {
        sign = product_sign == addend_sign ? product_sign : mode == 3;
        return {sign ? sign_bit : 0, 0, false};
    }
    bool tiny = leading + base < 1 - bias;
    int shift = leading - (int)F;
    if (!(tiny && (enables & 0x20)))
        shift = std::max(shift, -minimum);
    uint64_t rounded = shift < 0 ? product.shifted(0) << -shift : product.shifted(shift);
    bool discarded = shift > 0 && product.below(shift);
    bool half = shift > 0 && (product.shifted(shift - 1) & 1);
    bool increment = mode == 0 ? half && (product.below(shift - 1) || (rounded & 1))
                               : discarded && ((mode == 2 && !sign) || (mode == 3 && sign));
    rounded += increment;
    if (rounded >= hidden * 2)
        rounded >>= 1, ++leading;
    int exponent = leading + base + bias;
    unsigned flags = discarded ? 1 : 0;
    if (exponent >= (int)infinity) {
        flags |= 4;
        if (enables & 0x40)
            exponent -= E == 8 ? 192 : 1536;
        else {
            bool infinite = mode == 0 || (mode == 2 && !sign) || (mode == 3 && sign);
            uint64_t bits =
                (sign ? sign_bit : 0) | (infinite ? (uint64_t)infinity << F : ((uint64_t)infinity << F) - 1);
            return {bits, flags | 1u, increment};
        }
    }
    else if (tiny && (enables & 0x20)) {
        flags |= 2;
        exponent += E == 8 ? 192 : 1536;
    }
    else if (tiny && discarded)
        flags |= 2;
    if (exponent <= 0)
        exponent = rounded >= hidden ? 1 : 0;
    uint64_t bits = (sign ? sign_bit : 0) | (uint64_t)exponent << F | (rounded & fraction);
    return {bits, flags, increment};
}
}

#endif
