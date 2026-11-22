/*+===================================================================
  File:      jit-x64-divide-test.c

  Summary:   Implements jit x64 divide test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: __wrap_trap_direct, main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Check native division independently of the checked decoder. */
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
    const uint32 values[] = {0, 1, 0xFFFF, 0x8000, 0x7FFF, 0x80000000, 0x7FFFFFFF, 0xFFFFFFFF, 0xFFFF8000, 0x12345678};
    unsigned cases = 0;
    for (unsigned sign = 0; sign < 2; ++sign)
        for (unsigned src = 0; src < 8; ++src)
            for (unsigned dst = 0; dst < 8; ++dst) {
                WRITEUW(0x1000, 0x80C0 | sign << 8 | dst << 9 | src);
                int8 length = 1;
                unsigned translated;
                int terminal;
                void* code = syn68k_x64_compile(SYN68K_TO_US(0x1000), &length, 1, &translated, 0x1000, &terminal);
                assert(code && translated == 1 && terminal);
                for (unsigned a = 0; a < 10; ++a)
                    for (unsigned b = 0; b < 10; ++b)
                        for (unsigned flags = 0; flags < 32; ++flags) {
                            for (unsigned r = 0; r < 16; ++r)
                                EM_DREG(r) = 0xABCDEF00 + r;
                            EM_DREG(dst) = values[a];
                            EM_DREG(src) = values[b];
                            uint32 before[16];
                            for (unsigned r = 0; r < 16; ++r)
                                before[r] = EM_DREG(r);
                            cpu_state.ccx = flags & 16 ? 0x12345678 : 0;
                            cpu_state.ccc = flags & 1;
                            cpu_state.ccv = flags & 2;
                            cpu_state.ccn = flags & 8;
                            cpu_state.ccnz = !(flags & 4);
                            int64 divisor = sign ? (int16)before[src] : (uint16)before[src];
                            unsigned calls = decoder_calls;
                            uint32 next = syn68k_x64_execute(code);
                            if (!divisor) {
                                assert(next == 0x2000 && decoder_calls == calls + 1);
                                assert(cpu_state.ccc == (flags & 1));
                                assert(cpu_state.ccv == (flags & 2));
                                assert(cpu_state.ccn == (flags & 8));
                                assert(cpu_state.ccnz == !(flags & 4));
                            }
                            else {
                                int64 dividend = sign ? (int64)(int32)before[dst] : (int64)before[dst];
                                int64 q = dividend / divisor, rem = dividend % divisor;
                                int overflow = sign ? q < -32768 || q > 32767 : q > 65535;
                                assert(next == 0x1002 && decoder_calls == calls);
                                assert(!cpu_state.ccc && !!cpu_state.ccv == overflow);
                                assert(!!cpu_state.ccnz == ((uint32)q != 0));
                                assert(!!cpu_state.ccn == (sign ? !!((uint32)q >> 31) : !!((uint32)q & 0x8000)));
                                if (!overflow)
                                    before[dst] = (uint32)(uint16)rem << 16 | (uint16)q;
                            }
                            assert(cpu_state.ccx == (flags & 16 ? 0x12345678 : 0));
                            for (unsigned r = 0; r < 16; ++r)
                                assert(before[r] == EM_DREG(r));
                            ++cases;
                        }
                syn68k_x64_release(code);
            }
    printf("%u native division and guarded zero-divisor cases passed.\n", cases);
    free(memory);
    return 0;
}
