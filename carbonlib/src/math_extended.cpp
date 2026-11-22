/*+===================================================================
  File:      math_extended.cpp

  Summary:   Implement MathLib double-double arithmetic and packed extended80 conversion.

  Classes:   MathSoftFloat.

  Functions: ROMlib_carbon_math_dd_binary, ROMlib_carbon_math_dd_remquo, ROMlib_carbon_math_dd_unary,
             ROMlib_carbon_math_extended, ROMlib_carbon_math_quad_binary, ROMlib_carbon_math_quad_unary,
             ROMlib_carbon_math_wide_float, ROMlib_carbon_fract_to_double, ROMlib_carbon_fixed_to_double,
             ROMlib_carbon_double_to_fixed, ROMlib_carbon_double_to_fract, ROMlib_carbon_fract_cosine,
             ROMlib_carbon_fract_sine, ROMlib_carbon_fract_square_root, ROMlib_carbon_fixed_atan2.

  Origin:    Original native code using the local PowerPC MathLib ABI and Executor's SoftFloat library.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "math_native.h"
#include "math_wide.h"
#include "synppc/src/ppc-arithmetic.h"
#include <cstring>

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define LITTLEENDIAN 1
#endif
#define SOFTFLOAT_FAST_INT64
extern "C" {
#include "synppc/third_party/softfloat3/source/include/softfloat.h"
}

namespace {
constexpr uint64_t sign_bit = UINT64_C(0x8000000000000000);
constexpr uint64_t infinity = UINT64_C(0x7ff0000000000000);
constexpr unsigned high_word = __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ ? 1 : 0;

struct MathSoftFloat {
    uint_fast8_t rounding = softfloat_roundingMode;
    uint_fast8_t tininess = softfloat_detectTininess;
    uint_fast8_t flags = softfloat_exceptionFlags;
    uint_fast8_t precision = extF80_roundingPrecision;

    explicit MathSoftFloat(unsigned fpscr)
    {
        static constexpr uint_fast8_t modes[] = {softfloat_round_near_even, softfloat_round_minMag, softfloat_round_max,
                                                 softfloat_round_min};
        softfloat_roundingMode = modes[fpscr & 3];
        softfloat_detectTininess = softfloat_tininess_beforeRounding;
        softfloat_exceptionFlags = 0;
        extF80_roundingPrecision = 80;
    }

    ~MathSoftFloat()
    {
        softfloat_roundingMode = rounding;
        softfloat_detectTininess = tininess;
        softfloat_exceptionFlags = flags;
        extF80_roundingPrecision = precision;
    }
};

uint32_t raised_exceptions()
{
    auto flags = softfloat_exceptionFlags;
    return (flags & softfloat_flag_invalid ? 0x00000400 : 0) | (flags & softfloat_flag_infinite ? 0x04000000 : 0) |
           (flags & softfloat_flag_overflow ? 0x10000000 : 0) | (flags & softfloat_flag_underflow ? 0x08000000 : 0) |
           (flags & softfloat_flag_inexact ? 0x02000000 : 0);
}

void narrow_arguments(ROMlib_carbon_ppc_frame* call_frame, bool binary)
{
    MathSoftFloat rounding(call_frame->fpscr);
    auto* fpr = call_frame->floating;
    fpr[0] = f64_add(float64_t{fpr[0]}, float64_t{fpr[1]}).v;
    if (binary)
        fpr[1] = f64_add(float64_t{fpr[2]}, float64_t{fpr[3]}).v;
    call_frame->fpscr = romaw_ppc_record_fpscr(call_frame->fpscr, raised_exceptions());
}

uint64_t read_bytes(const unsigned char* bytes, size_t count)
{
    uint64_t value = 0;
    for (size_t index = 0; index != count; ++index)
        value = value << 8 | bytes[index];
    return value;
}

float64_t binary64_from_integer(int64_t number)
{
    // FixMath passes signed and unsigned 32-bit integers, all exact in binary64.
    const double value = static_cast<double>(number);
    float64_t bits;
    std::memcpy(&bits.v, &value, sizeof value);
    return bits;
}

int fixed_to_binary64(ROMlib_carbon_ppc_frame* call, uint64_t scale)
{
    const int32_t source = static_cast<int32_t>(call->words[0]);
    uint64_t bits = binary64_from_integer(source).v;
    if (!source && (call->fpscr & 3) == 3)
        bits = sign_bit;
    // The int32 conversion and power-of-two multiplication are both exact.
    return romaw_ppc_arithmetic_value(0xfc000032, bits, 0, scale, &call->fpscr, &call->floating[0]) != 0;
}

bool fixmath_profile(const ROMlib_carbon_reference& ref, uint32_t offset)
{
    return ref.code_offset == offset && ref.code_size == 0xb8fb4 && ref.toc && ref.toc_offset == 0x2f78 &&
           ref.toc_size == 0x10f0c;
}

const unsigned char* fixmath_constants(const ROMlib_carbon_reference& ref)
{
    const auto toc = static_cast<const unsigned char*>(ref.toc);
    const auto table = static_cast<uint32_t>(read_bytes(toc - 8, 4));
    return static_cast<const unsigned char*>(ROMlib_carbon_guest_memory(table, 0x3e8, 0));
}

int binary64_to_fixed(ROMlib_carbon_ppc_frame* call, const char* name, uint32_t offset, unsigned bias_offset,
                      uint64_t cutoff)
{
    const uint64_t bits = call->floating[0];
    const uint64_t magnitude = bits & ~sign_bit;
    const bool negative = (bits & sign_bit) != 0;
    const uint32_t saturated = negative ? UINT32_C(0x80000000) : UINT32_C(0x7fffffff);
    if (magnitude > infinity) {
        call->fpscr = romaw_ppc_record_fpscr(call->fpscr, 0x01000000);
        call->words[0] = UINT32_C(0x80000000);
        return 1;
    }
    if (magnitude >= cutoff) {
        call->words[0] = saturated;
        return 1;
    }

    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", name, &ref))
        return 0;
    if (!fixmath_profile(ref, offset))
        return ROMlib_carbon_invoke_frame(ref.function, call);

    // Carbon converts with a signed bias add under its own rounding mode,
    // restores FPSCR, then takes the magnitude from the low significand word.
    const auto constants = fixmath_constants(ref);
    if (!constants)
        return 0;
    const auto bias = read_bytes(constants + bias_offset + (negative ? 0x10 : 0), 8);
    const unsigned rounding = static_cast<unsigned>(read_bytes(constants + 0x3e0, 8)) & 3;
    const auto encoded = romaw_ppc_arithmetic(ppc_fp_add, false, rounding, bias, bits).bits;
    const auto units = static_cast<uint32_t>(encoded) >> 1;
    call->words[0] = encoded & UINT64_C(0x000fffff00000000) ? saturated : negative ? 0u - units : units;
    return 1;
}

int fract_trigonometry(ROMlib_carbon_ppc_frame* call, bool cosine)
{
    const char* name = cosine ? "FracCos" : "FracSin";
    const uint32_t offset = cosine ? 0xa99c8 : 0xa97d0;
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", name, &ref))
        return 0;
    if (!fixmath_profile(ref, offset))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    const auto constants = fixmath_constants(ref);
    if (!constants)
        return 0;

    // Reduce with Carbon's fixed-point pi/4. Reducing in radians instead
    // would accumulate a different phase error for large angles.
    constexpr uint32_t octant_width = 0xc910;
    constexpr uint32_t quadrant_width = 2 * octant_width;
    const auto angle = call->words[0];
    const bool negative_angle = (angle & UINT32_C(0x80000000)) != 0;
    const uint32_t magnitude = negative_angle ? 0u - angle : angle;
    const unsigned octant = magnitude / octant_width & 7;
    const unsigned remainder = magnitude % quadrant_width;
    const unsigned reduced = octant & 1 ? quadrant_width - remainder : remainder;
    const bool cosine_kernel = ((octant + 1) & 2) ? !cosine : cosine;
    const bool negative_result = cosine ? ((octant + 2) & 4) != 0 : ((octant & 4) != 0) != negative_angle;
    MathSoftFloat rounding(static_cast<uint32_t>(read_bytes(constants + 0x60, 4)));
    const auto coefficient = [&](unsigned at) { return float64_t{read_bytes(constants + at, 8)}; };
    const auto x = f64_mul(binary64_from_integer(reduced), coefficient(0x48));
    const auto x_squared = f64_mul(x, x);
    const unsigned start = cosine_kernel ? 0xe0 : 0xf8;
    auto polynomial = f64_mulAdd(x_squared, coefficient(start + 8), coefficient(start));
    polynomial = f64_mulAdd(x_squared, polynomial, coefficient(start + 16));
    const auto scaled = f64_mulAdd(cosine_kernel ? x_squared : x, polynomial, coefficient(cosine_kernel ? 8 : 0));
    const auto result = static_cast<uint32_t>(scaled.v);
    call->words[0] = negative_result ? 0u - result : result;
    return 1;
}

int fract_square_root(ROMlib_carbon_ppc_frame* call, const unsigned char* constants)
{
    uint32_t mantissa = call->words[0];
    if (!mantissa)
        return 1;
    MathSoftFloat rounding(static_cast<uint32_t>(read_bytes(constants + 0x60, 4)));
    const auto term = [&](unsigned at) { return float64_t{read_bytes(constants + at, 8)}; };
    auto bias = term(0);
    while (mantissa < UINT32_C(0x40000000)) {
        mantissa <<= 2;
        bias = f64_add(bias, bias);
    }

    const auto operand = f64_mul(binary64_from_integer(mantissa), term(0x38));
    const unsigned interval = (mantissa >> 28) - 4;
    const bool lower_interval = interval < 7;
    const unsigned base = lower_interval ? 0x160 + 56 * interval : 0x2e8 + 48 * (interval - 7);
    auto polynomial = f64_mulAdd(operand, term(base + 8), term(base));
    for (unsigned coefficient = 16; coefficient < (lower_interval ? 56u : 48u); coefficient += 8)
        polynomial = f64_mulAdd(operand, polynomial, term(base + coefficient));
    call->words[0] = static_cast<uint32_t>(f64_add(bias, polynomial).v);
    return 1;
}

int fixed_atan2(ROMlib_carbon_ppc_frame* call, const unsigned char* constants)
{
    const uint32_t x = call->words[0], y = call->words[1];
    const bool x_negative = (x & UINT32_C(0x80000000)) != 0;
    const bool y_negative = (y & UINT32_C(0x80000000)) != 0;
    const uint32_t x_magnitude = x_negative ? 0u - x : x, y_magnitude = y_negative ? 0u - y : y;
    const bool steep = y_magnitude > x_magnitude;
    const auto internal_fpscr = static_cast<uint32_t>(read_bytes(constants + 0x60, 4));
    MathSoftFloat rounding(internal_fpscr);
    auto ratio = f32_to_f64(float32_t{internal_fpscr});
    if (x_magnitude || y_magnitude) {
        const auto minor = steep ? x_magnitude : y_magnitude, major = steep ? y_magnitude : x_magnitude;
        ratio = f64_div(binary64_from_integer(minor), binary64_from_integer(major));
    }
    const auto term = [&](unsigned at) { return float64_t{read_bytes(constants + at, 8)}; };
    const auto squared = f64_mul(ratio, ratio);
    auto polynomial = f64_mulAdd(squared, term(0x70), term(0x68));
    for (unsigned coefficient = 0x78; coefficient <= 0x90; coefficient += 8)
        polynomial = f64_mulAdd(squared, polynomial, term(coefficient));
    uint32_t angle = static_cast<uint32_t>(f64_mulAdd(ratio, polynomial, term(0x10)).v);
    constexpr uint32_t half_pi = 0x19220;
    if (steep)
        angle = half_pi - angle;
    if (x_negative)
        angle = 2 * half_pi - angle;
    call->words[0] = y_negative ? 0u - angle : angle;
    return 1;
}

void write_bytes(unsigned char* bytes, size_t count, uint64_t value)
{
    while (count) {
        bytes[--count] = value;
        value >>= 8;
    }
}

float128_t extended_value(uint64_t high, uint64_t low)
{
    auto value = f64_to_f128(float64_t{high});
    if ((low & ~sign_bit) && (high & ~sign_bit) < infinity) {
        // Retain the side of an integer or half-integer when the tail is
        // smaller than a binary128 unit in the last place.
        const auto saved_rounding = softfloat_roundingMode;
        softfloat_roundingMode = softfloat_round_odd;
        value = f128_add(value, f64_to_f128(float64_t{low}));
        softfloat_roundingMode = saved_rounding;
    }
    return value;
}

bool nonzero(float128_t value)
{
    return (value.v[high_word] & ~sign_bit) || value.v[1 - high_word];
}

void split_extended(float128_t input, uint64_t& high, uint64_t& low, float128_t tail = {})
{
    /* A double-double high limb is the nearest representable double.
       Its low limb keeps the residual, including at binade boundaries. */
    const auto saved_rounding = softfloat_roundingMode;
    const auto saved_flags = softfloat_exceptionFlags;
    softfloat_roundingMode = softfloat_round_near_even;
    high = f128_to_f64(nonzero(tail) ? f128_add(input, tail) : input).v;
    low = 0;
    if ((high & ~sign_bit) && (high & ~sign_bit) < infinity)
        low = f128_to_f64(f128_add(f128_sub(input, f64_to_f128(float64_t{high})), tail)).v;
    auto range_flags = softfloat_exceptionFlags & (softfloat_flag_overflow | softfloat_flag_underflow);
    softfloat_roundingMode = saved_rounding;
    softfloat_exceptionFlags = saved_flags | (range_flags ? range_flags | softfloat_flag_inexact : 0);
}

