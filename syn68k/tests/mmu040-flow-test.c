/*+===================================================================
  File:      mmu040-flow-test.c

  Summary:   Implements mmu040 flow test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Mapped control transfers, extension fetches and exception metadata. */
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

static void flags(unsigned value)
{
    cpu_state.ccc = value & 1;
    cpu_state.ccv = value & 2;
    cpu_state.ccnz = !(value & 4);
    cpu_state.ccn = value & 8;
    cpu_state.ccx = value & 16;
}

static unsigned status(void)
{
    return (!!cpu_state.ccc) | ((!!cpu_state.ccv) << 1) | ((!cpu_state.ccnz) << 2) | ((!!cpu_state.ccn) << 3) |
           ((!!cpu_state.ccx) << 4);
}

static int condition(unsigned cc, unsigned f)
{
    int c = !!(f & 1), v = !!(f & 2), z = !!(f & 4), n = !!(f & 8);
    const int values[] = {1,  0, !c && !z, c || z, !c,     c,      !z,           z,
                          !v, v, !n,       n,      n == v, n != v, !z && n == v, z || n != v};
    return values[cc];
}

static uint32_t data(unsigned address, unsigned size)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < size; ++i)
        value = value << 8 | ram[physical(address + i)];
    return value;
}

static void branches(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned cc = 0; cc < 16; ++cc)
                for (unsigned form = 0; form < 4; ++form)
                    for (unsigned f = 0; f < 32; ++f) {
                        fixture(page8k, privileged);
                        unsigned pc = 0x10000 + page_size - 2;
                        unsigned tail = form < 2 ? 0 : form == 2 ? 2 : 4;
                        int displacement = form == 0 ? 6 : form == 1 ? -8 : form == 2 ? 0x1234 : -0x123456;
                        code_word(pc, 0x6000 | cc << 8 | (form < 2 ? displacement & 255 : form == 2 ? 0 : 255));
                        if (tail == 2)
                            code_word(pc + 2, displacement);
                        else if (tail == 4) {
                            code_word(pc + 2, (uint32_t)displacement >> 16);
                            code_word(pc + 4, displacement);
                        }
                        flags(f);
                        EM_A7 = 0x20000 + page_size + 2;
                        Syn68kIntegerStep result;
                        assert(syn68k_integer_step(pc, &memory, &result));
                        assert(!result.vector);
                        assert(result.next == (cc == 1 || condition(cc, f) ? pc + 2 + displacement : pc + 2 + tail));
                        assert(status() == f && EM_A7 == 0x20000 + page_size + (cc == 1 ? -2 : 2));
                        if (cc == 1)
                            assert(data(EM_A7, 4) == pc + 2 + tail);
                        ++cases;
                    }
}

static void counters(void)
{
    const unsigned values[] = {0, 1, 2, 0xFFFF};
    for (unsigned privileged = 0; privileged < 2; ++privileged)
        for (unsigned cc = 0; cc < 16; ++cc)
            for (unsigned reg = 0; reg < 8; ++reg)
                for (unsigned sample = 0; sample < 4; ++sample)
                    for (unsigned f = 0; f < 32; ++f) {
                        fixture(0, privileged);
                        unsigned pc = 0x10FFE;
                        code_word(pc, 0x50C8 | cc << 8 | reg);
                        code_word(pc + 2, -6);
                        unsigned value = values[sample], taken = !condition(cc, f);
                        EM_DREG(reg) = 0x12340000 | value;
                        flags(f);
                        Syn68kIntegerStep result;
                        assert(syn68k_integer_step(pc, &memory, &result));
                        assert(!result.vector && status() == f);
                        assert(EM_DREG(reg) == (0x12340000 | ((value - taken) & 0xFFFF)));
                        assert(result.next == (taken && value ? pc - 4 : pc + 4));
                        ++cases;
                    }
}

