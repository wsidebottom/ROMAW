/*+===================================================================
  File:      decimal.cpp

  Summary:   Implements locale-independent binary-to-decimal conversion and formatting for PPC MathLib.

  Classes:   Declared: decimal_digits.

  Functions: ROMlib_ppc_num2dec, ROMlib_ppc_num2decl, ROMlib_ppc_decimal_text,
             ROMlib_ppc_dec2str, ROMlib_ppc_str2dec, ROMlib_ppc_dec2num.

  Origin:    Original implementation of the record layout and formatting specified in Inside Macintosh:
             PowerPC Numerics, Chapter 9 and Appendix C. Uses the standard C++ character conversion library.

  Copyright and Legal notices: Applicable repository license: synppc/COPYING.
===================================================================+*/
#include "decimal.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <quadmath.h>
#include <system_error>

struct decimal_digits {
    char text[36];
    int count = 0, exponent = 0;
};

template <typename Float> static bool scientific_digits(Float value, int precision, decimal_digits& digits)
{
    char buffer[80];
    char* limit;
    if constexpr (sizeof(Float) == sizeof(double)) {
        auto result = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::scientific, precision - 1);
        if (result.ec != std::errc())
            return false;
        limit = result.ptr;
    }
    else {
        const int length = quadmath_snprintf(buffer, sizeof buffer, "%.*Qe", precision - 1, value);
        if (length < 0 || (size_t)length >= sizeof buffer)
            return false;
        limit = buffer + length;
    }
    char* cursor = buffer;
    digits.count = 0;
    while (cursor < limit && *cursor != 'e') {
        if (*cursor >= '0' && *cursor <= '9')
            digits.text[digits.count++] = *cursor;
        ++cursor;
    }
    if (cursor == limit)
        return false;
    ++cursor;
    if (cursor < limit && *cursor == '+')
        ++cursor;
    int exponent;
    auto parsed = std::from_chars(cursor, limit, exponent);
    if (parsed.ec != std::errc() || parsed.ptr != limit)
        return false;
    digits.exponent = exponent - digits.count + 1;
    return true;
}

template <typename Float>
static int decimal_from_binary(int style, int digits, Float magnitude, bool negative, char special,
                               unsigned char output[ROMLIB_PPC_DECIMAL_SIZE])
{
    if (!output || (style != 0 && style != 1) || digits < INT16_MIN || digits > INT16_MAX)
        return 0;
    std::memset(output, 0, ROMLIB_PPC_DECIMAL_SIZE);
    output[0] = negative;
    if (special) {
        output[4] = 1;
        output[5] = special;
        return 1;
    }
    const Float value = magnitude;
    decimal_digits converted;
    if (!scientific_digits(value, 36, converted))
        return 0;
    int order = converted.exponent + converted.count - 1;
    int precision = style == 0 ? std::clamp(digits, 1, 36) : order + digits + 1;
    if (precision <= 0) {
        bool above_half = false;
        if (precision == 0) {
            above_half = converted.text[0] > '5';
            if (converted.text[0] == '5')
                for (int i = 1; i < converted.count; ++i)
                    above_half |= converted.text[i] != '0';
        }
        // At an exact half, zero is the even multiple of the requested quantum.
        converted.text[0] = above_half ? '1' : '0';
        converted.count = 1;
        converted.exponent = above_half ? -digits : 0;
    }
    else {
        precision = std::min(precision, 36);
        if (!scientific_digits(value, precision, converted))
            return 0;
        int rounded_order = converted.exponent + converted.count - 1;
        if (style == 1 && rounded_order > order)
            while (converted.count < std::min(rounded_order + digits + 1, 36)) {
                converted.text[converted.count++] = '0';
                --converted.exponent;
            }
    }
    uint16_t exponent = static_cast<uint16_t>(converted.exponent);
    output[2] = exponent >> 8;
    output[3] = exponent;
    output[4] = converted.count;
    std::memcpy(output + 5, converted.text, converted.count);
    return 1;
}

extern "C" int ROMlib_ppc_num2dec(int style, int digits, double value, unsigned char output[ROMLIB_PPC_DECIMAL_SIZE])
{
    const char special = std::isnan(value) ? 'N' : std::isinf(value) ? 'I' : value == 0 ? '0' : 0;
    return decimal_from_binary(style, digits, std::fabs(value), std::signbit(value), special, output);
}

