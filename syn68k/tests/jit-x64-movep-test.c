/*+===================================================================
  File:      jit-x64-movep-test.c

  Summary:   Implements jit x64 movep test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: __wrap_trap_direct, main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Direct sparse transfers must preserve intervening bytes and raw CCR. */
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
    const int offsets[] = {-127, -3, -1, 0, 1, 2, 17, 127};
    unsigned cases = 0;
    for (unsigned data = 0; data < 8; ++data)
        for (unsigned address = 0; address < 8; ++address)
            for (unsigned wide = 0; wide < 2; ++wide)
                for (unsigned writing = 0; writing < 2; ++writing) {
                    WRITEUW(0x1000, 0x0108 | data << 9 | address | wide << 6 | writing << 7);
                    int8 length = 1;
                    unsigned translated;
                    int terminal;
                    void* code = syn68k_x64_compile(SYN68K_TO_US(0x1000), &length, 1, &translated, 0x1000, &terminal);
                    assert(code && translated == 1 && terminal);
                    for (unsigned sample = 0; sample < 8; ++sample)
                        for (unsigned flags = 0; flags < 32; ++flags) {
                            uint32 before[16];
                            for (unsigned r = 0; r < 16; ++r)
                                before[r] = EM_DREG(r) = 0xFEDCBA00 + r;
                            before[8 + address] = EM_AREG(address) = 0x4100;
                            WRITEUW(0x1002, offsets[sample]);
                            uint32 location = 0x4100 + offsets[sample];
                            unsigned bytes = wide ? 4 : 2;
                            uint8 expected[9];
                            for (unsigned i = 0; i < 9; ++i)
                                expected[i] = WRITEUB(location - 1 + i, 0x10 * i + sample);
                            uint32 result = 0;
                            for (unsigned i = 0; i < bytes; ++i)
                                if (writing)
                                    expected[1 + i * 2] = before[data] >> (8 * (bytes - i - 1));
                                else
                                    result = result << 8 | expected[1 + i * 2];
                            if (!writing)
                                before[data] = wide ? result : (before[data] & 0xFFFF0000) | result;
                            uint32* cc[] = {&cpu_state.ccc, &cpu_state.ccv, &cpu_state.ccnz, &cpu_state.ccn,
                                            &cpu_state.ccx};
                            uint32 before_cc[5];
                            for (unsigned bit = 0; bit < 5; ++bit)
                                before_cc[bit] = *cc[bit] = flags & (1U << bit) ? 0x87654321 + bit : 0;
                            unsigned calls = decoder_calls;
                            assert(syn68k_x64_execute(code) == 0x1004);
                            assert(decoder_calls == calls);
                            for (unsigned r = 0; r < 16; ++r)
                                assert(EM_DREG(r) == before[r]);
                            for (unsigned bit = 0; bit < 5; ++bit)
                                assert(*cc[bit] == before_cc[bit]);
                            for (unsigned i = 0; i < 9; ++i)
                                assert(READUB(location - 1 + i) == expected[i]);
                            ++cases;
                        }
                    /* Even a partial strided write must not precede a failed guard. */
                    WRITEUW(0x1002, 0);
                    EM_AREG(address) = 0x4FFE;
                    WRITEUB(0x4FFE, 0x5A);
                    unsigned calls = decoder_calls;
                    assert(syn68k_x64_execute(code) == 0x2000);
                    assert(decoder_calls == calls + 1 && READUB(0x4FFE) == 0x5A);
                    syn68k_x64_release(code);
                }
    printf("%u direct native MOVEP and guarded sparse transfers passed.\n", cases);
    free(memory);
    return 0;
}
