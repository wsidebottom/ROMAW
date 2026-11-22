/*+===================================================================
  File:      picture_header_test.c

  Summary:   Implements picture header test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "rsys/pictheader.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

int main(void)
{
    size_t page = sysconf(_SC_PAGESIZE), n;
    unsigned char* memory = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    unsigned char header[14] = {0};
    assert(memory != MAP_FAILED && !mprotect(memory + page, page, PROT_NONE));
    assert(!ROMlib_pict_header_valid(NULL, 100));
    for (n = 0; n < 14; ++n) {
        unsigned char* p = memory + page - n;
        memset(p, 0, n);
        assert(ROMlib_pict_header_valid(p, n) == (n >= 10));
    }
    header[1] = 9;
    assert(!ROMlib_pict_header_valid(header, 14));
    header[1] = 15;
    assert(!ROMlib_pict_header_valid(header, 14));
    header[1] = 14;
    assert(ROMlib_pict_header_valid(header, 14));
    header[0] = 0x98;
    header[1] = 0xe0;
    header[11] = 0x11;
    header[12] = 2;
    header[13] = 0xff;
    assert(ROMlib_pict_header_valid(header, 235744));
    header[0] = 0xff;
    header[1] = 0xff;
    assert(ROMlib_pict_header_valid(header, 70000));
    header[0] = 0;
    header[1] = 1;
    assert(ROMlib_pict_header_valid(header, 14));
    header[13] = 0xfe;
    assert(!ROMlib_pict_header_valid(header, 14));
    assert(!munmap(memory, page * 2));
    puts("PICT headers: protected-page truncations, legacy bounds and ignored version-2 sizes passed");
    return 0;
}