extern "C" int ROMlib_ppc_num2decl(int style, int digits, __float128 value, unsigned char output[ROMLIB_PPC_DECIMAL_SIZE])
{
    const char special = isnanq(value) ? 'N' : isinfq(value) ? 'I' : value == 0 ? '0' : 0;
    return decimal_from_binary(style, digits, fabsq(value), signbitq(value), special, output);
}

extern "C" int ROMlib_ppc_decimal_text(const unsigned char decimal[ROMLIB_PPC_DECIMAL_SIZE],
                                       char output[ROMLIB_PPC_DECIMAL_INPUT_SIZE])
{
    if (!decimal || !output || decimal[4] < 1 || decimal[4] > 36)
        return 0;
    char* end = output;
    if (decimal[0])
        *end++ = '-';
    if (decimal[5] == 'N' || decimal[5] == 'I') {
        std::memcpy(end, decimal[5] == 'N' ? "nan" : "inf", 4);
        return 1;
    }
    for (int i = 0; i < decimal[4]; ++i) {
        if (decimal[5 + i] < '0' || decimal[5 + i] > '9')
            return 0;
        *end++ = decimal[5 + i];
    }
    *end++ = 'e';
    const int exponent = static_cast<int16_t>((decimal[2] << 8) | decimal[3]);
    auto formatted = std::to_chars(end, output + ROMLIB_PPC_DECIMAL_INPUT_SIZE - 1, exponent);
    if (formatted.ec != std::errc())
        return 0;
    *formatted.ptr = 0;
    return 1;
}

extern "C" int ROMlib_ppc_dec2num(const unsigned char decimal[ROMLIB_PPC_DECIMAL_SIZE], double* output)
{
    char buffer[ROMLIB_PPC_DECIMAL_INPUT_SIZE];
    if (!output || !ROMlib_ppc_decimal_text(decimal, buffer))
        return 0;
    const char* end = buffer + std::strlen(buffer);
    double value;
    auto parsed = std::from_chars(buffer, end, value, std::chars_format::scientific);
    if (parsed.ec == std::errc::result_out_of_range) {
        const int exponent = static_cast<int16_t>((decimal[2] << 8) | decimal[3]);
        value = std::copysign(exponent + decimal[4] - 1 > 0 ? std::numeric_limits<double>::infinity() : 0.0,
                              decimal[0] ? -1.0 : 1.0);
    }
    else if (parsed.ec != std::errc() || parsed.ptr != end)
        return 0;
    *output = value;
    return 1;
}

