/*+===================================================================
  File:      mmu040-test.c

  Summary:   Implements mmu040 test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Original descriptor-bus and translation-policy fixtures for MC68040UM 3. */
#include "mmu040.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned cases;
static unsigned char ram[0x10000];

static struct {
    uint32_t address, value;
    unsigned kind; /* 0 read, 1 write, 2 locked U-bit RMW. */
} events[64];

static unsigned event_count, fail_event;

static uint32_t get(uint32_t address)
{
    assert(address <= sizeof ram - 4 && !(address & 3));
    return (uint32_t)ram[address] << 24 | (uint32_t)ram[address + 1] << 16 | (uint32_t)ram[address + 2] << 8 |
           ram[address + 3];
}

static void put(uint32_t address, uint32_t value)
{
    assert(address <= sizeof ram - 4 && !(address & 3));
    for (unsigned i = 0; i < 4; ++i)
        ram[address + i] = value >> (24 - i * 8);
}

static int record(uint32_t address, uint32_t value, unsigned kind)
{
    assert(event_count < 64);
    events[event_count].address = address;
    events[event_count].value = value;
    events[event_count++].kind = kind;
    return event_count != fail_event && address <= sizeof ram - 4 && !(address & 3);
}

static int read32(void* opaque, uint32_t address, uint32_t* value)
{
    assert(opaque == ram);
    if (!record(address, 0, 0))
        return 0;
    *value = get(address);
    return 1;
}

static int update32(void* opaque, uint32_t address, uint32_t value, int locked)
{
    assert(opaque == ram);
    if (!record(address, value, locked ? 2 : 1))
        return 0;
    put(address, locked ? get(address) | value : value);
    return 1;
}

static const Syn68kMMU040Bus bus = {ram, read32, update32};
static Syn68kMMU040 mmu;

static void fixture(int page8k)
{
    memset(ram, 0, sizeof ram);
    syn68k_mmu040_init(&mmu);
    mmu.tc = page8k ? 0xc000 : 0x8000;
    mmu.urp = 0x1000;
    mmu.srp = 0x4000;
    event_count = fail_event = 0;
}

/* Literal addresses from the manual's $76543210 example; separate user and
   supervisor roots share pointer/page tables. No production walker is used. */
static void example(int page8k, uint32_t root_bits, uint32_t pointer_bits, uint32_t page_bits)
{
    fixture(page8k);
    put(0x10ec, 0x2002 | root_bits);
    put(0x40ec, 0x2002 | root_bits);
    put(0x2054, 0x3002 | pointer_bits);
    put(page8k ? 0x3004 : 0x300c, 0xabcde001 | page_bits);
}

static Syn68kMMU040Result translate(uint32_t address, unsigned fc, int writing)
{
    ++cases;
    return syn68k_mmu040_translate(&mmu, &bus, address, fc, writing);
}

static void basic_and_history(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned supervisor = 0; supervisor < 2; ++supervisor)
            for (unsigned instruction = 0; instruction < 2; ++instruction)
                for (unsigned writing = 0; writing < 2; ++writing) {
                    example(page8k, 0, 0, 0x760);
                    unsigned fc = 1 + instruction + supervisor * 4;
                    Syn68kMMU040Result result = translate(0x76543210, fc, writing);
                    assert(result.fault == MMU040_OK && !result.cached);
                    assert(result.physical == (page8k ? 0xabcdf210u : 0xabcde210u));
                    assert(result.status == (0xabcde761u | (writing ? 16 : 0)));
                    assert(result.attributes == (0x760u | (writing ? 16 : 0)));
                    assert(mmu.mmusr == 0);
                    unsigned root = supervisor ? 0x40ec : 0x10ec;
                    unsigned page = page8k ? 0x3004 : 0x300c;
                    assert(event_count == 6);
                    assert(events[0].address == root && events[0].kind == 0);
                    assert(events[1].address == 0x2054 && events[1].kind == 0);
                    assert(events[2].address == root && events[2].kind == 1);
                    assert(events[3].address == page && events[3].kind == 0);
                    assert(events[4].address == 0x2054 && events[4].kind == 1);
                    assert(events[5].address == page && events[5].kind == (writing ? 1 : 2));
                    assert(get(root) == 0x200a && get(0x2054) == 0x300a);
                    assert(get(page) == (0xabcde769u | (writing ? 16 : 0)));
                    event_count = 0;
                    result = translate(0x76543211, fc, writing);
                    assert(result.fault == MMU040_OK && result.cached && !event_count);
                    assert(result.physical == (page8k ? 0xabcdf211u : 0xabcde211u));
                }

    // Updating M on an ATC read hit performs a new search, including a remap.
    example(0, 8, 8, 8);
    assert(translate(0x76543210, 1, 0).physical == 0xabcde210);
    put(0x300c, 0x12345009);
    event_count = 0;
    Syn68kMMU040Result result = translate(0x76543210, 1, 1);
    assert(!result.cached && result.physical == 0x12345210);
    assert(event_count == 4 && get(0x300c) == 0x12345019);
    put(0x300c, 0x99999001);
    event_count = 0;
    result = translate(0x76543210, 1, 1);
    assert(result.cached && !event_count && result.physical == 0x12345210);
}

