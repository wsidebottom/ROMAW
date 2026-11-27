/*+===================================================================
  File:      mmu040-moves-test.c

  Summary:   Implements mmu040 moves test support for syn68k 68K translation and runtime support.

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
#include <stdlib.h>
#include <string.h>

static unsigned char ram[0x20000];
static Syn68kMMU040 mmu;
static unsigned cases, reads, writes, page_size, supervisor;
static unsigned last_fc;
#ifdef GENERATE_X64_CODE
static int run_native;
#endif
static unsigned char devices[8][16];
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
    if (physical >= 0x10000)
        last_fc = fc;
    ++reads;
    if ((fc == 0 || fc == 3 || fc == 4 || fc == 7) && physical >= 0x20000 && physical - 0x20000 <= 16 &&
        size <= 16 - (physical - 0x20000)) {
        assert(!attributes);
        memcpy(bytes, devices[fc] + physical - 0x20000, size);
        return 1;
    }
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
    if (physical >= 0x10000)
        last_fc = fc;
    ++writes;
    if ((fc == 0 || fc == 3 || fc == 4 || fc == 7) && physical >= 0x20000 && physical - 0x20000 <= 16 &&
        size <= 16 - (physical - 0x20000)) {
        assert(!attributes);
        memcpy(devices[fc] + physical - 0x20000, bytes, size);
        return 1;
    }
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
#ifdef GENERATE_X64_CODE
    if (run_native) {
        unsigned opcode = (ram[physical(pc)] << 8) | ram[physical(pc + 1)];
        assert(syn68k_x64_mapped_mmu(pc, opcode, &memory, &result) == 1);
        ++reads; /* The mapped dispatcher receives the already fetched opcode. */
    }
    else
#endif
        assert(syn68k_integer_step(pc, &memory, &result));
    if (result.vector != vector)
        fprintf(stderr, "pc=%08x vector=%u expected=%u\n", pc, result.vector, vector);
    assert(result.vector == vector);
    ++cases;
    return result;
}

static void instruction(unsigned size, unsigned reg, unsigned writing, unsigned mode)
{
    code_word(0x10000, 0x0E00 | (size == 4 ? 2 : size == 2) << 6 | mode << 3);
    code_word(0x10002, reg << 12 | writing << 11);
}

static uint32_t data_physical(uint32_t logical, unsigned fc)
{
    unsigned saved = supervisor;
    supervisor = !!(fc & 4);
    uint32_t result = physical(logical);
    supervisor = saved;
    return result;
}

static void map_data(unsigned fc)
{
    unsigned saved = supervisor;
    supervisor = !!(fc & 4);
    put32(pte(0x20000), (supervisor ? 0x18000 : 0x10000) | 1);
    put32(pte(0x20000 + page_size), (supervisor ? 0x1C000 : 0x14000) | 1);
    supervisor = saved;
}

static void transfers(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned fc_index = 0; fc_index < 4; ++fc_index)
            for (unsigned size = 1; size <= 4; size *= 2)
                for (unsigned writing = 0; writing < 2; ++writing)
                    for (unsigned offset = 0; offset < (page8k ? 8192u : 4096u); ++offset) {
                        const unsigned fcs[] = {1, 2, 5, 6};
                        unsigned fc = fcs[fc_index], normalized = (fc & 4) | 1;
                        fixture(page8k, 1);
                        map_data(fc);
                        instruction(size, 0, writing, 2);
                        EM_A0 = 0x20000 + offset;
                        EM_D0 = 0xA1B2C3D4;
                        cpu_state.sfc = cpu_state.dfc = fc;
                        cpu_state.ccc = cpu_state.ccv = cpu_state.ccn = cpu_state.ccx = 1;
                        cpu_state.ccnz = 0;
                        for (unsigned i = 0; i < size; ++i)
                            ram[data_physical(EM_A0 + i, fc)] = 0x80 + i;
                        Syn68kIntegerStep result = step(0x10000, 0);
                        assert(result.next == 0x10004 && last_fc == normalized);
                        if (writing)
                            for (unsigned i = 0; i < size; ++i)
                                assert(ram[data_physical(EM_A0 + i, fc)] ==
                                       ((0xA1B2C3D4u >> ((size - i - 1) * 8)) & 255));
                        else
                            assert(EM_D0 == (size == 1 ? 0xA1B2C380u : size == 2 ? 0xA1B28081u : 0x80818283u));
                        assert(EM_A0 == 0x20000 + offset && EM_A7 == 0x76543210);
                        assert(cpu_state.ccc && cpu_state.ccv && cpu_state.ccn && cpu_state.ccx && !cpu_state.ccnz);
                    }
}

