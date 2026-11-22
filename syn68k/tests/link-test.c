/*+===================================================================
  File:      link-test.c

  Summary:   Implements link test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Original LINK frame-write, CCR liveness and native-offset fixtures. */
#include "syn68k_public.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned cases, trapped;
static uint32 expected_pc, expected_fault, expected_stack;
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
    for (unsigned i = 0; i < 15; ++i)
        cpu_state.regs[i].ul.n = 0x12345678u * i;
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
        fprintf(stderr, "LINK case %u vector %u PC %08x expected %08x\n", cases, trapped, pc, expected_pc);
    assert(trapped == 2 && pc == expected_pc);
    assert(READUW(EM_A7) == expected_sr);
    assert(READUL(EM_A7 + 2) == expected_pc);
    assert(READUW(EM_A7 + 6) == 0x7008);
    assert(READUL(EM_A7 + 20) == expected_fault);
    assert(cpu_state.usp == expected_stack);
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static void instruction(unsigned reg, unsigned wide, uint32 displacement, unsigned chain)
{
    unsigned pc = 0x1000;
    if (chain) {
        WRITEUW(pc, 0x588f);     // ADDQ.L #4,A7 leaves a native cached offset.
        WRITEUW(pc + 2, 0x5280); // ADDQ.L #1,D0 produces live arithmetic CCR.
        pc += 4;
    }
    expected_pc = pc;
    WRITEUW(pc, (wide ? 0x4808 : 0x4e50) | reg);
    if (wide)
        WRITEUL(pc + 2, displacement);
    else
        WRITEUW(pc + 2, displacement);
    pc += wide ? 6 : 4;
    if (chain) {
        WRITEUW(pc, chain == 1 ? 0x7200 : 0x5281);
        pc += 2;
    }
    WRITEUW(pc, 0x4ef9);
    WRITEUL(pc + 2, MAGIC_EXIT_EMULATOR_ADDRESS);
    destroy_blocks(0x1000, 24);
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

    const uint32 offsets[] = {0, 1, UINT32_MAX, 0x7fff, 0x8000, 0xffff8000, 0x80000000, 0x7fffffff};
    for (unsigned wide = 0; wide < 2; ++wide)
        for (unsigned reg = 0; reg < 8; ++reg)
            for (unsigned value = 0; value < sizeof offsets / sizeof *offsets; ++value) {
                instruction(reg, wide, offsets[value], 0);
                uint32 displacement = wide ? offsets[value] : (int16)offsets[value];
                for (unsigned bank = 0; bank < 3; ++bank)
                    for (unsigned ccr = 0; ccr < 32; ++ccr)
                        for (unsigned alignment = 0; alignment < 4; ++alignment) {
                            reset(ccr, bank);
                            uint32 stack = EM_A7 + alignment, frame = stack - 4;
                            EM_A7 = stack;
                            uint32 saved = reg == 7 ? frame : cpu_state.regs[8 + reg].ul.n;
                            run();
                            assert(!trapped && status() == expected_sr);
                            assert(READUL(frame) == saved);
                            assert(EM_A7 == frame + displacement);
                            if (reg != 7)
                                assert(cpu_state.regs[8 + reg].ul.n == frame);
                            for (unsigned i = 0; i < 15; ++i)
                                if (i != 8 + reg)
                                    assert(cpu_state.regs[i].ul.n == 0x12345678u * i);
                        }
            }

    for (unsigned wide = 0; wide < 2; ++wide)
        for (unsigned reg = 0; reg < 7; ++reg)
            for (unsigned chain = 1; chain < 3; ++chain) {
                instruction(reg, wide, 0xfffffff0, chain);
                for (unsigned bank = 0; bank < 3; ++bank)
                    for (unsigned ccr = 0; ccr < 32; ++ccr) {
                        reset(ccr, bank);
                        uint32 stack = EM_A7;
                        EM_D0 = chain == 1 ? UINT32_MAX : 0x7fffffff;
                        run();
                        assert(!trapped && EM_D0 == (chain == 1 ? 0 : 0x80000000));
                        assert(READUL(stack) == 0x12345678u * (8 + reg));
                        assert(cpu_state.regs[8 + reg].ul.n == stack);
                        assert(EM_A7 == stack + (wide ? 0xfffffff0 : (uint32)-16));
                        assert(EM_D1 == (chain == 1 ? 0 : 0x12345679));
                        assert(status() == (cpu_state.sr | (chain == 1 ? 0x14 : 0)));
                    }
            }

    // Keep the tail mapped for the before-fix regression, then protect it.
    for (unsigned protected_tail = 0; protected_tail < 2; ++protected_tail) {
        if (protected_tail) {
            DWORD previous;
            assert(VirtualProtect((uint8*)memory + 0xff000, 0x1000, PAGE_NOACCESS, &previous));
        }
        syn68k_set_memory_window(0xff000, 0, 0);
        for (unsigned wide = 0; wide < 2; ++wide)
            for (unsigned reg = 0; reg < 7; ++reg)
                for (unsigned chain = 0; chain < 3; ++chain) {
                    instruction(reg, wide, 0xfffffff0, chain);
                    for (unsigned ccr = 0; ccr < 32; ++ccr)
                        for (unsigned available = 0; available < 4; ++available) {
                            reset(ccr, 0);
                            EM_D0 = chain == 2 ? 0x7fffffff : UINT32_MAX;
                            expected_stack = 0xff000 - available;
                            expected_fault = 0xff000;
                            EM_A7 = expected_stack + 4 - (chain ? 4 : 0);
                            if (chain)
                                expected_sr = chain == 1 ? 0x15 : 0x0a;
                            for (unsigned i = 0; i < available; ++i)
                                WRITEUB(expected_stack + i, 0x5a);
                            run();
                            assert(trapped == 2);
                            assert(cpu_state.regs[8 + reg].ul.n == 0x12345678u * (8 + reg));
                            assert(EM_D1 == 0x12345678);
                            for (unsigned i = 0; i < available; ++i)
                                assert(READUB(expected_stack + i) == 0x5a);
                        }
                }
    }
    for (unsigned wide = 0; wide < 2; ++wide)
        for (unsigned reg = 0; reg < 7; ++reg) {
            instruction(reg, wide, 0, 0);
            for (unsigned repeat = 0; repeat < 64; ++repeat) {
                reset(repeat & 31, 0);
                EM_A7 = 0xff000;
                run();
                assert(!trapped && EM_A7 == 0xfeffc);
            }
            syn68k_set_memory_window(0xfeffe, 0, 0);
            reset(31, 0);
            expected_stack = 0xfeffc;
            expected_fault = 0xfeffe;
            EM_A7 = 0xff000;
            run();
            assert(trapped == 2);
            syn68k_set_memory_window(0xff000, 0, 0);
        }
    for (unsigned wide = 0; wide < 2; ++wide)
        for (unsigned reg = 0; reg < 7; ++reg) {
            instruction(reg, wide, 0, 0);
            for (unsigned stack = 0; stack < 4; ++stack) {
                reset(31, 0);
                EM_A7 = stack;
                expected_fault = expected_stack = stack - 4;
                run();
                assert(trapped == 2);
            }
        }
    /* Extensions are fetched at execution even when the block is cached. */
    for (unsigned wide = 0; wide < 2; ++wide)
        for (unsigned reg = 0; reg < 7; ++reg) {
            instruction(reg, wide, 0, 0);
            reset(31, 0);
            run();
            for (unsigned change = 0; change < 64; ++change) {
                uint32 displacement = (uint32) - (int32)(2 * change);
                if (wide)
                    WRITEUL(0x1002, displacement);
                else
                    WRITEUW(0x1002, displacement);
                reset(change & 31, 0);
                uint32 stack = EM_A7;
                run();
                assert(!trapped && EM_A7 == stack - 4 + displacement);
                assert(status() == expected_sr);
            }
        }
    /* A missing displacement must fault before changing SP or An. Include
     byte-truncated windows and an actually protected following host page. */
    for (unsigned guarded = 0; guarded < 2; ++guarded)
        for (unsigned wide = 0; wide < 2; ++wide)
            for (unsigned reg = 0; reg < 8; ++reg)
                for (unsigned available = 0; available < (wide ? 4 : 2); available += guarded ? 2 : 1) {
                    uint32 limit = guarded ? 0xff000 : 0xfef02 + available;
                    uint32 pc = guarded ? limit - available - 2 : 0xfef00;
                    WRITEUW(pc, (wide ? 0x4808 : 0x4e50) | reg);
                    for (unsigned i = 0; i < available; ++i)
                        WRITEUB(pc + 2 + i, 0x5a);
                    syn68k_set_memory_window(limit, 0, 0);
                    destroy_blocks(pc, available + 2);
                    for (unsigned bank = 0; bank < 3; ++bank)
                        for (unsigned ccr = 0; ccr < 32; ++ccr) {
                            reset(ccr, bank);
                            expected_pc = pc;
                            expected_stack = bank == 0 ? EM_A7 : cpu_state.usp;
                            expected_fault = limit;
                            interpret_code(hash_lookup_code_and_create_if_needed(pc));
                            ++cases;
                            assert(trapped == 2);
                            for (unsigned i = 0; i < 15; ++i)
                                assert(cpu_state.regs[i].ul.n == 0x12345678u * i);
                        }
                    syn68k_set_memory_window(0xff000, 0, 0);
                }
    printf("LINK: %u frame/CCR/alias/offset/boundary/live-extension cases passed (%s)\n", cases,
           native ? "native" : "interpreter");
    return 0;
}
