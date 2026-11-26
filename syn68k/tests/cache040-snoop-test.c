/*+===================================================================
  File:      cache040-snoop-test.c

  Summary:   Verify external snoop state transitions, byte transfers, aliases and ownership guards.

  Classes:   None.

  Functions: main

  Origin:    Original ROMAW regression for MC68040UM Table 4-1 and Figure 4-6.

  Copyright and Legal notices: Applicable license: MIT; see the SPDX notice below.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* UM Table 4-1/Figure 4-6 external-master state transitions. */
#include "mmu040-memory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef CACHE040_NATIVE_TEST
#include "native040.h"
#endif

static Syn68kCache040 cache, expected;
static Syn68kMMU040 mmu, saved_mmu;
static unsigned cases, kind, instruction_way;
static uint32_t cacr;
static unsigned char data[16], saved_data[16];
static Syn68kMMU040Cache binding;
static Syn68kMMU040Memory memory;

#ifdef CACHE040_NATIVE_TEST
Syn68kCache040Snoop __wrap_syn68k_cache040_snoop(Syn68kCache040* c, uint32_t a, unsigned sc, unsigned tt, unsigned n,
                                                 int w, unsigned char d[16])
{
    (void)c;
    (void)a;
    (void)sc;
    (void)tt;
    (void)n;
    (void)w;
    (void)d;
    assert(!"original C snoop helper used by emitted fixture");
    return (Syn68kCache040Snoop){.fault = CACHE040_BAD_REQUEST};
}
#endif

static Syn68kCache040Snoop invoke(Syn68kCache040* c, uint32_t a, unsigned sc, unsigned tt, unsigned n, int w,
                                  unsigned char* d)
{
    Syn68kCache040Snoop result;
#ifdef CACHE040_NATIVE_TEST
    if (!kind) {
        if (!d)
            return (Syn68kCache040Snoop){.fault = CACHE040_BAD_REQUEST};
        Syn68kNative040Cache p = {
            .cache = c, .address = a, .size = n, .writing = w, .snoop_control = sc, .transfer_type = tt};
        unsigned count = n < 16 ? n : 16;
        memcpy(p.line, d, count);
        Syn68kNative040Entry entry = syn68k_native040_entry(NATIVE040_CACHE_SNOOP);
        assert(entry);
        entry(&p);
        result = p.snoop;
        if (!w && result.intervention)
            memcpy(d, p.line, n);
    }
    else {
        binding.state = c;
        typedef Syn68kCache040Snoop (*Entry)(Syn68kMMU040Memory*, uint32_t, unsigned, unsigned, unsigned, int,
                                             unsigned char*);
        Entry entry = (Entry)(uintptr_t)syn68k_native040_entry(NATIVE040_BUS_SNOOP);
        assert(entry);
        result = entry(&memory, a, sc, tt, n, w, d);
    }
#else
    if (!kind)
        result = syn68k_cache040_snoop(c, a, sc, tt, n, w, d);
    else {
        binding.state = c;
        result = syn68k_mmu040_snoop(&memory, a, sc, tt, n, w, d);
    }
#endif
    assert(!memcmp(&mmu, &saved_mmu, sizeof mmu));
    return result;
}

static uint32_t seed(unsigned set, unsigned way, unsigned state)
{
    syn68k_cache040_init(&cache);
    cache.replacement[0] = 3;
    cache.replacement[1] = 1;
    cache.bypass_data_miss = 1;
    uint32_t address = (set < 32 ? 0x12345000u : 0xfffffc00u) + set * 16;
    instruction_way = (way + state + 1) & 3;
    for (unsigned bank = 0; bank < 2; ++bank)
        for (unsigned w = 0; w < 4; ++w) {
            Syn68kCache040Line* line = &cache.lines[bank][set][w];
            line->valid = bank || w != way || state != 0;
            line->address = w == (bank ? instruction_way : way) ? address : address ^ ((w + 1) << 16);
            line->dirty = bank ? 0 : w == way ? state > 1 ? state - 1 : state ? 0 : 15 : 9;
            for (unsigned i = 0; i < 16; ++i)
                line->bytes[i] = bank * 0x80 + w * 16 + i;
        }
    for (unsigned i = 0; i < 16; ++i)
        data[i] = saved_data[i] = 0xe0 + i;
    cacr = (way & 1 ? 0x80000000u : 0) | (way & 2 ? 0x8000u : 0);
    memset(&mmu, 0x6b, sizeof mmu);
    mmu.tc = set & 1 ? 0x8000 : 0;
    saved_mmu = mmu;
    binding = (Syn68kMMU040Cache){.state = &cache, .cacr = &cacr};
    memory = (Syn68kMMU040Memory){.mmu = &mmu, .cache = &binding};
    expected = cache;
    return address;
}

