/* SPDX-License-Identifier: MIT */
#define CACHE040_NATIVE_TEST
#define main cache040_matrix_unused
#include "cache040-test.c"
#undef main

int main(void)
{
    Syn68kNative040Entry access = syn68k_native040_entry(NATIVE040_CACHE_ACCESS);
    Syn68kNative040Entry maintain = syn68k_native040_entry(NATIVE040_CACHE_MAINTAIN);
    assert(access && maintain);
    for (unsigned mode = 0; mode < 3; ++mode)
        for (unsigned size = 1; size <= 4; ++size)
            for (unsigned offset = 0; offset < 32; ++offset) {
                fixture();
                unsigned address = 0x10040 + offset, old = value_at(address, size);
                uint32_t mask = size == 4 ? UINT32_MAX : (1u << (size * 8)) - 1;
                Syn68kNative040Cache f = {.cache = &cache,
                                          .bus = &bus,
                                          .ram = {ram + 0x10000, 0x10000, 0x1000},
                                          .address = address,
                                          .size = size,
                                          .fc = 5,
                                          .attributes = mode == 2 ? 0x20 : 0,
                                          .cacr = 0x80008000,
                                          .writing = !!mode,
                                          .value = 0x76543210,
                                          .caches = 3,
                                          .scope = 3,
                                          .page_shift = 12,
                                          .push = 1};
                access(&f);
                assert(!f.access.fault && f.access.completed == size && !reads && !writes);
                if (!mode)
                    assert(f.access.value == old);
                else if (mode == 1)
                    assert(value_at(address, size) == (0x76543210 & mask));
                else
                    assert(value_at(address, size) == old);
                maintain(&f);
                assert(!f.access.fault && !reads && !writes);
                if (mode)
                    assert(value_at(address, size) == (0x76543210 & mask));
                ++cases;
            }
    const unsigned alternate[] = {0, 3, 4, 7};
    for (unsigned i = 0; i < 4; ++i)
        for (unsigned writing = 0; writing < 2; ++writing) {
            fixture();
            Syn68kNative040Cache f = {.cache = &cache,
                                      .bus = &bus,
                                      .ram = {ram + 0x10000, 0x10000, 0x1000},
                                      .address = 0x10040,
                                      .size = 4,
                                      .fc = alternate[i],
                                      .attributes = 0x20,
                                      .cacr = 0x80008000,
                                      .writing = writing,
                                      .value = 0x76543210};
            access(&f);
            assert(!f.access.fault && (writing ? writes == 1 && !reads : reads == 1 && !writes));
            assert(!resident(0, 0x10040) && !resident(1, 0x10040));
            ++cases;
        }
    for (unsigned bad = 0; bad < 3; ++bad) {
        fixture();
        Syn68kNative040Cache f = {.cache = &cache,
                                  .bus = &bus,
                                  .ram = {ram + 0x10000, 0x10000, 0x1000},
                                  .address = 0x10040,
                                  .size = 4,
                                  .fc = 5,
                                  .cacr = 0x80000000};
        if (!bad)
            f.ram.size = 0;
        else if (bad == 1)
            f.ram = (Syn68kNative040RAM){(unsigned char*)1, 0xfffffff0, 32};
        else
            f.ram.size = 0x40;
        access(&f);
        assert(!f.access.fault && f.access.value == value_at(0x10040, 4) && reads == 4 && !writes);
        ++cases;
    }
    printf("%u native cache RAM/push/alternate-space/bounds cases passed\n", cases);
    return 0;
}
