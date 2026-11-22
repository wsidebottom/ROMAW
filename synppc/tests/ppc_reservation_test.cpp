/*+===================================================================
  File:      ppc_reservation_test.cpp

  Summary:   Implements ppc reservation test support for synppc PowerPC execution and fragment loading.

  Classes:   Used: ROMlib_ppc_environment, ROMlib_ppc_registers

  Functions: main

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Original MPC750UM 3.3.5.3 nonspecific reservation and fault fixtures. */
#include "synppc/ppc.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned char memory[1024], expected_memory[1024];
static unsigned data_accesses;
static bool nested_pending;
static uint32_t nested_address = 0x104;
static void nested_write(void);

static void* map(void*, uint32_t address, size_t size, int)
{
    if (nested_pending && address == nested_address) {
        nested_pending = false;
        nested_write();
    }
    if (address >= 0x200)
        ++data_accesses;
    return address <= sizeof memory && size <= sizeof memory - address ? memory + address : NULL;
}

static void put(unsigned char* p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        p[i] = value >> (24 - 8 * i);
}

static uint32_t get(const unsigned char* p)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value = value * 256 + p[i];
    return value;
}

static int service(void*, uint32_t id, ROMlib_ppc_registers* r)
{
    assert(id == 1);
    put(memory + 0x200, 0xabcdef01);
    r->lr = r->pc + 4;
    return 1;
}

static uint32_t load(unsigned rd, unsigned ra, unsigned rb)
{
    return (31u << 26) | (rd << 21) | (ra << 16) | (rb << 11) | (20 << 1);
}

static uint32_t store(unsigned rs, unsigned ra, unsigned rb)
{
    return (31u << 26) | (rs << 21) | (ra << 16) | (rb << 11) | (150 << 1) | 1;
}

static void nested_write(void)
{
    ROMlib_ppc_registers r = {};
    r.pc = 0x80;
    r.gpr[2] = 0x200;
    r.gpr[3] = 0x01020304;
    ROMlib_ppc_environment env = {};
    env.memory = map;
    assert(ROMlib_ppc_execute(&r, &env));
}

static unsigned polls;

static void poll(void*)
{
    ++polls;
    put(memory + 0x200, 0x01020304);
}

static void check(unsigned base, unsigned alias, unsigned target, unsigned sequence, unsigned flags, unsigned mode,
                  ROMlib_ppc_translation_cache* cache)
{
    for (unsigned i = 0; i < sizeof memory; ++i)
        memory[i] = i * 37 + 13;
    ROMlib_ppc_registers r = {};
    for (unsigned i = 0; i < 32; ++i)
        r.gpr[i] = 0x12345678u * (i + 1);
    r.gpr[0] = 0xf0000000;
    r.gpr[1] = 0x100;
    r.gpr[2] = base ? 0x100 : 0x200;
    r.gpr[4] = target;
    r.gpr[5] = 0x10203040;
    r.gpr[6] = 0x220;
    r.gpr[9] = 0x300;
    r.pc = 0x100;
    r.cr = 0x9abcdef0;
    r.xer = (flags << 29) | 0x42;
    uint32_t registers[32];
    memcpy(registers, r.gpr, sizeof registers);
    unsigned rd = alias == 1 ? 1 : alias == 2 ? 2 : 3;
    unsigned rs = alias == 3 ? rd : alias == 4 ? 0 : 5;
    unsigned pc = 0x100;
    auto emit = [&](uint32_t op) {
        put(memory + pc, op);
        pc += 4;
    };
    bool reservation = sequence != 0;
    if (reservation) {
        emit(load(rd, base ? 1 : 0, 2));
        registers[rd] = get(memory + 0x200);
    }
    if (sequence == 2) {
        emit(load(7, 0, 6));
        registers[7] = get(memory + 0x220);
    }
    if (sequence == 4) {
        emit(0x18000001);
        reservation = false;
    }
    if (sequence == 5)
        emit(0x90A60000); // An ordinary store by this CPU preserves the reservation.
    emit(store(rs, 0, 4));
    uint32_t condition = (reservation ? 2 : 0) | (flags >> 2);
    uint32_t expected_cr = (r.cr & 0x0fffffff) | (condition << 28);
    if (sequence == 3) {
        emit((31u << 26) | (8 << 21) | (19 << 1)); // mfcr r8.
        registers[8] = expected_cr;
        emit(store(5, 0, 9)); // Every stwcx. consumes the reservation.
        expected_cr = (r.cr & 0x0fffffff) | ((flags >> 2) << 28);
    }
    emit(0x18000000);
    memcpy(expected_memory, memory, sizeof memory);
    if (sequence == 4)
        put(expected_memory + 0x200, 0xabcdef01);
    if (sequence == 5)
        put(expected_memory + 0x220, registers[5]);
    if (reservation)
        put(expected_memory + target, registers[rs]);
    ROMlib_ppc_environment env = {};
    env.memory = map;
    env.service = service;
    if (mode & 1) {
        env.linear_memory = memory;
        env.linear_size = sizeof memory;
    }
    env.execution_flags = mode & 2 ? ROMLIB_PPC_EXEC_JIT : 0;
    if (cache && mode == 3) {
        env.translation_cache = cache;
        ROMlib_ppc_translate_range(cache, memory + 0x100, 0x100, pc - 0x100);
    }
    assert(ROMlib_ppc_execute(&r, &env));
    assert(!memcmp(r.gpr, registers, sizeof registers));
    assert(r.cr == expected_cr && r.xer == ((flags << 29) | 0x42));
    assert(!memcmp(memory, expected_memory, sizeof memory));
    if (cache && mode == 3) {
        unsigned kernels = 0;

        assert(env.native_instructions == (pc - 0x100) / 4 - 1 - (sequence == 4) && !env.native_compilations &&
               env.native_kernel_instructions == kernels);
    }
}