enum { HIT = 1, INTERVENE = 2, INVALIDATE = 4 };

/* Columns are miss, clean, dirty. These are the manual's transitions,
   independently of the implementation's lookup and update algorithm. */
static const unsigned read_actions[4][3] = {
    {0, 0, 0}, {0, HIT, HIT | INTERVENE}, {0, HIT | INVALIDATE, HIT | INTERVENE | INVALIDATE}, {0, 0, 0}};
static const unsigned scalar_write_actions[4][3] = {
    {0, 0, 0}, {0, HIT | INVALIDATE, HIT | INTERVENE}, {0, HIT | INVALIDATE, HIT | INVALIDATE}, {0, 0, 0}};
static const unsigned line_write_actions[4][3] = {
    {0, 0, 0}, {0, HIT | INVALIDATE, HIT | INVALIDATE}, {0, HIT | INVALIDATE, HIT | INVALIDATE}, {0, 0, 0}};

static void vector(unsigned set, unsigned way, unsigned state, unsigned sc, unsigned tt, unsigned size, unsigned offset,
                   int writing)
{
    uint32_t address = seed(set, way, state);
    unsigned status = state > 1 ? 2 : state;
    unsigned action = tt > 1       ? 0
                      : !writing   ? read_actions[sc][status]
                      : size == 16 ? line_write_actions[sc][status]
                                   : scalar_write_actions[sc][status];
    Syn68kCache040Line* line = &expected.lines[0][set][way];
    unsigned char output[16];
    memcpy(output, data, sizeof output);
    if (action & INTERVENE) {
        unsigned start = size == 16 ? 0 : offset;
        if (writing) {
            for (unsigned i = 0; i < size; ++i) {
                line->bytes[start + i] = data[i];
                line->dirty |= 1u << ((start + i) / 4);
            }
        }
        else
            memcpy(output, line->bytes + start, size);
    }
    if (action & INVALIDATE)
        line->valid = 0;
    unsigned instruction = tt < 2 && (sc == 1 || sc == 2) && (writing || sc == 2);
    if (instruction)
        expected.lines[1][set][instruction_way].valid = 0;
    Syn68kCache040Snoop result = invoke(&cache, address + offset, sc, tt, size, writing, data);
    assert(!result.fault && result.hit == ((action & HIT ? 1u : 0u) | (instruction ? 2u : 0u)) &&
           result.intervention == !!(action & INTERVENE) &&
           result.invalidated == ((action & INVALIDATE ? 1u : 0u) | (instruction ? 2u : 0u)));
    assert(result.way[0] == (action & HIT ? way : 0) && result.way[1] == (instruction ? instruction_way : 0));
    assert(!memcmp(&cache, &expected, sizeof cache) && !memcmp(data, output, sizeof data));
    ++cases;
}

