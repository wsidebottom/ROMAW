/*+===================================================================
  File:      mmu040-control-test.c

  Summary:   Implements mmu040 control test support for syn68k 68K translation and runtime support.

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

static void addressing(void)
{
    const unsigned operations[] = {0x4E80, 0x4EC0, 0x41C0, 0x4840};
    const unsigned eas[] = {0x10, 0x28, 0x30, 0x30, 0x38, 0x39, 0x3A, 0x3B};
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned op = 0; op < 4; ++op)
                for (unsigned form = 0; form < sizeof eas / sizeof *eas; ++form) {
                    fixture(page8k, privileged);
                    uint32_t pc = 0x10000 + page_size - 2;
                    code_word(pc, operations[op] | eas[form]);
                    uint32_t extension = pc + 2, target = 0x20080;
                    unsigned tail = 0;
                    EM_A0 = target;
                    EM_D0 = 4;
                    EM_A7 = 0x20010;
                    switch (form) {
                    case 0:
                        break;
                    case 1:
                        EM_A0 += 16;
                        code_word(extension, 0xFFF0);
                        tail = 2;
                        break;
                    case 2:
                        EM_A0 -= 4;
                        code_word(extension, 0x0800);
                        tail = 2;
                        break;
                    case 3:
                        EM_A0 = 0x20000;
                        code_word(extension, 0x0161);
                        code_word(extension + 2, 16);
                        put32(physical(EM_A0 + 16), target);
                        tail = 4;
                        break;
                    case 4:
                        target = 0xFFFF8000; /* Absolute word sign extension. */
                        code_word(extension, 0x8000);
                        tail = 2;
                        break;
                    case 5:
                        code_word(extension, target >> 16);
                        code_word(extension + 2, target);
                        tail = 4;
                        break;
                    case 6:
                        target = extension - 16;
                        code_word(extension, 0xFFF0);
                        tail = 2;
                        break;
                    case 7:
                        target = extension + 4;
                        code_word(extension, 0x0800);
                        tail = 2;
                        break;
                    }
                    unsigned expected_reads = form == 5 ? 2 : 1 + tail / 2 + (form == 3);
                    uint32_t next = extension + tail;
                    Syn68kIntegerStep result = step(pc, 0);
                    assert(result.next == (op < 2 ? target : next));
                    assert(reads == expected_reads); /* Target must never be fetched. */
                    if (op == 0 || op == 3) {
                        assert(writes == 1 && EM_A7 == 0x2000C);
                        uint32_t pushed;
                        assert(table_read(&mmu, physical(EM_A7), &pushed));
                        assert(pushed == (op == 0 ? next : target));
                    }
                    else
                        assert(!writes && EM_A7 == 0x20010);
                    if (op == 2)
                        assert(EM_A0 == target);
                }
}

static void stack_faults(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned op = 0; op < 2; ++op)
                for (unsigned bus = 0; bus < 2; ++bus) {
                    fixture(page8k, privileged);
                    code_word(0x10000, op ? 0x4850 : 0x4E90);
                    EM_A0 = 0x22334455;
                    uint32_t address = 0x20000 + page_size - 1;
                    EM_A7 = address + 4;
                    if (bus)
                        fail_physical = physical(address + 1);
                    else
                        put32(pte(address + 1), 0);
                    Syn68kIntegerStep result = step(0x10000, 2);
                    assert(result.format == 7 && result.exception_pc == 0x10000);
                    assert(result.access.request == address && result.access.size == 4);
                    assert(result.access.completed == 1 && result.access.writing);
                    assert(result.access.fc == (privileged ? 5 : 1));
                    assert(!!result.access.bus_error == !!bus && EM_A7 == address);
                    assert(ram[physical(address)] == (op ? 0x22 : 0));
                    assert(EM_A0 == 0x22334455);
                }
}

static void stack_offsets_and_aliases(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned op = 0; op < 2; ++op)
                for (unsigned offset = 0; offset < (page8k ? 8192u : 4096u); ++offset) {
                    fixture(page8k, privileged);
                    code_word(0x10000, op ? 0x4857 : 0x4E97); /* (A7) source. */
                    uint32_t address = 0x20000 + offset;
                    EM_A7 = address + 4;
                    Syn68kIntegerStep result = step(0x10000, 0);
                    assert(EM_A7 == address);
                    assert(result.next == (op ? 0x10002 : address + 4));
                    uint32_t expected = op ? address + 4 : 0x10002;
                    for (unsigned i = 0; i < 4; ++i)
                        assert(ram[physical(address + i)] == ((expected >> ((3 - i) * 8)) & 255));
                    /* A crossing store repeats its first-page portion before the second. */
                    assert(reads == 1 && writes == (offset + 4 > page_size ? 3u : 1u));
                }
    for (unsigned privileged = 0; privileged < 2; ++privileged) {
        fixture(0, privileged);
        code_word(0x10000, 0x4FEF); /* LEA 4(A7),A7. */
        code_word(0x10002, 4);
        EM_A7 = 0x20010;
        Syn68kIntegerStep result = step(0x10000, 0);
        assert(EM_A7 == 0x20014 && result.next == 0x10004 && !writes);
    }
}

int main(void)
{
    addressing();
    stack_faults();
    stack_offsets_and_aliases();
    printf("%u mapped JSR/JMP/LEA/PEA target/stack/fault cases passed.\n", cases);
}
