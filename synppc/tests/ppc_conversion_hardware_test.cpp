/*+===================================================================
  File:      ppc_conversion_hardware_test.cpp

  Summary:   Implements ppc conversion hardware test support for synppc PowerPC execution and fragment loading.

  Classes:   Used: ROMlib_ppc_environment, ROMlib_ppc_registers

  Functions: main

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Replay original physical-Mac FCTIW/FCTIWZ captures. */
#include "../src/ppc-arithmetic.h"
#include "synppc/ppc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char memory[20];

static void* map(void*, uint32_t address, size_t size, int)
{
    return address <= sizeof memory && size <= sizeof memory - address ? memory + address : NULL;
}

int main(void)
{
    unsigned opcode, initial, status, checked = 0, errors = 0;
    unsigned long long a, b, c, expected;
    while (scanf("%x %llx %llx %llx %x %llx %x", &opcode, &a, &b, &c, &initial, &expected, &status) == 7) {
        unsigned operation = (opcode >> 1) & 1023;
        assert(opcode >> 26 == 63 && (operation == 14 || operation == 15));
        uint64_t actual = 0;
        uint32_t actual_status = initial;
        assert(romaw_ppc_integer_value(b, operation == 15 ? 1 : initial & 3, &actual_status, &actual));
        if (uint32_t(actual) != uint32_t(expected) || actual_status != status) {
            fprintf(stderr, "%08x %016llx FPSCR=%08x: %08x/%08x %08x/%08x\n", opcode, b, initial, uint32_t(actual),
                    uint32_t(expected), actual_status, status);
            ++errors;
            continue;
        }
        for (unsigned destination = 1; destination < 5; ++destination)
            for (unsigned record = 0; record < 2; ++record)
                for (unsigned jit = 0; jit < 2; ++jit) {
                    unsigned code = (opcode & ~0x03e00001u) | (destination << 21) | record;
                    for (unsigned i = 0; i < 4; ++i)
                        memory[i] = code >> (24 - i * 8);
                    // Follow the conversion with STFIWX to exercise the integer
                    // result's direct memory path, without a host FP conversion.
                    uint32_t store = (31u << 26) | (destination << 21) | (5 << 11) | (983 << 1);
                    for (unsigned i = 0; i < 4; ++i)
                        memory[4 + i] = store >> (24 - i * 8);
                    memory[8] = 0x18;
                    memset(memory + 16, 0x5a, 4);
                    ROMlib_ppc_registers registers = {};
                    ROMlib_ppc_environment environment = {};
                    environment.memory = map;
                    environment.instruction_limit = 3;
                    environment.execution_flags = jit ? ROMLIB_PPC_EXEC_JIT : 0;
                    registers.fpscr = initial;
                    registers.cr = 0x13579bdf;
                    registers.gpr[5] = 16;
                    memcpy(&registers.fpr[2], &b, 8);
                    assert(ROMlib_ppc_execute(&registers, &environment));
                    memcpy(&actual, &registers.fpr[destination], 8);
                    assert(uint32_t(actual) == uint32_t(expected) && registers.fpscr == status);
                    for (unsigned i = 0; i < 4; ++i)
                        assert(memory[16 + i] == ((uint32_t(expected) >> (24 - i * 8)) & 255));
                    assert(registers.cr ==
                           (record ? (0x13579bdf & ~0x0f000000u) | ((status >> 4) & 0x0f000000u) : 0x13579bdf));
                }
        ++checked;
    }
    printf("%u hardware conversion cases checked, %u mismatches\n", checked, errors);
    return !checked || errors;
}