static void permissions_and_updates(void)
{
    for (unsigned protection = 0; protection < 8; ++protection)
        for (unsigned supervisor_page = 0; supervisor_page < 2; ++supervisor_page)
            for (unsigned supervisor = 0; supervisor < 2; ++supervisor)
                for (unsigned used = 0; used < 2; ++used)
                    for (unsigned modified = 0; modified < 2; ++modified)
                        for (unsigned writing = 0; writing < 2; ++writing) {
                            unsigned page_bits = (protection & 4) | supervisor_page * 128 | used * 8 | modified * 16;
                            example(0, (protection & 1) * 4, (protection & 2) * 2, page_bits);
                            Syn68kMMU040Result result = translate(0x76543210, supervisor ? 5 : 1, writing);
                            enum Syn68kMMU040Fault fault = supervisor_page && !supervisor ? MMU040_SUPERVISOR_ONLY
                                                           : protection && writing        ? MMU040_WRITE_PROTECTED
                                                                                          : MMU040_OK;
                            assert(result.fault == fault && (result.status & 1));
                            assert(!!(result.status & 4) == !!protection);
                            unsigned expected_modified = modified || (writing && fault == MMU040_OK);
                            assert(!!(get(0x300c) & 16) == expected_modified);
                            assert(get(0x300c) & 8);
                            event_count = 0;
                            result = translate(0x76543210, supervisor ? 5 : 1, writing);
                            assert(result.cached && !event_count && result.fault == fault);
                        }

    // Both resident PDT encodings, one indirect level only, invalid UDT/PDT.
    for (unsigned indirect = 0; indirect < 2; ++indirect)
        for (unsigned type = 0; type < 4; ++type) {
            example(0, 8, 8, 0);
            put(0x300c, indirect || type == 2 ? 0x6002 : 0xabcde000 | type);
            put(0x6000, 0xabcde000 | type);
            Syn68kMMU040Result result = translate(0x76543210, 1, 0);
            int resident = !!(type & 1);
            assert((result.fault == MMU040_OK) == resident);
            if (resident)
                assert(result.physical == 0xabcde210);
            else {
                assert(result.fault == MMU040_INVALID && !result.status);
                if (indirect)
                    assert(get(0x6000) == (0xabcde000 | type));
            }
        }
    for (unsigned level = 0; level < 2; ++level)
        for (unsigned type = 0; type < 4; ++type) {
            example(0, 0, 0, 0);
            unsigned address = level ? 0x2054 : 0x10ec;
            put(address, (level ? 0x3000 : 0x2000) | type);
            Syn68kMMU040Result result = translate(0x76543210, 1, 0);
            assert((result.fault == MMU040_OK) == !!(type & 2));
            if (!(type & 2))
                assert(get(address) == ((level ? 0x3000u : 0x2000u) | type));
        }
}

