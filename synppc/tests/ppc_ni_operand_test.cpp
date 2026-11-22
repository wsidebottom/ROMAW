/*+===================================================================
  File:      ppc_ni_operand_test.cpp

  Summary:   Implements ppc ni operand test support for synppc PowerPC execution and fragment loading.

  Classes:   Declared: test_case; Used: ROMlib_ppc_environment, ROMlib_ppc_registers, test_case

  Functions: main

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Original MPC750UM table 2-20 operand-zeroing and exception-order fixtures. */
#include "../src/ppc-arithmetic.h"
#include "synppc/ppc.h"
#include <assert.h>
#include <fenv.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>

static unsigned char memory[8];
static unsigned count;

static void* map(void*, uint32_t address, size_t size, int)
{
    return address <= sizeof memory && size <= sizeof memory - address ? memory + address : NULL;
}

static uint32_t summary(uint32_t value)
{
    value &= ~0x60000000u;
    if (value & 0x01f80700)
        value |= 0x20000000;
    if ((value >> 25) & (value >> 3) & 31)
        value |= 0x40000000;
    return value;
}

static uint32_t classification(uint64_t value)
{
    switch (value) {
    case 0:
        return 0x2000;
    case UINT64_C(0x8000000000000000):
        return 0x12000;
    case UINT64_C(0x3ff0000000000000):
        return 0x4000;
    case UINT64_C(0xbff0000000000000):
        return 0x8000;
    case UINT64_C(0x7ff0000000000000):
        return 0x5000;
    case UINT64_C(0xfff0000000000000):
        return 0x9000;
    case UINT64_C(0x7ff8000000000000):
        return 0x11000;
    default:
        assert(false);
        return 0;
    }
}

struct test_case {
    unsigned operation;
    uint64_t a, b, c, result;
    uint32_t raised;
};

static void check(bool single, const test_case& test, unsigned initial)
{
    bool estimate = test.operation == 24 || test.operation == 26;
    unsigned primary = (single || test.operation == 24) ? 59 : 63;
    if (test.operation == 12 || test.operation == 26)
        primary = 63;
    unsigned opcode = (primary << 26) | (4 << 21) | (test.operation << 1);
    if (test.operation != 25 && test.operation != 12 && !estimate && test.operation != 22)
        opcode |= 1 << 16;
    if (test.operation != 25)
        opcode |= 2 << 11;
    else
        opcode |= 1 << 16;
    if (test.operation == 25 || test.operation >= 28)
        opcode |= 3 << 6;
    uint64_t output = UINT64_C(0x7ff0000012345678);
    uint32_t actual_status = initial;
    int handled = romaw_ppc_arithmetic_value(opcode, test.a, test.b, test.c, &actual_status, &output);
    if (!handled)
        handled = romaw_ppc_estimate_value(opcode, test.b, &actual_status, &output);
    if (!handled)
        handled = romaw_ppc_fused_value(opcode, test.a, test.b, test.c, &actual_status, &output);
    bool inhibited =
        ((test.raised & 0x01f80700) && (initial & 0x80)) || ((test.raised & 0x04000000) && (initial & 0x10));
    uint32_t status = initial & ~(estimate || inhibited ? 0x60000u : 0x7f000u);
    if (test.raised & ~initial)
        status |= 0x80000000;
    status = summary(status | test.raised);
    if (!inhibited)
        status = (status & ~0x1f000u) | classification(test.result);
    uint64_t expected = inhibited ? UINT64_C(0x7ff0000012345678) : test.result;
    if (handled != (inhibited ? 2 : 1) || output != expected || actual_status != status) {
        fprintf(stderr, "case %u op%u single%u FPSCR=%08x: got %016llx/%08x/%d expected %016llx/%08x\n", count,
                test.operation, single, initial, (unsigned long long)output, actual_status, handled,
                (unsigned long long)expected, status);
        assert(false);
    }
    if (count % 17 == 0)
        for (unsigned destination = 1; destination <= 4; ++destination)
            for (unsigned rc = 0; rc < 2; ++rc)
                for (unsigned jit = 0; jit < 2; ++jit) {
                    unsigned code = (opcode & ~0x03e00001u) | (destination << 21) | rc;
                    for (unsigned i = 0; i < 4; ++i)
                        memory[i] = code >> (24 - i * 8);
                    memory[4] = 0x18;
                    ROMlib_ppc_registers registers = {};
                    ROMlib_ppc_environment environment = {};
                    environment.memory = map;
                    environment.instruction_limit = 2;
                    environment.execution_flags = jit ? ROMLIB_PPC_EXEC_JIT : 0;
                    registers.fpscr = initial;
                    registers.cr = 0x13579bdf;
                    memcpy(&registers.fpr[1], &test.a, 8);
                    memcpy(&registers.fpr[2], &test.b, 8);
                    memcpy(&registers.fpr[3], &test.c, 8);
                    uint64_t prior;
                    memcpy(&prior, &registers.fpr[destination], 8);
                    assert(ROMlib_ppc_execute(&registers, &environment));
                    memcpy(&output, &registers.fpr[destination], 8);
                    assert(output == (inhibited ? prior : test.result) && registers.fpscr == status);
                    assert(registers.cr ==
                           (rc ? (0x13579bdf & ~0x0f000000u) | ((status >> 4) & 0x0f000000u) : 0x13579bdf));
                }
    ++count;
}

