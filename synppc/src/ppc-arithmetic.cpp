/*+===================================================================
  File:      ppc-arithmetic.cpp

  Summary:   Implements ppc arithmetic support for synppc PowerPC execution and fragment loading.

  Classes:   Declared: softfloat_scope; Used: ppc_fp_result, ppc_fused_result, ppc_integer_result, softfloat_scope

  Functions: romaw_ppc_arithmetic, romaw_ppc_arithmetic_exceptions, romaw_ppc_arithmetic_value,
             romaw_ppc_compare_value, romaw_ppc_estimate_value, romaw_ppc_expand_single,
             romaw_ppc_fused_value, romaw_ppc_integer, romaw_ppc_integer_value, romaw_ppc_record_fpscr

  Origin:    Existing repository source in synppc/src; original attribution and provenance comments are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ppc-arithmetic.h"
#include "ppc-fused.h"
/* Match the C library's public extF80 struct layout and value API. */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define LITTLEENDIAN 1
#endif
#define SOFTFLOAT_FAST_INT64
extern "C" {
#include "../third_party/softfloat3/source/include/softfloat.h"
}

namespace {
struct softfloat_scope {
    uint_fast8_t rounding = softfloat_roundingMode;
    uint_fast8_t tininess = softfloat_detectTininess;
    uint_fast8_t flags = softfloat_exceptionFlags;
    uint_fast8_t precision = extF80_roundingPrecision;

    explicit softfloat_scope(unsigned mode)
    {
        static const uint_fast8_t modes[] = {softfloat_round_near_even, softfloat_round_minMag, softfloat_round_max,
                                             softfloat_round_min};
        softfloat_roundingMode = modes[mode & 3];
        softfloat_detectTininess = softfloat_tininess_beforeRounding;
        softfloat_exceptionFlags = 0;
    }

    ~softfloat_scope()
    {
        softfloat_roundingMode = rounding;
        softfloat_detectTininess = tininess;
        softfloat_exceptionFlags = flags;
        extF80_roundingPrecision = precision;
    }
};

bool is_nan(uint64_t bits, bool single)
{
    uint64_t exponent = single ? UINT64_C(0x7f800000) : UINT64_C(0x7ff0000000000000);
    uint64_t fraction = single ? UINT64_C(0x007fffff) : UINT64_C(0x000fffffffffffff);
    return (bits & exponent) == exponent && (bits & fraction);
}

uint64_t arithmetic_operand(uint32_t fpscr, bool single, uint64_t bits)
{
    // MPC750UM table 2-20 zeros denormal operands in NI mode. Preserve sign;
    // normal operands, NaNs and infinities retain their complete encoding.
    uint64_t magnitude = bits & UINT64_C(0x7fffffffffffffff);
    uint64_t minimum = single ? UINT64_C(0x3810000000000000) : UINT64_C(0x0010000000000000);
    return (fpscr & 4) && magnitude < minimum ? bits & UINT64_C(0x8000000000000000) : bits;
}

uint32_t record_exceptions(uint32_t status, uint32_t raised)
{
    if (raised & ~status)
        status |= 0x80000000; // FX is set only for a new sticky cause.
    status = (status | raised) & ~0x60000000u;
    if (status & 0x01f80700)
        status |= 0x20000000; // VX is the summary of invalid-operation causes.
    if (((status >> 25) & (status >> 3) & 0x1f) != 0)
        status |= 0x40000000; // FEX includes all enabled exception causes.
    return status;
}
}

