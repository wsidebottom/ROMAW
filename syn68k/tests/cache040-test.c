/* SPDX-License-Identifier: MIT */
/* Architectural RAM/bus observations for MC68040UM chapter 4. */
#include "cache040.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifdef CACHE040_NATIVE_TEST
#include "native040.h"

static Syn68kCache040Access emitted_access(Syn68kCache040* c, const Syn68kCache040Bus* b, uint32_t address, unsigned fc,
                                           unsigned size, int writing, uint32_t value, uint32_t attributes,
                                           uint32_t cacr, int no_allocate)
{
    Syn68kNative040Cache packet = {.cache = c,
                                   .bus = b,
                                   .address = address,
                                   .fc = fc,
                                   .size = size,
                                   .writing = writing,
                                   .value = value,
                                   .attributes = attributes,
                                   .cacr = cacr,
                                   .no_allocate = no_allocate};
    Syn68kNative040Entry entry = syn68k_native040_entry(NATIVE040_CACHE_ACCESS);
    assert(entry);
    entry(&packet);
    return packet.access;
}

static Syn68kCache040Access emitted_maintain(Syn68kCache040* c, const Syn68kCache040Bus* b, unsigned caches,
                                             unsigned scope, uint32_t address, unsigned page_shift, int push)
{
    Syn68kNative040Cache packet = {.cache = c,
                                   .bus = b,
                                   .address = address,
                                   .caches = caches,
                                   .scope = scope,
                                   .page_shift = page_shift,
                                   .push = push};
    Syn68kNative040Entry entry = syn68k_native040_entry(NATIVE040_CACHE_MAINTAIN);
    assert(entry);
    entry(&packet);
    return packet.access;
}

static Syn68kCache040Access emitted_transfer(Syn68kCache040* c, const Syn68kCache040Bus* b, uint32_t address,
                                             unsigned fc, unsigned size, int writing, uint32_t value,
                                             uint32_t attributes, uint32_t cacr, int no_allocate,
                                             enum Syn68kCache040Traffic traffic)
{
    Syn68kNative040Cache packet = {.cache = c,
                                   .bus = b,
                                   .address = address,
                                   .fc = fc,
                                   .size = size,
                                   .writing = writing,
                                   .value = value,
                                   .attributes = attributes,
                                   .cacr = cacr,
                                   .no_allocate = no_allocate,
                                   .traffic = traffic};
    Syn68kNative040Entry entry = syn68k_native040_entry(NATIVE040_CACHE_ACCESS);
    assert(entry);
    entry(&packet);
    return packet.access;
}

static Syn68kCache040Access emitted_maintain_traffic(Syn68kCache040* c, const Syn68kCache040Bus* b, unsigned caches,
                                                     unsigned scope, uint32_t address, unsigned shift, int push,
                                                     uint32_t cacr, enum Syn68kCache040Traffic traffic)
{
    Syn68kNative040Cache packet = {.cache = c,
                                   .bus = b,
                                   .address = address,
                                   .caches = caches,
                                   .scope = scope,
                                   .page_shift = shift,
                                   .push = push,
                                   .cacr = cacr,
                                   .traffic = traffic};
    Syn68kNative040Entry entry = syn68k_native040_entry(NATIVE040_CACHE_MAINTAIN);
    assert(entry);
    entry(&packet);
    return packet.access;
}

#define syn68k_cache040_transfer emitted_transfer
#define syn68k_cache040_maintain_traffic emitted_maintain_traffic
#define syn68k_cache040_access emitted_access
#define syn68k_cache040_maintain emitted_maintain
#endif

static unsigned char ram[0x80000];
static Syn68kCache040 cache;
static unsigned cases, reads, writes, inhibit_beat;
static uint32_t read_failure, write_failure;

static struct {
    uint32_t address;
    unsigned size, fc, burst, beat, way, writing;
} log_entries[2048];

