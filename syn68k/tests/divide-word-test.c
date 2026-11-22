/*+===================================================================
  File:      divide-word-test.c

  Summary:   Implements divide word test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Original word integer division, exception and operand-fetch fixtures. */
#include "syn68k_public.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned cases, trapped, expected_vector;
static uint32 expected_pc, expected_address, expected_a0;
static uint16 expected_sr;
static unsigned stack_bank;
static uint32 expected_source_sp;

static uint16 status(void)
{
    return cpu_state.sr | !!cpu_state.ccc | (!!cpu_state.ccv << 1) | (!cpu_state.ccnz << 2) | (!!cpu_state.ccn << 3) |
           (!!cpu_state.ccx << 4);
}

static void reset(unsigned ccr)
{
    cpu_state.sr = 0;
    cpu_state.ccc = ccr & 1;
    cpu_state.ccv = ccr & 2;
    cpu_state.ccnz = (~ccr) & 4;
    cpu_state.ccn = ccr & 8;
    cpu_state.ccx = ccr & 16;
    cpu_state.usp = EM_A7 = 0x8000;
    cpu_state.isp = 0x9000;
    EM_A0 = expected_a0 = 0x3000;
    trapped = expected_vector = 0;
    stack_bank = 3;
    expected_sr = status();
}

