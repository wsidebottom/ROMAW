/*+===================================================================
  File:      movie_test.c

  Summary:   Implements movie test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "rsys/movie.h"
#include <assert.h>
#include <stdio.h>
#include "movie_fixture.h"

int main(void)
{
    movie_fixture();
    movie_info info;
    assert(!movie_parse(movie_bytes, movie_used, &info));
    assert(info.width == 2 && info.height == 2 && info.scale == 60);
    assert(info.duration == 90 && info.count == 3);
    for (unsigned i = 0; i < 3; ++i) {
        assert(info.samples[i].offset == 8 + i * 12);
        assert(info.samples[i].size == 12 && info.samples[i].start == i * 30);
        assert(info.samples[i].duration == 30);
    }
    free(info.samples);
    for (unsigned length = 0; length < movie_used; ++length) {
        uint8_t* copy = malloc(length ? length : 1);
        memcpy(copy, movie_bytes, length);
        memset(&info, 0x5a, sizeof info);
        movie_info before = info;
        assert(movie_parse(copy, length, &info));
        assert(!memcmp(&info, &before, sizeof info));
        free(copy);
    }
    /* Every changed byte must either reject cleanly or yield bounded frames. */
    for (unsigned byte = 0; byte < movie_used; ++byte) {
        movie_bytes[byte] ^= 0xff;
        if (!movie_parse(movie_bytes, movie_used, &info)) {
            for (unsigned i = 0; i < info.count; ++i)
                assert(info.samples[i].offset <= movie_used &&
                       info.samples[i].size <= movie_used - info.samples[i].offset);
            free(info.samples);
        }
        movie_bytes[byte] ^= 0xff;
    }
    puts("Movie parser: RGB samples, timing, every truncation and malformed atoms passed");
    return 0;
}
