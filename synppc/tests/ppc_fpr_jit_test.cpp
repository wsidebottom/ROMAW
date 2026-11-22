/*+===================================================================
  File:      ppc_fpr_jit_test.cpp

  Summary:   Implements ppc fpr jit test support for synppc PowerPC execution and fragment loading.

  Classes:   Used: ROMlib_ppc_environment, ROMlib_ppc_registers

  Functions: main

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Original raw FPR move/sign, CR1, hot rewrite and precise-limit fixtures. */
#include "synppc/ppc.h"
#include <assert.h>
#include <fenv.h>
#include <stdio.h>
#include <string.h>

static unsigned char memory[4096];
static ROMlib_ppc_registers expected;
static unsigned services, invalid_mask, polls;
static bool invalid_test, status_program;
static const uint64_t values[] = {0,
                                  UINT64_C(0x8000000000000000),
                                  1,
                                  UINT64_C(0x000fffffffffffff),
                                  UINT64_C(0x0010000000000000),
                                  UINT64_C(0x7fefffffffffffff),
                                  UINT64_C(0x7ff0000000000000),
                                  UINT64_C(0xfff0000000000000),
                                  UINT64_C(0x7ff8123456789abc),
                                  UINT64_C(0x7ff0123456789abc),
                                  UINT64_C(0xfff0123456789abc),
                                  UINT64_C(0x3ff123456789abcd),
                                  UINT64_C(0x7ff0000000000001),
                                  UINT64_C(0xfff0000000000001)};
static constexpr unsigned value_count = sizeof values / sizeof values[0];

static void* map(void*, uint32_t address, size_t size, int)
{
    return address <= sizeof memory && size <= sizeof memory - address ? memory + address : NULL;
}

static void word(unsigned address, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        memory[address + i] = value >> (24 - i * 8);
}

static uint32_t readword(unsigned address)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value = value * 256 + memory[address + i];
    return value;
}

static uint64_t raw(const ROMlib_ppc_registers& r, unsigned reg)
{
    uint64_t value;
    memcpy(&value, &r.fpr[reg], 8);
    return value;
}

static void move(uint32_t opcode)
{
    if (((opcode >> 1) & 1023) == 64) {
        unsigned src = (opcode >> 18) & 7, dst = (opcode >> 23) & 7;
        unsigned shift = 28 - 4 * dst;
        unsigned value = (expected.fpscr >> (28 - 4 * src)) & 15;
        expected.cr = (expected.cr & ~(15u << shift)) | (value << shift);
        for (unsigned n = src * 4; n < src * 4 + 4; ++n)
            if (n == 0 || (n >= 3 && n <= 12) || (n >= 21 && n <= 23))
                expected.fpscr &= ~(0x80000000u >> n);
        bool invalid = false, enabled = false;
        for (unsigned n = 7; n <= 23; ++n)
            if ((n <= 12 || n >= 21) && (expected.fpscr & (0x80000000u >> n)))
                invalid = true;
        enabled = invalid && (expected.fpscr & 0x80);
        for (unsigned n = 3; n <= 6; ++n)
            if ((expected.fpscr & (0x80000000u >> n)) && (expected.fpscr & (0x80000000u >> (n + 22))))
                enabled = true;
        expected.fpscr = (expected.fpscr & ~0x60000000u) | (invalid ? 0x20000000 : 0) | (enabled ? 0x40000000 : 0);
        return;
    }
    unsigned source = (opcode >> 11) & 31, destination = (opcode >> 21) & 31;
    bool select = ((opcode >> 1) & 31) == 23;
    if (select) {
        uint64_t guard = raw(expected, (opcode >> 16) & 31);
        uint64_t magnitude = guard & UINT64_C(0x7fffffffffffffff);
        bool yes = (!(guard >> 63) || !magnitude) && magnitude <= UINT64_C(0x7ff0000000000000);
        if (yes)
            source = (opcode >> 6) & 31;
    }
    uint64_t value = raw(expected, source);
    switch (select ? 72 : (opcode >> 1) & 1023) {
    case 72:
        break;
    case 40:
        value ^= UINT64_C(0x8000000000000000);
        break;
    case 264:
        value &= UINT64_C(0x7fffffffffffffff);
        break;
    case 136:
        value |= UINT64_C(0x8000000000000000);
        break;
    case 583:
        value = expected.fpscr;
        break;
    default:
        assert(false);
    }
    memcpy(&expected.fpr[destination], &value, 8);
    if (opcode & 1)
        expected.cr = (expected.cr & ~0x0f000000u) | ((expected.fpscr >> 4) & 0x0f000000u);
}

static void compare(uint32_t opcode)
{
    int32_t a = expected.gpr[(opcode >> 16) & 31], b = (int16_t)opcode;
    unsigned shift = 28 - 4 * ((opcode >> 23) & 7);
    unsigned condition = (a == b ? 2 : a < b ? 8 : 4) | (expected.xer >> 31);
    expected.cr = (expected.cr & ~(15u << shift)) | (condition << shift);
}