static syn68k_addr_t handler(syn68k_addr_t pc, void* argument)
{
    trapped = (unsigned)(uintptr_t)argument;
    if (trapped != expected_vector || pc != expected_pc)
        fprintf(stderr, "case %u: vector %u/%u PC %08x/%08x\n", cases, trapped, expected_vector, pc, expected_pc);
    assert(trapped == expected_vector && pc == expected_pc);
    assert(EM_A0 == expected_a0);
    assert(READUW(EM_A7) == expected_sr && READUL(EM_A7 + 2) == pc);
    assert(READUW(EM_A7 + 6) == (trapped == 2 ? 0x7008 : trapped == 5 ? 0x2014 : 0x0010));
    if (stack_bank < 3) {
        if (stack_bank == 0)
            assert(cpu_state.usp == expected_source_sp && EM_A7 == 0x9000 - 12);
        else
            assert(EM_A7 == expected_source_sp - 12);
    }
    if (trapped == 2)
        assert(READUL(EM_A7 + 20) == expected_address);
    if (trapped == 5)
        assert(READUL(EM_A7 + 8) == expected_address);
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static void instruction(unsigned opcode, uint16 operand)
{
    WRITEUW(0x1000, opcode);
    WRITEUW(0x1002, operand);
    uint32 next = (opcode & 63) == 0x3c ? 0x1004 : 0x1002;
    WRITEUW(next, 0x4ef9);
    WRITEUL(next + 2, MAGIC_EXIT_EMULATOR_ADDRESS);
    destroy_blocks(0x1000, 16);
}

static void run(uint32 pc)
{
    interpret_code(hash_lookup_code_and_create_if_needed(pc));
    ++cases;
}

static void check_result(unsigned reg, int sign, uint32 source, uint32 next)
{
    uint32 original = cpu_state.regs[reg].ul.n, result = original;
    unsigned flags = expected_sr & 31, mask = 31;
    if (!(uint16)source) {
        expected_vector = 5;
        expected_pc = next;
        expected_address = 0x1000;
        expected_sr &= ~1u;
        flags &= ~1u;
    }
    else {
        int64 dividend = sign ? (int64)(int32)original : (int64)original;
        int64 divisor = sign ? (int16)source : (uint16)source;
        int64 quotient = dividend / divisor;
        int64 remainder = dividend % divisor;
        int overflow = sign ? quotient < -32768 || quotient > 32767 : quotient > 65535;
        flags &= 16;
        if (overflow) {
            flags |= 2;
            mask = 19; // N/Z undefined; destination is unchanged.
        }
        else {
            result = ((uint32)(uint16)remainder << 16) | (uint16)quotient;
            flags |= ((uint16)quotient & 0x8000 ? 8 : 0) | (!quotient ? 4 : 0);
        }
    }
    run(0x1000);
    assert(trapped == expected_vector);
    assert(cpu_state.regs[reg].ul.n == result);
    assert(((status() ^ flags) & mask) == 0);
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
    for (unsigned i = 0; i < 64; ++i)
        trap_install_handler(i, handler, (void*)(uintptr_t)i);

    // Warm an immediate operand, then edit it without invalidating its block.
    instruction(0x81fc, 2);
    reset(0);
    EM_D0 = 120;
    run(0x1000);
    assert(EM_D0 == 60);
    WRITEUW(0x1002, 3);
    reset(0);
    EM_D0 = 120;
    run(0x1000);
    assert(EM_D0 == 40);
    // Preserve each dispatch mode's historical undefined overflow flags.
    instruction(0x81c1, 0);
    reset(16);
    EM_D0 = 0x7fffffff;
    EM_D1 = 1;
    run(0x1000);
    assert(!trapped && EM_D0 == 0x7fffffff);
    assert((status() & 31) == (native && sizeof(void*) == 4 ? 0x1a : 0x12));
    const uint32 dividends[] = {0, 1, 0xffffffff, 0x80000000, 0x7fffffff, 32767, 32768, 65535, 65536, 0xffff8000};
    const uint16 divisors[] = {0, 1, 2, 3, 0xffff, 0x8000, 0x7fff, 16};
    for (unsigned sign = 0; sign < 2; ++sign)
        for (unsigned reg = 0; reg < 8; ++reg)
            for (unsigned source = 0; source < 8; ++source) {
                instruction((sign ? 0x81c0 : 0x80c0) | (reg << 9) | source, 0);
                for (unsigned a = 0; a < sizeof dividends / sizeof dividends[0]; ++a)
                    for (unsigned b = 0; b < sizeof divisors / sizeof divisors[0]; ++b)
                        for (unsigned ccr = 0; ccr < 32; ++ccr) {
                            reset(ccr);
                            cpu_state.regs[reg].ul.n = dividends[a];
                            if (source != reg)
                                cpu_state.regs[source].ul.n = 0xa55a0000 | divisors[b];
                            uint32 saved[8];
                            for (unsigned i = 0; i < 8; ++i)
                                saved[i] = cpu_state.regs[i].ul.n;
                            check_result(reg, sign, saved[source], 0x1002);
                            for (unsigned i = 0; i < 8; ++i)
                                if (i != reg)
                                    assert(cpu_state.regs[i].ul.n == saved[i]);
                        }
            }

    // Every source mode, including the PC-relative base before the EA extension.
    for (unsigned sign = 0; sign < 2; ++sign)
        for (unsigned ea = 0; ea < 64; ++ea) {
            unsigned mode = ea >> 3, reg = ea & 7;
            int legal = mode != 1 && (mode != 7 || reg <= 4);
            instruction((sign ? 0x81c0 : 0x80c0) | 0x0200 | ea, 0);
            reset(31);
            for (unsigned i = 0; i < 7; ++i)
                EM_AREG(i) = 0x3000;
            EM_D1 = 100;
            EM_D3 = 4;
            uint32 base = EM_AREG(reg), address = base, next = 0x1002;
            switch (mode) {
            case 0:
                cpu_state.regs[reg].ul.n = 3;
                break;
            case 3:
                if (reg == 0)
                    expected_a0 += 2;
                break;
            case 4:
                address -= 2;
                if (reg == 0)
                    expected_a0 -= 2;
                break;
            case 5:
                WRITEUW(next, 16);
                next += 2;
                address += 16;
                break;
            case 6:
                WRITEUW(next, 0x3a04);
                next += 2;
                address += 12;
                break;
            case 7:
                switch (reg) {
                case 0:
                    WRITEUW(next, 0x3000);
                    next += 2;
                    address = 0x3000;
                    break;
                case 1:
                    WRITEUL(next, 0x3000);
                    next += 4;
                    address = 0x3000;
                    break;
                case 2:
                    WRITEUW(next, 0x2000);
                    address = next + 0x2000;
                    next += 2;
                    break;
                case 3:
                    WRITEUW(next, 0x3a04);
                    address = next + 12;
                    next += 2;
                    break;
                case 4:
                    address = next;
                    next += 2;
                    break;
                }
                break;
            }
            if (mode && legal)
                WRITEUW(address, 3);
            WRITEUW(next, 0x4ef9);
            WRITEUL(next + 2, MAGIC_EXIT_EMULATOR_ADDRESS);
            destroy_blocks(0x1000, 32);
            if (!legal) {
                expected_vector = 4;
                expected_pc = 0x1000;
                run(0x1000);
                assert(trapped == 4);
            }
            else {
                check_result(1, sign, 3, next);
                assert(EM_A0 == expected_a0);
                assert(EM_AREG(reg) == base + (mode == 3 ? 2 : mode == 4 ? -2 : 0));
            }
        }

    // A7 source updates must be visible in the correct bank inside the handler.
    for (unsigned bank = 0; bank < 3; ++bank)
        for (unsigned pre = 0; pre < 2; ++pre)
            for (unsigned sign = 0; sign < 2; ++sign)
                for (unsigned ccr = 0; ccr < 32; ++ccr) {
                    instruction((sign ? 0x81c0 : 0x80c0) | (pre ? 0x27 : 0x1f), 0);
                    reset(ccr);
                    stack_bank = bank;
                    cpu_state.sr = bank == 0 ? 0 : bank == 1 ? 0x2000 : 0x3000;
                    EM_A7 = bank == 0 ? 0x8000 : bank == 1 ? 0x9000 : 0xa000;
                    WRITEUW(EM_A7 - (pre ? 2 : 0), 0);
                    expected_source_sp = EM_A7 + (pre ? -2 : 2);
                    expected_sr = status();
                    EM_D0 = 123;
                    check_result(0, sign, 0, 0x1002);
                }

    // Native ADDQ flags must reach both the decoder and its exception frames.
    for (unsigned sign = 0; sign < 2; ++sign)
        for (unsigned path = 0; path < 3; ++path) {
            instruction((sign ? 0x81c0 : 0x80c0) | (path == 2 ? 0x10 : 4), 0);
            WRITEUW(0x0ffe, 0x5283); // addq.l #1,d3
            destroy_blocks(0x0ffe, 32);
            reset(0);
            EM_D0 = 120;
            EM_D3 = 0xffffffff;
            EM_D4 = path == 1 ? 0 : 2;
            expected_sr = path == 2 ? 0x15 : 0x14;
            if (path) {
                expected_vector = path == 2 ? 2 : 5;
                expected_pc = path == 2 ? 0x1000 : 0x1002;
                expected_address = path == 2 ? 0x100000 : 0x1000;
            }
            if (path == 2)
                EM_A0 = expected_a0 = 0x100000;
            run(0x0ffe);
            assert(trapped == expected_vector);
            assert((status() & 31) == (path ? expected_sr : 0x10));
            assert(EM_D0 == (path ? 120 : 60));
        }

    DWORD old;
    assert(VirtualProtect((char*)memory + 0xff000, 0x1000, PAGE_NOACCESS, &old));
    // Missing immediate bytes and truncated memory words must not be host reads.
    for (unsigned sign = 0; sign < 2; ++sign)
        for (unsigned available = 0; available < 2; ++available) {
            uint32 limit = available ? 0xfef03 : 0xff000;
            uint32 pc = available ? 0xfef00 : 0xfeffe;
            WRITEUW(pc, sign ? 0x81fc : 0x80fc);
            if (available)
                WRITEUB(pc + 2, 0);
            syn68k_set_memory_window(limit, 0, 0);
            destroy_blocks(pc, 4);
            for (unsigned ccr = 0; ccr < 32; ++ccr) {
                reset(ccr);
                EM_D0 = 0x12345678;
                expected_vector = 2;
                expected_pc = pc;
                expected_address = limit;
                run(pc);
                assert(trapped == 2 && EM_D0 == 0x12345678);
            }
        }
    syn68k_set_memory_window(0xff000, 0, 0);
    for (unsigned sign = 0; sign < 2; ++sign)
        for (unsigned available = 0; available < 2; ++available) {
            instruction(sign ? 0x81d0 : 0x80d0, 0);
            if (available)
                WRITEUB(0xfefff, 0);
            for (unsigned ccr = 0; ccr < 32; ++ccr) {
                reset(ccr);
                EM_D0 = 0x12345678;
                EM_A0 = expected_a0 = 0xff000 - available;
                expected_vector = 2;
                expected_pc = 0x1000;
                expected_address = 0xff000;
                run(0x1000);
                assert(trapped == 2 && EM_D0 == 0x12345678);
            }
        }
    printf("%u word divide arithmetic/alias/CCR/EA/bank/fetch cases passed (%s)\n", cases,
           native ? "native" : "interpreter");
    return 0;
}
