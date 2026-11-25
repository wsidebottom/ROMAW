/* SPDX-License-Identifier: MIT */
/* Original cache-aware MMU operand/table-bus regressions, MC68040UM 3/4. */
#include "mmu040-memory.h"
#ifdef MMU040_CACHE_NATIVE
#include "native040.h"
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char ram[0x40000];
static Syn68kMMU040 mmu;
static Syn68kCache040 cache;
static uint32_t cacr;
static unsigned cases, reads, writes, table_reads, table_writes, locked_updates, attempted;
static uint32_t fail_read, fail_write, fail_table_read, fail_table_write;
static unsigned ram_limit;
static uint32_t mutate_mmusr;
static unsigned abort_table;
static Syn68kCache040Cycle last_cycle, cycle_log[128];
static unsigned cycle_count;

static uint32_t get(unsigned address, unsigned size)
{
    uint32_t value = 0;
    assert(address <= sizeof ram - size);
    for (unsigned i = 0; i < size; ++i)
        value = (value << 8) | ram[address + i];
    return value;
}

static void put(unsigned address, unsigned size, uint32_t value)
{
    assert(address <= sizeof ram - size);
    for (unsigned i = 0; i < size; ++i)
        ram[address + i] = value >> ((size - i - 1) * 8);
}

static int read_table(void* opaque, uint32_t address, uint32_t* value)
{
    assert(opaque == ram);
    ++table_reads;
    if (address == fail_table_read) {
        if (mutate_mmusr)
            mmu.mmusr = mutate_mmusr;
        return abort_table ? -1 : 0;
    }
    *value = get(address, 4);
    return 1;
}

static int update_table(void* opaque, uint32_t address, uint32_t value, int locked)
{
    assert(opaque == ram);
    ++table_writes;
    locked_updates += !!locked;
    if (address == fail_table_write) {
        if (mutate_mmusr)
            mmu.mmusr = mutate_mmusr;
        return abort_table ? -1 : 0;
    }
    put(address, 4, locked ? get(address, 4) | value : value);
    return 1;
}

static int read_cycle(void* opaque, uint32_t address, unsigned char* bytes, unsigned size,
                      const Syn68kCache040Cycle* cycle, unsigned* inhibit)
{
    assert(opaque == ram);
    ++reads;
    last_cycle = *cycle;
    assert(cycle_count < 128);
    cycle_log[cycle_count++] = *cycle;
    *inhibit = 0;
    if (address == fail_read)
        return 0;
    memcpy(bytes, ram + address, size);
    return 1;
}

static int write_cycle(void* opaque, uint32_t address, const unsigned char* bytes, unsigned size,
                       const Syn68kCache040Cycle* cycle)
{
    assert(opaque == ram);
    ++writes;
    last_cycle = *cycle;
    assert(cycle_count < 128);
    cycle_log[cycle_count++] = *cycle;
    if (address == fail_write) {
        if (mutate_mmusr)
            mmu.mmusr = mutate_mmusr;
        return 0;
    }
    memcpy(ram + address, bytes, size);
    return 1;
}

static Syn68kCache040Bus physical = {.opaque = ram, .read_cycle = read_cycle, .write_cycle = write_cycle};
static Syn68kMMU040Cache binding = {.state = &cache, .bus = &physical, .cacr = &cacr};
static Syn68kMMU040Native tracking = {.write_attempted = &attempted};
static Syn68kMMU040Memory memory = {
    .mmu = &mmu, .tables = {ram, read_table, update_table}, .native = &tracking, .cache = &binding};

static Syn68kMMU040Access access_memory(unsigned address, unsigned fc, unsigned size, int writing, int rmw,
                                        uint32_t* value)
{
#ifdef MMU040_CACHE_NATIVE
    Syn68kNative040MMU packet = {.memory = &memory,
                                 .address = address,
                                 .fc = fc,
                                 .size = size,
                                 .rmw = rmw,
                                 .value = *value,
                                 .ram = {ram, 0, ram_limit}};
    enum Syn68kNative040Operation op = writing ? (rmw ? NATIVE040_WRITE_RMW : NATIVE040_WRITE) : NATIVE040_READ;
    Syn68kNative040Entry entry = syn68k_native040_entry(op);
    assert(entry);
    entry(&packet);
    if (!writing && !packet.access.bus_error && packet.access.translation.fault == MMU040_OK)
        *value = packet.value;
    return packet.access;
#else
    return writing ? (rmw ? syn68k_mmu040_write_rmw(&memory, address, fc, size, *value)
                          : syn68k_mmu040_write(&memory, address, fc, size, *value))
                   : syn68k_mmu040_read(&memory, address, fc, size, rmw, value);
#endif
}

