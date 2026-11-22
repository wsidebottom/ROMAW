/*+===================================================================
  File:      jit-x64-frame-test.c

  Summary:   Implements jit x64 frame test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: __wrap_trap_direct, main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Native frame operations must preserve raw CCR values and live offsets. */
#include "jit-x64.h"
#include "trap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned decoder_calls;

syn68k_addr_t __wrap_trap_direct(uint32 selector, uint32 pc, uint32 address)
{
    assert(selector == 4 && pc == 0x1000 && address == 0);
    ++decoder_calls;
    return 0x2000;
}

int main(void)
{
    void* memory = calloc(1, 0x10000);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(0x10000, 0, 0);
    uint32 vectors[64] = {0};
    initialize_68k_emulator(NULL, 1, vectors, 0);
    const uint32 offsets[] = {0, 1, -1, 0x7FFF, 0x8000, 0x80000000, 0xFFFFFFFF, 0x12345678};
    unsigned cases = 0;
    for (unsigned operation = 0; operation < 3; ++operation)
        for (unsigned reg = 0; reg < 8; ++reg) {
            WRITEUW(0x1000, (operation == 0 ? 0x4E50 : operation == 1 ? 0x4808 : 0x4E58) | reg);
            int8 length = 1;
            unsigned translated;
            int terminal;
            void* code = syn68k_x64_compile(SYN68K_TO_US(0x1000), &length, 1, &translated, 0x1000, &terminal);
            assert(code && translated == 1 && terminal);
            for (unsigned sample = 0; sample < 8; ++sample)
                for (unsigned alignment = 0; alignment < 4; ++alignment)
                    for (unsigned flags = 0; flags < 32; ++flags) {
                        uint32 expected[16];
                        for (unsigned r = 0; r < 16; ++r)
                            expected[r] = EM_DREG(r) = 0xABCDE000 + r;
                        EM_A7 = expected[15] = 0x5100 + alignment;
                        if (operation == 2)
                            EM_DREG(reg + 8) = expected[reg + 8] = 0x4000 + alignment;
                        else if (operation == 0)
                            WRITEUW(0x1002, offsets[sample]);
                        else
                            WRITEUL(0x1002, offsets[sample]);
                        uint32* cc[] = {&cpu_state.ccc, &cpu_state.ccv, &cpu_state.ccnz, &cpu_state.ccn,
                                        &cpu_state.ccx};
                        uint32 before_cc[5];
                        for (unsigned bit = 0; bit < 5; ++bit)
                            before_cc[bit] = *cc[bit] = flags & (1U << bit) ? 0x87654321 + bit : 0;
                        uint32 stack;
                        if (operation == 2) {
                            stack = expected[reg + 8];
                            WRITEUL(stack, offsets[sample]);
                            expected[15] = stack;
                            expected[reg + 8] = offsets[sample];
                            expected[15] += 4;
                        }
                        else {
                            expected[15] -= 4;
                            stack = expected[15];
                            WRITEUL(stack, 0xDEADBEEF);
                        }
                        uint32 saved = expected[reg + 8];
                        if (operation != 2) {
                            expected[reg + 8] = stack;
                            expected[15] += operation == 0 ? (uint32)(int32)(int16)offsets[sample] : offsets[sample];
                        }
                        unsigned calls = decoder_calls;
                        uint32 next = syn68k_x64_execute(code);
                        uint32 expected_pc = operation == 0 ? 0x1004 : operation == 1 ? 0x1006 : 0x1002;
                        if (next != expected_pc)
                            fprintf(stderr,
                                    "operation %u reg %u sample %u align %u flags %u: next %x expected %x calls %u\n",
                                    operation, reg, sample, alignment, flags, next, expected_pc, decoder_calls - calls);
                        assert(next == expected_pc);
                        assert(decoder_calls == calls);
                        for (unsigned r = 0; r < 16; ++r)
                            assert(EM_DREG(r) == expected[r]);
                        for (unsigned bit = 0; bit < 5; ++bit)
                            assert(*cc[bit] == before_cc[bit]);
                        if (operation != 2)
                            assert(READUL(stack) == saved);
                        ++cases;
                    }
            /* Force stack/frame guards before any native guest side effects. */
            EM_A7 = 0x5000;
            EM_DREG(reg + 8) = 0x5000;
            uint32 before = EM_A7;
            syn68k_set_memory_window(0x2000, 0, 0);
            unsigned calls = decoder_calls;
            assert(syn68k_x64_execute(code) == 0x2000);
            assert(decoder_calls == calls + 1 && EM_A7 == before);
            syn68k_set_memory_window(0x10000, 0, 0);
            syn68k_x64_release(code);
        }
    printf("%u direct native LINK/UNLK cases and frame guards passed.\n", cases);
    free(memory);
    return 0;
}
