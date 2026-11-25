/* SPDX-License-Identifier: MIT */
/* Physical cache selection and interruptible maintenance through MMU/native bindings. */
#define main cache_operand_matrix_unused
#include "mmu040-cache-test.c"
#undef main

static volatile uint8_t pending[8];
static uint16_t interrupt_sr;
static unsigned inject_after, inject_level;

static int interrupt_write(void* opaque, uint32_t address, const unsigned char* bytes, unsigned size,
                           const Syn68kCache040Cycle* cycle)
{
    int ok = write_cycle(opaque, address, bytes, size, cycle);
    if (writes == inject_after)
        pending[inject_level] = 1;
    return ok;
}

static Syn68kMMU040Access maintain(unsigned caches, unsigned scope, unsigned address, int push)
{
#ifdef MMU040_CACHE_NATIVE
    Syn68kNative040MMU packet = {.memory = &memory,
                                 .all = caches,
                                 .size = scope,
                                 .address = address,
                                 .writing = push,
                                 .ram = {ram, 0, ram_limit}};
    Syn68kNative040Entry entry = syn68k_native040_entry(NATIVE040_MEMORY_CACHE_MAINTAIN);
    assert(entry);
    entry(&packet);
    return packet.access;
#else
    return syn68k_mmu040_cache_maintain(&memory, caches, scope, address, push);
#endif
}

static void selection(void)
{
    const unsigned addresses[] = {0x8040, 0x8ff0, 0x9000, 0xc000};
    const unsigned targets[] = {0x8047, 0x8fff, 0xdeadbeef};
    const unsigned dirty_masks[] = {0, 1, 5, 15};
    for (unsigned page = 12; page <= 13; ++page)
        for (unsigned enabled = 0; enabled < 2; ++enabled)
            for (unsigned which = 0; which < 4; ++which)
                for (unsigned scope = 0; scope < 4; ++scope)
                    for (unsigned push = 0; push < 2; ++push)
                        for (unsigned dirty = 0; dirty < 4; ++dirty)
                            for (unsigned target = 0; target < 3; ++target) {
                                fixture();
                                binding.interrupt = (Syn68kCache040Interrupt){0};
                                cacr = enabled ? 0x80008000 : 0;
                                mmu.tc = page == 13 ? 0x4000 : 0;
                                Syn68kCache040Line* lines[2][4];
                                for (unsigned bank = 0; bank < 2; ++bank)
                                    for (unsigned i = 0; i < 4; ++i) {
                                        unsigned set = (addresses[i] >> 4) & 63, way = 0;
                                        while (cache.lines[bank][set][way].valid)
                                            ++way;
                                        Syn68kCache040Line* line = &cache.lines[bank][set][way];
                                        *line = (Syn68kCache040Line){.address = addresses[i],
                                                                     .valid = 1,
                                                                     .dirty = bank ? 0 : dirty_masks[dirty]};
                                        for (unsigned word = 0; word < 4; ++word) {
                                            uint32_t value = 0x10203040u + bank * 0x04040404u + i * 0x01010101u + word;
                                            for (unsigned byte = 0; byte < 4; ++byte)
                                                line->bytes[word * 4 + byte] = value >> (24 - byte * 8);
                                        }
                                        lines[bank][i] = line;
                                    }
                                unsigned char before[4][16];
                                for (unsigned i = 0; i < 4; ++i)
                                    memcpy(before[i], ram + addresses[i], 16);
                                Syn68kMMU040Access access = maintain(which, scope, targets[target], push);
                                assert(access.translation.fault == (scope ? MMU040_OK : MMU040_BAD_REQUEST));
                                assert(!access.bus_error && !access.cache_exception && !access.cache.interrupt_level);
                                assert(!table_reads && !table_writes);
                                for (unsigned bank = 0; bank < 2; ++bank)
                                    for (unsigned i = 0; i < 4; ++i) {
                                        unsigned match =
                                            scope == 3 ||
                                            (scope == 1 && (addresses[i] >> 4) == (targets[target] >> 4)) ||
                                            (scope == 2 && (addresses[i] >> page) == (targets[target] >> page));
                                        unsigned selected = scope && (which & (1u << bank)) && match;
                                        assert(lines[bank][i]->valid == !selected);
                                        if (!bank)
                                            for (unsigned word = 0; word < 4; ++word) {
                                                unsigned written = selected && push && dirty_masks[dirty] &&
                                                                   (dirty_masks[dirty] != 1 || word == 0);
                                                uint32_t expected = written
                                                                        ? 0x10203040u + i * 0x01010101u + word
                                                                        : (uint32_t)before[i][word * 4] << 24 |
                                                                              (uint32_t)before[i][word * 4 + 1] << 16 |
                                                                              (uint32_t)before[i][word * 4 + 2] << 8 |
                                                                              before[i][word * 4 + 3];
                                                assert(get(addresses[i] + word * 4, 4) == expected);
                                            }
                                    }
                                ++cases;
                            }
}

static void interrupts(void)
{
    for (unsigned level = 1; level <= 7; ++level)
        for (unsigned mask = 0; mask <= 7; ++mask)
            for (unsigned after = 0; after < 2; ++after) {
                if (ram_limit && after)
                    continue;
                fixture();
                memset((void*)pending, 0, sizeof pending);
                interrupt_sr = mask << 8;
                binding.interrupt = (Syn68kCache040Interrupt){pending, &interrupt_sr};
                physical.write_cycle = interrupt_write;
                install(0x8040, 1);
                install(0x8050, 1);
                inject_after = after ? 1 : 99;
                inject_level = level;
                if (!after)
                    pending[level] = 1;
                Syn68kMMU040Access access = maintain(1, 3, 0, 1);
                unsigned eligible = level == 7 || level > mask;
                assert(!access.translation.fault && !access.bus_error && !access.cache_exception);
                assert(access.cache.interrupt_level == (eligible ? level : 0));
                assert(!!line_at(0, 0x8040) == !!(eligible && !after));
                assert(!!line_at(0, 0x8050) == !!eligible);
                assert(pending[level] == 1 && !cache.active && cache.traffic == CACHE040_NORMAL);
                ++cases;
            }
    binding.interrupt = (Syn68kCache040Interrupt){0};
    physical.write_cycle = write_cycle;
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
        selection();
        interrupts();
    }
    printf("%u cache maintenance selection/interrupt/native-RAM cases passed\n", cases);
    return 0;
}
