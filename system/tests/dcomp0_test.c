/*+===================================================================
  File:      dcomp0_test.c

  Summary:   Implements dcomp0 test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include <assert.h>
#include <stdio.h>
#include "rsys/dcomp0.h"

static void check(const uint8_t* src, size_t n, const uint8_t* expected, size_t size)
{
    uint8_t dest[2048], padded[2048];
    size_t i;
    size_t consumed = 999;
    assert(size + 2 <= sizeof dest);
    memset(dest, 0xa5, sizeof dest);
    assert(ROMlib_decompress_dcmp0(src, n, dest + 1, size));
    assert(!memcmp(dest + 1, expected, size));
    assert(dest[0] == 0xa5 && dest[size + 1] == 0xa5);
    assert(n + 2 <= sizeof padded);
    memcpy(padded, src, n);
    memset(padded + n, 0xab, 2);
    memset(dest, 0xa5, sizeof dest);
    assert(ROMlib_decompress_dcmp0_prefix(padded, n + 2, NULL, size, &consumed));
    assert(consumed == n);
    assert(ROMlib_decompress_dcmp0_prefix(padded, n + 2, dest + 1, size, &consumed));
    assert(!memcmp(dest + 1, expected, size));
    assert(dest[0] == 0xa5 && dest[size + 1] == 0xa5);
    assert(!ROMlib_decompress_dcmp0(padded, n + 2, dest + 1, size));
    for (i = 0; i < n; ++i) {
        assert(!ROMlib_decompress_dcmp0(src, i, dest + 1, size));
        assert(!ROMlib_decompress_dcmp0_prefix(src, i, NULL, size, &consumed));
    }
    if (size > 1)
        assert(!ROMlib_decompress_dcmp0(src, n, dest + 1, size - 2));
}

#define CHECK(s, e) check((const uint8_t*)(s), sizeof(s) - 1, (const uint8_t*)(e), sizeof(e) - 1)

int main(void)
{
    uint8_t data[1800], output[1800];
    size_t i, pos;
    size_t measured = 999;
    assert(ROMlib_decode_dcmp0((const uint8_t*)"\x01"
                                               "AB\xff",
                               4, NULL, sizeof output, &measured));
    assert(measured == 2);
    assert(!ROMlib_decode_dcmp0((const uint8_t*)"\x01"
                                                "AB\xff",
                                4, NULL, 0, &measured));
    CHECK("\xfe\x01\x20\x08\x01\x10\xff", "\x61\0\0\x20\x4e\xed\0\x10"
                                          "\x61\0\0\x18\x4e\xed\0\x18");
    CHECK("\xfe\x01\x20\0\x01\x10\x28\xff", "\x61\0\0\x20\x4e\xed\0\x10"
                                            "\x61\0\0\x18\x4e\xed\0\x28");
    CHECK("\xfe\x05\x7f\x02\x01\xbf\xff\xff", "\0\x7f\0\x80\0\x7f");
    CHECK("\x01"
          "AB\xff",
          "AB");
    CHECK("\x00\x01"
          "AB\xff",
          "AB");
    CHECK("\x11"
          "AB\x23\xff",
          "ABAB");
    CHECK("\x10\x01"
          "AB\x23\xff",
          "ABAB");
    CHECK("\x4b\x4e\xff", "\0\0Nu");
    CHECK("\xfe\x02\x41\x02\xff", "AAA");
    CHECK("\xfe\x03\xc1\x23\x01\xff", "\x01\x23\x01\x23");
    CHECK("\xfe\x04\x7f\x02\x01\xff\xff", "\0\x7f\0\x80\0\x7f");
    CHECK("\xfe\x06\xff\xff\xff\xff\xff\x02\x01\xbf\xff\xff", "\xff\xff\xff\xff\0\0\0\0\xff\xff\xff\xff");
    CHECK("\xfe\x00\x02\x02\x10\x0e\xff",
          "\x3f\x3c\0\x02\xa9\xf0\0\x10\x3f\x3c\0\x02\xa9\xf0\0\x18\x3f\x3c\0\x02\xa9\xf0");
    CHECK("\x01"
          "AZ\xff",
          "A"); /* Odd-sized resource padding. */
    CHECK("\xff", "");
    CHECK("\x10\0\x10\0\xff", "");
    CHECK("\xfe\0\x02\0\xff", "\x3f\x3c\0\x02\xa9\xf0");
    /* Two- and three-byte literal references. */
    pos = 0;
    for (i = 0; i < 41; ++i) {
        data[pos++] = 0x11;
        data[pos++] = i;
        data[pos++] = i + 1;
        output[2 * i] = i;
        output[2 * i + 1] = i + 1;
    }
    data[pos++] = 0x20;
    data[pos++] = 0;
    data[pos++] = 0x22;
    data[pos++] = 0;
    data[pos++] = 0;
    data[pos++] = 0xff;
    output[82] = output[84] = 40;
    output[83] = output[85] = 41;
    check(data, pos, output, 86);
    pos = 0;
    for (i = 0; i < 553; ++i) {
        data[pos++] = 0x11;
        data[pos++] = i;
        data[pos++] = i + 1;
        output[2 * i] = i;
        output[2 * i + 1] = i + 1;
    }
    data[pos++] = 0x21;
    data[pos++] = 0xff; /* Literal 551. */
    data[pos++] = 0x22;
    data[pos++] = 2;
    data[pos++] = 0; /* Literal 552. */
    data[pos++] = 0xff;
    output[1106] = 551 & 255;
    output[1107] = 552 & 255;
    output[1108] = 552 & 255;
    output[1109] = 553 & 255;
    check(data, pos, output, 1110);
    /* Reject invalid references, extensions, lengths, counts and trailing data. */
    assert(!ROMlib_decompress_dcmp0((const uint8_t*)"\x23\xff", 2, output, 2));
    assert(!ROMlib_decompress_dcmp0((const uint8_t*)"\xfe\x01\xff", 3, output, 2));
    assert(!ROMlib_decompress_dcmp0((const uint8_t*)"\xff\xff", 2, output, 0));
    assert(!ROMlib_decompress_dcmp0((const uint8_t*)"\xfe\x02\x01\xbf\xff\xff", 6, output, 0));
    /* Deterministic malformed-stream fuzzing, guarded output. */
    {
        uint32_t state = 1;
        for (i = 0; i < 20000; ++i) {
            size_t j, n = i % sizeof data, size = i % 128;
            for (j = 0; j < n; ++j) {
                state = state * 1664525U + 1013904223U;
                data[j] = state >> 24;
            }
            memset(output, 0xa5, sizeof output);
            ROMlib_decompress_dcmp0(data, n, output + 1, size);
            assert(output[0] == 0xa5 && output[size + 1] == 0xa5);
            ROMlib_decompress_dcmp0_prefix(data, n, output + 1, size, &measured);
            assert(output[0] == 0xa5 && output[size + 1] == 0xa5);
        }
    }
    puts("dcmp 0 decoding, truncation, guards and malformed-stream checks passed");
    return 0;
}
