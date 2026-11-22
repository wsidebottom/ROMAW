/*+===================================================================
  File:      control-transfer-test.c

  Summary:   Implements control transfer test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Original JMP/JSR live-EA, stack-write and condition fixtures. */
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

static void instruction(unsigned call, unsigned ea, uint32 extension, unsigned length)
{
    WRITEUW(0x1000, (call ? 0x4e80 : 0x4ec0) | ea);
    if (length == 4)
        WRITEUW(0x1002, extension);
    else if (length == 6)
        WRITEUL(0x1002, extension);
    uint32 next = 0x1000 + length;
    if (call) {
        WRITEUW(next, 0x2a7c); // MOVEA.L #return marker,A5.
        WRITEUL(next + 2, 0xfeed);
        next += 6;
    }
    WRITEUW(next, 0x4ef9);
    WRITEUL(next + 2, MAGIC_EXIT_EMULATOR_ADDRESS);
    destroy_blocks(0x1000, 32);
}

static void target(uint32 pc, unsigned call, uint32 marker)
{
    WRITEUW(pc, 0x2c7c); // MOVEA.L #target marker,A6.
    WRITEUL(pc + 2, marker);
    WRITEUW(pc + 6, call ? 0x4e75 : 0x4ef9);
    if (!call)
        WRITEUL(pc + 8, MAGIC_EXIT_EMULATOR_ADDRESS);
    destroy_blocks(pc, 12);
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
    // Warm each absolute target, then edit without invalidating its source block.
    for (unsigned call = 0; call < 2; ++call) {
        instruction(call, 0x39, 0x3000, 6);
        target(0x3000, call, 1);
        target(0x3100, call, 2);
        reset(31, 0);
        run();
        assert(EM_A6 == 1);
        WRITEUL(0x1002, 0x3100);
        reset(31, 0);
        run();
        assert(EM_A6 == 2 && status() == expected_sr);
    }

    // Cached target extensions remain live in all six extension-bearing modes.
    const unsigned live[] = {0x28, 0x30, 0x38, 0x39, 0x3a, 0x3b};
    for (unsigned call = 0; call < 2; ++call)
        for (unsigned i = 0; i < sizeof live / sizeof live[0]; ++i) {
            unsigned ea = live[i];
            unsigned extension = ea == 0x28 ? 0 : ea == 0x30 || ea == 0x3b ? 0x3a04 : ea == 0x3a ? 0x1ffe : 0x3000;
            instruction(call, ea, extension, ea == 0x39 ? 6 : 4);
            target(0x3000, call, 1);
            target(0x3010, call, 2);
            for (unsigned edit = 0; edit < 2; ++edit) {
                if (ea == 0x39)
                    WRITEUL(0x1002, extension + 16 * edit);
                else
                    WRITEUW(0x1002, extension + 16 * edit);
                reset(31, 0);
                EM_A0 = ea == 0x30 ? 0x2ff4 : 0x3000;
                EM_D3 = ea == 0x3b ? 0xffd : 4;
                run();
                assert(!trapped && EM_A6 == edit + 1 && status() == expected_sr);
            }
        }

    // Native cached source offsets and live arithmetic CCR precede JSR/JMP.
    for (unsigned call = 0; call < 2; ++call) {
        instruction(call, 0x28, 16, 4);
        target(0x3000, call, 1);
        WRITEUW(0x0ffc, 0x5888); // ADDQ.L #4,A0.
        WRITEUW(0x0ffe, 0x5280); // ADDQ.L #1,D0.
        destroy_blocks(0x0ffc, 32);
        reset(0, 0);
        EM_A0 = 0x2fec;
        EM_D0 = 0xffffffff;
        interpret_code(hash_lookup_code_and_create_if_needed(0x0ffc));
        ++cases;
        assert(!trapped && EM_A6 == 1 && (status() & 31) == 0x15);
    }

    for (unsigned call = 0; call < 2; ++call)
        for (unsigned ea = 0; ea < 64; ++ea) {
            unsigned mode = ea >> 3, reg = ea & 7;
            int legal = mode == 2 || mode == 5 || mode == 6 || (mode == 7 && reg < 4);
            uint32 extension = mode == 5              ? 16
                               : mode == 6            ? 0x3a04
                               : reg == 0 || reg == 1 ? 0x3000
                               : reg == 2             ? 0x1ffe
                                                      : 0x3a04;
            unsigned length = mode == 2 || !legal ? 2 : mode == 7 && reg == 1 ? 6 : 4;
            instruction(call, ea, extension, length);
            for (unsigned bank = 0; bank < (legal ? 3 : 1); ++bank)
                for (unsigned alignment = 0; alignment < (legal ? 4 : 1); ++alignment)
                    for (unsigned ccr = 0; ccr < 32; ++ccr) {
                        // Instruction targets must be even; unaligned data stacks work.
                        if (mode != 7 && reg == 7 && (alignment & 1))
                            continue;
                        reset(ccr, bank);
                        for (unsigned i = 0; i < 7; ++i)
                            EM_AREG(i) = 0x4000 + i * 256;
                        EM_A7 += alignment;
                        uint32 stack = EM_A7, address = 0x3000;
                        EM_D3 = mode == 7 && reg == 3 ? 0xffd : 4;
                        if (mode == 2 || mode == 5 || mode == 6) {
                            uint32 offset = mode == 2 ? 0 : mode == 5 ? 16 : 12;
                            if (reg == 7)
                                address = stack + offset;
                            else
                                EM_AREG(reg) = address - offset;
                        }
                        uint32 saved[16];
                        for (unsigned i = 0; i < 16; ++i)
                            saved[i] = cpu_state.regs[i].ul.n;
                        if (legal)
                            target(address, call, 0xabc);
                        else {
                            expected_vector = 4;
                            expected_stack = stack;
                        }
                        run();
                        if (legal) {
                            assert(!trapped && status() == expected_sr);
                            assert(EM_A6 == 0xabc && EM_A7 == stack);
                            if (call) {
                                assert(EM_A5 == 0xfeed);
                                assert(READUL(stack - 4) == 0x1000 + length);
                            }
                            for (unsigned i = 0; i < 16; ++i)
                                if (i != 14 && !(call && i == 13))
                                    assert(cpu_state.regs[i].ul.n == saved[i]);
                        }
                        else
                            assert(trapped == 4);
                    }
        }

    // Full-index indirect targets are resolved before pushing the return PC.
    for (unsigned call = 0; call < 2; ++call) {
        instruction(call, 0x30, 0, 8);
        WRITEUW(0x1002, 0x3b22);
        WRITEUW(0x1004, 16);
        WRITEUW(0x1006, 32);
        destroy_blocks(0x1000, 32);
        target(0x3000, call, 0xabc);
        for (unsigned ccr = 0; ccr < 32; ++ccr) {
            reset(ccr, 0);
            EM_A0 = 0x2000;
            EM_D3 = 4;
            WRITEUL(0x2018, 0x2fe0);
            run();
            assert(!trapped && EM_A6 == 0xabc && status() == expected_sr);
            if (call)
                assert(READUL(EM_A7 - 4) == 0x1008);
        }
    }

    DWORD old;
    assert(VirtualProtect((char*)memory + 0xff000, 0x1000, PAGE_NOACCESS, &old));
    syn68k_set_memory_window(0xff000, 0, 0);
    for (unsigned available = 0; available < 4; ++available)
        for (unsigned ccr = 0; ccr < 32; ++ccr) {
            instruction(1, 0x10, 0, 2);
            reset(ccr, 0);
            EM_A0 = 0x3000;
            EM_A7 = 0xff004 - available;
            expected_stack = EM_A7 - 4;
            expected_fault = 0xff000;
            for (unsigned i = 0; i < available; ++i)
                WRITEUB(expected_stack + i, 0xa5);
            run();
            assert(trapped == 2);
            for (unsigned i = 0; i < available; ++i)
                assert(READUB(expected_stack + i) == 0xa5);
        }

    const unsigned extended[] = {0x28, 0x30, 0x38, 0x39, 0x3a, 0x3b};
    for (unsigned call = 0; call < 2; ++call)
        for (unsigned i = 0; i < sizeof extended / sizeof extended[0]; ++i)
            for (unsigned available = 0; available < (extended[i] == 0x39 ? 4 : 2); ++available) {
                instruction(call, extended[i], 0, extended[i] == 0x39 ? 6 : 4);
                syn68k_set_memory_window(0x1002 + available, 0, 0);
                for (unsigned ccr = 0; ccr < 32; ++ccr) {
                    reset(ccr, 0);
                    expected_fault = 0x1002 + available;
                    run();
                    assert(trapped == 2);
                }
            }
    syn68k_set_memory_window(0xff000, 0, 0);
    for (unsigned call = 0; call < 2; ++call) {
        instruction(call, 0x30, 0, 8);
        WRITEUW(0x1002, 0x3b22);
        WRITEUW(0x1004, 16);
        WRITEUW(0x1006, 32);
        destroy_blocks(0x1000, 32);
        for (unsigned ccr = 0; ccr < 32; ++ccr) {
            reset(ccr, 0);
            EM_A0 = 0xff000 - 24;
            EM_D3 = 4;
            expected_fault = 0xff000;
            run();
            assert(trapped == 2);
        }
    }
    printf("%u control-transfer cases passed (%s)\n", cases, native ? "native" : "interpreter");
    return 0;
}
