/*+===================================================================
  File:      ppc_string_test.cpp

  Summary:   Implements ppc string test support for synppc PowerPC execution and fragment loading.

  Classes:   Used: ROMlib_ppc_environment, ROMlib_ppc_registers

  Functions: main

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* String-load register ranges, wrapping and dynamic XER byte counts. */
#include "synppc/ppc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>

static unsigned char memory[512], saved[512];
static unsigned data_calls;

static void* map(void*, uint32_t address, size_t size, int)
{
    if (address >= 0x100)
        ++data_calls;
    return address <= sizeof memory && size <= sizeof memory - address ? memory + address : NULL;
}

static void put(unsigned address, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        memory[address + i] = value >> (24 - i * 8);
}

static void check(bool immediate, unsigned first, unsigned ra, unsigned rb, unsigned count, unsigned mode)
{
    for (unsigned i = 0; i < sizeof memory; ++i)
        memory[i] = i * 37 + 11;
    for (unsigned i = 0; i < 4; ++i)
        put(0x40 + i * 4, 0x60000000);
    put(0x50, (31u << 26) | first << 21 | ra << 16 | (immediate ? count : rb) << 11 | (immediate ? 597 : 533) << 1);
    put(0x54, 6u << 26);
    ROMlib_ppc_registers r = {};
    r.pc = 0x40;
    for (unsigned i = 0; i < 32; ++i)
        r.gpr[i] = 0x10203000 + i;
    if (ra)
        r.gpr[ra] = 0x100;
    if (!immediate)
        r.gpr[rb] = 4;
    r.cr = 0x13579BDF;
    r.xer = 0xE0000000 | (immediate ? 19 : count);
    r.fpscr = 0xE1070084;
    ROMlib_ppc_registers expected = r;
    unsigned length = immediate && !count ? 32 : count;
    unsigned address = (ra ? r.gpr[ra] : 0) + (immediate ? 0 : r.gpr[rb]);
    bool loaded[32] = {};
    /* Even zero-length indexed loads cannot alias RT with an address field. */
    loaded[first] = true;
    for (unsigned i = 0; i < length; ++i) {
        unsigned reg = (first + i / 4) & 31;
        loaded[reg] = true;
        if (!(i & 3))
            expected.gpr[reg] = 0;
        expected.gpr[reg] |= uint32_t(memory[address + i]) << (24 - (i & 3) * 8);
    }
    bool bad = loaded[ra] || (!immediate && loaded[rb]);
    if (bad)
        expected = r;
    memcpy(saved, memory, sizeof memory);
    ROMlib_ppc_environment env = {};
    env.memory = map;
    env.instruction_limit = 10;
    env.execution_flags = mode & 1 ? ROMLIB_PPC_EXEC_JIT : 0;
    if (mode & 2) {
        env.linear_memory = memory;
        env.linear_size = sizeof memory;
    }
    data_calls = 0;
    assert(ROMlib_ppc_execute(&r, &env) == !bad);
    expected.pc = bad ? 0x50 : r.pc;
    assert(env.instructions == (bad ? 5 : 6));
    if (bad) {
        assert(strstr(env.error, "illegal PowerPC"));
        assert(!data_calls);
    }
    assert(!memcmp(&r, &expected, sizeof r));
    assert(!memcmp(memory, saved, sizeof memory));
}

static unsigned services;
static ROMlib_ppc_registers warm_expected;

static int grow(void*, uint32_t id, ROMlib_ppc_registers* r)
{
    assert(id == 1);
    r->lr = 0x40;
    r->gpr[3] = 0x100;
    r->gpr[1] = 4;
    if (++services == 16) {
        r->xer = 8; /* RT=2 now loads RA=3 too. */
        warm_expected = *r;
        warm_expected.pc = 0x50;
        memcpy(saved, memory, sizeof memory);
        data_calls = 0;
    }
    return 1;
}

static void hot_count(bool jit)
{
    memset(memory, 0xA5, sizeof memory);
    for (unsigned i = 0; i < 4; ++i)
        put(0x40 + i * 4, 0x60000000);
    put(0x50, (31u << 26) | 2 << 21 | 3 << 16 | 1 << 11 | 533 << 1);
    put(0x54, (6u << 26) | 1);
    ROMlib_ppc_registers r = {};
    r.pc = 0x40;
    r.gpr[3] = 0x100;
    r.gpr[1] = r.xer = 4;
    ROMlib_ppc_environment env = {};
    env.memory = map;
    env.service = grow;
    env.linear_memory = memory;
    env.linear_size = sizeof memory;
    env.execution_flags = jit ? ROMLIB_PPC_EXEC_JIT : 0;
    env.instruction_limit = 200;
    services = data_calls = 0;
    assert(!ROMlib_ppc_execute(&r, &env));
    assert(strstr(env.error, "illegal PowerPC"));
    assert(services == 16 && env.instructions == 101 && !data_calls);
    assert(!memcmp(&r, &warm_expected, sizeof r));
    assert(!memcmp(memory, saved, sizeof memory));
    if (jit && ROMlib_ppc_jit_available())
        assert(env.native_blocks && env.native_instructions);
}

