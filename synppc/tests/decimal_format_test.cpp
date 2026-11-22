/*+===================================================================
  File:      decimal_format_test.cpp

  Summary:   Checks PPC decimal record precision, rounding, bounded formatting and double round trips.

  Classes:   None.

  Functions: main.

  Origin:    Original regression fixture for Executor's MathLib decimal adapters.

  Copyright and Legal notices: Applicable repository license: synppc/COPYING.
===================================================================+*/
#include "decimal.h"
#include <cassert>
#include <cfenv>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>

static void check(int style, int precision, double value, const char* significant, int exponent, const char* text)
{
    unsigned char guarded[44];
    std::memset(guarded, 0xa5, sizeof guarded);
    auto* decimal = guarded + 1;
    assert(ROMlib_ppc_num2dec(style, precision, value, decimal));
    assert(decimal[0] == !!std::signbit(value));
    assert(decimal[4] == std::strlen(significant));
    if (std::memcmp(decimal + 5, significant, decimal[4]))
        std::fprintf(stderr, "decimal %.17g style %d digits %d: got %.*s, expected %s\n", value, style, precision,
                     decimal[4], decimal + 5, significant);
    assert(!std::memcmp(decimal + 5, significant, decimal[4]));
    assert(static_cast<int16_t>((decimal[2] << 8) | decimal[3]) == exponent);
    assert(guarded[0] == 0xa5 && guarded[43] == 0xa5);
    char output[82];
    std::memset(output, 0x5a, sizeof output);
    assert(ROMlib_ppc_dec2str(style, precision, decimal, output + 1));
    assert(!std::strcmp(output + 1, text));
    assert(output[0] == 0x5a && output[81] == 0x5a);
}

int main()
{
    check(0, 6, 123.45, "123450", -3, " 1.23450e+2");
    check(1, 2, 123.45, "12345", -2, "123.45");
    check(1, 4, 578, "5780000", -4, "578.0000");
    check(1, 2, 9.999, "1000", -2, "10.00");
    check(1, -2, -125, "1", 2, "-100");
    check(1, -2, 150, "2", 2, "200");
    check(1, -2, 50, "0", 0, "0");
    check(1, 1, 0.05, "1", -1, "0.1");
    check(1, 1, std::nextafter(0.05, 0.0), "0", 0, "0.0");
    check(1, 3, -0.0, "0", 0, "-0.000");
    check(0, 36, 0.1, "100000000000000005551115123125782702", -36, " 1.00000000000000005551115123125782702e-1");
    check(0, 3, std::numeric_limits<double>::infinity(), "I", 0, " INF");
    unsigned char decimal[42];
    char text[80];
    int index = 0, valid = -1;
    double parsed_value;
    assert(ROMlib_ppc_str2dec(" -12.5x", 7, &index, decimal, &valid));
    assert(index == 6 && valid == 0);
    assert(ROMlib_ppc_dec2num(decimal, &parsed_value) && parsed_value == -12.5);
    for (const char* input : {"12e+", "12e+x"}) {
        index = 0;
        assert(ROMlib_ppc_str2dec(input, std::strlen(input), &index, decimal, &valid));
        assert(index == 2 && valid == (std::strlen(input) == 4));
        assert(ROMlib_ppc_dec2num(decimal, &parsed_value) && parsed_value == 12);
    }
    index = 0;
    assert(ROMlib_ppc_str2dec("9999999999999999999999999999999999999", 37, &index, decimal, &valid));
    assert(decimal[4] == 36 && decimal[5] == '1' && decimal[3] == 2);
    for (const char* input : {"1e999999", "1e-999999", "-0", "INF", "NAN"}) {
        index = 0;
        assert(ROMlib_ppc_str2dec(input, std::strlen(input), &index, decimal, &valid));
        assert(valid && index == static_cast<int>(std::strlen(input)));
        assert(ROMlib_ppc_dec2num(decimal, &parsed_value));
        if (!std::strcmp(input, "1e999999") || !std::strcmp(input, "INF"))
            assert(std::isinf(parsed_value));
        else if (!std::strcmp(input, "NAN"))
            assert(std::isnan(parsed_value));
        else
            assert(parsed_value == 0);
    }
    index = -1;
    assert(!ROMlib_ppc_str2dec("0", 1, &index, decimal, &valid));
    index = 0;
    assert(!ROMlib_ppc_str2dec("0", 1000000, &index, decimal, &valid));
    assert(!ROMlib_ppc_num2dec(2, 1, 0, decimal));
    assert(!ROMlib_ppc_num2dec(1, INT32_MAX, 1, decimal));
    assert(ROMlib_ppc_num2dec(0, 3, std::numeric_limits<double>::quiet_NaN(), decimal));
    assert(ROMlib_ppc_dec2str(0, 3, decimal, text) && std::strstr(text, "NAN"));
    decimal[4] = 37;
    assert(!ROMlib_ppc_dec2str(0, 3, decimal, text));
    assert(ROMlib_ppc_num2dec(0, 1, 1, decimal));
    decimal[2] = 0x7f;
    decimal[3] = 0xff;
    assert(ROMlib_ppc_dec2str(1, 36, decimal, text) && !std::strcmp(text, "?"));
    // Conversion is independent of the active binary floating-point rounding direction.
    int rounding = std::fegetround();
    for (int mode : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
        assert(!std::fesetround(mode));
        check(0, 1, 2.5, "2", 0, " 2e+0");
    }
    std::fesetround(rounding);
    uint64_t state = 0xcafef00d5a5a4242;
    unsigned checked = 0;
    for (unsigned i = 0; i < 4000; ++i) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        double value;
        std::memcpy(&value, &state, 8);
        if (!std::isfinite(value))
            continue;
        assert(ROMlib_ppc_num2dec(0, 17, value, decimal));
        assert(ROMlib_ppc_dec2str(0, 17, decimal, text));
        char* start = text + (text[0] == ' ');
        double restored;
        auto result = std::from_chars(start, text + std::strlen(text), restored, std::chars_format::scientific);
        assert(result.ec == std::errc() && result.ptr == text + std::strlen(text));
        assert(!std::memcmp(&value, &restored, 8));
        index = 0;
        assert(ROMlib_ppc_str2dec(text, std::strlen(text), &index, decimal, &valid));
        assert(valid && index == static_cast<int>(std::strlen(text)));
        assert(ROMlib_ppc_dec2num(decimal, &restored));
        assert(!std::memcmp(&value, &restored, 8));
        ++checked;
    }
    std::printf("PPC decimals: 36-digit records, fixed/floating rounding, guards and %u binary64 round trips passed\n",
                checked);
}
