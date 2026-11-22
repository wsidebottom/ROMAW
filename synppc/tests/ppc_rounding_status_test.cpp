/*+===================================================================
  File:      ppc_rounding_status_test.cpp

  Summary:   Implements ppc rounding status test support for synppc PowerPC execution and fragment loading.

  Classes:   Used: ROMlib_ppc_environment, ROMlib_ppc_registers

  Functions: ppc_rounding_status_check

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
static unsigned char memory[8];

static void* map(void*, uint32_t a, size_t n, int)
{
    return a <= sizeof memory && n <= sizeof memory - a ? memory + a : NULL;
}

int ppc_rounding_status_check(FILE* cases)
{
    const unsigned operations[] = {21, 20, 25, 18, 22, 12};
    unsigned operation, single, count = 0;
    auto* cache = ROMlib_ppc_translation_cache_create();
    unsigned direct = 0, exceptional = 0;
    unsigned long long a, b, initial, expected, status, mask;
    while (fscanf(cases, "%u %u %llx %llx %llx %llx %llx %llx", &operation, &single, &a, &b, &initial, &expected,
                  &status, &mask) == 8) {
        unsigned destination = 1 + count % 4;
        unsigned code = ((single && operation != 5 ? 59u : 63u) << 26) | (destination << 21) |
                        (operations[operation] << 1) | (count & 1);
        uint64_t first = a, second = b, third = 0;
        if (operation >= 4) {
            code |= 1 << 11;
            second = a;
            first = 0;
        }
        else if (operation == 2) {
            code |= (1 << 16) | (2 << 6);
            third = b;
            second = 0;
        }
        else
            code |= (1 << 16) | (2 << 11);
        uint32_t actual_status = initial;
        uint64_t actual = 0;
        assert(romaw_ppc_arithmetic_value(code, first, second, third, &actual_status, &actual) == 1);
        if (actual != expected || (actual_status & mask) != (status & mask)) {
            fprintf(stderr,
                    "case %u op%u single%u a%llx b%llx initial%llx: got %llx/%08x expected %llx/%llx mask%llx\n", count,
                    operation, single, a, b, initial, (unsigned long long)actual, actual_status, expected, status,
                    mask);
            return 1;
        }
        if (count % 37 == 0) {
            for (unsigned i = 0; i < 4; ++i)
                memory[i] = code >> (24 - 8 * i);
            memory[4] = 0x18;
            for (unsigned native = 0; native <= unsigned(cache != nullptr); ++native) {
                ROMlib_ppc_registers r = {};
                ROMlib_ppc_environment env = {};
                env.memory = map;
                env.instruction_limit = 4;
                r.fpscr = initial;
                r.cr = 0x13579bdf;
                memcpy(&r.fpr[1], &a, 8);
                memcpy(&r.fpr[2], &b, 8);
                if (native) {
                    env.linear_memory = memory;
                    env.linear_size = sizeof memory;
                    env.translation_cache = cache;
                    env.execution_flags = ROMLIB_PPC_EXEC_JIT;
                    assert(ROMlib_ppc_translate_range(cache, memory, 0, 4) <= 1);
                }
                assert(ROMlib_ppc_execute(&r, &env));
                memcpy(&actual, &r.fpr[destination], 8);
                if (actual != expected || (r.fpscr & mask) != (status & mask))
                    fprintf(stderr, "native=%u rounding case %u opcode %08x got %llx/%08x expected %llx/%llx\n", native,
                            count, code, (unsigned long long)actual, r.fpscr, expected, status);
                assert(actual == expected && (r.fpscr & mask) == (status & mask));
                assert(r.cr ==
                       ((code & 1) ? ((0x13579bdf & ~0x0f000000u) | ((r.fpscr >> 4) & 0x0f000000u)) : 0x13579bdf));
                if (native) {
                    assert(env.native_instructions == 1 && env.native_cache_hits == 1 && !env.native_compilations &&
                           env.native_kernel_instructions <= 1);
                    (env.native_kernel_instructions ? exceptional : direct)++;
                }
            }
        }
        ++count;
    }
    assert(count > 100000);
    ROMlib_ppc_translation_cache_destroy(cache);
    printf("%u independent PowerPC rounding/status cases passed\n", count);
    printf("Native rounding/status oracle samples: %u direct, %u exceptional\n", direct, exceptional);
    return 0;
}
