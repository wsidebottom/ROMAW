/*+===================================================================
  File:      ppc_arithmetic_test.cpp

  Summary:   Implements ppc arithmetic test support for synppc PowerPC execution and fragment loading.

  Classes:   Used: ROMlib_ppc_environment, ROMlib_ppc_registers, ppc_fp_result

  Functions: ppc_arithmetic_check

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../src/ppc-arithmetic.h"
#include "synppc/ppc.h"
extern "C" {
#include "../third_party/softfloat3/source/include/softfloat.h"
}
#include <assert.h>
#include "ppc_model_checks.h"
#include <stdio.h>
#include <fenv.h>
#include <string.h>
#include <initializer_list>

static unsigned char memory[8];

static void* map(void*, uint32_t address, size_t length, int)
{
    return address <= sizeof memory && length <= sizeof memory - address ? memory + address : NULL;
}

static void reserved_fields()
{
    for (unsigned primary : {59u, 63u})
        for (unsigned field : {6u, 16u})
            for (unsigned value = 1; value < 32; ++value) {
                uint32_t opcode = (primary << 26) | (22 << 1) | (value << field);
                for (unsigned i = 0; i < 4; ++i)
                    memory[i] = opcode >> (24 - 8 * i);
                ROMlib_ppc_environment env = {};
                ROMlib_ppc_registers r = {};
                env.memory = map;
                env.instruction_limit = 4;
                r.fpr[0] = 17;
                assert(!ROMlib_ppc_execute(&r, &env));
                assert(strstr(env.error, "illegal PowerPC instruction"));
                assert(r.fpr[0] == 17);
            }
}

static void exception_status()
{
    const uint64_t infinity = UINT64_C(0x7ff0000000000000);
    const uint64_t one = UINT64_C(0x3ff0000000000000);
    const uint64_t snan = UINT64_C(0x7ff0000020000000);
    const uint64_t qnan = UINT64_C(0x7ff8000040000000);
    const uint64_t sign = UINT64_C(0x8000000000000000);

    const struct {
        unsigned operation;
        uint64_t a, b;
        uint32_t raised, classification;
    } cases[] = {{21, infinity, infinity | sign, 0x00800000, 0x11000},
                 {20, infinity, infinity, 0x00800000, 0x11000},
                 {25, infinity, 0, 0x00100000, 0x11000},
                 {25, sign, infinity, 0x00100000, 0x11000},
                 {18, infinity, infinity, 0x00400000, 0x11000},
                 {18, 0, sign, 0x00200000, 0x11000},
                 {22, 0, one | sign, 0x00000200, 0x11000},
                 {22, 0, infinity | sign, 0x00000200, 0x11000},
                 {21, qnan, snan, 0x01000000, 0x11000},
                 {20, snan, one, 0x01000000, 0x11000},
                 {25, one, snan, 0x01000000, 0x11000},
                 {18, snan, 0, 0x01000000, 0x11000},
                 {22, 0, snan, 0x01000000, 0x11000},
                 {12, 0, snan, 0x01000000, 0x11000},
                 {18, one, 0, 0x04000000, 0x05000},
                 {18, one, sign, 0x04000000, 0x09000},
                 {18, infinity, 0, 0, 0x05000}, /* Infinity/zero is not ZX. */
                 {21, one, one, 0, 0x04000},    /* VE does not suppress valid FPRF updates. */
                 {21, qnan, one, 0, 0x11000},
                 {22, 0, qnan | sign, 0, 0x11000},
                 {22, 0, sign, 0, 0x12000}};

    for (const auto& test : cases)
        for (unsigned single : {0u, 1u}) {
            if (single && test.operation == 12)
                continue;
            for (unsigned mode = 0; mode < 4; ++mode)
                for (unsigned destination : {1u, 2u, 3u, 4u})
                    for (unsigned enabled : {0u, 1u})
                        for (unsigned record : {0u, 1u}) {
                            uint32_t opcode =
                                ((single ? 59u : 63u) << 26) | (destination << 21) | (test.operation << 1) | record;
                            if (test.operation == 12 || test.operation == 22)
                                opcode |= 2 << 11;
                            else
                                opcode |= (1 << 16) | (test.operation == 25 ? 3 << 6 : 2 << 11);
                            for (unsigned i = 0; i < 4; ++i)
                                memory[i] = opcode >> (24 - 8 * i);
                            memory[4] = 0x18;
                            ROMlib_ppc_registers r = {};
                            ROMlib_ppc_environment env = {};
                            env.memory = map;
                            env.instruction_limit = 4;
                            uint32_t enable = enabled ? 0x90 : 0; // VE and ZE.
                            r.fpscr = mode | enable | 0x00070000;
                            r.cr = 0x13579bdf;
                            const uint64_t inputs[] = {0, test.a, test.operation == 25 ? snan : test.b,
                                                       test.operation == 25 ? test.b : snan,
                                                       UINT64_C(0x0123456789abcdef)};
                            for (unsigned i = 0; i < 5; ++i)
                                memcpy(&r.fpr[i], &inputs[i], sizeof inputs[i]);
                            assert(ROMlib_ppc_execute(&r, &env));
                            uint64_t result;
                            memcpy(&result, &r.fpr[destination], sizeof result);
                            bool inhibited = test.raised && enabled;
                            if (inhibited)
                                assert(result == inputs[destination]);
                            else if (test.classification == 0x11000)
                                assert((result & UINT64_C(0x7ff8000000000000)) == UINT64_C(0x7ff8000000000000));
                            uint32_t status = mode | enable | test.raised;
                            if (test.raised) {
                                status |= 0x80000000;
                                if (test.raised != 0x04000000)
                                    status |= 0x20000000;
                                if (enabled)
                                    status |= 0x40000000;
                            }
                            status |= inhibited ? 0x10000 : test.classification;
                            if (r.fpscr != status)
                                fprintf(stderr, "Operation %u status %08x expected %08x\n", test.operation, r.fpscr,
                                        status);
                            assert(r.fpscr == status);
                            assert(r.cr ==
                                   (record ? (0x13579bdf & ~0x0f000000u) | ((status >> 4) & 0x0f000000u) : 0x13579bdf));
                        }
        }
    uint32_t status = 0x00800080; // Existing VXISI, FX clear, VE enabled.
    assert(!romaw_ppc_arithmetic_exceptions((63u << 26) | (21 << 1), infinity, infinity | sign, 0, &status));
    assert(status == 0x60800080); // No new sticky cause: FX remains clear.
}