static Syn68kMMU040Access probe(unsigned address, int writing)
{
#ifdef MMU040_CACHE_NATIVE
    Syn68kNative040MMU packet = {
        .memory = &memory, .address = address, .fc = 5, .writing = writing, .ram = {ram, 0, ram_limit}};
    Syn68kNative040Entry entry = syn68k_native040_entry(NATIVE040_PTEST);
    assert(entry);
    entry(&packet);
    return packet.access;
#else
    return syn68k_mmu040_memory_translate(&memory, address, 5, writing, 1);
#endif
}

static void push_all(void)
{
#ifdef MMU040_CACHE_NATIVE
    Syn68kNative040Cache packet = {.cache = &cache,
                                   .bus = &physical,
                                   .caches = 1,
                                   .scope = 3,
                                   .page_shift = 12,
                                   .push = 1,
                                   .ram = {ram, 0, ram_limit}};
    Syn68kNative040Entry entry = syn68k_native040_entry(NATIVE040_CACHE_MAINTAIN);
    assert(entry);
    entry(&packet);
    assert(!packet.access.fault);
#else
    assert(!syn68k_cache040_maintain(&cache, &physical, 1, 3, 0, 12, 1).fault);
#endif
}

static void fixture(void)
{
    syn68k_mmu040_init(&mmu);
    syn68k_cache040_init(&cache);
    for (unsigned i = 0x8000; i < 0x9010; ++i)
        ram[i] = (i * 7 + (i >> 8) * 11) & 255;
    for (unsigned i = 0xc000; i < 0xc020; ++i)
        ram[i] = (i * 7 + (i >> 8) * 11) & 255;
    for (unsigned i = 0x3000; i < 0x3040; ++i)
        ram[i] = (i * 7 + (i >> 8) * 11) & 255;
    reads = writes = table_reads = table_writes = locked_updates = attempted = cycle_count = 0;
    fail_read = fail_write = fail_table_read = fail_table_write = UINT32_MAX;
    mutate_mmusr = abort_table = 0;
    cacr = 0x80008000;
    binding.traffic = CACHE040_NORMAL;
    memory.aligned_transfers = 0;
}

static Syn68kCache040Line* line_at(unsigned bank, unsigned address)
{
    for (unsigned way = 0; way < 4; ++way) {
        Syn68kCache040Line* line = &cache.lines[bank][(address >> 4) & 63][way];
        if (line->valid && line->address == (address & ~15u))
            return line;
    }
    return NULL;
}

static Syn68kCache040Line* install(unsigned address, unsigned dirty)
{
    Syn68kCache040Line* line = &cache.lines[0][(address >> 4) & 63][0];
    *line = (Syn68kCache040Line){.address = address & ~15u, .valid = 1, .dirty = dirty};
    memcpy(line->bytes, ram + line->address, 16);
    return line;
}

static void map_page(unsigned flags)
{
    mmu.tc = 0x8000;
    mmu.urp = mmu.srp = 0x1000;
    put(0x1000, 4, 0x200a);
    put(0x2000, 4, 0x300a);
    put(0x3010, 4, 0x8000 | flags);
}