static unsigned entries;
static const Syn68kCache040Line* observed_line;
static uint32_t observed_address, observed_value;
static unsigned observed_size, observed_dirty, observed_writes;

static void observe(uint32_t address, unsigned size, unsigned fc, unsigned burst, unsigned beat, unsigned way,
                    unsigned writing)
{
    assert(entries < sizeof log_entries / sizeof *log_entries);
    log_entries[entries].address = address;
    log_entries[entries].size = size;
    log_entries[entries].fc = fc;
    log_entries[entries].burst = burst;
    log_entries[entries].beat = beat;
    log_entries[entries].way = way;
    log_entries[entries++].writing = writing;
}

static int physical_read(void* opaque, uint32_t address, unsigned char* data, unsigned size, uint32_t attributes,
                         unsigned fc, unsigned burst, unsigned beat, unsigned way, unsigned* inhibit)
{
    assert(opaque == ram);
    (void)attributes;
    ++reads;
    observe(address, size, fc, burst, beat, way, 0);
    assert(size == 1 || size == 2 || size == 4);
    assert(!(address & (size - 1)));
    *inhibit = burst && beat == inhibit_beat;
    if (address == read_failure || (uint64_t)address + size > sizeof ram)
        return 0;
    memcpy(data, ram + address, size);
    return 1;
}

static int physical_write(void* opaque, uint32_t address, const unsigned char* data, unsigned size, uint32_t attributes,
                          unsigned fc, unsigned burst, unsigned beat, unsigned way)
{
    assert(opaque == ram);
    (void)attributes;
    ++writes;
    observe(address, size, fc, burst, beat, way, 1);
    assert(size == 1 || size == 2 || size == 4);
    assert(!(address & (size - 1)));
    if (observed_line) {
        assert(observed_line->valid && observed_line->address == (observed_address & ~15u) &&
               observed_line->dirty == observed_dirty);
        for (unsigned i = 0; i < observed_size; ++i)
            assert(observed_line->bytes[(observed_address & 15) + i] ==
                   (unsigned char)(observed_value >> ((observed_size - i - 1) * 8)));
        ++observed_writes;
    }
    if (address == write_failure || (uint64_t)address + size > sizeof ram)
        return 0;
    memcpy(ram + address, data, size);
    return 1;
}

static Syn68kCache040Bus bus = {.opaque = ram, .read = physical_read, .write = physical_write};

static void fixture(void)
{
    syn68k_cache040_init(&cache);
    reads = writes = entries = 0;
    inhibit_beat = 99;
    read_failure = write_failure = UINT32_MAX;
    observed_line = NULL;
    observed_writes = 0;
    for (unsigned i = 0; i < sizeof ram; ++i)
        ram[i] = (i * 13 + (i >> 8) * 7) & 255;
}

static uint32_t value_at(unsigned address, unsigned size)
{
    uint32_t v = 0;
    for (unsigned i = 0; i < size; ++i)
        v = (v << 8) | ram[address + i];
    return v;
}

static Syn68kCache040Access read_at(unsigned address, unsigned fc, unsigned size, unsigned mode)
{
    return syn68k_cache040_access(&cache, &bus, address, fc, size, 0, 0, mode << 5, 0x80008000, 0);
}

static Syn68kCache040Line* resident(unsigned bank, unsigned address)
{
    for (unsigned i = 0; i < 4; ++i) {
        Syn68kCache040Line* p = &cache.lines[bank][(address >> 4) & 63][i];
        if (p->valid && p->address == (address & ~15u))
            return p;
    }
    return NULL;
}

