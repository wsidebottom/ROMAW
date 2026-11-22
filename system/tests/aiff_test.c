/*+===================================================================
  File:      aiff_test.c

  Summary:   Implements aiff test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "rsys/aiff.h"
#include "rsys/sound_resource.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    const uint8_t* bytes;
    uint32_t length;
    int fail;
} source;

static int read_at(void* context, uint32_t position, void* bytes, uint32_t count)
{
    source* file = context;
    assert(position <= file->length && count <= file->length - position);
    if (file->fail)
        return 0;
    memcpy(bytes, file->bytes + position, count);
    return 1;
}

static void put_long(uint8_t* p, uint32_t value)
{
    p[0] = value >> 24;
    p[1] = value >> 16;
    p[2] = value >> 8;
    p[3] = value;
}

static void check_file(uint8_t* bytes, uint32_t length, int expected)
{
    aiff_info info, before;
    memset(&info, 0xa5, sizeof info);
    before = info;
    source file = {bytes, length, 0};
    assert(aiff_parse(read_at, &file, length, &info) == expected);
    if (expected)
        assert(!memcmp(&info, &before, sizeof info));
}

static void check_resources(void)
{
    uint8_t resource[44] = {0};
    resource[1] = 2;
    resource[5] = 1;
    resource[6] = 0x80;
    resource[7] = 81;
    put_long(resource + 10, 14);
    put_long(resource + 18, 8);
    put_long(resource + 22, 22050U << 16);
    resource_sound info;
    assert(!resource_sound_parse(resource, sizeof resource, &info));
    assert(info.frames == 8 && info.input_size == 8 && info.channels == 1 && info.bits == 8 && info.offset == 36 &&
           !info.compression);
    for (unsigned size = 0; size < sizeof resource; ++size) {
        uint8_t* prefix = malloc(size ? size : 1);
        memcpy(prefix, resource, size);
        memset(&info, 0xa5, sizeof info);
        resource_sound before = info;
        assert(resource_sound_parse(prefix, size, &info) == -206);
        assert(!memcmp(&info, &before, sizeof info));
        free(prefix);
    }
    put_long(resource + 10, 6); /* Header overlaps the command table. */
    assert(resource_sound_parse(resource, sizeof resource, &info) == -206);
    put_long(resource + 10, 0xffffffff);
    assert(resource_sound_parse(resource, sizeof resource, &info) == -206);
    put_long(resource + 10, 14);
    put_long(resource + 18, 0xffffffff);
    assert(resource_sound_parse(resource, sizeof resource, &info) == -206);
    put_long(resource + 18, 8);
    resource[7] = 13; /* Arbitrary callbacks are not PCM payloads. */
    assert(resource_sound_parse(resource, sizeof resource, &info) == -206);
    resource[7] = 81;
    uint8_t compressed[80] = {0};
    memcpy(compressed, resource, 14);
    put_long(compressed + 18, 1);
    put_long(compressed + 22, 22050U << 16);
    compressed[34] = 0xfe;
    put_long(compressed + 36, 1);
    compressed[71] = 3;
    compressed[77] = 8;
    assert(!resource_sound_parse(compressed, sizeof compressed, &info));
    assert(info.frames == 6 && info.compression == 3 && info.input_size == 2);
    for (unsigned size = 0; size < sizeof compressed; ++size) {
        uint8_t* prefix = malloc(size ? size : 1);
        memcpy(prefix, compressed, size);
        assert(resource_sound_parse(prefix, size, &info) == -206);
        free(prefix);
    }
    put_long(compressed + 36, 0xffffffff);
    assert(resource_sound_parse(compressed, sizeof compressed, &info) == -206);
}

