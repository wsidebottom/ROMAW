/*+===================================================================
  File:      lea-test.c

  Summary:   Implements lea test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Original LEA live-extension, alias, CCR and bounded-EA fixtures. */
#include "syn68k_public.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned cases, trapped, expected_vector = 2;
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
    expected_pc = 0x1000;
    expected_vector = 2;
}

static syn68k_addr_t handler(syn68k_addr_t pc, void* argument)
{
    trapped = (unsigned)(uintptr_t)argument;
    if (trapped != expected_vector || pc != expected_pc)
        fprintf(stderr, "LEA case %u vector %u PC %08x expected %08x\n", cases, trapped, pc, expected_pc);
    assert(trapped == expected_vector && pc == expected_pc);
    assert(READUW(EM_A7) == expected_sr);
    assert(READUL(EM_A7 + 2) == expected_pc);
    assert(READUW(EM_A7 + 6) == (trapped == 2 ? 0x7008 : 0x0010));
    if (trapped == 2)
        assert(READUL(EM_A7 + 20) == expected_fault);
    assert(cpu_state.usp == expected_stack);
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static void instruction(unsigned destination, unsigned ea, uint32 extension, unsigned length)
{
    WRITEUW(0x1000, 0x41c0 | (destination << 9) | ea);
    if (length == 4)
        WRITEUW(0x1002, extension);
    else if (length == 6)
        WRITEUL(0x1002, extension);
    WRITEUW(0x1000 + length, 0x4ef9);
    WRITEUL(0x1002 + length, MAGIC_EXIT_EMULATOR_ADDRESS);
    destroy_blocks(0x1000, 32);
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

    // Cached absolute extensions must be read again at execution.
    instruction(0, 0x39, 0x12345678, 6);
    reset(31, 0);
    run();
    assert(EM_A0 == 0x12345678);
    WRITEUL(0x1002, 0x87654321);
    reset(31, 0);
    run();
    assert(EM_A0 == 0x87654321);
    // Cached source extensions stay live for every destination and EA form.
    const unsigned live_modes[] = {0x28, 0x30, 0x38, 0x39, 0x3a, 0x3b};
    for (unsigned destination = 0; destination < 8; ++destination)
        for (unsigned i = 0; i < sizeof live_modes / sizeof live_modes[0]; ++i) {
            unsigned ea = live_modes[i];
            unsigned extension = ea == 0x28 ? 0 : ea == 0x3a ? 0x2000 : 0x3000;
            instruction(destination, ea, extension, ea == 0x39 ? 6 : 4);
            for (unsigned edit = 0; edit < 2; ++edit) {
                if (ea == 0x39)
                    WRITEUL(0x1002, extension + edit * 2);
                else
                    WRITEUW(0x1002, extension + edit * 2);
                reset(31, 0);
                EM_A0 = 0x3000;
                EM_D3 = 4;
                uint32 address = ea == 0x28 || ea == 0x38 || ea == 0x39 ? 0x3000
                                 : ea == 0x30                           ? 0x3004
                                 : ea == 0x3a                           ? 0x3002
                                                                        : 0x1006;
                run();
                assert(!trapped && EM_AREG(destination) == address + edit * 2);
                assert(status() == expected_sr);
            }
        }

    // Every source mode and destination, including source/destination An aliases.
    for (unsigned destination = 0; destination < 8; ++destination)
        for (unsigned ea = 0; ea < 64; ++ea) {
            unsigned mode = ea >> 3, reg = ea & 7;
            if (destination == 4 && mode == 0) // EXTB.L uses this encoding.
                continue;
            int legal = mode == 2 || mode == 5 || mode == 6 || (mode == 7 && reg < 4);
            uint32 extension = mode == 5   ? 0xfff0
                               : mode == 6 ? 0x3a04
                               : reg == 0  ? 0x8000
                               : reg == 1  ? 0xdeadbeef
                               : reg == 2  ? 0xfff0
                                           : 0x3a04;
            unsigned length = mode == 2 || !legal ? 2 : mode == 7 && reg == 1 ? 6 : 4;
            instruction(destination, ea, extension, length);
            for (unsigned bank = 0; bank < (legal ? 3 : 1); ++bank)
                for (unsigned ccr = 0; ccr < 32; ++ccr) {
                    reset(ccr, bank);
                    for (unsigned i = 0; i < 7; ++i)
                        EM_AREG(i) = 0x3000 + i * 256;
                    EM_D3 = 4;
                    uint32 base = EM_AREG(reg), stack = EM_A7;
                    uint32 address = mode == 2   ? base
                                     : mode == 5 ? base - 16
                                     : mode == 6 ? base + 12
                                     : reg == 0  ? 0xffff8000
                                     : reg == 1  ? extension
                                     : reg == 2  ? 0x0ff2
                                                 : 0x100e;
                    uint32 saved[16];
                    for (unsigned i = 0; i < 16; ++i)
                        saved[i] = cpu_state.regs[i].ul.n;
                    if (!legal) {
                        expected_vector = 4;
                        expected_stack = stack;
                    }
                    run();
                    if (legal) {
                        assert(!trapped && status() == expected_sr);
                        assert(EM_AREG(destination) == address);
                    }
                    else
                        assert(trapped == 4);
                    for (unsigned i = 0; i < (legal ? 16 : 15); ++i)
                        if (legal ? i != 8 + destination : 1)
                            assert(cpu_state.regs[i].ul.n == saved[i]);
                }
        }

    // EXTB.L shares the otherwise illegal LEA mode-0 encoding for An=4.
    for (unsigned reg = 0; reg < 8; ++reg) {
        instruction(4, reg, 0, 2);
        reset(31, 0);
        cpu_state.regs[reg].ul.n = 0x12345680;
        run();
        assert(!trapped && cpu_state.regs[reg].ul.n == 0xffffff80);
        assert((status() & 31) == 0x18);
    }

    // All index registers/sizes/scales, including An index/destination aliases.
    for (unsigned destination = 0; destination < 8; ++destination)
        for (unsigned index = 0; index < 16; ++index)
            for (unsigned scale = 0; scale < 4; ++scale)
                for (unsigned wide = 0; wide < 2; ++wide)
                    for (unsigned full = 0; full < 2; ++full) {
                        instruction(destination, 0x30,
                                    (index << 12) | (wide ? 0x800 : 0) | (scale << 9) | (full ? 0x120 : 0x80),
                                    full ? 6 : 4);
                        if (full) {
                            WRITEUW(0x1002, (index << 12) | (wide ? 0x800 : 0) | (scale << 9) | 0x120);
                            WRITEUW(0x1004, 0xfff0);
                            destroy_blocks(0x1000, 32);
                        }
                        for (unsigned ccr = 0; ccr < 32; ++ccr) {
                            reset(ccr, 0);
                            uint32 base = EM_A0;
                            uint32 value = cpu_state.regs[index].ul.n;
                            uint32 offset = wide ? value : (int16)value;
                            uint32 address = base + (offset << scale) + (full ? -16 : -128);
                            run();
                            assert(!trapped && status() == expected_sr);
                            assert(EM_AREG(destination) == address);
                        }
                    }

    // Native cached source offsets and arithmetic CCR must reach the decoder.
    for (unsigned destination = 0; destination < 8; ++destination) {
        instruction(destination, 0x2f, 16, 4); // LEA 16(A7),An.
        WRITEUW(0x0ffc, 0x588f);               // ADDQ.L #4,A7.
        WRITEUW(0x0ffe, 0x5280);               // ADDQ.L #1,D0.
        destroy_blocks(0x0ffc, 32);
        reset(0, 0);
        uint32 stack = EM_A7;
        EM_D0 = 0xffffffff;
        interpret_code(hash_lookup_code_and_create_if_needed(0x0ffc));
        ++cases;
        assert(!trapped && (status() & 31) == 0x15);
        assert(EM_AREG(destination) == stack + 20);
    }

    DWORD old;
    assert(VirtualProtect((char*)memory + 0xff000, 0x1000, PAGE_NOACCESS, &old));
    syn68k_set_memory_window(0xff000, 0, 0);
    const unsigned extended[] = {0x28, 0x30, 0x38, 0x39, 0x3a, 0x3b};
    // Missing extension bytes must not change the destination, even A7.
    for (unsigned destination = 0; destination < 8; ++destination)
        for (unsigned i = 0; i < sizeof extended / sizeof extended[0]; ++i)
            for (unsigned available = 0; available < (extended[i] == 0x39 ? 4 : 2); ++available) {
                instruction(destination, extended[i], 0, extended[i] == 0x39 ? 6 : 4);
                syn68k_set_memory_window(0x1002 + available, 0, 0);
                for (unsigned ccr = 0; ccr < 32; ++ccr) {
                    reset(ccr, 0);
                    expected_stack = EM_A7;
                    expected_fault = 0x1002 + available;
                    uint32 saved = EM_AREG(destination);
                    run();
                    assert(trapped == 2);
                    if (destination != 7)
                        assert(EM_AREG(destination) == saved);
                }
            }
    syn68k_set_memory_window(0xff000, 0, 0);

    // Reserved full-index encodings reject before changing any destination.
    const unsigned reserved[] = {0x0100, 0x0118, 0x0114};
    for (unsigned destination = 0; destination < 8; ++destination)
        for (unsigned i = 0; i < sizeof reserved / sizeof reserved[0]; ++i) {
            instruction(destination, 0x30, reserved[i], 4);
            for (unsigned ccr = 0; ccr < 32; ++ccr) {
                reset(ccr, 0);
                expected_vector = 4;
                expected_stack = EM_A7;
                uint32 saved = EM_AREG(destination);
                run();
                assert(trapped == 4);
                if (destination != 7)
                    assert(EM_AREG(destination) == saved);
            }
        }

    // Full-index indirection is read before replacing its base/index register.
    for (unsigned destination = 0; destination < 8; ++destination) {
        instruction(destination, 0x30, 0, 8);
        WRITEUW(0x1002, 0x3b22);
        WRITEUW(0x1004, 16);
        WRITEUW(0x1006, 32);
        destroy_blocks(0x1000, 32);
        for (unsigned ccr = 0; ccr < 32; ++ccr) {
            reset(ccr, 0);
            EM_A0 = 0x3000;
            EM_D3 = 4;
            WRITEUL(0x3018, 0xffff0000);
            run();
            assert(!trapped && EM_AREG(destination) == 0xffff0020);
            assert(status() == expected_sr);
            reset(ccr, 0);
            EM_A0 = 0xff000 - 24;
            EM_D3 = 4;
            expected_stack = EM_A7;
            expected_fault = 0xff000;
            uint32 saved = EM_AREG(destination);
            run();
            assert(trapped == 2);
            if (destination != 7)
                assert(EM_AREG(destination) == saved);
        }
    }
    printf("%u LEA cases passed (%s)\n", cases, native ? "native" : "interpreter");
    return 0;
}