__float128 quad_argument(const uint64_t* limbs)
{
    float128_t bits = extended_value(limbs[0], limbs[1]);
    __float128 value;
    static_assert(sizeof value == sizeof bits);
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

int set_quad_result(ROMlib_carbon_ppc_frame* call_frame, __float128 value)
{
    float128_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    uint64_t high, low;
    split_extended(bits, high, low);
    call_frame->floating[0] = high;
    call_frame->floating[1] = low;
    call_frame->fpscr = romaw_ppc_record_fpscr(call_frame->fpscr, raised_exceptions());
    return 1;
}

float128_t binary_scale(float128_t input, int32_t exponent)
{
    /* Bounds beyond these powers produce the same binary64 overflow or
       underflow for every finite double-double input. */
    exponent = exponent < -8192 ? -8192 : exponent > 8192 ? 8192 : exponent;
    float128_t factor{};
    factor.v[high_word] = uint64_t(exponent + 16383) << 48;
    return f128_mul(input, factor);
}

float128_t round_extended(float128_t value, carbon_math_extended_op operation, bool separated, bool negative)
{
    auto mode = softfloat_roundingMode;
    switch (operation) {
    case carbon_math_dd_floor:
        mode = softfloat_round_min;
        break;
    case carbon_math_dd_ceil:
        mode = softfloat_round_max;
        break;
    case carbon_math_dd_trunc:
    case carbon_math_dd_modf:
        mode = separated ? (negative ? softfloat_round_max : softfloat_round_min) : softfloat_round_minMag;
        break;
    case carbon_math_dd_round:
        if (separated) {
            if (((value.v[high_word] >> 48) & 0x7fff) >= 16383 + 52)
                return value;
            value = f128_add(
                value, f64_to_f128(float64_t{negative ? UINT64_C(0xbfe0000000000000) : UINT64_C(0x3fe0000000000000)}));
            mode = negative ? softfloat_round_max : softfloat_round_min;
        }
        else {
            mode = softfloat_round_near_maxMag;
        }
        break;
    default:
        break;
    }
    return f128_roundToInt(value, mode, operation != carbon_math_dd_nearby && operation != carbon_math_dd_modf);
}

int convert_extended(ROMlib_carbon_ppc_frame* call_frame, carbon_math_extended_op operation)
{
    bool read_x80 = operation == carbon_math_x80_to_double || operation == carbon_math_x80_to_dd;
    bool read_dd = operation == carbon_math_dd_to_x80;
    bool write_dd = operation == carbon_math_x80_to_dd;
    if (!call_frame->words[0] || (operation != carbon_math_x80_to_double && !call_frame->words[1]))
        return ROMlib_carbon_report_failure("Missing MathLib conversion pointer");
    auto input = static_cast<const unsigned char*>(ROMlib_carbon_guest_memory(call_frame->words[0],
                                                                              read_x80  ? 10
                                                                              : read_dd ? 16
                                                                                        : 8,
                                                                              0));
    if (!input)
        return 0;
    unsigned char* output = nullptr;
    if (operation != carbon_math_x80_to_double) {
        output = static_cast<unsigned char*>(ROMlib_carbon_guest_memory(call_frame->words[1], write_dd ? 16 : 10, 1));
        if (!output)
            return 0;
    }
    if (read_x80) {
        extFloat80_t value{};
        value.signExp = read_bytes(input, 2);
        value.signif = read_bytes(input + 2, 8);
        if (write_dd) {
            uint64_t high, low;
            split_extended(extF80_to_f128(value), high, low);
            write_bytes(output, 8, high);
            write_bytes(output + 8, 8, low);
        }
        else {
            call_frame->floating[0] = extF80_to_f64(value).v;
        }
    }
    else {
        uint64_t high = read_bytes(input, 8);
        extFloat80_t value =
            read_dd ? f128_to_extF80(extended_value(high, read_bytes(input + 8, 8))) : f64_to_extF80(float64_t{high});
        write_bytes(output, 2, value.signExp);
        write_bytes(output + 2, 8, value.signif);
    }
    return 1;
}
}

