/*+===================================================================
  File:      math_wide.c

  Summary:   Implements Math64Lib and classic FixMath wide arithmetic.

  Classes:   None.

  Functions: ROMlib_carbon_math_wide, ROMlib_carbon_fixmath_wide.

  Origin:    Math64Lib from Mac OS 9.2.1 and Universal Interfaces 3.4.2 Math64.h and FixMath.h.
             Inside Macintosh: QuickDraw GX Environment and Utilities, pages 8-49 to 8-54.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "math_wide.h"

static const uint64_t sign_bit = UINT64_C(1) << 63;

static uint64_t wide_pair(const uint32_t* words)
{
    return (uint64_t)words[0] << 32 | words[1];
}

static void store_wide(unsigned char* output, uint64_t value)
{
    for (unsigned index = 8; index; --index) {
        output[index - 1] = (unsigned char)value;
        value >>= 8;
    }
}

static uint64_t load_wide(const unsigned char* input)
{
    uint64_t value = 0;
    for (unsigned index = 0; index < 8; ++index)
        value = value << 8 | input[index];
    return value;
}

static int compare(uint64_t left, uint64_t right)
{
    /* The reference returns the signed comparison field of the PPC CR. */
    return left < right ? -2 : left > right;
}

static void divide(unsigned char* result, unsigned char* remainder, uint64_t left, uint64_t right, int is_signed)
{
    int left_negative = is_signed && (left & sign_bit);
    int right_negative = is_signed && (right & sign_bit);
    uint64_t quotient, residue;
    if (!right) {
        quotient = is_signed ? (left_negative ? sign_bit : sign_bit - 1) : UINT64_MAX;
        residue = left;
    }
    else {
        uint64_t dividend = left_negative ? -left : left;
        uint64_t divisor = right_negative ? -right : right;
        quotient = dividend / divisor;
        residue = dividend % divisor;
        if (left_negative != right_negative)
            quotient = -quotient;
        if (left_negative)
            residue = -residue;
    }
    store_wide(result, quotient);
    if (remainder)
        store_wide(remainder, residue);
}

static uint64_t signed_right(uint64_t value, unsigned count)
{
    uint32_t high = (uint32_t)(value >> 32);
    uint32_t sign = (value & sign_bit) ? UINT32_MAX : 0;
    unsigned shift = count & 31;
    uint32_t high_shifted = (high ^ sign) >> shift ^ sign;
    if (count & 64)
        return (uint64_t)sign << 32 | sign;
    if (count & 32)
        return (uint64_t)sign << 32 | high_shifted;
    return (uint64_t)high_shifted << 32 | (uint32_t)(value >> shift);
}

static uint64_t fixmath_shift(uint64_t value, int32_t count, int rounding)
{
    if (!count)
        return value;
    if (count < 0)
        return count > -64 ? value << -count : 0;
    if (count >= 64)
        return rounding ? 0 : ((value & sign_bit) ? UINT64_MAX : 0);
    return signed_right(value, (unsigned)count) + (rounding ? (value >> (count - 1) & 1) : 0);
}

static uint32_t fixmath_square_root(uint64_t value)
{
    uint64_t root = 0;
    for (uint64_t bit = UINT64_C(1) << 31; bit; bit >>= 1) {
        uint64_t trial = root | bit;
        if (trial * trial <= value)
            root = trial;
    }
    return (uint32_t)root;
}

static int fixmath_divide(ROMlib_carbon_ppc_frame* frame, unsigned char* output, uint64_t value, int wide_result)
{
    int64_t divisor = (int32_t)frame->words[1];
    uint32_t remainder_address = frame->words[2];
    int rounding = !remainder_address || remainder_address == UINT32_MAX;
    unsigned char* remainder = rounding ? NULL : ROMlib_carbon_guest_memory(remainder_address, 4, 1);
    if (!rounding && !remainder)
        return ROMlib_carbon_report_failure("Invalid FixMath remainder address");

    int dividend_negative = (value & sign_bit) != 0;
    int negative = dividend_negative != (divisor < 0);
    uint64_t magnitude = dividend_negative ? -value : value;
    uint64_t denominator = divisor < 0 ? -divisor : divisor;
    uint64_t quotient = denominator ? magnitude / denominator : UINT64_MAX;
    uint64_t residue = denominator ? magnitude % denominator : 0;
    if (rounding && denominator && residue * 2 >= denominator)
        ++quotient;
    uint64_t limit = negative ? (UINT64_C(1) << 31) : INT32_MAX;
    if (wide_result)
        limit = negative ? sign_bit : INT64_MAX;
    int overflow = quotient > limit;
    if (overflow)
        quotient = limit;
    uint64_t result = negative ? -quotient : quotient;
    if (wide_result)
        store_wide(output, result);
    else
        frame->words[0] = overflow && remainder_address == UINT32_MAX ? (uint32_t)INT32_MIN : (uint32_t)result;
    if (remainder) {
        uint32_t residue_word = overflow ? (uint32_t)INT32_MIN : (uint32_t)(dividend_negative ? -residue : residue);
        for (unsigned index = 4; index; --index) {
            remainder[index - 1] = (unsigned char)residue_word;
            residue_word >>= 8;
        }
    }
    return 1;
}

