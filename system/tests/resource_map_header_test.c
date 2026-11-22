/*+===================================================================
  File:      resource_map_header_test.c

  Summary:   Reject malformed map extents before allocating or copying its fixed header.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Reject malformed map extents before allocating or copying its fixed header. */
#define NDEBUG 1
#include "../../resources/resOpen.c"
#undef NDEBUG
#undef main
#include <assert.h>

int main(void)
{
    reshead header = {0};
    assert(!resource_header_valid(&header, 20));
    header.rdatoff = CLC(256);
    header.rmapoff = CLC(256);
    header.maplen = CLC(sizeof(resmap) + sizeof(INTEGER));
    LONGINT length = 256 + sizeof(resmap) + sizeof(INTEGER);
    assert(resource_header_valid(&header, length));
    assert(!resource_header_valid(&header, length - 1));
    assert(!resource_header_valid(&header, -1));
    const uint32_t bad[] = {0, 1, sizeof(reshead), UINT32_MAX};
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; ++i) {
        reshead damaged = header;
        damaged.maplen = CL(bad[i]);
        assert(!resource_header_valid(&damaged, length));
    }
    header.rmapoff = CLC(INT32_MAX);
    assert(!resource_header_valid(&header, length));
    header.rmapoff = CLC(256);
    header.datlen = CLC(UINT32_MAX);
    assert(!resource_header_valid(&header, length));
    puts("Resource map header: zero/short maps, truncation and invalid extents passed");
    return 0;
}
