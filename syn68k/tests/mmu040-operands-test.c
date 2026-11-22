/*+===================================================================
  File:      mmu040-operands-test.c

  Summary:   Implements mmu040 operands test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Mapped scalar/complex operands versus independent synthetic execution. */
#include "syn68k_public.h"
#include "integer.h"
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char ram[0x20000];
static Syn68kMMU040 mmu;
static unsigned cases, reads, writes, page_size, supervisor;
static uint32_t fail_physical;

static void put32(uint32_t address, uint32_t value)
{
    assert(address <= sizeof ram - 4);
    for (unsigned i = 0; i < 4; ++i)
        ram[address + i] = value >> (24 - i * 8);
}

static int table_read(void* opaque, uint32_t address, uint32_t* value)
{
    assert(opaque == &mmu);
    if (address > sizeof ram - 4)
        return 0;
    *value = (uint32_t)ram[address] << 24 | (uint32_t)ram[address + 1] << 16 | (uint32_t)ram[address + 2] << 8 |
             ram[address + 3];
    return 1;
}

static int table_write(void* opaque, uint32_t address, uint32_t value, int locked)
{
    uint32_t old;
    if (!table_read(opaque, address, &old))
        return 0;
    put32(address, locked ? old | value : value);
    return 1;
}

static int operand_read(void* opaque, uint32_t physical, unsigned char* bytes, unsigned size, uint32_t attributes,
                        unsigned fc)
{
    assert(opaque == ram);
    (void)attributes;
    (void)fc;
    ++reads;
    if (physical == fail_physical || physical > sizeof ram - size)
        return 0;
    memcpy(bytes, ram + physical, size);
    return 1;
}

static int operand_write(void* opaque, uint32_t physical, const unsigned char* bytes, unsigned size,
                         uint32_t attributes, unsigned fc)
{
    assert(opaque == ram);
    (void)attributes;
    (void)fc;
    ++writes;
    if (physical == fail_physical || physical > sizeof ram - size)
        return 0;
    memcpy(ram + physical, bytes, size);
    return 1;
}

static Syn68kMMU040Memory memory = {.mmu = &mmu,
                                    .tables = {&mmu, table_read, table_write},
                                    .opaque = ram,
                                    .read = operand_read,
                                    .write = operand_write,
                                    .native = NULL};

static uint32_t pte(uint32_t logical)
{
    return (supervisor ? 0x6000 : 0x3000) + (logical / page_size) * 4;
}

/* Independent literal mappings; these do not invoke the production walker. */
static uint32_t physical(uint32_t logical)
{
    if (logical >= 0x20000) {
        unsigned offset = logical - 0x20000;
        return (supervisor ? 0x18000 : 0x10000) + (offset < page_size ? offset : 0x4000 + offset - page_size);
    }
    unsigned offset = logical - 0x10000;
    return (supervisor ? 0xc000 : 0x8000) + (offset < page_size ? offset : 0x2000 + offset - page_size);
}

static void code_word(uint32_t logical, unsigned value)
{
    ram[physical(logical)] = value >> 8;
    ram[physical(logical + 1)] = value;
}

static void fixture(unsigned page8k, unsigned privileged)
{
    page_size = page8k ? 8192 : 4096;
    supervisor = privileged;
    syn68k_mmu040_init(&mmu);
    memset(ram + 0x1000, 0, 512);
    memset(ram + 0x2000, 0, 512);
    memset(ram + 0x3000, 0, 256);
    memset(ram + 0x4000, 0, 512);
    memset(ram + 0x5000, 0, 512);
    memset(ram + 0x6000, 0, 256);
    mmu.tc = page8k ? 0xc000 : 0x8000;
    mmu.urp = 0x1000;
    mmu.srp = 0x4000;
    put32(0x1000, 0x2002);
    put32(0x2000, 0x3002);
    put32(0x4000, 0x5002);
    put32(0x5000, 0x6002);
    put32(pte(0x10000), (supervisor ? 0xc000 : 0x8000) | 1);
    put32(pte(0x10000 + page_size), (supervisor ? 0xe000 : 0xa000) | 1);
    put32(pte(0x20000), (supervisor ? 0x18000 : 0x10000) | 1);
    put32(pte(0x20000 + page_size), (supervisor ? 0x1c000 : 0x14000) | 1);
    cpu_state.sr = supervisor ? 0x2000 : 0;
    EM_A7 = 0x76543210; // Stepping must not build a flat exception stack here.
    reads = writes = 0;
    fail_physical = 0xffffffff;
}

