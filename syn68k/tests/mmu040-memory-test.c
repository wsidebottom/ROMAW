/*+===================================================================
  File:      mmu040-memory-test.c

  Summary:   Implements mmu040 memory test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Original mapped operand fixtures; no Apple assets or host guest pointers. */
#include "mmu040-memory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char ram[0x10000];
static Syn68kMMU040 mmu;
static unsigned cases, ssw_cases, calls, fail_call, table_reads, table_writes;
static uint32_t failed_table;

static struct {
    uint32_t physical, attributes;
    unsigned size, writing, fc;
} operands[8];

static uint32_t get32(uint32_t address)
{
    assert(address <= sizeof ram - 4);
    return (uint32_t)ram[address] << 24 | (uint32_t)ram[address + 1] << 16 | (uint32_t)ram[address + 2] << 8 |
           ram[address + 3];
}

static void put32(uint32_t address, uint32_t value)
{
    assert(address <= sizeof ram - 4);
    for (unsigned i = 0; i < 4; ++i)
        ram[address + i] = value >> ((3 - i) * 8);
}

static int table_read(void* opaque, uint32_t address, uint32_t* value)
{
    assert(opaque == &mmu && !(address & 3));
    ++table_reads;
    if (address == failed_table || address > sizeof ram - 4)
        return 0;
    *value = get32(address);
    return 1;
}

static int table_update(void* opaque, uint32_t address, uint32_t value, int locked)
{
    assert(opaque == &mmu && !(address & 3));
    ++table_writes;
    if (address == failed_table || address > sizeof ram - 4)
        return 0;
    put32(address, locked ? get32(address) | value : value);
    return 1;
}

static int operand(void* opaque, uint32_t physical, unsigned size, uint32_t attributes, int writing, unsigned fc)
{
    assert(opaque == ram && calls < 8 && size >= 1 && size <= 4);
    operands[calls].physical = physical;
    operands[calls].size = size;
    operands[calls].fc = fc;
    operands[calls].attributes = attributes;
    operands[calls++].writing = writing;
    return calls != fail_call;
}

static int read_bytes(void* opaque, uint32_t physical, unsigned char* bytes, unsigned size, uint32_t attributes,
                      unsigned fc)
{
    if (!operand(opaque, physical, size, attributes, 0, fc))
        return 0;
    /* The high alias is a fixture for physical address wrap, not host wrapping. */
    uint32_t offset = physical >= 0xfffffff0u ? physical & 0xffff : physical;
    assert(offset <= sizeof ram - size);
    memcpy(bytes, ram + offset, size);
    return 1;
}

static int write_bytes(void* opaque, uint32_t physical, const unsigned char* bytes, unsigned size, uint32_t attributes,
                       unsigned fc)
{
    if (!operand(opaque, physical, size, attributes, 1, fc))
        return 0;
    uint32_t offset = physical >= 0xfffffff0u ? physical & 0xffff : physical;
    assert(offset <= sizeof ram - size);
    memcpy(ram + offset, bytes, size);
    return 1;
}

static Syn68kMMU040Memory memory = {.mmu = &mmu,
                                    .tables = {&mmu, table_read, table_update},
                                    .opaque = ram,
                                    .read = read_bytes,
                                    .write = write_bytes,
                                    .native = NULL};

static unsigned fixture(int page8k)
{
    memory.aligned_transfers = 0;
    syn68k_mmu040_init(&mmu);
    memset(ram, 0, sizeof ram);
    mmu.tc = page8k ? 0xc000 : 0x8000;
    mmu.urp = 0x1000;
    mmu.srp = 0x2000;
    put32(0x1000, 0x3002);
    put32(0x2000, 0x4002);
    put32(0x3000, 0x5002);
    put32(0x4000, 0x6002);
    unsigned index = page8k ? 15 : 31;
    put32(0x5000 + index * 4, 0x8001);
    put32(0x5004 + index * 4, 0xc061);
    put32(0x6000 + index * 4, 0xa081);
    put32(0x6004 + index * 4, 0xe0a1);
    calls = fail_call = table_reads = table_writes = 0;
    failed_table = 0xffffffff;
    return 0x5000 + index * 4;
}

static void success(Syn68kMMU040Access access, unsigned size)
{
    ++cases;
    assert(access.translation.fault == MMU040_OK && !access.bus_error);
    assert(access.completed == size);
}

