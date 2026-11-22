/*+===================================================================
  File:      ppc_dcbz_test.cpp

  Summary:   Implements ppc dcbz test support for synppc PowerPC execution and fragment loading.

  Classes:   Used: ROMlib_ppc_environment, ROMlib_ppc_registers

  Functions: main

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Original MPC750 32-byte DCBZ and reserved-encoding fixtures. */
#include "synppc/ppc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>

static unsigned char memory[512], expected[512];
static unsigned limit, writes, cases;
static bool deny;

static void* map(void*, uint32_t address, size_t size, int write)
{
    if (write) {
        ++writes;
        assert(size == 32 && !(address & 31));
        if (deny)
            return NULL;
    }
    return address <= limit && size <= limit - address ? memory + address : NULL;
}

static void put(unsigned offset, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        memory[offset + i] = value >> (24 - i * 8);
}

static void check(unsigned ra, unsigned rb, unsigned offset, unsigned mode, uint32_t reserved, unsigned missing,
                  ROMlib_ppc_translation_cache* cache)
{
    for (unsigned i = 0; i < sizeof memory; ++i)
        memory[i] = i * 37 + 19;
    ROMlib_ppc_registers r = {};
    for (unsigned i = 0; i < 32; ++i) {
        r.gpr[i] = 0x12345678u * (i + 1);
        uint64_t value = UINT64_C(0x7ff0000012345678) + i;
        memcpy(&r.fpr[i], &value, 8);
    }
    uint32_t address = 0x1e0 + offset;
    if (ra == rb)
        r.gpr[rb] = ra ? address / 2 : address;
    else {
        r.gpr[ra] = 0xffffffe0;
        r.gpr[rb] = ra ? address + 32 : address;
    }
    r.pc = 0x40;
    r.cr = 0x13579bdf;
    r.xer = 0xe0000042;
    r.fpscr = 0xe1070084;
    ROMlib_ppc_registers original = r;
    for (unsigned i = 0; i < 4; ++i)
        put(0x40 + i * 4, 0x60000000);
    put(0x50, (31u << 26) | (ra << 16) | (rb << 11) | (1014 << 1) | reserved);
    put(0x54, 6u << 26);
    memcpy(expected, memory, sizeof memory);
    limit = sizeof memory - (missing < 32 ? missing : 0);
    deny = missing == 32;
    writes = 0;
    if (!reserved && !missing)
        memset(expected + 0x1e0, 0, 32);
    ROMlib_ppc_environment env = {};
    env.memory = map;
    env.execution_flags = mode & 1 ? ROMLIB_PPC_EXEC_JIT : 0;
    env.instruction_limit = 7;
    if (mode & 2) {
        env.linear_memory = memory;
        env.linear_size = deny ? 0x1e0 : limit;
    }
    if (cache && mode == 3) {
        env.translation_cache = cache;
        ROMlib_ppc_translate_range(cache, memory + 0x40, 0x40, 20);
        ROMlib_ppc_translate_range(cache, memory + 0x50, 0x50, 4);
    }
    int success = ROMlib_ppc_execute(&r, &env);
    if (success != (!reserved && !missing)) {
        fprintf(stderr, "DCBZ case %u: %s\n", cases, env.error);
        assert(false);
    }
    if (reserved) {
        assert(strstr(env.error, "illegal PowerPC instruction"));
        assert(writes == 0);
    }
    else if (missing)
        assert(strstr(env.error, "PowerPC write fault"));
    if (reserved || missing)
        assert(r.pc == 0x50);
    if (cache && mode == 3 && !reserved) {
        assert(env.native_instructions == (missing ? 4u : 5u) && !env.native_kernel_instructions &&

               !env.native_compilations);
        assert(writes == bool(missing));
    }
    r.pc = original.pc;
    assert(!memcmp(&r, &original, sizeof r));
    assert(!memcmp(memory, expected, sizeof memory));
    ++cases;
}

static unsigned calls;

static int rewrite(void*, uint32_t id, ROMlib_ppc_registers* r)
{
    assert(id == 1);
    r->lr = 0x40;
    if (++calls == 16) {
        memory[0x53] |= 1;
        memset(memory + 0x1e0, 0x5a, 32);
    }
    return 1;
}

static void warm(bool jit)
{
    limit = sizeof memory;
    deny = false;
    writes = 0;
    memset(memory, 0, sizeof memory);
    for (unsigned i = 0; i < 4; ++i)
        put(0x40 + i * 4, 0x60000000);
    put(0x50, (31u << 26) | (2 << 11) | (1014 << 1));
    put(0x54, (6u << 26) | 1);
    ROMlib_ppc_registers r = {};
    r.pc = 0x40;
    r.gpr[0] = 0x12345678;
    r.gpr[2] = 0x1ff;
    r.cr = 0x13579bdf;
    r.fpscr = 0xe1070084;
    ROMlib_ppc_environment env = {};
    env.memory = map;
    env.service = rewrite;
    env.linear_memory = memory;
    env.linear_size = sizeof memory;
    env.execution_flags = jit ? ROMLIB_PPC_EXEC_JIT : 0;
    env.instruction_limit = 200;
    calls = 0;
    assert(!ROMlib_ppc_execute(&r, &env));
    assert(strstr(env.error, "illegal PowerPC instruction"));
    assert(calls == 16 && r.pc == 0x50 && env.instructions == 101);
    assert(r.cr == 0x13579bdf && r.fpscr == 0xe1070084);
    for (unsigned i = 0x1e0; i < sizeof memory; ++i)
        assert(memory[i] == 0x5a);
    if (jit && ROMlib_ppc_jit_available())
        assert(env.native_blocks > 0 && env.native_instructions > 0);
    ++cases;
}

int main(void)
{
    auto cache = ROMlib_ppc_translation_cache_create();
    for (unsigned mode = 0; mode < 4; ++mode)
        for (unsigned field = 0; field < 64; ++field)
            if (field)
                for (unsigned missing : {0u, 1u, 32u})
                    check(0, 2, 31, mode, (field >> 1) << 21 | (field & 1), missing, cache);
    for (unsigned mode = 0; mode < 4; ++mode)
        for (unsigned ra = 0; ra < 32; ++ra)
            for (unsigned rb : {0u, ra, 17u, 31u})
                for (unsigned offset = 0; offset < 32; ++offset)
                    check(ra, rb, offset, mode, 0, 0, cache);
    for (unsigned mode = 0; mode < 4; ++mode)
        for (unsigned missing = 1; missing <= 32; ++missing)
            for (unsigned offset = 0; offset < 32; ++offset)
                check(1, 2, offset, mode, 0, missing, cache);
    warm(false);
    warm(true);
    ROMlib_ppc_translation_cache_destroy(cache);
    printf("%u DCBZ block/alias/wrap/fault/reserved/warm-rewrite cases passed\n", cases);
}