static unsigned char* flat_ram;
static unsigned operand_pattern;

typedef struct {
    uint32_t regs[16];
    unsigned flags;
    unsigned char data[24];
} Snapshot;

static void state(unsigned offset, unsigned privileged)
{
    cpu_state.sr = privileged ? 0x2000 : 0;
    for (unsigned r = 0; r < 16; ++r)
        EM_DREG(r) = 0x12345678U ^ r * 0x11998877U ^ offset;
    if (operand_pattern) {
        static const uint32_t values[] = {0, 1, 0x7F, 0x80, 0x7FFF, 0x8000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF};
        for (unsigned r = 0; r < 8; ++r)
            EM_DREG(r) = values[(operand_pattern + r - 1) % 9];
        EM_D1 = offset & 63; /* Every variable shift count, including zero. */
    }
    EM_A0 = 0x20000 + offset;
    EM_A1 = 0x20000 + offset + 4;
    unsigned f = offset & 31;
    cpu_state.ccc = f & 1 ? 0x80000000 : 0;
    cpu_state.ccv = f & 2 ? 0x87654321 : 0;
    cpu_state.ccnz = f & 4 ? 0 : 0x80008000;
    cpu_state.ccn = f & 8 ? 0x12345678 : 0;
    cpu_state.ccx = f & 16 ? 0x88888888 : 0;
}

static void capture(Snapshot* out, unsigned address, int mapped)
{
    for (unsigned r = 0; r < 16; ++r)
        out->regs[r] = EM_DREG(r);
    out->flags = (!!cpu_state.ccc) | ((!!cpu_state.ccv) << 1) | ((!cpu_state.ccnz) << 2) | ((!!cpu_state.ccn) << 3) |
                 ((!!cpu_state.ccx) << 4);
    for (unsigned i = 0; i < sizeof out->data; ++i)
        out->data[i] = mapped ? ram[physical(address - 8 + i)] : flat_ram[address - 8 + i];
}

static unsigned instruction(unsigned pc, unsigned op, int mapped)
{
    unsigned words[5] = {op}, count = 1;
    if ((op & 0xF9C0) == 0x08C0 && (op & 0x600)) {
        words[count++] = 0x8081;
        if (op == 0x0CFC || op == 0x0EFC)
            words[count++] = 0x9103;
    }
    else if ((op & 0xF8C0) == 0xE8C0)
        words[count++] = 0x1080 | 31; /* D1, offset 2, width 31; five bytes. */
    else if ((op & 0xF1F0) == 0x8140 || (op & 0xF1F0) == 0x8180)
        words[count++] = 0x1234;
    else if ((op & 0xFF00) <= 0x0C00 && ((op >> 6) & 3) < 3 &&
             ((op & 0xFF00) == 0 || (op & 0xFF00) == 0x0200 || (op & 0xFF00) == 0x0400 || (op & 0xFF00) == 0x0600 ||
              (op & 0xFF00) == 0x0A00 || (op & 0xFF00) == 0x0C00)) {
        words[count++] = 0x1234;
        if (((op >> 6) & 3) == 2)
            words[count++] = 0x5678;
    }
    else if ((op & 0xFF00) == 0x0800)
        words[count++] = 31;
    for (unsigned i = 0; i < count; ++i)
        if (mapped)
            code_word(pc + i * 2, words[i]);
        else
            WRITEUW(pc + i * 2, words[i]);
    return pc + count * 2;
}

