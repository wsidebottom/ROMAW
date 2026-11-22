/*+===================================================================
  File:      mmu040-fpu-test.c

  Summary:   Implements mmu040 fpu test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Real FPU instruction stepping over disjoint code/data physical pages. */
#include "syn68k_public.h"
#include "fpu.h"
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
    syn68k_fpu_reset();
    cpu_state.sr = supervisor ? 0x2000 : 0;
    EM_A7 = 0x76543210; // Stepping must not build a flat exception stack here.
    reads = writes = 0;
    fail_physical = 0xffffffff;
}

static Syn68kFPUStep step(uint32_t pc, unsigned vector)
{
    Syn68kFPUStep result;
    assert(syn68k_fpu_step(pc, &memory, &result));
    if (result.vector != vector)
        fprintf(stderr, "pc=%08x vector=%u expected=%u\n", pc, result.vector, vector);
    assert(result.vector == vector && EM_A7 == 0x76543210);
    ++cases;
    return result;
}

static const unsigned sizes[7] = {4, 4, 12, 12, 2, 8, 1};
static const unsigned char two[7][12] = {{0, 0, 0, 2},
                                         {0x40, 0, 0, 0},
                                         {0x40, 0, 0, 0, 0x80, 0, 0, 0, 0, 0, 0, 0},
                                         {0}, // Packed decimal is intentionally not an implemented arithmetic format.
                                         {0, 2},
                                         {0x40, 0, 0, 0, 0, 0, 0, 0},
                                         {2}};

static void expect_two(void)
{
    assert(cpu_state.fpu.regs[0].exponent == 0x4000 &&
           cpu_state.fpu.regs[0].significand == UINT64_C(0x8000000000000000));
}

static void all_operand_offsets(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned offset = 0; offset < (page8k ? 8192u : 4096u); ++offset)
                for (unsigned format = 0; format < 7; ++format) {
                    if (format == 3)
                        continue;
                    for (unsigned writing = 0; writing < 2; ++writing) {
                        fixture(page8k, privileged);
                        code_word(0x10000, 0xf210);
                        code_word(0x10002, (writing ? 0x6000 : 0x4000) | format << 10);
                        uint32_t address = 0x20000 + offset;
                        unsigned size = sizes[format];
                        for (unsigned i = 0; i < size; ++i)
                            ram[physical(address + i)] = writing ? 0xcd : two[format][i];
                        if (!writing && format == 2) {
                            ram[physical(address + 2)] = 0xde;
                            ram[physical(address + 3)] = 0xad;
                        }
                        EM_A0 = address;
                        if (writing) {
                            cpu_state.fpu.regs[0].exponent = 0x4000;
                            cpu_state.fpu.regs[0].significand = UINT64_C(0x8000000000000000);
                        }
                        Syn68kFPUStep result = step(0x10000, 0);
                        assert(result.next == 0x10004 && !result.access.bus_error);
                        expect_two();
                        if (writing)
                            for (unsigned i = 0; i < size; ++i)
                                assert(ram[physical(address + i)] == two[format][i]);
                        else
                            assert(!writes);
                        assert(EM_A0 == address && mmu.mmusr == 0);
                    }
                }
}

static void immediates(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned format = 0; format < 7; ++format)
                for (unsigned tail = 2; tail <= 16; tail += 2) {
                    if (format == 3)
                        continue;
                    fixture(page8k, privileged);
                    uint32_t pc = 0x10000 + page_size - tail;
                    /* DTT identity mappings would fetch from the wrong physical RAM
               if any part of an immediate used a data FC. */
                    memset(ram + pc, 0xff, 20);
                    mmu.dtt[0] = 0x0000c000;
                    code_word(pc, 0xf23c);
                    code_word(pc + 2, 0x4000 | format << 10);
                    unsigned size = sizes[format], transfer = size == 1 ? 2 : size;
                    for (unsigned i = 0; i < transfer; ++i)
                        ram[physical(pc + 4 + i)] = size == 1 ? (i ? 2 : 0) : two[format][i];
                    Syn68kFPUStep result = step(pc, 0);
                    assert(result.next == pc + 4 + transfer && !writes);
                    expect_two();
                }
}

