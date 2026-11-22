/*+===================================================================
  File:      fpu_exception.h

  Summary:   Adapted from MAME m68kfpu.cpp, fpu_exception_vector, revision 8dcfb5c90f3d3ae7845bdfee96c8f9eb09614842. C
             adapter for Syn68k.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Adapted from MAME m68kfpu.cpp, fpu_exception_vector, revision may be used to endorse or promote products
             derived from this software

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) Karl Stenerud and R. Belmont
 *
 * Adapted from MAME m68kfpu.cpp, fpu_exception_vector, revision
 * 8dcfb5c90f3d3ae7845bdfee96c8f9eb09614842. C adapter for Syn68k.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef SYN68K_FPU_EXCEPTION_H
#define SYN68K_FPU_EXCEPTION_H

#include "../../../third_party/softfloat/softfloat-compat.h"

static unsigned fpu_exception_vector(unsigned exceptions)
{
    static const unsigned char vectors[8] = {48, 54, 52, 53, 51, 50, 49, 49};
    for (unsigned bit = 0; bit < 8; ++bit)
        if (exceptions & (0x8000u >> bit))
            return vectors[bit];
    return 0;
}

/* Division scaling and 67-bit writeback recovery adapted from MAME's
   fpu_div at the same pinned revision. Motorola's exponent range differs
   from SoftFloat's precision-only control, including exponent-zero normals. */
typedef struct {
    floatx80 result, condition, writeback;
    unsigned status, grs, sticky, exponent15, finite;
} FPUdivision;

static unsigned division_bit(uint64* remainder, uint64 denominator)
{
    unsigned carry = *remainder >> 63;
    *remainder <<= 1;
    if (carry || *remainder >= denominator) {
        *remainder -= denominator;
        return 1;
    }
    return 0;
}

static FPUdivision fpu_divide_ex(floatx80 destination, floatx80 source, unsigned precision, int destination_exponent,
                                 int source_exponent)
{
    FPUdivision division = {0};
    uint64 numerator = destination.low, denominator = source.low;
    division.finite = numerator && denominator && destination_exponent != 0x7fff && source_exponent != 0x7fff;
    if (!division.finite) {
        if (destination_exponent < 0)
            destination.high = (destination.high & 0x8000) | 0x3fff;
        if (source_exponent < 0)
            source.high = (source.high & 0x8000) | 0x3fff;
        division.result = division.condition = floatx80_div(destination, source);
        return division;
    }
    /* Operands have passed the Motorola normalized-data check. Scaling avoids
     discarding the exceptional exponent or subnormal mantissa in SoftFloat. */
    int exponent = destination_exponent - source_exponent + 0x3fff;
    floatx80 a = destination, b = source;
    a.high = (a.high & 0x8000) | 0x3fff;
    b.high = (b.high & 0x8000) | 0x3fff;
    int8 saved_precision = floatx80_rounding_precision;
    floatx80_rounding_precision = precision == 96 ? 32 : precision;
    division.condition = division.result = floatx80_div(a, b);
    floatx80_rounding_precision = saved_precision;
    int rounded_exponent = exponent + (division.result.high & 0x7fff) - 0x3fff;
    if (numerator < denominator)
        --exponent;
    int minimum = precision == 32 ? 0x3f81 : precision == 64 ? 0x3c01 : 0;
    int maximum = precision == 32 ? 0x407e : precision == 64 ? 0x43fe : 0x7ffe;
    if (exponent < minimum)
        division.status = 0x0800;
    else if (rounded_exponent > maximum)
        division.status = 0x1000;
    division.result.high = (division.result.high & 0x8000) | (rounded_exponent & 0x7fff);
    uint64 remainder = numerator < denominator ? (numerator << 1) - denominator : numerator - denominator;
    uint64 significand = UINT64_C(1) << 63;
    for (int bit = 62; bit >= 0; --bit)
        if (division_bit(&remainder, denominator))
            significand |= UINT64_C(1) << bit;
    division.grs = division_bit(&remainder, denominator) << 1;
    division.grs |= division_bit(&remainder, denominator);
    division.sticky = remainder != 0;
    if (division.status & 0x1000) {
        exponent = rounded_exponent;
        significand = division.result.low;
        division.grs = division.sticky = 0;
    }
    division.writeback.high = ((destination.high ^ source.high) & 0x8000) | (exponent & 0x7fff);
    division.writeback.low = significand;
    division.exponent15 = ((unsigned)exponent >> 15) & 1;
    return division;
}

static FPUdivision fpu_divide(floatx80 destination, floatx80 source, unsigned precision)
{
    return fpu_divide_ex(destination, source, precision, destination.high & 0x7fff, source.high & 0x7fff);
}

#endif
