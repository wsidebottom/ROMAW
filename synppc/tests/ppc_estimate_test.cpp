/*+===================================================================
  File:      ppc_estimate_test.cpp

  Summary:   Implements ppc estimate test support for synppc PowerPC execution and fragment loading.

  Classes:   Used: ROMlib_ppc_environment, ROMlib_ppc_registers

  Functions: main

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Original FRSQRTE vectors from PowerPC PEM instruction semantics. */
#include "synppc/ppc.h"
#include <assert.h>
#include <cmath>
#include <fenv.h>
#include <stdio.h>
#include <string.h>

static unsigned char memory[8];
static unsigned count;
static unsigned direct_count, exceptional_count;
static ROMlib_ppc_translation_cache* cache;

static void* map(void*, uint32_t address, size_t size, int)
{
    return address <= sizeof memory && size <= sizeof memory - address ? memory + address : NULL;
}

static void word(uint32_t opcode)
{
    for (unsigned i = 0; i < 4; ++i)
        memory[i] = opcode >> (24 - 8 * i);
    memory[4] = 0x18;
}

static uint64_t bits(const double& value)
{
    uint64_t result;
    memcpy(&result, &value, 8);
    return result;
}

static double value(uint64_t bits)
{
    double result;
    memcpy(&result, &bits, 8);
    return result;
}

static uint32_t summary(uint32_t status)
{
    status &= ~0x60000000u;
    if (status & 0x01f80700)
        status |= 0x20000000;
    if (((status & 0x20000000) && (status & 0x80)) || ((status & 0x10000000) && (status & 0x40)) ||
        ((status & 0x08000000) && (status & 0x20)) || ((status & 0x04000000) && (status & 0x10)) ||
        ((status & 0x02000000) && (status & 8)))
        status |= 0x40000000;
    return status;
}

static uint32_t classify(uint64_t result, bool single)
{
    uint64_t mag = result & UINT64_C(0x7fffffffffffffff);
    if (mag > UINT64_C(0x7ff0000000000000))
        return 0x11000;
    if (mag == UINT64_C(0x7ff0000000000000))
        return result >> 63 ? 0x9000 : 0x5000;
    if (!mag)
        return result >> 63 ? 0x12000 : 0x2000;
    return (result >> 63 ? 0x8000 : 0x4000) | (single && mag < UINT64_C(0x3810000000000000) ? 0x10000 : 0);
}

static void check(uint64_t input, uint32_t raised, uint64_t result, bool estimate, uint32_t initial, bool alias,
                  bool rc, bool reciprocal = false)
{
    unsigned dst = alias ? 2 : 3;
    uint32_t opcode = ((reciprocal ? 59u : 63u) << 26) | (dst << 21) | (2 << 11) | ((reciprocal ? 24 : 26) << 1) | rc;
    word(opcode);
    ROMlib_ppc_registers r = {};
    ROMlib_ppc_environment env = {};
    env.memory = map;
    env.instruction_limit = 4;
    uint64_t guard = UINT64_C(0x7ff0000000000001);
    memcpy(&r.fpr[0], &guard, 8);
    memcpy(&r.fpr[2], &input, 8);
    r.fpr[3] = 10;
    uint64_t prior = bits(r.fpr[dst]);
    r.cr = 0x13579bdf;
    r.fpscr = initial;
    auto native_registers = r;
    assert(ROMlib_ppc_execute(&r, &env));
    assert(bits(r.fpr[0]) == guard);
    if (!alias)
        assert(bits(r.fpr[2]) == input);
    uint32_t expected = initial & ~0x60000u;
    if (raised & ~initial)
        expected |= 0x80000000;
    expected = summary(expected | raised);
    bool inhibited = ((raised & 0x01000200) && (initial & 0x80)) || ((raised & 0x04000000) && (initial & 0x10));
    uint64_t actual = bits(r.fpr[dst]);
    if (inhibited)
        assert(actual == prior);
    else {
        if (estimate) {
            assert(actual > 0 && actual < UINT64_C(0x7ff0000000000000));
            double reference = 1 / (reciprocal ? value(input) : std::sqrt(value(input)));
            // The UISA permits 1/256 (FRES) or 1/32 (FRSQRTE). Require half.
            assert(std::fabs((value(actual) - reference) / reference) < (reciprocal ? 1.0 / 512 : 1.0 / 64));
        }
        else
            assert(actual == result);
        bool scaled = ((raised & 0x10000000) && (initial & 0x40)) || ((raised & 0x08000000) && (initial & 0x20));
        expected = (expected & ~0x1f000u) | classify(actual, reciprocal && !scaled);
    }
    if (r.fpscr != expected)
        fprintf(stderr, "%s input=%016llx status=%08x: %08x != %08x\n", reciprocal ? "FRES" : "FRSQRTE",
                (unsigned long long)input, initial, r.fpscr, expected);
    assert(r.fpscr == expected);
    uint32_t cr = rc ? (0x13579bdf & ~0x0f000000u) | ((expected >> 4) & 0x0f000000) : 0x13579bdf;
    assert(r.cr == cr && fegetround() == FE_DOWNWARD);
    if (cache) {
        env.linear_memory = memory;
        env.linear_size = sizeof memory;
        env.translation_cache = cache;
        env.execution_flags = ROMLIB_PPC_EXEC_JIT;
        assert(ROMlib_ppc_translate_range(cache, memory, 0, 4) <= 1);
        assert(ROMlib_ppc_execute(&native_registers, &env));
        if (memcmp(&native_registers, &r, sizeof r))
            fprintf(stderr, "native %s input=%016llx RN=%u got%016llx/%08x expected%016llx/%08x\n",
                    reciprocal ? "FRES" : "FRSQRTE", (unsigned long long)input, initial & 3,
                    (unsigned long long)bits(native_registers.fpr[dst]), native_registers.fpscr,
                    (unsigned long long)bits(r.fpr[dst]), r.fpscr);
        assert(!memcmp(&native_registers, &r, sizeof r));
        assert(env.native_instructions == 1 && env.native_cache_hits == 1 && !env.native_compilations &&
               env.native_kernel_instructions <= 1);
        if (estimate && (input >> 52) && (input >> 52) < 0x7ff) {
            if (env.native_kernel_instructions)
                fprintf(stderr, "unexpected estimate tail %s input=%016llx status=%08x result=%016llx\n",
                        reciprocal ? "FRES" : "FRSQRTE", (unsigned long long)input, initial,
                        (unsigned long long)bits(r.fpr[dst]));
            assert(!env.native_kernel_instructions);
        }
        (env.native_kernel_instructions ? exceptional_count : direct_count)++;
        assert(fegetround() == FE_DOWNWARD);
    }
    ++count;
}

