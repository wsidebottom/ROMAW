/*+===================================================================
  File:      native040-convert-test.c

  Summary:   Compare emitted floating conversions, operand tags and deferred integer values with the maintained model.

  Classes:   None.

  Functions: main.

  Origin:    Original ARDI Executor direct 68040 regression fixture.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "native040.h"
#include "fpu_arithmetic.h"
#include "fpu_convert.h"
#include <assert.h>
#include <stdio.h>

static unsigned cases;

static void prepare(unsigned rounding, unsigned precision, unsigned tininess)
{
    float_exception_flags = 0;
    float_rounding_mode = rounding;
    floatx80_rounding_precision = precision;
    float_detect_tininess = tininess;
}

static void check(Syn68kNative040FPU* frame, enum Syn68kNative040Operation operation, floatx80 expected, uint64_t bits,
                  unsigned flags)
{
    float_exception_flags = 0x5a;
    float_rounding_mode = 3 - frame->rounding;
    floatx80_rounding_precision = 91;
    float_detect_tininess = 1 - frame->tininess;
    Syn68kNative040Entry entry = syn68k_native040_entry(operation);
    assert(entry);
    entry(frame);
    assert(frame->result.high == expected.high && frame->result.low == expected.low);
    assert(frame->bits == bits && (unsigned)frame->flags == flags);
    assert(float_exception_flags == 0x5a && float_rounding_mode == 3 - frame->rounding &&
           floatx80_rounding_precision == 91 && float_detect_tininess == 1 - frame->tininess);
    ++cases;
}

static void sources(uint64_t bits)
{
    for (unsigned format = 0; format < 3; ++format) {
        prepare(0, 80, 0);
        FPUSource expected = format == 0   ? (FPUSource){.value = int32_to_floatx80((int32)bits)}
                             : format == 1 ? fpu_single_source(bits)
                                           : fpu_double_source(bits);
        unsigned flags = float_exception_flags;
        Syn68kNative040FPU frame = {
            .bits = bits, .precision = 80, .status = ~0u, .unsupported = ~0u, .signaling = ~0u, .source_tag = ~0u};
        check(&frame, NATIVE040_FROM_I32 + format, expected.value, bits, flags);
        assert(frame.unsupported == expected.unsupported && frame.signaling == expected.signaling &&
               frame.source_tag == expected.source_tag && !frame.status);
    }
}

static void destinations(floatx80 value)
{
    const unsigned widths[] = {80, 32, 64};
    for (unsigned rounding = 0; rounding < 4; ++rounding)
        for (unsigned p = 0; p < 3; ++p)
            for (unsigned tininess = 0; tininess < 2; ++tininess)
                for (unsigned format = 0; format < 5; ++format) {
                    unsigned precision = widths[p], status = 0;
                    prepare(rounding, precision, tininess);
                    floatx80 expected;
                    uint64_t bits;
                    if (format < 3)
                        bits = (int64)fpu_integer_destination(value,
                                                              format == 0   ? 32
                                                              : format == 1 ? 16
                                                                            : 8,
                                                              &expected);
                    else {
                        FPUdivision range = fpu_calculate(value, value, 0, format == 3 ? 32 : 64);
                        status = range.status;
                        expected = status == 0x1000 ? range.result : value;
                        bits = format == 3 ? floatx80_to_float32(value) : floatx80_to_float64(value);
                    }
                    unsigned flags = float_exception_flags;
                    Syn68kNative040FPU frame = {.source = {value.high, value.low},
                                                .rounding = rounding,
                                                .precision = precision,
                                                .tininess = tininess,
                                                .status = ~0u};
                    check(&frame, NATIVE040_TO_I32 + format, expected, bits, flags);
                    assert(frame.status == status);
                }
}

int main(void)
{
    const uint64_t boundaries[] = {0,
                                   UINT64_MAX,
                                   1,
                                   UINT64_C(0x8000000000000000),
                                   0x7fffffff,
                                   0x80000000,
                                   0x007fffff,
                                   0x00800000,
                                   0x7f800000,
                                   0xff800000,
                                   0x7f812345,
                                   0x7fc12345,
                                   UINT64_C(0x000fffffffffffff),
                                   UINT64_C(0x0010000000000000),
                                   UINT64_C(0x7ff0000000000000),
                                   UINT64_C(0xfff0000000000000),
                                   UINT64_C(0x7ff0123456789abc),
                                   UINT64_C(0x7ff8123456789abc)};
    for (unsigned i = 0; i < sizeof boundaries / sizeof *boundaries; ++i)
        sources(boundaries[i]);
    uint64_t random = UINT64_C(0x9876543210abcdef);
    for (unsigned i = 0; i < 16384; ++i) {
        random ^= random << 13;
        random ^= random >> 7;
        random ^= random << 17;
        sources(random);
        if (i < 1024)
            destinations((floatx80){(i * 97) & 0xffff, random | UINT64_C(0x8000000000000000)});
    }
    const int32 integers[] = {0, 1, -1, 127, 128, -128, -129, 32767, 32768, -32768, -32769, INT32_MAX, INT32_MIN};
    for (unsigned i = 0; i < sizeof integers / sizeof *integers; ++i) {
        floatx80 value = int32_to_floatx80(integers[i]);
        destinations(value);
        if (value.low) {
            value.low |= 0x80000001; // Inexact near integer-format boundaries.
            destinations(value);
        }
    }
    const floatx80 special[] = {{0x8000, 0},
                                {0, 1},
                                {0, UINT64_C(0x7fffffffffffffff)},
                                {0x7ffe, UINT64_MAX},
                                {0x7fff, UINT64_C(0x8000000000000000)},
                                {0xffff, UINT64_C(0x8000000000000000)},
                                {0x7fff, UINT64_C(0x80123456789abcde)},
                                {0xffff, UINT64_C(0xc0123456789abcde)}};
    for (unsigned i = 0; i < sizeof special / sizeof *special; ++i)
        destinations(special[i]);
    printf("%u emitted integer/single/double conversion/tag/rounding/state-isolation cases passed\n", cases);
    return 0;
}