static void operands(void)
{
    for (unsigned mode = 0; mode < 4; ++mode)
        for (unsigned enabled = 0; enabled < 2; ++enabled)
            for (unsigned fc = 1; fc <= 6; ++fc) {
                if (fc == 3 || fc == 4)
                    continue;
                for (unsigned width = 0; width < 3; ++width)
                    for (unsigned offset = 0; offset < 16; ++offset)
                        for (unsigned writing = 0; writing < 2; ++writing)
                            for (unsigned aligned = 0; aligned < 2; ++aligned) {
                                unsigned size = 1u << width;
                                fixture();
                                cacr = enabled ? 0x80008000 : 0;
                                mmu.itt[0] = mmu.dtt[0] = 0xc000 | mode << 5;
                                memory.aligned_transfers = aligned;
                                unsigned address = 0x8040 + offset;
                                uint32_t expected =
                                    writing ? 0x76543210u & (UINT32_MAX >> ((4 - size) * 8)) : get(address, size);
                                uint32_t value = writing ? expected : 0xdeadbeef;
                                Syn68kMMU040Access access = access_memory(address, fc, size, writing, 0, &value);
                                if (access.translation.fault || access.bus_error || access.cache.fault ||
                                    access.completed != size)
                                    fprintf(
                                        stderr,
                                        "operand mode=%u enable=%u fc=%u size=%u offset=%u write=%u aligned=%u: mmu=%u bus=%d cache=%u done=%u\n",
                                        mode, enabled, fc, size, offset, writing, aligned, access.translation.fault,
                                        access.bus_error, access.cache.fault, access.completed);
                                assert(access.translation.fault == MMU040_OK && !access.bus_error &&
                                       !access.cache.fault && access.completed == size);
                                assert(attempted == writing);
                                if (writing) {
                                    push_all();
                                    assert(get(address, size) == expected);
                                }
                                else {
                                    assert(value == expected);
                                    put(address, size, ~expected);
                                    unsigned before = reads;
                                    access = access_memory(address, fc, size, 0, 0, &value);
                                    assert(!access.bus_error && access.completed == size);
                                    assert(value == (enabled && mode < 2 ? expected : get(address, size)));
                                    if (enabled && mode < 2)
                                        assert(before == reads && line_at((fc & 3) == 2, address));
                                }
                                ++cases;
                            }
            }
}

static void page_chunks(void)
{
    /* Logical operands remain byte/word/longword. The odd longword at 4ffd
       reaches the cache as a three-byte tail twice, then one byte on the
       second page (MC68040UM 4.1). */
    for (unsigned offset = 0xffd; offset <= 0xfff; ++offset)
        for (unsigned writing = 0; writing < 2; ++writing)
            for (unsigned aligned = 0; aligned < 2; ++aligned) {
                fixture();
                map_page(0x39);
                put(0x3014, 4, 0xc039);
                memory.aligned_transfers = aligned;
                assert(!probe(0x4000, writing).translation.fault);
                assert(!probe(0x5000, writing).translation.fault);
                cache.replacement[0] = 0;
                unsigned first = 0x1000 - offset;
                uint32_t expected =
                    writing ? 0xaabbccddu : (get(0x8000 + offset, first) << ((4 - first) * 8)) | get(0xc000, 4 - first);
                uint32_t value = writing ? expected : 0;
                Syn68kMMU040Access access = access_memory(0x4000 + offset, 5, 4, writing, 0, &value);
                assert(!access.translation.fault && !access.bus_error && !access.cache.fault && access.completed == 4);
                if (cache.replacement[0] != 3)
                    fprintf(stderr, "page offset=%x write=%u aligned=%u counter=%u\n", offset, writing, aligned,
                            cache.replacement[0]);
                assert(cache.replacement[0] == 3);
                if (writing) {
                    push_all();
                    assert(get(0x8000 + offset, first) == expected >> ((4 - first) * 8));
                    assert(get(0xc000, 4 - first) == (expected & (UINT32_MAX >> (first * 8))));
                }
                else
                    assert(value == expected);
                ++cases;
            }
}

static void descriptors(void)
{
    for (unsigned writing = 0; writing < 2; ++writing)
        for (unsigned dirty = 0; dirty < 2; ++dirty)
            for (unsigned used = 0; used < 2; ++used) {
                fixture();
                unsigned flags = 1 | (used ? 8 : 0);
                map_page(flags);
                Syn68kCache040Line* line = install(0x3010, dirty);
                /* The cached descriptor is authoritative until locked update
                   preparation pushes/invalidates it; raw RAM can be stale. */
                put(0x3010, 4, 0x9000 | flags);
                if (!dirty)
                    put(0x3010, 4, 0x8000 | flags);
                uint32_t value = writing ? 0x12345678 : 0;
                Syn68kMMU040Access access = access_memory(0x4000, 5, 4, writing, 0, &value);
                assert(!access.bus_error && !access.translation.fault && !access.cache.fault);
                assert(access.translation.physical == 0x8000);
                unsigned result = 0x8000 | flags | 8 | (writing ? 16 : 0);
                if (writing || !used) {
                    assert(get(0x3010, 4) == result);
                    if (!writing) {
                        assert(!line->valid);
                        if (!ram_limit)
                            assert(locked_updates == 1);
                    }
                    else {
                        assert(line->valid && line->bytes[3] == (result & 255));
                        if (!ram_limit)
                            assert(!locked_updates);
                    }
                }
                else
                    assert(line->valid);
                assert(!line_at(0, 0x1000) && !line_at(0, 0x2000));
                ++cases;
            }
}