static void faults(ROMlib_ppc_translation_cache* cache)
{
    auto run = [&](ROMlib_ppc_registers& r, ROMlib_ppc_environment& env, unsigned length) {
        if (cache) {
            env.linear_memory = memory;
            env.linear_size = sizeof memory;
            env.execution_flags = ROMLIB_PPC_EXEC_JIT;
            env.translation_cache = cache;
            ROMlib_ppc_translate_range(cache, memory + r.pc, r.pc, length);
        }
        return ROMlib_ppc_execute(&r, &env);
    };
    for (unsigned conditional = 0; conditional < 2; ++conditional)
        for (unsigned invalid = 0; invalid < 2; ++invalid)
            for (unsigned low = 0; low < 4; ++low) {
                if (!invalid && !low)
                    continue;
                memset(memory, 0, sizeof memory);
                uint32_t op = conditional ? store(3, 0, 2) : load(3, 0, 2);
                if (invalid)
                    op ^= 1;
                put(memory + 0x100, op);
                put(memory + 0x104, 0x18000000);
                ROMlib_ppc_registers r = {};
                r.pc = 0x100;
                r.gpr[2] = 0x200 + low;
                r.gpr[3] = 0xabcdef01;
                r.cr = 0x9abcdef0;
                r.xer = 0xe0000042;
                ROMlib_ppc_environment env = {};
                env.memory = map;
                data_accesses = 0;
                assert(!run(r, env, 4));
                assert(strstr(env.error, invalid ? "illegal PowerPC instruction" : "unaligned PowerPC reservation"));
                assert(r.pc == 0x100 && r.gpr[3] == 0xabcdef01 && r.cr == 0x9abcdef0 && r.xer == 0xe0000042);
                assert(!data_accesses);
            }
    // No reservation: an aligned unmapped destination is not accessed.
    put(memory, store(3, 0, 2));
    put(memory + 4, 0x18000000);
    ROMlib_ppc_registers r = {};
    r.gpr[2] = 0x10000;
    r.xer = 0xe0000042;
    ROMlib_ppc_environment env = {};
    env.memory = map;
    data_accesses = 0;
    assert(run(r, env, 4) && !data_accesses);
    if (cache)
        assert(env.native_instructions == 1 && env.native_kernel_instructions == 0u && !env.native_compilations);
    assert(r.cr == 0x10000000 && r.xer == 0xe0000042);
    // Aligned invalid loads/stores retain the normal bounded memory diagnostic.
    for (unsigned conditional = 0; conditional < 2; ++conditional) {
        put(memory, load(3, 0, 2));
        put(memory + 4, store(3, 0, 4));
        put(memory + 8, 0x18000000);
        r = {};
        r.gpr[2] = conditional ? 0x200 : 0x10000;
        r.gpr[3] = 0xabcdef01;
        r.gpr[4] = 0x10000;
        r.cr = 0x9ABCDEF0;
        assert(!run(r, env, 8));
        assert(strstr(env.error, conditional ? "write fault" : "read fault"));
        assert(r.pc == (conditional ? 4u : 0u) && r.cr == 0x9ABCDEF0);
        if (cache)
            assert(env.native_instructions == conditional && env.native_kernel_instructions == 0);
        if (!conditional)
            assert(r.gpr[3] == 0xabcdef01);
    }
}