static void compare(unsigned op, unsigned offset, unsigned privileged, unsigned page8k)
{
    fixture(page8k, privileged);
    unsigned address = 0x20000 + offset, pc = 0x10000;
    Snapshot expected = {0}, actual = {0};
    for (unsigned mode = 0; mode < 2; ++mode) {
        state(offset, privileged);
        for (unsigned i = 0; i < sizeof expected.data; ++i)
            if (mode)
                ram[physical(address - 8 + i)] = (offset * 17 + i * 31) & 255;
            else
                flat_ram[address - 8 + i] = (offset * 17 + i * 31) & 255;
        unsigned next = instruction(pc, op, mode);
        if ((op & 0xF9C0) == 0x08C0 && (op & 0x600)) {
            unsigned size = (op & 0x600) == 0x200 ? 1 : (op & 0x600) == 0x400 ? 2 : 4;
            uint32_t first = 0, second = 0;
            for (unsigned i = 0; i < size; ++i) {
                first = first << 8 | (mode ? ram[physical(address + i)] : flat_ram[address + i]);
                second = second << 8 | (mode ? ram[physical(address + 4 + i)] : flat_ram[address + 4 + i]);
            }
            if (offset & 1)
                EM_D1 = first;
            if (offset & 2)
                EM_D3 = second;
        }
        if (!mode) {
            destroy_blocks(pc, 32);
            WRITEUW(next, 0x4EF9);
            WRITEUL(next + 2, MAGIC_EXIT_EMULATOR_ADDRESS);
            interpret_code(hash_lookup_code_and_create_if_needed(pc));
            capture(&expected, address, 0);
        }
        else {
            Syn68kIntegerStep result;
            assert(syn68k_integer_step(pc, &memory, &result));
            assert(!result.vector && result.next == next);
            capture(&actual, address, 1);
            if (memcmp(&expected, &actual, sizeof actual)) {
                fprintf(stderr, "mapped mismatch op=%04X offset=%u page8k=%u S=%u flags=%X/%X\n", op, offset, page8k,
                        privileged, expected.flags, actual.flags);
                abort();
            }
        }
    }
    ++cases;
}

static void fault_cases(void)
{
    for (unsigned privileged = 0; privileged < 2; ++privileged)
        for (unsigned page8k = 0; page8k < 2; ++page8k)
            for (unsigned writing = 0; writing < 2; ++writing) {
                fixture(page8k, privileged);
                unsigned address = 0x20000 + page_size - 2;
                code_word(0x10000, writing ? 0x20C0 : 0x2018);
                state(31, privileged);
                EM_A0 = address;
                EM_A7 = 0xFEDCBA98; /* No flat exception stack may be constructed. */
                EM_D0 = 0x12345678;
                put32(pte(0x20000 + page_size), 0);
                ram[physical(address)] = 0xA5;
                ram[physical(address + 1)] = 0x5A;
                Syn68kIntegerStep result;
                assert(syn68k_integer_step(0x10000, &memory, &result));
                assert(result.vector == 2 && result.format == 7 && result.exception_pc == 0x10000 &&
                       result.fault_address == 0x20000 + page_size);
                assert(result.access.completed == 2 && result.access.size == 4 && result.access.writing == writing &&
                       result.access.fc == (privileged ? 5 : 1));
                assert(EM_D0 == 0x12345678 && EM_A0 == address + 4 && EM_A7 == 0xFEDCBA98);
                assert(cpu_state.ccc && cpu_state.ccv && !cpu_state.ccnz && cpu_state.ccn && cpu_state.ccx);
                assert(ram[physical(address)] == (writing ? 0x12 : 0xA5) &&
                       ram[physical(address + 1)] == (writing ? 0x34 : 0x5A));
                ++cases;
            }
}

