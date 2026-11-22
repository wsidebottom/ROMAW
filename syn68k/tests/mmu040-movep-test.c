/*+===================================================================
  File:      mmu040-movep-test.c

  Summary:   Implements mmu040 movep test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Real integer instruction stepping over disjoint code/data physical pages. */
#include "syn68k_public.h"
#include "integer.h"
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
    cpu_state.ccc = 1;
    cpu_state.ccv = 2;
    cpu_state.ccn = 8;
    cpu_state.ccnz = 0;
    cpu_state.ccx = 16;
    reads = writes = 0;
    fail_physical = 0xffffffff;
}

static Syn68kIntegerStep step(uint32_t pc, unsigned vector)
{
    Syn68kIntegerStep result;
    assert(syn68k_integer_step(pc, &memory, &result));
    if (result.vector != vector)
        fprintf(stderr, "pc=%08x vector=%u expected=%u\n", pc, result.vector, vector);
    assert(result.vector == vector);
    assert(cpu_state.ccc == 1 && cpu_state.ccv == 2 && cpu_state.ccn == 8);
    assert(!cpu_state.ccnz && cpu_state.ccx == 16);
    ++cases;
    return result;
}

static void instruction(unsigned reg, unsigned base, unsigned count, unsigned writing, unsigned displacement)
{
    code_word(0x10000, 0x0108 | reg << 9 | base | (count == 4 ? 0x40 : 0) | writing << 7);
    code_word(0x10002, displacement);
}

static void transfer(unsigned reg, unsigned base, unsigned count, unsigned writing, unsigned offset)
{
    uint32_t address = 0x20000 + offset;
    EM_DREG(reg) = 0xA1B2C3D4;
    EM_AREG(base) = address + 16;
    uint32_t expected = 0;
    for (unsigned i = 0; i < count * 2; ++i) {
        ram[physical(address + i)] = 0x80 + i;
        if (!(i & 1))
            expected = (expected << 8) | (0x80 + i);
    }
    instruction(reg, base, count, writing, 0xFFF0);
    Syn68kIntegerStep result = step(0x10000, 0);
    assert(result.next == 0x10004 && EM_AREG(base) == address + 16);
    assert(EM_DREG(reg) == (writing ? 0xA1B2C3D4 : count == 2 ? 0xA1B20000 | expected : expected));
    for (unsigned i = 0; i < count * 2; ++i)
        assert(ram[physical(address + i)] ==
               (writing && !(i & 1) ? (0xA1B2C3D4u >> ((count - i / 2 - 1) * 8)) & 255 : 0x80 + i));
    assert(reads == 2 + (writing ? 0 : count) && writes == (writing ? count : 0));
}

static void offsets_and_registers(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned writing = 0; writing < 2; ++writing)
                for (unsigned count = 2; count <= 4; count += 2) {
                    for (unsigned offset = 0; offset < (page8k ? 8192u : 4096u); ++offset) {
                        fixture(page8k, privileged);
                        transfer(0, 0, count, writing, offset);
                    }
                    for (unsigned reg = 0; reg < 8; ++reg)
                        for (unsigned base = 0; base < 8; ++base) {
                            fixture(page8k, privileged);
                            transfer(reg, base, count, writing, 1);
                        }
                }
}

static void faults(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned writing = 0; writing < 2; ++writing)
                for (unsigned count = 2; count <= 4; count += 2)
                    for (unsigned completed = 0; completed < count; ++completed)
                        for (unsigned bus = 0; bus < 2; ++bus) {
                            fixture(page8k, privileged);
                            uint32_t boundary = 0x20000 + page_size;
                            uint32_t address = boundary - completed * 2;
                            EM_A0 = address;
                            EM_D0 = 0xA1B2C3D4;
                            for (unsigned i = 0; i < count * 2; ++i)
                                ram[physical(address + i)] = 0x80 + i;
                            instruction(0, 0, count, writing, 0);
                            if (bus)
                                fail_physical = physical(boundary);
                            else
                                put32(pte(boundary), 0);
                            Syn68kIntegerStep result = step(0x10000, 2);
                            assert(result.exception_pc == 0x10000 && result.format == 7);
                            assert(result.fault_address == boundary && result.access.request == boundary);
                            assert(result.access.size == 1 && !result.access.completed);
                            assert(result.access.fc == (privileged ? 5 : 1));
                            assert(!!result.access.bus_error == !!bus && result.access.writing == writing);
                            assert(EM_A0 == address && EM_D0 == 0xA1B2C3D4 && EM_A7 == 0x76543210);
                            for (unsigned i = 0; i < count * 2; ++i)
                                assert(ram[physical(address + i)] ==
                                       (writing && !(i & 1) && i / 2 < completed
                                            ? (0xA1B2C3D4u >> ((count - i / 2 - 1) * 8)) & 255
                                            : 0x80 + i));
                        }
}

static void fetch_and_alias(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged) {
            fixture(page8k, privileged);
            uint32_t pc = 0x10000 + page_size - 2;
            code_word(pc, 0x01C8);
            code_word(pc + 2, 0);
            put32(pte(pc + 2), 0);
            EM_D0 = 0x12345678;
            EM_A0 = 0x20000;
            Syn68kIntegerStep result = step(pc, 2);
            assert(result.access.request == pc + 2 && result.access.fc == (privileged ? 6 : 2));
            assert(reads == 1 && !writes && EM_D0 == 0x12345678 && EM_A0 == 0x20000);

            fixture(page8k, privileged);
            instruction(0, 0, 4, 1, 0);
            EM_A0 = 0x10000;
            EM_D0 = 0x12345678;
            result = step(0x10000, 0);
            assert(result.next == 0x10004 && reads == 2 && writes == 4);
            for (unsigned i = 0; i < 4; ++i)
                assert(ram[physical(0x10000 + i * 2)] == ((0x12345678u >> ((3 - i) * 8)) & 255));
            assert(EM_D0 == 0x12345678 && EM_A0 == 0x10000);
        }
}

int main(void)
{
    offsets_and_registers();
    faults();
    fetch_and_alias();
    printf("%u mapped MOVEP register/page/sparse/fault cases passed.\n", cases);
}