int ROMlib_carbon_math_wide_float(ROMlib_carbon_ppc_frame* frame, carbon_wide_operation operation)
{
    MathSoftFloat environment(frame->fpscr);
    if (operation == wide_signed_to_float || operation == wide_unsigned_to_float) {
        /* The original adds the high and low words separately and records
           the residual after applying the caller's binary64 rounding mode. */
        constexpr uint64_t upper_base = UINT64_C(0x4530000000000000);
        constexpr uint64_t lower_base = UINT64_C(0x4330000000000000);
        const uint32_t bias = operation == wide_signed_to_float ? UINT32_C(0x80000000) : 0;
        const auto upper = f64_sub(float64_t{upper_base | (frame->words[0] ^ bias)}, float64_t{upper_base | bias});
        const auto lower = f64_sub(float64_t{lower_base | frame->words[1]}, float64_t{lower_base});
        const auto sum = f64_add(upper, lower);
        frame->floating[0] = sum.v;
        frame->floating[1] = f64_sub(lower, f64_sub(sum, upper)).v;
        frame->fpscr = romaw_ppc_record_fpscr(frame->fpscr, raised_exceptions());
        return 1;
    }

    auto* output =
        frame->words[0] ? static_cast<unsigned char*>(ROMlib_carbon_guest_memory(frame->words[0], 8, 1)) : nullptr;
    if (!output)
        return ROMlib_carbon_report_failure("Invalid Math64Lib conversion result address");
    const auto value = extended_value(frame->floating[0], frame->floating[1]);
    const auto magnitude = value.v[high_word] & ~sign_bit;
    const bool negative = value.v[high_word] & sign_bit;
    const int exponent = static_cast<int>(magnitude >> 48) - 0x3fff;
    const uint64_t fraction = magnitude & UINT64_C(0x0000ffffffffffff);
    const bool nan = exponent == 0x4000 && (fraction || value.v[1 - high_word]);
    uint64_t integer = 0;
    if (exponent >= 0 && exponent < 64) {
        const uint64_t significand = fraction | (UINT64_C(1) << 48);
        if (exponent <= 48)
            integer = significand >> (48 - exponent);
        else
            integer = significand << (exponent - 48) | value.v[1 - high_word] >> (112 - exponent);
    }
    uint64_t result;
    if (operation == wide_float_to_signed) {
        if (nan || (negative && exponent >= 63))
            result = sign_bit;
        else if (exponent >= 63)
            result = sign_bit - 1;
        else
            result = negative ? -integer : integer;
    }
    else
        result = negative || nan ? 0 : exponent >= 64 ? UINT64_MAX : integer;
    write_bytes(output, 8, result);
    return 1;
}