static void faults(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged) {
            fixture(page8k, privileged);
            put32(pte(0x10000), 0);
            Syn68kFPUStep result = step(0x10000, 2);
            assert(result.exception_pc == 0x10000 && result.format == 7 && result.fault_address == 0x10000 && !reads);
            assert(result.access.translation.fault == MMU040_INVALID);
            uint16_t ssw;
            assert(result.access.request == 0x10000 && result.access.size == 2 &&
                   result.access.fc == (privileged ? 6u : 2u));
            assert(syn68k_mmu040_access_ssw(&result.access, &ssw) && ssw == (privileged ? 0x0546 : 0x0542));

            fixture(page8k, privileged);
            uint32_t pc = 0x10000 + page_size - 2;
            code_word(pc, 0xf210);
            put32(pte(pc + 2), 0);
            result = step(pc, 2);
            assert(result.exception_pc == pc && result.fault_address == pc + 2 && result.access.completed == 0);

            fixture(page8k, privileged);
            code_word(0x10000, 0xf210);
            code_word(0x10002, 0x4000);
            EM_A0 = 0x20000 + page_size - 2;
            put32(pte(0x20000 + page_size), 0);
            result = step(0x10000, 2);
            assert(result.fault_address == 0x20000 + page_size && result.access.completed == 2);
            assert(cpu_state.fpu.regs[0].exponent == 0x7fff && !writes);
            assert(result.access.request == EM_A0 && result.access.size == 4);
            assert(syn68k_mmu040_access_ssw(&result.access, &ssw) && ssw == (privileged ? 0x0d05 : 0x0d01));

            /* Even the unused extended-format word belongs to the transfer. */
            fixture(page8k, privileged);
            code_word(0x10000, 0xf210);
            code_word(0x10002, 0x4800);
            EM_A0 = 0x20000 + page_size - 2;
            put32(pte(0x20000 + page_size), 0);
            result = step(0x10000, 2);
            assert(result.fault_address == 0x20000 + page_size && result.access.completed == 2);

            fixture(page8k, privileged);
            pc = 0x10000 + page_size - 6;
            code_word(pc, 0xf23c);
            code_word(pc + 2, 0x4800);
            code_word(pc + 4, 0x4000);
            put32(pte(0x10000 + page_size), 0);
            result = step(pc, 2);
            assert(result.fault_address == 0x10000 + page_size && result.access.completed == 2);

            fixture(page8k, privileged);
            code_word(0x10000, 0xf210);
            code_word(0x10002,
                      0x6800); // Extended store crossing a protected page.
            EM_A0 = 0x20000 + page_size - 6;
            for (unsigned i = 0; i < 12; ++i)
                ram[physical(EM_A0 + i)] = 0xcd;
            cpu_state.fpu.regs[0].exponent = 0x4000;
            cpu_state.fpu.regs[0].significand = UINT64_C(0x8000000000000000);
            put32(pte(0x20000 + page_size), (privileged ? 0x1c000 : 0x14000) | 5);
            result = step(0x10000, 2);
            assert(result.exception_pc == 0x10000 && result.format == 7);
            assert(result.access.completed == 2 && result.fault_address == 0x20000 + page_size &&
                   result.access.translation.fault == MMU040_WRITE_PROTECTED);
            assert(result.access.request == EM_A0 + 4 && result.access.size == 4 && result.access.value == 0x80000000 &&
                   result.access.writing);
            assert(syn68k_mmu040_access_ssw(&result.access, &ssw) && ssw == (privileged ? 0x0c05 : 0x0c01));
            for (unsigned i = 0; i < 12; ++i)
                assert(ram[physical(EM_A0 + i)] == (i < 6 ? two[2][i] : 0xcd));

            fixture(page8k, privileged);
            code_word(0x10000, 0xf210);
            code_word(0x10002, 0x4000);
            EM_A0 = 0x20000;
            fail_physical = physical(EM_A0);
            result = step(0x10000, 2);
            assert(result.access.bus_error && result.access.translation.fault == MMU040_OK &&
                   result.fault_address == EM_A0);
            assert(syn68k_mmu040_access_ssw(&result.access, &ssw) && ssw == (privileged ? 0x0105 : 0x0101));
        }
}