static void registers_and_faults(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned fc = 1; fc <= 5; fc += 4)
            for (unsigned size = 1; size <= 4; size *= 2)
                for (unsigned reg = 0; reg < 16; ++reg)
                    for (unsigned mode = 2; mode <= 4; ++mode)
                        for (unsigned writing = 0; writing < 2; ++writing) {
                            fixture(page8k, 1);
                            map_data(fc);
                            instruction(size, reg, writing, mode);
                            EM_A0 = 0x20080;
                            if (reg != 8)
                                cpu_state.regs[reg].ul.n = 0xA1B2C3D4;
                            uint32_t address = mode == 4 ? EM_A0 - size : EM_A0;
                            uint32_t next_a0 = mode == 4 ? address : EM_A0 + (mode == 3 ? size : 0);
                            cpu_state.sfc = cpu_state.dfc = fc;
                            for (unsigned i = 0; i < size; ++i)
                                ram[data_physical(address + i, fc)] = 0x80 + i;
                            Syn68kIntegerStep result = step(0x10000, 0);
                            assert(result.next == 0x10004);
                            uint32_t mask = size == 4 ? UINT32_MAX : (1u << (size * 8)) - 1;
                            uint32_t loaded = size == 1 ? 0x80 : size == 2 ? 0x8081 : 0x80818283;
                            if (writing) {
                                uint32_t value = reg == 8 ? next_a0 : 0xA1B2C3D4;
                                for (unsigned i = 0; i < size; ++i)
                                    assert(ram[data_physical(address + i, fc)] ==
                                           ((value >> ((size - i - 1) * 8)) & 255));
                            }
                            else {
                                uint32_t value = reg < 8     ? (0xA1B2C3D4 & ~mask) | loaded
                                                 : size == 1 ? (uint32_t)(int32_t)(int8_t)loaded
                                                 : size == 2 ? (uint32_t)(int32_t)(int16_t)loaded
                                                             : loaded;
                                assert(cpu_state.regs[reg].ul.n == value);
                                if (reg == 8)
                                    next_a0 = value;
                            }
                            assert(EM_A0 == next_a0);
                        }

    for (unsigned writing = 0; writing < 2; ++writing)
        for (unsigned fc = 1; fc <= 6; ++fc)
            if (fc != 3 && fc != 4)
                for (unsigned bus = 0; bus < 2; ++bus) {
                    fixture(0, 1);
                    map_data(fc);
                    instruction(4, 0, writing, 3);
                    EM_A0 = 0x20FFF;
                    EM_D0 = 0xAABBCCDD;
                    cpu_state.sfc = cpu_state.dfc = fc;
                    if (bus)
                        fail_physical = data_physical(0x21000, fc);
                    else {
                        unsigned saved = supervisor;
                        supervisor = !!(fc & 4);
                        put32(pte(0x21000), 0);
                        supervisor = saved;
                    }
                    Syn68kIntegerStep result = step(0x10000, 2);
                    assert(result.access.fc == ((fc & 4) | 1) && result.access.completed == 1);
                    assert(result.access.request == 0x20FFF && result.fault_address == 0x21000);
                    assert(!!result.access.bus_error == !!bus && result.access.writing == writing);
                    assert(EM_A0 == 0x21003 && EM_D0 == 0xAABBCCDD && EM_A7 == 0x76543210);
                    if (writing)
                        assert(ram[data_physical(0x20FFF, fc)] == 0xAA);
                }

    fixture(0, 0);
    instruction(4, 0, 0, 2);
    Syn68kIntegerStep result = step(0x10000, 8);
    assert(reads == 1 && !writes && !result.format && EM_A7 == 0x76543210);
    fixture(0, 1);
    instruction(4, 0, 0, 2);
    code_word(0x10002, 1);
    result = step(0x10000, 4);
    assert(reads == 2 && !writes && !result.format && EM_A7 == 0x76543210);
}

