/*+===================================================================
  File:      powerpc_cache_test.c

  Summary:   Check the read-only virtual PowerPC cache-information records and boundaries.

  Classes:   None.

  Functions: main

  Origin:    Original regression fixture for MacTest Pro's fixed-address cache-information reads.

  Copyright and Legal notices: See the repository license.
===================================================================+*/

#define NDEBUG 1
#include "../../runtime/gestalt.c"
#undef main
#undef NDEBUG
#include <assert.h>

int main(void)
{
    uint32 value = 0xabcdef01;
    assert(ROMlib_powerpc_cache_read(0x5fffeff0, 4, &value) && value == 0x5fffe000);
    assert(ROMlib_powerpc_cache_read(0x5fffefd8, 4, &value) && value == 0x5fffe000);
    assert(ROMlib_powerpc_cache_read(0x5fffefdc, 2, &value) && value == 0);
    assert(ROMlib_powerpc_cache_read(0x5fffe018, 2, &value) && value == 1);
    assert(ROMlib_powerpc_cache_read(0x5fffe010, 4, &value) && value == 0);
    assert(ROMlib_powerpc_cache_read(0x5fffe014, 4, &value) && value == 0);
    const uint32 starts[] = {0x5fffe000, 0x5fffefd8};
    const unsigned lengths[] = {64, 28};
    for (unsigned r = 0; r < NELEM(starts); ++r)
        for (unsigned size = 1; size <= 4; size *= 2)
            for (unsigned offset = 0; offset <= lengths[r]; ++offset) {
                value = 0xabcdef01;
                int valid = offset + size <= lengths[r];
                assert(ROMlib_powerpc_cache_read(starts[r] + offset, size, &value) == valid);
                if (valid) {
                    uint32 bytes = 0;
                    for (unsigned i = 0; i < size; ++i) {
                        uint32 byte;
                        assert(ROMlib_powerpc_cache_read(starts[r] + offset + i, 1, &byte));
                        bytes = (bytes << 8) | byte;
                    }
                    assert(bytes == value);
                }
                else
                    assert(value == 0xabcdef01);
            }
    const uint32 unmapped[] = {0, 0x5fffdfff, 0x5fffe040, 0x5fffefd7, 0x5fffeff4, 0xfffffffe, 0xffffffff};
    for (unsigned i = 0; i < NELEM(unmapped); ++i) {
        value = 0xabcdef01;
        assert(!ROMlib_powerpc_cache_read(unmapped[i], 4, &value) && value == 0xabcdef01);
    }
    assert(!ROMlib_powerpc_cache_read(0x5fffeff0, 0, &value));
    assert(!ROMlib_powerpc_cache_read(0x5fffeff0, 3, &value));
    assert(!ROMlib_powerpc_cache_read(0x5fffeff0, UINT_MAX, &value));
    assert(!ROMlib_powerpc_cache_read(0x5fffeff0, 4, NULL));
    puts("PASS: virtual PowerPC cache pointers, big-endian subreads, absent physical caches and bounds");
    return 0;
}
