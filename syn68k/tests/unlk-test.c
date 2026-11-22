/*+===================================================================
  File:      unlk-test.c

  Summary:   Implements unlk test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Original UNLK frame, condition, cached-offset and boundary fixtures. */
#include "syn68k_public.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned cases, trapped;
static uint32 expected_pc = 0x1000, expected_fault, expected_stack;
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
        fprintf(stderr, "UNLK case %u: vector %u PC %08x expected %08x\n", cases, trapped, pc, expected_pc);
    assert(trapped == 2 && pc == expected_pc);
    if (READUW(EM_A7) != expected_sr)
        fprintf(stderr, "UNLK case %u: saved SR %04x expected %04x\n", cases, READUW(EM_A7), expected_sr);
    assert(READUW(EM_A7) == expected_sr);
    assert(READUL(EM_A7 + 2) == expected_pc);
    assert(READUW(EM_A7 + 6) == 0x7008);
    assert(READUL(EM_A7 + 20) == expected_fault);
    assert(cpu_state.usp == expected_stack);
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static void instruction(unsigned reg, int producer)
{
    unsigned pc = 0x1000;
    if (producer) {
        WRITEUW(pc, 0x5888 | reg); // ADDQ.L #4,An.
        WRITEUW(pc + 2, 0x5280);   // ADDQ.L #1,D0.
        pc += 4;
    }
    WRITEUW(pc, 0x4e58 | reg);
    WRITEUW(pc + 2, 0x4ef9);
    WRITEUL(pc + 4, MAGIC_EXIT_EMULATOR_ADDRESS);
    expected_pc = pc;
    destroy_blocks(0x1000, 12);
}

static void run(void)
{
    interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
    ++cases;
}

