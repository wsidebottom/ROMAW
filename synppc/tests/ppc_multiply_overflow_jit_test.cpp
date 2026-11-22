/*+===================================================================
  File:      ppc_multiply_overflow_jit_test.cpp

  Summary:   Implements ppc multiply overflow jit test support for synppc PowerPC execution and fragment loading.

  Classes:   Used: ROMlib_ppc_environment, ROMlib_ppc_registers

  Functions: main

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Original signed-overflow arithmetic model and native block state fixtures. */
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
        int64_t product = int64_t(int32_t(expected.gpr[ra])) * int32_t(expected.gpr[(op >> 11) & 31]);
        unsigned xo = (op >> 1) & 511;
        int64_t a = int32_t(expected.gpr[ra]);
        int64_t b = int32_t(expected.gpr[(op >> 11) & 31]);
        if (xo == 266)
            product = a + b;
        else if (xo == 40)
            product = b - a;
        else if (xo == 104)
            product = -a;
        uint32_t value = uint32_t(product);
        if (op & 0x400) {
            bool overflow = product < INT32_MIN || product > INT32_MAX;
            expected.xer = (expected.xer & ~0x40000000u) | (overflow ? 0xc0000000u : 0);
        }
        expected.gpr[rd] = value;
        if (op & 1)
            condition(0, int32_t(value));
    }
    assert(!memcmp(r->gpr, expected.gpr, sizeof r->gpr));
    assert(r->cr == expected.cr && r->xer == expected.xer);
    ++services;
    r->lr = r->pc + 4;
    expected.cr = r->cr = services * 0x13579bdu;
    expected.xer = r->xer = ((services & 7) << 29) | 0x42;
    for (unsigned i = 0; i < 32; ++i)
        expected.gpr[i] = r->gpr[i] = (r->gpr[i] << 1) | (r->gpr[i] >> 31);
    if (services == 12 || services == 24)
        for (unsigned i = 1; i < 5; ++i)
            word(0x100 + i * 4,
                 readword(0x100 + i * 4) ^ 0x401); // Toggle OE/Rc.
    return 1;
}

int main(void)
{
    const uint32_t values[] = {0,          1,          0xffffffff, 0x80000000, 0x7fffffff,
                               0xfffffffe, 0x80000001, 0x87654321, 0x7fff,     0x8000};
    unsigned cases = 0;
    const unsigned operations[] = {235, 266, 40, 104};
    for (unsigned kind = 0; kind < 8; ++kind)
        for (uint32_t a : values)
            for (uint32_t b : values)
                for (unsigned alias = 0; alias < 5; ++alias)
                    for (unsigned flags = 0; flags < 8; ++flags)
                        for (unsigned rc = 0; rc < 2; ++rc) {
                            unsigned rd = alias == 0 ? 3 : alias == 2 ? 2 : alias == 3 ? 0 : 1;
                            unsigned ra = alias == 3 ? 0 : 1;
                            unsigned rb = alias == 4 ? ra : 2;
                            if (operations[kind / 2] == 104)
                                rb = 0;
                            uint32_t op = (31u << 26) | (rd << 21) | (ra << 16) | (rb << 11) |
                                          (operations[kind / 2] << 1) | ((kind & 1) << 10) | rc;
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
                                word(0x118, 0x18000001);
                                word(0x11c, 0x4200ffe4);
                                word(0x120, 0x18000000);
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
                                        assert(env.native_instructions >= 150u);
                                }
                            }
                            ++cases;
                        }
    printf("%u native multiply/add/subtract/negate overflow/CR/alias/live-input/rewrite cases passed\n", cases);
}
