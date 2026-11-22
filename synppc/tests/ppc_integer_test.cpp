/*+===================================================================
  File:      ppc_integer_test.cpp

  Summary:   Implements ppc integer test support for synppc PowerPC execution and fragment loading.

  Classes:   Used: ROMlib_ppc_environment, ROMlib_ppc_registers, ppc_integer_result

  Functions: ppc_integer_check

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../src/ppc-arithmetic.h"
#include "synppc/ppc.h"
extern "C" {
#include "../third_party/softfloat3/source/include/softfloat.h"
}
#include <assert.h>
#include "ppc_model_checks.h"
#include <fenv.h>
#include <stdio.h>
#include <string.h>

static unsigned char memory[128];
static ROMlib_ppc_translation_cache* cache;

static void* map(void*, uint32_t address, size_t size, int)
{
    return address <= sizeof memory && size <= sizeof memory - address ? memory + address : NULL;
}

static void check_instruction(uint64_t input, uint32_t fpscr, bool zero, unsigned destination, bool record,
                              uint32_t expected, uint32_t expected_status, bool inhibited)
{
    uint32_t opcode = (63u << 26) | (destination << 21) | (1 << 11) | ((zero ? 15 : 14) << 1) | record;
    unsigned pc = (destination * 4 + unsigned(record) * 2 + unsigned(zero)) * 8;
    for (unsigned i = 0; i < 4; ++i)
        memory[pc + i] = opcode >> (24 - 8 * i);
    memory[pc + 4] = 0x18;
    ROMlib_ppc_registers r = {};
    r.pc = pc;
    ROMlib_ppc_environment env = {};
    env.memory = map;
    env.linear_memory = memory;
    env.linear_size = sizeof memory;
    env.instruction_limit = 4;
    r.fpscr = fpscr;
    r.cr = 0x13579bdf;
    uint64_t sentinel = UINT64_C(0x0123456789abcdef);
    memcpy(&r.fpr[destination], &sentinel, sizeof sentinel);
    memcpy(&r.fpr[1], &input, sizeof input);
    uint64_t previous;
    memcpy(&previous, &r.fpr[destination], sizeof previous);
    const auto initial = r;
    for (unsigned native = 0; native <= unsigned(cache != nullptr); ++native) {
        r = initial;
        if (native) {
            env.execution_flags = ROMLIB_PPC_EXEC_JIT;
            env.translation_cache = cache;
            assert(ROMlib_ppc_translate_range(cache, memory + pc, pc, 4) <= 1);
        }
        assert(ROMlib_ppc_execute(&r, &env));
        uint64_t actual;
        memcpy(&actual, &r.fpr[destination], sizeof actual);
        if (actual != (inhibited ? previous : expected) || r.fpscr != expected_status)
            fprintf(stderr,
                    "Conversion %016llx FPSCR %08x opcode %08x native %u: got %016llx/%08x, "
                    "expected %016llx/%08x\n",
                    (unsigned long long)input, fpscr, opcode, native, (unsigned long long)actual, r.fpscr,
                    (unsigned long long)(inhibited ? previous : expected), expected_status);
        assert(actual == (inhibited ? previous : expected) && r.fpscr == expected_status);
        assert(r.cr == (record ? ((0x13579bdf & ~0x0f000000u) | ((r.fpscr >> 4) & 0x0f000000)) : 0x13579bdf));
        assert(env.native_instructions == native && !env.native_kernel_instructions && !env.native_compilations);
    }
}

static void instruction(uint64_t input, unsigned rn, bool zero, unsigned destination, bool record, uint32_t expected,
                        uint32_t status, unsigned extra = 0)
{
    bool inhibited = (status & 0x100) && (extra & 0x80);
    uint32_t expected_status = (status & ~3u) | rn | 0x10000 | extra;
    if (((status & 0x20000000) && (extra & 0x80)) || ((status & 0x02000000) && (extra & 8)))
        expected_status |= 0x40000000;
    check_instruction(input, rn | 0x70000 | extra, zero, destination, record, expected, expected_status, inhibited);
}

static void sticky_and_summary(void)
{
    uint64_t output;
    uint32_t status = 0x02000008; // XX already set, FX explicitly clear, XE set.
    assert(romaw_ppc_integer_value(UINT64_C(0x3ff8000000000000), 0, &status, &output));
    assert(output == 2 && status == 0x42060008); // No new XX transition.
    check_instruction(UINT64_C(0x3ff8000000000000), 0x02000008, false, 1, true, 2, 0x42060008, false);
    status = 0x01000180; // Both invalid causes already set, FX clear, VE set.
    output = 123;
    assert(!romaw_ppc_integer_value(UINT64_C(0x7ff0000000000001), 0, &status, &output));
    assert(output == 123 && status == 0x61000180);
    check_instruction(UINT64_C(0x7ff0000000000001), 0x01000180, true, 2, true, 0, 0x61000180, true);
    // Every pre-existing exception participates in FEX, even on exact conversion.
    const uint32_t conditions[] = {0x20000100, 0x10000000, 0x08000000, 0x04000000, 0x02000000};
    const uint32_t enables[] = {0x80, 0x40, 0x20, 0x10, 8};
    for (unsigned i = 0; i < 5; ++i) {
        status = conditions[i] | enables[i] | 0x60000;
        assert(romaw_ppc_integer_value(0, 0, &status, &output));
        assert(status == (conditions[i] | enables[i] | 0x40000000));
        check_instruction(0, conditions[i] | enables[i] | 0x60000, false, 1, true, 0, status, false);
    }
    status = 0x60000000; // Stale summaries clear if there are no causes.
    assert(romaw_ppc_integer_value(0, 0, &status, &output));
    assert(status == 0);
    check_instruction(0, 0x60000000, true, 3, false, 0, 0, false);
}

static void reserved(void)
{
    for (unsigned ext = 14; ext <= 15; ++ext)
        for (unsigned field = 1; field < 32; ++field) {
            uint32_t opcode = (63u << 26) | (field << 16) | (ext << 1);
            for (unsigned i = 0; i < 4; ++i)
                memory[i] = opcode >> (24 - 8 * i);
            ROMlib_ppc_registers r = {};
            ROMlib_ppc_environment env = {};
            env.memory = map;
            env.instruction_limit = 4;
            r.fpr[0] = 17;
            r.fpscr = 0x12340000;
            r.cr = 0x13579bdf;
            assert(!ROMlib_ppc_execute(&r, &env));
            assert(strstr(env.error, "illegal PowerPC instruction"));
            assert(r.fpr[0] == 17 && r.fpscr == 0x12340000 && r.cr == 0x13579bdf);
        }
}

int ppc_integer_check(FILE* cases)
{
    unsigned mode, expected, flags, incremented, status, count = 0;
    unsigned long long bits;
    softfloat_roundingMode = softfloat_round_odd;
    softfloat_detectTininess = softfloat_tininess_afterRounding;
    softfloat_exceptionFlags = softfloat_flag_underflow;
    fenv_t original;
    assert(!fegetenv(&original));
    assert(!fesetround(FE_DOWNWARD));
    cache = ROMlib_ppc_translation_cache_create();
    while (fscanf(cases, "%u %llx %x %u %u %x", &mode, &bits, &expected, &flags, &incremented, &status) == 6) {
        ppc_integer_result r = romaw_ppc_integer(bits, mode);
        if (r.bits != expected || r.flags != flags || r.incremented != bool(incremented))
            fprintf(stderr, "Integer case %u %llx RN=%u got %x/%u/%u expected %x/%u/%u\n", count, bits, mode, r.bits,
                    r.flags, r.incremented, expected, flags, incremented);
        assert(r.bits == expected && r.flags == flags && r.incremented == bool(incremented));
        instruction(bits, mode, false, 1 + count % 3, count & 1, expected, status);
        instruction(bits, mode, false, 1, true, expected, status, 0x88);
        if (mode == 1)
            for (unsigned rn = 0; rn < 4; ++rn)
                instruction(bits, rn, true, 2, true, expected, status);
        assert(softfloat_roundingMode == softfloat_round_odd);
        assert(softfloat_detectTininess == softfloat_tininess_afterRounding);
        assert(softfloat_exceptionFlags == softfloat_flag_underflow);
        assert(fegetround() == FE_DOWNWARD);
        ++count;
    }
    assert(count > 8000);
    sticky_and_summary();
    reserved();
    ROMlib_ppc_translation_cache_destroy(cache);
    assert(!fesetenv(&original));
    printf("%u rational integer-conversion cases passed\n", count);
    return 0;
}
