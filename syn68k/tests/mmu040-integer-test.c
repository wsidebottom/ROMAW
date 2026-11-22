/*+===================================================================
  File:      mmu040-integer-test.c

  Summary:   Implements mmu040 integer test support for syn68k 68K translation and runtime support.

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
    reads = writes = 0;
    fail_physical = 0xffffffff;
}

static Syn68kIntegerStep step(uint32_t pc, unsigned vector)
{
    Syn68kIntegerStep result;
    assert(syn68k_integer_step(pc, &memory, &result));
    if (result.vector != vector)
        fprintf(stderr, "pc=%08x vector=%u expected=%u\n", pc, result.vector, vector);
    assert(result.vector == vector && EM_A7 == 0x76543210);
    ++cases;
    return result;
}

static const unsigned opcodes[] = {0xC0D0, 0xC1D0, 0x80D0, 0x81D0, 0x4C10, 0x4C10,
                                   0x4C10, 0x4C10, 0x4C50, 0x4C50, 0x4C50, 0x4C50};
static const unsigned extensions[] = {0, 0, 0, 0, 1, 0x0801, 0x0401, 0x0C01, 1, 0x0801, 0x0401, 0x0C01};

static int division(unsigned operation)
{
    return operation == 2 || operation == 3 || operation >= 8;
}

static void prepare(unsigned operation, uint32_t pc)
{
    EM_D0 = division(operation) ? 42 : 7;
    EM_D1 = 0;
    cpu_state.ccx = 1;
    cpu_state.ccc = cpu_state.ccv = cpu_state.ccn = 1;
    cpu_state.ccnz = 0;
    code_word(pc, opcodes[operation]);
    if (operation >= 4)
        code_word(pc + 2, extensions[operation]);
}

static void operand(uint32_t address, unsigned size, uint32_t value)
{
    for (unsigned i = 0; i < size; ++i)
        ram[physical(address + i)] = value >> ((size - i - 1) * 8);
}

static void offsets(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned operation = 0; operation < 12; ++operation)
                for (unsigned offset = 0; offset < (page8k ? 8192u : 4096u); ++offset) {
                    fixture(page8k, privileged);
                    prepare(operation, 0x10000);
                    EM_A0 = 0x20000 + offset;
                    operand(EM_A0, operation < 4 ? 2 : 4, 6);
                    Syn68kIntegerStep result = step(0x10000, 0);
                    assert(result.next == 0x10000 + (operation < 4 ? 2 : 4));
                    assert(EM_D0 == (division(operation) ? 7 : 42) && !EM_D1);
                    assert(!cpu_state.ccc && !cpu_state.ccv && !cpu_state.ccn);
                    assert(cpu_state.ccnz && cpu_state.ccx && !writes);
                    assert(EM_A0 == 0x20000 + offset);
                }
}

static void immediates_and_faults(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned operation = 0; operation < 12; ++operation) {
                fixture(page8k, privileged);
                unsigned size = operation < 4 ? 2 : 4;
                unsigned length = operation < 4 ? 2 : 4;
                uint32_t pc = 0x10000 + page_size - length;
                prepare(operation, pc);
                code_word(pc, (opcodes[operation] & ~63) | 0x3C);
                code_word(pc + length, size == 2 ? 6 : 0);
                if (size == 4)
                    code_word(pc + length + 2, 6);
                assert(step(pc, 0).next == pc + length + size);
                assert(EM_D0 == (division(operation) ? 7 : 42));

                fixture(page8k, privileged);
                prepare(operation, 0x10000);
                EM_A0 = 0x20000 + page_size - 1;
                operand(EM_A0, size, 6);
                put32(pte(0x20000 + page_size), 0);
                Syn68kIntegerStep result = step(0x10000, 2);
                assert(result.exception_pc == 0x10000 && result.format == 7);
                assert(result.fault_address == 0x20000 + page_size);
                assert(result.access.request == EM_A0 && result.access.size == size);
                assert(result.access.completed == 1 && !result.access.bus_error);
                assert(result.access.fc == (privileged ? 5 : 1));
                assert(EM_D0 == (division(operation) ? 42 : 7));
                assert(cpu_state.ccc && cpu_state.ccv && !cpu_state.ccnz);

                fixture(page8k, privileged);
                prepare(operation, 0x10000);
                EM_A0 = 0x20000 + page_size - 1;
                operand(EM_A0, size, 6);
                fail_physical = physical(0x20000 + page_size);
                result = step(0x10000, 2);
                assert(result.access.completed == 1 && result.access.bus_error);
                assert(result.access.translation.fault == MMU040_OK);
                assert(EM_D0 == (division(operation) ? 42 : 7));

                if (division(operation)) {
                    fixture(page8k, privileged);
                    prepare(operation, 0x10000);
                    EM_A0 = 0x20000;
                    operand(EM_A0, size, 0);
                    result = step(0x10000, 5);
                    assert(result.format == 2 && result.exception_pc == 0x10000 + length);
                    assert(result.fault_address == 0x10000 && EM_D0 == 42);
                    assert(!cpu_state.ccc && !writes);
                }
            }
}

static void fetch_and_invalid(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged) {
            fixture(page8k, privileged);
            prepare(4, 0x10000);
            put32(pte(0x10000), 0);
            Syn68kIntegerStep result = step(0x10000, 2);
            assert(result.access.fc == (privileged ? 6 : 2));
            assert(result.access.request == 0x10000 && !result.access.completed);
            assert(EM_D0 == 7 && !reads && !writes);

            fixture(page8k, privileged);
            uint32_t pc = 0x10000 + page_size - 2;
            prepare(4, pc);
            put32(pte(0x10000 + page_size), 0);
            result = step(pc, 2);
            assert(result.access.fc == (privileged ? 6 : 2));
            assert(result.access.request == pc + 2 && reads == 1);
            assert(EM_D0 == 7 && !writes);

            fixture(page8k, privileged);
            prepare(4, 0x10000);
            code_word(0x10002, 0x8000);
            result = step(0x10000, 4);
            assert(!result.format && EM_D0 == 7 && reads == 2);

            fixture(page8k, privileged);
            prepare(4, 0x10000);
            code_word(0x10000, 0x4C08); /* An direct is illegal before extension fetch. */
            result = step(0x10000, 4);
            assert(!result.format && EM_D0 == 7 && reads == 1);

            fixture(page8k, privileged);
            code_word(0x10000, 0x4AFC); /* Exception delivery is the caller's job. */
            assert(!syn68k_integer_step(0x10000, &memory, &result));
            assert(!result.vector && !syn68k_integer_step(0x10000, &memory, NULL));
        }
}

