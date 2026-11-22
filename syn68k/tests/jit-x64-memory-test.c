/*+===================================================================
  File:      jit-x64-memory-test.c

  Summary:   Implements jit x64 memory test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main, memory_test_alloc

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Compare checked native memory tails with the independent synthetic engine. */
#include "jit-x64.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef TEST_NO_NATIVE
#define __declspec(x)
#define VirtualAlloc memory_test_alloc
#include "../src/runtime/jit-x64.c"

void* __stdcall memory_test_alloc(void* address, size_t size, unsigned long kind, unsigned long protection)
{
    return NULL;
}
#endif

static unsigned char* memory;
static unsigned cases;
static uint32 seed = 0x45273819;

static uint32 random_value(void)
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

typedef struct {
    uint32 registers[16];
    unsigned flags;
    unsigned char data[256];
} result;

static void capture(result* r)
{
    for (unsigned i = 0; i < 16; ++i)
        r->registers[i] = EM_DREG(i);
    r->flags =
        !!cpu_state.ccc | !!cpu_state.ccv << 1 | !!cpu_state.ccnz << 2 | !!cpu_state.ccn << 3 | !!cpu_state.ccx << 4;
    memcpy(r->data, memory + 0x4000, sizeof r->data);
}

static unsigned extension(unsigned pc, unsigned ea, unsigned sample)
{
    switch (ea >> 3) {
    case 5:
        WRITEUW(pc, (int16)((sample & 31) * 4 - 64));
        return pc + 2;
    case 6:
        if (sample < 8) {
            WRITEUW(pc, (sample < 4 ? 0x7010 : 0x7810) | (sample & 3) << 9);
            return pc + 2;
        }
        if (sample < 16) {
            WRITEUW(pc, 0x7920);
            WRITEUW(pc + 2, 16);
            return pc + 4;
        }
        if (sample < 24) {
            WRITEUW(pc, 0x7930);
            WRITEUL(pc + 2, 16);
            return pc + 6;
        }
        WRITEUW(pc, 0x7922); /* Preindexed indirect, word outer displacement. */
        WRITEUW(pc + 2, 16);
        WRITEUW(pc + 4, 8);
        WRITEUL(EM_AREG(ea & 7) + EM_D7 + 16, 0x4040);
        return pc + 6;
    case 7:
        if ((ea & 7) == 3) {
            unsigned scale = sample & 3;
            EM_D7 = (0x4040 - pc) >> scale;
            WRITEUW(pc, (sample & 4 ? 0x7800 : 0x7000) | scale << 9 | (0x4040 - pc - (EM_D7 << scale)));
            return pc + 2;
        }
        if ((ea & 7) == 1) {
            WRITEUL(pc, 0x4040);
            return pc + 4;
        }
        WRITEUW(pc, (ea & 7) == 2 ? 0x4040 - pc : 0x4040);
        return pc + 2;
    default:
        return pc;
    }
}

