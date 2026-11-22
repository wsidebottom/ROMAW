/*+===================================================================
  File:      itl2_layout_test.c

  Summary:   Implements itl2 layout test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "rsys/itl2.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    unsigned char data[128] = {0};
    uint16_t offsets[5];
    const unsigned fields[] = {0, 2, 4, 6, 10};
    for (unsigned i = 0; i < 5; ++i)
        data[fields[i] + 1] = 126;
    assert(itl2_vectors(data, sizeof data, offsets));
    for (size_t length = 0; length < sizeof data; ++length)
        assert(!itl2_vectors(data, length, offsets));
    for (unsigned i = 0; i < 5; ++i) {
        const unsigned invalid[] = {0, 10, 13, 128, 32768, 65535};
        for (unsigned n = 0; n < sizeof invalid / sizeof invalid[0]; ++n) {
            data[fields[i]] = invalid[n] >> 8;
            data[fields[i] + 1] = invalid[n];
            memset(offsets, 0xa5, sizeof offsets);
            assert(!itl2_vectors(data, sizeof data, offsets));
            assert(offsets[0] == 0xa5a5 && offsets[4] == 0xa5a5);
        }
        data[fields[i]] = 0;
        data[fields[i] + 1] = 126;
    }
    assert(itl2_projection('a') == 'A');
    assert(itl2_projection(0x8e) == 'E');
    assert(itl2_projection(0xca) == ' ');
    assert(itl2_projection(0xf5) == 'I');
    assert(itl2_vernier('A', 0) < itl2_vernier('a', 0));
    assert(itl2_vernier('A', 1) == 0x4101);
    assert(itl2_vernier(0xcc, 0) == 0x8001);
    for (unsigned c = 0; c < 256; ++c)
        assert(itl2_projection(c) <= 255 && itl2_vernier(c, 255) <= 65535);
    for (size_t length = 0; length < 128; ++length) {
        unsigned char* p = malloc(length ? length : 1);
        for (unsigned sample = 0; sample < 500; ++sample) {
            for (size_t i = 0; i < length; ++i)
                p[i] = rand();
            itl2_vectors(p, length, offsets);
        }
        free(p);
    }
    puts("PASS: itl2 ABI, bounded hook vectors, Roman projection and vernier keys");
    return 0;
}
