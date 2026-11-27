/* SPDX-License-Identifier: MIT */
/* Actual nonallocating line transfers, cache hits and PD capture. */
#include "cache040.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef CACHE040_NATIVE_TEST
#include "native040.h"

static Syn68kCache040Access invoke(Syn68kCache040* c, const Syn68kCache040Bus* b, uint32_t a, unsigned fc, int w,
                                   unsigned char bytes[16], uint32_t attrs, uint32_t cacr)
{
    Syn68kNative040Cache p = {.cache = c,
                              .bus = b,
                              .address = a,
                              .fc = fc,
                              .writing = w,
                              .attributes = attrs,
                              .cacr = cacr,
                              .size = 16,
                              .traffic = CACHE040_MOVE16};
    if (!bytes)
        return (Syn68kCache040Access){.fault = CACHE040_BAD_REQUEST};
    memcpy(p.line, bytes, 16);
    Syn68kNative040Entry entry = syn68k_native040_entry(NATIVE040_CACHE_ACCESS);
    assert(entry);
    entry(&p);
    if (!w && p.access.fault == CACHE040_OK)
        memcpy(bytes, p.line, 16);
    return p.access;
}

Syn68kCache040Access __wrap_syn68k_cache040_move16(Syn68kCache040* c, const Syn68kCache040Bus* b, uint32_t a,
                                                   unsigned fc, int w, unsigned char bytes[16], uint32_t attrs,
                                                   uint32_t cacr)
{
    (void)c;
    (void)b;
    (void)a;
    (void)fc;
    (void)w;
    (void)bytes;
    (void)attrs;
    (void)cacr;
    assert(!"original C MOVE16 cache helper used by native fixture");
    return (Syn68kCache040Access){.fault = CACHE040_BAD_REQUEST};
}

#define syn68k_cache040_move16 invoke
#endif

static Syn68kCache040 cache;
static unsigned char ram[16], data[16], original[16], old[16];
static unsigned calls, operand, push, fail_at, fail_push, mutate_input, recurse, cases;
static Syn68kCache040Cycle cycles[12];
static unsigned addresses[12], directions[12];
static Syn68kCache040Bus bus;

static int beat(uint32_t address, unsigned char* bytes, unsigned size, const Syn68kCache040Cycle* cycle, int writing)
{
    assert(size == 4 && address >= 0x1010 && address <= 0x101c && !(address & 3) && calls < 12);
    assert(cache.active && cache.traffic == CACHE040_MOVE16);
    cycles[calls] = *cycle;
    addresses[calls] = address;
    directions[calls++] = writing;
    if (cycle->kind == CACHE040_PUSH) {
        assert(writing && !cycle->fc && !cycle->attributes && !cycle->locked && !cycle->lock_end);
        ++push;
        if (fail_push && cycle->beat == fail_at)
            return 0;
    }
    else {
        assert(cycle->traffic == CACHE040_MOVE16 && cycle->burst == 16 && cycle->beat == operand && !cycle->locked &&
               !cycle->lock_end && (cycle->fc == 1 || cycle->fc == 5));
        ++operand;
        if (!fail_push && cycle->beat == fail_at)
            return 0;
    }
    if (writing)
        memcpy(ram + address - 0x1010, bytes, 4);
    else
        memcpy(bytes, ram + address - 0x1010, 4);
    if (mutate_input && !fail_push && operand == 1)
        for (unsigned byte = 0; byte < 16; ++byte)
            data[byte] ^= 0xff;
    if (recurse) {
        recurse = 0;
        unsigned char untouched[16];
        memset(untouched, 0xc3, 16);
        Syn68kCache040Access r = syn68k_cache040_move16(&cache, &bus, address, 5, 0, untouched, 0x20, 0x80000000);
        assert(r.fault == CACHE040_BAD_REQUEST && cache.active && cache.traffic == CACHE040_MOVE16);
        for (unsigned byte = 0; byte < 16; ++byte)
            assert(untouched[byte] == 0xc3);
    }
    return 1;
}

static int read_cycle(void* opaque, uint32_t a, unsigned char* d, unsigned n, const Syn68kCache040Cycle* c,
                      unsigned* inhibit)
{
    assert(opaque == ram);
    *inhibit = 1; /* Never truncate a MOVE16 or allocate it. */
    return beat(a, d, n, c, 0);
}

static int write_cycle(void* opaque, uint32_t a, const unsigned char* d, unsigned n, const Syn68kCache040Cycle* c)
{
    assert(opaque == ram);
    return beat(a, (unsigned char*)d, n, c, 1);
}