static void hit_and_allocation(void)
{
    for (unsigned bank = 0; bank < 2; ++bank)
        for (unsigned set = 0; set < 64; ++set)
            for (unsigned lane = 0; lane < 4; ++lane) {
                fixture();
                unsigned fc = bank ? 6 : 5, base = 0x10000 + set * 16;
                for (unsigned way = 0; way < 4; ++way) {
                    unsigned address = base + way * 0x400 + lane * 4;
                    Syn68kCache040Access r = read_at(address, fc, 4, way & 1);
                    assert(r.fault == CACHE040_OK && r.completed == 4 && r.value == value_at(address, 4));
                    assert(reads == (way + 1) * 4 && writes == 0 &&
                           cache.lines[bank][set][way].address == (address & ~15u));
                    for (unsigned beat = 0; beat < 4; ++beat) {
                        unsigned n = way * 4 + beat;
                        assert(log_entries[n].address == (address & ~15u) + ((lane + beat) & 3) * 4 &&
                               log_entries[n].burst == 16 && log_entries[n].beat == beat && log_entries[n].way == way);
                    }
                }
                assert(cache.replacement[bank] == 0);
                for (unsigned i = 0; i < 4; ++i) {
                    unsigned address = base + i * 0x400 + lane * 4;
                    ram[address] ^= 255;
                    unsigned expected = value_at(address, 4) ^ 0xff000000u;
                    Syn68kCache040Access r = read_at(address, fc, 4, 0);
                    assert(r.fault == CACHE040_OK && r.value == expected && reads == 16);
                }
                assert(cache.replacement[bank] == 0);
                /* With all ways valid the current two-bit count selects way 0. */
                Syn68kCache040Access r = read_at(base + 0x1000, fc, 4, 0);
                assert(r.fault == CACHE040_OK && !resident(bank, base) && resident(bank, base + 0x1000));
                ++cases;
            }
}

static void write_policies(void)
{
    for (unsigned mode = 0; mode < 4; ++mode)
        for (unsigned allocate = 0; allocate < 2; ++allocate)
            for (unsigned size = 1; size <= 4; size *= 2)
                for (unsigned offset = 0; offset < 16; ++offset) {
                    fixture();
                    unsigned address = 0x12040 + offset, value = 0x76543210u;
                    unsigned char original[20];
                    memcpy(original, ram + 0x12040, sizeof original);
                    Syn68kCache040Access r = syn68k_cache040_access(&cache, &bus, address, 5, size, 1, value, mode << 5,
                                                                    0x80000000, !allocate);
                    assert(r.fault == CACHE040_OK && r.completed == size);
                    unsigned copyback = mode == 1 && allocate;
                    assert(copyback ? reads >= 4 && !writes : !reads && writes > 0);
                    uint32_t mask = size == 4 ? UINT32_MAX : (1u << (size * 8)) - 1;
                    if (copyback) {
                        assert(!memcmp(original, ram + 0x12040, sizeof original));
                        r = read_at(address, 5, size, 1);
                        assert(r.fault == CACHE040_OK && r.value == (value & mask));
                        Syn68kCache040Access pushed = syn68k_cache040_maintain(&cache, &bus, 1, 3, 0, 12, 1);
                        assert(pushed.fault == CACHE040_OK);
                    }
                    assert(value_at(address, size) == (value & mask));
                    for (unsigned i = 0; i < 20; ++i)
                        if (i < offset || i >= offset + size)
                            assert(ram[0x12040 + i] == original[i]);
                    ++cases;
                }
}