static void bus_faults(void)
{
    // Every descriptor read and U/M update, including pipelined upper U writes.
    for (unsigned writing = 0; writing < 2; ++writing)
        for (unsigned failure = 1; failure <= 6; ++failure) {
            example(0, 0, 0, 0);
            fail_event = failure;
            Syn68kMMU040Result result = translate(0x76543210, 1, writing);
            assert(result.fault == MMU040_TABLE_BUS_ERROR && result.status == 0x800);
            assert(result.descriptor_address == events[failure - 1].address);
            // A failed successor read still executes the pending U write.
            if (failure == 2)
                assert(get(0x10ec) == 0x200a && event_count == 3);
            if (failure == 4)
                assert(get(0x2054) == 0x300a && event_count == 5);
            if (failure == 6)
                assert(get(0x300c) == 0xabcde001);
            fail_event = event_count = 0;
            result = translate(0x76543210, 1, writing);
            assert(result.cached && !event_count && result.status == 0x800);
            syn68k_mmu040_flush(&mmu, 0, 0x76543210, 0, 1);
            result = translate(0x76543210, 1, writing);
            assert(result.fault == MMU040_OK && event_count);
        }

    example(0, 8, 8, 0);
    put(0x300c, 0xfffffffau);
    Syn68kMMU040Result result = translate(0x76543210, 1, 0);
    assert(result.fault == MMU040_TABLE_BUS_ERROR && result.status == 0x800);
    assert(result.descriptor_address == 0xfffffff8u);
}

static void missing_bus_callbacks(void)
{
    fixture(0);
    mmu.tc = 0;
    Syn68kMMU040Result result = syn68k_mmu040_translate(&mmu, NULL, 0x12345678, 1, 0);
    ++cases;
    assert(result.fault == MMU040_OK && result.physical == 0x12345678);
    example(0, 8, 8, 8);
    result = syn68k_mmu040_translate(&mmu, NULL, 0x76543210, 1, 0);
    ++cases;
    assert(result.fault == MMU040_TABLE_BUS_ERROR && result.status == 0x800);
    assert(result.descriptor_address == 0x10ec);
    example(0, 8, 8, 8);
    const Syn68kMMU040Bus readonly = {ram, read32, NULL};
    result = syn68k_mmu040_translate(&mmu, &readonly, 0x76543210, 1, 0);
    ++cases;
    assert(result.fault == MMU040_OK);
    result = syn68k_mmu040_translate(&mmu, &readonly, 0x76543210, 1, 1);
    ++cases;
    assert(result.fault == MMU040_TABLE_BUS_ERROR && result.status == 0x800);
    assert(result.descriptor_address == 0x300c && get(0x300c) == 0xabcde009);
}

static void transparent_and_bypass(void)
{
    for (unsigned enabled = 0; enabled < 2; ++enabled)
        for (unsigned field = 0; field < 4; ++field)
            for (unsigned supervisor = 0; supervisor < 2; ++supervisor)
                for (unsigned instruction = 0; instruction < 2; ++instruction)
                    for (unsigned writing = 0; writing < 2; ++writing)
                        for (unsigned octet = 0; octet < 256; ++octet) {
                            fixture(0);
                            mmu.tc = enabled ? 0x8000 : 0;
                            // Masked 16-Mbyte units, overlapping TT0/TT1 with TT0 priority.
                            mmu.dtt[0] = 0x56128000 | field * 0x2000 | 0x364;
                            mmu.itt[1] = 0x56128000 | field * 0x2000;
                            mmu.dtt[1] = 0x00ff8000 | 0x4000;
                            uint32_t logical = (uint32_t)octet << 24 | 0x123456;
                            int hit = (field >= 2 || field == supervisor) && ((octet & 0xed) == (0x56 & 0xed));
                            Syn68kMMU040Result result = translate(logical, 1 + instruction + supervisor * 4, writing);
                            if (hit || !instruction) {
                                assert(result.transparent && result.status == 3 && !event_count);
                                assert(result.physical == logical);
                                assert(result.fault ==
                                       (hit && !instruction && writing ? MMU040_WRITE_PROTECTED : MMU040_OK));
                                assert(result.attributes == (hit && !instruction ? 0x364 : 0));
                            }
                            else if (!enabled)
                                assert(result.fault == MMU040_OK && result.physical == logical && !event_count);
                            else
                                assert(result.fault == MMU040_INVALID && event_count == 1);
                        }
    fixture(0);
    mmu.dtt[0] = mmu.itt[0] = 0x00ffc004;
    for (unsigned fc = 0; fc < 8; ++fc)
        if (fc != 1 && fc != 2 && fc != 5 && fc != 6) {
            Syn68kMMU040Result result = translate(0xdeadbeef, fc, 1);
            assert(result.fault == MMU040_OK && result.physical == 0xdeadbeef);
            assert(!result.transparent && !event_count);
        }
    assert(translate(0, 8, 0).fault == MMU040_BAD_REQUEST);
}

