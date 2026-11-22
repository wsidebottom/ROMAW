/*+===================================================================
  File:      jit-x64-integer-test.c

  Summary:   Implements jit x64 integer test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Carry chains, all shift counts, bit operations and Scc against a 68K model. */
#include "syn68k_public.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned cases;
static int native;
static uint32 seed = 0x93841725;

static uint32 random_word(void)
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

static void registers(uint32* regs)
{
    for (unsigned i = 0; i < 16; ++i)
        regs[i] = random_word();
}

static uint32 edge(unsigned sample)
{
    static const uint32 values[] = {0, 1, 0xFFFFFFFF, 0x80000000, 0x7FFFFFFF, 0xFFFF, 0x8000, 0x7FFF, 0xFF, 0x80, 0x7F};
    return sample < 11 ? values[sample] : random_word();
}

static unsigned status(void)
{
    return !!cpu_state.ccc | (!!cpu_state.ccv << 1) | (!cpu_state.ccnz << 2) | (!!cpu_state.ccn << 3) |
           (!!cpu_state.ccx << 4);
}

static void execute(unsigned op, unsigned words, unsigned extension, const uint32* input, const uint32* expected,
                    unsigned flags, unsigned expected_flags, unsigned preserved)
{
    for (unsigned i = 0; i < 16; ++i)
        EM_DREG(i) = input[i];
    cpu_state.sr = 0;
    cpu_state.ccc = flags & 1 ? 0x80000000 : 0;
    cpu_state.ccv = flags & 2 ? 0x12345678 : 0;
    cpu_state.ccnz = flags & 4 ? 0 : 0x80008000;
    cpu_state.ccn = flags & 8 ? 0x87654321 : 0;
    cpu_state.ccx = flags & 16 ? 0x11111110 : 0;
    CCRElement original[] = {cpu_state.ccc, cpu_state.ccv, cpu_state.ccnz, cpu_state.ccn, cpu_state.ccx};
    destroy_blocks(0x1000, 32);
    WRITEUW(0x1000, op);
    if (words == 2)
        WRITEUW(0x1002, extension);
    unsigned end = 0x1000 + words * 2;
    WRITEUW(end, 0x4E71);
    WRITEUW(end + 2, 0x4EF9);
    WRITEUL(end + 4, MAGIC_EXIT_EMULATOR_ADDRESS);
    uint64 before = syn68k_native_execution_count();
    interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
    assert(syn68k_native_execution_count() == before + native);
    if (status() != expected_flags) {
        fprintf(stderr, "opcode %04x case %u flags %02x: %02x != %02x\n", op, cases, flags, status(), expected_flags);
        abort();
    }
    for (unsigned i = 0; i < 16; ++i)
        if (EM_DREG(i) != expected[i]) {
            fprintf(stderr, "opcode %04x case %u register %u: %08x != %08x\n", op, cases, i, EM_DREG(i), expected[i]);
            abort();
        }
    CCRElement result[] = {cpu_state.ccc, cpu_state.ccv, cpu_state.ccnz, cpu_state.ccn, cpu_state.ccx};
    for (unsigned i = 0; i < 5; ++i)
        if (preserved & (1U << i))
            assert(result[i] == original[i]);
    ++cases;
}

static void shift_case(unsigned kind, unsigned left, unsigned size, unsigned dst, unsigned src, unsigned count,
                       uint32 value, unsigned flags)
{
    uint32 input[16], expected[16];
    registers(input);
    input[dst] = value;
    if (src < 8)
        input[src] = count | 0xFEDC0000;
    memcpy(expected, input, sizeof expected);
    unsigned bits = 8U << size;
    uint32 mask = UINT32_MAX >> (32 - bits), sign = 1U << (bits - 1);
    uint32 value_low = input[dst] & mask;
    unsigned actual_count = src < 8 ? input[src] & 63 : count;
    unsigned x = !!(flags & 16), c = kind == 2 ? x : 0, v = 0;
    for (unsigned step = 0; step < actual_count; ++step) {
        unsigned old_sign = !!(value_low & sign);
        c = left ? old_sign : value_low & 1;
        if (left)
            value_low = ((value_low << 1) & mask) | (kind == 2 ? x : kind == 3 ? c : 0);
        else
            value_low = (value_low >> 1) | ((kind == 0 ? old_sign : kind == 2 ? x : kind == 3 ? c : 0) ? sign : 0);
        if (kind == 0 && left)
            v |= old_sign != !!(value_low & sign);
        if (kind != 3)
            x = c;
    }
    expected[dst] = (input[dst] & ~mask) | value_low;
    unsigned answer = c | (v << 1) | ((value_low == 0) << 2) | (!!(value_low & sign) << 3) | (x << 4);
    unsigned op =
        0xE000 | (src < 8 ? src : count & 7) << 9 | left << 8 | size << 6 | (src < 8 ? 0x20 : 0) | kind << 3 | dst;
    execute(op, 1, 0, input, expected, flags, answer, !actual_count || kind == 3 ? 16 : 0);
}

static int truth(unsigned cc, unsigned flags)
{
    int c = !!(flags & 1), v = !!(flags & 2), z = !!(flags & 4), n = !!(flags & 8);
    switch (cc) {
    case 0:
        return 1;
    case 1:
        return 0;
    case 2:
        return !c && !z;
    case 3:
        return c || z;
    case 4:
        return !c;
    case 5:
        return c;
    case 6:
        return !z;
    case 7:
        return z;
    case 8:
        return !v;
    case 9:
        return v;
    case 10:
        return !n;
    case 11:
        return n;
    case 12:
        return n == v;
    case 13:
        return n != v;
    case 14:
        return !z && n == v;
    default:
        return z || n != v;
    }
}