static void fault_flag_liveness(void)
{
    const uint32 inputs[] = {UINT32_MAX, 0x7fffffff, 0, 0x80000000};
    const unsigned flags[] = {0x15, 0x0a, 0, 8};
    for (unsigned reg = 0; reg < 8; ++reg)
        for (unsigned offset = 0; offset < 2; ++offset)
            for (unsigned killer = 0; killer < 2; ++killer) {
                unsigned pc = 0x1000;
                if (offset) {
                    WRITEUW(pc, 0x5888 | reg); // ADDQ.L #4,An.
                    pc += 2;
                }
                WRITEUW(pc, 0x5280); // ADDQ.L #1,D0 produces the fault's CCR.
                expected_pc = pc + 2;
                WRITEUW(pc + 2, 0x4e58 | reg);
                // These overwrite flags only if UNLK succeeds.
                WRITEUW(pc + 4, killer ? 0x5281 : 0x7200);
                WRITEUW(pc + 6, 0x4ef9);
                WRITEUL(pc + 8, MAGIC_EXIT_EMULATOR_ADDRESS);
                destroy_blocks(0x1000, 16);
                for (unsigned bank = 0; bank < 3; ++bank)
                    for (unsigned ccr = 0; ccr < 32; ++ccr)
                        for (unsigned value = 0; value < 4; ++value) {
                            reset(ccr, bank);
                            cpu_state.regs[8 + reg].ul.n = 0x7001 - offset * 4;
                            EM_D0 = inputs[value];
                            WRITEUL(0x7001, 0x12345678);
                            run();
                            assert(!trapped && EM_D0 == inputs[value] + 1);
                            assert(EM_D1 == (killer ? 0x12345679 : 0));
                            assert(status() == (cpu_state.sr | (killer ? 0 : 4 | (flags[value] & 16))));
                            // Preserve the existing A7 alias policy in both engines.
                            assert(EM_A7 == (reg == 7 ? 0x1234567c : 0x7005));
                            if (reg != 7)
                                assert(cpu_state.regs[8 + reg].ul.n == 0x12345678);
                        }
                for (unsigned ccr = 0; ccr < 32; ++ccr)
                    for (unsigned available = 0; available < 4; ++available)
                        for (unsigned value = 0; value < 4; ++value) {
                            reset(ccr, 0);
                            expected_stack = 0xff000 - available;
                            expected_fault = 0xff000;
                            expected_sr = flags[value];
                            cpu_state.regs[8 + reg].ul.n = expected_stack - offset * 4;
                            EM_D0 = inputs[value];
                            run();
                            assert(trapped == 2 && EM_D0 == inputs[value] + 1);
                            assert(EM_D1 == 0x12345678);
                            if (reg != 7)
                                assert(cpu_state.regs[8 + reg].ul.n == expected_stack);
                        }
            }
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
    const uint32 saved[] = {0, UINT32_MAX, 0x6000, 0x12345678};
    for (unsigned reg = 0; reg < 8; ++reg) {
        instruction(reg, 0);
        for (unsigned bank = 0; bank < 3; ++bank)
            for (unsigned ccr = 0; ccr < 32; ++ccr)
                for (unsigned offset = 0; offset < 4; ++offset)
                    for (unsigned value = 0; value < 4; ++value) {
                        reset(ccr, bank);
                        uint32 frame = 0x10000 + offset;
                        cpu_state.regs[8 + reg].ul.n = frame;
                        WRITEUL(frame, saved[value]);
                        run();
                        assert(!trapped && status() == expected_sr);
                        // A7 alias checks preserve legacy semantics, not a silicon oracle.
                        if (EM_A7 != (reg == 7 ? saved[value] + 4 : frame + 4))
                            fprintf(stderr,
                                    "case=%u reg=%u bank=%u ccr=%u value=%08x "
                                    "frame=%08x A7=%08x An=%08x\n",
                                    cases, reg, bank, ccr, saved[value], frame, EM_A7, cpu_state.regs[8 + reg].ul.n);
                        assert(EM_A7 == (reg == 7 ? saved[value] + 4 : frame + 4));
                        if (reg != 7)
                            assert(cpu_state.regs[8 + reg].ul.n == saved[value]);
                        for (unsigned i = 0; i < 15; ++i)
                            if (i != 8 + reg)
                                assert(cpu_state.regs[i].ul.n == 0x12345678u * i);
                    }
    }
    DWORD previous;
    assert(VirtualProtect((uint8*)memory + 0xff000, 0x1000, PAGE_NOACCESS, &previous));
    syn68k_set_memory_window(0xff000, 0, 0);
    for (unsigned reg = 0; reg < 8; ++reg) {
        instruction(reg, 0);
        for (unsigned ccr = 0; ccr < 32; ++ccr)
            for (unsigned available = 0; available < 4; ++available) {
                reset(ccr, 0);
                uint32 frame = 0xff000 - available;
                cpu_state.regs[8 + reg].ul.n = frame;
                for (unsigned i = 0; i < available; ++i)
                    WRITEUB(frame + i, 0xff);
                expected_stack = frame;
                expected_fault = 0xff000;
                run();
                assert(trapped == 2);
                if (reg != 7)
                    assert(cpu_state.regs[8 + reg].ul.n == frame);
            }
        for (unsigned offset = 0; offset < 4; ++offset) {
            reset(offset, 0);
            expected_fault = expected_stack = UINT32_MAX - offset;
            cpu_state.regs[8 + reg].ul.n = expected_stack;
            run();
            assert(trapped == 2);
        }
    }
    for (unsigned reg = 0; reg < 7; ++reg) {
        instruction(reg, 1);
        for (unsigned bank = 0; bank < 3; ++bank)
            for (unsigned ccr = 0; ccr < 32; ++ccr) {
                reset(ccr, bank);
                cpu_state.regs[8 + reg].ul.n = 0x7001;
                EM_D0 = UINT32_MAX;
                WRITEUL(0x7005, 0x12345678);
                run();
                assert(!trapped && EM_A7 == 0x7009 && EM_D0 == 0);
                assert(cpu_state.regs[8 + reg].ul.n == 0x12345678);
                assert(status() == (cpu_state.sr | 0x15));
            }
        for (unsigned available = 0; available < 4; ++available) {
            reset(31, 0);
            EM_D0 = UINT32_MAX;
            expected_sr = 0x15;
            expected_stack = 0xff000 - available;
            expected_fault = 0xff000;
            cpu_state.regs[8 + reg].ul.n = expected_stack - 4;
            run();
            assert(trapped == 2 && EM_D0 == 0);
        }
    }
    // Resize the window after the same instruction has acquired native code.
    for (unsigned reg = 0; reg < 7; ++reg) {
        instruction(reg, 0);
        for (unsigned repeat = 0; repeat < 64; ++repeat) {
            reset(repeat & 31, repeat % 3);
            cpu_state.regs[8 + reg].ul.n = 0xfeffc;
            WRITEUL(0xfeffc, 0x12345678);
            run();
            assert(!trapped && EM_A7 == 0xff000);
            assert(cpu_state.regs[8 + reg].ul.n == 0x12345678);
        }
        syn68k_set_memory_window(0xfeffe, 0, 0);
        reset(31, 0);
        expected_fault = 0xfeffe;
        expected_stack = cpu_state.regs[8 + reg].ul.n = 0xfeffc;
        run();
        assert(trapped == 2);
        syn68k_set_memory_window(0xff000, 0, 0);
    }
    fault_flag_liveness();
    printf("UNLK: %u frame/CCR/alias/offset/boundary cases passed (%s)\n", cases, native ? "native" : "interpreter");
    return 0;
}
