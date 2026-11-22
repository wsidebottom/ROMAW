/*+===================================================================
  File:      jit-x64-memory-ops-test.c

  Summary:   Implements jit x64 memory ops test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: __wrap_trap_direct, main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Compare direct emitted memory operations with the checked CPU semantics. */
#include "jit-x64.h"
#include "trap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned decoder_calls;
syn68k_addr_t __real_trap_direct(uint32 selector, uint32 pc, uint32 address);

syn68k_addr_t __wrap_trap_direct(uint32 selector, uint32 pc, uint32 address)
{
    ++decoder_calls;
    return __real_trap_direct(selector, pc, address);
}

static unsigned ccr(void)
{
    return !!cpu_state.ccc | !!cpu_state.ccv << 1 | !cpu_state.ccnz << 2 | !!cpu_state.ccn << 3 | !!cpu_state.ccx << 4;
}

int main(void)
{
    unsigned char* memory = calloc(1, 0x10000);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(0x10000, 0, 0);
    uint32 vectors[64] = {0};
    initialize_68k_emulator(NULL, 1, vectors, 0);
    const unsigned arithmetic[] = {0, 0x0200, 0x0400, 0x0600, 0x0A00, 0x0C00};
    const unsigned modes[] = {2, 3, 4, 6, 7, 7};
    const uint32 values[] = {0, 1, 0x7F, 0x80, 0xFF, 0x7FFF, 0x8000, 0xFFFF, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF};
    unsigned cases = 0;
    for (unsigned family = 0; family < 33; ++family)
        for (unsigned reg = 0; reg <= 7; reg += 7)
            for (unsigned mode_index = 0; mode_index < 6; ++mode_index) {
                if (mode_index >= 4 && (family < 15 || family > 18))
                    continue;
                unsigned mode = modes[mode_index];
                unsigned code = mode_index == 4 ? 58 : mode_index == 5 ? 59 : mode * 8 + reg;
                unsigned width = family < 18 ? family % 3 : family < 22 ? 0 : family < 25 ? family - 22 : 1;
                unsigned op = family < 18   ? arithmetic[family / 3] | width << 6
                              : family < 22 ? 0x0800 | (family - 18) << 6
                              : family < 25 ? 0x4000 | width << 6
                                            : 0xE0C0 | ((family - 25) >> 1) << 9 | ((family - 25) & 1) << 8;
                WRITEUW(0x1000, op | code);
                int8 length = 1;
                unsigned translated;
                int terminal;
                void* native = syn68k_x64_compile(SYN68K_TO_US(0x1000), &length, 1, &translated, 0x1000, &terminal);
                assert(native && translated == 1 && terminal);
                for (unsigned a = 0; a < 11; ++a)
                    for (unsigned b = 0; b < 11; ++b)
                        for (unsigned flags = 0; flags < 32; ++flags) {
                            for (unsigned r = 0; r < 16; ++r)
                                EM_DREG(r) = 0x12340000 + r;
                            EM_AREG(reg) = 0x4080 + (flags & 1);
                            EM_D6 = flags & 3;
                            uint32 cursor = 0x1002;
                            if (family < 22) {
                                if (width == 2)
                                    WRITEUL(cursor, values[b]), cursor += 4;
                                else
                                    WRITEUW(cursor, values[b]), cursor += 2;
                            }
                            uint32 location = EM_AREG(reg);
                            unsigned size = 1U << width;
                            if (mode == 4)
                                location -= size == 1 && reg == 7 ? 2 : size;
                            if (mode == 6) {
                                if (flags & 4) {
                                    WRITEUW(cursor, 0x6920);
                                    WRITEUW(cursor + 2, -64);
                                    cursor += 4;
                                }
                                else {
                                    WRITEUW(cursor, 0x68C0);
                                    cursor += 2;
                                }
                                location += EM_D6 - 64;
                            }
                            else if (code == 58) {
                                WRITEUW(cursor, 0x4040 - cursor);
                                cursor += 2;
                                location = 0x4040;
                            }
                            else if (code == 59) {
                                WRITEUW(cursor, 0x01F0);
                                WRITEUL(cursor + 2, 0x4041);
                                cursor += 6;
                                location = 0x4041;
                            }
                            WRITEUW(cursor, 0x4E71); /* Stop the checked memory batch. */
                            for (unsigned i = 0; i < 256; ++i)
                                memory[0x4000 + i] = i * 17 + flags;
                            for (unsigned i = 0; i < size; ++i)
                                WRITEUB(location + i, values[a] >> (8 * (size - i - 1)));
                            cpu_state.ccc = flags & 1 ? 0x80000000 : 0;
                            cpu_state.ccv = flags & 2 ? 0x12345678 : 0;
                            cpu_state.ccnz = flags & 4 ? 0 : 0x80008000;
                            cpu_state.ccn = flags & 8 ? 0x87654321 : 0;
                            cpu_state.ccx = flags & 16 ? 0x11111111 : 0;
                            CPUState before = cpu_state;
                            unsigned char initial[256], expected_bytes[256];
                            memcpy(initial, memory + 0x4000, sizeof initial);
                            assert(__real_trap_direct(4, 0x1000, 0) == cursor);
                            uint32 expected[16];
                            for (unsigned r = 0; r < 16; ++r)
                                expected[r] = EM_DREG(r);
                            unsigned expected_ccr = ccr();
                            memcpy(expected_bytes, memory + 0x4000, sizeof expected_bytes);
                            cpu_state = before;
                            memcpy(memory + 0x4000, initial, sizeof initial);
                            unsigned calls = decoder_calls;
                            uint32 actual_pc = syn68k_x64_execute(native);
                            if (actual_pc != cursor || decoder_calls != calls || ccr() != expected_ccr)
                                fprintf(stderr, "op %04x data %x imm %x flags %x: PC %x/%x CCR %x/%x calls %u\n",
                                        op | code, values[a], values[b], flags, actual_pc, cursor, ccr(), expected_ccr,
                                        decoder_calls - calls);
                            assert(actual_pc == cursor && decoder_calls == calls);
                            assert(ccr() == expected_ccr);
                            for (unsigned r = 0; r < 16; ++r)
                                assert(EM_DREG(r) == expected[r]);
                            assert(!memcmp(expected_bytes, memory + 0x4000, sizeof initial));
                            ++cases;
                        }
                syn68k_x64_release(native);
            }
    printf("%u direct memory immediate/bit/NEGX/shift cases passed.\n", cases);
    free(memory);
    return 0;
}