int ROMlib_carbon_fixmath_wide(ROMlib_carbon_ppc_frame* frame, carbon_fixmath_wide_operation operation)
{
    const uint32_t* words = frame->words;
    uint32_t target = operation == fixmath_multiply ? words[2] : words[0];
    int writes_target = operation != fixmath_compare && operation != fixmath_root && operation != fixmath_divide_long;
    unsigned char* output = target ? ROMlib_carbon_guest_memory(target, 8, writes_target) : NULL;
    if (!output)
        return ROMlib_carbon_report_failure("Invalid FixMath wide address");
    if (operation == fixmath_multiply) {
        int64_t product = (int64_t)(int32_t)words[0] * (int32_t)words[1];
        store_wide(output, (uint64_t)product);
        frame->words[0] = target;
        return 1;
    }

    uint64_t value = load_wide(output);
    switch (operation) {
    case fixmath_add:
    case fixmath_subtract:
    case fixmath_compare: {
        const unsigned char* source = words[1] ? ROMlib_carbon_guest_memory(words[1], 8, 0) : NULL;
        if (!source)
            return ROMlib_carbon_report_failure("Invalid FixMath source address");
        uint64_t other = load_wide(source);
        if (operation == fixmath_compare) {
            frame->words[0] = (uint32_t)((int64_t)value < (int64_t)other ? -1 : value != other);
            return 1;
        }
        value = operation == fixmath_add ? value + other : value - other;
        break;
    }
    case fixmath_negate:
        value = -value;
        break;
    case fixmath_shift_rounded:
    case fixmath_shift_bits:
        value = fixmath_shift(value, (int32_t)words[1], operation == fixmath_shift_rounded);
        break;
    case fixmath_root:
        frame->words[0] = fixmath_square_root(value);
        return 1;
    case fixmath_divide_long:
    case fixmath_divide_wide:
        return fixmath_divide(frame, output, value, operation == fixmath_divide_wide);
    default:
        return 0;
    }
    store_wide(output, value);
    return 1;
}

int ROMlib_carbon_math_wide(ROMlib_carbon_ppc_frame* frame, carbon_wide_operation operation)
{
    const uint32_t* words = frame->words;
    uint64_t left = wide_pair(words);
    uint64_t right = wide_pair(words + 2);
    switch (operation) {
    case wide_narrow:
        frame->words[0] = words[1];
        return 1;
    case wide_signed_compare:
        frame->words[0] = compare(left ^ sign_bit, right ^ sign_bit);
        return 1;
    case wide_unsigned_compare:
        frame->words[0] = compare(left, right);
        return 1;
    case wide_logical_and:
        frame->words[0] = left && right;
        return 1;
    case wide_logical_or:
        frame->words[0] = left || right;
        return 1;
    case wide_logical_xor:
        frame->words[0] = (left != 0) != (right != 0);
        return 1;
    case wide_logical_not:
        frame->words[0] = !left;
        return 1;
    case wide_signed_to_float:
    case wide_unsigned_to_float:
    case wide_float_to_signed:
    case wide_float_to_unsigned:
        return ROMlib_carbon_math_wide_float(frame, operation);
    default:
        break;
    }

    /* Math64Lib was compiled for an aggregate-based 64-bit type. A hidden
       result address precedes all explicit integer input words. */
    unsigned char* output = words[0] ? ROMlib_carbon_guest_memory(words[0], 8, 1) : NULL;
    if (!output)
        return ROMlib_carbon_report_failure("Invalid Math64Lib result address");
    left = wide_pair(words + 1);
    right = wide_pair(words + 3);
    unsigned shift = words[3] & 127;
    uint64_t result;
    switch (operation) {
    case wide_unsigned_max:
        result = UINT64_MAX;
        break;
    case wide_signed_max:
        result = sign_bit - 1;
        break;
    case wide_signed_min:
        result = sign_bit;
        break;
    case wide_add:
        result = left + right;
        break;
    case wide_subtract:
        result = left - right;
        break;
    case wide_multiply:
        result = left * right;
        break;
    case wide_negate:
        result = -left;
        break;
    case wide_absolute:
        result = left & sign_bit ? -left : left;
        break;
    case wide_signed_divide:
    case wide_unsigned_divide: {
        unsigned char* remainder = words[5] ? ROMlib_carbon_guest_memory(words[5], 8, 1) : NULL;
        if (words[5] && !remainder)
            return 0;
        divide(output, remainder, left, right, operation == wide_signed_divide);
        return 1;
    }
    case wide_sign_extend:
        result = (uint64_t)(int64_t)(int32_t)words[1];
        break;
    case wide_zero_extend:
        result = words[1];
        break;
    case wide_bitwise_and:
        result = left & right;
        break;
    case wide_bitwise_or:
        result = left | right;
        break;
    case wide_bitwise_xor:
        result = left ^ right;
        break;
    case wide_bitwise_not:
        result = ~left;
        break;
    case wide_signed_right:
        result = signed_right(left, shift);
        break;
    case wide_unsigned_right:
        result = shift < 64 ? left >> shift : 0;
        break;
    case wide_left:
        result = shift < 64 ? left << shift : 0;
        break;
    case wide_reinterpret:
        result = left;
        break;
    default:
        return ROMlib_carbon_report_failure("Invalid Math64Lib operation");
    }
    store_wide(output, result);
    return 1;
}