int main(void)
{
    const uint64_t one = UINT64_C(0x3ff0000000000000);
    const uint64_t negative_one = UINT64_C(0xbff0000000000000);
    const uint64_t infinity = UINT64_C(0x7ff0000000000000);
    const uint64_t nan = UINT64_C(0x7ff8000000000000);
    fenv_t original;
    assert(!fegetenv(&original));
    assert(!fesetround(FE_DOWNWARD));
    assert(!feclearexcept(FE_ALL_EXCEPT));
    assert(!feraiseexcept(FE_INVALID));
    for (unsigned single = 0; single < 2; ++single)
        for (unsigned largest = 0; largest < 2; ++largest)
            for (unsigned negative = 0; negative < 2; ++negative) {
                uint64_t sign = uint64_t(negative) << 63;
                uint64_t tiny = single    ? largest ? UINT64_C(0x380fffffc0000000) : UINT64_C(0x36a0000000000000)
                                   : largest ? UINT64_C(0x000fffffffffffff)
                                          : 1;
                tiny |= sign;
                const test_case tests[] = {
                    {21, tiny, one, 0, one, 0},
                    {21, one, tiny, 0, one, 0},
                    {20, tiny, one, 0, negative_one, 0},
                    {25, tiny, 0, one, sign, 0},
                    {25, one, 0, tiny, sign, 0},
                    {25, tiny, 0, infinity, nan, 0x00100000},
                    {18, one, tiny, 0, infinity | sign, 0x04000000},
                    {18, tiny, one, 0, sign, 0},
                    {18, tiny, tiny, 0, nan, 0x00200000},
                    {22, 0, tiny, 0, sign, 0},
                    {29, tiny, one, one, one, 0},
                    {29, one, tiny, one, one, 0},
                    {29, one, one, tiny, one, 0},
                    {29, tiny, one, infinity, nan, 0x00100000},
                    {28, one, tiny, one, one, 0},
                    {30, one, tiny, one, negative_one, 0},
                    {31, one, tiny, one, negative_one, 0},
                };
                for (const auto& test : tests)
                    for (unsigned rounding = 0; rounding < 4; ++rounding)
                        for (unsigned enables : {0u, 0x10u, 0x80u, 0xf8u})
                            for (unsigned old : {0u, 0x1ff80700u}) {
                                check(single, test, summary(4 | rounding | enables | old | 0x75000));
                                assert(fegetround() == FE_DOWNWARD && fetestexcept(FE_ALL_EXCEPT) == FE_INVALID);
                            }
                if (!single)
                    for (unsigned operation : {12u, 24u, 26u})
                        for (unsigned rounding = 0; rounding < 4; ++rounding)
                            for (unsigned enables : {0u, 0x10u, 0x80u, 0xf8u}) {
                                test_case test = {operation,
                                                  0,
                                                  tiny,
                                                  0,
                                                  operation == 12 ? sign : infinity | sign,
                                                  operation == 12 ? 0u : 0x04000000u};
                                check(false, test, summary(4 | rounding | enables | 0x75000));
                            }
            }
    assert(!fesetenv(&original));
    printf("%u 750 NI denormal operand/result/exception/alias cases passed\n", count);
}
