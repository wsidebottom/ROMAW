/*+===================================================================
  File:      mmu040-movem-test.c

  Summary:   Implements mmu040 movem test support for syn68k 68K translation and runtime support.

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
#include "jit-x64.h"
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
    assert(result.vector == vector);
    ++cases;
    return result;
}

static unsigned reverse(unsigned mask)
{
    unsigned result = 0;
    for (unsigned i = 0; i < 16; ++i)
        result |= ((mask >> i) & 1) << (15 - i);
    return result;
}

static void instruction(unsigned mode, unsigned size, unsigned load, unsigned mask)
{
    code_word(0x10000, 0x4880 | load << 10 | (size == 4 ? 0x40 : 0) | mode << 3);
    code_word(0x10002, mode == 4 ? reverse(mask) : mask);
}

static void transfer(unsigned mode, unsigned size, unsigned load, unsigned mask, uint32_t base)
{
    uint32_t expected[16], original[16];
    for (unsigned i = 0; i < 15; ++i)
        cpu_state.regs[i].ul.n = 0x87658001u + i * 0x1234123u;
    EM_A0 = base;
    for (unsigned i = 0; i < 16; ++i)
        original[i] = expected[i] = cpu_state.regs[i].ul.n;
    unsigned char bytes[128];
    uint32_t start = mode == 4 ? base - 64 : base;
    for (unsigned i = 0; i < sizeof bytes; ++i)
        bytes[i] = ram[physical(start + i)] = (i * 73 + 129) & 255;
    unsigned count = __builtin_popcount(mask);
    uint32_t cursor = mode == 4 ? base - count * size : base;
    for (unsigned reg = 0; reg < 16; ++reg)
        if (mask & (1u << reg)) {
            if (load) {
                uint32_t value = 0;
                for (unsigned j = 0; j < size; ++j)
                    value = (value << 8) | bytes[cursor - start + j];
                expected[reg] = size == 2 ? (uint32_t)(int32_t)(int16_t)value : value;
            }
            else {
                uint32_t value = mode == 4 && reg == 8 ? base - size : original[reg];
                for (unsigned j = 0; j < size; ++j)
                    bytes[cursor - start + j] = value >> ((size - j - 1) * 8);
            }
            cursor += size;
        }
    if (mode == 3)
        expected[8] = base + count * size;
    else if (mode == 4)
        expected[8] = base - count * size;
    cpu_state.ccc = 1;
    cpu_state.ccv = 2;
    cpu_state.ccn = 8;
    cpu_state.ccnz = 0;
    cpu_state.ccx = 16;
    instruction(mode, size, load, mask);
    Syn68kIntegerStep result = step(0x10000, 0);
    assert(result.next == 0x10004);
    for (unsigned i = 0; i < 16; ++i)
        assert(cpu_state.regs[i].ul.n == expected[i]);
    for (unsigned i = 0; i < sizeof bytes; ++i)
        assert(ram[physical(start + i)] == bytes[i]);
    assert(cpu_state.ccc == 1 && cpu_state.ccv == 2 && cpu_state.ccn == 8);
    assert(!cpu_state.ccnz && cpu_state.ccx == 16);
}

static void masks_and_pages(void)
{
    for (unsigned load = 0; load < 2; ++load)
        for (unsigned size = 2; size <= 4; size += 2)
            for (unsigned automatic = 0; automatic < 2; ++automatic)
                for (unsigned mask = 0; mask < 65536; ++mask) {
                    fixture(0, 0);
                    unsigned mode = automatic ? (load ? 3 : 4) : 2;
                    transfer(mode, size, load, mask, 0x20080);
                }
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned offset = 0; offset < (page8k ? 8192u : 4096u); ++offset)
                for (unsigned load = 0; load < 2; ++load)
                    for (unsigned size = 2; size <= 4; size += 2)
                        for (unsigned automatic = 0; automatic < 2; ++automatic) {
                            fixture(page8k, privileged);
                            unsigned mode = automatic ? (load ? 3 : 4) : 2;
                            transfer(mode, size, load, 0x0303, 0x20040 + offset);
                        }
}

static void faults(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned load = 0; load < 2; ++load)
                for (unsigned size = 2; size <= 4; size += 2)
                    for (unsigned automatic = 0; automatic < 2; ++automatic)
                        for (unsigned bus = 0; bus < 2; ++bus)
                            for (unsigned partial = 0; partial < 2; ++partial) {
                                fixture(page8k, privileged);
                                unsigned mode = automatic ? (load ? 3 : 4) : 2;
                                uint32_t boundary = 0x20000 + page_size;
                                /* Predecrement writes D1 first; other modes transfer D0 first. */
                                uint32_t address = mode == 4 && !partial ? boundary : boundary - (partial ? 1 : size);
                                EM_A0 = mode == 4 ? address + size : address;
                                EM_D0 = 0xAABBCCDD;
                                EM_D1 = 0xEEFF0011;
                                for (unsigned i = 0; i < size; ++i)
                                    ram[physical(address + i)] = i ? 0x81 : 0x80;
                                instruction(mode, size, load, 3);
                                uint32_t failure = mode == 4 && !partial ? boundary - size : boundary;
                                if (bus)
                                    fail_physical = physical(failure);
                                else
                                    put32(pte(failure), 0);
                                uint32_t expected_a0 = mode == 4 ? (partial ? address : failure) : EM_A0;
                                Syn68kIntegerStep result = step(0x10000, 2);
                                assert(result.format == 7 && result.exception_pc == 0x10000);
                                assert(result.fault_address == failure && !!result.access.bus_error == !!bus);
                                assert(result.access.fc == (privileged ? 5 : 1));
                                assert(result.access.size == size && result.access.completed == (partial ? 1 : 0));
                                assert(result.access.writing == !load);
                                if (load) {
                                    assert(EM_D0 == (partial ? 0xAABBCCDD : size == 2 ? 0xFFFF8081 : 0x80818181));
                                    assert(EM_D1 == 0xEEFF0011);
                                    if (mode == 3 && !partial)
                                        expected_a0 += size;
                                }
                                else {
                                    unsigned first = mode == 4 ? (size == 2 ? 0 : 0xEE) : size == 2 ? 0xCC : 0xAA;
                                    assert(ram[physical(address)] == first);
                                }
                                assert(EM_A0 == expected_a0 && EM_A7 == 0x76543210);
                            }
}

