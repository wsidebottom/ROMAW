/*+===================================================================
  File:      control-register-test.c

  Summary:   Implements control register test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Execute original instruction words, not a second copy of the decoder. */
#include "syn68k_public.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int trapped;
static int require_native_movec;
extern syn68k_addr_t __real_trap_direct(uint32 vector, syn68k_addr_t pc, uint32 argument);

syn68k_addr_t __wrap_trap_direct(uint32 vector, syn68k_addr_t pc, uint32 argument)
{
    assert(!require_native_movec || vector != 4 || (pc != 0x1000 && pc != 0x1004));
    assert(!require_native_movec || vector != 11 || pc != 0x1000 || (READUW(pc) & 0xff00) != 0xf500);
    return __real_trap_direct(vector, pc, argument);
}

static syn68k_addr_t unexpected_trap(syn68k_addr_t address, void* argument)
{
    trapped = (int)(uintptr_t)argument;
    assert(address == 0x1000 && READUL(EM_A7 + 2) == address);
    assert(READUW(EM_A7 + 6) == trapped * 4);
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static void control_masks_and_privilege(void)
{
    static const uint16 selectors[] = {0, 1, 2, 0x800, 0x803, 0x804};
    static const uint32 values[] = {0, 1, UINT32_MAX, 0x12345678};
    for (unsigned i = 0; i < sizeof selectors / sizeof *selectors; ++i)
        for (unsigned j = 0; j < sizeof values / sizeof *values; ++j) {
            uint16 selector = selectors[i];
            uint32 mask = selector < 2 ? 7 : selector == 2 ? 0x80008000u : UINT32_MAX;
            destroy_blocks(0x1000, 32);
            WRITEUW(0x1000, 0x4e7b); /* movec d0,control */
            WRITEUW(0x1002, selector);
            WRITEUW(0x1004, 0x4e7a); /* movec control,d1 */
            WRITEUW(0x1006, 0x1000 | selector);
            WRITEUW(0x1008, 0x40c2); /* move.w SR,D2 */
            WRITEUW(0x100a, 0x4e75);
            /* Keep the stack bank being written inactive. */
            cpu_state.sr = selector == 0x804 ? 0x3000 : 0x2000;
            cpu_state.ccc = cpu_state.ccn = cpu_state.ccv = cpu_state.ccx = 1;
            cpu_state.ccnz = 0;
            EM_A7 = 0xff000;
            EM_D0 = values[j];
            EM_D1 = ~values[j];
            trapped = 0;
            CALL_EMULATOR(0x1000);
            assert(!trapped && EM_D0 == values[j] && EM_D1 == (values[j] & mask));
            assert((EM_D2 & 31) == 31);
        }
    /* Every selector and direction is privileged, including selectors that
     are illegal on a 68040. Neither reads nor writes may change registers. */
    for (unsigned writing = 0; writing < 2; ++writing)
        for (unsigned selector = 0; selector < 4096; ++selector) {
            destroy_blocks(0x1000, 32);
            WRITEUW(0x1000, 0x4e7a | writing);
            WRITEUW(0x1002, selector);
            WRITEUW(0x1004, 0x4e75);
            cpu_state.sr = 0;
            cpu_state.isp = 0xfe000;
            EM_A7 = 0xff000;
            EM_D0 = 0x2468ace0;
            uint32 sfc = cpu_state.sfc, dfc = cpu_state.dfc;
            uint32 cacr = cpu_state.cacr, vbr = cpu_state.vbr;
            trapped = 0;
            CALL_EMULATOR(0x1000);
            assert(trapped == 8 && EM_D0 == 0x2468ace0);
            assert(cpu_state.sfc == sfc && cpu_state.dfc == dfc && cpu_state.cacr == cacr && cpu_state.vbr == vbr);
        }
    const unsigned invalid[] = {3, 4, 5, 6, 7, 0x802, 0x805, 0x806, 0x807, 0xfff}; /* No flat MMU or CAAR. */
    for (unsigned writing = 0; writing < 2; ++writing)
        for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; ++i) {
            destroy_blocks(0x1000, 32);
            WRITEUW(0x1000, 0x4e7a | writing);
            WRITEUW(0x1002, invalid[i]);
            cpu_state.sr = 0x2000;
            EM_A7 = 0xff000;
            EM_D0 = 0x13579bdf;
            trapped = 0;
            CALL_EMULATOR(0x1000);
            assert(trapped == 4 && EM_D0 == 0x13579bdf);
        }
}

