/*+===================================================================
  File:      mmu040-ea-test.c

  Summary:   Implements mmu040 ea test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Exercise the actual bounded EA decoder with distinct mapped code and data. */
#include "ea.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char ram[65536];
static Syn68kMMU040 mmu;
static unsigned reads, writes, cases;

static void put(uint32_t address, uint32_t value)
{
    assert(address <= sizeof ram - 4);
    for (unsigned i = 0; i < 4; ++i)
        ram[address + i] = value >> (24 - i * 8);
}

static int read_table(void* opaque, uint32_t address, uint32_t* value)
{
    assert(opaque == ram);
    if (address > sizeof ram - 4)
        return 0;
    *value = (uint32_t)ram[address] << 24 | (uint32_t)ram[address + 1] << 16 | (uint32_t)ram[address + 2] << 8 |
             ram[address + 3];
    return 1;
}

static int write_table(void* opaque, uint32_t address, uint32_t value, int locked)
{
    uint32_t old;
    if (!read_table(opaque, address, &old))
        return 0;
    put(address, locked ? old | value : value);
    return 1;
}

static int read_operand(void* opaque, uint32_t address, unsigned char* bytes, unsigned size, uint32_t attributes,
                        unsigned fc)
{
    assert(opaque == ram);
    (void)attributes;
    (void)fc;
    ++reads;
    if (address > sizeof ram - size)
        return 0;
    memcpy(bytes, ram + address, size);
    return 1;
}

static int write_operand(void* opaque, uint32_t address, const unsigned char* bytes, unsigned size, uint32_t attributes,
                         unsigned fc)
{
    assert(opaque == ram);
    (void)attributes;
    (void)fc;
    ++writes;
    if (address > sizeof ram - size)
        return 0;
    memcpy(ram + address, bytes, size);
    return 1;
}

static Syn68kMMU040Memory memory = {.mmu = &mmu,
                                    .tables = {ram, read_table, write_table},
                                    .opaque = ram,
                                    .read = read_operand,
                                    .write = write_operand,
                                    .native = NULL};

static Syn68kEAContext fixture(unsigned supervisor)
{
    memset(ram, 0, sizeof ram);
    syn68k_mmu040_init(&mmu);
    mmu.tc = 0x8000;
    mmu.urp = 0x1000;
    mmu.srp = 0x4000;
    put(0x1000, 0x2002);
    put(0x2000, 0x3002);
    put(0x4000, 0x5002);
    put(0x5000, 0x6002);
    put(0x3010, 0x8001);
    put(0x6010, 0xc001);
    put(0x3014, 0xa001);
    put(0x6014, 0xe001);
    cpu_state.sr = supervisor ? 0x2000 : 0;
    reads = writes = 0;
    Syn68kEAContext result = {.pc = 0x4000, .memory = &memory};
    return result;
}

static void instruction_and_data(void)
{
    for (unsigned supervisor = 0; supervisor < 2; ++supervisor)
        for (unsigned size = 1; size <= 4; size *= 2) {
            Syn68kEAContext context = fixture(supervisor);
            unsigned code = supervisor ? 0xc000 : 0x8000;
            unsigned data = supervisor ? 0xe000 : 0xa000;
            unsigned pte = supervisor ? 0x6010 : 0x3010;
            put(code, 0x00101234);
            put(data, 0x55667788);
            put(data + 16, 0xabcdef01);
            assert(syn68k_ea_fetch_memory(&context, 0x4000, 2) == 16);
            put(pte, data | 1); // Code ATC retains the earlier mapping.
            Syn68kEA ea = syn68k_effective_address(&context, 0x3c, size, 0, 1);
            assert(ea.instruction && ea.reg == -1 && context.pc == 0x4000 + (size == 1 ? 2 : size));
            uint32_t value = syn68k_ea_read_operand(&context, ea, size);
            assert(value == (size == 1 ? 16u : size == 2 ? 16u : 0x00101234u));
            ++cases;
            assert(!context.error);
            context.pc = 0x4000;
            ea = syn68k_effective_address(&context, 0x3a, 2, 0, 1);
            assert(!ea.instruction && ea.address == 0x4010);
            assert(syn68k_ea_read_operand(&context, ea, 2) == 0xabcd);
            ++cases;
            assert(syn68k_ea_read_memory(&context, 0x4000, 2) == 0x5566);
            syn68k_ea_write_memory(&context, 0x4000, 2, 0x9876);
            assert(!context.error && ram[data] == 0x98 && ram[data + 1] == 0x76);
            assert(ram[code] == 0 && ram[code + 1] == 16);
            // Explicit FC overrides the current SR, as required by MOVES.
            unsigned other_data = supervisor ? 0x8000 : 0xc000;
            ram[other_data] = 0x73;
            assert(syn68k_ea_read_memory_fc(&context, 0x4000, 1, supervisor ? 1 : 5, 0) == 0x73);
            ++cases;
        }
}

