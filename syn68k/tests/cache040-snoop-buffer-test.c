/*+===================================================================
  File:      cache040-snoop-buffer-test.c

  Summary:   Verify external snoops see an evicted dirty line between fill and writeback.

  Classes:   None.

  Functions: main

  Origin:    Original ROMAW regression for MC68040UM 4.7.2 and Tables 4-3/4-4.

  Copyright and Legal notices: Applicable license: MIT; see the SPDX notice below.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include "cache040.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef CACHE040_NATIVE_TEST
#include "native040.h"
#endif

static Syn68kCache040 cache, expected_cache;
static unsigned char ram[0x4000], expected_ram[sizeof ram], response[16], expected_response[16];
static Syn68kCache040Line expected_victim;
static unsigned way, mask, writing, quarter, control, master_write, length, offset, profile;
static unsigned grants, reads, pushes, fail_push, fail_beat, fail_read, cases, current_target;
static unsigned expected_hit, expected_invalidated, expected_intervention, expected_pushes;
static uint32_t read_addresses[4], push_addresses[4];
static const unsigned OLD = 0x1000, FRESH = 0x2400;

static Syn68kCache040Access access_call(unsigned address, unsigned n, int w, uint32_t value, int no_allocate,
                                        enum Syn68kCache040Traffic traffic);

static Syn68kCache040Snoop snoop_call(unsigned address, unsigned n, int w, unsigned char* data)
{
#ifdef CACHE040_NATIVE_TEST
    Syn68kNative040Cache p = {
        .cache = &cache, .address = address, .size = n, .writing = w, .snoop_control = control, .transfer_type = 0};
    memcpy(p.line, data, n);
    Syn68kNative040Entry entry = syn68k_native040_entry(NATIVE040_CACHE_SNOOP);
    assert(entry);
    entry(&p);
    if (!w && p.snoop.intervention)
        memcpy(data, p.line, n);
    return p.snoop;
#else
    return syn68k_cache040_snoop(&cache, address, control, 0, n, w, data);
#endif
}

static int read_cycle(void* opaque, uint32_t address, unsigned char* data, unsigned n, const Syn68kCache040Cycle* cycle,
                      unsigned* inhibit)
{
    assert(opaque == ram && address + n <= sizeof ram && reads < 4);
    read_addresses[reads++] = address;
    *inhibit = 0;
    if (fail_read && cycle->burst == 16 && cycle->beat == fail_beat)
        return 0;
    memcpy(data, ram + address, n);
    return 1;
}

static int write_cycle(void* opaque, uint32_t address, const unsigned char* data, unsigned n,
                       const Syn68kCache040Cycle* cycle)
{
    assert(opaque == ram && cycle->kind == CACHE040_PUSH && n == 4 && address >= OLD && address < OLD + 16 &&
           pushes < 4);
    push_addresses[pushes++] = address;
    assert(!cycle->fc && !cycle->attributes && !cycle->locked && !cycle->lock_end && cycle->way == way);
    if (fail_push && cycle->beat == fail_beat)
        return 0;
    memcpy(ram + address, data, n);
    return 1;
}

static Syn68kCache040Bus bus = {.opaque = ram, .read_cycle = read_cycle, .write_cycle = write_cycle};

static Syn68kCache040Access access_call(unsigned address, unsigned n, int w, uint32_t value, int no_allocate,
                                        enum Syn68kCache040Traffic traffic)
{
#ifdef CACHE040_NATIVE_TEST
    Syn68kNative040Cache p = {.cache = &cache,
                              .bus = &bus,
                              .address = address,
                              .fc = 5,
                              .size = n,
                              .writing = w,
                              .value = value,
                              .attributes = 0x20,
                              .cacr = 0x80000000,
                              .no_allocate = no_allocate,
                              .traffic = traffic};
    if (profile == 1)
        p.ram = (Syn68kNative040RAM){ram + FRESH, FRESH, 16};
    if (profile == 2)
        p.ram = (Syn68kNative040RAM){ram, 0, sizeof ram};
    Syn68kNative040Entry entry = syn68k_native040_entry(NATIVE040_CACHE_ACCESS);
    assert(entry);
    entry(&p);
    return p.access;
#else
    return syn68k_cache040_transfer(&cache, &bus, address, 5, n, w, value, 0x20, 0x80000000, no_allocate, traffic);
#endif
}

static void grant(void* opaque, Syn68kCache040* c)
{
    assert(opaque == &grants && c == &cache && !grants && !pushes);
    ++grants;
    assert(cache.active && cache.snoop_window == 1 && cache.victim_live == 1 && cache.victim_way == way);
    assert(cache.victim.valid && cache.victim.address == OLD && cache.victim.dirty == mask);
    assert(reads == (profile ? 0u : 4u));
    assert(cache.lines[0][0][way].address == FRESH && cache.lines[0][0][way].valid);
    assert(cache.lines[0][0][way].dirty == (writing ? 1u << quarter : 0));
    for (unsigned i = 0; i < 4; ++i)
        assert(cache.lines[0][0][way].bytes[quarter * 4 + i] == (writing ? 0xa1u + i * 0x11 : 0x80u + quarter * 4 + i));
    for (unsigned i = 0; i < 16; ++i)
        response[i] = master_write ? 0xc0 + i : 0xa5;
    unsigned target = current_target ? FRESH + quarter * 4 : OLD + offset;
    Syn68kCache040Snoop result = snoop_call(target, length, master_write, response);
    assert(!result.fault && result.hit == expected_hit && result.invalidated == expected_invalidated &&
           result.intervention == expected_intervention);
    assert(result.way[0] == ((expected_hit & 5) ? way : 0));
    assert(result.way[1] == ((expected_hit & 2) ? ((way + 1) & 3) : 0));
    if (!master_write && !result.intervention)
        memcpy(response, ram + (target & ~(length == 16 ? 15u : 0u)), length);
    if (master_write && !result.intervention)
        memcpy(ram + (target & ~(length == 16 ? 15u : 0u)), response, length);
    if (!master_write && result.intervention && control == 2)
        memcpy(ram + (target & ~(length == 16 ? 15u : 0u)), response, length);
    assert(!memcmp(response, expected_response, 16));
    /* A bus grant permits snoops, not recursive processor cache accesses. */
    Syn68kCache040 saved = cache;
    Syn68kCache040Access nested = access_call(FRESH, 4, 0, 0, 0, CACHE040_NORMAL);
    assert(nested.fault == CACHE040_BAD_REQUEST && !memcmp(&cache, &saved, sizeof cache));
}

