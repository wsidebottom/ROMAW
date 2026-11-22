/*+===================================================================
  File:      pea-test.c

  Summary:   Implements pea test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Original PEA effective-address, bounded-stack and CCR fixtures. */
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
        fprintf(stderr, "PEA case %u vector %u PC %08x expected %08x\n", cases, trapped, pc, expected_pc);
    assert(trapped == expected_vector && pc == expected_pc);
    assert(READUW(EM_A7) == expected_sr);
    assert(READUL(EM_A7 + 2) == expected_pc);
    assert(READUW(EM_A7 + 6) == (trapped == 2 ? 0x7008 : 0x0010));
    if (trapped == 2)
        assert(READUL(EM_A7 + 20) == expected_fault);
    assert(cpu_state.usp == expected_stack);
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static void instruction(unsigned ea, uint32 extension, unsigned length)
{
    WRITEUW(0x1000, 0x4840 | ea);
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

    // Cached absolute operands must be read again at execution.
    instruction(0x39, 0x12345678, 6);
    reset(31, 0);
    run();
    assert(READUL(EM_A7) == 0x12345678);
    WRITEUL(0x1002, 0x87654321);
    reset(31, 0);
    run();
    assert(READUL(EM_A7) == 0x87654321);
    // Every PEA source mode, CCR pattern, stack bank and stack alignment.
    for (unsigned ea = 16; ea < 64; ++ea) {
        unsigned mode = ea >> 3, reg = ea & 7;
        int legal = mode == 2 || mode == 5 || mode == 6 || (mode == 7 && reg < 4);
        uint32 extension = mode == 5   ? 0xfff0
                           : mode == 6 ? 0x3a04
                           : reg == 0  ? 0x8000
                           : reg == 1  ? 0xdeadbeef
                           : reg == 2  ? 0xfff0
                                       : 0x3a04;
        unsigned length = mode == 2 || !legal ? 2 : mode == 7 && reg == 1 ? 6 : 4;
        instruction(ea, extension, length);
        for (unsigned bank = 0; bank < (legal ? 3 : 1); ++bank)
            for (unsigned align = 0; align < (legal ? 4 : 1); ++align)
                for (unsigned ccr = 0; ccr < 32; ++ccr) {
                    reset(ccr, bank);
                    for (unsigned i = 0; i < 7; ++i)
                        EM_AREG(i) = 0x3000 + i * 256;
                    EM_D3 = 4;
                    EM_A7 += align;
                    uint32 stack = EM_A7, base = EM_AREG(reg);
                    uint32 address = mode == 2   ? base
                                     : mode == 5 ? base - 16
                                     : mode == 6 ? base + 12
                                     : reg == 0  ? 0xffff8000
                                     : reg == 1  ? extension
                                     : reg == 2  ? 0x0ff2
                                                 : 0x100e;
                    uint32 saved[15];
                    for (unsigned i = 0; i < 15; ++i)
                        saved[i] = cpu_state.regs[i].ul.n;
                    if (!legal) {
                        expected_vector = 4;
                        expected_stack = stack;
                    }
                    run();
                    if (legal) {
                        assert(!trapped && status() == expected_sr);
                        assert(EM_A7 == stack - 4 && READUL(EM_A7) == address);
                    }
                    else
                        assert(trapped == 4);
                    for (unsigned i = 0; i < 15; ++i)
                        assert(cpu_state.regs[i].ul.n == saved[i]);
                }
    }

    // Native cached SP offsets and live CCR must be committed before decoding EA.
    for (unsigned bank = 0; bank < 3; ++bank) {
        instruction(0x17, 0, 2); // PEA (A7).
        WRITEUW(0x0ffc, 0x588f); // ADDQ.L #4,A7.
        WRITEUW(0x0ffe, 0x5280); // ADDQ.L #1,D0.
        destroy_blocks(0x0ffc, 32);
        reset(0, bank);
        uint32 stack = EM_A7;
        EM_D0 = 0xffffffff;
        interpret_code(hash_lookup_code_and_create_if_needed(0x0ffc));
        ++cases;
        assert(!trapped && (status() & 31) == 0x15);
        assert(EM_A7 == stack && READUL(stack) == stack + 4);
    }

    DWORD old;
    assert(VirtualProtect((char*)memory + 0xff000, 0x1000, PAGE_NOACCESS, &old));
    syn68k_set_memory_window(0xff000, 0, 0);
    // Reject the complete stack write before touching any accessible prefix byte.
    for (unsigned available = 0; available < 4; ++available)
        for (unsigned ccr = 0; ccr < 32; ++ccr) {
            instruction(0x10, 0, 2);
            reset(ccr, 0);
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

    // Truncated instruction extensions fail before SP changes.
    const unsigned extended[] = {0x28, 0x30, 0x38, 0x39, 0x3a, 0x3b};
    for (unsigned i = 0; i < sizeof extended / sizeof extended[0]; ++i)
        for (unsigned available = 0; available < (extended[i] == 0x39 ? 4 : 2); ++available) {
            instruction(extended[i], 0, extended[i] == 0x39 ? 6 : 4);
            syn68k_set_memory_window(0x1002 + available, 0, 0);
            // Exception storage is physically valid; this fixture window constrains
            // instruction fetches, not an MMU mapping for the supervisor stack.
            for (unsigned ccr = 0; ccr < 32; ++ccr) {
                reset(ccr, 0);
                expected_stack = EM_A7;
                expected_fault = 0x1002 + available;
                run();
                assert(trapped == 2);
            }
        }
    syn68k_set_memory_window(0xff000, 0, 0);

    // Full-index memory indirection is fetched, but the final EA is not read.
    instruction(0x30, 0, 8);
    WRITEUW(0x1002,
            0x3b22); // D3.L * 2, word base displacement, preindex+word OD.
    WRITEUW(0x1004, 16);
    WRITEUW(0x1006, 32);
    destroy_blocks(0x1000, 32);
    for (unsigned ccr = 0; ccr < 32; ++ccr) {
        reset(ccr, 0);
        EM_A0 = 0x3000;
        EM_D3 = 4;
        WRITEUL(0x3018, 0xffff0000);
        run();
        assert(!trapped && READUL(EM_A7) == 0xffff0020);
        assert(status() == expected_sr);
        reset(ccr, 0);
        EM_A0 = 0xff000 - 24;
        EM_D3 = 4;
        expected_stack = EM_A7;
        expected_fault = 0xff000;
        run();
        assert(trapped == 2);
    }
    printf("%u PEA cases passed (%s)\n", cases, native ? "native" : "interpreter");
    return 0;
}