static void indirect_and_faults(void)
{
    Syn68kEAContext context = fixture(0);
    // Full extension: suppress index, word base displacement, preindexed indirect.
    put(0x8000, 0x01610010);
    put(0x8010, 0x00005020);
    put(0xa020, 0x12345678);
    EM_A0 = 0x4000;
    Syn68kEA ea = syn68k_effective_address(&context, 0x30, 4, 0, 1);
    assert(!context.error && context.pc == 0x4004 && ea.address == 0x5020);
    assert(syn68k_ea_read_operand(&context, ea, 4) == 0x12345678);
    ++cases;
    // A split fetch faults at the second page; it must not advance the PC.
    context = fixture(0);
    context.pc = 0x4ffe;
    put(0x3014, 0);
    syn68k_effective_address(&context, 0x39, 4, 0, 1);
    assert(context.error == 2 && context.pc == 0x4ffe);
    assert(context.fault_address == 0x5000 && context.access.completed == 2);
    unsigned old_reads = reads;
    syn68k_ea_write_memory(&context, 0x4000, 1, 7);
    assert(!syn68k_ea_read_memory(&context, 0x4000, 1));
    assert(reads == old_reads && writes == 0);
    ++cases;
    // A split write publishes the first page and retains its fault metadata.
    context = fixture(0);
    put(0x3014, 0xa005);
    syn68k_ea_write_memory(&context, 0x4fff, 2, 0x1234);
    assert(context.error == 2 && context.fault_address == 0x5000);
    assert(context.access.completed == 1 && context.access.translation.fault == MMU040_WRITE_PROTECTED);
    assert(ram[0x8fff] == 0x12 && ram[0xa000] == 0);
    ++cases;
    context = fixture(0);
    put(0x3010, 0x8005);
    assert(!syn68k_ea_read_memory_fc(&context, 0x4000, 1, 1, 1));
    assert(context.error == 2 && !reads && !writes);
    ++cases;
    // Physical operand failures are distinct from translation/table failures.
    context = fixture(0);
    put(0x3010, 0x10001);
    assert(!syn68k_ea_read_memory(&context, 0x4000, 4));
    assert(context.error == 2 && context.access.bus_error && context.access.translation.fault == MMU040_OK);
    ++cases;
}

static void register_sources(void)
{
    for (unsigned reg = 0; reg < 16; ++reg)
        for (unsigned size = 1; size <= 4; size *= 2) {
            Syn68kEAContext context = fixture(0);
            cpu_state.regs[reg].ul.n = 0x12345678;
            Syn68kEA ea = {.reg = reg};
            assert(syn68k_ea_read_operand(&context, ea, size) == (size == 1   ? 0x78u
                                                                  : size == 2 ? 0x5678u
                                                                              : 0x12345678u));
            assert(!context.error && !reads && !writes);
            ++cases;
        }
}

int main(void)
{
    instruction_and_data();
    indirect_and_faults();
    register_sources();
    printf("%u mapped EA instruction/data/operand cases passed\n", cases);
    return 0;
}
