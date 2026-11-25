/*+===================================================================
  File:      aiff.h

  Summary:   Bounded AIFF/AIFF-C metadata reader. Chunk offsets are file-relative. ifndef RSYS_AIFF_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Bounded AIFF/AIFF-C metadata reader. Chunk offsets are file-relative. */
#ifndef RSYS_AIFF_H
#define RSYS_AIFF_H
#include <stdint.h>
#include <string.h>

typedef int (*aiff_read_proc)(void*, uint32_t, void*, uint32_t);

typedef struct {
    uint32_t frames, rate, offset, input_size, format;
    unsigned channels, bits, little_endian, compression;
} aiff_info;

static uint32_t aiff_long(const uint8_t* p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* Convert a positive, normalized IEEE extended sample rate to unsigned 16.16.
   Reject NaNs, infinity, denormals and rates outside the mixer's fixed range. */
static uint32_t aiff_rate(const uint8_t* p)
{
    unsigned exponent = (unsigned)p[0] << 8 | p[1];
    uint64_t mantissa = (uint64_t)aiff_long(p + 2) << 32 | aiff_long(p + 6);
    if (exponent < 16367 || exponent > 16398 || !(mantissa >> 63))
        return 0;
    return mantissa >> (16430 - exponent);
}

/* Read the on-disk frame count and sample extent without requiring a decoder. */
static int aiff_read_header(aiff_read_proc read, void* context, uint32_t size, aiff_info* output)
{
    uint8_t header[24];
    aiff_info info = {0};
    uint32_t end, position = 12;
    int common = 0, sound = 0, compressed;
    if (!output || size < 12)
        return -208;
    if (!read(context, 0, header, 12))
        return -36;
    if (memcmp(header, "FORM", 4) || (memcmp(header + 8, "AIFF", 4) && memcmp(header + 8, "AIFC", 4)) ||
        aiff_long(header + 4) < 4 || aiff_long(header + 4) > size - 8)
        return -208;
    compressed = !memcmp(header + 8, "AIFC", 4);
    end = aiff_long(header + 4) + 8;
    for (unsigned chunks = 0; position < end; ++chunks) {
        if (chunks == 65536 || end - position < 8)
            return -208;
        if (!read(context, position, header, 8))
            return -36;
        uint32_t length = aiff_long(header + 4), payload = position + 8;
        if (length > end - payload || (length & 1) > end - payload - length)
            return -208;
        if (!memcmp(header, "COMM", 4)) {
            if (common++ || length < (compressed ? 23U : 18U))
                return -208;
            if (!read(context, payload, header, compressed ? 23 : 18))
                return -36;
            info.channels = (unsigned)header[0] << 8 | header[1];
            info.frames = aiff_long(header + 2);
            info.bits = (unsigned)header[6] << 8 | header[7];
            info.rate = aiff_rate(header + 8);
            info.format = aiff_long((const uint8_t*)"NONE");
            if (compressed) {
                if ((unsigned)header[22] > length - 23)
                    return -208;
                info.format = aiff_long(header + 18);
                info.little_endian = !memcmp(header + 18, "sowt", 4);
                if (!memcmp(header + 18, "MAC3", 4))
                    info.compression = 3;
                else if (!memcmp(header + 18, "MAC6", 4))
                    info.compression = 4;
            }
            if (!info.channels || info.channels > INT16_MAX || !info.bits || info.bits > 64 || !info.rate)
                return -208;
        }
        else if (!memcmp(header, "SSND", 4)) {
            if (sound++ || length < 8)
                return -208;
            if (!read(context, payload, header, 8))
                return -36;
            uint32_t offset = aiff_long(header);
            if (offset > length - 8)
                return -208;
            info.offset = payload + 8 + offset;
            info.input_size = length - 8 - offset;
        }
        position = payload + length + (length & 1);
    }
    if (!common || !sound)
        return -208;
    uint64_t needed = info.input_size;
    if (info.compression)
        needed = (uint64_t)info.frames * info.channels * (info.compression == 3 ? 2 : 1);
    else if (info.format == aiff_long((const uint8_t*)"NONE") ||
             info.format == aiff_long((const uint8_t*)"twos") || info.little_endian)
        needed = (uint64_t)info.frames * info.channels * ((info.bits + 7) / 8);
    if (needed > info.input_size)
        return -208;
    info.input_size = needed;
    *output = info;
    return 0;
}

/* Result: 0 success, -208 malformed/unsupported PCM, -223 unknown codec,
   -36 read failure. No output is published on error. */
static int aiff_parse(aiff_read_proc read, void* context, uint32_t size, aiff_info* output)
{
    aiff_info info;
    if (!output)
        return -208;
    const int error = aiff_read_header(read, context, size, &info);
    if (error)
        return error;
    if (!info.compression && !info.little_endian && info.format != aiff_long((const uint8_t*)"NONE") &&
        info.format != aiff_long((const uint8_t*)"twos"))
        return -223;
    if ((info.channels != 1 && info.channels != 2) || (info.bits != 8 && info.bits != 16))
        return -208;
    if (info.compression) {
        /* MACE COMM counts packet frames; each expands to six PCM frames. */
        if (info.frames > (UINT32_MAX - 1) / 6)
            return -208;
        info.frames *= 6;
    }
    *output = info;
    return 0;
}

/* AIFF 8-bit PCM is signed, unlike a classic standard/extended SoundHeader. */
static void aiff_decode_pcm(const uint8_t* input, uint8_t* output, uint32_t frames, const aiff_info* info)
{
    for (uint32_t frame_index = 0; frame_index < frames; ++frame_index) {
        unsigned sum = 0;
        for (unsigned channel = 0; channel < info->channels; ++channel) {
            if (info->bits == 8)
                sum += (*input++ ^ 0x80) * 256U;
            else {
                unsigned sample =
                    info->little_endian ? (unsigned)input[1] << 8 | input[0] : (unsigned)input[0] << 8 | input[1];
                sum += sample ^ 0x8000;
                input += 2;
            }
        }
        output[frame_index] = sum / (info->channels * 256);
    }
}
#endif