static void all_offsets(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k) {
        unsigned page_size = page8k ? 8192 : 4096;
        for (unsigned offset = 0; offset < page_size; ++offset)
            for (unsigned size = 1; size <= 4; size *= 2)
                for (unsigned supervisor = 0; supervisor < 2; ++supervisor)
                    for (unsigned writing = 0; writing < 2; ++writing) {
                        fixture(page8k);
                        uint32_t logical = 0x20000 - page_size + offset;
                        uint32_t physical = (supervisor ? 0xa000 : 0x8000) + offset;
                        unsigned first = page_size - offset;
                        if (first > size)
                            first = size;
                        uint32_t second = supervisor ? 0xe000 : 0xc000;
                        for (unsigned i = 0; i < size; ++i)
                            ram[i < first ? physical + i : second + i - first] = 0x51 + i;
                        uint32_t value = 0xfedcba98;
                        Syn68kMMU040Access access =
                            writing ? syn68k_mmu040_write(&memory, logical, supervisor ? 5 : 1, size, value)
                                    : syn68k_mmu040_read(&memory, logical, supervisor ? 5 : 1, size, 0, &value);
                        success(access, size);
                        assert(access.request == logical && access.size == size &&
                               access.fc == (supervisor ? 5u : 1u) && access.writing == writing && !access.rmw);
                        assert(calls == (first == size ? 1 : 3));
                        assert(operands[0].physical == physical && operands[0].size == first &&
                               operands[0].writing == writing);
                        assert(operands[0].attributes == (supervisor ? 0x80u : 0) + (writing ? 16u : 0));
                        if (first != size) {
                            assert(operands[1].physical == physical && operands[1].size == first &&
                                   operands[1].attributes == operands[0].attributes && operands[1].writing == writing);
                            assert(operands[2].physical == second && operands[2].size == size - first);
                            assert(operands[2].attributes == (supervisor ? 0xa0u : 0x60u) + (writing ? 16u : 0));
                        }
                        if (writing)
                            for (unsigned i = 0; i < size; ++i)
                                assert(ram[i < first ? physical + i : second + i - first] ==
                                       ((0xfedcba98u >> ((size - i - 1) * 8)) & 255));
                        else
                            assert(value == (size == 1 ? 0x51u : size == 2 ? 0x5152u : 0x51525354u));
                        assert(access.value ==
                               (writing ? 0xfedcba98u & (size == 4 ? UINT32_MAX : (1u << (size * 8)) - 1) : value));
                        for (unsigned i = 0; i < calls; ++i)
                            assert(operands[i].fc == (supervisor ? 5u : 1u));
                        assert(mmu.mmusr == 0);
                    }
    }
}

static void failures(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned writing = 0; writing < 2; ++writing)
            for (unsigned failure = 0; failure < 6; ++failure) {
                unsigned pte = fixture(page8k);
                unsigned size = page8k ? 8192 : 4096;
                ram[0x8000 + size - 1] = 0x12;
                ram[0xc000] = 0x34;
                if (failure == 0)
                    put32(pte, 0);
                else if (failure == 1)
                    put32(pte + 4, 0);
                else if (failure == 2)
                    put32(pte + 4, 0xc005); // Readable, write protected.
                else if (failure == 3)
                    failed_table = pte + 4;
                else
                    fail_call = failure == 4 ? 1 : 3; // Initial first page or second page after replay.
                uint32_t value = 0xdeadbeef;
                Syn68kMMU040Access access = writing ? syn68k_mmu040_write(&memory, 0x1ffff, 1, 2, 0x5678)
                                                    : syn68k_mmu040_read(&memory, 0x1ffff, 1, 2, 0, &value);
                ++cases;
                if (failure == 2 && !writing) {
                    assert(access.completed == 2 && value == 0x1234);
                    continue;
                }
                unsigned completed = failure == 0 || failure == 4 ? 0 : 1;
                assert(access.completed == completed && access.logical == 0x1ffff + completed);
                assert(value == 0xdeadbeef);
                assert(access.request == 0x1ffff && access.size == 2 && access.fc == 1 && access.writing == writing &&
                       !access.rmw);
                assert(access.value == (writing ? 0x5678u : completed ? 0x12u : 0));
                uint16_t ssw = 0xffff;
                assert(syn68k_mmu040_access_ssw(&access, &ssw));
                unsigned expected = writing ? 0x0041 : 0x0141;
                if (failure < 4)
                    expected += completed ? 0x0c00 : 0x0400;
                assert(ssw == expected);
                ++ssw_cases;
                if (failure >= 4)
                    assert(access.bus_error && access.translation.fault == MMU040_OK);
                else {
                    assert(!access.bus_error);
                    assert(access.translation.fault == (failure == 2   ? MMU040_WRITE_PROTECTED
                                                        : failure == 3 ? MMU040_TABLE_BUS_ERROR
                                                                       : MMU040_INVALID));
                }
                assert(ram[0xc000] == 0x34);
                assert(ram[0x8000 + size - 1] == (writing && completed ? 0x56 : 0x12));
                // Completed first-page history is not rolled back after a fault.
                if (failure != 0)
                    assert((get32(pte) & (writing ? 24 : 8)) == (writing ? 24u : 8u));
                assert(mmu.mmusr == 0);
            }
}

