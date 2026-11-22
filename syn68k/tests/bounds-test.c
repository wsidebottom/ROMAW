/*+===================================================================
  File:      bounds-test.c

  Summary:   Implements bounds test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Original CHK2/CMP2 range, runtime extension and bounded-memory fixtures. */
#include "syn68k_public.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned trapped, expected_vector, cases;
static uint32 expected_pc, fault_address, instruction_pc = 0x1000;
static uint16 expected_sr;

static uint16 status(void)
{
    return cpu_state.sr | !!cpu_state.ccc | (!!cpu_state.ccv << 1) | (!cpu_state.ccnz << 2) | (!!cpu_state.ccn << 3) |
           (!!cpu_state.ccx << 4);
}

static void reset(unsigned ccr)
{
    cpu_state.sr = 0x2000;
    cpu_state.ccc = ccr & 1;
    cpu_state.ccv = ccr & 2;
    cpu_state.ccnz = (~ccr) & 4;
    cpu_state.ccn = ccr & 8;
    cpu_state.ccx = ccr & 16;
    cpu_state.isp = EM_A7 = 0x8000;
    trapped = 0;
    expected_sr = status();
}

static syn68k_addr_t handler(syn68k_addr_t pc, void* argument)
{
    trapped = (unsigned)(uintptr_t)argument;
    if (trapped != expected_vector || pc != expected_pc)
        fprintf(stderr, "case %u: vector %u/%u PC %08x/%08x opcode %04x ext %04x\n", cases, trapped, expected_vector,
                pc, expected_pc, READUW(instruction_pc), READUW(instruction_pc + 2));
    assert(trapped == expected_vector && pc == expected_pc);
    assert(READUW(EM_A7) == expected_sr && READUL(EM_A7 + 2) == pc);
    assert(READUW(EM_A7 + 6) == (trapped == 2 ? 0x7008 : trapped == 6 ? 0x2018 : 16));
    if (trapped == 2)
        assert(READUL(EM_A7 + 20) == fault_address);
    if (trapped == 6)
        assert(READUL(EM_A7 + 8) == instruction_pc);
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static void write_value(uint32 address, unsigned size, uint32 value)
{
    for (unsigned i = 0; i < size; ++i)
        WRITEUB(address + i, value >> (8 * (size - i - 1)));
}

static void run(uint32 pc)
{
    interpret_code(hash_lookup_code_and_create_if_needed(pc));
    ++cases;
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
    const uint32 values[] = {0, 1, 0x7f, 0x80, 0xff, 0x7fff, 0x8000, 0xffff, 0x7fffffff, 0x80000000, 0xffffffff};
    for (unsigned scale = 0; scale < 3; ++scale) {
        unsigned size = 1u << scale;
        uint32 mask = size == 4 ? 0xffffffff : (1u << (size * 8)) - 1;
        WRITEUW(0x1000, 0x00f9 | (scale << 9));
        WRITEUL(0x1004, 0x3000);
        WRITEUW(0x1008, 0x4ef9);
        WRITEUL(0x100a, MAGIC_EXIT_EMULATOR_ADDRESS);
        destroy_blocks(0x1000, 16);
        for (unsigned reg = 0; reg < 16; ++reg)
            for (unsigned pair = 0; pair < 8; ++pair)
                for (unsigned sample = 0; sample < 11; ++sample)
                    for (unsigned ccr = 0; ccr < 32; ++ccr)
                        for (unsigned check = 0; check < 2; ++check) {
                            reset(ccr);
                            uint32 lower = values[pair] & mask;
                            uint32 upper = values[(pair * 3 + 2) % 11] & mask;
                            uint32 value = values[sample];
                            write_value(0x3000, size, lower);
                            write_value(0x3000 + size, size, upper);
                            cpu_state.regs[reg].ul.n = value;
                            WRITEUW(0x1002, (reg << 12) | (check << 11));
                            if (reg & 8) {
                                if (size == 1) {
                                    lower = (int8)lower;
                                    upper = (int8)upper;
                                }
                                else if (size == 2) {
                                    lower = (int16)lower;
                                    upper = (int16)upper;
                                }
                            }
                            else
                                value &= mask;
                            // Independent interval model, including wrapped intervals.
                            int outside =
                                lower <= upper ? value < lower || value > upper : value < lower && value > upper;
                            int equal = value == lower || value == upper;
                            expected_sr = 0x2000 | (ccr & ~5u) | (equal ? 4 : 0) | outside;
                            expected_vector = check && outside ? 6 : 0;
                            expected_pc = expected_vector ? 0x1008 : 0;
                            // Rn=A7 is an operand, not a host stack; supervisor CHK entry
                            // then pushes its frame relative to that operand.
                            if (reg == 15 && expected_vector)
                                continue;
                            run(0x1000);
                            assert(trapped == expected_vector && status() == expected_sr);
                            if (!trapped)
                                assert(cpu_state.regs[reg].ul.n == values[sample]);
                        }
    }
    /* Every control EA, both execution modes, and extensions changed without
     invalidating the translated instruction. */
    const unsigned addresses[] = {0x10, 0x28, 0x30, 0x38, 0x39, 0x3a, 0x3b};
    for (unsigned scale = 0; scale < 3; ++scale)
        for (unsigned kind = 0; kind < 7; ++kind) {
            unsigned size = 1u << scale;
            WRITEUW(0x1000, 0x00c0 | (scale << 9) | addresses[kind]);
            destroy_blocks(0x1000, 16);
            for (unsigned user = 0; user < 2; ++user)
                for (unsigned alternate = 0; alternate < 2; ++alternate) {
                    reset(31);
                    cpu_state.sr = user ? 0 : 0x2000;
                    EM_D0 = 7;
                    EM_D1 = 0;
                    uint32 address = alternate ? 0x3100 : 0x3000;
                    EM_A0 = address;
                    unsigned end = 0x1004;
                    WRITEUW(0x1002, 0);
                    if (kind == 1) {
                        EM_A0 = address - 32;
                        WRITEUW(end, 32);
                        end += 2;
                    }
                    else if (kind == 2 || kind == 6) {
                        /* D1.w index, zero displacement. */
                        if (kind == 6)
                            EM_D1 = address - end;
                        WRITEUW(end, 0x1000);
                        end += 2;
                    }
                    else if (kind == 3 || kind == 5) {
                        WRITEUW(end, kind == 3 ? address : address - end);
                        end += 2;
                    }
                    else if (kind == 4) {
                        WRITEUL(end, address);
                        end += 4;
                    }
                    write_value(address, size, alternate ? 7 : 1);
                    write_value(address + size, size, 9);
                    WRITEUW(end, 0x4ef9);
                    WRITEUL(end + 2, MAGIC_EXIT_EMULATOR_ADDRESS);
                    expected_vector = 0;
                    run(0x1000);
                    assert(!trapped && EM_D0 == 7);
                    assert(status() == ((user ? 0 : 0x2000) | 26 | (alternate ? 4 : 0)));
                }
        }
    for (unsigned ea = 0; ea < 64; ++ea) {
        unsigned mode = ea >> 3;
        if (mode == 2 || mode == 5 || mode == 6 || (mode == 7 && (ea & 7) < 4))
            continue;
        reset(31);
        WRITEUW(0x1000, 0x00c0 | ea);
        WRITEUW(0x1002, 0);
        destroy_blocks(0x1000, 16);
        expected_vector = 4;
        expected_pc = 0x1000;
        run(0x1000);
        assert(trapped == 4);
    }
    WRITEUW(0x1000, 0x00d0); // (A0)
    destroy_blocks(0x1000, 16);
    for (unsigned bit = 0; bit < 11; ++bit) {
        reset(31);
        EM_A0 = 0x100000;
        WRITEUW(0x1002, 1u << bit);
        expected_vector = 4;
        expected_pc = 0x1000;
        run(0x1000);
        assert(trapped == 4 && EM_A0 == 0x100000);
    }
    reset(31);
    WRITEUW(0x1002, 0);
    EM_A0 = 0xfffff;
    WRITEUB(0xfffff, 7);
    expected_vector = 2;
    expected_pc = 0x1000;
    fault_address = 0x100000;
    run(0x1000);
    assert(trapped == 2 && status() == 0x201f);
    DWORD protection;
    assert(VirtualProtect((uint8*)memory + 0xff000, 4096, PAGE_NOACCESS, &protection));
    syn68k_set_memory_window(0xff000, 0, 0);
    reset(31);
    EM_A0 = 0xfefff;
    WRITEUB(0xfefff, 7);
    fault_address = 0xff000;
    run(0x1000);
    assert(trapped == 2 && status() == 0x201f);
    assert(VirtualProtect((uint8*)memory + 0xff000, 4096, protection, &protection));
    syn68k_set_memory_window(1024 * 1024, 0, 0);
    reset(31);
    WRITEUW(0xffffe, 0x00d0);
    fault_address = 0x100000;
    expected_pc = 0xffffe;
    run(0xffffe);
    assert(trapped == 2);
    /* Native arithmetic produces N/V; bounds processing must retain these even
     when the translated successor only consumes C/Z. */
    WRITEUW(0x1100, 0x203c);
    WRITEUL(0x1102, 0x7fffffff);
    WRITEUW(0x1106, 0x5280);
    WRITEUW(0x1108, 0x04f9);
    WRITEUL(0x110c, 0x3000);
    WRITEUW(0x1110, 0x40c1);
    WRITEUW(0x1112, 0x4ef9);
    WRITEUL(0x1114, MAGIC_EXIT_EMULATOR_ADDRESS);
    destroy_blocks(0x1100, 32);
    instruction_pc = 0x1108;
    for (unsigned check = 0; check < 2; ++check) {
        reset(31);
        WRITEUW(0x110a, check ? 0x0800 : 0);
        WRITEUL(0x3000, 0);
        WRITEUL(0x3004, check ? 0x7fffffff : 0xffffffff);
        expected_vector = check ? 6 : 0;
        expected_pc = 0x1110;
        expected_sr = check ? 0x200b : 0x200a;
        EM_D1 = 0xdead0000;
        run(0x1100);
        assert(trapped == expected_vector && status() == expected_sr);
        assert(EM_D0 == 0x80000000);
        if (!check)
            assert(EM_D1 == 0xdead200a);
    }
    printf("%u CHK2/CMP2 range/register/CCR/runtime-extension/fault cases passed (%s)\n", cases,
           native ? "native" : "interpreter");
}