static Syn68kCache040Line* setup(unsigned state, unsigned way)
{
    syn68k_cache040_init(&cache);
    for (unsigned i = 0; i < 16; ++i) {
        ram[i] = 0x80 + i;
        data[i] = 0xa0 + i;
        old[i] = 0x50 + i;
    }
    memcpy(original, data, 16);
    Syn68kCache040Line* line = &cache.lines[0][1][way];
    *line = (Syn68kCache040Line){.address = 0x1010, .valid = !!state, .dirty = state == 2 ? 1 : state == 3 ? 15 : 0};
    memcpy(line->bytes, old, 16);
    cache.lines[1][1][0] = *line; /* Data transactions never invalidate I-cache. */
    calls = operand = push = 0;
    fail_push = mutate_input = recurse = 0;
    bus = (Syn68kCache040Bus){.opaque = ram, .read_cycle = read_cycle, .write_cycle = write_cycle};
    return line;
}

static void matrix(void)
{
    for (unsigned writing = 0; writing < 2; ++writing)
        for (unsigned mode = 0; mode < 4; ++mode)
            for (unsigned enabled = 0; enabled < 2; ++enabled)
                for (unsigned state = 0; state < 4; ++state)
                    for (unsigned offset = 0; offset < 16; ++offset)
                        for (unsigned user = 0; user < 2; ++user)
                            for (unsigned failed = 0; failed < 5; ++failed) {
                                unsigned way = offset & 3, hit = enabled && state, cached = !writing && hit && mode < 2;
                                Syn68kCache040Line* line = setup(state, way);
                                cache.bypass_data_miss = 1;
                                fail_at = failed;
                                unsigned fc = user ? 1 : 5;
                                Syn68kCache040Line instruction = cache.lines[1][1][0];
                                Syn68kCache040Access r =
                                    syn68k_cache040_move16(&cache, &bus, 0x1010 + offset, fc, writing, data,
                                                           mode << 5 | 0x100, enabled ? 0x80000000u : 0);
                                assert(!cache.active && cache.traffic == CACHE040_NORMAL);
                                assert(cache.bypass_data_miss == !(enabled && !state));
                                assert(!memcmp(&instruction, &cache.lines[1][1][0], sizeof instruction));
                                unsigned faults = !cached && failed < 4;
                                assert(r.fault == (faults ? CACHE040_OPERAND_FAULT : CACHE040_OK));
                                assert(r.completed == (faults ? failed * 4 : 16));
                                assert(operand == (cached ? 0 : faults ? failed + 1 : 4));
                                assert(push == (!writing && hit && mode >= 2 ? state == 2   ? 1u
                                                                               : state == 3 ? 4u
                                                                                            : 0u
                                                                             : 0u));
                                unsigned replacement = enabled && mode < 2 ? !writing ? 2 : mode == 1 ? 1 : operand : 0;
                                assert(cache.replacement[0] == (replacement & 3));
                                unsigned base = push;
                                for (unsigned i = 0; i < operand; ++i) {
                                    assert(addresses[base + i] == 0x1010 + ((((offset >> 2) + i) & 3) * 4));
                                    assert(cycles[base + i].fc == fc &&
                                           cycles[base + i].attributes == (mode << 5 | 0x100));
                                    assert(directions[base + i] == writing);
                                }
                                if (faults) {
                                    assert(r.bus_address == 0x1010 + ((((offset >> 2) + failed) & 3) * 4));
                                    if (writing) {
                                        assert(r.push_address == ((0x1010 + offset) & ~3u) && r.push_size == 16);
                                        assert(!memcmp(r.push_data, original, 16));
                                    }
                                    else
                                        assert(!memcmp(data, original, 16));
                                }
                                else if (!writing) {
                                    const unsigned char* expected = cached ? old : ram;
                                    assert(!memcmp(data, expected, 16));
                                    assert(r.cycle.burst == (cached ? 0u : 16u));
                                }
                                unsigned valid = state && !(enabled && (writing || mode >= 2));
                                assert(line->valid == valid);
                                assert(!memcmp(line->bytes, old, 16));
                                assert(line->dirty == (!writing && hit && mode >= 2 ? 0
                                                       : state == 2                 ? 1u
                                                       : state == 3                 ? 15u
                                                                                    : 0u));
                                if (writing) {
                                    unsigned done = faults ? failed : 4;
                                    for (unsigned word = 0; word < 4; ++word) {
                                        unsigned order = (word + 4 - (offset >> 2)) & 3;
                                        for (unsigned byte = 0; byte < 4; ++byte)
                                            assert(ram[word * 4 + byte] ==
                                                   (order < done ? original[word * 4 + byte] : 0x80 + word * 4 + byte));
                                    }
                                }
                                ++cases;
                            }
}

static void push_faults(void)
{
    for (unsigned mask = 0; mask < 2; ++mask)
        for (unsigned offset = 0; offset < 16; ++offset)
            for (unsigned failed = 0; failed < (mask ? 4u : 1u); ++failed) {
                Syn68kCache040Line* line = setup(mask ? 3 : 2, offset & 3);
                fail_push = 1;
                fail_at = failed;
                Syn68kCache040Access r =
                    syn68k_cache040_move16(&cache, &bus, 0x1010 + offset, 5, 0, data, 0x40, 0x80000000u);
                assert(r.fault == CACHE040_PUSH_FAULT && !r.completed && !line->valid && !cache.active && !operand);
                assert(r.push_address == 0x1010 && r.push_size == (mask ? 16u : 4u) && !memcmp(r.push_data, old, 16));
                assert(!memcmp(data, original, 16) && push == failed + 1);
                ++cases;
            }
}