static void spaces_and_rmw(void)
{
    unsigned pte = fixture(0);
    ram[0x8000] = 0x12;
    ram[0xc000] = 0x34;
    uint32_t value = 0;
    success(syn68k_mmu040_read(&memory, 0x1f000, 2, 1, 0, &value), 1);
    assert(value == 0x12);
    put32(pte, 0xc001);
    calls = 0;
    success(syn68k_mmu040_read(&memory, 0x1f000, 1, 1, 0, &value), 1);
    assert(value == 0x34); // Data ATC did not use the stale instruction entry.
    calls = 0;
    success(syn68k_mmu040_read(&memory, 0x1f000, 2, 1, 0, &value), 1);
    assert(value == 0x12);
    // A read portion of an RMW checks write protection before reading anything.
    pte = fixture(0);
    put32(pte, 0x8005);
    Syn68kMMU040Access access = syn68k_mmu040_read(&memory, 0x1f000, 1, 1, 1, &value);
    ++cases;
    assert(access.translation.fault == MMU040_WRITE_PROTECTED && !calls && !access.completed && !(get32(pte) & 16));
    pte = fixture(0);
    success(syn68k_mmu040_read(&memory, 0x1f000, 1, 1, 1, &value), 1);
    assert((get32(pte) & 24) == 24 && operands[0].attributes == 16);
    assert(!operands[0].writing); // Permission intent is not a physical write.
    // MOVES-normalized FCs are supplied by the CPU, not inferred by this layer.
    fixture(0);
    mmu.itt[0] = 0x0000c004;
    access = syn68k_mmu040_read(&memory, 0x1f000, 2, 1, 1, &value);
    ++cases;
    assert(access.translation.fault == MMU040_WRITE_PROTECTED && !calls);
    success(syn68k_mmu040_read(&memory, 0x1f000, 1, 1, 0, &value), 1);
}

static void wrap_and_bad_requests(void)
{
    for (unsigned fc = 0; fc < 8; ++fc)
        for (unsigned page8k = 0; page8k < 2; ++page8k)
            for (unsigned writing = 0; writing < 2; ++writing) {
                fixture(page8k);
                mmu.tc &= ~0x8000;
                ram[0xffff] = 0x12;
                ram[0] = 0x34;
                uint32_t value = 0;
                Syn68kMMU040Access access = writing ? syn68k_mmu040_write(&memory, 0xffffffff, fc, 2, 0xabcd)
                                                    : syn68k_mmu040_read(&memory, 0xffffffff, fc, 2, 0, &value);
                success(access, 2);
                unsigned last = (fc & 3) == 2 ? 1 : 2;
                assert(!table_reads && !table_writes && calls == last + 1);
                for (unsigned i = 0; i < last; ++i)
                    assert(operands[i].physical == 0xffffffff && !operands[i].attributes && operands[i].fc == fc);
                assert(operands[last].physical == 0 && !operands[last].attributes && operands[last].fc == fc);
                assert(writing ? ram[0xffff] == 0xab && ram[0] == 0xcd : value == 0x1234);
            }
    for (unsigned bad = 0; bad < 10; ++bad) {
        fixture(0);
        Syn68kMMU040Memory broken = memory;
        uint32_t value = 0x12345678;
        if (bad == 1)
            broken.mmu = NULL;
        if (bad == 2)
            broken.read = NULL;
        if (bad == 3)
            broken.write = NULL;
        if (bad == 9)
            broken.aligned_transfers = 2;
        Syn68kMMU040Access access = bad == 3 ? syn68k_mmu040_write(&broken, 0, 1, 4, value)
                                             : syn68k_mmu040_read(bad == 0 ? NULL : &broken, 0, bad == 4 ? 8 : 1,
                                                                  bad == 5   ? 0
                                                                  : bad == 6 ? 3
                                                                  : bad == 7 ? ~0u
                                                                             : 4,
                                                                  0, bad == 8 ? NULL : &value);
        ++cases;
        assert(access.translation.fault == MMU040_BAD_REQUEST && !access.completed && !access.bus_error);
        assert(value == 0x12345678 && !table_reads && !table_writes && !calls);
    }
}