static void fill_faults_and_inhibit(void)
{
    for (unsigned bank = 0; bank < 2; ++bank)
        for (unsigned lane = 0; lane < 4; ++lane)
            for (unsigned failed = 0; failed < 4; ++failed)
                for (unsigned victim = 0; victim < 3; ++victim) {
                    fixture();
                    unsigned base = 0x10040, incoming = 0x11040, fc = bank ? 6 : 5;
                    Syn68kCache040Line* old = &cache.lines[bank][4][0];
                    old->address = base;
                    old->valid = victim != 0;
                    old->dirty = victim == 2 ? 1 : 0;
                    memcpy(old->bytes, ram + base, 16);
                    /* Fill all ways so the replacement count selects the victim. */
                    for (unsigned way = 1; way < 4; ++way) {
                        cache.lines[bank][4][way].valid = 1;
                        cache.lines[bank][4][way].address = base + way * 0x400;
                    }
                    if (bank && victim == 2)
                        continue; /* I-cache lines cannot be dirty. */
                    read_failure = incoming + ((lane + failed) & 3) * 4;
                    Syn68kCache040Access r = read_at(incoming + lane * 4, fc, 4, 0);
                    assert(r.fault == (failed ? CACHE040_OK : CACHE040_FILL_FAULT));
                    assert(reads == failed + 1 && !writes && !resident(bank, incoming));
                    assert(!!resident(bank, base) == (victim == 2));
                    if (failed)
                        assert(r.value == value_at(incoming + lane * 4, 4) && r.completed == 4);
                    ++cases;
                }
    for (unsigned beat = 0; beat < 4; ++beat)
        for (unsigned writing = 0; writing < 2; ++writing) {
            fixture();
            inhibit_beat = beat;
            unsigned address = 0x11048, expected = value_at(address, 4);
            Syn68kCache040Access r =
                syn68k_cache040_access(&cache, &bus, address, 5, 4, writing, 0x76543210, 0x20, 0x80000000, 0);
            assert(r.fault == CACHE040_OK && reads == 4 && !!resident(0, address) == !!beat);
            if (writing)
                assert(beat ? !writes : writes == 1 && value_at(address, 4) == 0x76543210);
            else
                assert(r.value == expected && !writes);
            ++cases;
        }
}

static void dirty_pushes(void)
{
    for (unsigned dirty = 1; dirty < 16; ++dirty)
        for (unsigned failed = 0; failed < 5; ++failed) {
            unsigned size = (dirty & (dirty - 1)) ? 16 : 4, first = 0;
            if (size == 4)
                while (!(dirty & (1u << first)))
                    ++first;
            if (failed && failed > size / 4)
                continue;
            fixture();
            Syn68kCache040Line* line = &cache.lines[0][4][2];
            line->address = 0x12040;
            line->valid = 1;
            line->dirty = dirty;
            for (unsigned i = 0; i < 16; ++i)
                line->bytes[i] = 0xe0 + i;
            if (failed)
                write_failure = line->address + (first + failed - 1) * 4;
            Syn68kCache040Access r = syn68k_cache040_maintain(&cache, &bus, 1, 1, 0x12047, 12, 1);
            assert(r.fault == (failed ? CACHE040_PUSH_FAULT : CACHE040_OK) && !line->valid && !reads);
            assert(writes == (failed ? failed : size / 4));
            for (unsigned i = 0; i < writes; ++i)
                assert(log_entries[i].address == 0x12040 + (first + i) * 4 && log_entries[i].fc == 0 &&
                       log_entries[i].burst == (size == 16 ? 16u : 0u) && log_entries[i].way == 2);
            if (failed)
                assert(r.push_address == 0x12040 + first * 4 && r.push_size == size &&
                       !memcmp(r.push_data, line->bytes, 16));
            for (unsigned i = 0; i < (failed ? failed - 1 : size / 4) * 4; ++i)
                assert(ram[0x12040 + first * 4 + i] == 0xe0 + first * 4 + i);
            ++cases;
        }
}

