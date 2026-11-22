/*+===================================================================
  File:      jit-x64-liveness-test.c

  Summary:   Implements jit x64 liveness test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Mixed flag producers/consumers and materialization at bounded exits. */
#include "jit-x64.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32 seed = 0x3EB75329;

static uint32 random_value(void)
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

static unsigned status(void)
{
    return !!cpu_state.ccc | !!cpu_state.ccv << 1 | !cpu_state.ccnz << 2 | !!cpu_state.ccn << 3 | !!cpu_state.ccx << 4;
}

extern int syn68k_x64_optimize_flags;

int main(int argc, char** argv)
{
    if (argc > 1)
        syn68k_x64_optimize_flags = atoi(argv[1]);
    void* memory = calloc(1, 0x10000);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(0x10000, 0, 0);
    uint32 vectors[64] = {0};
    initialize_68k_emulator(NULL, 1, vectors, 0);
    for (unsigned trial = 0; trial < 2000; ++trial) {
        unsigned pc = 0x1000;
        unsigned count = trial & 1 ? 80 : 32;
        for (unsigned i = 0; i < count; ++i) {
            unsigned value = random_value(), src = (value >> 8) & 7;
            unsigned dst = (value >> 12) & 7, op;
            if (trial >= 1000 && (i < 16 || (value & 1))) {
                static const unsigned operands[] = {0x5290, 0x5390, 0xD090, 0x9190, 0xB090, 0x4690, 0x4A90,
                                                    0x4210, 0x50D0, 0x4AD0, 0x0110, 0x0190, 0x2010, 0x2080};
                op = i < 16 ? 0x5290 : operands[(value >> 1) % 14];
                unsigned address = i < 16 ? 0 : (value >> 16) & 7;
                op |= op == 0x2080 ? address << 9 : address;
                WRITEUW(pc, op);
                pc += 2;
                continue;
            }
            switch (value % 20) {
            case 0:
                op = 0xD080 | dst << 9 | src;
                break;
            case 1:
                op = 0x9040 | dst << 9 | src;
                break;
            case 2:
                op = 0x8000 | dst << 9 | src;
                break;
            case 3:
                op = 0xB080 | dst << 9 | src;
                break;
            case 4:
                op = 0xD180 | dst << 9 | src;
                break;
            case 5:
                op = 0x9140 | dst << 9 | src;
                break;
            case 6:
                op = 0x4000 | dst;
                break;
            case 7:
                op = 0xE000 | src << 9 | (value & 1) << 8 | ((value >> 16) % 3) << 6 | (value & 0x20) |
                     ((value >> 18) & 3) << 3 | dst;
                break;
            case 8:
                WRITEUW(pc, 0x0A80 | dst);
                WRITEUL(pc + 2, random_value());
                pc += 6;
                continue;
            case 9:
                op = 0x50C0 | ((value >> 16) & 15) << 8 | dst;
                break;
            case 10:
                op = 0x0100 | src << 9 | ((value >> 16) & 3) << 6 | dst;
                break;
            case 11:
                op = 0xC140 | dst << 9 | src;
                break;
            case 12:
                op = 0x7000 | dst << 9 | (value & 255);
                break;
            case 13: {
                static const unsigned logical[] = {0x003C, 0x023C, 0x0A3C};
                WRITEUW(pc, logical[(value >> 16) % 3]);
                WRITEUW(pc + 2, random_value());
                pc += 4;
                continue;
            }
            case 14:
                op = 0x44C0 | src;
                break;
            case 15:
                op = 0x42C0 | dst;
                break;
            case 16:
                op = 0x4AC0 | dst;
                break;
            case 17:
                op = 0x4280 | dst;
                break;
            case 18:
                op = 0x2000 | dst << 9 | src;
                break;
            default:
                op = 0x5080 | src << 9 | (value & 1) << 8 | dst;
                break;
            }
            WRITEUW(pc, op);
            pc += 2;
        }
        WRITEUW(pc, 0x4EF9);
        WRITEUL(pc + 2, MAGIC_EXIT_EMULATOR_ADDRESS);
        uint32 registers[16], expected[16];
        for (unsigned i = 0; i < 16; ++i)
            registers[i] = trial >= 1000 && i >= 8 ? 0x4080 : random_value();
        unsigned char bytes[256], expected_bytes[256];
        for (unsigned i = 0; i < sizeof bytes; ++i)
            bytes[i] = random_value();
        unsigned flags = random_value() & 31, expected_flags = 0;
        for (unsigned native = 0; native < 2; ++native) {
            syn68k_x64_enabled = native;
            destroy_blocks(0x1000, 2048);
            for (unsigned i = 0; i < 16; ++i)
                EM_DREG(i) = registers[i];
            memcpy(SYN68K_TO_US(0x4000), bytes, sizeof bytes);
            cpu_state.sr = 0;
            cpu_state.ccc = flags & 1 ? 0x80000000 : 0;
            cpu_state.ccv = flags & 2 ? 0x12345678 : 0;
            cpu_state.ccnz = flags & 4 ? 0 : 0x80000000;
            cpu_state.ccn = flags & 8 ? 0x87654321 : 0;
            cpu_state.ccx = flags & 16 ? 0x11111111 : 0;
            uint64 before = syn68k_native_execution_count();
            interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
            if (!native) {
                for (unsigned i = 0; i < 16; ++i)
                    expected[i] = EM_DREG(i);
                expected_flags = status();
                memcpy(expected_bytes, SYN68K_TO_US(0x4000), sizeof bytes);
            }
            else {
                assert(syn68k_native_execution_count() > before);
                for (unsigned i = 0; i < 16; ++i)
                    if (EM_DREG(i) != expected[i]) {
                        fprintf(stderr, "trial=%u r%u expected=%08X actual=%08X flags=%X/%X\n", trial, i, expected[i],
                                EM_DREG(i), expected_flags, status());
                        for (unsigned at = 0x1000; at <= pc; at += 2)
                            fprintf(stderr, "%04X: %04X\n", at, READUW(at));
                        abort();
                    }
                assert(status() == expected_flags);
                assert(!memcmp(expected_bytes, SYN68K_TO_US(0x4000), sizeof bytes));
            }
        }
    }
    for (unsigned native = 0; native < 2; ++native) {
        syn68k_x64_enabled = native;
        destroy_blocks(0, UINT32_MAX);
        WRITEUW(0x1000, 0x7000); /* MOVEQ #0,D0 establishes Z. */
        WRITEUW(0x1002, 0x4001); /* NEGX.B D1 must preserve that Z. */
        WRITEUW(0x1004, 0x42C2);
        WRITEUW(0x1006, 0x4ED0);
        EM_A0 = MAGIC_EXIT_EMULATOR_ADDRESS;
        EM_D1 = 0;
        cpu_state.ccc = cpu_state.ccv = cpu_state.ccn = cpu_state.ccx = 0;
        cpu_state.ccnz = 1;
        interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
        assert((EM_D2 & 65535) == 4);
    }
    destroy_blocks(0, UINT32_MAX);
    for (unsigned i = 0; i < 70; ++i)
        WRITEUW(0x1000 + 2 * i, 0x5280);
    WRITEUW(0x108C, 0x4ED0);
    int8 lengths[71];
    memset(lengths, 1, sizeof lengths);
    unsigned translated;
    int terminal;
    void* code = syn68k_x64_compile(SYN68K_TO_US(0x1000), lengths, 71, &translated, 0x1000, &terminal);
    assert(code && translated == 64 && !terminal);
    if (syn68k_x64_optimize_flags) {
        unsigned nz_stores = 0;
        for (unsigned i = 0; i < 1022; ++i)
            if (((uint8*)code)[i] == 0x0F && ((uint8*)code)[i + 1] == 0x95 && ((uint8*)code)[i + 2] == 0xC2)
                ++nz_stores;
        assert(nz_stores == 1);
    }
    EM_D0 = UINT32_MAX - 63;
    syn68k_x64_execute(code);
    assert(!EM_D0 && status() == 0x15);
    syn68k_x64_release(code);
    for (unsigned i = 0; i < 70; ++i)
        WRITEUW(0x1000 + 2 * i, 0x5290);
    code = syn68k_x64_compile(SYN68K_TO_US(0x1000), lengths, 71, &translated, 0x1000, &terminal);
    assert(code && translated == 64 && terminal);
    if (syn68k_x64_optimize_flags) {
        unsigned nz_stores = 0;
        for (unsigned i = 0; i < 2046; ++i)
            if (((uint8*)code)[i] == 0x0F && ((uint8*)code)[i + 1] == 0x95 && ((uint8*)code)[i + 2] == 0xC2)
                ++nz_stores;
        assert(nz_stores == 1);
    }
    EM_A0 = 0x4000;
    WRITEUL(0x4000, UINT32_MAX - 63);
    assert(syn68k_x64_execute(code) == 0x1080);
    assert(!READUL(0x4000) && status() == 0x15);
    syn68k_x64_release(code);
    puts("2000 mixed register/memory flag-liveness programs and bounded-exit materialization passed.");
    return 0;
}