static void fetch_and_code(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged) {
            fixture(page8k, privileged);
            uint32_t pc = 0x10000 + page_size - 2;
            code_word(pc, 0x48D0);
            code_word(pc + 2, 1);
            put32(pte(pc + 2), 0);
            EM_D0 = 0x12345678;
            EM_A0 = 0x20000;
            Syn68kIntegerStep result = step(pc, 2);
            assert(result.format == 7 && result.access.request == pc + 2);
            assert(result.access.fc == (privileged ? 6 : 2));
            assert(reads == 1 && !writes && EM_A0 == 0x20000);
            assert(EM_D0 == 0x12345678 && EM_A7 == 0x76543210);

            fixture(page8k, privileged);
            instruction(3, 4, 0, 1); /* Store postincrement is illegal. */
            result = step(0x10000, 4);
            assert(!result.format && reads == 1 && !writes);
            assert(EM_A7 == 0x76543210);

            fixture(page8k, privileged);
            instruction(2, 4, 0, 1);
            EM_D0 = 0x4E714E71;
            EM_A0 = 0x10000; /* Rewrite the executing opcode and register mask. */
            result = step(0x10000, 0);
            assert(result.next == 0x10004 && writes == 1);
            assert(ram[physical(0x10000)] == 0x4E);
            assert(ram[physical(0x10001)] == 0x71);
            assert(ram[physical(0x10002)] == 0x4E);
            assert(ram[physical(0x10003)] == 0x71);
            assert(syn68k_integer_step(0x10000, &memory, &result));
            assert(!result.vector && result.next == 0x10002);
            uint32_t descriptor;
            assert(table_read(&mmu, pte(0x10000), &descriptor));
            assert(descriptor & 16); /* Data access updated the page's M bit. */
        }
}

#ifdef GENERATE_X64_CODE
static void native_restart(void)
{
    unsigned count = 0;
    for (unsigned page = 0; page < 2; ++page)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned load = 0; load < 2; ++load)
                for (unsigned width = 2; width <= 4; width += 2)
                    for (unsigned fault = 0; fault < 3; ++fault) {
                        uint32_t saved = load ? 0x20020 : 0x20030;
                        unsigned opcode = 0x4880 | load << 10 | (width == 4 ? 0x40 : 0) | (load ? 3 : 4) << 3;
                        Syn68kIntegerStep expected;
                        uint32_t expected_d0 = 0, expected_a0 = 0;
                        unsigned expected_reads = 0, expected_writes = 0;
                        for (unsigned native = 0; native < 2; ++native) {
                            fixture(page, privileged);
                            instruction(load ? 3 : 4, width, load, 1);
                            EM_A0 = 0xdead0000;
                            EM_D0 = 0x12345678;
                            uint32_t operand = load ? saved : saved - width;
                            put32(physical(operand), 0xabcdef01);
                            if (fault == 1)
                                fail_physical = physical(0x10002);
                            else if (fault == 2)
                                fail_physical = physical(operand);
                            Syn68kIntegerStep result;
                            if (!native) {
                                assert(syn68k_integer_resume_movem(0x10000, opcode, &memory, saved, &expected));
                                expected_d0 = EM_D0;
                                expected_a0 = EM_A0;
                                expected_reads = reads;
                                expected_writes = writes;
                            }
                            else {
                                assert(syn68k_x64_resume_movem(0x10000, opcode, &memory, saved, &result) == 1);
                                assert(result.next == expected.next && result.vector == expected.vector &&
                                       result.format == expected.format && result.fault_address == expected.fault_address &&
                                       result.movem_state == expected.movem_state && result.movem_ea == expected.movem_ea);
                                assert(EM_D0 == expected_d0 && EM_A0 == expected_a0);
                                assert(reads == expected_reads && writes == expected_writes);
                                ++count;
                            }
                        }
                    }
    printf("%u native MOVEM restart mask and repeat-fault cases passed.\n", count);
}
#endif

int main(void)
{
    masks_and_pages();
    faults();
    fetch_and_code();
#ifdef GENERATE_X64_CODE
    native_restart();
#endif
    printf("%u mapped MOVEM mask/page/update/partial-fault cases passed.\n", cases);
}