static void scopes_and_disabled(void)
{
    for (unsigned selectors = 0; selectors < 4; ++selectors)
        for (unsigned scope = 1; scope < 4; ++scope)
            for (unsigned page8k = 0; page8k < 2; ++page8k) {
                fixture();
                const unsigned addresses[] = {0x10040, 0x10440, 0x11040, 0x12040};
                for (unsigned bank = 0; bank < 2; ++bank)
                    for (unsigned i = 0; i < 4; ++i) {
                        Syn68kCache040Line* p = &cache.lines[bank][4][i];
                        p->address = addresses[i];
                        p->valid = 1;
                    }
                Syn68kCache040Access r =
                    syn68k_cache040_maintain(&cache, &bus, selectors, scope, 0x10043, 12 + page8k, 0);
                assert(r.fault == CACHE040_OK && !reads && !writes);
                for (unsigned bank = 0; bank < 2; ++bank)
                    for (unsigned i = 0; i < 4; ++i) {
                        unsigned selected =
                            (selectors & (1u << bank)) &&
                            (scope == 3 ||
                             (scope == 1 ? i == 0 : (addresses[i] >> (12 + page8k)) == (0x10043u >> (12 + page8k))));
                        assert(cache.lines[bank][4][i].valid == !selected);
                    }
                ++cases;
            }
    fixture();
    Syn68kCache040Access r = read_at(0x10040, 5, 4, 1);
    assert(!r.fault);
    Syn68kCache040 original = cache;
    ram[0x10040] ^= 255;
    r = syn68k_cache040_access(&cache, &bus, 0x10040, 5, 4, 0, 0, 0x20, 0, 0);
    assert(!r.fault && r.value == value_at(0x10040, 4) && !memcmp(&original, &cache, sizeof cache));
    r = read_at(0x10040, 5, 4, 1);
    assert(!r.fault && r.value == (value_at(0x10040, 4) ^ 0xff000000u));
    ++cases;
}

static void counters_and_partial_operands(void)
{
    for (unsigned bank = 0; bank < 2; ++bank)
        for (unsigned seed = 0; seed < 4; ++seed)
            for (unsigned offset = 0; offset < 16; ++offset)
                for (unsigned size = 1; size <= 4; size *= 2) {
                    if (offset + size > 16)
                        continue;
                    fixture();
                    unsigned fc = bank ? 6 : 5;
                    for (unsigned way = 0; way < 4; ++way)
                        assert(!read_at(0x10040 + way * 0x400, fc, 4, 0).fault);
                    for (unsigned hit = 0; hit < seed; ++hit)
                        assert(!read_at(0x10040, fc, 1, 0).fault);
                    unsigned count = 1 + (((offset & 7) + size - 1) >> 3);
                    assert(!read_at(0x10040 + offset, fc, size, 0).fault);
                    unsigned selected = (seed + count) & 3;
                    assert(cache.replacement[bank] == selected);
                    assert(!read_at(0x11040, fc, 4, 0).fault);
                    assert(!resident(bank, 0x10040 + selected * 0x400) && resident(bank, 0x11040));
                    ++cases;
                }
    for (unsigned offset = 0; offset < 16; ++offset)
        for (unsigned size = 1; size <= 4; size *= 2)
            for (unsigned failed = 0; failed < 4; ++failed) {
                fixture();
                unsigned address = 0x10040 + offset;
                read_failure = 0x10040 + (((offset >> 2) + failed) & 3) * 4;
                Syn68kCache040Access r = read_at(address, 5, size, 1);
                unsigned first_chunk = size < 16 - offset ? size : 16 - offset;
                unsigned fatal = !failed || (failed == 1 && (offset & 3) + first_chunk > 4);
                assert(r.fault == (fatal ? CACHE040_FILL_FAULT : CACHE040_OK));
                assert(!resident(0, 0x10040));
                if (!fatal)
                    assert(r.completed == size && r.value == value_at(address, size));
                ++cases;
            }
}

