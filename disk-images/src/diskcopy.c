/*+===================================================================
  File:      diskcopy.c

  Summary:   Implements diskcopy support for disk images package support.

  Classes:   None.

  Functions: romaw_images_dc42_sync

  Origin:    Existing repository source in packages/disk-images/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include "romaw_images.h"
#include <limits.h>

static uint32_t big32(const unsigned char* p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

int romaw_images_dc42_sync(FILE* file)
{
    unsigned char header[84], buffer[65536];
    if (!file || fflush(file) || fseek(file, 0, SEEK_SET) || fread(header, 1, sizeof header, file) != sizeof header)
        return 0;
    uint32_t size = big32(header + 64), tags = big32(header + 68);
    if (header[0] > 63 || header[82] != 1 || header[83] || !size || size % 512 || size > INT32_MAX - 84 ||
        tags > INT32_MAX - 84 - size)
        return 1; /* A legacy 84-byte prefix need not be DiskCopy 4.2. */
    if (fseek(file, 0, SEEK_END) || ftell(file) != (long)(84 + size + tags) || fseek(file, 84, SEEK_SET))
        return 0;
    uint32_t checksum = 0;
    for (uint32_t at = 0; at < size;) {
        size_t count = size - at > sizeof buffer ? sizeof buffer : size - at;
        if (fread(buffer, 1, count, file) != count)
            return 0;
        for (size_t i = 0; i < count; i += 2) {
            checksum += (uint32_t)buffer[i] << 8 | buffer[i + 1];
            checksum = (checksum >> 1) | (checksum << 31);
        }
        at += (uint32_t)count;
    }
    unsigned char bytes[] = {checksum >> 24, checksum >> 16, checksum >> 8, checksum};
    return !fseek(file, 72, SEEK_SET) && fwrite(bytes, 1, 4, file) == 4 && !fflush(file);
}