static void boundary_and_changed_protection(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k) {
        example(page8k, 8, 8, 0);
        put(page8k ? 0x3004 : 0x300c, 0xfffff001u);
        Syn68kMMU040Result result = translate(0x76543fff, 1, 0);
        assert(result.fault == MMU040_OK && result.physical == 0xffffffffu);
        event_count = 0;
        result = translate(0x76542fff, 1, 0);
        if (page8k)
            assert(result.cached && !event_count && result.physical == 0xffffefffu);
        else
            assert(result.fault == MMU040_INVALID && event_count);
    }
    for (unsigned protected_page = 0; protected_page < 2; ++protected_page) {
        example(0, 8, 8, 8);
        assert(translate(0x76543210, 1, 0).fault == MMU040_OK);
        if (protected_page)
            put(0x300c, 0xabcde089);
        else
            put(0x10ec, 0x200e);
        Syn68kMMU040Result result = translate(0x76543210, 1, 1);
        assert(!result.cached && result.fault == (protected_page ? MMU040_SUPERVISOR_ONLY : MMU040_WRITE_PROTECTED));
        assert(!(get(0x300c) & 16));
    }
    fixture(0);
    mmu.tc = 0;
    mmu.dtt[0] = 0x00ffc000;
    mmu.dtt[1] = 0x00ffc004;
    assert(translate(0x12345678, 1, 1).fault == MMU040_OK);
    for (unsigned combination = 0; combination < 32; ++combination) {
        unsigned attributes = (combination & 3) * 256 | ((combination >> 2) & 3) * 32 | ((combination >> 4) & 1) * 4;
        mmu.dtt[0] = 0x00ffc000 | attributes;
        Syn68kMMU040Result result = translate(0x12345678, 1, 1);
        assert(result.attributes == attributes && result.status == 3);
        assert(result.fault == (attributes & 4 ? MMU040_WRITE_PROTECTED : MMU040_OK));
        assert(!event_count);
    }
}

static void cache_and_control(void)
{
    example(0, 8, 8, 8 | 16 | 1024);
    assert(translate(0x76543210, 1, 0).physical == 0xabcde210);
    assert(translate(0x76543210, 2, 0).physical == 0xabcde210);
    assert(translate(0x76543210, 5, 0).physical == 0xabcde210);
    put(0x300c, 0x12345019);
    syn68k_mmu040_flush(&mmu, 1, 0, 0, 0);
    event_count = 0;
    assert(translate(0x76543210, 1, 0).physical == 0xabcde210 && !event_count);
    syn68k_mmu040_flush(&mmu, 0, 0x76543fff, 0, 1);
    assert(translate(0x76543210, 1, 0).physical == 0x12345210);
    assert(translate(0x76543210, 2, 0).physical == 0x12345210);
    event_count = 0;
    assert(translate(0x76543210, 5, 0).physical == 0xabcde210 && !event_count);

    // PTEST refreshes only the selected bank/privilege, including global entries.
    put(0x300c, 0x98765019);
    Syn68kMMU040Result result = syn68k_mmu040_ptest(&mmu, &bus, 0x76543210, 1, 0);
    ++cases;
    assert(!result.cached && mmu.mmusr == 0x98765011);
    event_count = 0;
    assert(translate(0x76543210, 2, 0).physical == 0x12345210 && !event_count);
    assert(translate(0x76543210, 5, 0).physical == 0xabcde210 && !event_count);
    assert(mmu.mmusr == 0x98765011);

    // Every writable register mask; register writes and reset never flush ATCs.
    const uint32_t masks[] = {0xc000,     0xfffffe00, 0xfffffe00, 0xffffe364,
                              0xffffe364, 0xffffe364, 0xffffe364, 0xfffffff7};
    Syn68kMMU040Entry entries[2][16][4];
    memcpy(entries, mmu.atc, sizeof entries);
    for (unsigned reg = 0; reg < 8; ++reg) {
        uint32_t value = 0xffffffff;
        assert(syn68k_mmu040_control(&mmu, reg, 1, &value));
        assert(syn68k_mmu040_control(&mmu, reg, 0, &value) && value == masks[reg]);
        assert(!memcmp(entries, mmu.atc, sizeof entries));
        ++cases;
    }
    syn68k_mmu040_reset(&mmu);
    assert(mmu.tc == 0x4000 && mmu.dtt[0] == 0xffff6364 && mmu.itt[1] == 0xffff6364);
    assert(!memcmp(entries, mmu.atc, sizeof entries));
    uint32_t value = 0xfeedface;
    assert(!syn68k_mmu040_control(&mmu, 8, 1, &value) && value == 0xfeedface);
    assert(!syn68k_mmu040_control(&mmu, MMU040_TC, 1, NULL));
}