static unsigned native_ranges()
{
    auto cache = ROMlib_ppc_translation_cache_create();
    if (!cache)
        return 0;
    unsigned cases = 0;
    for (bool immediate : {false, true})
        for (bool writing : {false, true})
            for (unsigned first : {29u, 31u})
                for (unsigned count = 0; count < (immediate ? 32u : 128u); ++count)
                    for (unsigned layout = 0; layout < 5; ++layout) {
                        const uint32_t addresses[] = {0x104, 0x106, 0x106, 0x1FA, 0xFFFFFFF0};
                        uint32_t address = addresses[layout];
                        unsigned extent = layout == 2 ? 0x116 : sizeof memory;
                        unsigned base = first - 1;
                        unsigned length = immediate && !count ? 32 : count;
                        unsigned xo = writing ? (immediate ? 725 : 661) : (immediate ? 597 : 533);
                        unsigned operand = immediate ? count : base;
                        uint32_t opcode = (31u << 26) | first << 21 | base << 16 | operand << 11 | xo << 1;
                        ROMlib_ppc_registers before = {};
                        before.pc = 0x40;
                        before.cr = 0x13579BDF;
                        before.fpscr = 0xE1070084;
                        before.xer = 0xE0000001;
                        for (unsigned i = 0; i < 32; ++i)
                            before.gpr[i] = 0x10203040 + i;
                        before.gpr[base] = immediate ? address : address / 2;
                        before.gpr[7] = 0xA0000000 | count;
                        unsigned char initial[sizeof memory];
                        ROMlib_ppc_registers reference = {};
                        char error[256];
                        uint64_t instructions = 0;
                        unsigned callbacks = 0;
                        int succeeded = 0;
                        for (bool native : {false, true}) {
                            for (unsigned i = 0; i < sizeof memory; ++i)
                                memory[i] = (i * 37 + (i >> 2) * 3) & 255;
                            put(0x40, 0x7CE103A6); // MTXER r7: the indexed transfer sees the live native prefix.
                            for (unsigned i = 1; i < 4; ++i)
                                put(0x40 + i * 4, 0x60000000);
                            put(0x50, opcode);
                            put(0x54, 6u << 26);
                            if (native) {
                                ROMlib_ppc_translate_range(cache, memory + 0x40, 0x40, 20);
                                ROMlib_ppc_translate_range(cache, memory + 0x50, 0x50, 4);
                            }
                            else
                                memcpy(initial, memory, sizeof memory);
                            ROMlib_ppc_environment env = {};
                            env.memory = map;
                            env.linear_memory = memory;
                            env.linear_size = extent;
                            env.execution_flags = native ? ROMLIB_PPC_EXEC_JIT : 0;
                            env.translation_cache = cache;
                            env.instruction_limit = 10;
                            auto registers = before;
                            data_calls = 0;
                            int ok = ROMlib_ppc_execute(&registers, &env);
                            if (!native) {
                                reference = registers;
                                succeeded = ok;
                                instructions = env.instructions;
                                callbacks = data_calls;
                                memcpy(error, env.error, sizeof error);
                                memcpy(saved, memory, sizeof memory);
                                continue;
                            }
                            assert(ok == succeeded && env.instructions == instructions && data_calls == callbacks);
                            assert(!memcmp(&registers, &reference, sizeof registers));
                            assert(!strcmp(error, env.error) && !memcmp(memory, saved, sizeof memory));
                            bool valid = writing || length <= 124;
                            bool direct = valid && (!length || uint64_t(address) + length <= extent);
                            const unsigned kernels = 0;

                            assert(env.native_instructions == (direct ? 5u : 4u) && !env.native_compilations &&
                                   env.native_kernel_instructions == kernels);
                            if (ok && writing)
                                for (unsigned i = 0; i < length; ++i)
                                    assert(memory[address + i] ==
                                           uint8_t(before.gpr[(first + i / 4) & 31] >> (24 - (i & 3) * 8)));
                            if (ok && !writing)
                                for (unsigned i = 0; i < length; i += 4) {
                                    uint32_t expected = 0;
                                    for (unsigned j = 0; j < 4 && i + j < length; ++j)
                                        expected |= uint32_t(initial[address + i + j]) << (24 - j * 8);
                                    assert(registers.gpr[(first + i / 4) & 31] == expected);
                                }
                        }
                        ++cases;
                    }
    ROMlib_ppc_translation_cache_destroy(cache);
    return cases;
}

int main(void)
{
    unsigned cases = 0;
    const unsigned lengths[] = {0, 1, 4, 5, 32, 124, 127};
    for (unsigned mode = 0; mode < 4; ++mode)
        for (unsigned first = 0; first < 32; ++first)
            for (unsigned ra = 0; ra < 32; ++ra) {
                for (unsigned count = 0; count < 32; ++count) {
                    check(true, first, ra, 0, count, mode);
                    ++cases;
                }
                for (unsigned rb = 0; rb < 32; ++rb)
                    for (unsigned count : lengths) {
                        check(false, first, ra, rb, count, mode);
                        ++cases;
                    }
            }
    hot_count(false);
    hot_count(true);
    printf("%u string-load range/wrap/count cases and two hot XER changes "
           "passed.\n",
           cases);
    printf("%u eager native string load/store, count, and boundary cases passed.\n", native_ranges());
}