static void seed(void)
{
    cache = (Syn68kCache040){0};
    memset(ram, 0, sizeof ram);
    for (unsigned w = 0; w < 4; ++w) {
        Syn68kCache040Line* line = &cache.lines[0][0][w];
        *line = (Syn68kCache040Line){
            .address = w == way ? OLD : 0x1400 + w * 0x400, .valid = 1, .dirty = w == way ? mask : 0};
        for (unsigned i = 0; i < 16; ++i)
            line->bytes[i] = w == way ? 0x40 + i : 0x20 + w * 16 + i;
        memcpy(ram + line->address, line->bytes, 16);
    }
    for (unsigned i = 0; i < 16; ++i) {
        if (mask & (1u << (i / 4)))
            ram[OLD + i] = 0x10 + i;
        ram[FRESH + i] = 0x80 + i;
    }
    cache.lines[1][0][(way + 1) & 3] = (Syn68kCache040Line){.address = OLD, .valid = 1};
    memcpy(cache.lines[1][0][(way + 1) & 3].bytes, ram + OLD, 16);
    cache.replacement[0] = way;
    cache.arbitrate = grant;
    cache.arbitrate_opaque = &grants;
    expected_cache = cache;
    memcpy(expected_ram, ram, sizeof ram);
    expected_victim = cache.lines[0][0][way];
    grants = reads = pushes = 0;
    fail_read = 0;
    current_target = 0;
}

static void oracle(void)
{
    Syn68kCache040Line* fresh = &expected_cache.lines[0][0][way];
    *fresh = (Syn68kCache040Line){.address = FRESH, .valid = 1, .dirty = writing ? 1u << quarter : 0};
    memcpy(fresh->bytes, ram + FRESH, 16);
    if (writing)
        for (unsigned i = 0; i < 4; ++i)
            fresh->bytes[quarter * 4 + i] = 0xa1 + i * 0x11;
    expected_hit = expected_invalidated = expected_intervention = 0;
    for (unsigned i = 0; i < 16; ++i)
        expected_response[i] = master_write ? 0xc0 + i : 0xa5;
    unsigned first = current_target ? quarter * 4 : length == 16 ? 0 : offset;
    unsigned base = current_target ? FRESH : OLD;
    if (current_target) {
        assert(control == 1 && master_write && length == 4);
        expected_hit = CACHE040_SNOOP_DATA;
        if (writing) {
            expected_intervention = 1;
            memcpy(fresh->bytes + first, expected_response, 4);
        }
        else {
            expected_invalidated = CACHE040_SNOOP_DATA;
            fresh->valid = 0;
        }
    }
    else if (control == 1 || control == 2) {
        expected_hit = 4;
        if (master_write || control == 2) {
            expected_hit |= 2;
            expected_invalidated |= 2;
            expected_cache.lines[1][0][(way + 1) & 3].valid = 0;
        }
        if (!master_write) {
            expected_intervention = 1;
            memcpy(expected_response, expected_victim.bytes + first, length);
        }
        else if (control == 1 && length != 16) {
            expected_intervention = 1;
            for (unsigned i = 0; i < length; ++i) {
                expected_victim.bytes[first + i] = expected_response[i];
                expected_victim.dirty |= 1u << ((first + i) / 4);
            }
        }
        if (control == 2 || (master_write && length == 16)) {
            expected_invalidated |= 4;
            expected_victim.valid = 0;
        }
    }
    if (!master_write && !expected_intervention)
        memcpy(expected_response, expected_ram + base + first, length);
    if ((master_write && !expected_intervention) || (!master_write && expected_intervention && control == 2))
        memcpy(expected_ram + base + first, expected_response, length);
    unsigned dirty = expected_victim.dirty;
    expected_pushes = !expected_victim.valid ? 0 : (dirty & (dirty - 1)) ? 4 : 1;
    unsigned first_word = 0;
    if (expected_pushes == 1)
        while (!(dirty & (1u << first_word)))
            ++first_word;
    unsigned done = fail_push && expected_pushes ? fail_beat : expected_pushes;
    if (done > expected_pushes)
        done = expected_pushes;
    for (unsigned i = 0; i < done; ++i)
        memcpy(expected_ram + OLD + (first_word + i) * 4, expected_victim.bytes + (first_word + i) * 4, 4);
    expected_cache.replacement[0] = (way + 1) & 3;
}

