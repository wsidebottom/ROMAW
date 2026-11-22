/*+===================================================================
  File:      jit-x64-test.c

  Summary:   Implements jit x64 test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Independent arithmetic model and proof of native execution. */
#include "syn68k_public.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static uint32 seed = 0x75439821;
static int native;
static unsigned interrupts;

static syn68k_addr_t interrupt_callback(syn68k_addr_t pc, void* argument)
{
    (void)argument;
    assert(pc == 0x1000);
    ++interrupts;
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static void run(unsigned end, int fallback)
{
    WRITEUW(end, fallback ? 0x4687 : 0x4E71); /* NOT.L D7 or NOP */
    if (fallback) {
        /* Keep a checked boundary even when absolute moves become native.
         EORI #0 preserves the stored value and its MOVE/NOT flags. */
        WRITEUW(end + 2, 0x23C7); /* MOVE.L D7,$4000 */
        WRITEUL(end + 4, 0x4000);
        WRITEUW(end + 8, 0x0AB9); /* EORI.L #0,$4000 */
        WRITEUL(end + 10, 0);
        WRITEUL(end + 14, 0x4000);
        end += 16;
    }
    WRITEUW(end + 2, 0x4EF9);
    WRITEUL(end + 4, MAGIC_EXIT_EMULATOR_ADDRESS);
    uint64 before = syn68k_native_execution_count();
    interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
    /* Opcode-only memory boundaries can dispatch separate native bodies for
     the MOVE and EORI, rather than batching both in the decoder. */
    assert(syn68k_native_execution_count() == before + native * (fallback ? 3 : 1));
}

static uint32 random_word(void)
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
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
    static const unsigned opcodes[] = {0xD000, 0x9000, 0xB000, 0xC000, 0x8000, 0xB100};
    static const uint32 edges[] = {0, 1, 0xFFFFFFFF, 0x80000000, 0x7FFFFFFF, 0xFFFF, 0x10000};
    unsigned cases = 0;
    for (unsigned size = 0; size < 3; ++size)
        for (unsigned operation = 0; operation < 6; ++operation)
            for (unsigned immediate = 0; immediate < 2; ++immediate)
                for (unsigned src = 0; src < 8; ++src)
                    for (unsigned dst = 0; dst < 8; ++dst)
                        for (unsigned sample = 0; sample < 64; ++sample) {
                            uint32 expected[16];
                            for (unsigned i = 0; i < 16; ++i)
                                expected[i] = EM_DREG(i) = random_word();
                            unsigned bits = 8U << size;
                            uint32 mask = UINT32_MAX >> (32 - bits);
                            uint32 sign = 1U << (bits - 1);
                            uint32 boundaries[] = {0, 1, mask, sign, sign - 1, sign + 1, mask - 1};
                            uint32 a = sample < 49 ? boundaries[sample / 7] : random_word();
                            uint32 b = sample < 49 ? boundaries[sample % 7] : random_word();
                            /* Nonzero unrelated upper bits must survive byte/word writes. */
                            a = (a & mask) | (random_word() & ~mask);
                            b = (b & mask) | (random_word() & ~mask);
                            expected[dst] = EM_DREG(dst) = a;
                            if (!immediate) {
                                expected[src] = EM_DREG(src) = b;
                                a = expected[dst];
                            }
                            uint32 upper = a & ~mask;
                            a &= mask;
                            b &= mask;
                            unsigned x = sample & 1;
                            cpu_state.ccx = x;
                            cpu_state.ccnz = sample & 2;
                            cpu_state.ccn = sample & 4;
                            cpu_state.ccc = sample & 8;
                            cpu_state.ccv = sample & 16;
                            cpu_state.sr = 0;
                            uint32 result;
                            unsigned carry = 0, overflow = 0;
                            switch (operation) {
                            case 0:
                                result = a + b;
                                carry = ((uint64)a + b) >> bits;
                                overflow = !!((~(a ^ b) & (a ^ result)) & sign);
                                x = carry;
                                break;
                            case 1:
                            case 2:
                                result = a - b;
                                carry = a < b;
                                overflow = !!(((a ^ b) & (a ^ result)) & sign);
                                if (operation == 1)
                                    x = carry;
                                break;
                            case 3:
                                result = a & b;
                                break;
                            case 4:
                                result = a | b;
                                break;
                            default:
                                result = a ^ b;
                                break;
                            }
                            result &= mask;
                            if (operation != 2)
                                expected[dst] = upper | result;
                            destroy_blocks(0x1000, 16);
                            unsigned pc = 0x1000;
                            if (immediate) {
                                static const unsigned ops[] = {0x0600, 0x0400, 0x0C00, 0x0200, 0x0000, 0x0A00};
                                WRITEUW(pc, ops[operation] | size << 6 | dst);
                                if (size == 2)
                                    WRITEUL(pc + 2, b);
                                else
                                    WRITEUW(pc + 2, b | (size == 0 ? random_word() & 0xFF00 : 0));
                                pc += size == 2 ? 6 : 4;
                            }
                            else {
                                WRITEUW(pc, opcodes[operation] | size << 6 | (operation == 5 ? src : dst) << 9 |
                                                (operation == 5 ? dst : src));
                                pc += 2;
                            }
                            WRITEUW(pc, 0x4E71);
                            WRITEUW(pc + 2, 0x4EF9);
                            WRITEUL(pc + 4, MAGIC_EXIT_EMULATOR_ADDRESS);
                            uint64 before = syn68k_native_execution_count();
                            interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
                            assert(syn68k_native_execution_count() == before + native);
                            for (unsigned i = 0; i < 16; ++i)
                                assert(EM_DREG(i) == expected[i]);
                            assert(!!cpu_state.ccnz == (result != 0));
                            assert(!!cpu_state.ccn == !!(result & sign));
                            assert(!!cpu_state.ccc == carry);
                            assert(!!cpu_state.ccv == overflow);
                            assert(!!cpu_state.ccx == x);
                            ++cases;
                        }
    printf("%u arithmetic/alias/flag cases passed; native executions: %llu\n", cases,
           (unsigned long long)syn68k_native_execution_count());
    /* Moves, address operations and quick arithmetic with a following
     unsupported instruction exercise the native-to-interpreter boundary. */
    for (unsigned sample = 0; sample < 256; ++sample)
        for (unsigned reg = 0; reg < 8; ++reg) {
            destroy_blocks(0x1000, 32);
            uint32 value = sample < 7 ? edges[sample] : random_word();
            WRITEUW(0x1000, 0x207C | reg << 9); /* MOVEA.L #value,An */
            WRITEUL(0x1002, value);
            WRITEUW(0x1006, 0x5088 | (sample & 7) << 9 | reg);
            WRITEUW(0x1008, 0x5148 | reg);            /* SUBQ.W #8,An */
            WRITEUW(0x100A, 0x2008 | reg << 9 | reg); /* MOVE.L An,Dn */
            WRITEUW(0x100C, 0x5280 | reg);            /* ADDQ.L #1,Dn */
            WRITEUW(0x100E, 0x5380 | reg);            /* SUBQ.L #1,Dn */
            uint32 expected = value + ((sample & 7) ? (sample & 7) : 8) - 8;
            cpu_state.ccx = 1;
            EM_D7 = value;
            run(0x1010, 1);
            assert(EM_AREG(reg) == expected);
            assert(EM_DREG(reg) == (reg == 7 ? ~expected : expected));
            assert(EM_D7 == (reg == 7 ? ~expected : ~value));
            assert(!!cpu_state.ccx == (expected == UINT32_MAX));
            uint32 result = reg == 7 ? ~expected : ~value;
            assert(!!cpu_state.ccnz == (result != 0));
            assert(!!cpu_state.ccn == (result >> 31));
            assert(!cpu_state.ccc && !cpu_state.ccv);

            destroy_blocks(0x1000, 32);
            WRITEUW(0x1000, 0x7000 | reg << 9 | sample);
            WRITEUW(0x1002, 0x2040 | reg << 9 | reg);
            WRITEUW(0x1004, 0x4280 | reg);
            WRITEUW(0x1006, 0x4A80 | reg);
            cpu_state.ccx = 7;
            run(0x1008, 0);
            assert(EM_DREG(reg) == 0);
            assert(EM_AREG(reg) == (uint32)(int32)(int8)sample);
            assert(!cpu_state.ccnz && !cpu_state.ccn && !cpu_state.ccc && !cpu_state.ccv && cpu_state.ccx == 7);

            destroy_blocks(0x1000, 32);
            WRITEUW(0x1000, 0x203C | reg << 9);
            WRITEUL(0x1002, value);
            WRITEUW(0x1006, 0x4A80 | reg);
            run(0x1008, 0);
            assert(EM_DREG(reg) == value);
            assert(!!cpu_state.ccnz == (value != 0));
            assert(!!cpu_state.ccn == (value >> 31));
            assert(!cpu_state.ccc && !cpu_state.ccv && cpu_state.ccx == 7);
        }
    destroy_blocks(0x1000, 256);
    for (unsigned i = 0; i < 70; ++i)
        WRITEUW(0x1000 + i * 2, 0x7000 | i);
    run(0x1000 + 140, 0);
    assert(EM_D0 == 69); /* The final six instructions use the interpreter. */
    destroy_blocks(0x1000, 256);
    WRITEUW(0x1000, 0x23C0); /* Native absolute MOVE.L D0,$4000. */
    WRITEUL(0x1002, 0x4000);
    WRITEUW(0x1006, 0x4EF9);
    WRITEUL(0x1008, MAGIC_EXIT_EMULATOR_ADDRESS);
    uint64 before = syn68k_native_execution_count();
    interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
    assert(EM_D0 == 69 && READUL(0x4000) == 69);
    assert(syn68k_native_execution_count() == before + native);
    if (native) {
        /* A pending IRQ must dispatch its callback without executing or
         advancing through the native prefix that it interrupted. */
        destroy_blocks(0x1000, 256);
        WRITEUW(0x1000, 0x702A);
        WRITEUW(0x1002, 0x4E71);
        WRITEUW(0x1004, 0x4EF9);
        WRITEUL(0x1006, MAGIC_EXIT_EMULATOR_ADDRESS);
        trap_install_handler(26, interrupt_callback, NULL);
        EM_D0 = 99;
        cpu_state.sr = 0x2000;
        cpu_state.isp = EM_A7 = 0xF000;
        before = syn68k_native_execution_count();
        interrupt_generate(2);
        interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
        assert(interrupts == 1 && EM_D0 == 99);
        assert(syn68k_native_execution_count() == before);
    }
    puts("6146 mixed move/quick/fallback/bounded-prefix cases passed.");
    return 0;
}