static void aliases_and_no_allocate(void)
{
    for (unsigned mode = 2; mode < 4; ++mode)
        for (unsigned failed = 0; failed < 3; ++failed) {
            fixture();
            assert(!read_at(0x10040, 6, 4, 0).fault);
            unsigned instruction = value_at(0x10040, 4);
            Syn68kCache040Access r =
                syn68k_cache040_access(&cache, &bus, 0x10040, 5, 4, 1, 0x76543210, 0x20, 0x80000000, 0);
            assert(!r.fault && value_at(0x10040, 4) == instruction);
            reads = writes = entries = 0;
            if (failed == 1)
                write_failure = 0x10040;
            if (failed == 2)
                read_failure = 0x10040;
            r = read_at(0x10040, 5, 4, mode);
            assert(r.fault == (failed == 1 ? CACHE040_PUSH_FAULT : failed == 2 ? CACHE040_OPERAND_FAULT : CACHE040_OK));
            assert(!resident(0, 0x10040) && resident(1, 0x10040));
            assert(writes == 1 && reads == (failed == 1 ? 0u : 1u));
            if (!failed)
                assert(r.value == 0x76543210);
            write_failure = read_failure = UINT32_MAX;
            r = read_at(0x10040, 6, 4, 0);
            assert(!r.fault && r.value == instruction);
            ++cases;
        }
    for (unsigned writing = 0; writing < 2; ++writing) {
        fixture();
        Syn68kCache040Access r =
            syn68k_cache040_access(&cache, &bus, 0x10040, 5, 4, writing, 0x76543210, 0x20, 0x80000000, 1);
        assert(!r.fault && !resident(0, 0x10040) && (writing ? writes == 1 : reads == 1));
        assert(!read_at(0x10040, 5, 4, 1).fault);
        unsigned old_reads = reads, old_writes = writes;
        r = syn68k_cache040_access(&cache, &bus, 0x10040, 5, 4, writing, 0x12345678, 0x20, 0x80000000, 1);
        assert(!r.fault && reads == old_reads && writes == old_writes);
        ++cases;
    }
    /* WT writes to dirty CB aliases retain unrelated dirty data. This is a
       documented programming error with defined cache behavior. */
    fixture();
    assert(!syn68k_cache040_access(&cache, &bus, 0x10040, 5, 4, 1, 0x12345678, 0x20, 0x80000000, 0).fault);
    Syn68kCache040Access r = syn68k_cache040_access(&cache, &bus, 0x10047, 5, 4, 1, 0x76543210, 0, 0x80000000, 0);
    assert(!r.fault && resident(0, 0x10040)->dirty == 1 && writes == 3);
    assert(cache.replacement[0] == 0); /* One CB allocation plus three WT beats. */
    assert(!read_at(0x10040, 5, 4, 1).fault);
    assert(!syn68k_cache040_maintain(&cache, &bus, 1, 1, 0x10040, 12, 1).fault);
    assert(value_at(0x10040, 4) == 0x12345678 && value_at(0x10047, 4) == 0x76543210);
    ++cases;
}