static void guards(void)
{
    for (unsigned bad = 0; bad < 14; ++bad) {
        uint32_t address = seed(63, 3, 16);
        unsigned sc = 1, tt = 0, n = 4;
        int writing = 1;
        unsigned char* bytes = data;
        switch (bad) {
        case 0:
            sc = 4;
            break;
        case 1:
            tt = 4;
            break;
        case 2:
            n = 0;
            break;
        case 3:
            n = 3;
            break;
        case 4:
            n = 5;
            break;
        case 5:
            n = 8;
            break;
        case 6:
            n = 17;
            break;
        case 7:
            writing = -1;
            break;
        case 8:
            writing = 2;
            break;
        case 9:
            bytes = NULL;
            break;
        case 10:
            address += 15;
            n = 2;
            break;
        case 11:
            address += 13;
            break;
        case 12:
            cache.active = expected.active = 1;
            break;
        case 13:
            cache.active = expected.active = 1;
            sc = 2;
            tt = 1;
            break;
        }
        Syn68kCache040Snoop result = invoke(&cache, address, sc, tt, n, writing, bytes);
        assert(result.fault == CACHE040_BAD_REQUEST && !result.hit && !result.intervention && !result.invalidated &&
               !result.way[0] && !result.way[1]);
        assert(!memcmp(&cache, &expected, sizeof cache) && !memcmp(data, saved_data, sizeof data));
        ++cases;
    }
    for (unsigned sc = 0; sc < 4; ++sc)
        for (unsigned tt = 0; tt < 4; ++tt) {
            uint32_t address = seed(63, 3, 16);
            cache.active = expected.active = 1;
            Syn68kCache040Snoop result = invoke(&cache, address, sc, tt, 4, 0, data);
            unsigned busy = (sc == 1 || sc == 2) && tt < 2;
            assert(result.fault == (busy ? CACHE040_BAD_REQUEST : CACHE040_OK) && !result.hit && !result.intervention &&
                   !result.invalidated && !result.way[0] && !result.way[1]);
            assert(!memcmp(&cache, &expected, sizeof cache) && !memcmp(data, saved_data, sizeof data));
            ++cases;
            result = invoke(NULL, address, sc, tt, 4, 1, data);
            assert(!result.fault && !result.hit && !result.intervention && !result.invalidated && !result.way[0] &&
                   !result.way[1]);
            assert(!memcmp(&cache, &expected, sizeof cache) && !memcmp(data, saved_data, sizeof data));
            ++cases;
        }
}

static void aliases(void)
{
    for (unsigned n = 1; n <= 4; n *= 2)
        for (unsigned from = 0; from + n <= 16; ++from)
            for (unsigned to = 0; to + n <= 16; ++to) {
                uint32_t address = seed(1, 2, 2);
                unsigned char captured[4];
                memcpy(captured, cache.lines[0][1][2].bytes + from, n);
                Syn68kCache040Line* line = &expected.lines[0][1][2];
                for (unsigned i = 0; i < n; ++i) {
                    line->bytes[to + i] = captured[i];
                    line->dirty |= 1u << ((to + i) / 4);
                }
                expected.lines[1][1][instruction_way].valid = 0;
                Syn68kCache040Snoop result =
                    invoke(&cache, address + to, 1, 0, n, 1, cache.lines[0][1][2].bytes + from);
                assert(!result.fault && result.hit == 3 && result.intervention && result.invalidated == 2 &&
                       result.way[0] == 2 && result.way[1] == instruction_way);
                assert(!memcmp(&cache, &expected, sizeof cache));
                ++cases;
            }
}

static void metadata_aliases(void)
{
    for (unsigned way = 0; way < 4; ++way) {
        uint32_t address = seed(1, way, 2);
        unsigned char* input = (unsigned char*)&cache.lines[1][1][instruction_way].valid;
        unsigned char captured = *input;
        expected.lines[1][1][instruction_way].valid = 0;
        expected.lines[0][1][way].bytes[7] = captured;
        expected.lines[0][1][way].dirty |= 2;
        Syn68kCache040Snoop result = invoke(&cache, address + 7, 1, 0, 1, 1, input);
        assert(!result.fault && result.hit == 3 && result.intervention && result.invalidated == 2 &&
               result.way[0] == way && result.way[1] == instruction_way);
        assert(!memcmp(&cache, &expected, sizeof cache) &&
               "snoop input must be latched before instruction invalidation");
        ++cases;
    }
}

int main(void)
{
    static const unsigned sets[] = {0, 1, 32, 63}, sizes[] = {1, 2, 4, 16};
    for (kind = 0; kind < 2; ++kind) {
        for (unsigned s = 0; s < 4; ++s)
            for (unsigned way = 0; way < 4; ++way)
                for (unsigned state = 0; state < 17; ++state)
                    for (unsigned sc = 0; sc < 4; ++sc)
                        for (unsigned tt = 0; tt < 4; ++tt)
                            for (unsigned writing = 0; writing < 2; ++writing)
                                for (unsigned n = 0; n < 4; ++n)
                                    for (unsigned offset = 0; offset < 16; ++offset)
                                        if (sizes[n] == 16 || offset + sizes[n] <= 16)
                                            vector(sets[s], way, state, sc, tt, sizes[n], offset, writing);
        guards();
        aliases();
        metadata_aliases();
    }
    printf("%u physical snoop/state/SC/TT/dirty-mask/offset/alias/ownership cases passed\n", cases);
    return 0;
}
