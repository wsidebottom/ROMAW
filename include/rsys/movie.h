/*+===================================================================
  File:      movie.h

  Summary:   Original bounded reader for self-contained, raw RGB QuickTime movies. ifndef RSYS_MOVIE_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Original bounded reader for self-contained, raw RGB QuickTime movies. */
#ifndef RSYS_MOVIE_H
#define RSYS_MOVIE_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t offset, size, start, duration;
} movie_sample;

typedef struct {
    unsigned width, height;
    uint32_t scale, duration, count;
    movie_sample* samples;
} movie_info;

typedef struct {
    const uint8_t* data;
    uint32_t size;
} movie_atom;

static uint32_t movie_long(const uint8_t* p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* Validate every sibling, even after finding the requested atom. */
static int movie_find(movie_atom parent, const char* type, movie_atom* output)
{
    movie_atom found = {0};
    unsigned atoms = 0;
    while (parent.size) {
        if (parent.size < 8 || ++atoms > 65536)
            return -1;
        uint32_t length = movie_long(parent.data), header = 8;
        if (length == 1) {
            if (parent.size < 16 || movie_long(parent.data + 8))
                return -1;
            length = movie_long(parent.data + 12);
            header = 16;
        }
        else if (!length)
            length = parent.size;
        if (length < header || length > parent.size)
            return -1;
        if (!memcmp(parent.data + 4, type, 4)) {
            /* This first implementation has one video track/data extent.
             Reject duplicates instead of silently dropping media tracks. */
            if (found.data)
                return -1;
            found = (movie_atom){parent.data + header, length - header};
        }
        parent.data += length;
        parent.size -= length;
    }
    *output = found;
    return found.data != NULL;
}

static int movie_table(movie_atom parent, const char* type, unsigned stride, movie_atom* table, uint32_t* count)
{
    if (movie_find(parent, type, table) != 1 || table->size < 8 || movie_long(table->data))
        return 0;
    *count = movie_long(table->data + 4);
    return *count && *count <= 65536 && *count <= (table->size - 8) / stride;
}

/* Returns badFormat (-208), codecNotFoundErr (-8960), memFullErr (-108).
   Only publish an owned sample table after validating every frame extent. */
static int movie_parse(const uint8_t* data, uint32_t size, movie_info* output)
{
    movie_atom root = {data, size}, moov, trak, mdia, minf, stbl, mdhd, mdat;
    movie_atom stsd, stts, stsc, stsz, stco, hdlr;
    movie_info info = {0};
    uint32_t descriptions, times, runs, chunks;
    if (!data || !output || movie_find(root, "mdat", &mdat) != 1 || movie_find(root, "moov", &moov) != 1 ||
        movie_find(moov, "trak", &trak) != 1 || movie_find(trak, "mdia", &mdia) != 1 ||
        movie_find(mdia, "hdlr", &hdlr) != 1 || hdlr.size < 12 || memcmp(hdlr.data + 8, "vide", 4) ||
        movie_find(mdia, "mdhd", &mdhd) != 1 || mdhd.size < 24 || movie_long(mdhd.data) ||
        !(info.scale = movie_long(mdhd.data + 12)) || movie_find(mdia, "minf", &minf) != 1 ||
        movie_find(minf, "stbl", &stbl) != 1 || !movie_table(stbl, "stsd", 86, &stsd, &descriptions) ||
        descriptions != 1 || movie_long(stsd.data + 8) != 86)
        return -208;
    const uint8_t* description = stsd.data + 8;
    if (memcmp(description + 4, "raw ", 4) || description[14] != 0 || description[15] != 1 || description[82] != 0 ||
        description[83] != 24)
        return -8960;
    info.width = (unsigned)description[32] << 8 | description[33];
    info.height = (unsigned)description[34] << 8 | description[35];
    if (!info.width || !info.height || info.width > 2048 || info.height > 2048 ||
        !movie_table(stbl, "stts", 8, &stts, &times) || !movie_table(stbl, "stsc", 12, &stsc, &runs) ||
        !movie_table(stbl, "stco", 4, &stco, &chunks) || movie_find(stbl, "stsz", &stsz) != 1 || stsz.size < 12 ||
        movie_long(stsz.data))
        return -208;
    uint32_t fixed = movie_long(stsz.data + 4);
    info.count = movie_long(stsz.data + 8);
    if (!info.count || info.count > 65536 || (!fixed && info.count > (stsz.size - 12) / 4))
        return -208;
    info.samples = calloc(info.count, sizeof *info.samples);
    if (!info.samples)
        return -108;
    uint32_t sample = 0, time = 0;
    for (unsigned i = 0; i < times; ++i) {
        uint32_t n = movie_long(stts.data + 8 + 8 * i);
        uint32_t delta = movie_long(stts.data + 12 + 8 * i);
        if (!n || !delta || n > info.count - sample || (uint64_t)time + (uint64_t)n * delta > INT32_MAX)
            goto malformed;
        for (unsigned j = 0; j < n; ++j) {
            info.samples[sample].start = time;
            info.samples[sample++].duration = delta;
            time += delta;
        }
    }
    if (sample != info.count || time != movie_long(mdhd.data + 16))
        goto malformed;
    info.duration = time;
    sample = 0;
    for (unsigned i = 0; i < runs; ++i) {
        const uint8_t* run = stsc.data + 8 + 12 * i;
        uint32_t first = movie_long(run), per_chunk = movie_long(run + 4);
        uint32_t end = i + 1 == runs ? chunks + 1 : movie_long(run + 12);
        if ((!i && first != 1) || !first || first >= end || end > chunks + 1 || !per_chunk || movie_long(run + 8) != 1)
            goto malformed;
        for (uint32_t chunk = first; chunk < end; ++chunk) {
            uint32_t offset = movie_long(stco.data + 8 + 4 * (chunk - 1));
            if (per_chunk > info.count - sample)
                goto malformed;
            for (unsigned j = 0; j < per_chunk; ++j) {
                uint32_t length = fixed ? fixed : movie_long(stsz.data + 12 + 4 * sample);
                uint32_t data_start = mdat.data - data;
                if (length != info.width * info.height * 3 || offset < data_start || offset - data_start > mdat.size ||
                    length > mdat.size - (offset - data_start))
                    goto malformed;
                info.samples[sample].offset = offset;
                info.samples[sample++].size = length;
                offset += length;
            }
        }
    }
    if (sample != info.count)
        goto malformed;
    *output = info;
    return 0;
malformed:
    free(info.samples);
    return -208;
}
#endif
