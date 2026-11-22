/*+===================================================================
  File:      jit-x64-ccr-test.c

  Summary:   Implements jit x64 ccr test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Exhaustive CCR bit combinations, raw booleans and byte TAS values. */
#include "jit-x64.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned status(void)
{
    return !!cpu_state.ccc | !!cpu_state.ccv << 1 | !cpu_state.ccnz << 2 | !!cpu_state.ccn << 3 | !!cpu_state.ccx << 4;
}

int main(void)
{
    void* memory = calloc(1, 0x10000);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(0x10000, 0, 0);
    uint32 vectors[64] = {0};
    initialize_68k_emulator(NULL, 1, vectors, 0);
    unsigned cases = 0;
    const unsigned bases[] = {0x42C0, 0x44C0, 0x4AC0, 0x003C, 0x023C, 0x0A3C};
    for (unsigned kind = 0; kind < 6; ++kind)
        for (unsigned reg = 0; reg < (kind < 3 ? 8 : 1); ++reg) {
            unsigned length = kind < 3 ? 1 : 2;
            WRITEUW(0x1000, bases[kind] | reg);
            WRITEUW(0x1002 + length * 2, 0);
            WRITEUW(0x1000 + length * 2, 0x4ED0); /* JMP (A0) */
            EM_A0 = MAGIC_EXIT_EMULATOR_ADDRESS;
            for (unsigned operand = 0; operand < 256; ++operand) {
                WRITEUW(0x1002, kind < 3 ? 0x4ED0 : 0xAB00 | operand);
                int8 lengths[] = {length, 1};
                unsigned translated;
                int terminal;
                void* code = syn68k_x64_compile(SYN68K_TO_US(0x1000), lengths, 2, &translated, 0x1000, &terminal);
                assert(code && translated == 2 && terminal);
                for (unsigned backend = 0; backend < 3; ++backend) {
                    syn68k_x64_enabled = backend != 0;
                    destroy_blocks(0x1000, 16);
                    for (unsigned cc = 0; cc < 32; ++cc) {
                        cpu_state.ccc = cc & 1 ? 0x80000000 : 0;
                        cpu_state.ccv = cc & 2 ? 0x12345678 : 0;
                        cpu_state.ccnz = cc & 4 ? 0 : 0x80008000;
                        cpu_state.ccn = cc & 8 ? 0x87654321 : 0;
                        cpu_state.ccx = cc & 16 ? 0x11111111 : 0;
                        cpu_state.sr = 0x2700;
                        uint32 original = 0x1234AB00 | operand;
                        EM_DREG(reg) = original;
                        unsigned flags = kind == 1   ? operand & 31
                                         : kind == 2 ? (cc & 16) | (operand & 128 ? 8 : 0) | (operand ? 0 : 4)
                                         : kind == 3 ? cc | (operand & 31)
                                         : kind == 4 ? cc & operand
                                         : kind == 5 ? cc ^ (operand & 31)
                                                     : cc;
                        uint32 expected = kind == 0   ? (original & 0xFFFF0000) | cc
                                          : kind == 2 ? original | 128
                                                      : original;
                        uint64 before = syn68k_native_execution_count();
                        if (backend == 2)
                            assert(syn68k_x64_execute(code) == MAGIC_EXIT_EMULATOR_ADDRESS);
                        else
                            interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
                        assert((syn68k_native_execution_count() > before) == (backend != 0));
                        assert(EM_DREG(reg) == expected);
                        assert(status() == flags && cpu_state.sr == 0x2700);
                        ++cases;
                    }
                }
                syn68k_x64_release(code);
            }
        }
    printf("%u CCR/transfer/TAS interpreter/JIT/direct cases passed.\n", cases);
    return 0;
}
