/*+===================================================================
  File:      ppc_carry_jit_test.cpp

  Summary:   Implements ppc carry jit test support for synppc PowerPC execution and fragment loading.

  Classes:   Used: ROMlib_ppc_environment, ROMlib_ppc_registers

  Functions: main

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Original widened-integer carry model and native block state fixtures. */
#include "synppc/ppc.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned char memory[4096];
static unsigned services;
static ROMlib_ppc_registers expected;

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

static void condition(unsigned field, int64_t value)
{
    unsigned nibble = (value == 0 ? 2 : value < 0 ? 8 : 4) | (expected.xer >> 31);
    unsigned shift = 28 - field * 4;
    expected.cr = (expected.cr & ~(15u << shift)) | (nibble << shift);
}

static int service(void*, uint32_t id, ROMlib_ppc_registers* r)
{
    assert(id == 1);
    for (unsigned i = 0; i < 6; ++i) {
        uint32_t op = readword(0x100 + i * 4);
        unsigned ra = (op >> 16) & 31, rd = (op >> 21) & 31;
        if (i == 0 || i == 5) {
            condition((op >> 23) & 7, int64_t(int32_t(expected.gpr[ra])) - int16_t(op));
            continue;
        }
        unsigned primary = op >> 26;
        uint32_t a = expected.gpr[ra];
        uint32_t b = primary == 31 ? expected.gpr[(op >> 11) & 31] : uint32_t(int32_t(int16_t(op)));
        bool subtract = primary == 8 || (primary == 31 && ((op >> 1) & 1023) == 8);
        uint64_t sum = uint64_t(a) + b;
        uint32_t value = subtract ? b - a : uint32_t(sum);
        bool carry = subtract ? b >= a : sum > UINT32_MAX;
        expected.gpr[rd] = value;
        expected.xer = (expected.xer & ~0x20000000u) | (carry ? 0x20000000u : 0);
        if (primary == 13 || (primary == 31 && (op & 1)))
            condition(0, int32_t(value));
    }
    // ADDE must consume the CA just produced by the preceding operations.
    uint64_t sum = uint64_t(expected.gpr[4]) * 2 + ((expected.xer >> 29) & 1);
    expected.gpr[4] = uint32_t(sum);
    expected.xer = (expected.xer & ~0x20000000u) | (sum > UINT32_MAX ? 0x20000000u : 0);
    assert(!memcmp(r->gpr, expected.gpr, sizeof r->gpr));
    assert(r->cr == expected.cr && r->xer == expected.xer);
    ++services;
    r->lr = r->pc + 4;
    expected.cr = r->cr = services * 0x13579bdu;
    expected.xer = r->xer = ((services & 7) << 29) | 0x42;
    for (unsigned i = 0; i < 32; ++i)
        expected.gpr[i] = r->gpr[i] = (r->gpr[i] << 1) | (r->gpr[i] >> 31);
    if (services == 12 || services == 24)
        for (unsigned i = 1; i < 5; ++i) {
            uint32_t op = readword(0x100 + i * 4);
            // Rewrite warm blocks: swap ADDC/SUBFC or change the signed immediate.
            word(0x100 + i * 4, op ^ (op >> 26 == 31 ? 4 : 0x8001));
        }
    return 1;
}

int main(void)
{
    const uint32_t values[] = {0,          1,          0xffffffff, 0x80000000, 0x7fffffff,
                               0xfffffffe, 0x80000001, 0x87654321, 0x7fff,     0x8000};
    unsigned cases = 0;
    for (unsigned kind = 0; kind < 4; ++kind)
        for (uint32_t a : values)
            for (uint32_t b : values)
                for (unsigned alias = 0; alias < 5; ++alias)
                    for (unsigned flags = 0; flags < 8; ++flags)
                        for (unsigned rc = 0; rc < (kind == 3 ? 1u : 2u); ++rc) {
                            unsigned rd = alias == 0 ? 3 : alias == 2 ? 2 : alias == 3 ? 0 : 1;
                            unsigned ra = alias == 3 ? 0 : 1;
                            unsigned rb = alias == 4 ? ra : 2;
                            uint32_t op =
                                kind < 2
                                    ? (31u << 26) | (rd << 21) | (ra << 16) | (rb << 11) | ((kind ? 8 : 10) << 1) | rc
                                    : ((kind == 2 ? 12u + rc : 8u) << 26) | (rd << 21) | (ra << 16) | (b & 0xffff);
                            ROMlib_ppc_registers initial = {}, reference = {};
                            initial.pc = 0x100;
                            initial.ctr = 40;
                            initial.cr = 0x13579bdf;
                            initial.xer = (flags << 29) | 0x42;
                            for (unsigned i = 0; i < 32; ++i)
                                initial.gpr[i] = 0x9e3779b9u * i;
                            initial.gpr[ra] = a;
                            if (rb != ra)
                                initial.gpr[rb] = b;
                            uint64_t count = 0;
                            for (unsigned mode = 0; mode < 2; ++mode) {
                                word(0x100, (11u << 26) | (7 << 23) | (ra << 16));
                                for (unsigned i = 1; i < 5; ++i)
                                    word(0x100 + i * 4, op);
                                word(0x114, (11u << 26) | (6 << 23) | (rd << 16) | 0x8000);
                                word(0x118, (31u << 26) | (4 << 21) | (4 << 16) | (4 << 11) | (138 << 1));
                                word(0x11c, 0x18000001);
                                word(0x120, 0x4200ffe0);
                                word(0x124, 0x18000000);
                                ROMlib_ppc_registers r = initial;
                                expected = initial;
                                services = 0;
                                ROMlib_ppc_environment env = {};
                                env.memory = map;
                                env.linear_memory = memory;
                                env.linear_size = sizeof memory;
                                env.service = service;
                                env.execution_flags = mode ? ROMLIB_PPC_EXEC_JIT : 0;
                                assert(ROMlib_ppc_execute(&r, &env));
                                assert(services == 40);
                                if (!mode) {
                                    reference = r;
                                    count = env.instructions;
                                }
                                else {
                                    assert(!memcmp(&r, &reference, sizeof r));
                                    assert(env.instructions == count);
                                    if (ROMlib_ppc_jit_available())
                                        // Compiling only the surrounding compares is not enough.
                                        assert(env.native_instructions >= 150);
                                }
                            }
                            ++cases;
                        }
    printf("%u native carry/CR/alias/live-input/rewrite cases passed\n", cases);
}