static void locked_chunks(void)
{
    for (unsigned offset = 0xffd; offset <= 0xfff; ++offset)
        for (unsigned enabled = 0; enabled < 2; ++enabled) {
            fixture();
            map_page(0x19);
            put(0x3014, 4, 0xc019);
            cacr = enabled ? 0x80008000 : 0;
            install(0x8ff0, 8);
            install(0xc000, 1);
            unsigned first = 0x1000 - offset;
            uint32_t expected = get(0x8000 + offset, first) << ((4 - first) * 8) | get(0xc000, 4 - first);
            uint32_t value = 0;
            Syn68kMMU040Access access = access_memory(0x4000 + offset, 5, 4, 0, 1, &value);
            assert(!access.translation.fault && !access.bus_error && value == expected);
            assert(!line_at(0, 0x8ff0) && !line_at(0, 0xc000));
            for (unsigned i = 0; i < cycle_count; ++i)
                assert(cycle_log[i].locked && !cycle_log[i].lock_end);
            cycle_count = 0;
            value = 0x98765432;
            access = access_memory(0x4000 + offset, 5, 4, 1, 1, &value);
            assert(!access.translation.fault && !access.bus_error && access.completed == 4);
            assert(get(0x8000 + offset, first) == value >> ((4 - first) * 8));
            assert(get(0xc000, 4 - first) == (value & (UINT32_MAX >> (first * 8))));
            if (!ram_limit) {
                unsigned ends = 0;
                for (unsigned i = 0; i < cycle_count; ++i) {
                    assert(cycle_log[i].locked && cycle_log[i].kind == CACHE040_OPERAND);
                    ends += cycle_log[i].lock_end;
                    if (i + 1 != cycle_count)
                        assert(!cycle_log[i].lock_end);
                }
                assert(ends == 1 && cycle_log[cycle_count - 1].lock_end);
            }
            ++cases;
        }
    fixture();
    binding.traffic = CACHE040_LOCKED_CONTINUE;
    uint32_t value = 0x12345678;
    assert(!access_memory(0x8040, 5, 4, 1, 1, &value).bus_error);
    binding.traffic = CACHE040_NORMAL;
    assert(!access_memory(0x8044, 5, 4, 1, 1, &value).bus_error);
    if (!ram_limit)
        assert(cycle_count == 2 && !cycle_log[0].lock_end && cycle_log[1].lock_end);
    ++cases;
}

static void physical_retry(void)
{
    if (ram_limit)
        return;
    fixture();
    map_page(1);
    install(0x3010, 1);
    fail_write = 0x3010;
    uint32_t value = 0xdeadbeef;
    Syn68kMMU040Access access = access_memory(0x4000, 5, 4, 0, 0, &value);
    assert(access.cache_exception && access.cache.fault == CACHE040_PUSH_FAULT && value == 0xdeadbeef);
    /* Guest cleanup writes the captured physical push. A physical abort must
       not leave a negative logical ATC entry that invents a later table fault. */
    memcpy(ram + access.cache.push_address, access.cache.push_data + (access.cache.push_address & 15),
           access.cache.push_size);
    fail_write = UINT32_MAX;
    access = access_memory(0x4000, 5, 4, 0, 0, &value);
    assert(!access.translation.fault && !access.bus_error && !access.cache.fault && access.completed == 4 &&
           value == get(0x8000, 4));
    ++cases;
}

static void physical_table_aborts(void)
{
    if (ram_limit == sizeof ram)
        return;
    for (unsigned cached = 0; cached < 2; ++cached)
        for (unsigned writing = 0; writing < 2; ++writing) {
            fixture();
            map_page(1);
            memory.cache = cached ? &binding : NULL;
            if (writing)
                fail_table_write = 0x3010;
            else
                fail_table_read = 0x1000;
            abort_table = 1;
            mutate_mmusr = 0xcafebabe;
            mmu.mmusr = 0x12345678;
            Syn68kMMU040Access access = probe(0x4000, writing);
            assert(access.translation.fault == MMU040_PHYSICAL_ABORT && access.cache_exception &&
                   mmu.mmusr == 0xcafebabe);
            assert(access.cache_table == cached);
            for (unsigned way = 0; way < 4; ++way)
                assert(!mmu.atc[0][4][way].valid);
            ++cases;
        }
    memory.cache = &binding;
}