int ROMlib_carbon_math_dd_unary(ROMlib_carbon_ppc_frame* call_frame, double (*function)(double))
{
    narrow_arguments(call_frame, false);
    if (!ROMlib_carbon_math_unary(call_frame, function))
        return 0;
    call_frame->floating[1] = 0;
    return 1;
}

int ROMlib_carbon_math_dd_binary(ROMlib_carbon_ppc_frame* call_frame, double (*function)(double, double))
{
    narrow_arguments(call_frame, true);
    if (!ROMlib_carbon_math_binary(call_frame, function))
        return 0;
    call_frame->floating[1] = 0;
    return 1;
}

int ROMlib_carbon_math_dd_remquo(ROMlib_carbon_ppc_frame* call_frame)
{
    uint32_t quotient;
    if (!ROMlib_carbon_frame_word(call_frame, 8, &quotient))
        return 0;
    narrow_arguments(call_frame, true);
    call_frame->words[4] = quotient;
    if (!ROMlib_carbon_math_aux(call_frame, carbon_math_remquo))
        return 0;
    call_frame->floating[1] = 0;
    return 1;
}

int ROMlib_carbon_math_quad_unary(ROMlib_carbon_ppc_frame* call_frame, __float128 (*function)(__float128))
{
    MathSoftFloat rounding(call_frame->fpscr);
    math_host_environment saved;
    if (!ROMlib_carbon_math_begin_host(&saved, call_frame->fpscr))
        return 0;
    __float128 value = function(quad_argument(call_frame->floating));
    ROMlib_carbon_math_finish_host(call_frame, &saved);
    return set_quad_result(call_frame, value);
}

