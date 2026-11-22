/*+===================================================================
  File:      jit-x64-pair-test.c

  Summary:   Implements jit x64 pair test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: __wrap_trap_direct, main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Direct complex MOVE and carry-chain operands, aliases and rollback tails. */
#include "jit-x64.h"
#include "trap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned decoder_calls, cases;
static void* retire_code;
syn68k_addr_t __real_trap_direct(uint32 selector, uint32 pc, uint32 address);

syn68k_addr_t __wrap_trap_direct(uint32 selector, uint32 pc, uint32 address)
{
    assert(selector == 4 && pc == 0x1000 && address == 0);
    ++decoder_calls;
    if (retire_code) {
        syn68k_x64_release(retire_code);
        retire_code = NULL;
        return 0x2000;
    }
    return __real_trap_direct(selector, pc, address);
}

static unsigned status(void)
{
    return !!cpu_state.ccc | !!cpu_state.ccv << 1 | !cpu_state.ccnz << 2 | !!cpu_state.ccn << 3 | !!cpu_state.ccx << 4;
}

static unsigned extension(unsigned cursor, unsigned ea, unsigned size, unsigned sample, int destination)
{
    int displacement = destination ? 64 : -64;
    if ((ea >> 3) == 5)
        WRITEUW(cursor, displacement);
    else if ((ea >> 3) == 6) {
        if (sample & 1) {
            WRITEUW(cursor, 0x6920);
            WRITEUW(cursor + 2, displacement);
            return cursor + 4;
        }
        WRITEUW(cursor, 0x6800 | (uint8)displacement);
    }
    else if (ea == 56 || ea == 58)
        WRITEUW(cursor, ea == 56 ? (destination ? 0x40C0 : 0x4040) : 0x4040 - cursor);
    else if (ea == 57) {
        WRITEUL(cursor, destination ? 0x40C1 : 0x4041);
        return cursor + 4;
    }
    else if (ea == 59) {
        WRITEUW(cursor, 0x01F0);
        WRITEUL(cursor + 2, 0x4040);
        return cursor + 6;
    }
    else if (ea == 60) {
        if (size == 4)
            WRITEUL(cursor, sample & 1 ? 0x80000000 : UINT32_MAX);
        else
            WRITEUW(cursor, sample & 1 ? 0x8000 : 0xFFFF);
        return cursor + (size == 4 ? 4 : 2);
    }
    else
        return cursor;
    return cursor + 2;
}

static void* compile(unsigned op)
{
    WRITEUW(0x1000, op);
    int8 length = 1;
    unsigned translated;
    int terminal;
    void* native = syn68k_x64_compile(SYN68K_TO_US(0x1000), &length, 1, &translated, 0x1000, &terminal);
    assert(native && translated == 1 && terminal);
    return native;
}