static void fault_status_fields(void)
{
    /* MC68040UM table 3-2 and figures 8-7/8-8, literal TT/TM encodings. */
    static const unsigned transfers[] = {0x10, 1, 2, 0x13, 0x14, 5, 6, 0x17};

    static const struct {
        unsigned size;
        uint16_t read, write;
    } sizes[] = {{1, 0x0120, 0x0020}, {2, 0x0140, 0x0040}, {4, 0x0100, 0x0000}};

    for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; ++i)
        for (unsigned fc = 0; fc < 8; ++fc)
            for (unsigned writing = 0; writing < 2; ++writing)
                for (unsigned locked = 0; locked < 2; ++locked) {
                    fixture(0);
                    mmu.tc = 0;
                    fail_call = 1;
                    uint32_t value = 0x12345678;
                    Syn68kMMU040Access access =
                        writing ? (locked ? syn68k_mmu040_write_rmw(&memory, 0x8000, fc, sizes[i].size, value)
                                          : syn68k_mmu040_write(&memory, 0x8000, fc, sizes[i].size, value))
                                : syn68k_mmu040_read(&memory, 0x8000, fc, sizes[i].size, locked, &value);
                    uint16_t ssw = 0xffff;
                    assert(access.bus_error && !access.completed && access.writing == writing && access.rmw == locked);
                    assert(syn68k_mmu040_access_ssw(&access, &ssw));
                    assert(ssw == ((writing ? sizes[i].write : sizes[i].read) | transfers[fc] | (locked ? 0x0200 : 0)));
                    ++ssw_cases;
                }
    unsigned pte = fixture(0);
    put32(pte, 0x8005);
    uint32_t value = 0x12345678;
    Syn68kMMU040Access access = syn68k_mmu040_read(&memory, 0x1f000, 1, 1, 1, &value);
    uint16_t ssw;
    assert(syn68k_mmu040_access_ssw(&access, &ssw) && ssw == 0x0721);
    assert(value == 0x12345678 && !access.value && !calls);
    access = syn68k_mmu040_write_rmw(&memory, 0x1f000, 1, 1, 0x12345678);
    assert(syn68k_mmu040_access_ssw(&access, &ssw) && ssw == 0x0621);
    assert(access.value == 0x78 && !calls);
    ssw_cases += 2;
    access = syn68k_mmu040_read(NULL, 0, 1, 1, 0, &value);
    ssw = 0xa55a;
    assert(!syn68k_mmu040_access_ssw(&access, &ssw) && ssw == 0xa55a);
    assert(!syn68k_mmu040_access_ssw(NULL, &ssw) && ssw == 0xa55a);
    assert(!syn68k_mmu040_access_ssw(&access, NULL));
    fixture(0);
    access = syn68k_mmu040_read(&memory, 0x1f000, 1, 1, 0, &value);
    assert(!syn68k_mmu040_access_ssw(&access, &ssw) && ssw == 0xa55a);
    ++ssw_cases;
    for (unsigned fc = 0; fc < 8; ++fc) {
        if (fc == 1 || fc == 2 || fc == 5 || fc == 6)
            continue;
        access = (Syn68kMMU040Access){.translation = {.fault = MMU040_INVALID}, .size = 4, .fc = fc};
        assert(!syn68k_mmu040_access_ssw(&access, &ssw) && ssw == 0xa55a);
        ++ssw_cases;
    }
}