static int service(void*, uint32_t id, ROMlib_ppc_registers* r)
{
    assert(id == 1);
    r->lr = r->pc + 4;
    if (invalid_test) {
        move(readword(0x10c));
        ++services;
        assert(!memcmp(r->fpr, expected.fpr, sizeof r->fpr));
        assert(r->fpscr == expected.fpscr && r->cr == expected.cr);
        if (services == 16)
            word(0x10c, readword(0x10c) | invalid_mask);
        return 1;
    }
    if (readword(0x100) >> 26 == 63)
        move(readword(0x100));
    else
        compare(readword(0x100));
    for (unsigned i = 1; i <= 4; ++i)
        move(readword(0x100 + i * 4));
    compare(readword(0x114));
    assert(!memcmp(r->fpr, expected.fpr, sizeof r->fpr));
    assert(!memcmp(r->gpr, expected.gpr, sizeof r->gpr));
    assert(r->cr == expected.cr && r->fpscr == expected.fpscr && r->xer == expected.xer);
    ++services;
    expected.cr = r->cr = 0x13579bdu * services;
    expected.fpscr = r->fpscr = 0x9e3779b9u * services;
    expected.xer = r->xer = ((services & 7) << 29) | 0x42;
    for (unsigned i = 0; i < 32; ++i) {
        uint64_t value = values[(services + i) % value_count];
        memcpy(&r->fpr[i], &value, 8);
        memcpy(&expected.fpr[i], &value, 8);
    }
    if (services == 12 || services == 24)
        for (unsigned i = 1; i <= 4; ++i) {
            // Change sign operation, both register fields and the record bit.
            uint32_t opcode = readword(0x100 + i * 4);
            unsigned xo = (opcode >> 1) & 1023;
            word(0x100 + i * 4,
                 opcode ^ (xo == 64                     ? 0x039c0000
                           : xo == 583                  ? 0x03e00001
                           : ((opcode >> 1) & 31) == 23 ? 0x03ffffc1
                                                        : 0x03e0f801 ^ (xo == 72 || xo == 40 ? 0xc0 : 0x300)));
        }
    if (status_program && services == 36)
        for (unsigned i = 0; i <= 4; ++i)
            if (readword(0x100 + i * 4) >> 26 == 63)
                word(0x100 + i * 4, (63u << 26) | ((i + 7) << 21) | ((i + 3) << 11) | (72 << 1));
    return 1;
}

static ROMlib_ppc_registers initial(unsigned seed)
{
    ROMlib_ppc_registers r = {};
    r.pc = 0x100;
    r.cr = 0x13579bdf;
    r.xer = 0xe0000042;
    r.fpscr = seed * 0x9e3779b9u;
    r.ctr = 48;
    for (unsigned i = 0; i < 32; ++i) {
        r.gpr[i] = i * 0x12345678u;
        uint64_t value = values[(seed + i) % value_count];
        memcpy(&r.fpr[i], &value, 8);
    }
    return r;
}

static void poll(void*)
{
    ++polls;
    assert(fegetround() == FE_DOWNWARD && fetestexcept(FE_ALL_EXCEPT) == FE_INVALID);
}