static void compare(unsigned op, void* native, unsigned size, unsigned sample, unsigned char* memory)
{
    for (unsigned r = 0; r < 16; ++r)
        EM_DREG(r) = r < 8 ? (0xFEDCBA98 ^ (r * 0x1020304U) ^ sample) : 0x4080 + (sample & 1);
    EM_D6 = 16;
    unsigned cursor = 0x1002;
    if ((op >> 12) >= 1 && (op >> 12) <= 3) {
        cursor = extension(cursor, op & 63, size, sample, 0);
        unsigned dest = ((op >> 3) & 56) | ((op >> 9) & 7);
        cursor = extension(cursor, dest, size, sample, 1);
    }
    WRITEUW(cursor, 0x4E71);
    for (unsigned i = 0; i < 512; ++i)
        memory[0x4000 + i] = sample ^ (i * 37);
    cpu_state.ccc = sample & 1 ? 0x80000000 : 0;
    cpu_state.ccv = sample & 2 ? 0x12345678 : 0;
    cpu_state.ccnz = sample & 4 ? 0 : 0x80008000;
    cpu_state.ccn = sample & 8 ? 0x87654321 : 0;
    cpu_state.ccx = sample & 16 ? 0x11111111 : 0;
    CPUState before = cpu_state;
    unsigned char initial[512], expected_bytes[512];
    memcpy(initial, memory + 0x4000, sizeof initial);
    assert(syn68k_x64_memory_run(0x1000, op) == cursor);
    uint32 expected[16];
    for (unsigned r = 0; r < 16; ++r)
        expected[r] = EM_DREG(r);
    unsigned expected_status = status();
    memcpy(expected_bytes, memory + 0x4000, sizeof expected_bytes);
    cpu_state = before;
    memcpy(memory + 0x4000, initial, sizeof initial);
    unsigned calls = decoder_calls;
    uint32 actual_pc = syn68k_x64_execute(native);
    if (actual_pc != cursor || decoder_calls != calls || status() != expected_status)
        fprintf(stderr, "op %04x sample %u: PC %x/%x CCR %x/%x calls %u\n", op, sample, actual_pc, cursor, status(),
                expected_status, decoder_calls - calls);
    assert(actual_pc == cursor && decoder_calls == calls);
    assert(status() == expected_status);
    for (unsigned r = 0; r < 16; ++r)
        assert(EM_DREG(r) == expected[r]);
    assert(!memcmp(expected_bytes, memory + 0x4000, sizeof initial));
    ++cases;
}

int main(void)
{
    unsigned char* memory = calloc(1, 0x10000);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(0x10000, 0, 0);
    uint32 vectors[64] = {0};
    initialize_68k_emulator(NULL, 1, vectors, 0);
    for (unsigned size = 1; size <= 4; size *= 2)
        for (unsigned src = 0; src <= 60; ++src)
            for (unsigned dest = 0; dest <= 57; ++dest) {
                if ((size == 1 && ((src >= 8 && src < 16) || (dest >= 8 && dest < 16))))
                    continue;
                unsigned family = size == 1 ? 1 : size == 2 ? 3 : 2;
                unsigned op = family << 12 | (dest & 7) << 9 | (dest >> 3) << 6 | src;
                void* native = compile(op);
                for (unsigned sample = 0; sample < 8; ++sample)
                    compare(op, native, size, sample * 7, memory);
                syn68k_x64_release(native);
            }
    const unsigned bases[] = {0x9108, 0xD108, 0xB108};
    for (unsigned kind = 0; kind < 3; ++kind)
        for (unsigned width = 0; width < 3; ++width)
            for (unsigned src = 0; src < 8; ++src)
                for (unsigned dest = 0; dest < 8; ++dest) {
                    unsigned op = bases[kind] | width << 6 | dest << 9 | src;
                    void* native = compile(op);
                    for (unsigned sample = 0; sample < 32; ++sample)
                        compare(op, native, 1U << width, sample, memory);
                    syn68k_x64_release(native);
                }
    const unsigned faults[] = {0x22D8, 0xD188, 0xB188};
    for (unsigned i = 0; i < 3; ++i) {
        retire_code = compile(faults[i]);
        EM_A0 = i == 1 ? 0x5008 : 0x4080;
        EM_A1 = 0x5000;
        if (i < 2)
            syn68k_x64_code_pages[5] = 1;
        else {
            syn68k_host_alias_count = 1;
            syn68k_host_alias_base = 0x4080;
            syn68k_host_alias_size = 4;
        }
        CPUState before = cpu_state;
        unsigned calls = decoder_calls;
        assert(syn68k_x64_execute(retire_code) == 0x2000);
        assert(!retire_code && decoder_calls == calls + 1);
        assert(!memcmp(&before, &cpu_state, sizeof before));
        syn68k_x64_code_pages[5] = 0;
        syn68k_host_alias_count = syn68k_host_alias_size = 0;
    }
    printf("%u direct MOVE/ADDX/SUBX/CMPM pair cases and rollback tails passed.\n", cases);
    free(memory);
    return 0;
}
