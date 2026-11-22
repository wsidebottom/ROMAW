/*+===================================================================
  File:      jit-x64-displacement-test.c

  Summary:   Implements jit x64 displacement test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Live displacement/absolute operands, guarded exits and self-modification. */
#include "jit-x64.h"
#include "hash.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static uint32 fault_pc, fault_address;
static unsigned faults;

static syn68k_addr_t fault_handler(syn68k_addr_t pc, void* argument)
{
    assert(pc == fault_pc && READUW(EM_A7 + 6) == 0x7008);
    assert(READUL(EM_A7 + 20) == fault_address);
    ++faults;
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static void* compile(uint32 pc, unsigned op, unsigned words)
{
    WRITEUW(pc, op);
    for (unsigned i = 1; i < words; ++i)
        WRITEUW(pc + i * 2, 0);
    WRITEUW(pc + words * 2, 0x4EF9);
    WRITEUL(pc + words * 2 + 2, MAGIC_EXIT_EMULATOR_ADDRESS);
    int8 lengths[] = {words, 1};
    unsigned translated;
    int terminal;
    assert(syn68k_x64_fast_memory_opcode(op, pc) == words);
    void* code = syn68k_x64_compile(SYN68K_TO_US(pc), lengths, 2, &translated, pc, &terminal);
    assert(code && translated == 2 && terminal);
    return code;
}

static unsigned indexed_cases(void)
{
    unsigned cases = 0;
    const int edges[] = {-32768, -129, -1, 0, 1, 32767};
    syn68k_address_mask = UINT32_MAX;
    syn68k_set_memory_window(0x1000000, 0, 0);
    destroy_blocks(0, UINT32_MAX);
    for (unsigned relative = 0; relative < 2; ++relative)
        for (unsigned index = 0; index < 16; ++index)
            for (unsigned wide = 0; wide < 2; ++wide)
                for (unsigned scale = 0; scale < 4; ++scale)
                    for (unsigned sample = 0; sample < sizeof edges / sizeof *edges; ++sample) {
                        uint32 pc = relative ? 0x80000 : 0x1000;
                        for (unsigned reg = 0; reg < 16; ++reg)
                            EM_DREG(reg) = 0;
                        EM_A0 = 0x80000;
                        if (index != 8)
                            EM_DREG(index) = wide ? (uint32)edges[sample] : 0x5A5A0000 | (uint16)edges[sample];
                        int displacement = sample & 1 ? 127 : -128;
                        uint32 value = EM_DREG(index);
                        if (!wide)
                            value = (int16)value;
                        uint32 address = (relative ? pc + 2 : EM_A0) + (value << scale) + displacement;
                        if (address < pc + 10 && address + 4 > pc)
                            continue;
                        void* code = compile(pc, relative ? 0x203B : 0x2030, 2);
                        WRITEUW(pc + 2, index << 12 | wide << 11 | scale << 9 | (uint8)displacement);
                        uint32 expected = 0x12345678 + cases;
                        WRITEUL(address, expected);
                        uint32 next = (address & 4095) > 4092 ? pc + 4 : MAGIC_EXIT_EMULATOR_ADDRESS;
                        assert(syn68k_x64_execute(code) == next);
                        assert(EM_D0 == expected && EM_A0 == 0x80000);
                        syn68k_x64_release(code);
                        ++cases;
                    }
    /* An extension changed from brief to full must return the actual next PC,
     bypassing the old two-word prefix layout and its cached following JMP. */
    void* code = compile(0x1000, 0x2030, 2);
    WRITEUW(0x1002, 0x7810);
    EM_A0 = 0x4000;
    EM_D7 = 4;
    WRITEUL(0x4014, 42);
    assert(syn68k_x64_execute(code) == MAGIC_EXIT_EMULATOR_ADDRESS);
    assert(EM_D0 == 42);
    WRITEUW(0x1002, 0x7920);
    WRITEUW(0x1004, 16);
    WRITEUW(0x1006, 0x4EF9);
    WRITEUL(0x1008, MAGIC_EXIT_EMULATOR_ADDRESS);
    assert(syn68k_x64_execute(code) == 0x1006 && EM_D0 == 42);
    syn68k_x64_release(code);
    destroy_blocks(0, UINT32_MAX);
    WRITEUW(0x1002, 0x7810);
    WRITEUW(0x1004, 0x4EF9);
    WRITEUL(0x1006, MAGIC_EXIT_EMULATOR_ADDRESS);
    interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
    assert(EM_D0 == 42);
    WRITEUW(0x1002, 0x7920);
    WRITEUW(0x1004, 16);
    WRITEUW(0x1006, 0x4EF9);
    WRITEUL(0x1008, MAGIC_EXIT_EMULATOR_ADDRESS);
    interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
    assert(EM_D0 == 42);
    return cases + 2;
}

int main(void)
{
    void* ram = calloc(1, 0x1000000);
    assert(ram);
    ROMlib_offset = (uintptr_t)ram;
    syn68k_set_memory_window(0x20000, 0, 0);
    /* Keep immortal callback blocks stable while testing alias-window changes. */
    syn68k_callback_address_space = (uint16*)((uint8*)ram + 0x18000);
    uint32 vectors[64] = {0};
    initialize_68k_emulator(NULL, 1, vectors, 0);
    unsigned cases = 0;
    const int displacements[] = {-32768, -65, -1, 0, 1, 63, 32767};
    for (unsigned reg = 0; reg < 8; ++reg)
        for (unsigned i = 0; i < sizeof displacements / sizeof *displacements; ++i) {
            void* code = compile(0x1000, 0x2028 | reg, 2);
            EM_AREG(reg) = 0xC000;
            WRITEUW(0x1002, displacements[i]);
            WRITEUL(0xC000 + displacements[i], 0x12345678 + i);
            uint32 first_address = 0xC000 + displacements[i];
            uint32 next = (first_address & 4095) > 4092 ? 0x1004 : MAGIC_EXIT_EMULATOR_ADDRESS;
            assert(syn68k_x64_execute(code) == next);
            assert(EM_D0 == 0x12345678 + i && EM_AREG(reg) == 0xC000);
            WRITEUW(0x1002, displacements[i] + (i == 6 ? -4 : 4));
            uint32 address = 0xC000 + displacements[i] + (i == 6 ? -4 : 4);
            WRITEUL(address, 0xABCDEF00 + i);
            assert(syn68k_x64_execute(code) == MAGIC_EXIT_EMULATOR_ADDRESS);
            assert(EM_D0 == 0xABCDEF00 + i);
            syn68k_x64_release(code);
            ++cases;
        }
    for (unsigned i = 0; i < sizeof displacements / sizeof *displacements; ++i) {
        void* code = compile(0x9000, 0x203A, 2);
        WRITEUW(0x9002, displacements[i]);
        uint32 address = 0x9002 + displacements[i];
        /* Reads overlapping the instruction use its actual bytes as data. */
        if (address + 4 <= 0x9000 || address >= 0x900A)
            WRITEUL(address, 0x55667788);
        uint32 expected = READUL(0x9002 + displacements[i]);
        assert(syn68k_x64_execute(code) == MAGIC_EXIT_EMULATOR_ADDRESS);
        assert(EM_D0 == expected);
        syn68k_x64_release(code);
        ++cases;
    }
    /* A long extension can itself cross a page; a checked exit returns the next
     guest PC, while a fully native execution also consumes the following JMP. */
    void* code = compile(0x1FFC, 0x2039, 3);
    WRITEUL(0x1FFE, 0x4000);
    WRITEUL(0x4000, 0xDEADBEEF);
    assert(syn68k_x64_execute(code) == 0x2002 && EM_D0 == 0xDEADBEEF);
    syn68k_x64_release(code);
    ++cases;

    trap_install_handler(2, fault_handler, NULL);
    for (unsigned kind = 0; kind < 3; ++kind) {
        syn68k_set_memory_window(kind ? 0x20000 : 0x20010, 0, 0);
        fault_pc = kind ? 0x1000 : 0x1FFF8;
        fault_address = kind ? 0x20000 : 0x1FFFC;
        code = compile(fault_pc, kind == 1 ? 0x23C0 : 0x2039, 3);
        WRITEUL(fault_pc + 2, 0x1FFFE);
        if (kind)
            WRITEUW(0x1FFFE, 0xAABB);
        /* Change the window after compiling to force an extension fetch fault. */
        if (!kind)
            syn68k_set_memory_window(0x1FFFC, 0, 0);
        EM_D0 = 0x12345678;
        EM_A7 = cpu_state.isp = 0xF000;
        cpu_state.sr = 0x2000;
        cpu_state.ccc = cpu_state.ccx = cpu_state.ccn = 1;
        cpu_state.ccv = cpu_state.ccnz = 0;
        assert(syn68k_x64_execute(code) == MAGIC_EXIT_EMULATOR_ADDRESS);
        assert(faults == kind + 1 && EM_D0 == 0x12345678);
        assert((READUW(EM_A7) & 31) == 0x1D);
        assert(READUW(kind ? 0x1FFFE : 0x1FFFA) == (kind ? 0xAABB : 1));
        syn68k_x64_release(code);
        ++cases;
    }
    /* A guarded absolute store must tail-call before freeing its executing page. */
    syn68k_set_memory_window(0x20000, 0, 0);
    destroy_blocks(0, UINT32_MAX);
    WRITEUW(0x1000, 0x23C0);
    WRITEUL(0x1002, 0x1000);
    WRITEUW(0x1006, 0x4EF9);
    WRITEUL(0x1008, MAGIC_EXIT_EMULATOR_ADDRESS);
    EM_D0 = 0x4E714E71;
    interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
    assert(READUL(0x1000) == EM_D0 && !hash_lookup(0x1000));
    ++cases;

    destroy_blocks(0, UINT32_MAX);
    syn68k_set_memory_window(0x20000, 0x40000, 0x40000);
    void* host = calloc(1, 0x20000);
    assert(host);
    uint32 alias = US_TO_SYN68K(host);
    WRITEUL(alias + 16, 0x11223344);
    code = compile(0x1000, 0x2028, 2);
    WRITEUW(0x1002, 16);
    EM_A0 = alias;
    assert(syn68k_x64_execute(code) == 0x1004 && EM_D0 == 0x11223344);
    syn68k_x64_release(code);
    ++cases;
    code = compile(alias + 64, 0x2039, 3);
    WRITEUL(alias + 66, 0x4000);
    WRITEUL(0x4000, 0x55667788);
    assert(syn68k_x64_execute(code) == alias + 70 && EM_D0 == 0x55667788);
    syn68k_x64_release(code);
    ++cases;

    syn68k_address_mask = 0xFFFFFF;
    syn68k_set_memory_window(0x1000000, 0, 0);
    code = compile(0x1000, 0x2038, 2);
    WRITEUW(0x1002, 0xFFFC);
    WRITEUL(0xFFFFFC, 0x10203040);
    assert(syn68k_x64_execute(code) == MAGIC_EXIT_EMULATOR_ADDRESS);
    assert(EM_D0 == 0x10203040);
    syn68k_x64_release(code);
    ++cases;
    code = compile(0x1000, 0x2028, 2);
    WRITEUW(0x1002, (uint16)-16);
    EM_A0 = 0xAB004010;
    WRITEUL(0x4000, 0x50607080);
    assert(syn68k_x64_execute(code) == MAGIC_EXIT_EMULATOR_ADDRESS);
    assert(EM_D0 == 0x50607080 && EM_A0 == 0xAB004010);
    syn68k_x64_release(code);
    ++cases;
    unsigned indexed = indexed_cases();
    printf("%u live displacement/absolute/PC-relative and fault cases passed.\n", cases);
    printf("%u brief indexed register/scale/sign/live-format cases passed.\n", indexed);
    return 0;
}