static void stack_and_faults(void)
{
    for (unsigned privileged = 0; privileged < 2; ++privileged)
        for (unsigned page8k = 0; page8k < 2; ++page8k)
            for (unsigned kind = 0; kind < 5; ++kind) {
                fixture(page8k, privileged);
                flags(31);
                unsigned pc = 0x10000 + page_size - 2;
                code_word(pc, kind == 0   ? 0x51C8
                              : kind == 1 ? 0x6700
                              : kind == 2 ? 0x6100
                              : kind == 3 ? 0x4E50
                                          : 0x4E73);
                EM_D0 = 123;
                EM_A0 = 0x76543210;
                EM_A7 = 0x20000 + page_size + 2;
                if (kind < 4)
                    put32(pte(0x10000 + page_size), 0);
                Syn68kIntegerStep result;
                assert(syn68k_integer_step(pc, &memory, &result));
                if (kind < 4) {
                    assert(result.vector == 2 && result.fault_address == pc + 2);
                    assert(EM_D0 == 123 && EM_A0 == 0x76543210 && EM_A7 == 0x20000 + page_size + 2 && status() == 31);
                    assert(result.access.fc == (privileged ? 6 : 2));
                }
                else if (!privileged)
                    assert(result.vector == 8 && !writes && status() == 31);
                ++cases;
            }
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned format = 0; format < 4; ++format)
            for (unsigned f = 0; f < 32; ++f) {
                fixture(page8k, 1);
                unsigned stack = 0x20000 + page_size - 4;
                unsigned frame = format == 3 ? 7 : format == 0 ? 0 : format + 1;
                unsigned length = frame == 7 ? 60 : frame ? 12 : 8;
                code_word(0x10000, 0x4E73);
                EM_A7 = stack;
                unsigned char bytes[60] = {0};
                bytes[0] = 0x20;
                bytes[1] = f;
                bytes[2] = 0x12;
                bytes[3] = 0x34;
                bytes[4] = 0x56;
                bytes[5] = 0x78;
                bytes[6] = frame << 4;
                for (unsigned i = 0; i < length; ++i)
                    ram[physical(stack + i)] = bytes[i];
                Syn68kIntegerStep result;
                assert(syn68k_integer_step(0x10000, &memory, &result));
                assert(!result.vector && result.next == 0x12345678 && EM_A7 == stack + length && status() == f);
                ++cases;
            }
}

static void movec(void)
{
    const unsigned selectors[] = {3, 4, 5, 6, 7, 0x805, 0x806, 0x807};
    const uint32_t masks[] = {0xC000,     0xFFFFE364, 0xFFFFE364, 0xFFFFE364,
                              0xFFFFE364, 0xFFFFFFF7, 0xFFFFFE00, 0xFFFFFE00};
    const uint32_t values[] = {0, 0xFFFFFFFF, 0x12345678, 0x8000, 0xC000};
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned selector = 0; selector < 8; ++selector)
                for (unsigned reg = 0; reg < 16; ++reg)
                    for (unsigned writing = 0; writing < 2; ++writing)
                        for (unsigned sample = 0; sample < 5; ++sample) {
                            fixture(page8k, privileged);
                            uint32_t* controls[] = {&mmu.tc,     &mmu.itt[0], &mmu.itt[1], &mmu.dtt[0],
                                                    &mmu.dtt[1], &mmu.mmusr,  &mmu.urp,    &mmu.srp};
                            uint32_t original = *controls[selector];
                            EM_DREG(reg) = values[sample];
                            unsigned pc = 0x10000 + page_size - 2;
                            code_word(pc, writing ? 0x4E7B : 0x4E7A);
                            code_word(pc + 2, reg << 12 | selectors[selector]);
                            flags(31);
                            Syn68kIntegerStep result;
                            assert(syn68k_integer_step(pc, &memory, &result));
                            assert(status() == 31);
                            if (!privileged) {
                                assert(result.vector == 8 && reads == 1);
                                assert(*controls[selector] == original && EM_DREG(reg) == values[sample]);
                            }
                            else {
                                assert(!result.vector && result.next == pc + 4);
                                assert(*controls[selector] == (writing ? values[sample] & masks[selector] : original));
                                assert(EM_DREG(reg) == (writing ? values[sample] : original & masks[selector]));
                            }
                            ++cases;
                        }
}