static void bypass_spaces(void)
{
    const unsigned spaces[] = {0, 3, 4, 7};
    for (unsigned space = 0; space < sizeof spaces / sizeof *spaces; ++space)
        for (unsigned size = 1; size <= 4; size *= 2)
            for (unsigned writing = 0; writing < 2; ++writing) {
                unsigned fc = spaces[space];
                fixture(0, 1);
                memset(devices, 0x5A, sizeof devices);
                instruction(size, 0, writing, 2);
                cpu_state.sfc = cpu_state.dfc = fc;
                EM_A0 = 0x20000;
                EM_D0 = 0x12345678;
                Syn68kIntegerStep result = step(0x10000, 0);
                assert(result.next == 0x10004 && last_fc == fc);
                if (writing) {
                    for (unsigned bank = 0; bank < 8; ++bank)
                        for (unsigned i = 0; i < 16; ++i)
                            assert(devices[bank][i] ==
                                   (bank == fc && i < size ? (0x12345678u >> ((size - i - 1) * 8)) & 255 : 0x5A));
                }
                else
                    assert(EM_D0 == (size == 1 ? 0x1234565Au : size == 2 ? 0x12345A5Au : 0x5A5A5A5Au));
                assert(EM_A7 == 0x76543210);
            }
}

static void fetch_and_indirection(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k) {
        fixture(page8k, 1);
        uint32_t pc = 0x10000 + page_size - 2;
        code_word(pc, 0x0E90);
        code_word(pc + 2, 0);
        put32(pte(pc + 2), 0);
        EM_A0 = 0x20000;
        Syn68kIntegerStep result = step(pc, 2);
        assert(result.access.request == pc + 2 && result.access.fc == 6);
        assert(reads == 1 && !writes && EM_A0 == 0x20000);
        assert(EM_A7 == 0x76543210);

        fixture(page8k, 1);
        map_data(1);
        instruction(4, 0, 0, 6);
        code_word(0x10004, 0x0161); /* Full indirection, suppressed index. */
        code_word(0x10006, 16);
        EM_A0 = 0x20000;
        cpu_state.sfc = 2;       /* Final operand is normalized to user data. */
        put32(0x18010, 0x20080); /* Pointer is in supervisor data. */
        put32(0x10080, 0x12345678);
        result = step(0x10000, 0);
        assert(result.next == 0x10008 && EM_D0 == 0x12345678 && last_fc == 1);
        assert(EM_A0 == 0x20000 && EM_A7 == 0x76543210);

        fixture(page8k, 1);
        map_data(1);
        instruction(4, 0, 0, 6);
        code_word(0x10004, 0x0161);
        code_word(0x10006, 16);
        EM_A0 = 0x20000;
        EM_D0 = 0xDEADBEEF;
        cpu_state.sfc = 1;
        fail_physical = 0x18010;
        result = step(0x10000, 2);
        assert(result.access.request == 0x20010 && result.access.fc == 5);
        assert(result.access.bus_error && EM_D0 == 0xDEADBEEF && !writes);
        assert(EM_A7 == 0x76543210);
    }
}

int main(int argc, char** argv)
{
#ifdef GENERATE_X64_CODE
    run_native = argc > 1;
    uint64 emits = syn68k_native_compile_count(), hits = syn68k_aot_hit_count();
#else
    (void)argc;
    (void)argv;
#endif
    transfers();
    registers_and_faults();
    bypass_spaces();
    fetch_and_indirection();
#ifdef GENERATE_X64_CODE
    if (run_native) {
        emits = syn68k_native_compile_count() - emits;
        hits = syn68k_aot_hit_count() - hits;
        printf("MOVES generated entries emitted=%llu hits=%llu.\n", (unsigned long long)emits,
               (unsigned long long)hits);
        assert(atoi(argv[1]) ? emits == 0 && hits > 0 : emits > 0);
    }
#endif
    printf("%u mapped MOVES space/width/register/update/fault cases passed.\n", cases);
}