int ROMlib_carbon_math_quad_binary(ROMlib_carbon_ppc_frame* call_frame, __float128 (*function)(__float128, __float128))
{
    MathSoftFloat rounding(call_frame->fpscr);
    math_host_environment saved;
    if (!ROMlib_carbon_math_begin_host(&saved, call_frame->fpscr))
        return 0;
    const auto* fpr = call_frame->floating;
    __float128 value = function(quad_argument(fpr), quad_argument(fpr + 2));
    ROMlib_carbon_math_finish_host(call_frame, &saved);
    return set_quad_result(call_frame, value);
}

int ROMlib_carbon_fixed_to_double(ROMlib_carbon_ppc_frame* call)
{
    return fixed_to_binary64(call, UINT64_C(0x3ef0000000000000));
}

int ROMlib_carbon_fract_to_double(ROMlib_carbon_ppc_frame* call)
{
    return fixed_to_binary64(call, UINT64_C(0x3e10000000000000));
}

int ROMlib_carbon_double_to_fixed(ROMlib_carbon_ppc_frame* call)
{
    return binary64_to_fixed(call, "X2Fix", 0xa8eb4, 0x20, UINT64_C(0x40f0000000000000));
}

int ROMlib_carbon_double_to_fract(ROMlib_carbon_ppc_frame* call)
{
    return binary64_to_fixed(call, "X2Frac", 0xa9000, 0x18, UINT64_C(0x4010000000000000));
}

