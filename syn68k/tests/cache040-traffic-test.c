/* SPDX-License-Identifier: MIT */
/* MMU/cache traffic policies: MC68040UM 3.2.5, 4.2 and 4.3.3. */
#define main cache040_traffic_matrix_unused
#include "cache040-test.c"
#undef main

static Syn68kCache040Cycle cycles[2048];
static unsigned typed_hits;

static int read_cycle(void* opaque, uint32_t address, unsigned char* bytes, unsigned size,
                      const Syn68kCache040Cycle* cycle, unsigned* inhibit)
{
    assert(entries < 2048 && cycle->traffic == cache.traffic);
    cycles[entries] = *cycle;
    ++typed_hits;
    return physical_read(opaque, address, bytes, size, cycle->attributes, cycle->fc, cycle->burst, cycle->beat,
                         cycle->way, inhibit);
}

static int write_cycle(void* opaque, uint32_t address, const unsigned char* bytes, unsigned size,
                       const Syn68kCache040Cycle* cycle)
{
    assert(entries < 2048 && cycle->traffic == cache.traffic);
    cycles[entries] = *cycle;
    ++typed_hits;
    return physical_write(opaque, address, bytes, size, cycle->attributes, cycle->fc, cycle->burst, cycle->beat,
                          cycle->way);
}

static Syn68kCache040Bus cycle_bus = {.opaque = ram, .read_cycle = read_cycle, .write_cycle = write_cycle};

static void traffic_fixture(void)
{
    syn68k_cache040_init(&cache);
    reads = writes = entries = typed_hits = 0;
    inhibit_beat = 99;
    read_failure = write_failure = UINT32_MAX;
    observed_line = NULL;
    observed_writes = 0;
    /* These fixtures touch only this physical RAM window. Keep its initial
       bytes deterministic without clearing unrelated backing storage. */
    for (unsigned i = 0x10000; i < 0x13000; ++i)
        ram[i] = (i * 13 + (i >> 8) * 7) & 255;
}

static void install(unsigned bank, unsigned address, unsigned state)
{
    if (!state)
        return;
    Syn68kCache040Line* line = &cache.lines[bank][(address >> 4) & 63][0];
    line->address = address & ~15u;
    line->valid = 1;
    line->dirty = state == 2 ? 3 : 0;
    for (unsigned i = 0; i < 16; ++i)
        line->bytes[i] = 0xe0 + i;
}

static uint32_t cached_value(unsigned address, unsigned size)
{
    uint32_t v = 0;
    for (unsigned i = 0; i < size; ++i)
        v = (v << 8) | (0xe0 + ((address + i) & 15));
    return v;
}

static void partial_chunks(void)
{
    for (unsigned both = 0; both < 2; ++both)
        for (unsigned bank = 0; bank < 2; ++bank)
            for (unsigned mode = 0; mode < 4; ++mode)
                for (unsigned enabled = 0; enabled < 2; ++enabled)
                    for (unsigned writing = 0; writing < 2; ++writing)
                        for (unsigned offset = 0; offset < 18; ++offset) {
                            if (bank && writing)
                                continue;
                            traffic_fixture();
                            Syn68kCache040Bus b = cycle_bus;
                            if (both) {
                                b.read = physical_read;
                                b.write = physical_write;
                            }
                            unsigned address = 0x10040 + offset, fc = bank ? 6 : 5;
                            unsigned original = value_at(address, 3);
                            Syn68kCache040Access r =
                                syn68k_cache040_transfer(&cache, &b, address, fc, 3, writing, 0x76543210, mode << 5,
                                                         enabled ? 0x80008000 : 0, 0, CACHE040_NORMAL);
                            assert(!r.fault && r.completed == 3 && typed_hits == entries && !cache.active);
                            if (!writing) {
                                assert(r.value == original);
                                if (enabled && mode < 2)
                                    assert(cache.replacement[bank] == 1 + (((offset & 7) + 2) >> 3));
                            }
                            else {
                                assert(r.value == 0x543210);
                                assert(!syn68k_cache040_maintain(&cache, &b, 1, 3, 0, 12, 1).fault);
                                assert(value_at(address, 3) == 0x543210);
                            }
                            assert(cache.traffic == CACHE040_NORMAL);
                            ++cases;
                        }
}