static void compare(unsigned opcode, unsigned ea, unsigned sample)
{
    assert(syn68k_x64_memory_opcode(opcode));
    uint32 regs[16];
    unsigned char bytes[256];
    for (unsigned i = 0; i < 16; ++i)
        regs[i] = i < 8 ? random_value() : 0x4080;
    regs[7] = (int32)(sample & 7) - 4;
    if ((ea >> 3) == 6 && sample < 4)
        regs[7] = 0x12340000 | (uint16)regs[7];
    for (unsigned i = 0; i < sizeof bytes; ++i)
        bytes[i] = random_value();
    result reference = {0}, actual = {0};
    for (unsigned backend = 0; backend < 3; ++backend) {
        unsigned native = backend != 0;
#ifdef TEST_NO_NATIVE
        if (backend == 2)
            continue;
#endif
        syn68k_x64_enabled = native;
        destroy_blocks(0x1000, 64);
        for (unsigned i = 0; i < 16; ++i)
            EM_DREG(i) = regs[i];
        memcpy(memory + 0x4000, bytes, sizeof bytes);
        cpu_state.ccc = sample & 1;
        cpu_state.ccv = sample & 2;
        cpu_state.ccnz = sample & 4;
        cpu_state.ccn = sample & 8;
        cpu_state.ccx = sample & 16;
        cpu_state.sr = 0;
        WRITEUW(0x1000, opcode);
        unsigned pc = 0x1002;
        if ((opcode & 0xF100) == 0x0000 && (opcode & 0xFF00) <= 0x0C00) {
            unsigned size = (opcode >> 6) & 3;
            if ((opcode & 0xFF00) == 0x0800)
                size = 0;
            if (size == 2) {
                WRITEUL(pc, sample * 0x1234567U);
                pc += 4;
            }
            else {
                WRITEUW(pc, sample * 37);
                pc += 2;
            }
        }
        pc = extension(pc, ea, sample);
        if ((opcode >> 12) >= 1 && (opcode >> 12) <= 3 && (opcode & 63) >= 16) {
            unsigned dest = ((opcode >> 3) & 56) | ((opcode >> 9) & 7);
            if (dest >= 16)
                pc = extension(pc, dest, sample);
        }
        WRITEUW(pc, 0x4EF9);
        WRITEUL(pc + 2, MAGIC_EXIT_EMULATOR_ADDRESS);
        if (backend == 2 && !syn68k_x64_fast_memory_opcode(opcode, 0x1000))
            continue;
        uint64 count = syn68k_native_execution_count();
        if (backend == 2) {
            int8 lengths[] = {(pc - 0x1000) / 2, 1};
            unsigned translated;
            int terminal;
            void* code = syn68k_x64_compile(SYN68K_TO_US(0x1000), lengths, 2, &translated, 0x1000, &terminal);
            assert(code && translated == 2 && terminal);
            assert(syn68k_x64_execute(code) == MAGIC_EXIT_EMULATOR_ADDRESS);
            syn68k_x64_release(code);
        }
        else
            interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
        if (native)
#ifdef TEST_NO_NATIVE
            assert(syn68k_native_execution_count() == count);
#else
            assert(syn68k_native_execution_count() > count);
#endif
        capture(native ? &actual : &reference);
        if (native && memcmp(&reference, &actual, sizeof actual)) {
            fprintf(stderr, "memory mismatch op=%04X ea=%02X sample=%u flags=%X/%X\n", opcode, ea, sample,
                    reference.flags, actual.flags);
            for (unsigned i = 0; i < 16; ++i)
                if (reference.registers[i] != actual.registers[i])
                    fprintf(stderr, "r%u=%08X/%08X\n", i, reference.registers[i], actual.registers[i]);
            abort();
        }
    }
    ++cases;
}

static uint32 fault_pc, fault_address;
static unsigned faults;

