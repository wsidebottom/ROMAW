/* SPDX-License-Identifier: MIT */
/* Each MOVE16 half latches one translation and commits only complete reads. */
#include "native040.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#if defined(MMU040_MOVE16_NATIVE) && !defined(MMU040_MOVE16_GROUPED)
Syn68kMMU040Result __wrap_syn68k_mmu040_translate(Syn68kMMU040* m, const Syn68kMMU040Bus* b, uint32_t a, unsigned fc,
                                                  int w)
{
    (void)m;
    (void)b;
    (void)a;
    (void)fc;
    (void)w;
    assert(!"original C translator used by native MOVE16 fixture");
    return (Syn68kMMU040Result){.fault = MMU040_BAD_REQUEST};
}
#endif
static Syn68kMMU040 mmu;
static Syn68kCache040 cache;
static Syn68kMMU040Cache binding;
static Syn68kMMU040Memory memory;
static Syn68kMMU040Native native;
static unsigned char ram[0x10000], data[16], saved[16];
static unsigned count, fail_at, change, attempted, cases, table_mutate;
static uint32_t observed[4], attrs_seen[4];

static uint32_t get(unsigned a)
{
    return (uint32_t)ram[a] << 24 | (uint32_t)ram[a + 1] << 16 | (uint32_t)ram[a + 2] << 8 | ram[a + 3];
}

static void put(unsigned a, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i)
        ram[a + i] = v >> (24 - i * 8);
}

static int table_read(void* opaque, uint32_t a, uint32_t* value)
{
    assert(opaque == ram);
    *value = get(a);
    if (table_mutate && a == 0x3000) {
        table_mutate = 0;
        memset(data, 0x33, 16);
    }
    return 1;
}

static int table_write(void* opaque, uint32_t a, uint32_t value, int locked)
{
    assert(opaque == ram);
    put(a, locked ? get(a) | value : value);
    return 1;
}

static int transfer(uint32_t a, unsigned char* d, unsigned n, uint32_t attrs, unsigned fc, int writing)
{
    assert(a < sizeof ram - 3 && n == 4 && (fc == 1 || fc == 5) && count < 4);
    unsigned beat = count;
    observed[count] = a;
    attrs_seen[count++] = attrs;
    if (beat == fail_at)
        return 0;
    if (writing)
        memcpy(ram + a, d, 4);
    else
        memcpy(d, ram + a, 4);
    if (change) {
        change = 0;
        put(0x3000, 0xb059);
        syn68k_mmu040_flush(&mmu, 1, 0, 1, 1);
        memset(data, 0x77, 16);
    }
    return 1;
}

static int read_operand(void* opaque, uint32_t a, unsigned char* d, unsigned n, uint32_t attrs, unsigned fc)
{
    assert(opaque == ram);
    return transfer(a, d, n, attrs, fc, 0);
}

static int write_operand(void* opaque, uint32_t a, const unsigned char* d, unsigned n, uint32_t attrs, unsigned fc)
{
    assert(opaque == ram);
    return transfer(a, (unsigned char*)d, n, attrs, fc, 1);
}

static int read_cycle(void* opaque, uint32_t a, unsigned char* d, unsigned n, const Syn68kCache040Cycle* c,
                      unsigned* inhibit)
{
    assert(c->kind == CACHE040_OPERAND && c->traffic == CACHE040_MOVE16 && c->burst == 16 && !c->locked &&
           !c->lock_end);
    *inhibit = 1;
    return read_operand(opaque, a, d, n, c->attributes, c->fc);
}

static int write_cycle(void* opaque, uint32_t a, const unsigned char* d, unsigned n, const Syn68kCache040Cycle* c)
{
    assert(c->kind == CACHE040_OPERAND && c->traffic == CACHE040_MOVE16 && c->burst == 16 && !c->locked &&
           !c->lock_end);
    return write_operand(opaque, a, d, n, c->attributes, c->fc);
}

static Syn68kCache040Bus physical;

