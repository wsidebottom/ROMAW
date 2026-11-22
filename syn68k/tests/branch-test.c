/*+===================================================================
  File:      branch-test.c

  Summary:   Implements branch test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Original relative branch live-displacement and condition fixtures. */
#include "syn68k_public.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned cases, trapped, expected_vector;
static uint32 expected_stack;
static uint32 expected_pc = 0x1000, expected_fault;
static uint16 expected_sr;

static uint16 status(void)
{
    return cpu_state.sr | !!cpu_state.ccc | (!!cpu_state.ccv << 1) | (!cpu_state.ccnz << 2) | (!!cpu_state.ccn << 3) |
           (!!cpu_state.ccx << 4);
}

static void reset(unsigned ccr, unsigned bank)
{
    cpu_state.sr = bank == 0 ? 0 : bank == 1 ? 0x2000 : 0x3000;
    cpu_state.ccc = ccr & 1;
    cpu_state.ccv = ccr & 2;
    cpu_state.ccnz = (~ccr) & 4;
    cpu_state.ccn = ccr & 8;
    cpu_state.ccx = ccr & 16;
    cpu_state.usp = 0x6000;
    cpu_state.isp = 0x8000;
    cpu_state.msp = 0xa000;
    EM_A7 = bank == 0 ? cpu_state.usp : bank == 1 ? cpu_state.isp : cpu_state.msp;
    expected_sr = status();
    trapped = 0;
    expected_vector = 2;
    expected_pc = 0x1000;
    expected_stack = EM_A7;
}

