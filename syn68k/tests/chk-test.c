/*+===================================================================
  File:      chk-test.c

  Summary:   Implements chk test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Original CHK operand, condition, runtime-extension and fault fixtures. */
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

    // A negative Dn still fetches its bound before posting CHK's exception.
    reset(23);
    EM_D0 = 0xffffffff;
    EM_A0 = 0x100000;
    WRITEUW(0x1000, 0x4190);
    destroy_blocks(0x1000, 16);
    expected_vector = 2;
    expected_pc = 0x1000;
    fault_address = 0x100000;
    run(0x1000);
    assert(trapped == 2 && status() == 0x2017);

    const uint32 values[] = {0, 1, 0x7f, 0x80, 0x7fff, 0x8000, 0xffff, 0x7fffffff, 0x80000000, 0xffffffff};
    for (unsigned size = 2; size <= 4; size += 2)
        for (unsigned reg = 0; reg < 8; ++reg) {
            WRITEUW(0x1000, (size == 2 ? 0x4180 : 0x4100) | (reg << 9) | 0x39);
            WRITEUL(0x1002, 0x3000);
            WRITEUW(0x1006, 0x4ef9);
            WRITEUL(0x1008, MAGIC_EXIT_EMULATOR_ADDRESS);
            destroy_blocks(0x1000, 16);
            for (unsigned a = 0; a < 10; ++a)
                for (unsigned b = 0; b < 10; ++b)
                    for (unsigned ccr = 0; ccr < 32; ++ccr) {
                        reset(ccr);
                        if (ccr & 1) {
                            cpu_state.sr = 0;
                            cpu_state.usp = EM_A7 = 0x7000;
                        }
                        int32 value = size == 2 ? (int16)values[a] : (int32)values[a];
                        int32 bound = size == 2 ? (int16)values[b] : (int32)values[b];
                        cpu_state.regs[reg].ul.n = values[a];
                        write_value(0x3000, size, values[b]);
                        expected_sr = status() & ~8u;
                        if (value < 0)
                            expected_sr |= 8;
                        expected_vector = value < 0 || value > bound ? 6 : 0;
                        expected_pc = expected_vector ? 0x1006 : 0;
                        run(0x1000);
                        assert(trapped == expected_vector);
                        assert((status() & 31) == (expected_sr & 31));
                        assert(cpu_state.regs[reg].ul.n == values[a]);
                    }
        }

    // All EA encodings, including Dn/immediate/PC forms and A7 updates.
    for (unsigned size = 2; size <= 4; size += 2)
        for (unsigned ea = 0; ea < 64; ++ea) {
            unsigned mode = ea >> 3, reg = ea & 7;
            int legal = mode != 1 && (mode != 7 || reg <= 4);
            reset(31);
            for (unsigned i = 0; i < 8; ++i)
                cpu_state.regs[i].ul.n = 10;
            EM_D0 = 7;
            for (unsigned i = 0; i < 8; ++i)
                cpu_state.regs[8 + i].ul.n = mode == 4 ? 0x3000 + size : 0x3000;
            if (!legal)
                EM_A7 = 0x8000;
            WRITEUW(0x1000, (size == 2 ? 0x4180 : 0x4100) | ea);
            unsigned end = 0x1002;
            if (mode == 5) {
                cpu_state.regs[8 + reg].ul.n = 0x2fe0;
                WRITEUW(end, 32);
                end += 2;
            }
            else if (mode == 6 || (mode == 7 && reg == 3)) {
                EM_D1 = mode == 6 ? 0 : 0x3000 - end;
                WRITEUW(end, 0x1000);
                end += 2;
            }
            else if (mode == 7 && (reg == 0 || reg == 2)) {
                WRITEUW(end, reg == 0 ? 0x3000 : 0x3000 - end);
                end += 2;
            }
            else if (mode == 7 && reg == 1) {
                WRITEUL(end, 0x3000);
                end += 4;
            }
            else if (mode == 7 && reg == 4) {
                write_value(end, size, 10);
                end += size;
            }
            write_value(0x3000, size, 10);
            WRITEUW(end, 0x4ef9);
            WRITEUL(end + 2, MAGIC_EXIT_EMULATOR_ADDRESS);
            destroy_blocks(0x1000, 16);
            expected_sr = legal ? 0x2017 : 0x201f;
            expected_vector = legal ? 0 : 4;
            expected_pc = legal ? 0 : 0x1000;
            run(0x1000);
            assert(trapped == expected_vector && status() == expected_sr);
            if (legal && mode == 3)
                assert(cpu_state.regs[8 + reg].ul.n == 0x3000 + size);
            if (legal && mode == 4)
                assert(cpu_state.regs[8 + reg].ul.n == 0x3000);
        }

    // Re-read changed absolute extensions without invalidating the opcode.
    WRITEUW(0x1000, 0x41b9);
    WRITEUW(0x1006, 0x4ef9);
    WRITEUL(0x1008, MAGIC_EXIT_EMULATOR_ADDRESS);
    destroy_blocks(0x1000, 16);
    for (unsigned alternate = 0; alternate < 4; ++alternate) {
        reset(31);
        EM_D0 = 7;
        WRITEUL(0x1002, alternate & 1 ? 0x3100 : 0x3000);
        WRITEUW(0x3000, 10);
        WRITEUW(0x3100, 6);
        expected_sr = 0x2017;
        expected_vector = alternate & 1 ? 6 : 0;
        expected_pc = expected_vector ? 0x1006 : 0;
        run(0x1000);
        assert(trapped == expected_vector && status() == expected_sr);
    }

    // A missing bound extension must fault before flags change, with no host read.
    DWORD protection;
    assert(VirtualProtect((uint8*)memory + 0xff000, 4096, PAGE_NOACCESS, &protection));
    syn68k_set_memory_window(0xff000, 0, 0);
    for (unsigned size = 2; size <= 4; size += 2) {
        reset(23);
        instruction_pc = size == 2 ? 0xfeffe : 0xfeffc;
        WRITEUW(instruction_pc, size == 2 ? 0x41bc : 0x413c);
        if (size == 4)
            WRITEUW(instruction_pc + 2, 0x1234);
        destroy_blocks(instruction_pc, 2);
        EM_D0 = 0xffffffff;
        expected_vector = 2;
        expected_pc = instruction_pc;
        fault_address = 0xff000;
        run(instruction_pc);
        assert(trapped == 2 && status() == 0x2017);
        instruction_pc = 0x1000;
        WRITEUW(0x1000, size == 2 ? 0x41b9 : 0x4139);
        destroy_blocks(0x1000, 16);
        for (unsigned available = 0; available < size; ++available) {
            reset(23);
            EM_D0 = 0xffffffff;
            WRITEUL(0x1002, 0xff000 - available);
            expected_pc = 0x1000;
            run(0x1000);
            assert(trapped == 2 && status() == 0x2017);
        }
    }
    syn68k_set_memory_window(1024 * 1024, 0, 0);
    assert(VirtualProtect((uint8*)memory + 0xff000, 4096, protection, &protection));

    reset(23);
    instruction_pc = 0x1000;
    EM_D0 = 0xffffffff;
    EM_A0 = 0x100000;
    WRITEUW(0x1000, 0x41b0);
    WRITEUW(0x1002, 0x1100); // Reserved full-index base-displacement size.
    destroy_blocks(0x1000, 16);
    expected_vector = 4;
    expected_pc = 0x1000;
    run(0x1000);
    assert(trapped == 4 && status() == 0x2017);

    // Preserve every native ADDQ-produced flag except CHK's defined N result.
    reset(0);
    WRITEUW(0x1000, 0x70ff); // moveq #-1,d0
    WRITEUW(0x1002, 0x5200); // addq.b #1,d0: X/C/Z set
    WRITEUW(0x1004, 0x41bc); // chk.w #10,d0: negative low word
    WRITEUW(0x1006, 10);
    destroy_blocks(0x1000, 16);
    instruction_pc = 0x1004;
    expected_vector = 6;
    expected_pc = 0x1008;
    expected_sr = 0x201d;
    run(0x1000);
    assert(trapped == 6 && EM_D0 == 0xffffff00 && status() == expected_sr);
    printf("%u CHK operand/CCR/EA/extension/fault cases passed (%s)\n", cases, native ? "native" : "interpreter");
    VirtualFree(memory, 0, MEM_RELEASE);
    return 0;
}
