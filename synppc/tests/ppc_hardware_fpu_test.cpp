/*+===================================================================
  File:      ppc_hardware_fpu_test.cpp

  Summary:   Implements ppc hardware fpu test support for synppc PowerPC execution and fragment loading.

  Classes:   Used: ROMlib_ppc_environment, ROMlib_ppc_registers

  Functions: main

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Compare captured real PowerPC arithmetic values/status with the adapter. */
#include "../src/ppc-arithmetic.h"
#include "synppc/ppc.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static unsigned char memory[8];

static void* map(void*, uint32_t address, size_t size, int)
{
    return address <= sizeof memory && size <= sizeof memory - address ? memory + address : NULL;
}

static void dispatch(unsigned opcode, uint64_t a, uint64_t b, uint64_t c, unsigned incoming, uint64_t expected,
                     unsigned status, unsigned mask)
{
    for (unsigned destination = 1; destination <= 4; ++destination)
        for (unsigned record = 0; record < 2; ++record)
            for (unsigned jit = 0; jit < 2; ++jit) {
                unsigned code = (opcode & ~0x03e00001u) | (destination << 21) | record;
                for (unsigned i = 0; i < 4; ++i)
                    memory[i] = code >> (24 - i * 8);
                memory[4] = 0x18;
                ROMlib_ppc_registers registers = {};
                ROMlib_ppc_environment environment = {};
                environment.memory = map;
                environment.instruction_limit = 2;
                environment.execution_flags = jit ? ROMLIB_PPC_EXEC_JIT : 0;
                registers.fpscr = incoming;
                registers.cr = 0x13579bdf;
                memcpy(&registers.fpr[1], &a, 8);
                memcpy(&registers.fpr[2], &b, 8);
                memcpy(&registers.fpr[3], &c, 8);
                assert(ROMlib_ppc_execute(&registers, &environment));
                uint64_t actual;
                memcpy(&actual, &registers.fpr[destination], 8);
                assert(actual == expected && (registers.fpscr & mask) == (status & mask));
                assert(registers.cr ==
                       (record ? (0x13579bdf & ~0x0f000000u) | ((status >> 4) & 0x0f000000u) : 0x13579bdf));
            }
}

static int evaluate(unsigned opcode, uint64_t a, uint64_t b, uint64_t c, uint32_t* status, uint64_t* result)
{
    int handled = romaw_ppc_arithmetic_value(opcode, a, b, c, status, result);
    if (!handled)
        handled = romaw_ppc_estimate_value(opcode, b, status, result);
    if (!handled)
        handled = romaw_ppc_fused_value(opcode, a, b, c, status, result);
    return handled;
}

int main(int argc, char** argv)
{
    bool include_ni = false, instructions = false, reciprocal_powers = false;
    for (int i = 1; i < argc; ++i)
        if (!strcmp(argv[i], "--include-ni"))
            include_ni = true;
        else if (!strcmp(argv[i], "--dispatch"))
            instructions = true;
        else if (!strcmp(argv[i], "--reciprocal-powers"))
            reciprocal_powers = true;
        else
            return 2;
    unsigned opcode, incoming, expected_status, checked = 0, skipped = 0, errors = 0;
    unsigned long long a, b, c, expected;
    while (scanf("%x %llx %llx %llx %x %llx %x", &opcode, &a, &b, &c, &incoming, &expected, &expected_status) == 7) {
        // The G5's NI behavior is not the 750's documented behavior. Estimates
        // may differ within their architecture-defined error bound too.
        unsigned operation = (opcode >> 1) & 31;
        if (((incoming & 4) && !include_ni) || operation == 26 ||
            (operation == 24 &&
             (!reciprocal_powers || (b & UINT64_C(0x000fffffffffffff)) || !(b & UINT64_C(0x7ff0000000000000)) ||
              (b & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000)))) {
            ++skipped;
            continue;
        }
        if (opcode >> 26 == 59 && operation != 24) {
            // Single arithmetic inputs must have single-precision encodings.
            bool valid = true;
            const uint64_t inputs[] = {a, b, c};
            for (uint64_t input : inputs) {
                uint32_t small = romaw_ppc_arithmetic(ppc_fp_round_single, true, 0, input, 0).bits;
                if (romaw_ppc_expand_single(small) != input)
                    valid = false;
            }
            if (!valid) {
                ++skipped;
                continue;
            }
        }
        uint32_t status = incoming;
        uint64_t result = 0;
        int handled = evaluate(opcode, a, b, c, &status, &result);
        assert(handled == 1);
        // FRES FR/FI are architecturally undefined; do not impose hardware bits.
        // The 603 capture also sets XX on some range paths. Keep the target's
        // documented unchanged-XX policy, independently checked by estimate tests.
        unsigned mask = operation == 24 ? ~0x02060000u : ~0u;
        ++checked;
        if (result != expected || (status & mask) != (expected_status & mask)) {
            fprintf(stderr, "%08x %016llx RN=%u: %016llx/%016llx %08x/%08x\n", opcode, a, incoming & 3,
                    (unsigned long long)result, expected, status, expected_status);
            ++errors;
            continue;
        }
        else if (instructions)
            dispatch(opcode, a, b, c, incoming, expected, expected_status, mask);
        // Repeat an already-recorded cause after explicitly clearing FX. Sticky
        // UX/XX must not recreate FX, while FI and the result still update.
        uint32_t repeated = (incoming | (expected_status & 0x1ff80700u)) & ~0x80000000u;
        uint64_t repeated_result = 0;
        handled = evaluate(opcode, a, b, c, &repeated, &repeated_result);
        assert(handled == 1 && repeated_result == expected &&
               (repeated & mask) == (expected_status & mask & ~0x80000000u));
    }
    printf("%u hardware arithmetic cases checked, %u excluded, %u mismatches\n", checked, skipped, errors);
    assert(checked);
    return errors != 0;
}
