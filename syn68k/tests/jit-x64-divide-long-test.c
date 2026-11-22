/*+===================================================================
  File:      jit-x64-divide-long-test.c

  Summary:   Implements jit x64 divide long test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: __wrap_trap_direct, main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Independent widened model, including INT64_MIN/-1 and live extensions. */
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
    const uint32 values[] = {0, 1, 2, 0xFFFF, 0x80000000, 0x7FFFFFFF, 0xFFFFFFFF, 0xFFFF8000, 0x12345678};
    unsigned cases = 0;
    for (unsigned src = 0; src < 8; ++src) {
        WRITEUW(0x1000, 0x4C40 | src);
        int8 length = 1;
        unsigned translated;
        int terminal;
        void* code = syn68k_x64_compile(SYN68K_TO_US(0x1000), &length, 1, &translated, 0x1000, &terminal);
        assert(code && translated == 1 && terminal);
        for (unsigned low = 0; low < 8; ++low)
            for (unsigned high = 0; high < 8; ++high)
                for (unsigned sign = 0; sign < 2; ++sign)
                    for (unsigned wide = 0; wide < 2; ++wide) {
                        WRITEUW(0x1002, low << 12 | sign << 11 | wide << 10 | high);
                        for (unsigned a = 0; a < 9; ++a)
                            for (unsigned b = 0; b < 9; ++b)
                                for (unsigned c = 0; c < 9; ++c)
                                    for (unsigned flags = 0; flags < 2; ++flags) {
                                        uint32 before[16];
                                        for (unsigned r = 0; r < 16; ++r)
                                            EM_DREG(r) = 0xABCDEF00 + r;
                                        EM_DREG(low) = values[a];
                                        EM_DREG(high) = values[c];
                                        EM_DREG(src) = values[b];
                                        for (unsigned r = 0; r < 16; ++r)
                                            before[r] = EM_DREG(r);
                                        uint64 raw = before[low];
                                        if (wide)
                                            raw |= (uint64)before[high] << 32;
                                        __int128 dividend =
                                            sign ? (wide ? (__int128)(int64)raw : (__int128)(int32)raw) : (__int128)raw;
                                        __int128 divisor = sign ? (int64)(int32)before[src] : (int64)before[src];
                                        uint32 old = flags ? 0x87654321 : 0;
                                        cpu_state.ccx = cpu_state.ccc = cpu_state.ccv = cpu_state.ccn = cpu_state.ccnz =
                                            old;
                                        unsigned calls = decoder_calls;
                                        uint32 next = syn68k_x64_execute(code);
                                        if (!divisor) {
                                            assert(next == 0x2000 && decoder_calls == calls + 1);
                                            assert(cpu_state.ccc == old && cpu_state.ccv == old);
                                            assert(cpu_state.ccn == old && cpu_state.ccnz == old);
                                        }
                                        else {
                                            __int128 q = dividend / divisor;
                                            int overflow = sign ? q < INT32_MIN || q > INT32_MAX : q > UINT32_MAX;
                                            assert(next == 0x1004 && decoder_calls == calls);
                                            assert(!cpu_state.ccc && !!cpu_state.ccv == overflow);
                                            if (overflow)
                                                assert(cpu_state.ccn == old && cpu_state.ccnz == old);
                                            else {
                                                before[high] = dividend % divisor;
                                                before[low] = q;
                                                assert(!!cpu_state.ccn == !!((uint32)q >> 31));
                                                assert(!!cpu_state.ccnz == ((uint32)q != 0));
                                            }
                                        }
                                        assert(cpu_state.ccx == old);
                                        for (unsigned r = 0; r < 16; ++r)
                                            assert(EM_DREG(r) == before[r]);
                                        ++cases;
                                    }
                    }
        for (unsigned bit = 0; bit < 16; ++bit)
            if ((1U << bit) & 0x83F8) {
                WRITEUW(0x1002, 1U << bit);
                unsigned calls = decoder_calls;
                assert(syn68k_x64_execute(code) == 0x2000);
                assert(decoder_calls == calls + 1);
            }
        syn68k_set_memory_window(0x1002, 0, 0);
        unsigned calls = decoder_calls;
        assert(syn68k_x64_execute(code) == 0x2000);
        assert(decoder_calls == calls + 1);
        syn68k_set_memory_window(0x10000, 0, 0);
        syn68k_x64_release(code);
    }
    printf("%u native long division and guarded zero-divisor cases passed.\n", cases);
    free(memory);
    return 0;
}