int ROMlib_carbon_fract_cosine(ROMlib_carbon_ppc_frame* call)
{
    return fract_trigonometry(call, true);
}

int ROMlib_carbon_fract_sine(ROMlib_carbon_ppc_frame* call)
{
    return fract_trigonometry(call, false);
}

int ROMlib_carbon_fract_square_root(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", "FracSqrt", &ref))
        return 0;
    if (!fixmath_profile(ref, 0xa944c))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    const auto constants = fixmath_constants(ref);
    return constants && fract_square_root(call, constants);
}

int ROMlib_carbon_fixed_atan2(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", "FixATan2", &ref))
        return 0;
    if (!fixmath_profile(ref, 0xa9b50))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    const auto constants = fixmath_constants(ref);
    return constants && fixed_atan2(call, constants);
}

extern "C" int ROMlib_carbon_math_extended(ROMlib_carbon_ppc_frame* call_frame, carbon_math_extended_op operation)
{
    MathSoftFloat rounding(call_frame->fpscr);
    if (operation >= carbon_math_x80_to_double) {
        if (!convert_extended(call_frame, operation))
            return 0;
        call_frame->fpscr = romaw_ppc_record_fpscr(call_frame->fpscr, raised_exceptions());
        return 1;
    }

    auto* fpr = call_frame->floating;
    uint64_t magnitude = fpr[0] & ~sign_bit;
    bool negative = (fpr[0] & sign_bit) != 0;
    // The high limb is integral at this scale, and binary128 cannot
    // preserve its fractional low limb as part of a single sum.
    bool separated = magnitude >= UINT64_C(0x46f0000000000000) && magnitude < infinity;
    if (operation == carbon_math_dd_relation) {
        fpr[0] = f64_add(float64_t{fpr[0]}, float64_t{fpr[1]}).v;
        fpr[1] = f64_add(float64_t{fpr[2]}, float64_t{fpr[3]}).v;
        call_frame->fpscr = romaw_ppc_record_fpscr(call_frame->fpscr, raised_exceptions());
        return ROMlib_carbon_math_bits(call_frame, carbon_math_relation);
    }
    bool scalar = operation == carbon_math_to_long || operation == carbon_math_round_to_long;
    bool round_limbs = separated && (operation <= carbon_math_dd_nearby || operation == carbon_math_dd_modf);
    auto value = extended_value(fpr[0], scalar || round_limbs ? 0 : fpr[1]);
    if (operation >= carbon_math_to_long) {
        bool ties_away = operation == carbon_math_round_to_long || operation == carbon_math_dd_round_to_long;
        auto mode = ties_away ? softfloat_round_near_maxMag : softfloat_roundingMode;
        call_frame->words[0] = uint32_t(f128_to_i32(value, mode, true));
    }
    else {
        float128_t tail{};
        switch (operation) {
        case carbon_math_dd_floor:
        case carbon_math_dd_ceil:
        case carbon_math_dd_trunc:
        case carbon_math_dd_round:
        case carbon_math_dd_rint:
        case carbon_math_dd_nearby:
            if (round_limbs)
                tail = round_extended(f64_to_f128(float64_t{fpr[1]}), operation, true, negative);
            else
                value = round_extended(value, operation, false, negative);
            break;
        case carbon_math_dd_sqrt: {
            value = f128_sqrt(value);
            if (!(fpr[0] & sign_bit) && fpr[0] && fpr[0] < infinity) {
                // Recover a small residual even when sqrt(x) rounded to the
                // high limb in binary128. The binary64 square is exact here.
                auto root = f64_to_f128(f128_to_f64(value));
                auto square = f128_mul(root, root);
                auto difference = f128_sub(f64_to_f128(float64_t{fpr[0]}), square);
                tail = f128_div(f128_add(difference, f64_to_f128(float64_t{fpr[1]})), f128_add(value, root));
                value = root;
            }
            break;
        }
        case carbon_math_dd_scale:
            value = binary_scale(f64_to_f128(float64_t{fpr[0]}), int32_t(call_frame->words[4]));
            if ((fpr[0] & ~sign_bit) < infinity)
                tail = binary_scale(f64_to_f128(float64_t{fpr[1]}), int32_t(call_frame->words[4]));
            break;
        case carbon_math_dd_modf: {
            if (!call_frame->words[4])
                return ROMlib_carbon_report_failure("Missing MathLib integral output pointer");
            auto output = static_cast<unsigned char*>(ROMlib_carbon_guest_memory(call_frame->words[4], 16, 1));
            if (!output)
                return 0;
            auto low_limb = f64_to_f128(float64_t{fpr[1]});
            auto integral = round_extended(round_limbs ? low_limb : value, operation, round_limbs, negative);
            uint64_t high, low;
            if (round_limbs) {
                split_extended(value, high, low, integral);
                write_bytes(output, 8, high);
                write_bytes(output + 8, 8, low);
                value = f128_sub(low_limb, integral);
                if (!nonzero(value))
                    value.v[high_word] = fpr[0] & sign_bit;
                break;
            }
            split_extended(integral, high, low);
            write_bytes(output, 8, high);
            write_bytes(output + 8, 8, low);
            if (magnitude == infinity)
                value = f64_to_f128(float64_t{fpr[0] & sign_bit});
            else if (magnitude < infinity) {
                value = f128_sub(f64_to_f128(float64_t{fpr[0]}), integral);
                tail = low_limb;
                if (!nonzero(value) && !nonzero(tail))
                    value.v[high_word] = fpr[0] & sign_bit;
            }
            break;
        }
        default:
            return ROMlib_carbon_report_failure("Invalid extended MathLib operation");
        }
        uint64_t high, low;
        split_extended(value, high, low, tail);
        fpr[0] = high;
        fpr[1] = low;
    }
    call_frame->fpscr = romaw_ppc_record_fpscr(call_frame->fpscr, raised_exceptions());
    return 1;
}
