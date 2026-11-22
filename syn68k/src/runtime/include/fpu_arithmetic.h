/*+===================================================================
  File:      fpu_arithmetic.h

  Summary:   Declares fpu arithmetic support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in syn68k/src/runtime/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Motorola normalized arithmetic, retaining the exponent and 67-bit
   writeback value before the selected precision is applied. */
#ifndef SYN68K_FPU_ARITHMETIC_H
#define SYN68K_FPU_ARITHMETIC_H

#include "fpu_exception.h"

static FPUdivision fpu_intermediate(unsigned sign, int exponent, uint64 high, uint64 low, unsigned precision)
{
    FPUdivision value = {0};
    if (!(high | low)) {
        value.result.high = value.condition.high = sign;
        return value;
    }
    if (!high) {
        high = low;
        low = 0;
        exponent -= 64;
    }
    unsigned shift = countLeadingZeros64(high);
    if (shift) {
        shortShift128Left(high, low, shift, &high, &low);
        exponent -= shift;
    }
    value.finite = 1;
    value.condition = roundAndPackFloatx80(precision == 96 ? 32 : precision, sign != 0, 0x3fff, high, low);
    int rounded_exponent = exponent + (value.condition.high & 0x7fff) - 0x3fff;
    value.result = value.condition;
    value.result.high = sign | (rounded_exponent & 0x7fff);
    int minimum = precision == 32 ? 0x3f81 : precision == 64 ? 0x3c01 : 0;
    int maximum = precision == 32 ? 0x407e : precision == 64 ? 0x43fe : 0x7ffe;
    if (exponent < minimum)
        value.status = 0x0800;
    else if (rounded_exponent > maximum)
        value.status = 0x1000;
    value.grs = low >> 62;
    value.sticky = (low << 2) != 0;
    if (value.status == 0x1000) {
        exponent = rounded_exponent;
        high = value.result.low;
        value.grs = value.sticky = 0;
    }
    value.writeback.high = sign | (exponent & 0x7fff);
    value.writeback.low = high;
    value.exponent15 = ((unsigned)exponent >> 15) & 1;
    return value;
}

static FPUdivision fpu_calculate_ex(floatx80 destination, floatx80 source, unsigned operation, unsigned precision,
                                    int destination_exponent, int source_exponent)
{
    /* FSGL instructions use a 24-bit mantissa and extended exponent range.
     96 is internal shorthand for that combination, not a storage format. */
    if (operation == 0x24 || operation == 0x27) {
        if (source_exponent != 0x7fff)
            source.low &= UINT64_C(0xffffff0000000000);
        if (destination_exponent != 0x7fff)
            destination.low &= UINT64_C(0xffffff0000000000);
        operation = operation == 0x24 ? 0x20 : 0x23;
        precision = 96;
    }
    if (operation == 0x20)
        return fpu_divide_ex(destination, source, precision, destination_exponent, source_exponent);
    FPUdivision value = {0};
    if (source_exponent == 0x7fff ||
        ((operation == 0x22 || operation == 0x23 || operation == 0x28) && destination_exponent == 0x7fff) ||
        (operation == 4 && (source.high & 0x8000) && source.low)) {
        if (source_exponent < 0)
            source.high = (source.high & 0x8000) | 0x3fff;
        if (destination_exponent < 0)
            destination.high = (destination.high & 0x8000) | 0x3fff;
        switch (operation) {
        case 4:
            value.result = floatx80_sqrt(source);
            break;
        case 0x22:
            value.result = floatx80_add(destination, source);
            break;
        case 0x23:
            value.result = floatx80_mul(destination, source);
            break;
        case 0x28:
            value.result = floatx80_sub(destination, source);
            break;
        default:
            if (operation == 0x18)
                source.high &= 0x7fff;
            if (operation == 0x1a)
                source.high ^= 0x8000;
            value.result = floatx80_mul(source, int32_to_floatx80(1));
            break;
        }
        value.condition = value.result;
        return value;
    }
    unsigned sign = source.high & 0x8000;
    int exponent = source_exponent;
    uint64 high = source.low, low = 0;
    if (operation == 0x18)
        sign = 0;
    else if (operation == 0x1a)
        sign ^= 0x8000;
    else if (operation == 0x23) {
        sign ^= destination.high & 0x8000;
        exponent += destination_exponent - 0x3fff + 1;
        mul64To128(destination.low, source.low, &high, &low);
    }
    else if (operation == 0x22 || operation == 0x28) {
        if (operation == 0x28)
            sign ^= 0x8000;
        unsigned a_sign = destination.high & 0x8000;
        int a_exponent = destination_exponent;
        uint64 a_high = destination.low, a_low = 0;
        /* Zero has no exponent. Keep the other operand's actual exponent. */
        if (!a_high)
            a_exponent = exponent;
        if (!high)
            exponent = a_exponent;
        int difference = a_exponent - exponent;
        if (difference > 0) {
            shift128RightJamming(high, low, difference > 128 ? 128 : difference, &high, &low);
            exponent = a_exponent;
        }
        else if (difference < 0)
            shift128RightJamming(a_high, a_low, difference < -128 ? 128 : -difference, &a_high, &a_low);
        if (sign == a_sign) {
            uint64 original = high;
            add128(a_high, a_low, high, low, &high, &low);
            if (high < original || (high == original && low < a_low)) {
                shift128RightJamming(high, low, 1, &high, &low);
                high |= UINT64_C(1) << 63;
                ++exponent;
            }
        }
        else if (lt128(high, low, a_high, a_low)) {
            sub128(a_high, a_low, high, low, &high, &low);
            sign = a_sign;
        }
        else
            sub128(high, low, a_high, a_low, &high, &low);
        if (!(high | low) && sign != a_sign)
            sign = float_rounding_mode == float_round_down ? 0x8000 : 0;
    }
    else if (operation == 4 && high) {
        int unbiased = exponent - 0x3fff;
        unsigned parity = (unsigned)unbiased & 1;
        exponent = (unbiased - (int)parity) / 2 + 0x3fff;
        unsigned shift = 67 + parity;
        uint64 root_high = 0, root_low = 0, rem_high = 0, rem_low = 0;
        /* Restoring integer square root. The radicand is the 64-bit source
         shifted by 67/68, yielding 66 root bits plus an exact remainder. */
        for (int pair = 65; pair >= 0; --pair) {
            unsigned digit = 0;
            for (int bit = pair * 2 + 1; bit >= pair * 2; --bit)
                digit =
                    digit * 2 + (bit >= (int)shift && bit < (int)shift + 64 ? (source.low >> (bit - shift)) & 1 : 0);
            shortShift128Left(rem_high, rem_low, 2, &rem_high, &rem_low);
            rem_low |= digit;
            uint64 trial_high, trial_low;
            shortShift128Left(root_high, root_low, 2, &trial_high, &trial_low);
            trial_low |= 1;
            int take = !lt128(rem_high, rem_low, trial_high, trial_low);
            if (take)
                sub128(rem_high, rem_low, trial_high, trial_low, &rem_high, &rem_low);
            shortShift128Left(root_high, root_low, 1, &root_high, &root_low);
            root_low |= take;
        }
        high = (root_high << 62) | (root_low >> 2);
        low = (root_low << 62) | !!(rem_high | rem_low);
        sign = 0;
    }
    return fpu_intermediate(sign, exponent, high, low, precision);
}

static FPUdivision fpu_calculate(floatx80 destination, floatx80 source, unsigned operation, unsigned precision)
{
    return fpu_calculate_ex(destination, source, operation, precision, destination.high & 0x7fff, source.high & 0x7fff);
}
#endif