static void exercise(unsigned target)
{
    seed();
    current_target = target;
    oracle();
    unsigned should_fail = fail_push && expected_pushes && profile != 2;
    if (profile == 2) {
        for (unsigned i = 0; i < expected_pushes; ++i) {
            unsigned first = 0;
            if (expected_pushes == 1)
                while (!(expected_victim.dirty & (1u << first)))
                    ++first;
            memcpy(expected_ram + OLD + (first + i) * 4, expected_victim.bytes + (first + i) * 4, 4);
        }
        should_fail = 0;
    }
    Syn68kCache040Access result = access_call(FRESH + quarter * 4, 4, writing, 0xa1b2c3d4, 0, CACHE040_NORMAL);
    assert(grants == 1 && "victim buffer arbitration must run once");
    assert(result.fault == (should_fail ? CACHE040_PUSH_FAULT : CACHE040_OK));
    assert(result.completed == (should_fail && !writing ? 0u : 4u));
    if (should_fail) {
        assert(result.push_size == (expected_pushes == 4 ? 16u : 4u) && result.cycle.way == way);
        assert(!memcmp(result.push_data, expected_victim.bytes, 16));
    }
    else
        assert(result.value == (writing ? 0xa1b2c3d4u : 0x80818283u + quarter * 0x04040404u));
    assert(reads == (profile ? 0u : 4u));
    assert(pushes == (profile == 2 ? 0 : should_fail ? fail_beat + 1 : expected_pushes));
    assert(!memcmp(&cache, &expected_cache, sizeof cache) && !memcmp(ram, expected_ram, sizeof ram));
    ++cases;
}

int main(void)
{
#ifdef CACHE040_NATIVE_TEST
    unsigned profiles = 3;
#else
    unsigned profiles = 1;
#endif
    static const unsigned sizes[] = {1, 2, 4, 16}, offsets[] = {0, 3, 4, 8, 12};
    for (profile = 0; profile < profiles; ++profile)
        for (way = 0; way < 4; ++way)
            for (mask = 1; mask < 16; ++mask)
                for (writing = 0; writing < 2; ++writing)
                    for (quarter = 0; quarter < 4; ++quarter)
                        for (master_write = 0; master_write < 2; ++master_write)
                            for (control = 0; control < 4; ++control)
                                for (unsigned n = 0; n < 4; ++n)
                                    for (unsigned o = 0; o < 5; ++o) {
                                        length = sizes[n];
                                        offset = offsets[o];
                                        fail_push = 0;
                                        if (length == 16 || offset + length <= 16)
                                            exercise(0);
                                    }
    for (profile = 0; profile < profiles && profile < 2; ++profile)
        for (way = 0; way < 4; ++way)
            for (mask = 1; mask < 16; ++mask)
                for (writing = 0; writing < 2; ++writing)
                    for (quarter = 0; quarter < 4; ++quarter)
                        for (master_write = 0; master_write < 2; ++master_write) {
                            control = 1;
                            length = master_write ? 4 : 16;
                            offset = master_write ? 3 : 0;
                            for (fail_beat = 0; fail_beat < 4; ++fail_beat) {
                                seed();
                                oracle();
                                if (fail_beat >= expected_pushes)
                                    continue;
                                fail_push = 1;
                                exercise(0);
                            }
                        }
    /* A CPU operand is acquired/precommitted before granting the bus. A
       fresh clean line can be invalidated without changing the latched read;
       a dirty CB line sinks the master's newer bytes without reapplying the
       original CPU store after arbitration. */
    for (profile = 0; profile < profiles; ++profile)
        for (way = 0; way < 4; ++way)
            for (mask = 1; mask < 16; ++mask)
                for (writing = 0; writing < 2; ++writing)
                    for (quarter = 0; quarter < 4; ++quarter) {
                        control = master_write = 1;
                        length = 4;
                        offset = fail_push = 0;
                        exercise(1);
                    }
    printf("%u victim-buffer snoop/fill/writeback/sink/invalidate/fault/ownership cases passed\n", cases);
    return 0;
}