static void special_policies(void)
{
    for (unsigned descriptor = 0; descriptor < 2; ++descriptor)
        for (unsigned mode = 0; mode < 4; ++mode)
            for (unsigned enabled = 0; enabled < 2; ++enabled)
                for (unsigned state = 0; state < 3; ++state)
                    for (unsigned writing = 0; writing < 2; ++writing)
                        for (unsigned size = 1; size <= 4; ++size)
                            for (unsigned offset = 0; offset <= 12; ++offset) {
                                if (descriptor && (size != 4 || (offset & 3)))
                                    continue;
                                traffic_fixture();
                                install(0, 0x10040, state);
                                Syn68kCache040 before = cache;
                                unsigned address = 0x10040 + offset, effective = descriptor ? 0 : mode;
                                unsigned expected = value_at(address, size);
                                unsigned char expected_ram[16];
                                memcpy(expected_ram, ram + 0x10040, 16);
                                Syn68kCache040Access r = syn68k_cache040_transfer(
                                    &cache, &cycle_bus, address, 5, size, writing, 0x76543210, 0x300 | (mode << 5),
                                    enabled ? 0x80000000 : 0, 0, descriptor ? CACHE040_DESCRIPTOR : CACHE040_EXCEPTION);
                                if (descriptor && !enabled && state) {
                                    assert(r.fault == CACHE040_DESCRIPTOR_LOCKUP && !entries &&
                                           r.bus_address == address && !memcmp(&before, &cache, sizeof cache));
                                    ++cases;
                                    continue;
                                }
                                assert(!r.fault && r.completed == size && typed_hits == entries);
                                if (enabled && state && effective < 2)
                                    expected = cached_value(address, size);
                                if (enabled && state == 2 && effective >= 2) {
                                    for (unsigned i = 0; i < 16; ++i)
                                        expected_ram[i] = 0xe0 + i;
                                    expected = cached_value(address, size);
                                }
                                if (!writing)
                                    assert(r.value == expected);
                                if (writing && !(enabled && state && effective == 1))
                                    for (unsigned i = 0; i < size; ++i)
                                        expected_ram[offset + i] = 0x76543210u >> ((size - i - 1) * 8);
                                assert(!memcmp(expected_ram, ram + 0x10040, 16));
                                Syn68kCache040Line* line = resident(0, 0x10040);
                                assert(!!line == !!(state && !(enabled && effective >= 2)));
                                if (line) {
                                    unsigned dirty = state == 2 ? 3 : 0;
                                    for (unsigned i = 0; i < 16; ++i) {
                                        unsigned changed = enabled && writing && i >= offset && i < offset + size;
                                        unsigned expected_byte =
                                            changed ? (unsigned char)(0x76543210u >> ((size - (i - offset) - 1) * 8))
                                                    : 0xe0 + i;
                                        assert(line->bytes[i] == expected_byte);
                                        if (changed && effective == 1)
                                            dirty |= 1u << (i >> 2);
                                    }
                                    assert(line->dirty == dirty);
                                }
                                for (unsigned i = 0; i < entries; ++i) {
                                    assert(cycles[i].traffic ==
                                               (descriptor ? CACHE040_DESCRIPTOR : CACHE040_EXCEPTION) &&
                                           !cycles[i].locked && !cycles[i].lock_end);
                                    if (cycles[i].kind == CACHE040_OPERAND)
                                        assert(!cycles[i].burst && (cycles[i].attributes & 0x60) == effective << 5);
                                    else
                                        assert(cycles[i].kind == CACHE040_PUSH && !cycles[i].fc);
                                }
                                ++cases;
                            }
}

static void locked_policies(void)
{
    for (unsigned enabled = 0; enabled < 2; ++enabled)
        for (unsigned state = 0; state < 3; ++state)
            for (unsigned mode = 0; mode < 4; ++mode)
                for (unsigned writing = 0; writing < 2; ++writing)
                    for (unsigned fragment = 0; fragment < 2; ++fragment)
                        for (unsigned size = 1; size <= 4; ++size)
                            for (unsigned offset = 0; offset <= 12; ++offset) {
                                traffic_fixture();
                                install(0, 0x10040, state);
                                unsigned address = 0x10040 + offset;
                                unsigned expected = state == 2 ? cached_value(address, size) : value_at(address, size);
                                enum Syn68kCache040Traffic traffic =
                                    fragment ? CACHE040_LOCKED_CONTINUE : CACHE040_LOCKED;
                                Syn68kCache040Access r =
                                    syn68k_cache040_transfer(&cache, &cycle_bus, address, 5, size, writing, 0x76543210,
                                                             0x300 | (mode << 5), enabled ? 0x80000000 : 0, 0, traffic);
                                assert(!r.fault && r.completed == size && !resident(0, 0x10040) &&
                                       cache.bypass_data_miss == !!(!enabled && state) && typed_hits == entries &&
                                       entries);
                                if (!writing)
                                    assert(r.value == expected);
                                unsigned end = 0;
                                for (unsigned i = 0; i < entries; ++i) {
                                    assert(cycles[i].traffic == traffic && cycles[i].locked);
                                    if (cycles[i].kind == CACHE040_PUSH)
                                        assert(!cycles[i].fc && !cycles[i].lock_end);
                                    else {
                                        assert(cycles[i].kind == CACHE040_OPERAND && cycles[i].fc == 5 &&
                                               (cycles[i].attributes & 0x60) == 0x40 && !cycles[i].burst);
                                        if (cycles[i].lock_end) {
                                            assert(writing && !fragment && i == entries - 1);
                                            ++end;
                                        }
                                    }
                                }
                                assert(end == !!(writing && !fragment) && !cache.active &&
                                       cache.traffic == CACHE040_NORMAL);
                                ++cases;
                            }
}