ppc_fp_result romaw_ppc_arithmetic(ppc_fp_operation operation, bool single, unsigned rounding, uint64_t a, uint64_t b)
{
    softfloat_scope scope(rounding);
    uint64_t bits;
    if (operation == ppc_fp_round_single)
        bits = f64_to_f32(float64_t{a}).v;
    else if (single) {
        float32_t first = {(uint32_t)a}, second = {(uint32_t)b};
        switch (operation) {
        case ppc_fp_add:
            bits = f32_add(first, second).v;
            break;
        case ppc_fp_subtract:
            bits = f32_sub(first, second).v;
            break;
        case ppc_fp_multiply:
            bits = f32_mul(first, second).v;
            break;
        case ppc_fp_divide:
            bits = f32_div(first, second).v;
            break;
        case ppc_fp_sqrt:
            bits = f32_sqrt(first).v;
            break;
        default:
            return {0, softfloat_flag_invalid};
        }
    }
    else {
        float64_t first = {a}, second = {b};
        switch (operation) {
        case ppc_fp_add:
            bits = f64_add(first, second).v;
            break;
        case ppc_fp_subtract:
            bits = f64_sub(first, second).v;
            break;
        case ppc_fp_multiply:
            bits = f64_mul(first, second).v;
            break;
        case ppc_fp_divide:
            bits = f64_div(first, second).v;
            break;
        case ppc_fp_sqrt:
            bits = f64_sqrt(first).v;
            break;
        default:
            return {0, softfloat_flag_invalid};
        }
    }
    /* PowerPC prefers FRA over FRB even if only FRB is signaling. Keep
     SoftFloat's exception result, but apply the architecture's NaN choice. */
    if (operation != ppc_fp_round_single) {
        uint64_t quiet = single ? UINT64_C(0x00400000) : UINT64_C(0x0008000000000000);
        if (is_nan(a, single))
            bits = a | quiet;
        else if (operation != ppc_fp_sqrt && is_nan(b, single))
            bits = b | quiet;
    }
    return {bits, (unsigned)softfloat_exceptionFlags};
}

uint64_t romaw_ppc_expand_single(uint32_t value)
{
    softfloat_scope scope(0);
    return f32_to_f64(float32_t{value}).v;
}

ppc_integer_result romaw_ppc_integer(uint64_t value, unsigned rounding)
{
    softfloat_scope scope(rounding);
    int32_t result = f64_to_i32(float64_t{value}, softfloat_roundingMode, true);
    /* The selected SoftFloat specialization returns zero for NaNs. PowerPC's
     integer-indefinite value is INT32_MIN, for either sign and NaN class. */
    if (is_nan(value, false))
        result = INT32_MIN;
    bool incremented = false;
    if (!(softfloat_exceptionFlags & softfloat_flag_invalid) && (softfloat_exceptionFlags & softfloat_flag_inexact)) {
        int exponent = int((value >> 52) & 0x7ff) - 1023;
        uint64_t truncated =
            exponent < 0 ? 0
                         : ((value & UINT64_C(0x000fffffffffffff)) | UINT64_C(0x0010000000000000)) >> (52 - exponent);
        uint64_t magnitude = result < 0 ? -int64_t(result) : result;
        incremented = magnitude > truncated;
    }
    return {uint32_t(result), unsigned(softfloat_exceptionFlags), incremented};
}

bool romaw_ppc_integer_value(uint64_t value, unsigned rounding, uint32_t* fpscr, uint64_t* output)
{
    ppc_integer_result result = romaw_ppc_integer(value, rounding);
    const uint32_t xx = 0x02000000, vxsnan = 0x01000000, vxcvi = 0x00000100;
    const uint32_t fr = 0x00040000, fi = 0x00020000, ve = 0x80;
    uint32_t raised = 0;
    if (result.flags & softfloat_flag_invalid) {
        raised = vxcvi;
        if (is_nan(value, false) && !(value & UINT64_C(0x0008000000000000)))
            raised |= vxsnan;
    }
    else if (result.flags & softfloat_flag_inexact)
        raised = xx;
    uint32_t status = record_exceptions(*fpscr & ~(fr | fi), raised);
    if (!(result.flags & softfloat_flag_invalid)) {
        if (result.flags & softfloat_flag_inexact)
            status |= fi;
        if (result.incremented)
            status |= fr;
    }
    *fpscr = status;
    if ((result.flags & softfloat_flag_invalid) && (status & ve))
        return false;
    /* The high word is architecturally undefined. Use zero without routing the
     integer payload through a host floating register or conversion. */
    *output = result.bits;
    return true;
}