int main(void)
{
    check_resources();
    uint8_t file[84] = {0};
    memcpy(file, "FORM", 4);
    put_long(file + 4, 76);
    memcpy(file + 8, "AIFFJUNK", 8);
    put_long(file + 16, 1); /* Unknown odd-sized chunks include padding. */
    memcpy(file + 22, "COMM", 4);
    put_long(file + 26, 18);
    file[31] = 2;
    put_long(file + 32, 4);
    file[37] = 16;
    const uint8_t rate[] = {0x40, 0x0e, 0xac, 0x44, 0, 0, 0, 0, 0, 0};
    memcpy(file + 38, rate, sizeof rate);
    memcpy(file + 48, "SSND", 4);
    put_long(file + 52, 28);
    put_long(file + 56, 4); /* SSND data offset, not a file offset. */
    const uint8_t pcm[] = {0x80, 0, 0x80, 0, 0, 0, 0, 0, 0x7f, 0xff, 0x7f, 0xff, 0x80, 0, 0x7f, 0xff};
    memcpy(file + 68, pcm, sizeof pcm);
    source input = {file, sizeof file, 0};
    aiff_info info;
    assert(!aiff_parse(read_at, &input, sizeof file, &info));
    assert(info.rate == 44100U << 16 && info.frames == 4 && info.offset == 68 && info.channels == 2 &&
           info.bits == 16 && !info.little_endian);
    uint8_t decoded[6] = {0xa5, 0, 0, 0, 0, 0x5a};
    aiff_decode_pcm(file + info.offset, decoded + 1, info.frames, &info);
    assert(decoded[0] == 0xa5 && decoded[5] == 0x5a);
    assert(decoded[1] == 0 && decoded[2] == 128 && decoded[3] == 255 && decoded[4] == 127);
    /* Every strict prefix is copied to an allocation of exactly that extent. */
    for (unsigned length = 0; length < sizeof file; ++length) {
        uint8_t* prefix = malloc(length ? length : 1);
        memcpy(prefix, file, length);
        check_file(prefix, length, -208);
        free(prefix);
    }
    input.fail = 1;
    assert(aiff_parse(read_at, &input, sizeof file, &info) == -36);
    put_long(file + 56, 21);
    check_file(file, sizeof file, -208);
    put_long(file + 56, 4);
    put_long(file + 32, 0xffffffff);
    check_file(file, sizeof file, -208);
    put_long(file + 32, 4);
    put_long(file + 16, 0xffffffff);
    check_file(file, sizeof file, -208);
    put_long(file + 16, 1);
    file[38] |= 0x80; /* Negative sample rate. */
    check_file(file, sizeof file, -208);
    memcpy(file + 38, rate, sizeof rate);

    /* AIFC sowt, COMM after SSND, with signed little-endian stereo PCM. */
    uint8_t compressed[64] = {0};
    memcpy(compressed, "FORM", 4);
    put_long(compressed + 4, sizeof compressed - 8);
    memcpy(compressed + 8, "AIFCSSND", 8);
    put_long(compressed + 16, 12);
    memcpy(compressed + 28, "\0\x80\xff\x7f", 4);
    memcpy(compressed + 32, "COMM", 4);
    put_long(compressed + 36, 24);
    compressed[41] = 2;
    put_long(compressed + 42, 1);
    compressed[47] = 16;
    memcpy(compressed + 48, rate, sizeof rate);
    memcpy(compressed + 58, "sowt", 4);
    input = (source){compressed, sizeof compressed, 0};
    assert(!aiff_parse(read_at, &input, sizeof compressed, &info));
    aiff_decode_pcm(compressed + info.offset, decoded + 1, 1, &info);
    assert(decoded[1] == 127 && info.little_endian);
    memcpy(compressed + 58, "MAC3", 4);
    assert(!aiff_parse(read_at, &input, sizeof compressed, &info));
    assert(info.frames == 6 && info.compression == 3 && info.input_size == 4);
    put_long(compressed + 42, 2);
    check_file(compressed, sizeof compressed, -208);
    memcpy(compressed + 58, "MAC6", 4);
    assert(!aiff_parse(read_at, &input, sizeof compressed, &info));
    assert(info.frames == 12 && info.compression == 4 && info.input_size == 4);
    put_long(compressed + 42, 0xffffffff);
    check_file(compressed, sizeof compressed, -208);
    put_long(compressed + 42, 1);
    memcpy(compressed + 58, "xxxx", 4);
    check_file(compressed, sizeof compressed, -223);
    memcpy(compressed + 58, "NONE", 4);
    compressed[63] = 2; /* Compression name extends beyond the COMM chunk. */
    compressed[62] = 2;
    check_file(compressed, sizeof compressed, -208);

    info.channels = 1;
    info.bits = 8;
    uint8_t signed_pcm[] = {0x80, 0, 0x7f};
    aiff_decode_pcm(signed_pcm, decoded + 1, 3, &info);
    assert(decoded[1] == 0 && decoded[2] == 128 && decoded[3] == 255);
    puts("AIFF: bounded chunks, signed PCM, stereo, rates and truncations passed");
    return 0;
}
