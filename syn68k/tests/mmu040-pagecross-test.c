/* SPDX-License-Identifier: MIT */
/* MC68040UM 4.1 first-page replay: literal bus sequences and changing data. */
#include "native040.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#if defined(MMU040_PAGECROSS_NATIVE) && !defined(MMU040_PAGECROSS_GROUPED)
static unsigned native_active;
Syn68kMMU040Access __real_syn68k_mmu040_memory_translate(Syn68kMMU040Memory*, uint32_t, unsigned, int, int);

Syn68kMMU040Access __wrap_syn68k_mmu040_memory_translate(Syn68kMMU040Memory* m, uint32_t a, unsigned f, int w, int p)
{
    assert(!native_active);
    return __real_syn68k_mmu040_memory_translate(m, a, f, w, p);
}

Syn68kCache040Access __real_syn68k_cache040_transfer(Syn68kCache040*, const Syn68kCache040Bus*, uint32_t, unsigned,
                                                     unsigned, int, uint32_t, uint32_t, uint32_t, int,
                                                     enum Syn68kCache040Traffic);

Syn68kCache040Access __wrap_syn68k_cache040_transfer(Syn68kCache040* c, const Syn68kCache040Bus* b, uint32_t a,
                                                     unsigned f, unsigned n, int w, uint32_t v, uint32_t attr,
                                                     uint32_t cacr, int noalloc, enum Syn68kCache040Traffic t)
{
    assert(!native_active);
    return __real_syn68k_cache040_transfer(c, b, a, f, n, w, v, attr, cacr, noalloc, t);
}
#endif

static unsigned char tables[0x4000];
static unsigned count, fail_at, cases;

static struct {
    uint32_t address, value;
    unsigned size, writing, fc;
} seen[8];

static uint32_t get(unsigned a)
{
    assert(a <= sizeof tables - 4);
    return (uint32_t)tables[a] << 24 | (uint32_t)tables[a + 1] << 16 | (uint32_t)tables[a + 2] << 8 | tables[a + 3];
}

static void put(unsigned a, uint32_t v)
{
    assert(a <= sizeof tables - 4);
    for (unsigned i = 0; i < 4; ++i)
        tables[a + i] = v >> (24 - i * 8);
}

static int table_read(void* p, uint32_t a, uint32_t* v)
{
    (void)p;
    *v = get(a);
    return 1;
}

static int table_update(void* p, uint32_t a, uint32_t v, int locked)
{
    (void)p;
    put(a, locked ? get(a) | v : v);
    return 1;
}

static int read_bytes(void* p, uint32_t a, unsigned char* b, unsigned n, uint32_t attr, unsigned fc)
{
    (void)p;
    (void)attr;
    assert(count < 8);
    unsigned slot = count++;
    seen[slot].address = a;
    seen[slot].size = n;
    seen[slot].writing = 0;
    seen[slot].fc = fc;
    if (count == fail_at)
        return 0;
    for (unsigned i = 0; i < n; ++i)
        b[i] = 0x20 + count * 8 + i;
    return 1;
}

static int write_bytes(void* p, uint32_t a, const unsigned char* b, unsigned n, uint32_t attr, unsigned fc)
{
    (void)p;
    (void)attr;
    assert(count < 8);
    unsigned slot = count++;
    seen[slot].address = a;
    seen[slot].size = n;
    seen[slot].writing = 1;
    seen[slot].fc = fc;
    seen[slot].value = 0;
    for (unsigned i = 0; i < n; ++i)
        seen[slot].value = seen[slot].value << 8 | b[i];
    return count != fail_at;
}

typedef struct {
    unsigned offset, size, pass;
} Beat;

/* An odd long's three-byte first page is byte/word, repeated byte/word.
   These literal sequences do not invoke the implementation's splitter. */
static const Beat word1[] = {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}};
static const Beat long1[] = {{0, 1, 0}, {0, 1, 1}, {1, 2, 1}, {3, 1, 1}};
static const Beat long2[] = {{0, 2, 0}, {0, 2, 1}, {2, 2, 1}};
static const Beat long3[] = {{0, 1, 0}, {1, 2, 0}, {0, 1, 1}, {1, 2, 1}, {3, 1, 1}};