unsigned romaw_ppc_compare_value(uint64_t a, uint64_t b, bool ordered, uint32_t* fpscr)
{
    unsigned condition;
    uint32_t raised = 0;
    if (is_nan(a, false) || is_nan(b, false)) {
        condition = 1;
        bool signaling = (is_nan(a, false) && !(a & UINT64_C(0x0008000000000000))) ||
                         (is_nan(b, false) && !(b & UINT64_C(0x0008000000000000)));
        if (signaling)
            raised |= 0x01000000; // VXSNAN, for either comparison form.
        if (ordered && (!signaling || !(*fpscr & 0x80)))
            raised |= 0x00080000; // VXVC; enabled SNaNs take precedence.
    }
    else if (a == b || (!(a << 1) && !(b << 1)))
        condition = 2;
    else {
        bool less = (a >> 63) != (b >> 63) ? bool(a >> 63) : ((a < b) != bool(a >> 63));
        condition = less ? 8 : 4;
    }
    *fpscr = (record_exceptions(*fpscr, raised) & ~0x0000f000u) | (condition << 12);
    return condition;
}

uint32_t romaw_ppc_record_fpscr(uint32_t status, uint32_t raised)
{
    if (raised & ~status & 0x1ff80700u)
        status |= 0x80000000u;
    return record_exceptions(status | (raised & ~0x60000000u), 0);
}

bool romaw_ppc_arithmetic_exceptions(uint32_t opcode, uint64_t a, uint64_t b, uint64_t c, uint32_t* fpscr)
{
    bool single = opcode >> 26 == 59;
    a = arithmetic_operand(*fpscr, single, a);
    b = arithmetic_operand(*fpscr, single, b);
    c = arithmetic_operand(*fpscr, single, c);
    unsigned operation = (opcode >> 1) & 31;
    if (operation == 25)
        b = c;
    else if (operation == 22 || operation == 12) {
        a = b;
        b = 0;
    }
    const uint64_t quiet = UINT64_C(0x0008000000000000);
    const uint64_t magnitude = UINT64_C(0x7fffffffffffffff);
    const uint64_t infinity = UINT64_C(0x7ff0000000000000);
    bool nan_a = is_nan(a, false), nan_b = is_nan(b, false);
    uint32_t raised = 0;
    if ((nan_a && !(a & quiet)) || (nan_b && !(b & quiet)))
        raised = 0x01000000; // VXSNAN, before any host or binary32 conversion.
    if (operation >= 28) {
        bool nan_c = is_nan(c, false);
        if (nan_c && !(c & quiet))
            raised |= 0x01000000;
        bool infinite_a = (a & magnitude) == infinity;
        bool infinite_c = (c & magnitude) == infinity;
        /* PEM Appendix D.2: invalid causes apply to the multiply and add
         separately. An addend NaN does not suppress infinity times zero. */
        if ((infinite_a && !(c & magnitude)) || (infinite_c && !(a & magnitude)))
            raised |= 0x00100000;
        else if (!nan_a && !nan_b && !nan_c && (infinite_a || infinite_c) && (b & magnitude) == infinity &&
                 bool((a ^ b ^ c) >> 63) == bool(operation & 1))
            raised |= 0x00800000;
    }
    else if (!nan_a && !nan_b) {
        bool infinite_a = (a & magnitude) == infinity;
        bool infinite_b = (b & magnitude) == infinity;
        bool zero_a = !(a & magnitude), zero_b = !(b & magnitude);
        switch (operation) {
        case 20:
        case 21:
            if (infinite_a && infinite_b && bool((a ^ b) >> 63) == (operation == 21))
                raised |= 0x00800000; // VXISI.
            break;
        case 18:
            if (infinite_a && infinite_b)
                raised |= 0x00400000; // VXIDI.
            else if (zero_a && zero_b)
                raised |= 0x00200000; // VXZDZ.
            else if (!infinite_a && !zero_a && zero_b)
                raised |= 0x04000000; // ZX; infinity/zero is not a zero divide.
            break;
        case 25:
            if ((infinite_a && zero_b) || (zero_a && infinite_b))
                raised |= 0x00100000; // VXIMZ.
            break;
        case 22:
            if ((a >> 63) && !zero_a)
                raised |= 0x00000200; // VXSQRT, including negative infinity.
            break;
        }
    }
    uint32_t status = *fpscr;
    if (raised)
        status &= ~0x00060000u; // FR/FI clear for invalid and zero divide.
    status = record_exceptions(status, raised);
    *fpscr = status;
    return !((raised & ~0x04000000u) && (status & 0x80)) && !((raised & 0x04000000u) && (status & 0x10));
}

