/*+===================================================================
  File:      diskimage.c

  Summary:   Implements diskimage support for HFS volumes and catalog operations.

  Classes:   None.

  Functions: romaw_image_probe

  Origin:    Existing repository source in executor/src/hfs; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include "rsys/diskimage.h"
#include <limits.h>
#include <string.h>

static uint32_t be32(const unsigned char* p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static unsigned be16(const unsigned char* p)
{
    return (unsigned)p[0] << 8 | p[1];
}

static int read_block(FILE* file, uint64_t offset, unsigned char* block)
{
    return offset <= INT32_MAX - 512 && !fseek(file, (long)offset, SEEK_SET) && fread(block, 1, 512, file) == 512;
}

static int hfs_volume(FILE* file, uint64_t start, uint64_t length)
{
    unsigned char b[512];
    if (length < 1536 || !read_block(file, start + 1024, b) || be16(b) != 0x4244 || be16(b + 124) == 0x482b)
        return 0;
    uint32_t size = be32(b + 20);
    unsigned blocks = be16(b + 18), first = be16(b + 28);
    if (!blocks || size < 512 || size % 512 || !b[36] || b[36] > 27 || be16(b + 34) > blocks || be16(b + 14) < 3 ||
        (uint64_t)be16(b + 14) * 512 >= (uint64_t)first * 512 ||
        (uint64_t)first * 512 + (uint64_t)blocks * size + 1024 > length)
        return 0;
    /* Extents/catalog files must have nonempty, bounded initial extents. */
    const unsigned tables[] = {134, 150};
    for (unsigned t = 0; t < 2; ++t) {
        unsigned at = tables[t], covered = 0;
        uint32_t bytes = be32(b + at - 4);
        if (!bytes || bytes > (uint64_t)blocks * size)
            return 0;
        for (unsigned i = 0; i < 3; ++i) {
            unsigned block = be16(b + at + i * 4);
            unsigned count = be16(b + at + i * 4 + 2);
            if (block + count > blocks)
                return 0;
            covered += count;
        }
        if (!covered)
            return 0;
    }
    return 1;
}

int romaw_image_probe(FILE* file, romaw_image_plan* plan)
{
    if (!plan)
        return 0;
    unsigned char block[512];
    romaw_image_plan result = {0};
    memset(plan, 0, sizeof *plan);
    if (!file || fseek(file, 0, SEEK_END))
        return 0;
    long end = ftell(file);
    if (end < 1536 || (uint64_t)end > INT32_MAX || !read_block(file, 0, block))
        return 0;
    uint64_t base = 0, length = (uint64_t)end;
    /* DiskCopy's 84-byte header describes the data fork; tag data follows it. */
    if (block[0] <= 63 && block[82] == 1 && block[83] == 0 && be32(block + 64) >= 1536 &&
        (uint64_t)be32(block + 64) + be32(block + 68) + 84 == length) {
        base = 84;
        length = be32(block + 64);
    }
    if (hfs_volume(file, base, length)) {
        result.offset[result.count++] = (uint32_t)base;
        *plan = result;
        return 1;
    }
    if (!read_block(file, base, block))
        return 0;
    unsigned sector = be16(block) == 0x4552 ? be16(block + 2) : 512;
    if (sector < 512 || sector > 32768 || sector % 512 || !read_block(file, base + sector, block) ||
        be16(block) != 0x504d)
        return 0;
    uint32_t entries = be32(block + 4);
    if (!entries || entries > 4096 || ((uint64_t)entries + 1) * sector > length)
        return 0;
    for (uint32_t i = 1; i <= entries; ++i) {
        if (!read_block(file, base + (uint64_t)i * sector, block) || be16(block) != 0x504d ||
            be32(block + 4) != entries)
            return 0;
        uint64_t start = (uint64_t)be32(block + 8) * sector;
        uint64_t size = (uint64_t)be32(block + 12) * sector;
        if (start > length || size > length - start)
            return 0;
        if (memcmp(block + 48, "Apple_HFS", 10))
            continue;
        if (result.count == ROMAW_IMAGE_VOLUMES || !hfs_volume(file, base + start, size))
            return 0;
        result.offset[result.count++] = (uint32_t)(base + start);
    }
    if (!result.count)
        return 0;
    *plan = result;
    return 1;
}