static void setup(unsigned cached, unsigned ram_mode)
{
    memset(ram, 0, sizeof ram);
    syn68k_mmu040_init(&mmu);
    syn68k_cache040_init(&cache);
    mmu.tc = 0x8000;
    mmu.srp = mmu.urp = 0x1000;
    mmu.mmusr = 0x12345678;
    put(0x1008, 0x200a);
    put(0x2000, 0x300a);
    put(0x3000, 0x9039);
    static const uint32_t cacr = 0x80000000;
    physical = (Syn68kCache040Bus){.opaque = ram, .read_cycle = read_cycle, .write_cycle = write_cycle};
    binding = (Syn68kMMU040Cache){.state = &cache, .bus = &physical, .cacr = &cacr};
    memory = (Syn68kMMU040Memory){.mmu = &mmu,
                                  .tables = {ram, table_read, table_write},
                                  .opaque = ram,
                                  .read = read_operand,
                                  .write = write_operand,
                                  .cache = cached ? &binding : NULL};
    native = (Syn68kMMU040Native){.write_attempted = &attempted};
    memory.native = &native;
#ifdef MMU040_MOVE16_NATIVE
    Syn68kNative040RAM window = ram_mode ? (Syn68kNative040RAM){ram + 0x9040, 0x9040, 8} : (Syn68kNative040RAM){0};
    assert(syn68k_native040_memory(&memory, &native, window));
    native.write_attempted = &attempted;
#else
    (void)ram_mode;
#endif
    count = attempted = change = table_mutate = 0;
    fail_at = 4;
    for (unsigned i = 0; i < 16; ++i) {
        ram[0x9040 + i] = 0x80 + i;
        ram[0xb040 + i] = 0xd0 + i;
        data[i] = 0xa0 + i;
    }
    memcpy(saved, data, 16);
}

static void faults(void)
{
    for (unsigned cached = 0; cached < 2; ++cached)
        for (unsigned writing = 0; writing < 2; ++writing)
            for (unsigned offset = 0; offset < 16; ++offset)
                for (unsigned user = 0; user < 2; ++user)
                    for (unsigned failure = 0; failure < 5; ++failure) {
                        setup(cached, 0);
                        fail_at = failure;
                        unsigned fc = user ? 1 : 5;
                        Syn68kMMU040Access r = syn68k_mmu040_move16(&memory, 0x04000040 + offset, fc, writing, data);
                        assert(r.translation.fault == MMU040_OK && r.bus_error == (failure < 4));
                        assert(count == (failure < 4 ? failure + 1 : 4) && attempted == writing &&
                               mmu.mmusr == 0x12345678);
                        for (unsigned i = 0; i < count; ++i) {
                            unsigned word = ((offset >> 2) + i) & 3;
                            assert(observed[i] == 0x9040 + word * 4 && attrs_seen[i] == 0x30);
                        }
                        if (failure < 4) {
                            unsigned word = ((offset >> 2) + failure) & 3;
                            assert(r.size == 4 && !r.completed && r.request == 0x04000040 + word * 4 &&
                                   r.logical == r.request);
                            assert(r.translation.physical == 0x9040 + word * 4 && r.cache.completed == failure * 4);
                            assert(r.cache.fault == CACHE040_OPERAND_FAULT && r.cache_exception && !r.cache_table &&
                                   r.cache.cycle.traffic == CACHE040_MOVE16 && r.cache.cycle.beat == failure);
                            if (!writing)
                                assert(!memcmp(data, saved, 16));
                            if (writing) {
                                uint32_t expected = (uint32_t)saved[word * 4] << 24 |
                                                    (uint32_t)saved[word * 4 + 1] << 16 |
                                                    (uint32_t)saved[word * 4 + 2] << 8 | saved[word * 4 + 3];
                                assert(r.value == expected && !memcmp(r.cache.push_data, saved, 16));
                            }
                        }
                        else {
                            assert(r.size == 16 && r.completed == 16 && r.cache.completed == 16);
                            if (!writing)
                                assert(!memcmp(data, ram + 0x9040, 16));
                        }
                        for (unsigned way = 0; way < 4; ++way)
                            assert(!cache.lines[0][4][way].valid);
                        ++cases;
                    }
}

static void latched(void)
{
    for (unsigned cached = 0; cached < 2; ++cached)
        for (unsigned writing = 0; writing < 2; ++writing)
            for (unsigned offset = 0; offset < 16; ++offset)
                for (unsigned ram_mode = 0; ram_mode < 2; ++ram_mode) {
                    setup(cached, ram_mode);
                    change = 1;
                    unsigned char expected[16];
                    memcpy(expected, writing ? saved : ram + 0x9040, 16);
                    Syn68kMMU040Access r = syn68k_mmu040_move16(&memory, 0x04000040 + offset, 5, writing, data);
                    assert(!r.translation.fault && !r.bus_error && r.completed == 16 && !change);
                    for (unsigned i = 0; i < count; ++i)
                        assert((observed[i] & ~15u) == 0x9040 && attrs_seen[i] == 0x30);
                    assert(!memcmp(writing ? ram + 0x9040 : data, expected, 16));
                    if (writing)
                        for (unsigned byte = 0; byte < 16; ++byte)
                            assert(data[byte] == 0x77);
                    count = 0;
                    r = syn68k_mmu040_move16(&memory, 0x04000040 + offset, 5, 0, data);
                    assert(!r.translation.fault && !r.bus_error && count == 4 && !memcmp(data, ram + 0xb040, 16));
                    for (unsigned i = 0; i < count; ++i)
                        assert((observed[i] & ~15u) == 0xb040 && attrs_seen[i] == 0x50);
                    ++cases;
                }
}

