/*+===================================================================
  File:      volume_geometry_test.c

  Summary:   Implements volume geometry test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "rsys/volume_geometry.h"
#include <assert.h>
#include <stdio.h>

static void check(uint64_t total, uint64_t available)
{
    ROMlib_volume_geometry g = ROMlib_legacy_volume_geometry(total, available);
    uint64_t capacity = (uint64_t)g.blocks * g.block_size;
    uint64_t free_bytes = (uint64_t)g.free_blocks * g.block_size;
    const uint64_t maximum = UINT64_C(0x7ffffe00) * UINT16_MAX;
    uint64_t capped = total > maximum ? maximum : total;
    assert(g.block_size >= 512 && g.block_size % 512 == 0);
    assert(capacity <= capped && capped - capacity < g.block_size);
    assert(g.free_blocks <= g.blocks && free_bytes <= capacity);
    assert(free_bytes <= available);
    assert(g.block_size <= INT32_MAX);
    if (available <= capped)
        assert(available - free_bytes < g.block_size);
}

int main(void)
{
    const uint64_t sizes[] = {0,
                              1,
                              511,
                              512,
                              513,
                              65535 * 512,
                              65536 * 512,
                              0x7ffffdff,
                              0x7ffffe00,
                              0x7fffffff,
                              UINT64_C(0x100000000),
                              UINT64_C(2000000000000),
                              UINT64_C(6000000000000),
                              UINT64_C(0x7ffffe00) * UINT16_MAX,
                              UINT64_MAX};
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; ++i)
        for (unsigned j = 0; j < sizeof sizes / sizeof sizes[0]; ++j)
            check(sizes[i], sizes[j]);
    ROMlib_volume_geometry g = ROMlib_legacy_volume_geometry(UINT64_C(2000000000000), UINT64_C(500000000000));
    assert(g.free_blocks < g.blocks);
    assert((uint64_t)g.blocks * g.block_size > UINT32_MAX);
    assert((uint64_t)g.free_blocks * g.block_size > UINT32_MAX);
    for (uint64_t size = 0; size < UINT64_C(0x80000000); size += 123457)
        check(size, size / 3);
    puts("Legacy volume geometry: bounded counts, large volumes and free-space limits passed");
    return 0;
}