namespace {
extFloat80_t wide_operation(ppc_fp_operation operation, uint64_t a, uint64_t b)
{
    extFloat80_t first = f64_to_extF80(float64_t{a});
    extFloat80_t second = f64_to_extF80(float64_t{b});
    switch (operation) {
    case ppc_fp_add:
        return extF80_add(first, second);
    case ppc_fp_subtract:
        return extF80_sub(first, second);
    case ppc_fp_multiply:
        return extF80_mul(first, second);
    case ppc_fp_divide:
        return extF80_div(first, second);
    case ppc_fp_sqrt:
        return extF80_sqrt(first);
    case ppc_fp_round_single:
        return extF80_mul(first, f64_to_extF80(float64_t{UINT64_C(0x3ff0000000000000)}));
    }
    return {};
}

uint32_t classify_result(uint64_t bits, bool single, bool scaled)
{
    uint64_t magnitude = bits & UINT64_C(0x7fffffffffffffff);
    bool negative = bits >> 63;
    if (magnitude > UINT64_C(0x7ff0000000000000))
        return 0x11000;
    if (magnitude == UINT64_C(0x7ff0000000000000))
        return negative ? 0x9000 : 0x5000;
    if (!magnitude)
        return negative ? 0x12000 : 0x2000;
    uint64_t minimum = single && !scaled ? UINT64_C(0x3810000000000000) : UINT64_C(0x0010000000000000);
    return (negative ? 0x8000 : 0x4000) | (magnitude < minimum ? 0x10000 : 0);
}

bool flush_non_ieee_result(uint32_t fpscr, bool single, bool scaled, uint64_t& bits, uint32_t& raised,
                           bool report_inexact)
{
    // MPC750UM 2.2.4 and tables 2-20/2-21: NI does not return denormals.
    // UE-enabled scaled results retain the existing range-exception path.
    uint64_t magnitude = bits & UINT64_C(0x7fffffffffffffff);
    uint64_t minimum = single ? UINT64_C(0x3810000000000000) : UINT64_C(0x0010000000000000);
    if (!(fpscr & 4) || (fpscr & 0x20) || scaled || !((raised & 0x08000000) || (magnitude && magnitude < minimum)))
        return false;
    // Tininess is before rounding: even a tiny value rounded up to min-normal
    // is flushed. Ordinary arithmetic raises UX/XX; estimates do not alter XX.
    bits &= UINT64_C(0x8000000000000000);
    raised |= 0x08000000 | (report_inexact ? 0x02000000 : 0);
    return true;
}
}

