/* SPDX-License-Identifier: MIT */
#include "native040.h"
#include <string.h>
#include <assert.h>
#include <stdio.h>
static unsigned attempts, fail_at, write_attempts, fail_write_at, cases;
#ifdef PARTIAL_NATIVE
static unsigned native_mode;
#endif

static unsigned char sample(uint32_t a)
{
    return (unsigned char)(a ^ 0xa5);
}

static int read_bytes(void* p, uint32_t a, unsigned char* bytes, unsigned n, uint32_t attr, unsigned fc)
{
    (void)p;
    (void)attr;
    (void)fc;
    if (++attempts == fail_at) {
        memset(bytes, 0xee, n);
        return 0;
    }
    for (unsigned i = 0; i < n; ++i)
        bytes[i] = sample(a + i);
    return 1;
}

static int read_cycle(void* p, uint32_t a, unsigned char* bytes, unsigned n, const Syn68kCache040Cycle* cycle,
                      unsigned* inhibit)
{
    *inhibit = 0;
    return read_bytes(p, a, bytes, n, cycle->attributes, cycle->fc);
}

static int write_bytes(void* p, uint32_t a, const unsigned char* bytes, unsigned n, uint32_t attr, unsigned fc)
{
    (void)p;
    (void)a;
    (void)bytes;
    (void)n;
    (void)attr;
    (void)fc;
    return ++write_attempts != fail_write_at;
}

static int write_cycle(void* p, uint32_t a, const unsigned char* bytes, unsigned n, const Syn68kCache040Cycle* cycle)
{
    return write_bytes(p, a, bytes, n, cycle->attributes, cycle->fc);
}

#if defined(PARTIAL_NATIVE) && !defined(PARTIAL_NATIVE_GROUPED)
Syn68kCache040Access __real_syn68k_cache040_transfer(Syn68kCache040*, const Syn68kCache040Bus*, uint32_t, unsigned,
                                                     unsigned, int, uint32_t, uint32_t, uint32_t, int,
                                                     enum Syn68kCache040Traffic);

Syn68kCache040Access __wrap_syn68k_cache040_transfer(Syn68kCache040* c, const Syn68kCache040Bus* b, uint32_t a,
                                                     unsigned f, unsigned n, int w, uint32_t v, uint32_t at,
                                                     uint32_t cacr, int noalloc, enum Syn68kCache040Traffic traffic)
{
    assert(!native_mode);
    return __real_syn68k_cache040_transfer(c, b, a, f, n, w, v, at, cacr, noalloc, traffic);
}

Syn68kCache040Access __real_syn68k_cache040_access(Syn68kCache040*, const Syn68kCache040Bus*, uint32_t, unsigned,
                                                   unsigned, int, uint32_t, uint32_t, uint32_t, int);

Syn68kCache040Access __wrap_syn68k_cache040_access(Syn68kCache040* c, const Syn68kCache040Bus* b, uint32_t a,
                                                   unsigned f, unsigned n, int w, uint32_t v, uint32_t at,
                                                   uint32_t cacr, int noalloc)
{
    assert(!native_mode);
    return __real_syn68k_cache040_access(c, b, a, f, n, w, v, at, cacr, noalloc);
}

Syn68kMMU040Access __real_syn68k_mmu040_memory_translate(Syn68kMMU040Memory*, uint32_t, unsigned, int, int);

Syn68kMMU040Access __wrap_syn68k_mmu040_memory_translate(Syn68kMMU040Memory* m, uint32_t a, unsigned fc, int w,
                                                         int probe)
{
    assert(!native_mode);
    return __real_syn68k_mmu040_memory_translate(m, a, fc, w, probe);
}
#endif
static Syn68kCache040Access physical_read(Syn68kCache040* cache, const Syn68kCache040Bus* bus, uint32_t a, unsigned fc,
                                          unsigned n, unsigned attrs, unsigned cacr, int noalloc)
{
#ifdef PARTIAL_NATIVE
    if (native_mode) {
        Syn68kNative040Cache p = {.cache = cache,
                                  .bus = bus,
                                  .address = a,
                                  .fc = fc,
                                  .size = n,
                                  .attributes = attrs,
                                  .cacr = cacr,
                                  .no_allocate = noalloc};
        Syn68kNative040Entry entry = syn68k_native040_entry(NATIVE040_CACHE_ACCESS);
        assert(entry);
        entry(&p);
        return p.access;
    }
#endif
    return syn68k_cache040_access(cache, bus, a, fc, n, 0, 0, attrs, cacr, noalloc);
}