static void frames(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned busy = 0; busy < 2; ++busy)
            for (unsigned tail = 2; tail <= 104; tail += 2) {
                fixture(page8k, 1);
                code_word(0x10000, 0xf200);
                code_word(0x10002, busy ? 0x0420 : 0x5c00);
                if (busy) {
                    cpu_state.fpu.fpcr = 0x200;
                    cpu_state.fpu.regs[0].exponent = 0x3fff;
                    cpu_state.fpu.regs[0].significand = UINT64_C(0x8000000000000000);
                    cpu_state.fpu.regs[1].exponent = 0x4000;
                    cpu_state.fpu.regs[1].significand = UINT64_C(0xc000000000000000);
                }
                step(0x10000, busy ? 0 : 11);
                unsigned size = busy ? 100 : 52;
                assert(cpu_state.fpu.frame_size == size);
                unsigned char expected[100];
                memcpy(expected, cpu_state.fpu.frame, size);
                EM_A0 = 0x20000 + page_size - tail;
                uint32_t address = EM_A0;
                code_word(0x10000, 0xf310);
                step(0x10000, 0);
                for (unsigned i = 0; i < size; ++i)
                    assert(ram[physical(address + i)] == expected[i]);
                assert(cpu_state.fpu.frame_size == 0);
                code_word(0x10000, 0xf358);
                step(0x10000, 0);
                assert(cpu_state.fpu.frame_size == size && EM_A0 == address + size);
            }
    for (unsigned page8k = 0; page8k < 2; ++page8k) {
        fixture(page8k, 1);
        code_word(0x10000, 0xf200);
        code_word(0x10002, 0x5c00);
        step(0x10000, 11); // Produce a real 52-byte unimplemented state frame.
        unsigned char frame[52];
        memcpy(frame, cpu_state.fpu.frame, sizeof frame);
        EM_A0 = 0x20000 + page_size - 18;
        uint32_t address = EM_A0;
        put32(pte(0x20000 + page_size), 0);
        code_word(0x10000, 0xf310);
        Syn68kFPUStep failure = step(0x10000, 2);
        assert(failure.fault_address == 0x20000 + page_size && failure.access.completed == 2);
        assert(cpu_state.fpu.frame_size == 52 && !memcmp(cpu_state.fpu.frame, frame, sizeof frame));
        for (unsigned i = 0; i < 18; ++i)
            assert(ram[physical(address + i)] == frame[i]);
        // Complete the save, then fault midway through a postincrement restore.
        put32(pte(0x20000 + page_size), 0x1c001);
        syn68k_mmu040_flush(&mmu, 1, 0, 0, 1);
        step(0x10000, 0);
        Syn68kFPUState before = cpu_state.fpu;
        put32(pte(0x20000 + page_size), 0);
        syn68k_mmu040_flush(&mmu, 1, 0, 0, 1);
        code_word(0x10000, 0xf358);
        failure = step(0x10000, 2);
        assert(failure.fault_address == 0x20000 + page_size && failure.access.completed == 2 && EM_A0 == address);
        assert(!memcmp(&cpu_state.fpu, &before, sizeof before));
    }
    fixture(0, 0);
    code_word(0x10000, 0xf310);
    EM_A0 = 0x20000;
    Syn68kFPUStep result = step(0x10000, 8);
    assert(result.format == 0 && result.exception_pc == 0x10000 && !writes);
}

int main(void)
{
    all_operand_offsets();
    immediates();
    faults();
    frames();
    fixture(0, 0);
    code_word(0x10000, 0x4e71);
    Syn68kFPUStep result;
    assert(!syn68k_fpu_step(0x10000, &memory, &result) && !result.vector);
    assert(!syn68k_fpu_step(0x10000, &memory, NULL));
    printf("%u mapped FPU operand/immediate/frame/fault cases passed\n", cases);
    return 0;
}