static void indexes_and_offsets(void)
{
    uint32_t random = 0x324f618b;
    for (unsigned page8k = 0; page8k < 2; ++page8k) {
        unsigned bytes = page8k ? 8192 : 4096;
        example(page8k, 8, 8, 8 | 16);
        for (unsigned offset = 0; offset < bytes; ++offset) {
            uint32_t logical = (page8k ? 0x76542000u : 0x76543000u) + offset;
            Syn68kMMU040Result result = translate(logical, 1, 0);
            assert(result.fault == MMU040_OK && result.physical == 0xabcde000u + offset);
        }
        for (unsigned i = 0; i < 4096; ++i) {
            random = random * 1664525u + 1013904223u;
            uint32_t logical = i == 0 ? 0 : i == 1 ? 0xffffffffu : random;
            unsigned root_index = logical / 33554432u;
            unsigned pointer_index = (logical / 262144u) % 128;
            unsigned page_index = (logical / bytes) % (262144 / bytes);
            unsigned supervisor = i & 1, instruction = (i >> 1) & 1;
            fixture(page8k);
            put((supervisor ? 0x4000 : 0x1000) + root_index * 4, 0x2002 | (i & 1));
            put(0x2000 + pointer_index * 4, 0x3002 | ((i >> 1) & 1));
            // The 8K descriptor's bit 12 is user-reserved, not an address bit.
            uint32_t descriptor = 0x2468a001u | (page8k ? 0x1000 : 0) | (i & 2);
            put(0x3000 + page_index * 4, descriptor);
            Syn68kMMU040Result result = translate(logical, 1 + instruction + supervisor * 4, 0);
            assert(result.fault == MMU040_OK);
            assert(result.physical == 0x2468a000u + logical % bytes);
            assert(events[0].address == (supervisor ? 0x4000u : 0x1000u) + root_index * 4);
            assert(events[1].address == 0x2000 + pointer_index * 4);
            assert(events[3].address == 0x3000 + page_index * 4);
        }
    }
}

static void transparent_masks(void)
{
    fixture(0);
    mmu.tc = 0;
    for (unsigned base = 0; base < 256; base += 85)
        for (unsigned mask = 0; mask < 256; ++mask) {
            mmu.dtt[0] = (uint32_t)base << 24 | mask << 16 | 0xc000;
            mmu.dtt[1] = 0;
            for (unsigned address = 0; address < 256; ++address) {
                int hit = 1;
                for (unsigned bit = 0; bit < 8; ++bit)
                    if (!(mask & (1u << bit)) && ((base ^ address) & (1u << bit)))
                        hit = 0;
                Syn68kMMU040Result result = translate((uint32_t)address << 24, 1, 0);
                assert(!!result.transparent == hit && result.fault == MMU040_OK);
                assert(!event_count);
            }
        }
}

