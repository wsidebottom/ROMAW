/*+===================================================================
  File:      move16-test.c

  Summary:   Implements move16 test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Original 68040 MOVE16 line, register, CCR and bounded-fault fixtures. */
#include "syn68k_public.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned trapped, expected_vector, cases;
static uint32 fault_pc, fault_address;
static uint16 saved_sr;

static uint16 status(void)
{
    return cpu_state.sr | !!cpu_state.ccc | (!!cpu_state.ccv << 1) | (!cpu_state.ccnz << 2) | (!!cpu_state.ccn << 3) |
           (!!cpu_state.ccx << 4);
}

static void flags(unsigned ccr)
{
    cpu_state.ccc = ccr & 1;
    cpu_state.ccv = ccr & 2;
    cpu_state.ccnz = (~ccr) & 4;
    cpu_state.ccn = ccr & 8;
    cpu_state.ccx = ccr & 16;
}

static syn68k_addr_t handler(syn68k_addr_t pc, void* argument)
{
    trapped = (unsigned)(uintptr_t)argument;
    assert(trapped == expected_vector && pc == fault_pc);
    assert(READUW(EM_A7) == saved_sr && READUL(EM_A7 + 2) == pc);
    assert(READUW(EM_A7 + 6) == ((trapped == 2 ? 7 : 0) << 12) + trapped * 4);
    if (trapped == 2)
        assert(READUL(EM_A7 + 8) == fault_address && READUL(EM_A7 + 20) == fault_address);
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static void instruction(uint32 pc, unsigned mode, unsigned reg, unsigned other, uint32 absolute)
{
    WRITEUW(pc, 0xf600 | (mode << 3) | reg);
    if (mode == 4)
        WRITEUW(pc + 2, 0x8000 | (other << 12));
    else
        WRITEUL(pc + 2, absolute);
    unsigned length = mode == 4 ? 4 : 6;
    WRITEUW(pc + length, 0x4ef9);
    WRITEUL(pc + length + 2, MAGIC_EXIT_EMULATOR_ADDRESS);
}

static void transfer(unsigned mode, unsigned reg, unsigned other, unsigned low, unsigned dest_low, unsigned ccr,
                     int user)
{
    cpu_state.sr = user ? 0 : 0x2000;
    flags(ccr);
    uint32 registers[16];
    for (unsigned i = 0; i < 16; ++i)
        cpu_state.regs[i].ul.n = 0x91827364u ^ (0x1234567u * i);
    uint32 source = 0x4010 + low, destination = 0x5010 + dest_low;
    if (mode == 4 && reg == other)
        destination = source;
    if (mode == 4 || !(mode & 1))
        cpu_state.regs[8 + reg].ul.n = source;
    if (mode == 4)
        cpu_state.regs[8 + other].ul.n = destination;
    else if (mode & 1)
        cpu_state.regs[8 + reg].ul.n = destination;
    for (unsigned i = 0; i < 16; ++i)
        registers[i] = cpu_state.regs[i].ul.n;
    uint8 first[64], second[64];
    for (unsigned i = 0; i < 64; ++i) {
        WRITEUB(0x4000 + i, first[i] = i * 37 + ccr);
        WRITEUB(0x5000 + i, second[i] = i * 13 + ccr + 1);
    }
    if (destination != source)
        memcpy(second + 16, first + 16, 16);
    instruction(0x1000, mode, reg, other, mode & 1 ? source : destination);
    // Preserve cached blocks when only the runtime extension changes.
    static unsigned previous_opcode = 0xffff;
    unsigned opcode = READUW(0x1000);
    if (opcode != previous_opcode)
        destroy_blocks(0x1000, 16);
    previous_opcode = opcode;
    trapped = 0;
    interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
    assert(!trapped && status() == ((user ? 0 : 0x2000) | ccr));
    if (mode == 4) {
        registers[8 + reg] = source + 16;
        registers[8 + other] = destination + 16;
    }
    else if (mode < 2)
        registers[8 + reg] += 16;
    for (unsigned i = 0; i < 16; ++i)
        assert(cpu_state.regs[i].ul.n == registers[i]);
    for (unsigned i = 0; i < 64; ++i)
        assert(READUB(0x4000 + i) == first[i] && READUB(0x5000 + i) == second[i]);
    ++cases;
}

static void fault(unsigned vector, uint32 pc, uint32 address)
{
    expected_vector = vector;
    fault_pc = pc;
    fault_address = address;
    cpu_state.sr = 0x2000;
    flags(31);
    saved_sr = status();
    EM_A7 = 0x8000;
    trapped = 0;
    interpret_code(hash_lookup_code_and_create_if_needed(pc));
    assert(trapped == vector);
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
    for (unsigned mode = 0; mode < 5; ++mode)
        for (unsigned reg = 0; reg < 8; ++reg)
            for (unsigned other = 0; other < (mode == 4 ? 8u : 1u); ++other)
                for (unsigned low = 0; low < 16; ++low)
                    for (unsigned ccr = 0; ccr < 32; ++ccr)
                        transfer(mode, reg, other, low, (low * 7 + reg) & 15, ccr, 0);
    for (unsigned mode = 0; mode < 5; ++mode)
        for (unsigned low = 0; low < 16; ++low)
            for (unsigned dest_low = 0; dest_low < 16; ++dest_low)
                transfer(mode, 0, 1, low, dest_low, low ^ dest_low, 1);

    instruction(0x1000, 0, 0, 0, 0x601f);
    destroy_blocks(0x1000, 16);
    for (unsigned destination = 0x6000; destination <= 0x7000; destination += 0x1000) {
        cpu_state.sr = 0x2000;
        flags(31);
        cpu_state.regs[8].ul.n = 0x4013;
        WRITEUL(0x1002, destination + 15);
        for (unsigned i = 0; i < 32; ++i)
            WRITEUB(destination + i, 0);
        interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
        assert(status() == 0x201f && cpu_state.regs[8].ul.n == 0x4023);
        for (unsigned i = 0; i < 32; ++i)
            assert(READUB(destination + i) == (i < 16 ? READUB(0x4010 + i) : 0));
        ++cases;
    }
    uint8 unchanged[16];
    for (unsigned i = 0; i < 16; ++i)
        unchanged[i] = READUB(0x5010 + i);
    instruction(0x1000, 4, 0, 1, 0);
    destroy_blocks(0x1000, 16);
    cpu_state.regs[8].ul.n = 0xfffffff1;
    cpu_state.regs[9].ul.n = 0x501f;
    fault(2, 0x1000, 0xfffffff0);
    assert(cpu_state.regs[8].ul.n == 0xfffffff1 && cpu_state.regs[9].ul.n == 0x501f);
    cpu_state.regs[8].ul.n = 0x401f;
    cpu_state.regs[9].ul.n = 0x100001;
    fault(2, 0x1000, 0x100000);
    assert(cpu_state.regs[8].ul.n == 0x401f && cpu_state.regs[9].ul.n == 0x100001);
    DWORD protection;
    assert(VirtualProtect((uint8*)memory + 0xff000, 4096, PAGE_NOACCESS, &protection));
    syn68k_set_memory_window(0xff008, 0, 0);
    cpu_state.regs[8].ul.n = 0xff001;
    cpu_state.regs[9].ul.n = 0x501f;
    fault(2, 0x1000, 0xff000);
    assert(VirtualProtect((uint8*)memory + 0xff000, 4096, protection, &protection));
    syn68k_set_memory_window(1024 * 1024, 0, 0);
    for (unsigned i = 0; i < 16; ++i)
        assert(READUB(0x5010 + i) == unchanged[i]);
    for (unsigned bit = 0; bit < 16; ++bit)
        if (bit < 12 || bit == 15) {
            WRITEUW(0x1002, (0x9000u ^ (1u << bit)));
            fault(11, 0x1000, 0);
        }
    WRITEUW(0xffffe, 0xf620);
    fault(2, 0xffffe, 0x100000);
    WRITEUW(0xffffc, 0xf600);
    WRITEUW(0xffffe, 0);
    fault(2, 0xffffc, 0xffffe);
    // Native arithmetic flags survive MOVE16 and a subsequent SR transfer.
    cpu_state.sr = 0x2000;
    flags(31);
    EM_A7 = 0x8000;
    cpu_state.regs[1].ul.n = 0xdead0000;
    cpu_state.regs[8].ul.n = 0x4013;
    cpu_state.regs[9].ul.n = 0x5012;
    WRITEUW(0x1100, 0x203c);
    WRITEUL(0x1102, 0x7fffffff);
    WRITEUW(0x1106, 0x5280);
    WRITEUW(0x1108, 0xf620);
    WRITEUW(0x110a, 0x9000);
    WRITEUW(0x110c, 0x40c1);
    WRITEUW(0x110e, 0x4ef9);
    WRITEUL(0x1110, MAGIC_EXIT_EMULATOR_ADDRESS);
    destroy_blocks(0x1100, 32);
    trapped = 0;
    interpret_code(hash_lookup_code_and_create_if_needed(0x1100));
    assert(!trapped && status() == 0x200a);
    assert(cpu_state.regs[0].ul.n == 0x80000000 && cpu_state.regs[1].ul.n == 0xdead200a);
    assert(cpu_state.regs[8].ul.n == 0x4023 && cpu_state.regs[9].ul.n == 0x5022);
    ++cases;
    printf("%u MOVE16 line/alignment/register/CCR/user/fault cases passed (%s)\n", cases,
           native ? "native" : "interpreter");
    return 0;
}
