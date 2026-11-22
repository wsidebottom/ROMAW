/*+===================================================================
  File:      ppc_shift_jit_test.cpp

  Summary:   Implements ppc shift jit test support for synppc PowerPC execution and fragment loading.

  Classes:   Used: ROMlib_ppc_environment, ROMlib_ppc_registers

  Functions: main

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Original bit-at-a-time SRAW/SRAWI and native block state fixtures. */
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

static void condition(unsigned field, uint32_t value)
{
    unsigned nibble = (!value ? 2 : value & 0x80000000 ? 8 : 4) | (expected.xer >> 31);
    unsigned shift = 28 - field * 4;
    expected.cr = (expected.cr & ~(15u << shift)) | (nibble << shift);
}

static int service(void*, uint32_t id, ROMlib_ppc_registers* r)
{
    assert(id == 1);
    for (unsigned i = 0; i < 6; ++i) {
        uint32_t op = readword(0x100 + i * 4);
        if (i == 0 || i == 5) {
            int64_t a = (int32_t)expected.gpr[(op >> 16) & 31];
            int64_t b = (int16_t)op;
            condition((op >> 23) & 7, a == b ? 0 : a < b ? 0x80000000 : 1);
            continue;
        }
        unsigned rs = (op >> 21) & 31, ra = (op >> 16) & 31;
        unsigned count = (op >> 11) & 31;
        if (((op >> 1) & 1023) == 792)
            count = expected.gpr[count] & 63;
        uint32_t value = expected.gpr[rs];
        bool negative = value & 0x80000000, lost = false;
        for (unsigned bit = 0; bit < count; ++bit) {
            lost |= value & 1;
            value = value / 2 + (negative ? 0x80000000 : 0);
        }
        expected.gpr[ra] = value;
        expected.xer = (expected.xer & ~0x20000000u) | (negative && lost ? 0x20000000u : 0);
        if (op & 1)
            condition(0, value);
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
            // Toggle variable/immediate forms, record bits and shift/count fields.
            word(0x100 + i * 4, readword(0x100 + i * 4) ^ 0x841);
    return 1;
}

int main(void)
{
    const uint32_t values[] = {0, 1, 0xffffffff, 0x80000000, 0x7fffffff, 0xfffffffe, 0x80000001, 0x87654321};
    const uint32_t amounts[] = {0, 1, 7, 15, 16, 31, 32, 33, 63, 64, 65, 127, 128, 0xffffffff};
    unsigned cases = 0;
    for (unsigned immediate = 0; immediate < 2; ++immediate)
        for (uint32_t value : values)
            for (uint32_t amount : amounts)
                for (unsigned alias = 0; alias < 5; ++alias)
                    for (unsigned flags = 0; flags < 4; ++flags)
                        for (unsigned rc = 0; rc < 2; ++rc) {
                            unsigned dst = alias == 0 ? 3 : alias == 2 ? 2 : alias == 3 ? 0 : 1;
                            unsigned rb = immediate ? amount & 31 : alias == 4 ? 1 : 2;
                            uint32_t op =
                                (31u << 26) | (1 << 21) | (dst << 16) | (rb << 11) | ((immediate ? 824 : 792) << 1);
                            ROMlib_ppc_registers initial = {}, reference = {};
                            initial.pc = 0x100;
                            initial.ctr = 40;
                            initial.cr = 0x13579bdf;
                            initial.xer = (flags << 29) | 0x80000042;
                            for (unsigned i = 0; i < 32; ++i)
                                initial.gpr[i] = 0x9e3779b9u * i;
                            initial.gpr[1] = value;
                            initial.gpr[2] = amount;
                            uint64_t count = 0;
                            for (unsigned mode = 0; mode < 2; ++mode) {
                                word(0x100, (11u << 26) | (7 << 23) | (1 << 16));
                                for (unsigned i = 1; i < 5; ++i)
                                    word(0x100 + i * 4, op | (rc ^ (i & 1)));
                                word(0x114, (11u << 26) | (6 << 23) | (dst << 16) | 0x8000);
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
                                        assert(env.native_blocks && env.native_instructions);
                                }
                            }
                            ++cases;
                        }
    printf("%u native arithmetic shift/CA/CR/alias/live-input/rewrite cases passed\n", cases);
}
