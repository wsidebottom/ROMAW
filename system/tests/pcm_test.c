/*+===================================================================
  File:      pcm_test.c

  Summary:   Implements pcm test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "../../include/rsys/pcm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const uint8_t mono16[] = {0x80, 0, 0xff, 0xff, 0, 0, 0x7f, 0xff};
    const uint8_t stereo16[] = {0x80, 0, 0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff, 0x80, 0, 0x80, 0, 0, 0, 0, 0};
    const uint8_t mono8[] = {0, 127, 128, 255};
    const uint8_t stereo8[] = {0, 255, 128, 128, 255, 255, 0, 0};
    uint8_t out[6];
    memset(out, 0x5a, sizeof out);
    assert(ROMlib_pcm_decode(mono16, sizeof mono16, 1, 16, out + 1, 4));
    assert(!memcmp(out, "\x5a\x00\x7f\x80\xff\x5a", 6));
    assert(ROMlib_pcm_decode(stereo16, sizeof stereo16, 2, 16, out + 1, 4));
    assert(!memcmp(out, "\x5a\x7f\xff\x00\x80\x5a", 6));
    assert(ROMlib_pcm_decode(mono8, sizeof mono8, 1, 8, out + 1, 4));
    assert(!memcmp(out + 1, mono8, 4));
    assert(ROMlib_pcm_decode(stereo8, sizeof stereo8, 2, 8, out + 1, 4));
    assert(!memcmp(out, "\x5a\x7f\x80\xff\x00\x5a", 6));
    memset(out, 0x5a, sizeof out);
    assert(!ROMlib_pcm_decode(mono16, sizeof mono16 - 1, 1, 16, out, 4));
    assert(!ROMlib_pcm_decode(mono16, sizeof mono16, 3, 16, out, 1));
    assert(!ROMlib_pcm_decode(mono16, sizeof mono16, 1, 24, out, 1));
    assert(!ROMlib_pcm_decode(mono16, sizeof mono16, 1, 16, out, SIZE_MAX));
    assert(!ROMlib_pcm_decode(NULL, sizeof mono16, 1, 16, out, 1));
    assert(!ROMlib_pcm_decode(mono16, sizeof mono16, 1, 16, NULL, 1));
    assert(ROMlib_pcm_decode(NULL, 0, 1, 16, NULL, 0));
    assert(!memcmp(out, "ZZZZZZ", 6));
    puts("Extended PCM conversion checks passed");
    return 0;
}
