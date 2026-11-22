/*+===================================================================
  File:      ppc_fused_status_test.cpp

  Summary:   Implements ppc fused status test support for synppc PowerPC execution and fragment loading.

  Classes:   Used: ROMlib_ppc_environment, ROMlib_ppc_registers

  Functions: ppc_fused_status_check

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
#include <stdio.h>
#include <string.h>
#include <fenv.h>
static unsigned char memory[8];

static void* map(void*, uint32_t a, size_t n, int)
{
    return a <= sizeof memory && n <= sizeof memory - a ? memory + a : NULL;
}

int ppc_fused_status_check(FILE* cases)
{
    fenv_t original;
    assert(!fegetenv(&original));
    assert(!fesetround(FE_DOWNWARD));
    assert(!feclearexcept(FE_ALL_EXCEPT));
    assert(!feraiseexcept(FE_INVALID));
    unsigned op, single, count = 0;
    unsigned direct = 0, exceptional = 0;
    auto* cache = ROMlib_ppc_jit_available() ? ROMlib_ppc_translation_cache_create() : nullptr;
    assert(!ROMlib_ppc_jit_available() || cache);
    unsigned long long a, b, c, initial, expected, status, mask;
    while (fscanf(cases, "%u %u %llx %llx %llx %llx %llx %llx %llx", &op, &single, &a, &b, &c, &initial, &expected,
                  &status, &mask) == 9) {
        unsigned destination = 1 + count % 4;
        uint32_t opcode = ((single ? 59u : 63u) << 26) | (destination << 21) | (1 << 16) | (2 << 11) | (3 << 6) |
                          (op << 1) | (count & 1);
        uint64_t result = 0;
        uint32_t actual_status = initial;
        assert(romaw_ppc_fused_value(opcode, a, b, c, &actual_status, &result) == 1);
        if (result != expected || (actual_status & mask) != (status & mask)) {
            fprintf(stderr, "case %u op%u/%u a%llx b%llx c%llx initial%llx got%llx/%08x expected%llx/%llx mask%llx\n",
                    count, op, single, a, b, c, initial, (unsigned long long)result, actual_status, expected, status,
                    mask);
            return 1;
        }
        assert(fegetround() == FE_DOWNWARD && fetestexcept(FE_ALL_EXCEPT) == FE_INVALID);
        if (count % 53 == 0) {
            for (unsigned i = 0; i < 4; ++i)
                memory[i] = opcode >> (24 - 8 * i);
            memory[4] = 0x18;
            for (unsigned native = 0; native <= unsigned(cache != nullptr); ++native) {
                ROMlib_ppc_environment env = {};
                env.memory = map;
                env.instruction_limit = 4;
                ROMlib_ppc_registers r = {};
                r.fpscr = initial;
                r.cr = 0x13579bdf;
                memcpy(&r.fpr[1], &a, 8);
                memcpy(&r.fpr[2], &b, 8);
                memcpy(&r.fpr[3], &c, 8);
                if (native) {
                    env.linear_memory = memory;
                    env.linear_size = sizeof memory;
                    env.translation_cache = cache;
                    env.execution_flags = ROMLIB_PPC_EXEC_JIT;
                    assert(ROMlib_ppc_translate_range(cache, memory, 0, 4) <= 1);
                }
                assert(ROMlib_ppc_execute(&r, &env));
                memcpy(&result, &r.fpr[destination], 8);
                if (result != expected || (r.fpscr & mask) != (status & mask))
                    fprintf(
                        stderr,
                        "native=%u fused case %u op%u/%u a%llx b%llx c%llx initial%llx got%llx/%x expected%llx/%llx\n",
                        native, count, op, single, a, b, c, initial, (unsigned long long)result, r.fpscr, expected,
                        status);
                assert(result == expected && (r.fpscr & mask) == (status & mask));
                assert(r.cr ==
                       ((opcode & 1) ? ((0x13579bdf & ~0x0f000000u) | ((r.fpscr >> 4) & 0x0f000000u)) : 0x13579bdf));
                assert(fegetround() == FE_DOWNWARD && fetestexcept(FE_ALL_EXCEPT) == FE_INVALID);
                if (native) {
                    bool special =
                        ((a >> 52) & 0x7ff) == 0x7ff || ((b >> 52) & 0x7ff) == 0x7ff || ((c >> 52) & 0x7ff) == 0x7ff;
                    assert(env.native_instructions == 1 && !env.native_compilations && env.native_cache_hits == 1 &&
                           env.native_kernel_instructions == unsigned(special));
                    (special ? exceptional : direct)++;
                }
            }
        }
        ++count;
    }
    assert(feof(cases) && count > 250000);
    assert(!fesetenv(&original));
    ROMlib_ppc_translation_cache_destroy(cache);
    printf("%u independent fused FPSCR cases passed\n", count);
    printf("Native fused oracle samples: %u direct, %u exceptional\n", direct, exceptional);
    return 0;
}