static void disabled_hit_latch(void)
{
    for (unsigned state = 1; state <= 2; ++state)
        for (unsigned method = 0; method < 2; ++method) {
            traffic_fixture();
            install(0, 0x10040, state);
            Syn68kCache040Access r =
                method ? syn68k_cache040_maintain_traffic(&cache, &cycle_bus, 1, 1, 0x10040, 12, 1, 0, CACHE040_LOCKED)
                       : syn68k_cache040_transfer(&cache, &cycle_bus, 0x10040, 5, 4, 0, 0, 0, 0, 0, CACHE040_LOCKED);
            assert(!r.fault && cache.bypass_data_miss && !resident(0, 0x10040));
            /* Disabled reads, instruction accesses and data hits do not consume
               the one enabled nonlocked data-miss bypass. */
            assert(!syn68k_cache040_access(&cache, &cycle_bus, 0x11040, 5, 4, 0, 0, 0, 0, 0).fault);
            assert(cache.bypass_data_miss);
            assert(!syn68k_cache040_access(&cache, &cycle_bus, 0x11040, 6, 4, 0, 0, 0, 0x8000, 0).fault);
            assert(cache.bypass_data_miss);
            install(0, 0x10080, 1);
            assert(!read_at(0x10080, 5, 4, 0).fault && cache.bypass_data_miss);
            unsigned old_reads = reads;
            assert(!read_at(0x11040, 5, 4, 0).fault && !cache.bypass_data_miss && !resident(0, 0x11040));
            assert(reads == old_reads + 1);
            assert(!read_at(0x11040, 5, 4, 0).fault && resident(0, 0x11040) && reads == old_reads + 5);
            ++cases;
        }
}

static void fault_cycles(void)
{
    for (unsigned kind = 0; kind < 4; ++kind)
        for (unsigned failed = 0; failed < (kind == 0 || kind == 3 ? 4u : 2u); ++failed) {
            traffic_fixture();
            if (kind == 0 || kind == 3) {
                install(0, 0x10040, 2);
                write_failure = 0x10040 + failed * 4;
            }
            else if (kind == 1)
                read_failure = 0x10041 + failed;
            else
                write_failure = 0x10041 + failed;
            unsigned address = kind == 0 || kind == 3 ? 0x10040 : 0x10041;
            Syn68kCache040Access r =
                kind == 3
                    ? syn68k_cache040_maintain_traffic(&cache, &cycle_bus, 1, 1, address, 12, 1, 0, CACHE040_LOCKED)
                    : syn68k_cache040_transfer(&cache, &cycle_bus, address, 5, 3, kind == 2, 0x76543210, 0x20,
                                               0x80000000, 0, CACHE040_LOCKED);
            assert(r.fault == (kind == 0 || kind == 3 ? CACHE040_PUSH_FAULT : CACHE040_OPERAND_FAULT) &&
                   !memcmp(&r.cycle, &cycles[entries - 1], sizeof r.cycle) && r.cycle.locked &&
                   r.bus_address == log_entries[entries - 1].address);
            assert(!cache.active && cache.traffic == CACHE040_NORMAL);
            ++cases;
        }
}

int main(void)
{
    partial_chunks();
    special_policies();
    locked_policies();
    disabled_hit_latch();
    fault_cycles();
    printf("%u cache chunk/traffic/descriptor/locked-cycle/latch/fault cases passed\n", cases);
    return 0;
}