static syn68k_addr_t fault_handler(syn68k_addr_t pc, void* argument)
{
    assert(pc == fault_pc);
    assert(READUW(EM_A7 + 6) == 0x7008);
    assert(READUL(EM_A7 + 20) == fault_address);
    ++faults;
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static void bounded_cases(void)
{
    syn68k_x64_enabled = 1;
    trap_install_handler(2, fault_handler, NULL);
    for (unsigned kind = 0; kind < 5; ++kind) {
        destroy_blocks(0, UINT32_MAX);
        syn68k_set_memory_window(0x10000, 0, 0);
        cpu_state.sr = 0x2000;
        EM_A7 = cpu_state.isp = 0xF000;
        EM_D0 = 0x12345678;
        cpu_state.ccc = cpu_state.ccn = cpu_state.ccx = 1;
        cpu_state.ccv = cpu_state.ccnz = 0;
        fault_pc = kind == 0 ? 0xFFFE : 0x1000;
        fault_address = 0x10000;
        WRITEUW(fault_pc, kind == 0 ? 0x2039 : kind == 1 ? 0x2080 : kind == 2 ? 0x2010 : kind == 3 ? 0x06B9 : 0x5290);
        /* Missing absolute EA, partial longword store/read, or faulting
         destination after a complete immediate longword. */
        EM_A0 = 0xFFFE;
        WRITEUW(0xFFFC, 0xAABB);
        if (kind != 0)
            WRITEUW(0xFFFE, 0xCCDD);
        if (kind == 3) {
            WRITEUL(0x1002, 1);
            WRITEUL(0x1006, 0xFFFF);
        }
        if (kind == 4) {
            WRITEUW(0x1002, 0x4EF9);
            WRITEUL(0x1004, MAGIC_EXIT_EMULATOR_ADDRESS);
        }
        interpret_code(hash_lookup_code_and_create_if_needed(fault_pc));
        assert(faults == kind + 1);
        assert(EM_D0 == 0x12345678);
        assert(cpu_state.ccc && cpu_state.ccn && cpu_state.ccx && !cpu_state.ccv && !cpu_state.ccnz);
        assert(READUW(0xFFFC) == 0xAABB);
        if (kind)
            assert(READUW(0xFFFE) == 0xCCDD);
    }

    /* An allocation-failure fallback must retain the flags preceding a fault,
     even if a later static branch target overwrites all flags. */
    destroy_blocks(0, UINT32_MAX);
    WRITEUW(0x1000, 0x5280);
    WRITEUW(0x1002, 0x5291);
    WRITEUW(0x1004, 0x5290);
    WRITEUW(0x1006, 0x6002);
    WRITEUW(0x1008, 0x4E71);
    WRITEUW(0x100A, 0x5280);
    WRITEUW(0x100C, 0x4EF9);
    WRITEUL(0x100E, MAGIC_EXIT_EMULATOR_ADDRESS);
    EM_D0 = UINT32_MAX;
    EM_A0 = 0x4000;
    EM_A1 = 0xFFFE;
    EM_A7 = cpu_state.isp = 0xF000;
    cpu_state.sr = 0x2000;
    cpu_state.ccc = cpu_state.ccx = cpu_state.ccv = cpu_state.ccn = 0;
    cpu_state.ccnz = 1;
    fault_pc = 0x1002;
    fault_address = 0x10000;
    interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
    assert(faults == 6 && !EM_D0);
    assert((READUW(EM_A7) & 31) == 0x15);

    /* Change an absolute EA extension without invalidating the opcode. */
    destroy_blocks(0, UINT32_MAX);
    WRITEUW(0x1000, 0x2039);
    WRITEUL(0x1002, 0x4000);
    WRITEUW(0x1006, 0x4EF9);
    WRITEUL(0x1008, MAGIC_EXIT_EMULATOR_ADDRESS);
    WRITEUL(0x4000, 42);
    WRITEUL(0x4004, 73);
    interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
    assert(EM_D0 == 42);
    WRITEUL(0x1002, 0x4004);
    interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
    assert(EM_D0 == 73);

    /* The store destroys the native page that tail-called the helper. */
    destroy_blocks(0, UINT32_MAX);
    WRITEUW(0x1000, 0x33FC); /* MOVE.W #$4E71,$1000 */
    WRITEUW(0x1002, 0x4E71);
    WRITEUL(0x1004, 0x1000);
    WRITEUW(0x1008, 0x4EF9);
    WRITEUL(0x100A, MAGIC_EXIT_EMULATOR_ADDRESS);
    interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
    assert(READUW(0x1000) == 0x4E71);
    destroy_blocks(0, UINT32_MAX);
    WRITEUW(0x1000, 0x33FC); /* Rewrite the following store as NOP. */
    WRITEUW(0x1002, 0x4E71);
    WRITEUL(0x1004, 0x1008);
    WRITEUW(0x1008, 0x2080); /* MOVE.L D0,(A0) must not run. */
    WRITEUW(0x100A, 0x4EF9);
    WRITEUL(0x100C, MAGIC_EXIT_EMULATOR_ADDRESS);
    EM_A0 = 0x4000;
    WRITEUL(0x4000, 77);
    interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
    assert(READUL(0x4000) == 77);

    destroy_blocks(0, UINT32_MAX);
    for (unsigned i = 0; i < 70; ++i)
        WRITEUW(0x1000 + i * 2, 0x5290); /* ADDQ.L #1,(A0) */
    WRITEUL(0x4000, 0);
    cpu_state.sr = 0x2000;
    SET_INTERRUPT_STATUS(INTERRUPT_STATUS_UNCHANGED);
    assert(syn68k_x64_memory_run(0x1000, 0x5290) == 0x1080);
    assert(READUL(0x4000) == 64);
    SET_INTERRUPT_STATUS(INTERRUPT_STATUS_CHANGED);
    assert(syn68k_x64_memory_run(0x1000, 0x5290) == 0x1002);
    assert(READUL(0x4000) == 65);
    SET_INTERRUPT_STATUS(INTERRUPT_STATUS_UNCHANGED);
    cpu_state.sr |= 0x8000;
    assert(syn68k_x64_memory_run(0x1000, 0x5290) == 0x1002);
    assert(READUL(0x4000) == 66);
    puts("Checked faults, atomic stores, live extensions and self-invalidation passed.");
}

int main(void)
{
    memory = calloc(1, 0x10000);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(0x10000, 0, 0);
    uint32 vectors[64] = {0};
    initialize_68k_emulator(NULL, 1, vectors, 0);
    static const unsigned eas[] = {16, 24, 32, 40, 48, 56, 57, 23, 31, 39};
    static const unsigned alu[] = {0x8000, 0x9000, 0xB000, 0xC000, 0xD000, 0x8100, 0x9100, 0xB100,
                                   0xC100, 0xD100, 0x0000, 0x0200, 0x0400, 0x0600, 0x0A00, 0x0C00,
                                   0x4000, 0x4200, 0x4400, 0x4600, 0x4A00, 0x5000, 0x5100};
    for (unsigned size = 0; size < 3; ++size)
        for (unsigned i = 0; i < sizeof alu / sizeof *alu; ++i)
            for (unsigned j = 0; j < sizeof eas / sizeof *eas; ++j)
                for (unsigned sample = 0; sample < 32; ++sample)
                    compare(alu[i] | size << 6 | eas[j], eas[j], sample);
    for (unsigned i = 0; i < 16; ++i)
        for (unsigned j = 0; j < sizeof eas / sizeof *eas; ++j)
            for (unsigned sample = 0; sample < 32; ++sample)
                compare(0x50C0 | i << 8 | eas[j], eas[j], sample);
    for (unsigned kind = 0; kind < 8; ++kind)
        for (unsigned j = 0; j < sizeof eas / sizeof *eas; ++j)
            for (unsigned sample = 0; sample < 32; ++sample) {
                compare(0xE0C0 | kind << 8 | eas[j], eas[j], sample);
                compare((kind < 4 ? 0x0100 : 0x0800) | (kind & 3) << 6 | eas[j], eas[j], sample);
            }
    for (unsigned j = 0; j < sizeof eas / sizeof *eas; ++j)
        for (unsigned sample = 0; sample < 32; ++sample)
            compare(0x4AC0 | eas[j], eas[j], sample);
    /* PC-relative EAs are readable, never writable. */
    static const unsigned reads[] = {0x8000, 0x9000, 0xB000, 0xC000, 0xD000, 0x4A00};
    for (unsigned ea = 58; ea <= 59; ++ea)
        for (unsigned size = 0; size < 3; ++size)
            for (unsigned sample = 0; sample < 32; ++sample) {
                for (unsigned i = 0; i < sizeof reads / sizeof *reads; ++i)
                    compare(reads[i] | size << 6 | ea, ea, sample);
                compare((size == 0 ? 0x1000 : size == 1 ? 0x3000 : 0x2000) | ea, ea, sample);
                if (size) {
                    compare((size == 2 ? 0x2040 : 0x3040) | ea, ea, sample);
                    compare(0x90C0 | (size == 2 ? 0x100 : 0) | ea, ea, sample);
                    compare(0xB0C0 | (size == 2 ? 0x100 : 0) | ea, ea, sample);
                    compare(0xD0C0 | (size == 2 ? 0x100 : 0) | ea, ea, sample);
                }
            }
    for (unsigned ea = 58; ea <= 59; ++ea)
        for (unsigned sample = 0; sample < 32; ++sample)
            compare(0x0100 | ea, ea, sample);
    for (unsigned size = 0; size < 3; ++size)
        for (unsigned j = 0; j < sizeof eas / sizeof *eas; ++j)
            for (unsigned sample = 0; sample < 32; ++sample) {
                unsigned base = size == 0 ? 0x1000 : size == 1 ? 0x3000 : 0x2000;
                unsigned dest = (eas[j] & 7) << 9 | (eas[j] & 56) << 3;
                compare(base | eas[j], eas[j], sample);
                compare(base | dest, eas[j], sample);
                compare(base | dest | eas[j], eas[j], sample);
                compare(0xD108 | size << 6, 0, sample);
                compare(0x9308 | size << 6, 0, sample);
                compare(0xB308 | size << 6, 0, sample);
                if (size) {
                    compare((size == 2 ? 0x2040 : 0x3040) | eas[j], eas[j], sample);
                    compare(0x90C0 | (size == 2 ? 0x100 : 0) | eas[j], eas[j], sample);
                    compare(0xB0C0 | (size == 2 ? 0x100 : 0) | eas[j], eas[j], sample);
                    compare(0xD0C0 | (size == 2 ? 0x100 : 0) | eas[j], eas[j], sample);
                }
            }
    printf("%u checked memory differential cases passed.\n", cases);
    bounded_cases();
    return 0;
}