static void callbacks(ROMlib_ppc_translation_cache* cache)
{
    for (unsigned mode = 0; mode < 4; ++mode)
        for (unsigned nested = 0; nested < 2; ++nested) {
            memset(memory, 0, sizeof memory);
            put(memory + 0x200, 0xdeadbeef);
            put(memory + 0x204, 0xaabbccdd);
            put(memory + 0x100, load(3, 0, 2));
            if (nested) {
                put(memory + 0x80, (36u << 26) | (3 << 21) | (2 << 16));
                put(memory + 0x84, 0x18000000);
                put(memory + 0x104, store(5, 0, 4));
                put(memory + 0x108, 0x18000000);
            }
            else {
                for (unsigned i = 0; i < 4; ++i)
                    put(memory + 0x104 + i * 4, 0x60000000);
                put(memory + 0x114, 0x4200fff0);
                put(memory + 0x118, store(5, 0, 4));
                put(memory + 0x11c, 0x18000000);
            }
            ROMlib_ppc_registers r = {};
            r.pc = 0x100;
            r.ctr = 800;
            r.gpr[2] = 0x200;
            r.gpr[4] = 0x204;
            r.gpr[5] = 0x12345678;
            r.cr = 0x9abcdef0;
            r.xer = 0xe0000042;
            ROMlib_ppc_environment env = {};
            env.memory = map;
            env.poll = nested ? NULL : poll;
            if ((mode & 1) && nested) {
                // Keep instruction fetches on the mapper for the nested fixture.
                env.linear_memory = memory + 0x200;
                env.linear_address = 0x200;
                env.linear_size = sizeof memory - 0x200;
            }
            else if (mode & 1) {
                env.linear_memory = memory;
                env.linear_size = sizeof memory;
            }
            env.execution_flags = mode & 2 ? ROMLIB_PPC_EXEC_JIT : 0;
            if (cache && mode == 3 && !nested) {
                env.translation_cache = cache;
                ROMlib_ppc_translate_range(cache, memory + 0x100, 0x100, 28);
            }
            nested_pending = nested;
            polls = 0;
            assert(ROMlib_ppc_execute(&r, &env));
            assert(!nested_pending && (nested || polls == 3));
            assert(r.gpr[3] == 0xdeadbeef && r.cr == 0x1abcdef0 && r.xer == 0xe0000042);
            assert(get(memory + 0x200) == 0x01020304 && get(memory + 0x204) == 0xaabbccdd);
            if (cache && mode == 3 && !nested)
                assert(env.native_instructions > 1000 && env.native_kernel_instructions == 0u);
        }

    if (!cache)
        return;
    // Reenter through a mapped fetch between two native reservation operations.
    memset(memory, 0, sizeof memory);
    put(memory + 0x80, 0x90620000);
    put(memory + 0x84, 0x18000000);
    put(memory + 0x100, load(3, 0, 2));
    put(memory + 0x104, 0x480001FC); // b 0x300, outside the linear window.
    put(memory + 0x108, store(5, 0, 4));
    put(memory + 0x10C, 0x18000000);
    put(memory + 0x200, 0xDEADBEEF);
    put(memory + 0x204, 0xAABBCCDD);
    put(memory + 0x300, 0x4BFFFE08); // b 0x108.
    ROMlib_ppc_registers r = {};
    r.pc = 0x100;
    r.gpr[2] = 0x200;
    r.gpr[4] = 0x204;
    r.gpr[5] = 0x12345678;
    r.cr = 0x9ABCDEF0;
    r.xer = 0xE0000042;
    ROMlib_ppc_environment env = {};
    env.memory = map;
    env.linear_memory = memory;
    env.linear_size = 0x240;
    env.execution_flags = ROMLIB_PPC_EXEC_JIT;
    env.translation_cache = cache;
    ROMlib_ppc_translate_range(cache, memory + 0x100, 0x100, 12);
    nested_address = 0x300;
    nested_pending = true;
    assert(ROMlib_ppc_execute(&r, &env) && !nested_pending);
    assert(r.gpr[3] == 0xDEADBEEF && r.cr == 0x1ABCDEF0 && r.xer == 0xE0000042);
    assert(get(memory + 0x200) == 0x01020304 && get(memory + 0x204) == 0xAABBCCDD);
    assert(env.native_instructions == 3 && env.native_kernel_instructions == 0u && !env.native_compilations);
}

int main(void)
{
    unsigned cases = 0;
    auto cache = ROMlib_ppc_translation_cache_create();
    const unsigned targets[] = {0x200, 0x204, 0x220, 0x300};
    for (unsigned mode = 0; mode < 4; ++mode)
        for (unsigned base = 0; base < 2; ++base)
            for (unsigned alias = 0; alias < 5; ++alias)
                for (unsigned target : targets)
                    for (unsigned sequence = 0; sequence < 6; ++sequence)
                        for (unsigned flags = 0; flags < 8; ++flags) {
                            check(base, alias, target, sequence, flags, mode, cache);
                            ++cases;
                        }
    faults(nullptr);
    if (cache)
        faults(cache);
    callbacks(cache);
    ROMlib_ppc_translation_cache_destroy(cache);
    printf("%u MPC750 reservation/CR/alias/clear cases and encoding/alignment faults passed\n", cases);
}