static syn68k_addr_t handler(syn68k_addr_t pc, void* argument)
{
    trapped = (unsigned)(uintptr_t)argument;
    if (trapped != expected_vector || pc != expected_pc)
        fprintf(stderr, "Control-transfer case %u: vector %u PC %08x expected %08x\n", cases, trapped, pc, expected_pc);
    assert(trapped == expected_vector && pc == expected_pc);
    assert(READUW(EM_A7) == expected_sr);
    assert(READUL(EM_A7 + 2) == expected_pc);
    assert(READUW(EM_A7 + 6) == (trapped == 2 ? 0x7008 : 0x0010));
    if (trapped == 2)
        assert(READUL(EM_A7 + 20) == expected_fault);
    assert(cpu_state.usp == expected_stack);
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static void target(uint32 pc, unsigned marker)
{
    WRITEUW(pc, 0x2c7c);
    WRITEUL(pc + 2, marker);
    WRITEUW(pc + 6, 0x4ef9);
    WRITEUL(pc + 8, MAGIC_EXIT_EMULATOR_ADDRESS);
    destroy_blocks(pc, 12);
}

static void instruction(unsigned condition, unsigned size, int32 displacement)
{
    WRITEUW(0x1000, 0x6000 | (condition << 8) | (size == 1 ? displacement & 255 : size == 2 ? 0 : 255));
    if (size == 2)
        WRITEUW(0x1002, displacement);
    else if (size == 4)
        WRITEUL(0x1002, displacement);
    target(0x1000 + (size == 1 ? 2 : size + 2), 0);
    target(0x1002 + displacement, 1);
    destroy_blocks(0x1000, 32);
}

static void run(void)
{
    interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
    ++cases;
}

int main(int argc, char** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    void* memory = VirtualAlloc(NULL, 1024 * 1024, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(1024 * 1024, 0, 0);
    uint32 vectors[64] = {0};
    int native = argc > 1 && atoi(argv[1]);
    initialize_68k_emulator(NULL, native, vectors, 0);
    native = syn68k_native_code_enabled();
    for (unsigned i = 0; i < 64; ++i)
        trap_install_handler(i, handler, (void*)(uintptr_t)i);
    WRITEUW(0x1000, 0x6600); // BNE.W
    WRITEUW(0x1002, 0x1ffe);
    for (unsigned i = 0; i < 2; ++i) {
        WRITEUW(0x3000 + i * 256, 0x2c7c);
        WRITEUL(0x3002 + i * 256, i + 1);
        WRITEUW(0x3006 + i * 256, 0x4ef9);
        WRITEUL(0x3008 + i * 256, MAGIC_EXIT_EMULATOR_ADDRESS);
    }
    reset(0, 0);
    run();
    assert(EM_A6 == 1);
    WRITEUW(0x1002, 0x20fe);
    reset(0, 0);
    run();
    assert(EM_A6 == 2);
    // Truth masks independently enumerate all incoming CCR combinations.
    const uint32 taken[16] = {0xffffffffu, 0x0u,        0x5050505u,  0xfafafafau, 0x55555555u, 0xaaaaaaaau,
                              0xf0f0f0fu,  0xf0f0f0f0u, 0x33333333u, 0xccccccccu, 0xff00ffu,   0xff00ff00u,
                              0xcc33cc33u, 0x33cc33ccu, 0xc030c03u,  0xf3fcf3fcu};
    const int32 offsets[] = {-256, -128, -64, -16, 16, 64, 126, 128, 256, 32766};
    for (unsigned condition = 0; condition < 16; ++condition) {
        if (condition == 1) // BSR has a separate fixture.
            continue;
        for (unsigned size = 1; size <= 4; size *= 2)
            for (unsigned i = 0; i < sizeof offsets / sizeof offsets[0]; ++i) {
                if (size == 1 && (offsets[i] < -128 || offsets[i] > 127))
                    continue;
                instruction(condition, size, offsets[i]);
                for (unsigned bank = 0; bank < 3; ++bank)
                    for (unsigned ccr = 0; ccr < 32; ++ccr) {
                        reset(ccr, bank);
                        uint32 stack = EM_A7;
                        run();
                        assert(!trapped && EM_A6 == ((taken[condition] >> ccr) & 1));
                        assert(EM_A7 == stack && status() == expected_sr);
                    }
            }
    }

    // Live word/long displacement edits cover each condition and both outcomes.
    for (unsigned condition = 0; condition < 16; ++condition)
        for (unsigned size = 2; size <= 4; size += 2) {
            if (condition == 1)
                continue;
            instruction(condition, size, 0x1ffe);
            target(0x3100, 2);
            for (unsigned edit = 0; edit < 2; ++edit) {
                if (size == 2)
                    WRITEUW(0x1002, edit ? 0x20fe : 0x1ffe);
                else
                    WRITEUL(0x1002, edit ? 0x20fe : 0x1ffe);
                for (unsigned ccr = 0; ccr < 32; ++ccr) {
                    reset(ccr, 0);
                    run();
                    assert(!trapped && status() == expected_sr);
                    assert(EM_A6 == (((taken[condition] >> ccr) & 1) ? edit + 1 : 0));
                }
            }
        }

    // Flags produced by native arithmetic must govern the next extended branch.
    for (unsigned condition = 0; condition < 16; ++condition) {
        if (condition == 1)
            continue;
        instruction(condition, 2, 0x1ffe);
        WRITEUW(0x0ffe, 0x5280); // ADDQ.L #1,D0 produces X/Z/C.
        destroy_blocks(0x0ffe, 32);
        reset(0, 0);
        EM_D0 = 0xffffffff;
        interpret_code(hash_lookup_code_and_create_if_needed(0x0ffe));
        ++cases;
        assert(!trapped && EM_A6 == ((taken[condition] >> 0x15) & 1));
        assert((status() & 31) == 0x15);
    }

    DWORD old;
    assert(VirtualProtect((char*)memory + 0xff000, 0x1000, PAGE_NOACCESS, &old));
    for (unsigned condition = 0; condition < 16; ++condition)
        for (unsigned size = 2; size <= 4; size += 2)
            for (unsigned available = 0; available < size; ++available) {
                if (condition == 1)
                    continue;
                uint32 pc = available ? 0xfef00 : 0xfeffe;
                WRITEUW(pc, 0x6000 | (condition << 8) | (size == 2 ? 0 : 255));
                syn68k_set_memory_window(pc + 2 + available, 0, 0);
                destroy_blocks(pc, 8);
                for (unsigned ccr = 0; ccr < 32; ++ccr) {
                    reset(ccr, 0);
                    expected_pc = pc;
                    expected_fault = pc + 2 + available;
                    interpret_code(hash_lookup_code_and_create_if_needed(pc));
                    ++cases;
                    assert(trapped == 2);
                }
            }
    printf("%u relative branch cases passed (%s)\n", cases, native ? "native" : "interpreter");
    return 0;
}