static Syn68kMMU040Access logical_read(Syn68kMMU040Memory* memory, uint32_t a, unsigned fc, unsigned n, uint32_t* out)
{
#ifdef PARTIAL_NATIVE
    if (native_mode) {
        Syn68kNative040MMU p = {.memory = memory, .address = a, .fc = fc, .size = n, .value = *out};
        Syn68kNative040Entry entry = syn68k_native040_entry(NATIVE040_READ);
        assert(entry);
        entry(&p);
        if (!p.access.bus_error && !p.access.translation.fault)
            *out = p.value;
        return p.access;
    }
#endif
    return syn68k_mmu040_read(memory, a, fc, n, 0, out);
}

/* Independent literal natural-beat widths indexed by size-1 and address&3. */
static const unsigned widths[4][4][3] = {{{1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}},
                                         {{2, 0, 0}, {1, 1, 0}, {2, 0, 0}, {1, 1, 0}},
                                         {{2, 1, 0}, {1, 2, 0}, {2, 1, 0}, {1, 2, 0}},
                                         {{4, 0, 0}, {1, 2, 1}, {2, 2, 0}, {1, 2, 1}}};

static void natural_faults(void)
{
    for (unsigned mode = 0; mode < 7; ++mode)
        for (unsigned offset = 0; offset < 32; ++offset)
            for (unsigned size = 1; size <= 4; ++size) {
                if (mode < 3 && size == 3)
                    continue;
                const unsigned* beats = widths[size - 1][offset & 3];
                unsigned count = 0;
                while (count < 3 && beats[count])
                    ++count;
                for (unsigned fail = 0; fail <= count; ++fail) {
                    unsigned completed = 0;
                    for (unsigned i = 0; i < (fail ? fail - 1 : count); ++i)
                        completed += beats[i];
                    uint32_t address = 0x1000 + offset, expected = 0;
                    for (unsigned i = 0; i < completed; ++i)
                        expected = expected << 8 | sample(address + i);
                    attempts = 0;
                    fail_at = fail;
                    Syn68kCache040 cache;
                    syn68k_cache040_init(&cache);
                    unsigned got_completed;
                    uint32_t got_value;
                    int fault;
                    if (mode < 3) {
                        Syn68kMMU040 mmu;
                        syn68k_mmu040_init(&mmu);
                        mmu.dtt[0] = 0xc040;
                        uint32_t cacr = mode == 2 ? 0x80000000u : 0;
                        Syn68kMMU040Cache binding = {.state = &cache, .cacr = &cacr};
                        Syn68kMMU040Memory memory = {.mmu = &mmu,
                                                     .read = read_bytes,
                                                     .cache = mode ? &binding : 0,
                                                     .aligned_transfers = mode == 0};
                        uint32_t output = 0xdeadbeef;
                        Syn68kMMU040Access r = logical_read(&memory, address, 5, size, &output);
                        got_completed = r.completed;
                        got_value = r.value;
                        fault = r.bus_error;
                        if (r.translation.fault || output != (fail ? 0xdeadbeef : expected))
                            fprintf(
                                stderr,
                                "outer mode=%u offset=%u size=%u fail=%u output=%x/%x translation=%u completed=%u attempts=%u\n",
                                mode, offset, size, fail, output, fail ? 0xdeadbeef : expected, r.translation.fault,
                                r.completed, attempts);
                        assert(!r.translation.fault && output == (fail ? 0xdeadbeef : expected));
                        if (mode)
                            assert(r.cache.completed == (fail ? completed : 0));
                    }
                    else {
                        Syn68kCache040Bus bus = {.read_cycle = read_cycle, .write_cycle = write_cycle};
                        Syn68kCache040Access r = physical_read(&cache, &bus, address, 5, size,
                                                               mode == 5   ? 0x40
                                                               : mode == 6 ? 0x60
                                                                           : 0,
                                                               mode == 3 ? 0 : 0x80000000u, 1);
                        got_completed = r.completed;
                        got_value = r.value;
                        fault = r.fault != CACHE040_OK;
                    }
                    if (got_completed != completed || got_value != expected || fault != !!fail)
                        fprintf(stderr, "mode=%u offset=%u size=%u fail=%u completed=%u/%u value=%x/%x fault=%u\n",
                                mode, offset, size, fail, got_completed, completed, got_value, expected, fault);
                    assert(attempts == (fail ? fail : count) && got_completed == completed && got_value == expected &&
                           fault == !!fail);
                    ++cases;
                }
            }
}