static void fused_exception_status()
{
    const uint64_t inf = UINT64_C(0x7ff0000000000000);
    const uint64_t one = UINT64_C(0x3ff0000000000000);
    const uint64_t snan = UINT64_C(0x7ff0000020000000);
    const uint64_t qnan = UINT64_C(0x7ff8000040000000);
    const uint64_t sign = UINT64_C(0x8000000000000000);

    const struct {
        uint64_t a, b, c;
        uint32_t add, subtract;
    } cases[] = {{inf, one, 0, 0x00100000, 0x00100000},     {0, snan, inf | sign, 0x01100000, 0x01100000},
                 {inf, qnan, 0, 0x00100000, 0x00100000},    {snan, inf, one, 0x01000000, 0x01000000},
                 {qnan, one, snan, 0x01000000, 0x01000000}, {inf, inf, one, 0, 0x00800000},
                 {inf, inf, one | sign, 0x00800000, 0},     {inf, qnan, one, 0, 0},
                 {qnan, snan, one, 0x01000000, 0x01000000}, {one, one, one, 0, 0}};

    for (const auto& test : cases)
        for (unsigned primary : {59u, 63u})
            for (unsigned operation = 28; operation < 32; ++operation)
                for (unsigned mode = 0; mode < 4; ++mode)
                    for (unsigned destination : {1u, 2u, 3u, 4u})
                        for (unsigned enabled : {0u, 1u})
                            for (unsigned record : {0u, 1u}) {
                                uint32_t opcode = (primary << 26) | (destination << 21) | (1 << 16) | (2 << 11) |
                                                  (3 << 6) | (operation << 1) | record;
                                for (unsigned i = 0; i < 4; ++i)
                                    memory[i] = opcode >> (24 - 8 * i);
                                memory[4] = 0x18;
                                ROMlib_ppc_environment env = {};
                                env.memory = map;
                                env.instruction_limit = 4;
                                ROMlib_ppc_registers r = {};
                                const uint64_t input[] = {0, test.a, test.b, test.c, UINT64_C(0x0123456789abcdef)};
                                for (unsigned i = 0; i < 5; ++i)
                                    memcpy(&r.fpr[i], &input[i], sizeof input[i]);
                                r.fpscr = mode | 0x70000 | (enabled ? 0x80 : 0);
                                r.cr = 0x13579bdf;
                                assert(ROMlib_ppc_execute(&r, &env));
                                uint32_t raised = (operation & 1) ? test.add : test.subtract;
                                bool inhibited = raised && enabled;
                                uint64_t result;
                                memcpy(&result, &r.fpr[destination], sizeof result);
                                uint32_t classification;
                                if (inhibited) {
                                    assert(result == input[destination]);
                                    classification = 0x10000;
                                }
                                else if (raised || (test.a & ~sign) > inf || (test.b & ~sign) > inf ||
                                         (test.c & ~sign) > inf) {
                                    assert((result & UINT64_C(0x7ff8000000000000)) == UINT64_C(0x7ff8000000000000));
                                    classification = 0x11000;
                                }
                                else if (test.a == one) {
                                    bool negative = operation >= 30;
                                    uint64_t expected = (operation & 1) ? UINT64_C(0x4000000000000000) : 0;
                                    if (!(operation & 1) && mode == 3)
                                        negative = !negative;
                                    expected |= negative ? sign : 0;
                                    assert(result == expected);
                                    classification =
                                        (operation & 1) ? (negative ? 0x8000 : 0x4000) : (negative ? 0x12000 : 0x2000);
                                }
                                else {
                                    uint64_t expected =
                                        inf | ((((test.a ^ test.c) >> 63) ^ (operation >= 30)) ? sign : 0);
                                    assert(result == expected);
                                    classification = (expected & sign) ? 0x9000 : 0x5000;
                                }
                                uint32_t status = mode | (enabled ? 0x80 : 0) | classification;
                                status |= raised ? (0xa0000000 | raised | (enabled ? 0x40000000 : 0)) : 0;
                                assert(r.fpscr == status);
                                assert(r.cr == (record ? (0x13579bdf & ~0x0f000000u) | ((status >> 4) & 0x0f000000u)
                                                       : 0x13579bdf));
                            }
    uint32_t status = 0x00100080;
    assert(!romaw_ppc_arithmetic_exceptions((63u << 26) | (29 << 1), inf, one, 0, &status));
    assert(status == 0x60100080);
}

