/*+===================================================================
  File:      diskimage_test.c

  Summary:   Implements diskimage test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include "rsys/diskimage.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void put16(unsigned char* p, unsigned n)
{
    p[0] = n >> 8;
    p[1] = n;
}

static void put32(unsigned char* p, uint32_t n)
{
    put16(p, n >> 16);
    put16(p + 2, n);
}

static void volume(unsigned char* mdb)
{
    put16(mdb, 0x4244);
    put16(mdb + 14, 3);
    put16(mdb + 18, 100);
    put32(mdb + 20, 512);
    put16(mdb + 28, 4);
    mdb[36] = 4;
    memcpy(mdb + 37, "Test", 4);
    put32(mdb + 130, 512);
    put16(mdb + 136, 1);
    put32(mdb + 146, 512);
    put16(mdb + 152, 1);
}

static romaw_image_plan probe(const unsigned char* data, size_t length, int expected)
{
    FILE* file = tmpfile();
    assert(file);
    assert(fwrite(data, 1, length, file) == length);
    romaw_image_plan plan;
    memset(&plan, 0xff, sizeof plan);
    assert(romaw_image_probe(file, &plan) == expected);
    if (!expected)
        assert(!plan.count);
    fclose(file);
    return plan;
}

int main(void)
{
    const unsigned size = 128 * 512;
    unsigned char* data = calloc(1, size + 84);
    assert(data);
    volume(data + 1024);
    romaw_image_plan plan = probe(data, size, 1);
    assert(plan.count == 1 && plan.offset[0] == 0);
    probe(data, 0, 0);
    probe(data, 1025, 0);
    probe(data, 32768, 0);
    put32(data + 1044, 0);
    probe(data, size, 0);
    put32(data + 1044, 513);
    probe(data, size, 0);
    put32(data + 1044, 0xffffffff);
    probe(data, size, 0);
    volume(data + 1024);
    data[1060] = 28;
    probe(data, size, 0);
    volume(data + 1024);
    put16(data + 1160, 101);
    probe(data, size, 0);
    volume(data + 1024);
    memmove(data + 84, data, size);
    memset(data, 0, 84);
    put32(data + 64, size);
    data[82] = 1;
    plan = probe(data, size + 84, 1);
    assert(plan.offset[0] == 84);
    probe(data, size + 83, 0);
    memset(data, 0, size + 84);
    put16(data, 0x4552);
    put16(data + 2, 2048);
    unsigned char* map = data + 2048;
    put16(map, 0x504d);
    put32(map + 4, 1);
    put32(map + 8, 2);
    put32(map + 12, 30);
    memcpy(map + 48, "Apple_HFS", 10);
    volume(data + 4096 + 1024);
    plan = probe(data, size, 1);
    assert(plan.offset[0] == 4096);
    put32(map + 12, 0xffffffff);
    probe(data, size, 0);
    put32(map + 12, 30);
    put32(map + 4, 4097);
    probe(data, size, 0);
    put32(map + 4, 2);
    probe(data, size, 0);
    put32(map + 4, 1);
    memcpy(map + 48, "Apple_HFSX", 10);
    probe(data, size, 0);
    memcpy(map + 48, "Apple_HFS", 10);
    put16(data + 2, 0);
    probe(data, size, 0);
    memset(data, 0, size);
    memcpy(data + 32769, "CD001", 5);
    probe(data, size, 0);
    free(data);
    puts("Disk-image probe: raw HFS, DiskCopy, partitioned CD images and malformed inputs passed");
}
