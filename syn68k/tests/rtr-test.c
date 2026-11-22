/*+===================================================================
  File:      rtr-test.c

  Summary:   Implements rtr test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Original RTR stack-frame, stack, condition and boundary fixtures. */
#include "syn68k_public.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned cases, trapped;
static uint32 instruction_pc = 0x1000, expected_pc = 0x1000, expected_fault;
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
        fprintf(stderr, "RTR case %u: vector %u PC %08x expected %08x\n", cases, trapped, pc, expected_pc);
    assert(trapped == 2 && pc == expected_pc);
    assert(READUW(EM_A7) == expected_sr);
    assert(READUL(EM_A7 + 2) == expected_pc);
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
    WRITEUW(0x1000, 0x4e77);
    // All saved words: only CCR bits change, never the active stack bank or SR.
    for (unsigned saved = 0; saved <= 0xffff; ++saved) {
        reset((saved >> 5) & 31, saved % 3);
        uint32 stack = EM_A7;
        uint16 upper = cpu_state.sr;
        WRITEUW(stack, saved);
        WRITEUL(stack + 2, MAGIC_EXIT_EMULATOR_ADDRESS);
        run();
        assert(!trapped && EM_A7 == stack + 6);
        assert(status() == (upper | (saved & 31)));
    }
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
                    reset(ccr ^ 31, bank);
                    EM_A7 += offset;
                    uint32 stack = EM_A7;
                    uint16 upper = cpu_state.sr;
                    WRITEUW(stack, 0xffe0 | ccr);
                    WRITEUL(stack + 2, targets[target]);
                    run();
                    assert(!trapped && EM_A7 == stack + 6 && EM_A0 == 48 + target);
                    assert(status() == (upper | ccr));
                }
    DWORD previous;
    assert(VirtualProtect((uint8*)memory + 0xff000, 0x1000, PAGE_NOACCESS, &previous));
    syn68k_set_memory_window(0xff000, 0, 0);
    // Reject each truncated frame before committing the stack or restored CCR.
    for (unsigned ccr = 0; ccr < 32; ++ccr)
        for (unsigned available = 0; available < 6; ++available) {
            reset(ccr, 0);
            EM_A7 = 0xff000 - available;
            uint32 stack = EM_A7;
            for (unsigned i = 0; i < available; ++i)
                WRITEUB(stack + i, 0xff);
            expected_fault = 0xff000;
            run();
            assert(trapped == 2 && cpu_state.usp == stack);
        }
    reset(0, 0);
    EM_A7 = 0xfeffa;
    WRITEUW(EM_A7, 0xffff);
    WRITEUL(EM_A7 + 2, MAGIC_EXIT_EMULATOR_ADDRESS);
    run();
    assert(!trapped && EM_A7 == 0xff000 && status() == 31);
    // Arithmetic flags must be live on a fault, but replaced on a valid return.
    WRITEUW(0x1000, 0x5280); // ADDQ.L #1,D0.
    WRITEUW(0x1002, 0x4e77);
    destroy_blocks(0x1000, 4);
    for (unsigned ccr = 0; ccr < 32; ++ccr) {
        reset(ccr, 1);
        EM_D0 = UINT32_MAX;
        WRITEUW(EM_A7, ccr);
        WRITEUL(EM_A7 + 2, MAGIC_EXIT_EMULATOR_ADDRESS);
        run();
        assert(!trapped && EM_D0 == 0 && EM_A7 == 0x8006);
        assert(status() == (0x2000 | ccr));
        reset(ccr, 0);
        EM_D0 = UINT32_MAX;
        EM_A7 = 0xff000;
        expected_sr = 0x15;
        expected_pc = 0x1002;
        expected_fault = 0xff000;
        run();
        assert(trapped == 2 && cpu_state.usp == 0xff000);
    }
    printf("RTR: %u saved-CCR/stack/target/boundary cases passed (%s)\n", cases, native ? "native" : "interpreter");
    return 0;
}
