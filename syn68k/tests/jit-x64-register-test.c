/*+===================================================================
  File:      jit-x64-register-test.c

  Summary:   Implements jit x64 register test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Register-only instructions checked against an independent 68K model. */
#include "syn68k_public.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

enum operation {
    move,
    movea,
    clear,
    test,
    invert,
    negate,
    quick_add,
    quick_sub,
    swap,
    ext_word,
    ext_long,
    ext_byte,
    exchange,
    address_add,
    address_sub,
    address_compare
};

static uint32 seed = 0x97532021;
static unsigned cases;
static int native;

static uint32 random_word(void)
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

static uint32 value(unsigned sample)
{
    static const uint32 edges[] = {0, 1, 0xFFFFFFFF, 0x80000000, 0x7FFFFFFF, 0xFFFF, 0x8000, 0x7FFF, 0xFF, 0x80, 0x7F};
    return sample < sizeof edges / sizeof edges[0] ? edges[sample] : random_word();
}

static void check(unsigned op, unsigned words, uint32 immediate, enum operation operation, unsigned size, unsigned dst,
                  unsigned src, unsigned sample)
{
    uint32 expected[16];
    for (unsigned i = 0; i < 16; ++i)
        expected[i] = EM_DREG(i) = random_word();
    expected[dst] = EM_DREG(dst) = value(sample);
    if (src < 16)
        expected[src] = EM_DREG(src) = value(sample / 2);
    uint32 a = expected[dst];
    uint32 b = src < 16 ? expected[src] : immediate;
    unsigned bits = 8U << size;
    uint32 mask = UINT32_MAX >> (32 - bits), sign = 1U << (bits - 1);
    unsigned nz = !!(sample & 1), n = !!(sample & 2), c = !!(sample & 4);
    unsigned v = !!(sample & 8), x = !!(sample & 16);
    cpu_state.ccnz = nz;
    cpu_state.ccn = n;
    cpu_state.ccc = c;
    cpu_state.ccv = v;
    cpu_state.ccx = x;
    cpu_state.sr = 0;
    uint32 result = a;
    int set_flags = 1, write_result = 1;
    switch (operation) {
    case move:
        result = b;
        break;
    case movea:
        result = size == 1 ? (uint32)(int32)(int16)b : b;
        mask = UINT32_MAX;
        set_flags = 0;
        break;
    case clear:
        result = 0;
        break;
    case test:
        write_result = 0;
        break;
    case invert:
        result = ~a;
        break;
    case negate:
        result = 0U - (a & mask);
        c = x = (a & mask) != 0;
        v = (a & mask) == sign;
        break;
    case quick_add:
        result = (a & mask) + immediate;
        c = x = ((uint64)(a & mask) + immediate) >> bits;
        v = !!((~(a ^ immediate) & (a ^ result)) & sign);
        break;
    case quick_sub:
        result = (a & mask) - immediate;
        c = x = (a & mask) < immediate;
        v = !!(((a ^ immediate) & (a ^ result)) & sign);
        break;
    case swap:
        result = a << 16 | a >> 16;
        break;
    case ext_word:
        result = (uint32)(int32)(int8)a;
        break;
    case ext_long:
        result = (uint32)(int32)(int16)a;
        break;
    case ext_byte:
        result = (uint32)(int32)(int8)a;
        break;
    case exchange:
        expected[src] = a;
        result = b;
        set_flags = 0;
        break;
    case address_add:
    case address_sub:
    case address_compare:
        if (size == 1)
            b = (uint32)(int32)(int16)b;
        result = operation == address_add ? a + b : a - b;
        if (operation == address_compare) {
            c = a < b;
            v = !!(((a ^ b) & (a ^ result)) & 0x80000000);
            write_result = 0;
        }
        else
            set_flags = 0;
        bits = 32;
        sign = 0x80000000;
        mask = UINT32_MAX;
        break;
    }
    if (write_result)
        expected[dst] = (a & ~mask) | (result & mask);
    if (set_flags) {
        nz = (result & mask) != 0;
        n = !!(result & sign);
        if (operation != negate && operation != quick_add && operation != quick_sub && operation != address_compare)
            c = v = 0;
    }
    destroy_blocks(0x1000, 32);
    WRITEUW(0x1000, op);
    if (words == 2)
        WRITEUW(0x1002, immediate);
    else if (words == 3)
        WRITEUL(0x1002, immediate);
    unsigned end = 0x1000 + words * 2;
    WRITEUW(end, 0x4E71);
    WRITEUW(end + 2, 0x4EF9);
    WRITEUL(end + 4, MAGIC_EXIT_EMULATOR_ADDRESS);
    uint64 before = syn68k_native_execution_count();
    interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
    assert(syn68k_native_execution_count() == before + native);
    for (unsigned i = 0; i < 16; ++i)
        if (EM_DREG(i) != expected[i]) {
            fprintf(stderr, "opcode %04x sample %u reg %u: %08x != %08x\n", op, sample, i, EM_DREG(i), expected[i]);
            abort();
        }
    assert(!!cpu_state.ccnz == nz && !!cpu_state.ccn == n);
    assert(!!cpu_state.ccc == c && !!cpu_state.ccv == v && !!cpu_state.ccx == x);
    ++cases;
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
    for (unsigned sample = 0; sample < 64; ++sample)
        for (unsigned dst = 0; dst < 8; ++dst) {
            for (unsigned size = 0; size < 3; ++size) {
                unsigned base = size == 0 ? 0x1000 : size == 1 ? 0x3000 : 0x2000;
                for (unsigned src = 0; src < (size == 0 ? 8 : 16); ++src) {
                    check(base | dst << 9 | src, 1, 0, move, size, dst, src, sample);
                    if (size)
                        check(base | 0x40 | dst << 9 | src, 1, 0, movea, size, dst + 8, src, sample);
                }
                unsigned words = size == 2 ? 3 : 2;
                check(base | 0x3C | dst << 9, words, value(sample), move, size, dst, 16, sample);
                if (size)
                    check(base | 0x7C | dst << 9, words, value(sample), movea, size, dst + 8, 16, sample);
                for (unsigned kind = 0; kind < 4; ++kind) {
                    static const unsigned ops[] = {0x4200, 0x4A00, 0x4600, 0x4400};

                    static const enum operation kinds[] = {clear, test, invert, negate};

                    check(ops[kind] | size << 6 | dst, 1, 0, kinds[kind], size, dst, 16, sample);
                }
                for (unsigned quick = 1; quick <= 8; ++quick)
                    for (unsigned sub = 0; sub < 2; ++sub)
                        check(0x5000 | sub << 8 | (quick & 7) << 9 | size << 6 | dst, 1, quick,
                              sub ? quick_sub : quick_add, size, dst, 16, sample);
                if (size)
                    for (unsigned kind = 0; kind < 3; ++kind)
                        for (unsigned src = 0; src <= 16; ++src) {
                            static const unsigned ops[] = {0xD0C0, 0x90C0, 0xB0C0};

                            static const enum operation kinds[] = {address_add, address_sub, address_compare};

                            unsigned op = ops[kind] | (size == 2 ? 0x100 : 0) | dst << 9 | (src == 16 ? 0x3C : src);
                            check(op, src == 16 ? words : 1, value(sample), kinds[kind], size, dst + 8, src, sample);
                        }
            }
            for (unsigned kind = 0; kind < 4; ++kind) {
                static const unsigned ops[] = {0x4840, 0x4880, 0x48C0, 0x49C0};

                static const enum operation kinds[] = {swap, ext_word, ext_long, ext_byte};

                check(ops[kind] | dst, 1, 0, kinds[kind], kind == 1 ? 1 : 2, dst, 16, sample);
            }
            for (unsigned src = 0; src < 8; ++src) {
                check(0xC140 | dst << 9 | src, 1, 0, exchange, 2, dst, src, sample);
                check(0xC148 | dst << 9 | src, 1, 0, exchange, 2, dst + 8, src + 8, sample);
                check(0xC188 | dst << 9 | src, 1, 0, exchange, 2, dst, src + 8, sample);
            }
        }
    printf("%u move/unary/quick/exchange/address cases passed (%s).\n", cases, native ? "x64 JIT" : "interpreter");
    return 0;
}