int main(int argc, char** argv)
{
    native = argc > 1 && atoi(argv[1]);
    void* memory = calloc(1, 0x10000);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(0x10000, 0, 0);
    uint32 vectors[64] = {0};
    initialize_68k_emulator(NULL, native, vectors, 0);
    for (unsigned size = 0; size < 3; ++size)
        for (unsigned kind = 0; kind < 4; ++kind)
            for (unsigned left = 0; left < 2; ++left)
                for (unsigned dst = 0; dst < 8; ++dst) {
                    for (unsigned src = 0; src < 8; ++src)
                        for (unsigned count = 0; count < 64; ++count)
                            shift_case(kind, left, size, dst, src, count, edge(count % 11), count & 31);
                    for (unsigned sample = 0; sample < 11; ++sample) {
                        for (unsigned count = 0; count < 64; ++count)
                            shift_case(kind, left, size, dst, (dst + 1) & 7, count, edge(sample), count & 31);
                        for (unsigned count = 1; count <= 8; ++count)
                            shift_case(kind, left, size, dst, 8, count, edge(sample), (sample + count) & 31);
                    }
                }
    for (unsigned size = 0; size < 3; ++size)
        for (unsigned operation = 0; operation < 3; ++operation)
            for (unsigned dst = 0; dst < 8; ++dst)
                for (unsigned src = 0; src < (operation == 2 ? 1 : 8); ++src)
                    for (unsigned sample = 0; sample < 64; ++sample)
                        for (unsigned state = 0; state < 4; ++state) {
                            uint32 input[16], expected[16];
                            registers(input);
                            unsigned bits = 8U << size;
                            uint32 mask = UINT32_MAX >> (32 - bits), sign = 1U << (bits - 1);
                            uint32 boundaries[] = {0, 1, mask, sign, sign - 1, sign + 1, mask - 1};
                            input[dst] =
                                (random_word() & ~mask) | (sample < 49 ? boundaries[sample / 7] : random_word() & mask);
                            if (operation != 2)
                                input[src] = (random_word() & ~mask) |
                                             (sample < 49 ? boundaries[sample % 7] : random_word() & mask);
                            memcpy(expected, input, sizeof expected);
                            uint32 a = operation == 2 ? 0 : input[dst] & mask;
                            uint32 b = input[operation == 2 ? dst : src] & mask;
                            unsigned x = !!(state & 2);
                            uint32 result = (operation == 0 ? a + b + x : a - b - x) & mask;
                            int64 sa = a & sign ? (int64)a - ((int64)1 << bits) : a;
                            int64 sb = b & sign ? (int64)b - ((int64)1 << bits) : b;
                            int64 signed_result = operation == 0 ? sa + sb + x : sa - sb - x;
                            unsigned c = operation == 0 ? ((uint64)a + b + x) >> bits : (uint64)a < (uint64)b + x;
                            unsigned v = signed_result < -(int64)sign || signed_result >= (int64)sign;
                            expected[dst] = (input[dst] & ~mask) | result;
                            unsigned flags = (sample & 11) | (state & 1 ? 4 : 0) | (x << 4);
                            unsigned answer =
                                c | (v << 1) | (((state & 1) && !result) << 2) | (!!(result & sign) << 3) | (c << 4);
                            unsigned op =
                                operation == 2 ? 0x4000 | dst : (operation == 0 ? 0xD100 : 0x9100) | dst << 9 | src;
                            execute(op | size << 6, 1, 0, input, expected, flags, answer, 0);
                        }
    for (unsigned kind = 0; kind < 4; ++kind)
        for (unsigned dst = 0; dst < 8; ++dst)
            for (unsigned src = 0; src <= 8; ++src)
                for (unsigned count = 0; count < 256; ++count) {
                    uint32 input[16], expected[16];
                    registers(input);
                    input[dst] = edge(count % 11);
                    if (src < 8)
                        input[src] = count | 0xFEDC0000;
                    memcpy(expected, input, sizeof expected);
                    unsigned index = (src < 8 ? input[src] : count) & 31;
                    uint32 bit = (uint32)1 << index;
                    unsigned flags = count & 31;
                    unsigned answer = (flags & ~4U) | (!(input[dst] & bit) << 2);
                    if (kind == 1)
                        expected[dst] ^= bit;
                    else if (kind == 2)
                        expected[dst] &= ~bit;
                    else if (kind == 3)
                        expected[dst] |= bit;
                    unsigned op = (src < 8 ? 0x0100 | src << 9 : 0x0800) | kind << 6 | dst;
                    execute(op, src < 8 ? 1 : 2, count | 0xAB00, input, expected, flags, answer, 27);
                }
    for (unsigned cc = 0; cc < 16; ++cc)
        for (unsigned flags = 0; flags < 32; ++flags)
            for (unsigned dst = 0; dst < 8; ++dst) {
                uint32 input[16], expected[16];
                registers(input);
                memcpy(expected, input, sizeof expected);
                expected[dst] = (input[dst] & ~255U) | (truth(cc, flags) ? 255 : 0);
                execute(0x50C0 | cc << 8 | dst, 1, 0, input, expected, flags, flags, 31);
            }
    printf("%u shift/rotate/carry/bit/Scc cases passed (%s).\n", cases, native ? "x64 JIT" : "interpreter");
    return 0;
}