static void raw_nan_payloads()
{
    const uint64_t qnan = UINT64_C(0xfff8000040000123);
    const uint64_t snan = UINT64_C(0x7ff0000060000456);
    const unsigned operations[] = {12, 18, 20, 21, 22, 25};
    const uint64_t one = UINT64_C(0x3ff0000000000000);
    const uint64_t first[] = {qnan, snan, qnan, snan, one, one, qnan, snan};
    const uint64_t second[] = {one, one, snan, snan, qnan, snan, qnan, qnan};
    unsigned count = 0;
    for (unsigned operation : operations)
        for (unsigned single = 0; single < 2; ++single)
            for (unsigned selection = 0; selection < 8; ++selection)
                for (unsigned enabled = 0; enabled < 2; ++enabled)
                    for (unsigned mode = 0; mode < 4; ++mode) {
                        if (single && operation == 12)
                            continue;
                        bool unary = operation == 12 || operation == 22;
                        uint64_t a = first[selection], b = second[selection];
                        if (unary && a == one)
                            continue;
                        if (unary)
                            b = a;
                        uint64_t selected = (a == one ? b : a) | UINT64_C(0x0008000000000000);
                        if (operation == 12)
                            selected &= ~UINT64_C(0x1fffffff);
                        bool signaling = a == snan || (!unary && b == snan);
                        unsigned destination = 1 + count % 4;
                        unsigned opcode =
                            ((single ? 59u : 63u) << 26) | (destination << 21) | (operation << 1) | (count & 1);
                        if (unary)
                            opcode |= 2 << 11;
                        else
                            opcode |= (1 << 16) | (operation == 25 ? 3 << 6 : 2 << 11);
                        for (unsigned i = 0; i < 4; ++i)
                            memory[i] = opcode >> (24 - 8 * i);
                        memory[4] = 0x18;
                        ROMlib_ppc_environment env = {};
                        env.memory = map;
                        env.instruction_limit = 4;
                        ROMlib_ppc_registers r = {};
                        r.fpscr = 0x70000 | mode | (enabled ? 0x80 : 0);
                        const uint64_t values[] = {0, a, b, operation == 25 ? b : snan, 0x12345678};
                        for (unsigned i = 0; i < 5; ++i)
                            memcpy(&r.fpr[i], &values[i], 8);
                        assert(ROMlib_ppc_execute(&r, &env));
                        uint64_t result;
                        memcpy(&result, &r.fpr[destination], 8);
                        bool inhibited = signaling && enabled;
                        assert(result == (inhibited ? values[destination] : selected));
                        uint32_t status = mode | (enabled ? 0x80 : 0) | (inhibited ? 0x10000 : 0x11000);
                        if (signaling)
                            status |= 0xa1000000 | (enabled ? 0x40000000 : 0);
                        assert(r.fpscr == status);
                        ++count;
                    }
    printf("%u full-register arithmetic NaN payload cases passed\n", count);
}