static void mmu_instructions(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned fc = 0; fc < 8; ++fc)
                for (unsigned reg = 0; reg < 8; ++reg)
                    for (unsigned read = 0; read < 2; ++read) {
                        fixture(page8k, privileged);
                        code_word(0x10000, (read ? 0xF568 : 0xF548) | reg);
                        EM_AREG(reg) = 0x20040;
                        cpu_state.dfc = fc;
                        cpu_state.sfc = 7 - fc;
                        mmu.mmusr = 0x12345678;
                        flags(31);
                        Syn68kIntegerStep result;
                        assert(syn68k_integer_step(0x10000, &memory, &result));
                        assert(status() == 31 && reads == 1 && !writes);
                        if (!privileged)
                            assert(result.vector == 8 && mmu.mmusr == 0x12345678);
                        else {
                            uint32_t expected = fc == 5 || fc == 6 ? 0x18001 | (read ? 0 : 16) : 0;
                            assert(!result.vector && result.next == 0x10002 && mmu.mmusr == expected);
                        }
                        ++cases;
                    }
    const unsigned flush[] = {0xF500, 0xF508, 0xF510, 0xF518};
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned fc = 0; fc < 8; ++fc)
                for (unsigned kind = 0; kind < 4; ++kind) {
                    fixture(page8k, privileged);
                    code_word(0x10000, flush[kind]);
                    EM_A0 = 0x28040;
                    unsigned shift = page8k ? 13 : 12, page = EM_A0 >> shift, set = page & 15;
                    for (unsigned bank = 0; bank < 2; ++bank)
                        for (unsigned way = 0; way < 4; ++way)
                            mmu.atc[bank][set][way] = (Syn68kMMU040Entry){.logical_page = page + (way == 3),
                                                                          .status = way >= 2 ? 1024 : 0,
                                                                          .valid = 1,
                                                                          .supervisor = way & 1,
                                                                          .shift = shift};
                    mmu.mmusr = 0x12345678;
                    cpu_state.dfc = fc;
                    flags(31);
                    Syn68kIntegerStep result;
                    assert(syn68k_integer_step(0x10000, &memory, &result));
                    assert(status() == 31 && mmu.mmusr == 0x12345678);
                    assert(result.vector == (privileged ? 0U : 8U));
                    for (unsigned bank = 0; bank < 2; ++bank)
                        for (unsigned way = 0; way < 4; ++way) {
                            int removed = privileged && (kind & 1 || way < 2) &&
                                          (kind >= 2 || ((fc == 1 || fc == 2 || fc == 5 || fc == 6) &&
                                                         (way & 1) == !!(fc & 4) && way != 3));
                            assert(mmu.atc[bank][set][way].valid == !removed);
                        }
                    ++cases;
                }
}

static void operand(unsigned logical, unsigned size, uint32_t value)
{
    for (unsigned i = 0; i < size; ++i)
        ram[physical(logical + i)] = value >> ((size - i - 1) * 8);
}

static void bounds(void)
{
    const uint32_t values[] = {0, 1, 3, 7, 10, 0x7FFF, 0x8000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF};
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned size = 2; size <= 4; size += 2)
                for (unsigned sample = 0; sample < 10; ++sample) {
                    fixture(page8k, privileged);
                    unsigned pc = 0x10000 + page_size - 2;
                    code_word(pc, size == 2 ? 0x4190 : 0x4110);
                    EM_D0 = values[sample];
                    EM_A0 = 0x20000 + page_size - 1;
                    operand(EM_A0, size, 7);
                    flags(31);
                    int32_t value = size == 2 ? (int16_t)EM_D0 : (int32_t)EM_D0;
                    Syn68kIntegerStep result;
                    assert(syn68k_integer_step(pc, &memory, &result));
                    assert(result.vector == (value < 0 || value > 7 ? 6U : 0U));
                    assert(status() == (23U | (value < 0 ? 8 : 0)));
                    if (result.vector)
                        assert(result.exception_pc == pc + 2 && result.format == 2 && result.fault_address == pc);
                    ++cases;
                }
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned field = 0; field < 3; ++field)
                for (unsigned address_reg = 0; address_reg < 2; ++address_reg)
                    for (unsigned lower_i = 0; lower_i < 10; ++lower_i)
                        for (unsigned upper_i = 0; upper_i < 10; ++upper_i)
                            for (unsigned value_i = 0; value_i < 10; ++value_i)
                                for (unsigned trap = 0; trap < 2; ++trap) {
                                    fixture(page8k, privileged);
                                    unsigned pc = 0x10000 + page_size - 2, size = 1U << field;
                                    unsigned reg = address_reg ? 9 : 1;
                                    uint32_t mask = size == 1 ? 255 : size == 2 ? 65535 : UINT32_MAX;
                                    uint32_t lower = values[lower_i] & mask;
                                    uint32_t upper = values[upper_i] & mask;
                                    uint32_t value = values[value_i];
                                    code_word(pc, 0x00D0 | field << 9);
                                    code_word(pc + 2, reg << 12 | (trap ? 0x800 : 0));
                                    EM_A0 = 0x20000 + page_size - size;
                                    operand(EM_A0, size, lower);
                                    operand(EM_A0 + size, size, upper);
                                    EM_DREG(reg) = value;
                                    if (address_reg && size < 4) {
                                        lower = size == 1 ? (int8_t)lower : (int16_t)lower;
                                        upper = size == 1 ? (int8_t)upper : (int16_t)upper;
                                    }
                                    else if (!address_reg)
                                        value &= mask;
                                    int outside = lower <= upper ? value < lower || value > upper
                                                                 : value < lower && value > upper;
                                    flags(31);
                                    Syn68kIntegerStep result;
                                    assert(syn68k_integer_step(pc, &memory, &result));
                                    assert(result.vector == (trap && outside ? 6U : 0U));
                                    assert(status() ==
                                           (26U | (outside ? 1 : 0) | (value == lower || value == upper ? 4 : 0)));
                                    if (result.vector)
                                        assert(result.exception_pc == pc + 4 && result.format == 2);
                                    ++cases;
                                }
}