/* 0 = unsupported, 1 = commit, 2 = enabled-invalid/zero-divide inhibition. */
int romaw_ppc_arithmetic_value(uint32_t opcode, uint64_t a, uint64_t b, uint64_t c, uint32_t* fpscr, uint64_t* output)
{
    unsigned primary = opcode >> 26;
    if (primary != 59 && primary != 63)
        return 0;
    ppc_fp_operation operation;
    bool single = primary == 59;
    if (primary == 63 && ((opcode >> 1) & 1023) == 12) {
        operation = ppc_fp_round_single;
        single = true;
    }
    else
        switch ((opcode >> 1) & 31) {
        case 18:
            operation = ppc_fp_divide;
            break;
        case 20:
            operation = ppc_fp_subtract;
            break;
        case 21:
            operation = ppc_fp_add;
            break;
        case 22:
            operation = ppc_fp_sqrt;
            break;
        case 25:
            operation = ppc_fp_multiply;
            break;
        default:
            return 0;
        }
    // FRSP's source is binary64 even though its result is binary32.
    a = arithmetic_operand(*fpscr, single && operation != ppc_fp_round_single, a);
    b = arithmetic_operand(*fpscr, single && operation != ppc_fp_round_single, b);
    c = arithmetic_operand(*fpscr, single && operation != ppc_fp_round_single, c);
    if (!romaw_ppc_arithmetic_exceptions(opcode, a, b, c, fpscr))
        return 2;
    if (operation == ppc_fp_round_single || operation == ppc_fp_sqrt) {
        a = b;
        b = 0;
    }
    else if (operation == ppc_fp_multiply)
        b = c;
    /* PEM 3.3.1 preserves the selected full register payload for arithmetic;
     only FRSP clears the low 29 bits. Do this before binary32 conversion. */
    const uint64_t operands[] = {a, b};
    for (uint64_t operand : operands)
        if (is_nan(operand, false)) {
            *output = operand | UINT64_C(0x0008000000000000);
            if (operation == ppc_fp_round_single)
                *output &= ~UINT64_C(0x1fffffff);
            *fpscr = record_exceptions(*fpscr & ~0x0007f000u, 0) | 0x11000;
            return 1;
        }
    unsigned rounding = *fpscr & 3;
    uint64_t first = a, second = b;
    /* Classic compilers use FSUBS to subtract a common binary64 bias after
     integer conversion. Equal finite sign/exponent fields guarantee that
     this cancellation is exact in binary64. Narrow only the result. */
    bool exact_cancellation =
        single && operation == ppc_fp_subtract && (a >> 52) == (b >> 52) && ((a >> 52) & 0x7ff) != 0x7ff;
    if (single && operation != ppc_fp_round_single && !exact_cancellation) {
        first = romaw_ppc_arithmetic(ppc_fp_round_single, true, rounding, a, 0).bits;
        second = romaw_ppc_arithmetic(ppc_fp_round_single, true, rounding, b, 0).bits;
    }
    auto evaluate = [&](unsigned mode) -> ppc_fp_result {
        if (exact_cancellation) {
            softfloat_scope scope(mode);
            float64_t difference = f64_sub(float64_t{a}, float64_t{b});
            uint32_t narrowed = f64_to_f32(difference).v;
            return {narrowed, softfloat_exceptionFlags};
        }
        return romaw_ppc_arithmetic(operation, single, mode, first, second);
    };
    ppc_fp_result result = evaluate(rounding);
    uint64_t bits = single ? romaw_ppc_expand_single(result.bits) : result.bits;
    uint32_t raised = 0;
    bool scaled = false, incremented = false;
    if (!(result.flags & (softfloat_flag_invalid | softfloat_flag_infinite))) {
        if (result.flags & softfloat_flag_overflow)
            raised |= 0x10000000;
        if (result.flags & softfloat_flag_underflow)
            raised |= 0x08000000;
        if (result.flags & softfloat_flag_inexact)
            raised |= 0x02000000;
        bool finite = ((a >> 52) & 0x7ff) != 0x7ff && ((b >> 52) & 0x7ff) != 0x7ff;
        if (finite && (raised & 0x02000000) && rounding != 1) {
            ppc_fp_result trunc = evaluate(1);
            uint64_t mask = single ? UINT64_C(0x7fffffff) : UINT64_C(0x7fffffffffffffff);
            incremented = (result.bits & mask) > (trunc.bits & mask);
        }
        uint64_t minimum = single ? UINT64_C(0x3810000000000000) : UINT64_C(0x0010000000000000);
        bool range_candidate = ((raised & 0x10000000) && (*fpscr & 0x40)) ||
                               ((*fpscr & 0x20) && (bits & UINT64_C(0x7fffffffffffffff)) <= minimum);
        // The normal path already rounded the sign of an exact zero sum.
        // Skip extended-precision normalization, which shifts a zero significand by 64.
        bool zero_sum = (operation == ppc_fp_add || operation == ppc_fp_subtract) && !((a | b) << 1);
        if (finite && range_candidate && !zero_sum) {
            softfloat_scope scope(rounding);
            extF80_roundingPrecision = single ? 32 : 64;
            extFloat80_t rounded = wide_operation(operation, a, b);
            unsigned wide_flags = softfloat_exceptionFlags;
            softfloat_roundingMode = softfloat_round_minMag;
            softfloat_exceptionFlags = 0;
            extFloat80_t truncated = wide_operation(operation, a, b);
            bool tiny = truncated.signif && (truncated.signExp & 0x7fff) < unsigned(16383 + (single ? -126 : -1022));
            bool wide_incremented =
                (rounded.signExp & 0x7fff) > (truncated.signExp & 0x7fff) ||
                ((rounded.signExp & 0x7fff) == (truncated.signExp & 0x7fff) && rounded.signif > truncated.signif);
            if ((result.flags & softfloat_flag_overflow) && (*fpscr & 0x40)) {
                rounded.signExp -= single ? 192 : 1536;
                scaled = true;
            }
            else if (tiny && (*fpscr & 0x20)) {
                rounded.signExp += single ? 192 : 1536;
                scaled = true;
                raised |= 0x08000000;
            }
            if (scaled) {
                raised &= ~0x02000000u;
                if (wide_flags & softfloat_flag_inexact)
                    raised |= 0x02000000;
                incremented = wide_incremented;
                bits = extF80_to_f64(rounded).v;
            }
        }
    }
    if (flush_non_ieee_result(*fpscr, single, scaled, bits, raised, true))
        incremented = false;
    uint32_t status = *fpscr & ~0x0007f000u;
    if (raised & 0x02000000)
        status |= 0x00020000;
    if (incremented)
        status |= 0x00040000;
    status = record_exceptions(status, raised);
    status |= classify_result(bits, single, scaled);
    *fpscr = status;
    *output = bits;
    return 1;
}