static void replacement_push_faults(void)
{
    for (unsigned dirty = 1; dirty < 16; ++dirty)
        for (unsigned failed = 0; failed < 5; ++failed) {
            unsigned push_size = (dirty & (dirty - 1)) ? 16 : 4, first = 0;
            if (push_size == 4)
                while (!(dirty & (1u << first)))
                    ++first;
            if (failed && failed > push_size / 4)
                continue;
            fixture();
            for (unsigned way = 0; way < 4; ++way) {
                Syn68kCache040Line* line = &cache.lines[0][4][way];
                line->address = 0x10040 + way * 0x400;
                line->valid = 1;
                memcpy(line->bytes, ram + line->address, 16);
            }
            Syn68kCache040Line* old = &cache.lines[0][4][0];
            old->dirty = dirty;
            for (unsigned i = 0; i < 16; ++i)
                old->bytes[i] = 0xe0 + i;
            if (failed)
                write_failure = 0x10040 + (first + failed - 1) * 4;
            Syn68kCache040Access r = read_at(0x11048, 5, 4, 1);
            assert(r.fault == (failed ? CACHE040_PUSH_FAULT : CACHE040_OK) && reads == 4 &&
                   writes == (failed ? failed : push_size / 4) && resident(0, 0x11040) && !resident(0, 0x10040));
            for (unsigned i = 0; i < 4; ++i)
                assert(!log_entries[i].writing && log_entries[i].address == 0x11040 + ((2 + i) & 3) * 4);
            for (unsigned i = 4; i < entries; ++i)
                assert(log_entries[i].writing && log_entries[i].fc == 0);
            if (failed) {
                assert(r.push_address == 0x10040 + first * 4 && r.push_size == push_size);
                for (unsigned i = 0; i < 16; ++i)
                    assert(r.push_data[i] == 0xe0 + i);
                /* Reading the installed incoming line cannot reread a device
                   or repeat the already captured victim push. */
                unsigned before_reads = reads, before_writes = writes;
                r = read_at(0x11048, 5, 4, 1);
                assert(!r.fault && r.value == value_at(0x11048, 4) && reads == before_reads && writes == before_writes);
            }
            ++cases;
        }
    for (unsigned dirty = 0; dirty < 2; ++dirty)
        for (unsigned writing = 0; writing < 2; ++writing) {
            fixture();
            for (unsigned way = 0; way < 4; ++way) {
                Syn68kCache040Line* line = &cache.lines[0][4][way];
                line->address = 0x10040 + way * 0x400;
                line->valid = 1;
                memcpy(line->bytes, ram + line->address, 16);
            }
            cache.lines[0][4][0].dirty = dirty;
            inhibit_beat = 0;
            Syn68kCache040Access r =
                syn68k_cache040_access(&cache, &bus, 0x11048, 5, 4, writing, 0x76543210, 0x20, 0x80000000, 0);
            assert(!r.fault && reads == 4 && !resident(0, 0x11040) && !!resident(0, 0x10040) == dirty);
            assert(writing ? writes == 1 && value_at(0x11048, 4) == 0x76543210 : !writes);
            ++cases;
        }
    fixture();
    for (unsigned way = 0; way < 3; ++way) {
        Syn68kCache040Line* line = &cache.lines[0][4][way];
        line->address = 0x10040 + way * 0x400;
        line->valid = 1;
        line->dirty = 1;
        for (unsigned i = 0; i < 16; ++i)
            line->bytes[i] = 0xe0 + way;
    }
    write_failure = 0x10440;
    Syn68kCache040Access r = syn68k_cache040_maintain(&cache, &bus, 3, 3, 0, 12, 1);
    assert(r.fault == CACHE040_PUSH_FAULT && writes == 2 && !resident(0, 0x10040) && !resident(0, 0x10440) &&
           resident(0, 0x10840) && value_at(0x10040, 4) == 0xe0e0e0e0);
    write_failure = UINT32_MAX;
    assert(!syn68k_cache040_maintain(&cache, &bus, 3, 3, 0, 12, 1).fault && writes == 3 && !resident(0, 0x10840));
    ++cases;
}

