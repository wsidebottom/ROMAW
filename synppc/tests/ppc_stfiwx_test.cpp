/*+===================================================================
  File:      ppc_stfiwx_test.cpp

  Summary:   Implements ppc stfiwx test support for synppc PowerPC execution and fragment loading.

  Classes:   Used: ROMlib_ppc_environment, ROMlib_ppc_registers

  Functions: main

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Original MPCFPE stfiwx raw low-word and bounded-memory fixtures. */
#include "synppc/ppc.h"
#include <assert.h>
#include <fenv.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>

static unsigned char memory[512], expected[512];
static unsigned writes;
static bool deny;

static void* map(void*, uint32_t address, size_t size, int write)
{
    if (write) {
        ++writes;
        assert(size == 4);
        if (deny)
            return NULL;
    }
    return address <= sizeof memory && size <= sizeof memory - address ? memory + address : NULL;
}

static void put(unsigned char* p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        p[i] = value >> (24 - i * 8);
}

static unsigned checked;
static unsigned calls, active_source;

static int rewrite(void*, uint32_t id, ROMlib_ppc_registers* r)
{
    assert(id == 1);
    r->lr = 0x40;
    if (++calls == 16) {
        memory[0x53] |= 1;
        uint64_t value = UINT64_C(0x7ff00000abcdef01);
        memcpy(&r->fpr[active_source], &value, 8);
    }
    return 1;
}

static void warm_rewrite(unsigned source, bool jit)
{
    memset(memory, 0, sizeof memory);
    for (unsigned i = 0; i < 4; ++i)
        put(memory + 0x40 + i * 4, 0x60000000);
    put(memory + 0x50, (31u << 26) | (source << 21) | (2 << 11) | (983 << 1));
    put(memory + 0x54, (6u << 26) | 1);
    ROMlib_ppc_registers r = {};
    r.pc = 0x40;
    r.gpr[2] = 0x180;
    r.cr = 0x13579bdf;
    r.fpscr = 0xe1070084;
    uint64_t value = UINT64_C(0x7ff0000012345678);
    memcpy(&r.fpr[source], &value, 8);
    ROMlib_ppc_environment env = {};
    env.memory = map;
    env.service = rewrite;
    env.linear_memory = memory;
    env.linear_size = sizeof memory;
    env.execution_flags = jit ? ROMLIB_PPC_EXEC_JIT : 0;
    env.instruction_limit = 200;
    calls = 0;
    active_source = source;
    assert(!ROMlib_ppc_execute(&r, &env));
    assert(strstr(env.error, "illegal PowerPC instruction"));
    assert(calls == 16 && r.pc == 0x50 && env.instructions == 101);
    assert(r.cr == 0x13579bdf && r.fpscr == 0xe1070084);
    const unsigned char result[] = {0x12, 0x34, 0x56, 0x78};
    assert(!memcmp(memory + 0x180, result, 4));
    memcpy(&value, &r.fpr[source], 8);
    assert(value == UINT64_C(0x7ff00000abcdef01));
    if (jit && ROMlib_ppc_jit_available())
        assert(env.native_blocks > 0 && env.native_instructions > 0);
    ++checked;
}

static void check(unsigned source, unsigned ra, unsigned rb, uint64_t payload, unsigned mode, unsigned target,
                  bool invalid)
{
    for (unsigned i = 0; i < sizeof memory; ++i)
        memory[i] = i * 37 + 19;
    ROMlib_ppc_registers r = {};
    for (unsigned i = 0; i < 32; ++i) {
        r.gpr[i] = 0x12345678 * (i + 1);
        uint64_t value = UINT64_C(0x7ff0000012345678) + i;
        memcpy(&r.fpr[i], &value, 8);
    }
    uint32_t address = target == 0 ? 0x180 : target == 1 ? 0x1fc : target == 2 ? 0x1fe : target == 3 ? 0xfffffffe : 0;
    if (ra == rb)
        r.gpr[rb] = ra ? address / 2 : address;
    else {
        r.gpr[ra] = 0xfffffff0;
        r.gpr[rb] = ra ? address + 16 : address;
    }
    memcpy(&r.fpr[source], &payload, 8);
    r.pc = 0x40;
    r.lr = 0xabcdef00;
    r.ctr = 0x12345678;
    r.cr = 0x13579bdf;
    r.xer = 0xe0000042;
    r.fpscr = 0xe1070084 | (source & 3);
    ROMlib_ppc_registers original = r;
    for (unsigned i = 0; i < 3; ++i)
        put(memory + 0x40 + i * 4, 0x60000000);
    put(memory + 0x4c, (31u << 26) | (source << 21) | (ra << 16) | (rb << 11) | (983 << 1) | invalid);
    put(memory + 0x50, 6u << 26);
    memcpy(expected, memory, sizeof memory);
    deny = target == 5;
    writes = 0;
    bool fault = target == 2 || target == 3 || deny;
    if (!invalid && !fault)
        put(expected + address, uint32_t(payload));
    ROMlib_ppc_environment env = {};
    env.memory = map;
    env.execution_flags = mode & 1 ? ROMLIB_PPC_EXEC_JIT : 0;
    env.instruction_limit = 6;
    if ((mode & 2) && !fault) {
        env.linear_memory = memory;
        env.linear_size = sizeof memory;
    }
    int success = ROMlib_ppc_execute(&r, &env);
    if (success != (!invalid && !fault)) {
        fprintf(stderr, "stfiwx case %u: %s\n", checked, env.error);
        assert(false);
    }
    if (invalid)
        assert(strstr(env.error, "illegal PowerPC instruction"));
    else if (fault)
        assert(strstr(env.error, "PowerPC write fault") || strstr(env.error, "invalid PowerPC memory extent"));
    if (invalid || fault)
        assert(r.pc == 0x4c);
    r.pc = original.pc;
    assert(!memcmp(&r, &original, sizeof r));
    assert(!memcmp(memory, expected, sizeof memory));
    if (invalid)
        assert(writes == 0);
    assert(fegetround() == FE_DOWNWARD);
    ++checked;
}

int main(void)
{
    fenv_t original;
    assert(!fegetenv(&original));
    assert(!fesetround(FE_DOWNWARD));
    const uint64_t payloads[] = {0,
                                 UINT64_C(0xffffffffffffffff),
                                 UINT64_C(0x7ff0000012345678),
                                 UINT64_C(0xfff8000087654321),
                                 UINT64_C(0x8000000000000001),
                                 UINT64_C(0x3ff12345abcdef01)};
    for (unsigned source = 0; source < 32; ++source)
        for (unsigned ra : {0u, 1u, 31u})
            for (unsigned rb : {0u, 1u, 31u})
                for (uint64_t payload : payloads)
                    for (unsigned mode = 0; mode < 4; ++mode)
                        for (unsigned target = 0; target < 6; ++target) {
                            check(source, ra, rb, payload, mode, target, false);
                            if (payload == payloads[2])
                                check(source, ra, rb, payload, mode, target, true);
                        }
    for (unsigned source = 0; source < 32; ++source)
        for (unsigned jit = 0; jit < 2; ++jit)
            warm_rewrite(source, jit);
    assert(!fesetenv(&original));
    printf("%u stfiwx raw-word/register/address/fault/reserved cases passed\n", checked);
}
