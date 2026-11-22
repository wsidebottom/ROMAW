/*+===================================================================
  File:      ppc_memory_encoding_test.cpp

  Summary:   Implements ppc memory encoding test support for synppc PowerPC execution and fragment loading.

  Classes:   Used: ROMlib_ppc_environment, ROMlib_ppc_registers

  Functions: main

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Reserved X-form memory record bits fail before any data access. */
#include "synppc/ppc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char memory[512], saved_memory[512];
static unsigned data_calls;

static void* map(void*, uint32_t address, size_t size, int)
{
    if (address >= 0x100)
        ++data_calls;
    return address <= sizeof memory && size <= sizeof memory - address ? memory + address : NULL;
}

static void put(unsigned address, uint32_t word)
{
    for (unsigned i = 0; i < 4; ++i)
        memory[address + i] = word >> (24 - i * 8);
}

static unsigned services;
static ROMlib_ppc_registers warm_expected;

static int rewrite(void*, uint32_t id, ROMlib_ppc_registers* r)
{
    assert(id == 1);
    r->lr = 0x40;
    r->gpr[2] = 0x100;
    r->gpr[3] = 4;
    if (++services == 16) {
        memory[0x53] |= 1;
        warm_expected = *r;
        warm_expected.pc = 0x50;
        memcpy(saved_memory, memory, sizeof memory);
        data_calls = 0;
    }
    return 1;
}

static void hot_rewrite(unsigned xo, bool jit)
{
    memset(memory, 0xA5, sizeof memory);
    for (unsigned i = 0; i < 4; ++i)
        put(0x40 + i * 4, 0x60000000);
    put(0x50, (31u << 26) | 1 << 21 | 2 << 16 | 3 << 11 | xo << 1);
    put(0x54, (6u << 26) | 1);
    ROMlib_ppc_registers r = {};
    r.pc = 0x40;
    r.gpr[2] = 0x100;
    r.gpr[3] = 4;
    r.xer = 3;
    ROMlib_ppc_environment env = {};
    env.memory = map;
    env.service = rewrite;
    env.linear_memory = memory;
    env.linear_size = sizeof memory;
    env.execution_flags = jit ? ROMLIB_PPC_EXEC_JIT : 0;
    env.instruction_limit = 200;
    services = data_calls = 0;
    assert(!ROMlib_ppc_execute(&r, &env));
    assert(strstr(env.error, "illegal PowerPC instruction"));
    assert(services == 16 && env.instructions == 101 && data_calls == 0);
    assert(!memcmp(&r, &warm_expected, sizeof r));
    assert(!memcmp(memory, saved_memory, sizeof memory));
    if (jit && ROMlib_ppc_jit_available())
        assert(env.native_blocks > 0 && env.native_instructions > 0);
}

int main(void)
{
    const unsigned operations[] = {23,  55,  87,  119, 279, 311, 343, 375, 534, 790, 151, 183, 215, 247, 407,
                                   439, 662, 918, 535, 567, 599, 631, 663, 695, 727, 759, 597, 533, 725, 661};
    unsigned cases = 0;
    for (unsigned xo : operations)
        for (unsigned mode = 0; mode < 4; ++mode)
            for (unsigned reg = 0; reg < 32; ++reg) {
                memset(memory, 0xA5, sizeof memory);
                for (unsigned i = 0; i < 4; ++i)
                    put(0x40 + i * 4, 0x60000000);
                /* Keep integer load/update destinations different from RA. */
                unsigned ra = reg == 2 ? 4 : 2;
                put(0x50, (31u << 26) | reg << 21 | ra << 16 | 3 << 11 | xo << 1 | 1);
                put(0x54, 6u << 26);
                ROMlib_ppc_registers r = {};
                r.pc = 0x40;
                r.gpr[ra] = 0x100;
                r.gpr[3] = 4;
                r.cr = 0x13579BDF;
                r.xer = 0xE0000003;
                r.fpscr = 0xE1070084;
                ROMlib_ppc_registers expected = r;
                expected.pc = 0x50;
                memcpy(saved_memory, memory, sizeof memory);
                ROMlib_ppc_environment env = {};
                env.memory = map;
                env.instruction_limit = 10;
                env.execution_flags = mode & 1 ? ROMLIB_PPC_EXEC_JIT : 0;
                if (mode & 2) {
                    env.linear_memory = memory;
                    env.linear_size = sizeof memory;
                }
                data_calls = 0;
                assert(!ROMlib_ppc_execute(&r, &env));
                assert(strstr(env.error, "illegal PowerPC instruction"));
                assert(env.instructions == 5 && data_calls == 0);
                assert(!memcmp(&r, &expected, sizeof r));
                assert(!memcmp(memory, saved_memory, sizeof memory));
                ++cases;
            }
    for (unsigned xo : operations)
        for (unsigned jit = 0; jit < 2; ++jit) {
            hot_rewrite(xo, jit != 0);
            ++cases;
        }
    printf("%u reserved memory-encoding/register/no-access cases passed.\n", cases);
}
