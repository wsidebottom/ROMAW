/*+===================================================================
  File:      jit-x64-indexed-test.c

  Summary:   Implements jit x64 indexed test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: __wrap_trap_direct, main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Full-format indexed native bodies, dynamic continuations and helper unwind. */
#include "jit-x64.h"
#include "trap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned decoder_calls;
static void* retire_code;

syn68k_addr_t __wrap_trap_direct(uint32 selector, uint32 pc, uint32 address)
{
    assert(selector == 4 && pc == 0x1000 && address == 0);
    ++decoder_calls;
    if (retire_code) {
        syn68k_x64_release(retire_code);
        retire_code = NULL;
    }
    return 0x2000;
}

static void* compile(unsigned op)
{
    WRITEUW(0x1000, op);
    int8 length = 1;
    unsigned translated;
    int terminal;
    void* code = syn68k_x64_compile(SYN68K_TO_US(0x1000), &length, 1, &translated, 0x1000, &terminal);
    assert(code && translated == 1 && terminal);
    return code;
}

static int overlaps_code(uint32 address, unsigned span)
{
    return address < 0x1020 && (uint64)address + span > 0x1000;
}

int main(void)
{
    unsigned char* memory = calloc(1, 0x10000);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(0x10000, 0, 0);
    uint32 vectors[64] = {0};
    initialize_68k_emulator(NULL, 1, vectors, 0);
    const unsigned opcodes[] = {0x43C0, 0x4EC0, 0x4E80, 0x4840, 0x2000, 0x2180, 0x4C40, 0x4C00,
                                0x80C0, 0xC0C0, 0x4180, 0x04C0, 0x04C0, 0x4CC0, 0x48C0};
    unsigned cases = 0;
    for (unsigned family = 0; family < 15; ++family)
        for (unsigned relative = 0; relative < 2; ++relative) {
            if (relative && (family == 5 || family == 14))
                continue;
            unsigned code = relative ? 59 : 48;
            unsigned op = family == 5 ? opcodes[family] : opcodes[family] | code;
            void* native = compile(op);
            for (unsigned index_reg = 2; index_reg <= 10; index_reg += 8)
                for (unsigned wide = 0; wide < 2; ++wide)
                    for (unsigned scale = 0; scale < 4; ++scale)
                        for (unsigned suppression = 0; suppression < 4; ++suppression)
                            for (unsigned bd = 1; bd <= 3; ++bd)
                                for (unsigned indirect = 0; indirect < 8; ++indirect) {
                                    /* IS=1 reserves postindexed I/IS forms (PRM Table 2-2). */
                                    if (indirect == 4 || ((suppression & 2) && indirect >= 4))
                                        continue;
                                    for (unsigned r = 0; r < 16; ++r)
                                        EM_DREG(r) = 0x12340000 + r;
                                    EM_D0 = family >= 10 && family <= 12 ? 5 : 1000;
                                    EM_D1 = 0;
                                    EM_A0 = 0x4000;
                                    EM_A7 = 0xD000;
                                    EM_DREG(index_reg) = wide ? 0x20 : 0xFACE0020;
                                    unsigned offset = family == 6 || family == 7 || family >= 11 ? 4 : 2;
                                    if (offset == 4)
                                        WRITEUW(0x1002, family == 12 ? 0x800 : family == 11 ? 0 : 1);
                                    uint32 cursor = 0x1000 + offset;
                                    unsigned extension = index_reg << 12 | wide << 11 | scale << 9 | 0x100 |
                                                         (suppression & 1 ? 0x80 : 0) | (suppression & 2 ? 0x40 : 0) |
                                                         bd << 4 | indirect;
                                    WRITEUW(cursor, extension);
                                    uint32 base = suppression & 1 ? 0 : relative ? cursor : EM_A0;
                                    uint32 index = suppression & 2 ? 0 : 0x20U << scale;
                                    cursor += 2;
                                    if (bd == 2) {
                                        WRITEUW(cursor, 0x3000);
                                        base += 0x3000;
                                        cursor += 2;
                                    }
                                    else if (bd == 3) {
                                        WRITEUL(cursor, 0x3100);
                                        base += 0x3100;
                                        cursor += 4;
                                    }
                                    uint32 outer = 0;
                                    if ((indirect & 3) == 2) {
                                        WRITEUW(cursor, -16);
                                        outer = (uint32)-16;
                                        cursor += 2;
                                    }
                                    else if ((indirect & 3) == 3) {
                                        WRITEUL(cursor, 32);
                                        outer = 32;
                                        cursor += 4;
                                    }
                                    uint32 target = base + index;
                                    if (indirect) {
                                        uint32 pointer = base + (indirect < 4 ? index : 0);
                                        if (overlaps_code(pointer, 4))
                                            continue;
                                        WRITEUL(pointer, 0x5100);
                                        target = 0x5100 + outer + (indirect >= 5 ? index : 0);
                                    }
                                    if (overlaps_code(target, family == 13 || family == 14 ? 64 : 8))
                                        continue;
                                    if (family >= 4) {
                                        if (family >= 8 && family <= 10)
                                            WRITEUW(target, 9);
                                        else
                                            WRITEUL(target, family == 11 || family == 12 ? 2 : 9);
                                        if (family == 11 || family == 12)
                                            WRITEUL(target + 4, 9);
                                    }
                                    uint32 expected[16];
                                    for (unsigned r = 0; r < 16; ++r)
                                        expected[r] = EM_DREG(r);
                                    uint32* cc[] = {&cpu_state.ccc, &cpu_state.ccv, &cpu_state.ccnz, &cpu_state.ccn,
                                                    &cpu_state.ccx};
                                    uint32 expected_cc[5];
                                    for (unsigned r = 0; r < 5; ++r)
                                        expected_cc[r] = *cc[r] = cases & (1U << r) ? 0x87654321 + r : 0;
                                    uint32 next = cursor;
                                    if (family == 0)
                                        expected[9] = target;
                                    else if (family == 1 || family == 2)
                                        next = target;
                                    if (family == 2 || family == 3)
                                        expected[15] -= 4;
                                    if (family >= 4 && family <= 9) {
                                        if (family == 4)
                                            expected[0] = 9;
                                        else if (family == 6)
                                            expected[0] = 111, expected[1] = 1;
                                        else if (family == 7 || family == 9)
                                            expected[0] = 9000;
                                        else if (family == 8)
                                            expected[0] = 0x1006F;
                                        expected_cc[0] = expected_cc[1] = expected_cc[3] = 0;
                                        expected_cc[2] = 1;
                                    }
                                    else if (family == 10)
                                        expected_cc[3] = 0;
                                    else if (family == 11 || family == 12)
                                        expected_cc[0] = 0, expected_cc[2] = 1;
                                    else if (family == 13)
                                        expected[0] = 9;
                                    unsigned calls = decoder_calls;
                                    uint32 actual = syn68k_x64_execute(native);
                                    if (actual != next || decoder_calls != calls)
                                        fprintf(stderr, "family %u PC %u ext %04x next %x/%x decoder %u\n", family,
                                                relative, extension, actual, next, decoder_calls - calls);
                                    assert(actual == next && decoder_calls == calls);
                                    for (unsigned r = 0; r < 16; ++r)
                                        assert(EM_DREG(r) == expected[r]);
                                    for (unsigned r = 0; r < 5; ++r) {
                                        int changed = (family >= 4 && family <= 9 && r < 4) ||
                                                      (family == 10 && r == 3) ||
                                                      ((family == 11 || family == 12) && (r == 0 || r == 2));
                                        if (changed)
                                            assert(!!*cc[r] == !!expected_cc[r]);
                                        else
                                            assert(*cc[r] == expected_cc[r]);
                                    }
                                    if (family == 2 || family == 3)
                                        assert(READUL(0xCFFC) == (family == 2 ? cursor : target));
                                    if (family == 5 || family == 14)
                                        assert(READUL(target) == 1000);
                                    ++cases;
                                }
            syn68k_x64_release(native);
        }
    /* Both helper failure and a later data guard must drop their native call
     frame before a callback frees the currently executing generated block. */
    for (unsigned failure = 0; failure < 4; ++failure) {
        retire_code = compile(0x2030);
        WRITEUW(0x1002, failure == 0 ? 0x0100 : failure == 3 ? 0x0155 : 0x01F0);
        WRITEUL(0x1004, 0x5000);
        if (failure == 1)
            syn68k_set_memory_window(0x1004, 0, 0);
        if (failure == 2)
            syn68k_x64_code_pages[5] = 1;
        CPUState before = cpu_state;
        unsigned calls = decoder_calls;
        assert(syn68k_x64_execute(retire_code) == 0x2000);
        assert(!retire_code && decoder_calls == calls + 1);
        assert(memcmp(&before, &cpu_state, sizeof before) == 0);
        syn68k_set_memory_window(0x10000, 0, 0);
        syn68k_x64_code_pages[5] = 0;
    }
    assert(!syn68k_x64_indexed_address(16, 0x1002, NULL));
    printf("%u full-indexed native bodies and callback-unwind cases passed.\n", cases);
    free(memory);
    return 0;
}