static void capacity_and_replacement(void)
{
    fixture(0);
    put(0x1000, 0x200a);
    put(0x2000, 0x300a);
    for (unsigned page = 0; page < 64; ++page)
        put(0x3000 + page * 4, 0x500019 + page * 4096 + (page & 1) * 1024);
    for (unsigned bank = 0; bank < 2; ++bank)
        for (unsigned page = 0; page < 64; ++page) {
            event_count = 0;
            assert(translate(page * 4096, 1 + bank, 1).physical == 0x500000 + page * 4096);
        }
    event_count = 0;
    for (unsigned bank = 0; bank < 2; ++bank)
        for (unsigned page = 0; page < 64; ++page) {
            assert(translate(page * 4096, 1 + bank, 1).cached);
            assert(!event_count);
            put(0x3000 + page * 4, 0x700019 + page * 4096 + (page & 1) * 1024);
        }
    syn68k_mmu040_flush(&mmu, 1, 0, 0, 0);
    for (unsigned bank = 0; bank < 2; ++bank)
        for (unsigned page = 0; page < 64; ++page) {
            event_count = 0;
            Syn68kMMU040Result result = translate(page * 4096, 1 + bank, 0);
            assert(result.physical == (page & 1 ? 0x500000u : 0x700000u) + page * 4096);
            assert(!!result.cached == !!(page & 1));
            assert(!!event_count == !(page & 1));
        }
    syn68k_mmu040_flush(&mmu, 1, 0, 0, 1);
    for (unsigned bank = 0; bank < 2; ++bank)
        for (unsigned set = 0; set < 16; ++set)
            for (unsigned way = 0; way < 4; ++way)
                assert(!mmu.atc[bank][set][way].valid);

    // Four colliding pages fill one set. A hit advances the replacement counter.
    fixture(0);
    put(0x1000, 0x200a);
    put(0x2000, 0x300a);
    put(0x2004, 0x400a);
    for (unsigned i = 0; i < 4; ++i) {
        put(0x3000 + i * 64, 0x500019 + i * 4096);
        assert(translate(i * 65536, 1, 0).physical == 0x500000 + i * 4096);
    }
    assert(translate(0, 1, 0).cached);
    put(0x4000, 0x600019);
    assert(translate(0x40000, 1, 0).physical == 0x600000);
    event_count = 0;
    assert(translate(0, 1, 0).cached && !event_count);
    assert(!translate(0x10000, 1, 0).cached && event_count);
}

static void negative_cache_and_control_changes(void)
{
    example(0, 8, 8, 0);
    put(0x300c, 0);
    Syn68kMMU040Result result = syn68k_mmu040_ptest(&mmu, &bus, 0x76543210, 1, 0);
    ++cases;
    assert(result.fault == MMU040_INVALID && mmu.mmusr == 0);
    put(0x300c, 0xabcde019);
    event_count = 0;
    result = translate(0x76543210, 1, 1);
    assert(result.cached && result.fault == MMU040_INVALID && !event_count);
    result = syn68k_mmu040_ptest(&mmu, &bus, 0x76543210, 1, 1);
    ++cases;
    assert(result.fault == MMU040_OK && mmu.mmusr == 0xabcde011);

    // Register writes and a warm reset leave a cached translation intact.
    uint32_t value = 0x6000;
    assert(syn68k_mmu040_control(&mmu, MMU040_URP, 1, &value));
    event_count = 0;
    assert(translate(0x76543210, 1, 0).cached && !event_count);
    syn68k_mmu040_reset(&mmu);
    assert(translate(0x76543210, 1, 0).physical == 0x76543210);
    value = 0x8000;
    assert(syn68k_mmu040_control(&mmu, MMU040_TC, 1, &value));
    result = translate(0x76543210, 1, 0);
    assert(result.cached && result.physical == 0xabcde210);
    // TTRs take priority even over an existing writable translation.
    value = 0x7600c004;
    assert(syn68k_mmu040_control(&mmu, MMU040_DTT0, 1, &value));
    result = syn68k_mmu040_ptest(&mmu, &bus, 0x76543210, 1, 1);
    ++cases;
    assert(result.transparent && result.fault == MMU040_WRITE_PROTECTED);
    assert(result.attributes == 4 && mmu.mmusr == 3);
    value = 0;
    assert(syn68k_mmu040_control(&mmu, MMU040_DTT0, 1, &value));
    result = translate(0x76543210, 1, 0);
    assert(result.fault == MMU040_INVALID); // PTEST flushed the old ATC entry.
    assert(mmu.mmusr == 3);                 // Ordinary accesses never overwrite MMUSR.
    result = syn68k_mmu040_ptest(&mmu, &bus, 0x76543210, 8, 0);
    ++cases;
    assert(result.fault == MMU040_BAD_REQUEST && mmu.mmusr == 3);
}

int main(void)
{
    basic_and_history();
    permissions_and_updates();
    bus_faults();
    missing_bus_callbacks();
    transparent_and_bypass();
    boundary_and_changed_protection();
    cache_and_control();
    indexes_and_offsets();
    transparent_masks();
    capacity_and_replacement();
    negative_cache_and_control_changes();
    printf("%u 68040 MMU translation/cache/control cases passed\n", cases);
    return 0;
}
