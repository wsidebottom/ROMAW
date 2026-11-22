/*+===================================================================
  File:      alias_record_test.c

  Summary:   Implements alias record test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "rsys/alias_record.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    unsigned char record[256] = {0}, output[65];
    const char path[] = "Disk:One:Two:File";
    size_t n = sizeof path - 1, size = 150 + 4 + n + (n & 1) + 4;
    record[4] = size >> 8;
    record[5] = size;
    record[7] = 2;
    memcpy(record + 10, "\4Disk", 5);
    memcpy(record + 50, "\4File", 5);
    record[151] = 2;
    record[153] = n;
    memcpy(record + 154, path, n);
    record[size - 4] = record[size - 3] = 255;
    for (int index = -3; index <= 4; ++index) {
        const char* expected = index == -1  ? "Disk"
                               : index == 0 ? "File"
                               : index == 1 ? "Two"
                               : index == 2 ? "One"
                                            : "";
        memset(output, 0xa5, sizeof output);
        assert(!alias_record_info(record, size, index, output));
        assert(output[0] == strlen(expected));
        assert(!memcmp(output + 1, expected, output[0]));
        assert(output[64] == 0xa5);
    }
    for (size_t length = 0; length < size; ++length)
        assert(alias_record_info(record, length, 0, output) == -50);
    record[153] = 255;
    assert(alias_record_info(record, size, 0, output) == -50);
    record[153] = n;
    assert(alias_record_info(record, size, -4, output) == -50);
    assert(alias_record_info(NULL, size, 0, output) == -50);
    memset(record + 150, 0, sizeof record - 150);
    size = 150 + 4 + 80 + 4;
    record[4] = size >> 8;
    record[5] = size;
    record[137] = 16;
    record[151] = 9;
    record[153] = 80;
    record[154 + 13] = 30;
    record[154 + 15] = 60;
    memcpy(record + 154 + 30, "\4Zone", 5);
    memcpy(record + 154 + 60, "\6Server", 7);
    record[size - 4] = record[size - 3] = 255;
    assert(!alias_record_info(record, size, -3, output));
    assert(!memcmp(output, "\4Zone", 5));
    assert(!alias_record_info(record, size, -2, output));
    assert(!memcmp(output, "\6Server", 7));
    record[137] = 0;
    assert(!alias_record_info(record, size, -2, output) && !output[0]);
    {
        unsigned char relative[256];
        const unsigned char* path = (const unsigned char*)"Disk:One:Two:File";
        const unsigned char* leaf = (const unsigned char*)"\4File";
        const char* expected[] = {":File", ":Two:File", ":One:Two:File"};
        for (int level = 1; level <= 3; ++level) {
            assert(alias_relative_path(path, 17, level, leaf, relative));
            assert(relative[0] == strlen(expected[level - 1]));
            assert(!memcmp(relative + 1, expected[level - 1], relative[0]));
        }
        assert(!alias_relative_path(path, 17, 4, leaf, relative));
        assert(!alias_relative_path(path, 17, -1, leaf, relative));
        assert(!alias_relative_path(path, 17, 129, leaf, relative));
        assert(!alias_relative_path(NULL, 0, 1, leaf, relative));
        assert(!alias_relative_path((const unsigned char*)"Disk::File", 10, 1, leaf, relative));
    }
    record[137] = 16;
    record[154 + 15] = 255;
    assert(!alias_record_info(record, size, -2, output) && !output[0]);
    puts("PASS: alias names, parent traversal and malformed/truncated records");
    return 0;
}