static void cached_prefixes(void)
{
    for (unsigned api = 0; api < 2; ++api)
        for (unsigned head = 1; head < 4; ++head)
            for (unsigned fc = 1; fc <= 6; ++fc) {
                if (fc == 3 || fc == 4)
                    continue;
                unsigned nbeats = head == 1 ? 2 : 1;
                for (unsigned scenario = 0; scenario < 3; ++scenario)
                    for (unsigned fail = 0; fail <= (scenario == 0 ? nbeats : 4); ++fail) {
                        Syn68kCache040 cache;
                        syn68k_cache040_init(&cache);
                        unsigned bank = fc == 2 || fc == 6;
                        if (bank && scenario == 2)
                            continue;
                        Syn68kCache040Line* first = &cache.lines[bank][0][0];
                        *first = (Syn68kCache040Line){.address = 0x1000, .valid = 1};
                        for (unsigned i = 0; i < 16; ++i)
                            first->bytes[i] = 0x70 + i;
                        if (scenario == 2)
                            for (unsigned way = 0; way < 4; ++way) {
                                Syn68kCache040Line* victim = &cache.lines[bank][1][way];
                                *victim =
                                    (Syn68kCache040Line){.address = 0x2010 + way * 0x400, .valid = 1, .dirty = 15};
                                for (unsigned i = 0; i < 16; ++i)
                                    victim->bytes[i] = 0xc0 + way + i;
                            }
                        /* The CPU's data miss bypass is bank-specific. Instruction no-allocation
      cases use the physical API, whose no_allocate flag owns that policy. */
                        if (!api && scenario == 0 && bank)
                            continue;
                        cache.bypass_data_miss = scenario == 0;
                        attempts = write_attempts = 0;
                        fail_at = scenario == 2 ? 0 : fail;
                        fail_write_at = scenario == 2 ? fail : 0;
                        Syn68kCache040Bus bus = {.read_cycle = read_cycle, .write_cycle = write_cycle};
                        unsigned completed = 4, fault = 0;
                        if (fail && (scenario == 2 || (scenario == 1 && fail == 1)))
                            completed = head, fault = 1;
                        if (fail && scenario == 0)
                            completed = head + (fail == 2 ? 2 : 0), fault = 1;
                        uint32_t expected = 0;
                        for (unsigned i = 0; i < completed; ++i)
                            expected = expected << 8 | (i < head ? 0x80 - head + i : sample(0x1010 + i - head));
                        uint32_t value;
                        unsigned got_completed;
                        enum Syn68kCache040Fault kind;
                        if (api) {
                            Syn68kCache040Access result =
                                physical_read(&cache, &bus, 0x1010 - head, fc, 4, 0, 0x80008000, scenario == 0);
                            value = result.value;
                            got_completed = result.completed;
                            kind = result.fault;
                        }
                        else {
                            Syn68kMMU040 mmu;
                            syn68k_mmu040_init(&mmu);
                            mmu.dtt[0] = mmu.itt[0] = 0xc000;
                            uint32_t cacr = 0x80008000;
                            Syn68kMMU040Cache binding = {.state = &cache, .cacr = &cacr};
                            Syn68kMMU040Memory memory = {
                                .mmu = &mmu, .read = read_bytes, .write = write_bytes, .cache = &binding};
                            uint32_t output = 0xdeadbeef;
                            Syn68kMMU040Access result = logical_read(&memory, 0x1010 - head, fc, 4, &output);
                            if (result.translation.fault || output != (fault ? 0xdeadbeef : expected))
                                fprintf(
                                    stderr,
                                    "outer cached head=%u fc=%u scenario=%u fail=%u completed=%u output=%x/%x kind=%u\n",
                                    head, fc, scenario, fail, result.completed, output, fault ? 0xdeadbeef : expected,
                                    result.cache.fault);
                            assert(!result.translation.fault && output == (fault ? 0xdeadbeef : expected));
                            value = result.value;
                            got_completed = result.completed;
                            kind = result.cache.fault;
                            if (fault)
                                assert(result.cache.completed == head + (scenario == 0 && fail == 2 ? 2 : 0) &&
                                       result.cache.value == expected);
                        }
                        if (got_completed != completed || value != expected || !!kind != fault)
                            fprintf(
                                stderr,
                                "cached api=%u head=%u fc=%u scenario=%u fail=%u completed=%u/%u value=%x/%x kind=%u\n",
                                api, head, fc, scenario, fail, got_completed, completed, value, expected, kind);
                        assert(got_completed == completed && value == expected && !!kind == fault);
                        assert(attempts == (scenario == 0   ? (fail ? fail : nbeats)
                                            : scenario == 1 ? (fail ? fail : 4)
                                                            : 4));
                        assert(write_attempts == (scenario == 2 ? (fail ? fail : 4) : 0));
                        if (fault)
                            assert(kind == (scenario == 0   ? CACHE040_OPERAND_FAULT
                                            : scenario == 1 ? CACHE040_FILL_FAULT
                                                            : CACHE040_PUSH_FAULT));
                        ++cases;
                    }
            }
}

int main(void)
{
#if !defined(PARTIAL_NATIVE_GROUPED) && !defined(PARTIAL_NATIVE_ONLY)
    natural_faults();
    cached_prefixes();
#endif
#ifdef PARTIAL_NATIVE
    native_mode = 1;
    natural_faults();
    cached_prefixes();
#endif
    printf("%u independent read-prefix/natural-beat/cache-hit/fill/push/atomic-output cases passed\n", cases);
    return 0;
}
