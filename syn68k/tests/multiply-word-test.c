/*+===================================================================
  File:      multiply-word-test.c

  Summary:   Implements multiply word test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Original word integer multiplication, exception and operand-fetch fixtures. */
#include "syn68k_public.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned cases, trapped, expected_vector;
static uint32 expected_pc, expected_address, expected_a0;
static uint16 expected_sr;
static unsigned stack_bank = 3;
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
    assert(READUW(EM_A7 + 6) == (trapped == 2 ? 0x7008 : 0x0010));
    if (trapped == 2)
        assert(READUL(EM_A7 + 20) == expected_address);
    if (stack_bank < 3) {
        if (stack_bank == 0)
            assert(cpu_state.usp == expected_source_sp && EM_A7 == 0x9000 - 60);
        else
            assert(EM_A7 == expected_source_sp - 60);
    }
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

static void check_result(unsigned reg, int sign, uint32 source)
{
    uint32 original = cpu_state.regs[reg].ul.n;
    uint32 result =
        sign ? (int32)(int16)original * (int32)(int16)source : (uint32)(uint16)original * (uint32)(uint16)source;
    unsigned flags = (expected_sr & 16) | (result >> 31 ? 8 : 0) | (!result ? 4 : 0);
    run(0x1000);
    assert(!trapped);
    assert(cpu_state.regs[reg].ul.n == result);
    assert((status() & 31) == flags);
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
    for (unsigned sign = 0; sign < 2; ++sign) {
        instruction(sign ? 0xc1fc : 0xc0fc, 2);
        reset(0);
        EM_D0 = 120;
        run(0x1000);
        assert(EM_D0 == 240);
        WRITEUW(0x1002, 3);
        reset(0);
        EM_D0 = 120;
        run(0x1000);
        assert(EM_D0 == 360);
    }
    // Cached displacement, absolute and indexed extensions also stay live.
    const unsigned extended_modes[] = {0x28, 0x30, 0x38, 0x39, 0x3a, 0x3b};
    for (unsigned sign = 0; sign < 2; ++sign)
        for (unsigned i = 0; i < sizeof extended_modes / sizeof extended_modes[0]; ++i) {
            unsigned ea = extended_modes[i];
            instruction((sign ? 0xc1c0 : 0xc0c0) | ea, 0);
            unsigned extension = ea == 0x28 ? 0 : ea == 0x3a ? 0x2000 : 0x3000;
            uint32 address = ea == 0x30 ? 0x3004 : ea >= 0x3a ? 0x3002 : 0x3000;
            uint32 next = ea == 0x39 ? 0x1006 : 0x1004;
            if (ea == 0x39)
                WRITEUL(0x1002, extension);
            else
                WRITEUW(0x1002, extension);
            WRITEUW(next, 0x4ef9);
            WRITEUL(next + 2, MAGIC_EXIT_EMULATOR_ADDRESS);
            WRITEUW(address, 2);
            WRITEUW(address + 2, 3);
            destroy_blocks(0x1000, 32);
            reset(0);
            EM_D0 = 120;
            EM_D3 = ea == 0x3b ? 0x2000 : 4;
            check_result(0, sign, 2);
            if (ea == 0x39)
                WRITEUL(0x1002, extension + 2);
            else
                WRITEUW(0x1002, extension + 2);
            reset(0);
            EM_D0 = 120;
            EM_D3 = ea == 0x3b ? 0x2000 : 4;
            check_result(0, sign, 3);
        }
    const uint32 multiplicands[] = {0, 1, 0xffffffff, 0x80000000, 0x7fffffff, 32767, 32768, 65535, 65536, 0xffff8000};
    const uint16 multipliers[] = {0, 1, 2, 3, 0xffff, 0x8000, 0x7fff, 16};
    for (unsigned sign = 0; sign < 2; ++sign)
        for (unsigned reg = 0; reg < 8; ++reg)
            for (unsigned source = 0; source < 8; ++source) {
                instruction((sign ? 0xc1c0 : 0xc0c0) | (reg << 9) | source, 0);
                for (unsigned a = 0; a < sizeof multiplicands / sizeof multiplicands[0]; ++a)
                    for (unsigned b = 0; b < sizeof multipliers / sizeof multipliers[0]; ++b)
                        for (unsigned ccr = 0; ccr < 32; ++ccr) {
                            reset(ccr);
                            cpu_state.regs[reg].ul.n = multiplicands[a];
                            if (source != reg)
                                cpu_state.regs[source].ul.n = 0xa55a0000 | multipliers[b];
                            uint32 saved[8];
                            for (unsigned i = 0; i < 8; ++i)
                                saved[i] = cpu_state.regs[i].ul.n;
                            check_result(reg, sign, saved[source]);
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
            instruction((sign ? 0xc1c0 : 0xc0c0) | 0x0200 | ea, 0);
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
                check_result(1, sign, 3);
                assert(EM_A0 == expected_a0);
                assert(EM_AREG(reg) == base + (mode == 3 ? 2 : mode == 4 ? -2 : 0));
            }
        }

    // Native ADDQ flags must reach both the decoder and its exception frame.
    for (unsigned sign = 0; sign < 2; ++sign)
        for (unsigned fault = 0; fault < 2; ++fault) {
            instruction((sign ? 0xc1c0 : 0xc0c0) | (fault ? 0x10 : 4), 0);
            WRITEUW(0x0ffe, 0x5283); // addq.l #1,d3
            destroy_blocks(0x0ffe, 32);
            reset(0);
            EM_D0 = 120;
            EM_D3 = 0xffffffff;
            EM_D4 = 2;
            if (fault) {
                expected_sr = 0x15;
                expected_vector = 2;
                expected_pc = 0x1000;
                EM_A0 = expected_a0 = expected_address = 0x100000;
            }
            run(0x0ffe);
            assert(trapped == expected_vector);
            assert((status() & 31) == (fault ? 0x15 : 0x10));
            assert(EM_D0 == (fault ? 120 : 240));
        }

    DWORD old;
    assert(VirtualProtect((char*)memory + 0xff000, 0x1000, PAGE_NOACCESS, &old));
    // Missing immediate bytes and truncated memory words must not be host reads.
    for (unsigned sign = 0; sign < 2; ++sign)
        for (unsigned available = 0; available < 2; ++available) {
            uint32 limit = available ? 0xfef03 : 0xff000;
            uint32 pc = available ? 0xfef00 : 0xfeffe;
            WRITEUW(pc, sign ? 0xc1fc : 0xc0fc);
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
            instruction(sign ? 0xc1d0 : 0xc0d0, 0);
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
    // Pre/post A0 updates survive faults. A7 predecrement retains its bank.
    // A7 postincrement at this page would also fault while writing the frame;
    // double access faults are outside this fixture and the current model.
    for (unsigned sign = 0; sign < 2; ++sign)
        for (unsigned pre = 0; pre < 2; ++pre)
            for (unsigned bank = 0; bank < 4; ++bank)
                for (unsigned ccr = 0; ccr < 32; ++ccr) {
                    if (!pre && bank < 3)
                        continue;
                    unsigned reg = bank == 3 ? 0 : 7;
                    instruction((sign ? 0xc1c0 : 0xc0c0) | (pre ? 0x20 : 0x18) | reg, 0);
                    reset(ccr);
                    stack_bank = bank;
                    cpu_state.sr = bank == 1 ? 0x2000 : bank == 2 ? 0x3000 : 0;
                    EM_AREG(reg) = 0xff000 + (pre ? 2 : 0);
                    expected_source_sp = pre ? 0xff000 : 0xff002;
                    if (reg == 0)
                        expected_a0 = expected_source_sp;
                    EM_D0 = 0x12345678;
                    expected_sr = status();
                    expected_vector = 2;
                    expected_pc = 0x1000;
                    expected_address = 0xff000;
                    run(0x1000);
                    assert(trapped == 2 && EM_D0 == 0x12345678);
                }
    printf("%u word multiply arithmetic/alias/CCR/EA/fetch cases passed (%s)\n", cases,
           native ? "native" : "interpreter");
    return 0;
}