static void matrix(unsigned native_mode)
{
#if defined(MMU040_PAGECROSS_NATIVE) && !defined(MMU040_PAGECROSS_GROUPED)
    native_active = native_mode;
#endif
    for (unsigned shift = 12; shift <= 13; ++shift)
        for (unsigned topology = 0; topology < 4; ++topology)
            for (unsigned bound = 0; bound < 4; ++bound) {
                if (topology == 2 && bound == 3)
                    continue;
                for (unsigned size = 2; size <= 4; size *= 2)
                    for (unsigned prefix = 1; prefix < size; ++prefix) {
                        const Beat* list = size == 2 ? word1 : prefix == 1 ? long1 : prefix == 2 ? long2 : long3;
                        unsigned length = size == 2 ? 3 : prefix == 1 ? 4 : prefix == 2 ? 3 : 5;
                        Beat raw[] = {{0, prefix, 0}, {0, prefix, 1}, {prefix, size - prefix, 1}};
                        if (!bound)
                            list = raw, length = 3;
                        for (unsigned writing = 0; writing < 2; ++writing)
                            for (unsigned rmw = 0; rmw < 2; ++rmw)
                                for (unsigned user = 0; user < 2; ++user)
                                    for (unsigned fault = 0; fault < 3; ++fault) {
                                        if (fault && topology)
                                            continue;
                                        for (unsigned fail = 0; fail <= (fault ? 0 : length); ++fail) {
                                            unsigned page = 1u << shift, fc = user ? 1 : 5;
                                            uint32_t logical = topology == 3 ? 0u - prefix : page - prefix;
                                            uint32_t first = topology == 0 ? 0xa000 + page - prefix : logical;
                                            uint32_t second = topology == 0 ? 0xe000 : topology == 3 ? 0 : page;
                                            Syn68kMMU040 mmu;
                                            syn68k_mmu040_init(&mmu);
                                            mmu.tc = (shift == 13 ? 0x4000 : 0) | (topology == 2 ? 0 : 0x8000);
                                            mmu.srp = mmu.urp = 0x1000;
                                            memset(tables, 0, sizeof tables);
                                            put(0x1000, 0x2002);
                                            put(0x2000, 0x3002);
                                            put(0x3000, 0xa059);
                                            put(0x3004, fault == 1 ? 0 : fault == 2 ? 0xe05d : 0xe059);
                                            if (topology == 1 || topology == 3)
                                                mmu.dtt[0] = 0xc040, mmu.dtt[1] = 0xff00c040;
                                            Syn68kMMU040Memory memory = {.mmu = &mmu,
                                                                         .tables = {0, table_read, table_update},
                                                                         .read = read_bytes,
                                                                         .write = write_bytes,
                                                                         .aligned_transfers = bound == 1};
                                            uint32_t cacr = bound == 3 ? 0x80000000 : 0;
                                            Syn68kCache040 cache;
                                            syn68k_cache040_init(&cache);
                                            Syn68kMMU040Cache binding = {.state = &cache, .cacr = &cacr};
                                            if (bound >= 2)
                                                memory.cache = &binding;
#ifdef MMU040_PAGECROSS_NATIVE
                                            Syn68kMMU040Native native;
                                            if (native_mode)
                                                assert(
                                                    syn68k_native040_memory(&memory, &native, (Syn68kNative040RAM){0}));
#else
                                            assert(!native_mode);
#endif
                                            count = 0;
                                            fail_at = fail;
                                            memset(seen, 0, sizeof seen);
                                            uint32_t value = 0xdeadbeef;
                                            Syn68kMMU040Access result =
                                                writing
                                                    ? (rmw ? syn68k_mmu040_write_rmw(&memory, logical, fc, size, value)
                                                           : syn68k_mmu040_write(&memory, logical, fc, size, value))
                                                    : syn68k_mmu040_read(&memory, logical, fc, size, rmw, &value);
                                            int atc = fault == 1 || (fault == 2 && (writing || rmw));
                                            unsigned first_replay = prefix == 3 && bound ? 2 : 1;
                                            unsigned attempts = fail ? fail : atc ? first_replay * 2 : length;
                                            assert(count == attempts);
                                            for (unsigned i = 0; i < count; ++i) {
                                                const Beat* b = &list[i];
                                                uint32_t address = b->offset < prefix ? first + b->offset
                                                                                      : second + b->offset - prefix;
                                                assert(seen[i].address == address && seen[i].size == b->size &&
                                                       seen[i].writing == writing && seen[i].fc == fc);
                                                if (writing) {
                                                    uint32_t expected = 0;
                                                    for (unsigned j = 0; j < b->size; ++j)
                                                        expected =
                                                            expected << 8 |
                                                            ((0xdeadbeefu >> ((size - 1 - b->offset - j) * 8)) & 255);
                                                    assert(seen[i].value == expected);
                                                }
                                            }
                                            unsigned completed = fail ? list[fail - 1].offset : atc ? prefix : size;
                                            assert(result.request == logical &&
                                                   result.logical ==
                                                       (uint32_t)(logical + (fail || atc  ? completed
                                                                             : bound == 1 ? list[length - 1].offset
                                                                                          : prefix)));
                                            assert(result.completed == completed && result.size == size &&
                                                   result.fc == fc && result.writing == writing && result.rmw == rmw);
                                            assert(result.bus_error == !!fail &&
                                                   result.translation.fault ==
                                                       (atc ? (fault == 1 ? MMU040_INVALID : MMU040_WRITE_PROTECTED)
                                                            : MMU040_OK));
                                            if (writing)
                                                assert(result.value == (size == 2 ? 0xbeef : 0xdeadbeef));
                                            else {
                                                if (fail || atc) {
                                                    assert(value == 0xdeadbeef);
                                                    unsigned begin = fail && !list[fail - 1].pass ? 0 : first_replay;
                                                    unsigned end = fail ? fail - 1 : first_replay * 2;
                                                    uint32_t expected = 0;
                                                    for (unsigned i = begin; i < end; ++i)
                                                        for (unsigned j = 0; j < list[i].size; ++j)
                                                            expected = expected << 8 | (0x20 + (i + 1) * 8 + j);
                                                    assert(result.value == expected);
                                                }
                                                else {
                                                    uint32_t expected = 0;
                                                    for (unsigned i = first_replay; i < length; ++i)
                                                        for (unsigned j = 0; j < list[i].size; ++j)
                                                            expected = expected << 8 | (0x20 + (i + 1) * 8 + j);
                                                    assert(value == expected && result.value == expected);
                                                }
                                            }
                                            if (fail || atc) {
                                                uint16_t ssw;
                                                assert(syn68k_mmu040_access_ssw(&result, &ssw));
                                                assert(ssw == (fc | (size == 2 ? 0x40 : 0) | (!writing ? 0x100 : 0) |
                                                               (rmw ? 0x200 : 0) | (atc ? 0xc00 : 0)));
                                            }
                                            ++cases;
                                        }
                                    }
                    }
            }
}

