/*+===================================================================
  File:      usp-test.c

  Summary:   Implements usp test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include "syn68k_public.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned trapped;
static uint16 original_sr;

static syn68k_addr_t handler(syn68k_addr_t pc, void* argument)
{
    trapped = (unsigned)(uintptr_t)argument;
    assert(pc == 0x1000 && READUL(EM_A7 + 2) == pc);
    assert(trapped == 8 && READUW(EM_A7) == original_sr);
    assert(READUW(EM_A7 + 6) == 32);
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static uint16 status(void)
{
    return cpu_state.sr | !!cpu_state.ccc | (!!cpu_state.ccv << 1) | (!cpu_state.ccnz << 2) | (!!cpu_state.ccn << 3) |
           (!!cpu_state.ccx << 4);
}

static void reset(unsigned bank, unsigned ccr)
{
    original_sr = (bank == 0 ? 0 : bank == 1 ? 0x2000 : 0x3000) | ccr;
    cpu_state.sr = original_sr & ~31;
    cpu_state.ccc = ccr & 1;
    cpu_state.ccv = ccr & 2;
    cpu_state.ccnz = (~ccr) & 4;
    cpu_state.ccn = ccr & 8;
    cpu_state.ccx = ccr & 16;
    cpu_state.usp = 0x7000;
    cpu_state.isp = 0x8000;
    cpu_state.msp = 0x9000;
    for (unsigned i = 0; i < 7; ++i)
        EM_AREG(i) = 0x11110000 + i;
    EM_A7 = bank == 0 ? 0x7000 : bank == 1 ? 0x8000 : 0x9000;
    trapped = 0;
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
    for (unsigned i = 2; i < 64; ++i)
        trap_install_handler(i, handler, (void*)(uintptr_t)i);
    const uint32 values[] = {0, 1, UINT32_MAX, 0x12345678};
    unsigned count = 0;
    for (unsigned from = 0; from < 2; ++from)
        for (unsigned reg = 0; reg < 8; ++reg)
            for (unsigned bank = 0; bank < 3; ++bank)
                for (unsigned ccr = 0; ccr < 32; ++ccr)
                    for (unsigned value = 0; value < 4; ++value) {
                        reset(bank, ccr);
                        if (from && bank)
                            cpu_state.usp = values[value];
                        else if (!from)
                            EM_AREG(reg) = values[value];
                        uint32 old_address = EM_AREG(reg), old_stack = EM_A7;
                        destroy_blocks(0x1000, 2);
                        WRITEUW(0x1000, 0x4e60 | (from ? 8 : 0) | reg);
                        WRITEUW(0x1002, 0x4ef9);
                        WRITEUL(0x1004, MAGIC_EXIT_EMULATOR_ADDRESS);
                        interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
                        if (!bank) {
                            assert(trapped == 8 && cpu_state.usp == old_stack);
                            assert(status() == (original_sr | 0x2000));
                            if (reg != 7)
                                assert(EM_AREG(reg) == old_address);
                        }
                        else {
                            assert(!trapped && status() == original_sr);
                            assert(EM_AREG(reg) == (from ? values[value] : old_address));
                            assert(cpu_state.usp == values[value]);
                            if (reg != 7)
                                assert(EM_A7 == old_stack);
                        }
                        ++count;
                    }
    reset(1, 31);
    destroy_blocks(0x1000, 16);
    WRITEUW(0x1000, 0x207c);
    WRITEUL(0x1002, 0x11223344);
    WRITEUW(0x1006, 0x4e60);
    WRITEUW(0x1008, 0x4e69);
    WRITEUW(0x100a, 0x4ef9);
    WRITEUL(0x100c, MAGIC_EXIT_EMULATOR_ADDRESS);
    interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
    assert(!trapped && EM_A0 == 0x11223344 && EM_A1 == 0x11223344 && cpu_state.usp == 0x11223344);
    printf("%u MOVE USP privilege/register/CCR/stack cases passed (%s).\n", count, native ? "native" : "interpreter");
    VirtualFree(memory, 0, MEM_RELEASE);
    return 0;
}
