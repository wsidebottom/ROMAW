/*+===================================================================
  File:      bounded_names_test.c

  Summary:   Implements bounded names test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "rsys/common.h"
#include "FileMgr.h"
#include <assert.h>
#undef warning_unexpected
#define warning_unexpected(...) ((void)0)
#include "../../text/string.c"
#include "../../files/fileHighlevel.c"
#undef main

int main(void)
{
    struct {
        unsigned char before;
        unsigned char name[28];
        unsigned char after;
    } volume;

    struct {
        unsigned char before;
        Str63 name;
        unsigned char after;
    } file;

    Str255 path;
    char long_name[301];
    int n;
    memset(long_name, 'x', 300);
    long_name[300] = 0;
    memset(&volume, 0xA5, sizeof volume);
    pstr_from_c_string(volume.name, sizeof volume.name, long_name);
    assert(volume.before == 0xA5 && volume.after == 0xA5);
    assert(volume.name[0] == 27 && !memcmp(volume.name + 1, long_name, 27));
    pstr_from_c_string(volume.name, sizeof volume.name, "");
    assert(!volume.name[0]);
    for (n = 0; n <= 255; ++n) {
        memset(&file, 0xA5, sizeof file);
        path[0] = n;
        memset(path + 1, 'x', n);
        if (n <= 63) {
            assert(extract_name(file.name, path) == noErr);
            assert(file.name[0] == n && !memcmp(file.name + 1, path + 1, n));
        }
        else {
            assert(extract_name(file.name, path) == bdNamErr);
            assert(file.name[0] == 0xA5);
        }
        assert(file.before == 0xA5 && file.after == 0xA5);
    }
    str255_from_c_string(path, "Volume:Folder:Book:");
    assert(extract_name(file.name, path) == noErr);
    assert(file.name[0] == 4 && !memcmp(file.name + 1, "Book", 4));
    str255_from_c_string(path, ":");
    assert(extract_name(file.name, path) == noErr && !file.name[0]);
    puts("PASS: bounded volume aliases and FSSpec names, lengths 0 through 255");
    return 0;
}