static void faults(void)
{
    fixture();
    map_page(1);
    install(0x3010, 1);
    cacr = 0;
    mmu.mmusr = 0x12345678;
    Syn68kMMU040Access access = probe(0x4000, 0);
    assert(access.translation.fault == MMU040_PHYSICAL_ABORT && access.cache_table && access.cache_exception &&
           access.cache.fault == CACHE040_DESCRIPTOR_LOCKUP && mmu.mmusr == 0x12345678);
    assert(!table_writes && !writes);
    for (unsigned way = 0; way < 4; ++way)
        assert(!mmu.atc[0][4][way].valid);
    ++cases;
    if (ram_limit)
        return;
    fixture();
    map_page(1);
    install(0x3010, 1);
    fail_write = 0x3010;
    mmu.mmusr = 0x12345678;
    access = probe(0x4000, 0);
    assert(access.cache_table && access.cache_exception && access.cache.fault == CACHE040_PUSH_FAULT &&
           access.cache.push_address == 0x3010 && access.cache.push_size == 4 &&
           access.cache.cycle.kind == CACHE040_PUSH && access.cache.cycle.locked && !access.cache.cycle.lock_end);
    assert(mmu.mmusr == 0x12345678 && !table_writes && !line_at(0, 0x3010));
    ++cases;
    fixture();
    map_page(1);
    install(0x3010, 1);
    fail_write = 0x3010;
    mutate_mmusr = 0xcafebabe;
    mmu.mmusr = 0x12345678;
    access = probe(0x4000, 0);
    assert(access.cache_exception && access.cache.fault == CACHE040_PUSH_FAULT && mmu.mmusr == 0xcafebabe);
    ++cases;
    fixture();
    map_page(1);
    fail_table_read = 0x1000;
    access = probe(0x4000, 0);
    assert(access.cache_table && !access.cache_exception && access.cache.fault == CACHE040_OPERAND_FAULT &&
           access.cache.bus_address == 0x1000 && mmu.mmusr == 0x800);
    ++cases;
    fixture();
    map_page(1);
    install(0x3010, 1);
    fail_table_write = 0x3010;
    access = probe(0x4000, 0);
    assert(access.cache_table && !access.cache_exception && access.cache.fault == CACHE040_OPERAND_FAULT &&
           access.cache.cycle.traffic == CACHE040_LOCKED && access.cache.cycle.lock_end && mmu.mmusr == 0x800);
    assert(get(0x3010, 4) == 0x8001 && !line_at(0, 0x3010));
    ++cases;
    fixture();
    map_page(1);
    put(0x1000, 4, 0x2002);
    fail_table_read = 0x2000;
    fail_table_write = 0x1000;
    access = probe(0x4000, 0);
    assert(access.cache.bus_address == 0x2000 && access.translation.descriptor_address == 0x2000 && table_writes == 1 &&
           !access.cache_exception);
    ++cases;
    fixture();
    map_page(9);
    Syn68kCache040Line* line = install(0x3010, 0);
    fail_table_write = 0x3010;
    access = probe(0x4000, 1);
    assert(access.cache.fault == CACHE040_OPERAND_FAULT && !access.cache_exception && access.cache_table &&
           line->bytes[3] == 0x19 && get(0x3010, 4) == 0x8009 && mmu.mmusr == 0x800);
    ++cases;
}

int main(void)
{
#ifdef MMU040_CACHE_NATIVE
    unsigned modes = 2;
#else
    unsigned modes = 1;
#endif
    for (unsigned mode = 0; mode < modes; ++mode) {
        ram_limit = mode ? sizeof ram : 0;
        operands();
        page_chunks();
        descriptors();
        locked_chunks();
        physical_retry();
        physical_table_aborts();
        faults();
    }
#ifdef MMU040_CACHE_NATIVE
    /* Bind a real permanent window that excludes the root descriptor. The
       native RAM adapter must preserve a negative fallback callback status. */
    ram_limit = 0x800;
    physical_table_aborts();
#endif
    printf("%u cache-aware MMU operand/descriptor/atomic/fault cases passed\n", cases);
    return 0;
}
