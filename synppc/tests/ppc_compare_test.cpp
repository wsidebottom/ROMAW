/*+===================================================================
  File:      ppc_compare_test.cpp

  Summary:   Implements ppc compare test support for synppc PowerPC execution and fragment loading.

  Classes:   Used: ROMlib_ppc_environment, ROMlib_ppc_registers

  Functions: ppc_compare_check

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../src/ppc-arithmetic.h"
#include "synppc/ppc.h"
#include <assert.h>
#include "ppc_model_checks.h"
#include <fenv.h>
#include <stdio.h>
#include <string.h>

static unsigned char memory[20];
static ROMlib_ppc_translation_cache* cache;

static void* map(void*, uint32_t address, size_t size, int)
{
    return address <= sizeof memory && size <= sizeof memory - address ? memory + address : NULL;
}

static void word(unsigned offset, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        memory[offset + i] = value >> (24 - 8 * i);
}

static void instruction(uint64_t a, uint64_t b, bool ordered, uint32_t input, unsigned condition, uint32_t expected,
                        unsigned field)
{
    word(0, (63u << 26) | (field << 23) | (1 << 16) | (2 << 11) | (ordered ? 64 : 0));
    word(4, 0x18000000);
    ROMlib_ppc_registers initial = {};
    memcpy(&initial.fpr[1], &a, sizeof a);
    memcpy(&initial.fpr[2], &b, sizeof b);
    initial.fpscr = input;
    initial.cr = 0x13579bdf;
    initial.xer = 0x80000000; // Integer SO must leave the floating condition field alone.
    unsigned shift = 28 - 4 * field;
    for (unsigned native = 0; native <= unsigned(cache != nullptr); ++native) {
        auto r = initial;
        ROMlib_ppc_environment env = {};
        env.memory = map;
        env.instruction_limit = 4;
        if (native) {
            env.linear_memory = memory;
            env.linear_size = sizeof memory;
            env.translation_cache = cache;
            env.execution_flags = ROMLIB_PPC_EXEC_JIT;
            assert(ROMlib_ppc_translate_range(cache, memory, 0, 4) <= 1);
        }
        assert(ROMlib_ppc_execute(&r, &env));
        assert(!memcmp(r.fpr, initial.fpr, sizeof r.fpr) && r.xer == initial.xer);
        assert(r.fpscr == expected);
        assert(r.cr == ((initial.cr & ~(15u << shift)) | (condition << shift)));
        if (native)
            assert(env.native_instructions == 1 && env.native_cache_hits == 1 && !env.native_kernel_instructions &&
                   !env.native_compilations);
    }
}

static void sticky(void)
{
    const uint64_t snan = UINT64_C(0x7ff0000000000001);
    instruction(snan, 0, false, 0x01000000, 1, 0x21001000, 0); // VXSNAN set, FX explicitly clear.
    instruction(snan, 0, true, 0x01000080, 1, 0x61001080, 1);  // VE inhibits VXVC.
    instruction(snan, 0, true, 0x01000000, 1, 0xA1081000, 2);  // New VXVC sets FX.
    instruction(0, 0, false, 0x10000040, 2, 0x50002040, 3);    // Prior overflow and OE contribute to FEX.
}

static void reserved(void)
{
    for (unsigned ordered = 0; ordered < 2; ++ordered)
        for (unsigned fields = 1; fields < 8; ++fields) {
            word(0, (63u << 26) | (ordered ? 64 : 0) | ((fields & 3) << 21) | (fields >> 2));
            ROMlib_ppc_registers r = {};
            ROMlib_ppc_environment env = {};
            env.memory = map;
            env.instruction_limit = 4;
            r.fpscr = 0x12340000;
            r.cr = 0x13579bdf;
            assert(!ROMlib_ppc_execute(&r, &env));
            assert(strstr(env.error, "illegal PowerPC instruction"));
            assert(r.fpscr == 0x12340000 && r.cr == 0x13579bdf);
            if (cache) {
                env.linear_memory = memory;
                env.linear_size = sizeof memory;
                env.translation_cache = cache;
                env.execution_flags = ROMLIB_PPC_EXEC_JIT;
                assert(ROMlib_ppc_translate_range(cache, memory, 0, 4) <= 1);
                assert(!ROMlib_ppc_execute(&r, &env));
                assert(r.fpscr == 0x12340000 && r.cr == 0x13579bdf);
                assert(env.native_instructions == env.native_kernel_instructions);
            }
        }
}

static void compare_branch()
{
    word(0, 0xFD011000); // fcmpu cr2,f1,f2
    word(4, 0xFC60048E); // mffs f3
    word(8, 0x41880008); // bt cr2[LT],0x10
    word(12, 0);
    word(16, 0x18000000);
    assert(ROMlib_ppc_translate_range(cache, memory, 0, 12) == 1u);
    ROMlib_ppc_registers r = {};
    uint64_t a = 1, b = 2, copied;
    memcpy(&r.fpr[1], &a, sizeof a);
    memcpy(&r.fpr[2], &b, sizeof b);
    r.fpscr = 4; // NI has no effect on the comparison of these subnormals.
    r.xer = 0x80000000;
    ROMlib_ppc_environment env = {};
    env.memory = map;
    env.linear_memory = memory;
    env.linear_size = sizeof memory;
    env.execution_flags = ROMLIB_PPC_EXEC_JIT;
    env.translation_cache = cache;
    assert(ROMlib_ppc_execute(&r, &env));
    memcpy(&copied, &r.fpr[3], sizeof copied);
    assert(copied == 0x8004 && r.fpscr == copied && r.cr == 0x00800000);
    assert(env.native_instructions == 3 && env.native_blocks == 1u && !env.native_kernel_instructions);
}

int ppc_compare_check(FILE* cases)
{
    unsigned long long a, b;
    unsigned ordered, enabled, condition, expected, count = 0;
    fenv_t original;
    assert(!fegetenv(&original));
    assert(!fesetround(FE_DOWNWARD));
    cache = ROMlib_ppc_translation_cache_create();
    while (fscanf(cases, "%llx %llx %u %u %x %x", &a, &b, &ordered, &enabled, &condition, &expected) == 6) {
        uint32_t status = 0x70000 | (enabled ? 0x80 : 0);
        assert(romaw_ppc_compare_value(a, b, ordered, &status) == condition);
        assert(status == (expected | 0x70000));
        instruction(a, b, ordered, 0x70000 | (enabled ? 0x80 : 0) | (count & 7), condition,
                    expected | 0x70000 | (count & 7), (count >> 2) % 8);
        assert(fegetround() == FE_DOWNWARD);
        ++count;
    }
    assert(count == 4000);
    sticky();
    reserved();
    if (cache)
        compare_branch();
    ROMlib_ppc_translation_cache_destroy(cache);
    assert(!fesetenv(&original));
    printf("%u rational comparison/NaN cases passed in interpreter and available native execution\n", count);
    return 0;
}