/* MC68040UM 7.3: odd longwords use byte/word/byte bus transfers. */
static void aligned_bus_transfers(void)
{
    static const struct {
        unsigned size, lane, count, widths[3];
    } vectors[] = {{1, 0, 1, {1}}, {1, 1, 1, {1}},       {1, 2, 1, {1}},    {1, 3, 1, {1}},
                   {2, 0, 1, {2}}, {2, 1, 2, {1, 1}},    {2, 2, 1, {2}},    {2, 3, 2, {1, 1}},
                   {4, 0, 1, {4}}, {4, 1, 3, {1, 2, 1}}, {4, 2, 2, {2, 2}}, {4, 3, 3, {1, 2, 1}}};

    /* Literal page-cross bus orders, including the discarded initial pass. */
    static const struct {
        unsigned size, lane, count, widths[5], offsets[5];
    } cross[] = {{2, 3, 3, {1, 1, 1}, {0, 0, 1}},
                 {4, 1, 5, {1, 2, 1, 2, 1}, {0, 1, 0, 1, 3}},
                 {4, 2, 3, {2, 2, 2}, {0, 0, 2}},
                 {4, 3, 4, {1, 1, 2, 1}, {0, 0, 1, 3}}};

    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned supervisor = 0; supervisor < 2; ++supervisor)
            for (unsigned boundary = 0; boundary < 2; ++boundary)
                for (unsigned v = 0; v < sizeof vectors / sizeof *vectors; ++v) {
                    unsigned count = vectors[v].count, widths[5] = {0}, offsets[5] = {0};
                    memcpy(widths, vectors[v].widths, sizeof vectors[v].widths);
                    for (unsigned i = 1; i < count; ++i)
                        offsets[i] = offsets[i - 1] + widths[i - 1];
                    if (boundary)
                        for (unsigned i = 0; i < sizeof cross / sizeof *cross; ++i)
                            if (cross[i].size == vectors[v].size && cross[i].lane == vectors[v].lane) {
                                count = cross[i].count;
                                memcpy(widths, cross[i].widths, sizeof widths);
                                memcpy(offsets, cross[i].offsets, sizeof offsets);
                            }
                    for (unsigned writing = 0; writing < 2; ++writing)
                        for (unsigned fail = 0; fail <= count; ++fail) {
                            fixture(page8k);
                            memory.aligned_transfers = 1;
                            unsigned size = vectors[v].size, page_size = page8k ? 8192 : 4096;
                            uint32_t logical = (boundary ? 0x1fffc : 0x20000 - page_size) + vectors[v].lane;
                            uint32_t physical[4];
                            unsigned written[4] = {0};
                            for (unsigned i = 0; i < size; ++i) {
                                uint32_t a = logical + i;
                                physical[i] = a < 0x20000 ? (supervisor ? 0xa000 : 0x8000) + (a & (page_size - 1))
                                                          : (supervisor ? 0xe000 : 0xc000) + (a & (page_size - 1));
                                ram[physical[i]] = 0x51 + i;
                            }
                            fail_call = fail;
                            uint32_t value = 0xfedcba98;
                            unsigned fc = supervisor ? 5 : 1;
                            Syn68kMMU040Access access = writing
                                                            ? syn68k_mmu040_write(&memory, logical, fc, size, value)
                                                            : syn68k_mmu040_read(&memory, logical, fc, size, 0, &value);
                            unsigned completed = fail ? offsets[fail - 1] : size;
                            for (unsigned i = 0; i < (fail ? fail : count); ++i) {
                                assert(operands[i].physical == physical[offsets[i]] && operands[i].size == widths[i]);
                                assert(!(operands[i].physical & (operands[i].size - 1)));
                                assert(operands[i].fc == fc && operands[i].writing == writing);
                                if (!fail || i + 1 < fail)
                                    for (unsigned j = 0; j < widths[i]; ++j)
                                        written[offsets[i] + j] = 1;
                            }
                            assert(calls == (fail ? fail : count));
                            assert(access.completed == completed && access.bus_error == !!fail);
                            assert(access.request == logical && access.size == size && access.fc == fc &&
                                   access.translation.fault == MMU040_OK);
                            if (fail) {
                                uint16_t ssw;
                                assert(access.logical == logical + completed);
                                assert(syn68k_mmu040_access_ssw(&access, &ssw));
                                assert(ssw == (fc | (size == 1 ? 0x20 : size == 2 ? 0x40 : 0) | (writing ? 0 : 0x100)));
                            }
                            uint32_t prefix = 0;
                            for (unsigned i = 0; i < size; ++i) {
                                if (i < completed)
                                    prefix = prefix << 8 | (0x51 + i);
                                unsigned expected =
                                    writing && written[i] ? (0xfedcba98u >> ((size - i - 1) * 8)) & 255 : 0x51 + i;
                                assert(ram[physical[i]] == expected);
                            }
                            assert(
                                access.value ==
                                (writing ? 0xfedcba98u & (size == 4 ? UINT32_MAX : (1u << (size * 8)) - 1) : prefix));
                            assert(value == (writing || fail ? 0xfedcba98 : prefix));
                            ++cases;
                        }
                }
    memory.aligned_transfers = 0;
}

int main(void)
{
    aligned_bus_transfers();
    all_offsets();
    failures();
    spaces_and_rmw();
    wrap_and_bad_requests();
    fault_status_fields();
    printf("%u mapped MMU operand cases passed\n", cases);
    printf("%u access-fault metadata/SSW cases passed\n", ssw_cases);
    return 0;
}
