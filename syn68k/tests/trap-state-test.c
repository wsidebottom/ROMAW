/*+===================================================================
  File:      trap-state-test.c

  Summary:   Implements trap state test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Original trap/CCR/native-chain fixtures, MC68040UM exception-frame rules. */
#include "syn68k_public.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned trapped, expected_vector, expected_length;
static uint16 expected_sr;
static uint32 expected_pc, instruction_pc;
static uint16 initial_trace;
static int return_from_handler;

static syn68k_addr_t handler(syn68k_addr_t pc, void* argument)
{
    trapped = (unsigned)(uintptr_t)argument;
    assert(trapped == expected_vector && pc == expected_pc);
    if (READUW(EM_A7) != expected_sr)
        fprintf(stderr, "Trap %u at %08x: stacked SR %04x != %04x\n", trapped, instruction_pc, READUW(EM_A7),
                expected_sr);
    assert(READUW(EM_A7) == expected_sr);
    assert(READUL(EM_A7 + 2) == expected_pc);
    assert(READUW(EM_A7 + 6) == ((expected_length == 12 ? 0x2000 : 0) | (trapped * 4)));
    if (expected_length == 12)
        assert(READUL(EM_A7 + 8) == instruction_pc);
    return return_from_handler ? 0x2000 : MAGIC_EXIT_EMULATOR_ADDRESS;
}

static uint16 status(void)
{
    return cpu_state.sr | !!cpu_state.ccc | (!!cpu_state.ccv << 1) | (!cpu_state.ccnz << 2) | (!!cpu_state.ccn << 3) |
           (!!cpu_state.ccx << 4);
}

static void reset(unsigned bank, unsigned ccr)
{
    expected_sr = (bank == 0 ? 0 : bank == 1 ? 0x2000 : 0x3000) | ccr | initial_trace;
    cpu_state.sr = expected_sr & ~31;
    cpu_state.ccc = ccr & 1;
    cpu_state.ccv = ccr & 2;
    cpu_state.ccnz = (~ccr) & 4;
    cpu_state.ccn = ccr & 8;
    cpu_state.ccx = ccr & 16;
    cpu_state.usp = 0x7000;
    cpu_state.isp = 0x8000;
    cpu_state.msp = 0x9000;
    EM_A7 = bank == 0 ? 0x7000 : bank == 1 ? 0x8000 : 0x9000;
    trapped = 0;
}

static int condition(unsigned code, unsigned ccr)
{
    int c = !!(ccr & 1), v = !!(ccr & 2), z = !!(ccr & 4), n = !!(ccr & 8);
    const int conditions[] = {1,  0, !c && !z, c || z, !c,     c,      !z,           z,
                              !v, v, !n,       n,      n == v, n != v, !z && n == v, z || n != v};
    return conditions[code];
}

static void execute(unsigned opcode, unsigned size, unsigned vector, int take, unsigned bank, unsigned ccr,
                    int native_chain)
{
    reset(bank, native_chain ? 0x15 : ccr);
    destroy_blocks(0x1000, 32);
    instruction_pc = 0x1000;
    if (native_chain) {
        WRITEUW(0x1000, 0x203c); // MOVE.L #$7fffffff,D0
        WRITEUL(0x1002, 0x7fffffff);
        WRITEUW(0x1006, 0x5280); // ADDQ.L #1,D0: N=V=1, C=X=Z=0.
        instruction_pc += 8;
        expected_sr = (expected_sr & ~31) | 0x0a;
    }
    WRITEUW(instruction_pc, opcode);
    if (size >= 4)
        WRITEUW(instruction_pc + 2, 0xdead);
    if (size == 6)
        WRITEUW(instruction_pc + 4, 0xbeef);
    expected_pc = instruction_pc + size;
    expected_vector = vector;
    expected_length = vector == 7 ? 12 : 8;
    WRITEUW(expected_pc, 0x4ef9);
    WRITEUL(expected_pc + 2, MAGIC_EXIT_EMULATOR_ADDRESS);
    uint32 original_stack = EM_A7;
    interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
    assert(trapped == (take ? vector : 0));
    assert(status() == (take && !return_from_handler ? (expected_sr & 0x3fff) | 0x2000 : expected_sr));
    if (take && !return_from_handler) {
        assert(EM_A7 == (bank == 2 ? 0x9000 : 0x8000) - expected_length);
        if (!bank)
            assert(cpu_state.usp == original_stack);
    }
    else
        assert(EM_A7 == original_stack);
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
    WRITEUW(0x2000, 0x4e73); // Real guest RTE from the selected frame.
    unsigned count = 0;
    for (unsigned roundtrip = 0; roundtrip < 2; ++roundtrip) {
        return_from_handler = roundtrip;
        for (unsigned bank = 0; bank < 3; ++bank) {
            for (unsigned ccr = 0; ccr < 32; ++ccr) {
                for (unsigned vector = 0; vector < 16; ++vector) {
                    execute(0x4e40 | vector, 2, 32 + vector, 1, bank, ccr, 0);
                    ++count;
                }
                execute(0x4e76, 2, 7, !!(ccr & 2), bank, ccr, 0);
                ++count;
                for (unsigned cc = 0; cc < 16; ++cc)
                    for (unsigned form = 0; form < 3; ++form) {
                        const unsigned low[] = {0xfc, 0xfa, 0xfb};
                        execute(0x5000 | (cc << 8) | low[form], 2 + form * 2, 7, condition(cc, ccr), bank, ccr, 0);
                        ++count;
                    }
            }
            for (unsigned vector = 0; vector < 16; ++vector)
                execute(0x4e40 | vector, 2, 32 + vector, 1, bank, 0x0a, 1);
            execute(0x4e76, 2, 7, 1, bank, 0x0a, 1);
            for (unsigned cc = 0; cc < 16; ++cc)
                for (unsigned form = 0; form < 3; ++form) {
                    const unsigned low[] = {0xfc, 0xfa, 0xfb};
                    execute(0x5000 | (cc << 8) | low[form], 2 + form * 2, 7, condition(cc, 0x0a), bank, 0x0a, 1);
                }
            count += 65;
        }
    }
    return_from_handler = 0;
    initial_trace = 0xc000;
    for (unsigned bank = 0; bank < 3; ++bank) {
        for (unsigned vector = 0; vector < 16; ++vector)
            execute(0x4e40 | vector, 2, 32 + vector, 1, bank, 0x1f, 0);
        execute(0x4e76, 2, 7, 1, bank, 0x1f, 0);
        execute(0x50fc, 2, 7, 1, bank, 0x1f, 0);
        execute(0x50fa, 4, 7, 1, bank, 0x1f, 0);
        execute(0x50fb, 6, 7, 1, bank, 0x1f, 0);
        count += 20;
    }
    printf("%u 68040 TRAP/TRAPV/TRAPcc frame/CCR/stack/native-chain cases passed (%s)\n", count,
           native ? "native" : "interpreter");
    VirtualFree(memory, 0, MEM_RELEASE);
}
