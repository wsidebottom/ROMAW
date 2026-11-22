/*+===================================================================
  File:      sound_resource.h

  Summary:   Bounded, single-buffer snd resource decoding metadata. ifndef RSYS_SOUND_RESOURCE_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Bounded, single-buffer snd resource decoding metadata. */
#ifndef RSYS_SOUND_RESOURCE_H
#define RSYS_SOUND_RESOURCE_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t frames, rate, offset, input_size;
    unsigned channels, bits, compression;
} resource_sound;

static uint32_t resource_long(const uint8_t* p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static unsigned resource_word(const uint8_t* p)
{
    return (unsigned)p[0] << 8 | p[1];
}

static int resource_sound_parse(const uint8_t* bytes, size_t size, resource_sound* output)
{
    resource_sound info = {0};
    if (!bytes || !output || size < 6 || size > INT32_MAX)
        return -206;
    unsigned format = resource_word(bytes), count;
    size_t commands;
    if (format == 1) {
        unsigned modifiers = resource_word(bytes + 2);
        if (modifiers > (size - 6) / 6)
            return -206;
        for (unsigned i = 0; i < modifiers; ++i)
            if (resource_word(bytes + 4 + 6 * i) != 5)
                return -206;
        commands = 4 + 6 * modifiers;
    }
    else if (format == 2)
        commands = 4;
    else
        return -206;
    count = resource_word(bytes + commands);
    commands += 2;
    if (!count || count > (size - commands) / 8)
        return -206;
    uint32_t header = 0;
    for (unsigned i = 0; i < count; ++i) {
        const uint8_t* command = bytes + commands + 8 * i;
        unsigned number = resource_word(command);
        if (number == 0) /* nullCmd */
            continue;
        if (number != 0x8051 || header) /* one relative bufferCmd */
            return -206;
        header = resource_long(command + 4);
    }
    if (header < commands + 8 * count || header > size || size - header < 22)
        return -206;
    const uint8_t* sound = bytes + header;
    if (resource_long(sound)) /* External pointers are not disk resource data. */
        return -206;
    info.rate = resource_long(sound + 8);
    if (!info.rate)
        return -206;
    info.channels = 1;
    info.bits = 8;
    if (sound[20] == 0) {
        info.frames = resource_long(sound + 4);
        info.offset = header + 22;
        info.input_size = info.frames;
    }
    else {
        if (size - header < 64 || (sound[20] != 0xfe && sound[20] != 0xff))
            return -206;
        info.channels = resource_long(sound + 4);
        info.frames = resource_long(sound + 22);
        if (info.channels != 1 && info.channels != 2)
            return -206;
        info.offset = header + 64;
        uint64_t length;
        if (sound[20] == 0xff) {
            info.bits = resource_word(sound + 48);
            if (info.bits != 8 && info.bits != 16)
                return -206;
            length = (uint64_t)info.frames * info.channels * (info.bits / 8);
        }
        else {
            info.compression = resource_word(sound + 56);
            if ((info.compression != 3 && info.compression != 4) || resource_word(sound + 62) != 8 ||
                info.frames > (UINT32_MAX - 1) / 6)
                return -206;
            length = (uint64_t)info.frames * info.channels * (info.compression == 3 ? 2 : 1);
            info.frames *= 6;
        }
        if (length > UINT32_MAX)
            return -206;
        info.input_size = length;
    }
    if (info.frames == UINT32_MAX || info.input_size > size - info.offset)
        return -206;
    *output = info;
    return 0;
}
#endif