int romaw_ppc_fused_value(uint32_t opcode, uint64_t a, uint64_t b, uint64_t c, uint32_t* fpscr, uint64_t* output)
{
    unsigned operation = (opcode >> 1) & 31;
    if ((opcode >> 26 != 59 && opcode >> 26 != 63) || operation < 28)
        return 0;
    a = arithmetic_operand(*fpscr, opcode >> 26 == 59, a);
    b = arithmetic_operand(*fpscr, opcode >> 26 == 59, b);
    c = arithmetic_operand(*fpscr, opcode >> 26 == 59, c);
    if (!romaw_ppc_arithmetic_exceptions(opcode, a, b, c, fpscr))
        return 2;
    const uint64_t operands[] = {a, b, c};
    for (uint64_t operand : operands)
        if (is_nan(operand, false)) {
            *output = operand | UINT64_C(0x0008000000000000);
            *fpscr = record_exceptions(*fpscr & ~0x0007f000u, 0) | 0x11000;
            return 1;
        }
    if (!(operation & 1))
        b ^= UINT64_C(0x8000000000000000);
    unsigned rounding = *fpscr & 3;
    bool single = opcode >> 26 == 59;
    if (single) {
        a = romaw_ppc_arithmetic(ppc_fp_round_single, true, rounding, a, 0).bits;
        b = romaw_ppc_arithmetic(ppc_fp_round_single, true, rounding, b, 0).bits;
        c = romaw_ppc_arithmetic(ppc_fp_round_single, true, rounding, c, 0).bits;
    }
    ppc_fused_result result = romaw_ppc_fused_status(single, rounding, a, c, b, *fpscr);
    uint64_t bits = single ? romaw_ppc_expand_single(result.bits) : result.bits;
    if (operation >= 30 && !is_nan(bits, false))
        bits ^= UINT64_C(0x8000000000000000);
    uint32_t raised =
        (result.flags & 1 ? 0x02000000 : 0) | (result.flags & 2 ? 0x08000000 : 0) | (result.flags & 4 ? 0x10000000 : 0);
    bool incremented = result.incremented;
    bool scaled = ((raised & 0x08000000) && (*fpscr & 0x20)) || ((raised & 0x10000000) && (*fpscr & 0x40));
    if (flush_non_ieee_result(*fpscr, single, scaled, bits, raised, true))
        incremented = false;
    uint32_t status = *fpscr & ~0x0007f000u;
    if (raised & 0x02000000)
        status |= 0x20000;
    if (incremented)
        status |= 0x40000;
    *fpscr = record_exceptions(status, raised) | classify_result(bits, single, false);
    *output = bits;
    return 1;
}