static void snapshot_and_reentry(void)
{
    for (unsigned offset = 0; offset < 16; ++offset) {
        setup(0, 0);
        fail_at = 4;
        mutate_input = recurse = 1;
        Syn68kCache040Access r = syn68k_cache040_move16(&cache, &bus, 0x1010 + offset, 5, 1, data, 0x20, 0x80000000u);
        assert(!r.fault && !memcmp(ram, original, 16) && !cache.active);
        for (unsigned byte = 0; byte < 16; ++byte)
            assert(data[byte] == (unsigned char)(original[byte] ^ 0xff));
        ++cases;
    }
}

static void invalid(void)
{
    for (unsigned bad = 0; bad < 8; ++bad) {
        setup(1, 0);
        fail_at = 4;
        Syn68kCache040 saved = cache;
        Syn68kCache040Bus broken = bus;
        unsigned fc = 5;
        int w = 1;
        unsigned char* bytes = data;
        if (bad == 0)
            fc = 2;
        if (bad == 1)
            fc = 6;
        if (bad == 2)
            fc = 8;
        if (bad == 3)
            w = 2;
        if (bad == 4)
            bytes = NULL;
        if (bad == 5)
            broken.read_cycle = NULL;
        if (bad == 6)
            broken.write_cycle = NULL;
        if (bad == 7) {
            cache.active = 1;
            saved = cache;
        }
        Syn68kCache040Access r = syn68k_cache040_move16(&cache, &broken, 0x1010, fc, w, bytes, 0x20, 0x80000000u);
        assert(r.fault == CACHE040_BAD_REQUEST && !calls && !memcmp(&cache, &saved, sizeof cache) &&
               !memcmp(data, original, 16));
        ++cases;
    }
}

static unsigned retry_index, retry_hits;

static int retry_line_read(void* opaque, uint32_t address, unsigned char* bytes, unsigned size,
                           const Syn68kCache040Cycle* cycle, unsigned* inhibit)
{
    if (cycle->beat == retry_index && !retry_hits++)
        return CACHE040_CYCLE_RETRY;
    return read_cycle(opaque, address, bytes, size, cycle, inhibit);
}

static int retry_line_write(void* opaque, uint32_t address, const unsigned char* bytes, unsigned size,
                            const Syn68kCache040Cycle* cycle)
{
    if (cycle->beat == retry_index && !retry_hits++)
        return CACHE040_CYCLE_RETRY;
    return write_cycle(opaque, address, bytes, size, cycle);
}

static void line_cycle_retries(void)
{
    for (unsigned writing = 0; writing < 2; ++writing)
        for (unsigned critical = 0; critical < 4; ++critical)
            for (retry_index = 0; retry_index < 4; ++retry_index) {
                setup(0, 0);
                fail_at = 4;
                retry_hits = 0;
                unsigned char before[16];
                memcpy(before, ram, sizeof before);
                const Syn68kCache040Bus retry_bus = {
                    .opaque = ram, .read_cycle = retry_line_read, .write_cycle = retry_line_write};
                unsigned address = 0x1010 + critical * 4;
                Syn68kCache040Access r = syn68k_cache040_move16(&cache, &retry_bus, address, 5, writing, data, 0x20, 0);
                assert(calls == (retry_index ? retry_index : 4) && operand == calls && !push);
                assert(retry_hits == (retry_index ? 1u : 2u));
                assert(r.completed == (retry_index ? retry_index * 4 : 16));
                if (retry_index) {
                    unsigned failed_word = (critical + retry_index) & 3;
                    assert(r.fault == CACHE040_OPERAND_FAULT && r.cycle.beat == retry_index &&
                           r.bus_address == 0x1010 + failed_word * 4);
                    if (writing)
                        assert(r.push_word == failed_word && !memcmp(r.push_data, original, 16));
                }
                else
                    assert(!r.fault);
                if (writing || retry_index)
                    assert(!memcmp(data, original, 16));
                else
                    assert(!memcmp(data, before, 16));
                for (unsigned beat = 0; beat < 4; ++beat) {
                    unsigned offset = ((critical + beat) & 3) * 4;
                    const unsigned char* expected = writing && beat < calls ? original : before;
                    assert(!memcmp(ram + offset, expected + offset, 4));
                }
                assert(!cache.active);
                ++cases;
            }
    puts("32 MOVE16 first-beat retries and partial read/write aborts passed");
}
int main(void)
{
    line_cycle_retries();
    matrix();
    push_faults();
    snapshot_and_reentry();
    invalid();
    printf("%u MOVE16 physical line/cache/nonallocation/burst/PD/reentry cases passed\n", cases);
    return 0;
}