int main(void)
{
    fenv_t original;
    assert(!fegetenv(&original));
    assert(!fesetround(FE_DOWNWARD));
    assert(!feclearexcept(FE_ALL_EXCEPT));
    assert(!feraiseexcept(FE_INVALID));
    unsigned cases = 0;
    const unsigned operations[] = {64, 583, 72, 40, 264, 136, 23};
    for (unsigned operation : operations)
        for (unsigned source = 0; source < (operation == 64 ? 8u : operation == 583 ? 1u : 32u); ++source)
            for (unsigned destination = 0; destination < (operation == 64 ? 8u : 32u); ++destination)
                for (unsigned rc = 0; rc < 3; ++rc) {
                    ROMlib_ppc_registers reference = {};
                    uint64_t instructions = 0;
                    unsigned reference_polls = 0;
                    bool success = false;
                    for (unsigned mode = 0; mode < 2; ++mode) {
                        word(0x100, (11u << 26) | (1 << 23) | (1 << 16));
                        unsigned opcode = (63u << 26) | (destination << 21) | (source << 11) | (operation << 1);
                        if (operation == 23)
                            opcode =
                                (63u << 26) | (destination << 21) | (source << 16) | (2 << 11) | (3 << 6) | (23 << 1);
                        if (operation == 64)
                            opcode = (63u << 26) | (destination << 23) | (source << 18) | (64 << 1);
                        if (cases % 5 == 0)
                            word(0x100,
                                 opcode); // Also compile blocks entered at an FPR op.
                        for (unsigned i = 1; i <= 4; ++i) {
                            unsigned next = opcode;
                            if (operation == 583 && (i == 2 || i == 3))
                                next = (63u << 26) | (((destination + i) & 31) << 21) | (destination << 11) |
                                       ((i == 2 ? 72 : 40) << 1);
                            if (operation == 64) {
                                next = i == 3 ? (63u << 26) | (((destination + 1) & 7) << 23) |
                                                    (((source + 3) & 7) << 18) | (64 << 1)
                                       : i == 1 ? opcode
                                                : (63u << 26) | ((i == 2 ? 4 : 5) << 21) | (583 << 1);
                            }
                            word(0x100 + i * 4, next | (((next >> 1) & 1023) == 64 || rc == 2 ? 0 : rc ^ (i & 1)));
                        }
                        unsigned comparison_field = operation == 64 ? (destination + 4) & 7 : 6;
                        word(0x114, (11u << 26) | (comparison_field << 23) | (2 << 16) | 0x8000);
                        // Consume a live CR1 bit before the host service.
                        word(0x118, (16u << 26) | (12 << 21) | ((4 + cases % 4) << 16) | 8);
                        word(0x11c, 0x60000000);
                        word(0x120, 0x18000001);
                        word(0x124, 0x4200ffdc);
                        word(0x128, 0x18000000);
                        ROMlib_ppc_registers r = initial(cases);
                        if (cases % 67 == 0)
                            r.ctr = 160;
                        expected = r;
                        services = 0;
                        polls = 0;
                        invalid_test = false;
                        status_program = operation == 64;
                        ROMlib_ppc_environment env = {};
                        env.memory = map;
                        env.linear_memory = memory;
                        env.linear_size = sizeof memory;
                        env.service = service;
                        env.poll = poll;
                        env.execution_flags = mode ? ROMLIB_PPC_EXEC_JIT : 0;
                        env.instruction_limit = cases % 3 ? 0 : 251 + cases % 13;
                        bool result = ROMlib_ppc_execute(&r, &env);
                        assert(fegetround() == FE_DOWNWARD && fetestexcept(FE_ALL_EXCEPT) == FE_INVALID);
                        if (!mode) {
                            reference = r;
                            instructions = env.instructions;
                            success = result;
                            reference_polls = polls;
                        }
                        else {
                            assert(result == success);
                            assert(!memcmp(&r, &reference, sizeof r));
                            assert(env.instructions == instructions);
                            assert(polls == reference_polls);
                            if (ROMlib_ppc_jit_available())
                                assert(env.native_blocks && env.native_instructions);
                        }
                    }
                    ++cases;
                }
    unsigned invalid = 0;
    const unsigned invalid_operations[] = {64, 583, 72, 40, 264, 136};
    for (unsigned operation : invalid_operations)
        for (unsigned bit = 0; bit < 32; ++bit)
            if ((operation == 64 ? 0x0063f801u : operation == 583 ? 0x001ff800u : 0x001f0000u) & (1u << bit))
                for (unsigned rc = 0; rc < (operation == 64 ? 1u : 2u); ++rc)
                    for (unsigned hot = 0; hot < 2; ++hot)
                        for (unsigned mode = 0; mode < 2; ++mode) {
                            word(0x100, 0x60000000);
                            word(0x104, 0x60000000);
                            word(0x108, 0x60000000);
                            invalid_mask = 1u << bit;
                            word(0x10c, (63u << 26) | (4 << 21) | (operation == 583 || operation == 64 ? 0 : 2 << 11) |
                                            (operation << 1) | rc | (hot ? 0 : invalid_mask));
                            word(0x110, 0x18000001);
                            word(0x114, 0x4200ffec);
                            word(0x118, 0x18000000);
                            ROMlib_ppc_registers r = initial(invalid);
                            expected = r;
                            services = 0;
                            invalid_test = true;
                            ROMlib_ppc_environment env = {};
                            env.memory = map;
                            env.linear_memory = memory;
                            env.linear_size = sizeof memory;
                            env.service = service;
                            env.execution_flags = mode ? ROMLIB_PPC_EXEC_JIT : 0;
                            assert(!ROMlib_ppc_execute(&r, &env));
                            assert(strstr(env.error, "illegal PowerPC instruction"));
                            assert(r.pc == 0x10c && services == (hot ? 16 : 0));
                            assert(!memcmp(r.fpr, expected.fpr, sizeof r.fpr));
                            assert(r.fpscr == expected.fpscr && r.cr == expected.cr);
                            if (hot && mode && ROMlib_ppc_jit_available())
                                assert(env.native_blocks);
                            ++invalid;
                        }
    assert(!fesetenv(&original));
    printf(
        "%u native raw FPR/CR1/alias/live-input/rewrite/branch/poll/limit cases passed; %u reserved-field cold/hot cases\n",
        cases, invalid);
}