static void flat_mmu_privilege(void)
{
    const unsigned families[] = {0xf500, 0xf508, 0xf510, 0xf518, 0xf548, 0xf568};
    for (unsigned i = 0; i < sizeof families / sizeof *families; ++i)
        for (unsigned reg = 0; reg < (i == 2 || i == 3 ? 1u : 8u); ++reg)
            for (unsigned supervisor = 0; supervisor < 2; ++supervisor) {
                destroy_blocks(0x1000, 32);
                WRITEUW(0x1000, families[i] | reg);
                cpu_state.sr = supervisor ? 0x2000 : 0;
                cpu_state.isp = 0xfe000;
                EM_A7 = 0xff000;
                EM_A0 = 0xdeadbeef;
                cpu_state.dfc = 5;
                trapped = 0;
                CALL_EMULATOR(0x1000);
                assert(trapped == (supervisor ? 11 : 8));
                assert(EM_A0 == 0xdeadbeef && cpu_state.dfc == 5);
            }
}

int main(int argc, char** argv)
{
    const uint32 size = 1024 * 1024;
    void* memory = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(size, 0, 0);
    uint32 vectors[64] = {0};
    int native = argc > 1 ? atoi(argv[1]) : 0;
    int aot_run = argc > 2 ? atoi(argv[2]) : 0;
    initialize_68k_emulator(NULL, native, vectors, 0);
    native = syn68k_native_code_enabled();
    require_native_movec = native;
    uint64 hits_before = syn68k_aot_hit_count(), emits_before = syn68k_native_compile_count();
    uint32 initial_vbr = cpu_state.vbr;
    trap_install_handler(4, unexpected_trap, (void*)(uintptr_t)4);
    trap_install_handler(8, unexpected_trap, (void*)(uintptr_t)8);
    trap_install_handler(11, unexpected_trap, (void*)(uintptr_t)11);
    for (unsigned reg = 0; reg < 15; ++reg)
        for (unsigned value = 0; value < 4; ++value) {
            const uint32 values[] = {0x2000, 0x3000, 0x4000, 0x5000};
            // Keep both vector tables valid so an unexpected illegal instruction
            // reaches the fixture callback instead of jumping into unmapped RAM.
            memcpy(SYN68K_TO_US(values[value]), vectors, sizeof vectors);
            unsigned destination = (reg + 1) % 15;
            const uint32 pc = 0x1000;
            destroy_blocks(pc, 10);
            // MOVEC Rn,VBR; MOVEC VBR,Rm; RTS.
            WRITEUW(pc, 0x4e7b);
            WRITEUW(pc + 2, reg << 12 | 0x801);
            WRITEUW(pc + 4, 0x4e7a);
            WRITEUW(pc + 6, destination << 12 | 0x801);
            WRITEUW(pc + 8, 0x4e75);
            EM_A7 = size - 16;
            cpu_state.vbr = initial_vbr;
            cpu_state.regs[reg].ul.n = values[value];
            cpu_state.regs[destination].ul.n = ~values[value];
            trapped = 0;
            CALL_EMULATOR(pc);
            assert(!trapped);
            assert(cpu_state.vbr == values[value]);
            assert(cpu_state.regs[reg].ul.n == values[value]);
            assert(cpu_state.regs[destination].ul.n == values[value]);
        }
    cpu_state.vbr = initial_vbr;
    control_masks_and_privilege();
    flat_mmu_privilege();
    uint64 hits = syn68k_aot_hit_count() - hits_before;
    uint64 emits = syn68k_native_compile_count() - emits_before;
    if (aot_run == 1)
        assert(native && emits >= 4);
    else if (aot_run == 2)
        assert(native && hits >= 4 && emits == 0);
    printf("MOVEC directions, masks, privilege and illegal selectors passed (%s; emitted=%llu hits=%llu).\n",
           native ? "native" : "interpreter", (unsigned long long)emits, (unsigned long long)hits);
    VirtualFree(memory, 0, MEM_RELEASE);
    return 0;
}