int romaw_ppc_estimate_value(uint32_t opcode, uint64_t operand, uint32_t* fpscr, uint64_t* output)
{
    bool reciprocal = opcode >> 26 == 59 && ((opcode >> 1) & 31) == 24;
    if (!reciprocal && (opcode >> 26 != 63 || ((opcode >> 1) & 31) != 26))
        return 0;
    // Estimate source operands are binary64, including FRES's single result.
    operand = arithmetic_operand(*fpscr, false, operand);
    const uint64_t sign = UINT64_C(0x8000000000000000);
    uint64_t magnitude = operand & ~sign, result;
    uint32_t raised = 0;
    bool scaled = false;
    if (is_nan(operand, false)) {
        if (!(operand & UINT64_C(0x0008000000000000)))
            raised = 0x01000000; // VXSNAN
        result = operand | UINT64_C(0x0008000000000000);
    }
    else if (!magnitude) {
        raised = 0x04000000; // ZX, including negative zero.
        result = (operand & sign) | UINT64_C(0x7ff0000000000000);
    }
    else if (!reciprocal && (operand & sign)) {
        raised = 0x00000200; // VXSQRT, including negative infinity.
        result = UINT64_C(0x7ff8000000000000);
    }
    else if (magnitude == UINT64_C(0x7ff0000000000000))
        result = reciprocal ? operand & sign : 0;
    else if (reciprocal) {
        /* Evaluate in an extended exponent range before converting to binary32.
         This also preserves finite scaled OE/UE results for binary64 inputs
         whose reciprocals overflow or underflow binary64. Estimate instructions
         do not raise XX; FR/FI are undefined and consistently cleared here. */
        softfloat_scope scope(*fpscr & 3);
        extFloat80_t one = f64_to_extF80(float64_t{UINT64_C(0x3ff0000000000000)});
        extFloat80_t source = f64_to_extF80(float64_t{operand});
        extF80_roundingPrecision = 80;
        extFloat80_t wide = extF80_div(one, source);
        float32_t normal = extF80_to_f32(wide);
        unsigned flags = softfloat_exceptionFlags;
        bool tiny = (wide.signExp & 0x7fff) < 16383 - 126;
        if (flags & softfloat_flag_overflow)
            raised |= 0x10000000;
        if ((flags & softfloat_flag_underflow) || (tiny && (*fpscr & 0x20)))
            raised |= 0x08000000;
        if (((raised & 0x10000000) && (*fpscr & 0x40)) || (tiny && (*fpscr & 0x20))) {
            extF80_roundingPrecision = 32;
            extFloat80_t rounded = extF80_div(one, source);
            rounded.signExp += raised & 0x10000000 ? -192 : 192;
            result = extF80_to_f64(rounded).v;
            scaled = true;
        }
        else
            result = romaw_ppc_expand_single(normal.v);
    }
    else {
        /* PEM frsqrte permits an implementation-dependent double estimate with
         relative error <= 1/32. Two software binary64 operations provide a
         substantially tighter bound across every finite positive exponent.
         Neither intermediate can overflow or underflow. RN does not specify
         estimate bits; select deterministic nearest-even without host fenv.
         FR/FI are undefined; no OX/UX/XX cause is raised by this instruction. */
        softfloat_scope scope(0);
        float64_t root = f64_sqrt(float64_t{operand});
        result = f64_div(float64_t{UINT64_C(0x3ff0000000000000)}, root).v;
    }
    if (reciprocal)
        flush_non_ieee_result(*fpscr, true, scaled, result, raised, false);
    uint32_t status = record_exceptions(*fpscr & ~0x00060000u, raised);
    *fpscr = status;
    if ((raised == 0x04000000 && (status & 0x10)) || ((raised & 0x01000200) && (status & 0x80)))
        return 2;
    *fpscr = (status & ~0x0001f000u) | classify_result(result, reciprocal, scaled);
    *output = result;
    return 1;
}
