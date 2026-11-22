/*+===================================================================
  File:      sr-test.c

  Summary:   Implements sr test support for syn68k 68K translation and runtime support.

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
static uint16 initial_sr;

static syn68k_addr_t handler(syn68k_addr_t pc, void* argument)
{
    trapped = (unsigned)(uintptr_t)argument;
    assert(pc == 0x1000 && READUL(EM_A7 + 2) == pc);
    assert(trapped == 8 && READUW(EM_A7 + 6) == 32);
    assert(READUW(EM_A7) == initial_sr);
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static void initialize_state(uint16 sr)
{
    initial_sr = sr;
    cpu_state.sr = sr & ~31;
    cpu_state.ccc = sr & 1;
    cpu_state.ccv = sr & 2;
    cpu_state.ccnz = (~sr) & 4;
    cpu_state.ccn = sr & 8;
    cpu_state.ccx = sr & 16;
    cpu_state.usp = 0x7000;
    cpu_state.isp = 0x8000;
    cpu_state.msp = 0x9000;
    EM_A7 = (sr & 0x2000) ? (sr & 0x1000) ? 0x9000 : 0x8000 : 0x7000;
    trapped = 0;
}

static uint16 status(void)
{
    return cpu_state.sr | !!cpu_state.ccc | (!!cpu_state.ccv << 1) | (!cpu_state.ccnz << 2) | (!!cpu_state.ccn << 3) |
           (!!cpu_state.ccx << 4);
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
    WRITEUW(0x1004, 0x4ef9);
    WRITEUL(0x1006, MAGIC_EXIT_EMULATOR_ADDRESS);
    const uint16 opcodes[] = {0x027c, 0x007c, 0x0a7c};
    unsigned cases = 0;
    for (unsigned operation = 0; operation < 3; ++operation)
        for (unsigned sample = 0; sample < 36; ++sample) {
            uint16 immediate = sample < 16    ? 1u << sample
                               : sample < 32  ? ~(1u << (sample - 16))
                               : sample == 32 ? 0
                               : sample == 33 ? 0xffff
                               : sample == 34 ? 0x2000
                                              : 0x1000;
            destroy_blocks(0x1000, 4);
            WRITEUW(0x1000, opcodes[operation]);
            WRITEUW(0x1002, immediate);
            for (unsigned ccr = 0; ccr < 32; ++ccr)
                for (unsigned bank = 0; bank < 3; ++bank) {
                    uint16 old = (bank == 0 ? 0 : bank == 1 ? 0x2500 : 0x3500) | ccr;
                    initialize_state(old);
                    interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
                    if (!bank) {
                        assert(trapped == 8 && cpu_state.usp == 0x7000);
                        assert(EM_A7 == 0x7ff8 && status() == (old | 0x2000));
                    }
                    else {
                        uint16 expected = (operation == 0   ? old & immediate
                                           : operation == 1 ? old | immediate
                                                            : old ^ immediate) &
                                          0xf71f;
                        assert(!trapped && status() == expected);
                        uint32 stack = !(expected & 0x2000) ? 0x7000 : expected & 0x1000 ? 0x9000 : 0x8000;
                        assert(EM_A7 == stack);
                    }
                    ++cases;
                }
        }
    printf("%u 68040 SR immediate privilege/CCR/stack cases passed (%s).\n", cases, native ? "native" : "interpreter");
    VirtualFree(memory, 0, MEM_RELEASE);
    return 0;
}