static void locked_cases(void)
{
    const unsigned ops[] = {0x4AD0, 0x0AD0, 0x0CD0, 0x0ED0, 0x0CFC, 0x0EFC};
    for (unsigned privileged = 0; privileged < 2; ++privileged)
        for (unsigned page8k = 0; page8k < 2; ++page8k)
            for (unsigned i = 0; i < sizeof ops / sizeof *ops; ++i) {
                fixture(page8k, privileged);
                state(31, privileged);
                EM_A0 = 0x20040;
                EM_A1 = 0x20080;
                instruction(0x10000, ops[i], 1);
                uint32_t descriptor;
                assert(table_read(&mmu, pte(EM_A0), &descriptor));
                put32(pte(EM_A0), descriptor | 4); /* write protected */
                Syn68kIntegerStep result;
                assert(syn68k_integer_step(0x10000, &memory, &result));
                assert(result.vector == 2 && result.access.rmw && !result.access.writing && !result.access.completed &&
                       result.access.translation.fault == MMU040_WRITE_PROTECTED);
                assert(!writes && cpu_state.ccc && cpu_state.ccv && !cpu_state.ccnz && cpu_state.ccn && cpu_state.ccx);
                ++cases;
            }
}

int main(void)
{
    flat_ram = calloc(1, 0x40000);
    assert(flat_ram);
    ROMlib_offset = (uintptr_t)flat_ram;
    syn68k_set_memory_window(0x40000, 0, 0);
    uint32 vectors[64] = {0};
    initialize_68k_emulator(NULL, 0, vectors, 0);
    const unsigned ops[] = {
        0x2010, 0x2080, 0x2018, 0x20C0, 0xD190, 0x9190, 0xB190, 0xC190, 0x8090, 0xB090, 0x5290, 0x4290, 0x4490, 0x4090,
        0x4690, 0x4A90, 0x4AD0, 0x56D0, 0xE2D0, 0x0390, 0x0690, 0xD188, 0x9188, 0xB188, 0x0AD0, 0x0CD0, 0x0ED0, 0x0CFC,
        0x0EFC, 0xC308, 0x8308, 0x8348, 0x8388, 0x4810, 0xE8D0, 0xE9D0, 0xEAD0, 0xEBD0, 0xECD0, 0xEDD0, 0xEED0, 0xEFD0,
        0x4E71, 0x70FF, 0x7E7F, 0x4840, 0x4880, 0x48C0, 0x49C0, 0xC141, 0xC149, 0xC189, 0xD101, 0xD141, 0xD181, 0x9101,
        0x9141, 0x9181, 0x4280, 0x4480, 0x4080, 0x4680, 0x4A80, 0x4AC0, 0x56C0, 0x5280, 0x5288, 0x5388, 0xD081, 0x9081,
        0xB081, 0xC081, 0x8081, 0xB181, 0xD0C1, 0x90C1, 0xB0C1, 0xD1C1, 0x91C1, 0xB1C1, 0xB008, 0x4A48, 0x4A88, 0x2200,
        0x3240, 0x2041, 0x0680, 0x0480, 0x0080, 0x0280, 0x0A80, 0x0C80, 0x0300, 0x0340, 0x0380, 0x03C0, 0x0800, 0x0840,
        0x0880, 0x08C0, 0x003C, 0x023C, 0x0A3C, 0x42C0, 0x44C0, 0x42D0, 0x44D0, 0xE320, 0xE360, 0xE3A0, 0xE220, 0xE260,
        0xE2A0, 0xE328, 0xE368, 0xE3A8, 0xE330, 0xE370, 0xE3B0, 0xE338, 0xE378, 0xE3B8, 0xE4C0, 0xE5C0, 0xE6C0, 0xE7C0};
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned i = 0; i < sizeof ops / sizeof *ops; ++i)
                for (unsigned sample = 0; sample < 64; ++sample) {
                    unsigned page = page8k ? 8192 : 4096;
                    unsigned offset = sample < 32 ? sample + 8 : page - 32 + sample - 32;
                    compare(ops[i], offset, privileged, page8k);
                }
    fault_cases();
    locked_cases();
    for (operand_pattern = 1; operand_pattern <= 9; ++operand_pattern)
        for (unsigned i = 42; i < sizeof ops / sizeof *ops; ++i)
            for (unsigned count = 0; count < 64; ++count)
                compare(ops[i], 8 + count, operand_pattern & 1, operand_pattern & 1);
    printf("%u mapped scalar/complex differential and partial-fault cases passed.\n", cases);
    free(flat_ram);
    return 0;
}