static void write_callback_order(void)
{
    for (unsigned copyback = 0; copyback < 2; ++copyback)
        for (unsigned offset = 0; offset <= 12; ++offset)
            for (unsigned size = 1; size <= 4; size *= 2)
                for (unsigned dirty = 0; dirty < 2; ++dirty) {
                    unsigned beats = copyback                 ? (dirty ? 4 : 1)
                                     : !(offset & (size - 1)) ? 1
                                     : size == 2              ? 2
                                     : (offset & 1)           ? 3
                                                              : 2;
                    for (unsigned failed = 0; failed <= beats; ++failed) {
                        fixture();
                        unsigned address = (copyback ? 0x11040 : 0x10040) + offset;
                        unsigned prefix = size;
                        if (copyback) {
                            for (unsigned way = 0; way < 4; ++way) {
                                Syn68kCache040Line* line = &cache.lines[0][4][way];
                                line->address = 0x10040 + way * 0x400;
                                line->valid = 1;
                                memcpy(line->bytes, ram + line->address, 16);
                            }
                            cache.lines[0][4][0].dirty = dirty ? 3 : 1;
                            for (unsigned i = 0; i < 16; ++i)
                                cache.lines[0][4][0].bytes[i] = 0xe0 + i;
                            if (failed)
                                write_failure = 0x10040 + (failed - 1) * 4;
                        }
                        else {
                            assert(!read_at(0x10040, 5, 4, 0).fault);
                            cache.lines[0][4][0].dirty = dirty;
                            if (failed) {
                                prefix = failed == 1 ? 0 : (offset & 1) ? (failed == 2 ? 1 : 3) : 2;
                                write_failure = address + prefix;
                            }
                        }
                        unsigned char original[16];
                        memcpy(original, ram + (address & ~15u), 16);
                        observed_line = &cache.lines[0][4][0];
                        observed_address = address;
                        observed_value = 0x76543210;
                        observed_size = size;
                        observed_dirty = copyback ? 0 : dirty;
                        if (copyback)
                            for (unsigned i = 0; i < size; ++i)
                                observed_dirty |= 1u << ((offset + i) >> 2);
                        unsigned old_writes = writes;
                        Syn68kCache040Access r = syn68k_cache040_access(
                            &cache, &bus, address, 5, size, 1, observed_value, copyback ? 0x20 : 0, 0x80000000, 0);
                        assert(r.fault ==
                               (failed ? copyback ? CACHE040_PUSH_FAULT : CACHE040_OPERAND_FAULT : CACHE040_OK));
                        assert(r.completed == (copyback ? size : prefix) &&
                               observed_writes == (failed ? failed : beats) && writes == old_writes + observed_writes);
                        unsigned mask = size == 4 ? UINT32_MAX : (1u << (size * 8)) - 1;
                        assert(r.value == (observed_value & mask));
                        for (unsigned i = 0; i < 16; ++i) {
                            unsigned changed = !copyback && i >= offset && i < offset + prefix;
                            unsigned byte = changed ? (unsigned char)(observed_value >> ((size - (i - offset) - 1) * 8))
                                                    : original[i];
                            assert(ram[(address & ~15u) + i] == byte);
                        }
                        if (copyback && failed)
                            for (unsigned i = 0; i < 16; ++i)
                                assert(r.push_data[i] == 0xe0 + i);
                        ++cases;
                    }
                }
    for (unsigned failed_line = 0; failed_line < 2; ++failed_line) {
        fixture();
        for (unsigned set = 4; set <= 5; ++set)
            for (unsigned way = 0; way < 4; ++way) {
                Syn68kCache040Line* line = &cache.lines[0][set][way];
                line->address = 0x10000 + set * 16 + way * 0x400;
                line->valid = 1;
                line->dirty = 1;
                memcpy(line->bytes, ram + line->address, 16);
            }
        /* The first cache access advances the bank-wide replacement counter,
           so the second line replaces way one in its different set. */
        write_failure = failed_line ? 0x10450 : 0x10040;
        Syn68kCache040Access r =
            syn68k_cache040_access(&cache, &bus, 0x1104f, 5, 4, 1, 0x76543210, 0x20, 0x80000000, 0);
        assert(r.fault == CACHE040_PUSH_FAULT && r.completed == (failed_line ? 4u : 1u) && r.value == 0x76543210 &&
               resident(0, 0x11040)->bytes[15] == 0x76);
        Syn68kCache040Line* next = resident(0, 0x11050);
        if (failed_line)
            assert(next && next->bytes[0] == 0x54 && next->bytes[1] == 0x32 && next->bytes[2] == 0x10 &&
                   next->dirty == 1);
        else
            assert(!next);
        ++cases;
    }
}

int main(void)
{
    hit_and_allocation();
    write_policies();
    fill_faults_and_inhibit();
    dirty_pushes();
    scopes_and_disabled();
    counters_and_partial_operands();
    aliases_and_no_allocate();
    replacement_push_faults();
    write_callback_order();
    printf("%u physical 68040 cache allocation/policy/burst/fault/push/scope cases passed\n", cases);
    return 0;
}