static void instruction_result(unsigned operation, unsigned single, unsigned mode, uint64_t a, uint64_t b,
                               uint64_t expected, unsigned count)
{
    static const unsigned extended[] = {21, 20, 25, 18, 22, 12};
    unsigned destination = 1 + count % 3;
    unsigned primary = single && operation != ppc_fp_round_single ? 59 : 63;
    unsigned opcode = (primary << 26) | (destination << 21) | (extended[operation] << 1) | (count & 1);
    if (operation == ppc_fp_sqrt || operation == ppc_fp_round_single)
        opcode |= 1 << 11;
    else
        opcode |= (1 << 16) | (2 << (operation == ppc_fp_multiply ? 6 : 11));
    for (unsigned i = 0; i < 4; ++i)
        memory[i] = opcode >> (24 - 8 * i);
    memory[4] = 0x18;
    ROMlib_ppc_environment env = {};
    ROMlib_ppc_registers r = {};
    env.memory = map;
    env.instruction_limit = 4;
    r.fpscr = mode;
    r.cr = 0x13579bdf;
    if (single && operation != ppc_fp_round_single) {
        a = romaw_ppc_expand_single(a);
        b = romaw_ppc_expand_single(b);
    }
    memcpy(&r.fpr[1], &a, sizeof a);
    memcpy(&r.fpr[2], &b, sizeof b);
    assert(ROMlib_ppc_execute(&r, &env));
    uint64_t actual;
    memcpy(&actual, &r.fpr[destination], sizeof actual);
    if (single || operation == ppc_fp_round_single)
        expected = romaw_ppc_expand_single(expected);
    if (actual != expected)
        fprintf(stderr, "Instruction case %u: %08x got %llx expected %llx\n", count, opcode, (unsigned long long)actual,
                (unsigned long long)expected);
    assert(actual == expected);
    assert((r.fpscr & 3) == mode);
    uint32_t expected_cr = count & 1 ? (0x13579bdf & ~0x0f000000u) | ((r.fpscr >> 4) & 0x0f000000u) : 0x13579bdf;
    assert(r.cr == expected_cr);
}

int ppc_arithmetic_check(FILE* cases)
{
    unsigned operation, single, mode, flags, count = 0;
    unsigned long long a, b, expected;
    extF80_roundingPrecision = 80;
    softfloat_roundingMode = softfloat_round_odd;
    softfloat_detectTininess = softfloat_tininess_afterRounding;
    softfloat_exceptionFlags = softfloat_flag_invalid;
    fenv_t original;
    assert(!fegetenv(&original));
    assert(!fesetround(FE_DOWNWARD));
    while (fscanf(cases, "%u %u %u %llx %llx %llx %u", &operation, &single, &mode, &a, &b, &expected, &flags) == 7) {
        ppc_fp_result result = romaw_ppc_arithmetic((ppc_fp_operation)operation, single, mode, a, b);
        if (result.bits != expected || result.flags != flags) {
            fprintf(stderr,
                    "Arithmetic case %u: op=%u single=%u RN=%u a=%llx b=%llx "
                    "result=%llx/%u expected=%llx/%u\n",
                    count, operation, single, mode, a, b, (unsigned long long)result.bits, result.flags, expected,
                    flags);
            return 1;
        }
        assert(extF80_roundingPrecision == 80);
        assert(softfloat_roundingMode == softfloat_round_odd);
        assert(softfloat_detectTininess == softfloat_tininess_afterRounding);
        assert(softfloat_exceptionFlags == softfloat_flag_invalid);
        assert(fegetround() == FE_DOWNWARD);
        /* Keep the complete value/flag oracle cheap; sample independent inputs,
         every rounding direction, NaNs and register aliases through dispatch. */
        if (count % 97 < 4 || (operation == ppc_fp_round_single && count % 31 < 4) || flags & 16)
            instruction_result(operation, single, mode, a, b, expected, count);
        ++count;
    }
    assert(count > 30000);
    reserved_fields();
    exception_status();
    fused_exception_status();
    raw_nan_payloads();
    assert(!fesetenv(&original));
    printf("%u rational arithmetic/flags and integer-root cases passed\n", count);
    return 0;
}