extern "C" int ROMlib_ppc_str2dec(const char* text, size_t length, int* index,
                                  unsigned char decimal[ROMLIB_PPC_DECIMAL_SIZE], int* valid_prefix)
{
    if (!text || !index || !decimal || !valid_prefix || length > INT16_MAX || *index < 0 ||
        static_cast<size_t>(*index) > length)
        return 0;
    size_t cursor = *index;
    std::memset(decimal, 0, ROMLIB_PPC_DECIMAL_SIZE);
    decimal[4] = 1;
    decimal[5] = 'N';
    while (cursor < length && (text[cursor] == ' ' || text[cursor] == '\t'))
        ++cursor;
    if (cursor < length && (text[cursor] == '-' || text[cursor] == '+'))
        decimal[0] = text[cursor++] == '-';
    auto token = [&](const char* word) {
        size_t count = std::strlen(word);
        if (count > length - cursor)
            return false;
        for (size_t i = 0; i < count; ++i) {
            char c = text[cursor + i];
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
            if (c != word[i])
                return false;
        }
        cursor += count;
        return true;
    };
    if (token("infinity") || token("inf") || token("nan")) {
        decimal[5] = text[cursor - 1] == 'n' || text[cursor - 1] == 'N' ? 'N' : 'I';
        *index = static_cast<int>(cursor);
        *valid_prefix = cursor == length;
        return 1;
    }
    char significant[36];
    int count = 0, fraction = 0, dropped = 0, round = -1;
    bool seen = false, point = false, nonzero = false, sticky = false;
    while (cursor < length) {
        char c = text[cursor];
        if (c == '.' && !point) {
            point = true;
            ++cursor;
            continue;
        }
        if (c < '0' || c > '9')
            break;
        seen = true;
        fraction += point;
        nonzero |= c != '0';
        if (nonzero) {
            if (count < 36)
                significant[count++] = c;
            else {
                ++dropped;
                if (round < 0)
                    round = c - '0';
                else
                    sticky |= c != '0';
            }
        }
        ++cursor;
    }
    if (!seen) {
        *valid_prefix = cursor == length;
        return 1;
    }
    size_t accepted = cursor;
    int exponent = 0;
    if (cursor < length && (text[cursor] == 'e' || text[cursor] == 'E')) {
        ++cursor;
        bool negative = false;
        if (cursor < length && (text[cursor] == '+' || text[cursor] == '-'))
            negative = text[cursor++] == '-';
        size_t start = cursor;
        while (cursor < length && text[cursor] >= '0' && text[cursor] <= '9') {
            exponent = std::min(100000, exponent * 10 + text[cursor++] - '0');
        }
        if (cursor != start) {
            accepted = cursor;
            if (negative)
                exponent = -exponent;
        }
        else
            exponent = 0;
    }
    *index = static_cast<int>(accepted);
    *valid_prefix = cursor == length;
    exponent += dropped - fraction;
    if (!nonzero) {
        significant[0] = '0';
        count = 1;
        exponent = 0;
    }
    if (round > 5 || (round == 5 && (sticky || (significant[count - 1] & 1)))) {
        int i = count - 1;
        while (i >= 0 && significant[i] == '9')
            significant[i--] = '0';
        if (i >= 0)
            ++significant[i];
        else {
            significant[0] = '1';
            ++exponent;
        }
    }
    if (exponent < INT16_MIN || exponent > INT16_MAX) {
        decimal[5] = exponent > 0 ? 'I' : '0';
        return 1;
    }
    decimal[2] = static_cast<uint16_t>(exponent) >> 8;
    decimal[3] = exponent;
    decimal[4] = count;
    std::memcpy(decimal + 5, significant, count);
    return 1;
}

extern "C" int ROMlib_ppc_dec2str(int style, int digits, const unsigned char decimal[ROMLIB_PPC_DECIMAL_SIZE],
                                  char output[ROMLIB_PPC_DECIMAL_STRING_SIZE])
{
    if (!decimal || !output || (style != 0 && style != 1) || digits < INT16_MIN || digits > INT16_MAX)
        return 0;
    output[0] = 0;
    int count = decimal[4];
    if (count < 1 || count > 36)
        return 0;
    bool negative = decimal[0] != 0;
    int used = 0;
    if (negative || style == 0)
        output[used++] = negative ? '-' : ' ';
    if (decimal[5] == 'N' || decimal[5] == 'I') {
        std::memcpy(output + used, decimal[5] == 'N' ? "NAN" : "INF", 4);
        return 1;
    }
    for (int i = 0; i < count; ++i)
        if (decimal[5 + i] < '0' || decimal[5 + i] > '9')
            return 0;
    int exponent = static_cast<int16_t>((decimal[2] << 8) | decimal[3]);
    if (style == 0) {
        int precision = std::clamp(digits, 1, 36);
        output[used++] = decimal[5];
        if (precision > 1)
            output[used++] = '.';
        for (int i = 1; i < precision; ++i)
            output[used++] = i < count ? decimal[5 + i] : '0';
        output[used++] = 'e';
        exponent += count - 1;
        output[used++] = exponent < 0 ? '-' : '+';
        auto result = std::to_chars(output + used, output + ROMLIB_PPC_DECIMAL_STRING_SIZE - 1, std::abs(exponent));
        if (result.ec != std::errc())
            return 0;
        *result.ptr = 0;
    }
    else {
        int point = count + exponent;
        int before = std::max(point, 1), fraction = std::clamp(digits, 0, 36);
        if (used + before + fraction + (fraction != 0) >= ROMLIB_PPC_DECIMAL_STRING_SIZE) {
            std::memcpy(output, "?", 2);
            return 1;
        }
        for (int i = 0; i < before; ++i)
            output[used++] = point > 0 && i < count ? decimal[5 + i] : '0';
        if (fraction)
            output[used++] = '.';
        for (int i = 0; i < fraction; ++i) {
            int index = point + i;
            output[used++] = index >= 0 && index < count ? decimal[5 + index] : '0';
        }
        output[used] = 0;
    }
    return 1;
}
