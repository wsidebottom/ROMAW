/*+===================================================================
  File:      mac_storage_test.c

  Summary:   The same byte/extent decoder serves guest File Manager and native Finder.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* The same byte/extent decoder serves guest File Manager and native Finder. */
#include "rsys/mac_storage.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void put(unsigned char* p, uint32_t value)
{
    p[0] = value >> 24;
    p[1] = value >> 16;
    p[2] = value >> 8;
    p[3] = value;
}

int main(void)
{
    unsigned char bytes[160] = {0};
    put(bytes, 0x00051607);
    put(bytes + 4, 0x00020000);
    bytes[25] = 2;
    put(bytes + 26, 9);
    put(bytes + 30, 64);
    put(bytes + 34, 32);
    put(bytes + 38, 2);
    put(bytes + 42, 96);
    put(bytes + 46, 64);
    ROMAWMacStorageEntry entry;
    assert(romaw_mac_storage_find(bytes, 160, 160, 9, 0, &entry));
    assert(entry.offset == 64 && entry.length == 32 && entry.capacity == 32);
    assert(romaw_mac_storage_find(bytes, 160, 160, 2, 0, &entry));
    assert(entry.offset == 96 && entry.length == 64 && entry.capacity == INT32_MAX);
    assert(!romaw_mac_storage_find(bytes, 32, 32, 9, 1, &entry));
    for (size_t size = 0; size < 160; ++size) {
        unsigned char prefix[160];
        memcpy(prefix, bytes, size);
        assert(romaw_mac_storage_find(prefix, size, 160, 2, 0, &entry) == (size >= 50));
    }
    put(bytes + 42, 159);
    assert(!romaw_mac_storage_find(bytes, 160, 160, 2, 0, &entry));
    put(bytes + 42, 12);
    assert(!romaw_mac_storage_find(bytes, 160, 160, 2, 0, &entry));
    put(bytes + 42, UINT32_MAX);
    assert(!romaw_mac_storage_find(bytes, 160, 160, 2, 0, &entry));
    /* The shared encoder must not publish invalid replacements or touch
       payload bytes, even when input overlaps the descriptor table. */
    put(bytes + 42, 96);
    unsigned char before[160];
    memcpy(before, bytes, sizeof bytes);
    ROMAWMacStorageEntry replacement = {2, 100, 20, 0};
    assert(romaw_mac_storage_store(bytes, 160, 160, 1, &replacement));
    assert(romaw_mac_storage_find(bytes, 160, 160, 2, 0, &entry));
    assert(entry.offset == 100 && entry.length == 20);
    assert(!memcmp(bytes, before, 38) && !memcmp(bytes + 50, before + 50, 110));
    memcpy(before, bytes, sizeof bytes);
    replacement.length = 61;
    assert(!romaw_mac_storage_store(bytes, 160, 160, 1, &replacement));
    replacement.offset = 30;
    assert(!romaw_mac_storage_store(bytes, 160, 160, 1, &replacement));
    assert(!romaw_mac_storage_store(bytes, 49, 160, 1, &replacement));
    assert(!romaw_mac_storage_store(bytes, 160, 160, 2, &replacement));
    assert(!memcmp(before, bytes, sizeof bytes));
    memset(bytes, 0, sizeof bytes);
    assert(romaw_mac_storage_find(bytes, 32, 32, 9, 1, &entry));
    assert(entry.offset == 0 && entry.length == 32);
    assert(romaw_mac_storage_find(bytes, 16, 16, 9, 1, &entry));
    assert(!romaw_mac_storage_find(bytes, 15, 15, 9, 1, &entry));
    assert(!romaw_mac_storage_find(bytes, 32, 32, 2, 1, &entry));
    assert(!romaw_mac_storage_find(bytes, 32, 32, 9, 0, &entry));
    assert(!romaw_mac_storage_find(bytes, 32, 100, 9, 1, &entry));
    puts("PASS: shared Macintosh storage extents, compact metadata and truncated headers");
}
