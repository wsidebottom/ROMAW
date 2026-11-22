/*+===================================================================
  File:      rts-test.c

  Summary:   Implements rts test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Original RTS cache, stack, condition and boundary fixtures. */
#include "syn68k_public.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned cases, trapped;
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
}

static syn68k_addr_t handler(syn68k_addr_t pc, void* argument)
{
    trapped = (unsigned)(uintptr_t)argument;
    if (trapped != 2 || pc != expected_pc)
        fprintf(stderr, "RTS case %u: vector %u PC %08x expected %08x\n", cases, trapped, pc, expected_pc);
    assert(trapped == 2 && pc == expected_pc);
    assert(READUW(EM_A7) == expected_sr);
    assert(READUL(EM_A7 + 2) == expected_pc);
    assert(READUW(EM_A7 + 6) == 0x7008);
    assert(READUL(EM_A7 + 20) == expected_fault);
    return MAGIC_EXIT_EMULATOR_ADDRESS;
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
    WRITEUW(0x1000, 0x4e75);
    const uint32 targets[] = {0x2000, 0x2100, 0x2200};
    for (unsigned i = 0; i < 3; ++i) {
        WRITEUW(targets[i], 0x207c); // MOVEA.L #marker,A0 preserves CCR.
        WRITEUL(targets[i] + 2, i + 48);
        WRITEUW(targets[i] + 6, 0x4ef9);
        WRITEUL(targets[i] + 8, MAGIC_EXIT_EMULATOR_ADDRESS);
    }
    for (unsigned bank = 0; bank < 3; ++bank)
        for (unsigned ccr = 0; ccr < 32; ++ccr)
            for (unsigned offset = 0; offset < 4; ++offset)
                for (unsigned target = 0; target < 3; ++target) {
                    reset(ccr, bank);
                    EM_A7 += offset;
                    uint32 stack = EM_A7;
                    WRITEUL(stack, targets[target]);
                    run();
                    assert(!trapped && EM_A7 == stack + 4 && EM_A0 == 48 + target);
                    assert(status() == expected_sr);
                }
    DWORD previous;
    assert(VirtualProtect((uint8*)memory + 0xff000, 0x1000, PAGE_NOACCESS, &previous));
    syn68k_set_memory_window(0xff000, 0, 0);
    for (unsigned ccr = 0; ccr < 32; ++ccr)
        for (unsigned available = 0; available < 4; ++available) {
            reset(ccr, 0);
            EM_A7 = 0xff000 - available;
            uint32 stack = EM_A7;
            for (unsigned i = 0; i < available; ++i)
                WRITEUB(stack + i, 0xff);
            expected_fault = 0xff000;
            run();
            assert(trapped == 2 && cpu_state.usp == stack);
        }
    // Detect unsigned address wrap before issuing a host read.
    for (unsigned i = 0; i < 4; ++i) {
        reset(i, 0);
        expected_fault = EM_A7 = UINT32_MAX - i;
        run();
        assert(trapped == 2 && cpu_state.usp == expected_fault);
    }
    reset(31, 0);
    EM_A7 = 0xfeffc;
    WRITEUL(EM_A7, MAGIC_EXIT_EMULATOR_ADDRESS);
    run();
    assert(!trapped && EM_A7 == 0xff000 && status() == 31);
    // A native producer leaves flags and an offset A7 mapping live at RTS.
    WRITEUW(0x1000, 0x588f); // ADDQ.L #4,A7.
    WRITEUW(0x1002, 0x5280); // ADDQ.L #1,D0.
    WRITEUW(0x1004, 0x4e75);
    destroy_blocks(0x1000, 6);
    for (unsigned bank = 0; bank < 3; ++bank)
        for (unsigned ccr = 0; ccr < 32; ++ccr) {
            reset(ccr, bank);
            uint32 stack = EM_A7;
            EM_D0 = UINT32_MAX;
            WRITEUL(stack + 4, MAGIC_EXIT_EMULATOR_ADDRESS);
            run();
            assert(!trapped && EM_D0 == 0 && EM_A7 == stack + 8);
            assert(status() == (cpu_state.sr | 0x15));
        }
    for (unsigned ccr = 0; ccr < 32; ++ccr) {
        reset(ccr, 0);
        EM_D0 = UINT32_MAX;
        EM_A7 = 0xfeffc;
        expected_sr = 0x15;
        expected_pc = 0x1004;
        expected_fault = 0xff000;
        run();
        assert(trapped == 2 && cpu_state.usp == 0xff000);
    }
    // Exercise actual JSR/RTS returns as well as standalone saved targets.
    WRITEUW(0x1000, 0x4eb9); // JSR absolute long.
    WRITEUL(0x1002, 0x3000);
    WRITEUW(0x1006, 0x4ef9); // JMP absolute long.
    WRITEUL(0x1008, MAGIC_EXIT_EMULATOR_ADDRESS);
    WRITEUW(0x3000, 0x4e75);
    destroy_blocks(0x1000, 12);
    for (unsigned bank = 0; bank < 3; ++bank)
        for (unsigned ccr = 0; ccr < 32; ++ccr)
            for (unsigned repeat = 0; repeat < 4; ++repeat) {
                reset(ccr, bank);
                uint32 stack = EM_A7;
                run();
                assert(!trapped && EM_A7 == stack && status() == expected_sr);
            }
    // BSR retains the optional native return cache; warm and repeat its returns.
    WRITEUW(0x1000, 0x6100);
    WRITEUW(0x1002, 0x1ffe);
    WRITEUW(0x1004, 0x4ef9);
    WRITEUL(0x1006, MAGIC_EXIT_EMULATOR_ADDRESS);
    destroy_blocks(0x1000, 12);
    for (unsigned repeat = 0; repeat < 4; ++repeat)
        for (unsigned ccr = 0; ccr < 32; ++ccr) {
            reset(ccr, 0);
            uint32 stack = EM_A7;
            run();
            assert(!trapped && EM_A7 == stack && status() == expected_sr);
        }
    printf("RTS: %u stack/cache/CCR/boundary cases passed (%s)\n", cases, native ? "native" : "interpreter");
    return 0;
}