static void move_lines(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned mode = 0; mode < 5; ++mode)
                for (unsigned reg = 0; reg < 8; ++reg)
                    for (unsigned other = 0; other < (mode == 4 ? 8U : 1U); ++other)
                        for (unsigned offset = 0; offset < 16; ++offset) {
                            fixture(page8k, privileged);
                            unsigned source = 0x20000 + page_size - 16;
                            unsigned dest = 0x20000 + page_size + 16;
                            for (unsigned r = 0; r < 8; ++r)
                                EM_AREG(r) = 0x20100;
                            if (mode == 4) {
                                EM_AREG(reg) = source + offset;
                                EM_AREG(other) = other == reg ? source + offset : dest + offset;
                                if (other == reg)
                                    dest = source;
                            }
                            else
                                EM_AREG(reg) = (mode & 1 ? dest : source) + offset;
                            for (unsigned i = 0; i < 16; ++i) {
                                ram[physical(source + i)] = i * 17 + reg;
                                if (dest != source)
                                    ram[physical(dest + i)] = 0xAA;
                            }
                            code_word(0x10000, 0xF600 | mode << 3 | reg);
                            if (mode == 4)
                                code_word(0x10002, 0x8000 | other << 12);
                            else {
                                unsigned absolute = (mode & 1 ? source : dest) + offset;
                                code_word(0x10002, absolute >> 16);
                                code_word(0x10004, absolute);
                            }
                            uint32_t before[16];
                            for (unsigned r = 0; r < 16; ++r)
                                before[r] = EM_DREG(r);
                            if (mode == 4) {
                                before[reg + 8] += 16;
                                if (other != reg)
                                    before[other + 8] += 16;
                            }
                            else if (mode < 2)
                                before[reg + 8] += 16;
                            flags(31);
                            Syn68kIntegerStep result;
                            assert(syn68k_integer_step(0x10000, &memory, &result));
                            assert(!result.vector && status() == 31 &&
                                   result.next == (mode == 4 ? 0x10004U : 0x10006U));
                            for (unsigned r = 0; r < 16; ++r)
                                assert(EM_DREG(r) == before[r]);
                            for (unsigned i = 0; i < 16; ++i)
                                assert(ram[physical(dest + i)] == (unsigned char)(i * 17 + reg));
                            ++cases;
                        }
    for (unsigned privileged = 0; privileged < 2; ++privileged)
        for (unsigned fail_write = 0; fail_write < 2; ++fail_write) {
            fixture(0, privileged);
            unsigned source = 0x20040, dest = 0x21040;
            EM_A0 = source + 3;
            EM_A1 = dest + 7;
            code_word(0x10000, 0xF620);
            code_word(0x10002, 0x9000);
            for (unsigned i = 0; i < 16; ++i) {
                ram[physical(source + i)] = i;
                ram[physical(dest + i)] = 0xAA;
            }
            unsigned fault = fail_write ? dest + 4 : source + 8;
            fail_physical = physical(fault);
            flags(31);
            Syn68kIntegerStep result;
            assert(syn68k_integer_step(0x10000, &memory, &result));
            assert(result.vector == 2 && result.fault_address == fault && result.access.bus_error &&
                   result.access.size == 4 && result.access.writing == fail_write);
            assert(status() == 31 && EM_A0 == source + 3 && EM_A1 == dest + 7);
            for (unsigned i = 0; i < 16; ++i)
                assert(ram[physical(dest + i)] == (fail_write && i < 4 ? i : 0xAA));
            ++cases;
        }
}

int main(void)
{
    branches();
    counters();
    stack_and_faults();
    movec();
    mmu_instructions();
    bounds();
    move_lines();
    printf("%u mapped control flow, stack, RTE and fetch-fault cases passed.\n", cases);
    return 0;
}