static void addressing(void)
{
    const unsigned eas[] = {2, 0x18, 0x20, 0x28, 0x30, 0x30, 0x39, 0x3A, 0x3B};
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned operation = 0; operation < 12; ++operation)
                for (unsigned form = 0; form < sizeof eas / sizeof *eas; ++form) {
                    fixture(page8k, privileged);
                    uint32_t pc = 0x10000;
                    prepare(operation, pc);
                    code_word(pc, (opcodes[operation] & ~63) | eas[form]);
                    uint32_t extension = pc + (operation < 4 ? 2 : 4);
                    unsigned size = operation < 4 ? 2 : 4;
                    uint32_t address = 0x20080, final_a0 = address;
                    EM_A0 = address;
                    EM_D2 = 6;
                    unsigned tail = 0;
                    switch (form) {
                    case 0: /* D2 direct. */
                        break;
                    case 1: /* (A0)+. */
                        final_a0 += size;
                        break;
                    case 2: /* -(A0). */
                        EM_A0 += size;
                        break;
                    case 3: /* Signed d16(A0). */
                        EM_A0 += 16;
                        code_word(extension, 0xFFF0);
                        tail = 2;
                        break;
                    case 4: /* Brief D2.L index. */
                        EM_D2 = 4;
                        EM_A0 -= 4;
                        code_word(extension, 0x2800);
                        tail = 2;
                        break;
                    case 5: /* Full preindexed indirection, suppressed index. */
                        EM_A0 = 0x20000;
                        code_word(extension, 0x0161);
                        code_word(extension + 2, 0x0010);
                        operand(EM_A0 + 16, 4, address);
                        tail = 4;
                        break;
                    case 6: /* Absolute long. */
                        code_word(extension, address >> 16);
                        code_word(extension + 2, address);
                        tail = 4;
                        break;
                    case 7: /* PC-relative source uses data FC, not immediate FC. */
                        address = extension + 0x100;
                        code_word(extension, 0x100);
                        tail = 2;
                        break;
                    case 8: /* PC-relative brief D2.L. */
                        address = extension + 4;
                        EM_D2 = 4;
                        code_word(extension, 0x2800);
                        tail = 2;
                        break;
                    }
                    if (form != 0)
                        operand(address, size, 6);
                    if (form != 1 && form != 2)
                        final_a0 = EM_A0;
                    Syn68kIntegerStep result = step(pc, 0);
                    assert(result.next == extension + tail);
                    assert(EM_D0 == (division(operation) ? 7 : 42));
                    assert(EM_A0 == final_a0 && !writes);
                }
}

int main(void)
{
    offsets();
    immediates_and_faults();
    fetch_and_invalid();
    addressing();
    printf("%u mapped integer multiply/divide operand/fetch/fault cases passed.\n", cases);
}
