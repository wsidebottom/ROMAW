/*+===================================================================
  File:      rtd-test.c

  Summary:   Implements rtd test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Original RTD live-extension, stack, condition and boundary fixtures. */
#include "syn68k_public.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned cases, trapped;
static uint32 instruction_pc = 0x1000, expected_fault;
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
}

static syn68k_addr_t handler(syn68k_addr_t pc, void* argument)
{
    trapped = (unsigned)(uintptr_t)argument;
    assert(trapped == 2 && pc == instruction_pc);
    assert(READUW(EM_A7) == expected_sr);
    assert(READUL(EM_A7 + 2) == instruction_pc);
    assert(READUW(EM_A7 + 6) == 0x7008);
    assert(READUL(EM_A7 + 20) == expected_fault);
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static void run(void)
{
    interpret_code(hash_lookup_code_and_create_if_needed(instruction_pc));
    ++cases;
}

int main(int argc, char** argv)
{
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

    WRITEUW(0x1000, 0x4e74);
    WRITEUW(0x1002, 0);
    reset(23, 0);
    WRITEUL(EM_A7, MAGIC_EXIT_EMULATOR_ADDRESS);
    run();
    assert(!trapped && EM_A7 == 0x6004 && status() == expected_sr);
    // A changed cached extension must be fetched by the executed instruction.
    WRITEUW(0x1002, 32);
    reset(23, 0);
    WRITEUL(EM_A7, MAGIC_EXIT_EMULATOR_ADDRESS);
    run();
    if (EM_A7 != 0x6024)
        fprintf(stderr, "RTD used a stale extension: SP=%08x, expected 00006024\n", EM_A7);
    assert(!trapped && EM_A7 == 0x6024 && status() == expected_sr);

    // Every signed 16-bit displacement, all stack banks and CCR combinations.
    for (unsigned displacement = 0; displacement <= 0xffff; ++displacement) {
        WRITEUW(0x1002, displacement);
        reset(displacement & 31, displacement % 3);
        uint32 stack = EM_A7;
        WRITEUL(stack, MAGIC_EXIT_EMULATOR_ADDRESS);
        run();
        assert(!trapped && EM_A7 == stack + 4 + (int16)displacement);
        assert(status() == expected_sr);
    }

    // Return targets are live stack values, including unaligned data accesses.
    const uint32 targets[] = {0x2000, 0x2100, 0x2200};
    for (unsigned i = 0; i < 3; ++i) {
        WRITEUW(targets[i], 0x7030 | i); // MOVEQ #48+i,D0.
        WRITEUW(targets[i] + 2, 0x4ef9);
        WRITEUL(targets[i] + 4, MAGIC_EXIT_EMULATOR_ADDRESS);
    }
    for (unsigned bank = 0; bank < 3; ++bank)
        for (unsigned ccr = 0; ccr < 32; ++ccr)
            for (unsigned offset = 0; offset < 4; ++offset)
                for (unsigned target = 0; target < 3; ++target) {
                    reset(ccr, bank);
                    EM_A7 += offset;
                    uint32 stack = EM_A7;
                    WRITEUL(stack, targets[target]);
                    WRITEUW(0x1002, 0xfff8);
                    run();
                    assert(!trapped && EM_A7 == stack - 4 && EM_D0 == 48 + target);
                }

    // All partial/unmapped stack reads with a separate, valid exception stack.
    DWORD previous;
    assert(VirtualProtect((uint8*)memory + 0xff000, 0x1000, PAGE_NOACCESS, &previous));
    syn68k_set_memory_window(0xff000, 0, 0);
    WRITEUW(0x1002, 32);
    for (unsigned ccr = 0; ccr < 32; ++ccr)
        for (unsigned available = 0; available < 4; ++available) {
            reset(ccr, 0);
            EM_A7 = 0xff000 - available;
            uint32 stack = EM_A7;
            expected_fault = 0xff000;
            run();
            assert(trapped == 2 && cpu_state.usp == stack);
        }

    // A valid last longword does not read beyond the memory window.
    reset(31, 0);
    EM_A7 = 0xfeffc;
    WRITEUL(EM_A7, MAGIC_EXIT_EMULATOR_ADDRESS);
    WRITEUW(0x1002, 0xfffc);
    run();
    assert(!trapped && EM_A7 == 0xfeffc && status() == expected_sr);

    // Missing extension bytes cannot be fetched by the translator into a guard.
    for (unsigned available = 0; available < 2; ++available) {
        instruction_pc = 0xfeffe;
        WRITEUW(instruction_pc, 0x4e74);
        if (available) {
            assert(VirtualProtect((uint8*)memory + 0xff000, 0x1000, PAGE_READWRITE, &previous));
            WRITEUB(0xff000, 0);
        }
        syn68k_set_memory_window(0xff000 + available, 0, 0);
        destroy_blocks(instruction_pc, 2);
        reset(31, 1);
        WRITEUL(EM_A7, MAGIC_EXIT_EMULATOR_ADDRESS);
        expected_fault = instruction_pc + 2 + available;
        run();
        assert(trapped == 2);
    }

    // Native arithmetic flags must be committed before entering the RTD helper.
    instruction_pc = 0x1000;
    WRITEUW(0x1000, 0x5280); // ADDQ.L #1,D0.
    WRITEUW(0x1002, 0x4e74);
    WRITEUW(0x1004, 0);
    destroy_blocks(0x1000, 8);
    const uint32 inputs[] = {0xffffffff, 0x7fffffff, 0, 0x80000000};
    const uint16 flags[] = {0x15, 0x0a, 0, 8};
    for (unsigned i = 0; i < 4; ++i)
        for (unsigned ccr = 0; ccr < 32; ++ccr) {
            reset(ccr, 1);
            EM_D0 = inputs[i];
            WRITEUL(EM_A7, MAGIC_EXIT_EMULATOR_ADDRESS);
            run();
            assert(!trapped && EM_D0 == inputs[i] + 1);
            assert(EM_A7 == 0x8004 && status() == (0x2000 | flags[i]));
        }
    printf("RTD: %u live-extension/stack/CCR/boundary cases passed (%s)\n", cases, native ? "native" : "interpreter");
    return 0;
}