/* A logical long extension at the last word of a code page is two stream
   chunks, not a misaligned data-unit operand. Physical half-line prefetch
   sequencing is a separate boundary (MC68040UM 7.3/7.4.2). */
static void instruction_stream(unsigned native_mode)
{
#if defined(MMU040_PAGECROSS_NATIVE) && !defined(MMU040_PAGECROSS_GROUPED)
    native_active = native_mode;
#endif
    for (unsigned shift = 12; shift <= 13; ++shift)
        for (unsigned topology = 0; topology < 4; ++topology)
            for (unsigned bound = 0; bound < 4; ++bound) {
                if (topology == 2 && bound == 3)
                    continue;
                for (unsigned fc = 2; fc <= 6; fc += 4)
                    for (unsigned fail = 0; fail <= 2; ++fail) {
                        unsigned page = 1u << shift;
                        uint32_t logical = topology == 3 ? 0u - 2 : page - 2;
                        uint32_t first = topology == 0 ? 0xa000 + page - 2 : logical;
                        uint32_t second = topology == 0 ? 0xe000 : topology == 3 ? 0 : page;
                        Syn68kMMU040 mmu;
                        syn68k_mmu040_init(&mmu);
                        mmu.tc = (shift == 13 ? 0x4000 : 0) | (topology == 2 ? 0 : 0x8000);
                        mmu.srp = mmu.urp = 0x1000;
                        memset(tables, 0, sizeof tables);
                        put(0x1000, 0x2002);
                        put(0x2000, 0x3002);
                        put(0x3000, 0xa059);
                        put(0x3004, 0xe059);
                        if (topology == 1 || topology == 3)
                            mmu.itt[0] = 0xc040, mmu.itt[1] = 0xff00c040;
                        Syn68kMMU040Memory memory = {.mmu = &mmu,
                                                     .tables = {0, table_read, table_update},
                                                     .read = read_bytes,
                                                     .write = write_bytes,
                                                     .aligned_transfers = bound == 1};
                        uint32_t cacr = bound == 3 ? 0x8000 : 0;
                        Syn68kCache040 cache;
                        syn68k_cache040_init(&cache);
                        Syn68kMMU040Cache binding = {.state = &cache, .cacr = &cacr};
                        if (bound >= 2)
                            memory.cache = &binding;
#ifdef MMU040_PAGECROSS_NATIVE
                        Syn68kMMU040Native native;
                        if (native_mode)
                            assert(syn68k_native040_memory(&memory, &native, (Syn68kNative040RAM){0}));
#else
                        assert(!native_mode);
#endif
                        count = 0;
                        fail_at = fail;
                        uint32_t value = 0xdeadbeef;
                        Syn68kMMU040Access r = syn68k_mmu040_read(&memory, logical, fc, 4, 0, &value);
                        assert(count == (fail ? fail : 2));
                        assert(seen[0].address == first && seen[0].size == 2 && seen[0].fc == fc && !seen[0].writing);
                        if (count == 2)
                            assert(seen[1].address == second && seen[1].size == 2 && seen[1].fc == fc &&
                                   !seen[1].writing);
                        assert(!r.translation.fault && r.bus_error == !!fail &&
                               r.completed == (fail ? (fail - 1) * 2 : 4));
                        assert(r.request == logical && r.size == 4 && r.fc == fc && !r.writing && !r.rmw);
                        assert(fail ? value == 0xdeadbeef : value == 0x28293031 && r.value == value);
                        ++cases;
                    }
            }
}

int main(void)
{
#ifndef MMU040_PAGECROSS_GROUPED
    matrix(0);
    instruction_stream(0);
#endif
#ifdef MMU040_PAGECROSS_NATIVE
    matrix(1);
    instruction_stream(1);
#endif
    printf("%u page-cross replay/volatile-data/bus-fault/ATC/wrap cases passed\n", cases);
    return 0;
}