static void hits_and_permissions(void)
{
    for (unsigned offset = 0; offset < 16; ++offset) {
        setup(1, 1);
        Syn68kCache040Line* line = &cache.lines[0][4][offset & 3];
        *line = (Syn68kCache040Line){.address = 0x9040, .valid = 1, .dirty = 15};
        memset(line->bytes, 0x55, 16);
        Syn68kMMU040Access r = syn68k_mmu040_move16(&memory, 0x04000040 + offset, 5, 0, data);
        assert(!r.bus_error && !count && r.cache.completed == 16 && !r.cache.cycle.burst);
        for (unsigned byte = 0; byte < 16; ++byte)
            assert(data[byte] == 0x55);
        assert(line->valid && line->dirty == 15);
        count = 0;
        r = syn68k_mmu040_move16(&memory, 0x04000040 + offset, 5, 1, data);
        assert(!r.bus_error && !line->valid && count <= 4);
        for (unsigned byte = 0; byte < 16; ++byte)
            assert(ram[0x9040 + byte] == 0x55);
        ++cases;
    }
    for (unsigned cached = 0; cached < 2; ++cached)
        for (unsigned writing = 0; writing < 2; ++writing) {
            setup(cached, 0);
            put(0x3000, 0x9021);
            Syn68kMMU040Access r = syn68k_mmu040_move16(&memory, 0x04000040, 5, writing, data);
            assert(!r.translation.fault && !r.bus_error && get(0x3000) == (writing ? 0x9039u : 0x9029u));
            ++cases;
        }
    setup(0, 0);
    put(0x3000, 0x903d);
    Syn68kMMU040Access r = syn68k_mmu040_move16(&memory, 0x0400004f, 5, 1, data);
    assert(r.translation.fault == MMU040_WRITE_PROTECTED && !r.bus_error && !count && attempted &&
           r.request == 0x0400004c && r.value == 0xacadaeaf);
    ++cases;
}

static void invalid_requests(void)
{
    for (unsigned bad = 0; bad < 12; ++bad) {
        setup(1, 0);
        Syn68kMMU040Memory* m = &memory;
        unsigned fc = 5;
        int writing = 1;
        unsigned char* d = data;
        if (bad == 0)
            m = NULL;
        if (bad == 1)
            memory.mmu = NULL;
        if (bad == 2)
            fc = 2;
        if (bad == 3)
            fc = 6;
        if (bad == 4)
            writing = 2;
        if (bad == 5)
            d = NULL;
        if (bad == 6)
            cache.active = 1;
        if (bad == 7)
            binding.traffic = CACHE040_EXCEPTION;
        if (bad == 8)
            memory.aligned_transfers = 2;
        if (bad == 9)
            binding.state = NULL;
        if (bad == 10)
            binding.cacr = NULL;
        if (bad == 11) {
            memory.cache = NULL;
            memory.write = NULL;
        }
        Syn68kMMU040 saved_mmu = mmu;
        Syn68kCache040 saved_cache = cache;
        Syn68kMMU040Access r = syn68k_mmu040_move16(m, 0x04000040, fc, writing, d);
        assert(r.translation.fault == MMU040_BAD_REQUEST && !count && !attempted && !memcmp(data, saved, 16));
        assert(!memcmp(&mmu, &saved_mmu, sizeof mmu) && !memcmp(&cache, &saved_cache, sizeof cache));
        ++cases;
    }
    setup(0, 0);
    table_mutate = 1;
    Syn68kMMU040Access r = syn68k_mmu040_move16(&memory, 0x0400004c, 5, 1, data);
    assert(!r.translation.fault && !r.bus_error && !table_mutate && !memcmp(ram + 0x9040, saved, 16));
    for (unsigned i = 0; i < 16; ++i)
        assert(data[i] == 0x33);
    ++cases;
}

int main(void)
{
    faults();
    latched();
    hits_and_permissions();
    invalid_requests();
    printf("%u MOVE16 MMU/line/latch/RAM/history/permission cases passed\n", cases);
}