int main(void)
{
    fenv_t original;
    assert(!fegetenv(&original) && !fesetround(FE_DOWNWARD));
    cache = ROMlib_ppc_jit_available() ? ROMlib_ppc_translation_cache_create() : nullptr;
    assert(!ROMlib_ppc_jit_available() || cache);

    const struct {
        uint64_t input, result;
        uint32_t raised;
    } cases[] = {
        {0, UINT64_C(0x7ff0000000000000), 0x04000000},
        {UINT64_C(0x8000000000000000), UINT64_C(0xfff0000000000000), 0x04000000},
        {UINT64_C(0x7ff0000000000000), 0, 0},
        {UINT64_C(0xfff0000000000000), UINT64_C(0x7ff8000000000000), 0x200},
        {UINT64_C(0xbff0000000000000), UINT64_C(0x7ff8000000000000), 0x200},
        {UINT64_C(0x8000000000000001), UINT64_C(0x7ff8000000000000), 0x200},
        {UINT64_C(0x7ff0000000000001), UINT64_C(0x7ff8000000000001), 0x01000000},
        {UINT64_C(0xfff123456789abcd), UINT64_C(0xfff923456789abcd), 0x01000000},
        {UINT64_C(0x7ff923456789abcd), UINT64_C(0x7ff923456789abcd), 0},
        {UINT64_C(0xfff923456789abcd), UINT64_C(0xfff923456789abcd), 0},
        {UINT64_C(0x3ff0000000000000), UINT64_C(0x3ff0000000000000), 0},
        {UINT64_C(0x4010000000000000), UINT64_C(0x3fe0000000000000), 0},
    };

    const uint32_t seeds[] = {0x75000, 0x75090, 0x01075080, 0x04075010, 0x00075280, 0x1e075078};
    for (auto c : cases)
        for (uint32_t seed : seeds)
            for (unsigned rn = 0; rn < 4; ++rn)
                for (unsigned alias = 0; alias < 2; ++alias)
                    for (unsigned rc = 0; rc < 2; ++rc)
                        check(c.input, c.raised, c.result, false, summary(seed | rn), alias, rc);

    const struct {
        uint64_t input, result;
        uint32_t raised;
    } reciprocals[] = {
        {0, UINT64_C(0x7ff0000000000000), 0x04000000},
        {UINT64_C(0x8000000000000000), UINT64_C(0xfff0000000000000), 0x04000000},
        {UINT64_C(0x7ff0000000000000), 0, 0},
        {UINT64_C(0xfff0000000000000), UINT64_C(0x8000000000000000), 0},
        {UINT64_C(0x7ff0000000000001), UINT64_C(0x7ff8000000000001), 0x01000000},
        {UINT64_C(0xfff123456789abcd), UINT64_C(0xfff923456789abcd), 0x01000000},
        {UINT64_C(0x7ff923456789abcd), UINT64_C(0x7ff923456789abcd), 0},
        {UINT64_C(0x3ff0000000000000), UINT64_C(0x3ff0000000000000), 0},
        {UINT64_C(0xc010000000000000), UINT64_C(0xbfd0000000000000), 0},
    };

    for (auto c : reciprocals)
        for (uint32_t seed : seeds)
            for (unsigned rn = 0; rn < 4; ++rn)
                for (unsigned alias = 0; alias < 2; ++alias)
                    for (unsigned rc = 0; rc < 2; ++rc)
                        check(c.input, c.raised, c.result, false, summary(seed | rn), alias, rc, true);
    // Exact powers cross every binary32 normal/subnormal/scaled boundary.
    for (int exponent = -1074; exponent <= 1023; ++exponent)
        for (unsigned negative = 0; negative < 2; ++negative)
            for (unsigned rn = 0; rn < 4; ++rn)
                for (unsigned enables = 0; enables < 4; ++enables)
                    for (unsigned ni = 0; ni < 2; ++ni) {
                        // The 750's NI denormal-input policy is a separate open audit.
                        if (ni && exponent < -1022)
                            continue;
                        uint64_t sign = uint64_t(negative) << 63;
                        uint64_t input = sign | (exponent >= -1022 ? uint64_t(exponent + 1023) << 52
                                                                   : UINT64_C(1) << (exponent + 1074));
                        int power = -exponent;
                        uint32_t raised = 0;
                        uint64_t output;
                        if (power > 127) {
                            raised = 0x10000000;
                            if (enables & 1)
                                output = uint64_t(power - 192 + 1023) << 52;
                            else
                                output = rn == 0 || (rn == 2 && !negative) || (rn == 3 && negative)
                                             ? UINT64_C(0x7ff0000000000000)
                                             : UINT64_C(0x47efffffe0000000);
                        }
                        else if (power < -126 && (enables & 2)) {
                            raised = 0x08000000;
                            output = uint64_t(power + 192 + 1023) << 52;
                        }
                        else if (power < -126 && ni) {
                            raised = 0x08000000;
                            output = 0;
                        }
                        else if (power < -149) {
                            raised = 0x08000000;
                            output = (rn == 2 && !negative) || (rn == 3 && negative) ? UINT64_C(0x36a0000000000000) : 0;
                        }
                        else
                            output = uint64_t(power + 1023) << 52;
                        uint32_t initial =
                            0x75000 | rn | (ni ? 4 : 0) | (enables & 1 ? 0x40 : 0) | (enables & 2 ? 0x20 : 0);
                        if (exponent & 4)
                            initial = summary(initial | raised); // Repeat a sticky cause.
                        if (exponent & 8)
                            initial = summary(initial | 0x02000000); // Preserve sticky XX.
                        check(input, raised, sign | output, false, initial, exponent & 1, negative, true);
                    }
    // Tiny before rounding, including a disabled result rounded to min-normal.
    for (unsigned negative = 0; negative < 2; ++negative)
        for (unsigned rn = 0; rn < 4; ++rn)
            for (unsigned enabled = 0; enabled < 2; ++enabled)
                for (unsigned ni = 0; ni < 2; ++ni) {
                    bool upward = rn == 0 || (rn == 2 && !negative) || (rn == 3 && negative);
                    uint64_t result = enabled  ? upward ? UINT64_C(0x4410000000000000) : UINT64_C(0x440fffffe0000000)
                                       : upward ? UINT64_C(0x3810000000000000)
                                               : UINT64_C(0x380fffffc0000000);
                    uint64_t sign = uint64_t(negative) << 63;
                    check(sign | UINT64_C(0x47d0000000000001), 0x08000000, sign | (ni && !enabled ? 0 : result), false,
                          0x75000 | rn | (ni ? 4 : 0) | (enabled ? 0x20 : 0), enabled, negative, true);
                }
    uint64_t random = UINT64_C(0x123456789abcdef);
    for (unsigned exponent = 0; exponent < 2047; ++exponent)
        for (unsigned pattern = 0; pattern < 3; ++pattern) {
            random ^= random << 13;
            random ^= random >> 7;
            random ^= random << 17;
            uint64_t fraction = pattern == 0   ? 0
                                : pattern == 1 ? UINT64_C(0xfffffffffffff)
                                               : random & UINT64_C(0xfffffffffffff);
            uint64_t input = (uint64_t(exponent) << 52) | fraction;
            if (!input)
                input = 1;
            check(input, 0, 0, true, 0x75000 | (exponent & 3), pattern & 1, exponent & 1);
            if (exponent >= 897 && exponent <= 1148)
                check(input, 0, 0, true, 0x75000 | (exponent & 3), pattern & 1, exponent & 1, true);
        }
    for (unsigned reciprocal = 0; reciprocal < 2; ++reciprocal)
        for (unsigned bit = 0; bit < 32; ++bit)
            if ((1u << bit) & 0x001f07c0) {
                word(((reciprocal ? 59u : 63u) << 26) | ((reciprocal ? 24 : 26) << 1) | (1u << bit));
                ROMlib_ppc_registers r = {};
                ROMlib_ppc_environment env = {};
                env.memory = map;
                env.instruction_limit = 4;
                r.fpscr = 0x12340000;
                assert(!ROMlib_ppc_execute(&r, &env));
                assert(strstr(env.error, "illegal PowerPC instruction"));
                assert(r.fpscr == 0x12340000);
            }
    assert(!fesetenv(&original));
    ROMlib_ppc_translation_cache_destroy(cache);
    printf("%u reciprocal estimate special/status/alias and full-exponent accuracy cases passed\n", count);
    printf("Native estimate paths: %u direct, %u exceptional\n", direct_count, exceptional_count);
}
