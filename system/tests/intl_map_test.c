/*+===================================================================
  File:      intl_map_test.c

  Summary:   Implements intl map test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "rsys/intl_map.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    const unsigned char data[] = {
        0, 1,  0, 0,  0, 2,                           /* version, format, table count */
        0, 0,  0, 30, 0, 0, 0, 14,                    /* script table */
        0, 0,  0, 44, 0, 0, 0, 22,                    /* language table */
        0, 0,  0, 0,  0, 0, 0, 0,                     /* no region table */
        0, 64, 0, 0,  0, 2,                           /* max script, default language, count */
        0, 0,  0, 4,  0, 1, 0, 11, 0, 11, 0, 0, 0, 4, /* max language, default script, count */
        0, 4,  0, 0,  0, 0, 0, 0,  0, 11, 0, 1, 0, 2, 0, 0};
    romaw_intl_table scripts, languages;
    unsigned associated;
    assert(sizeof data == 66);
    assert(intl_map_table(data, sizeof data, 0, &scripts));
    assert(intl_map_table(data, sizeof data, 1, &languages));
    assert(intl_map_entry(&languages, 4, &associated) == 0 && !associated);
    assert(intl_language_order(4, 0, 0, 0, 0, &scripts, &languages) == -1);
    assert(intl_language_order(2, 4, 0, 0, 0, &scripts, &languages) == 1);
    assert(intl_language_order(11, 2, 0, 0, 0, &scripts, &languages) == 1);
    assert(!intl_language_order(-2, 0, 0, 0, 0, &scripts, &languages));
    assert(intl_language_order(-3, 0, 0, 0, 0, &scripts, &languages) == -1);
    assert(!intl_language_order(-6, 11, 1, 0, 0, &scripts, &languages));
    assert(!intl_language_order(-7, 4, -1, 0, 0, &scripts, &languages));
    assert(!intl_language_order(-1, -8, 0, 0, 0, &scripts, &languages));
    assert(!intl_script_position(-1, &scripts));
    assert(intl_script_position(-3, &scripts) == 32767);
    assert(intl_script_position(65, &scripts) == 32767);
    for (size_t length = 0; length < sizeof data; ++length)
        assert(!intl_map_table(data, length, 1, &languages));
    for (size_t length = 0; length < 128; ++length) {
        unsigned char* random = malloc(length ? length : 1);
        for (unsigned trial = 0; trial < 500; ++trial) {
            for (size_t i = 0; i < length; ++i)
                random[i] = rand();
            if (intl_map_table(random, length, trial & 1, &languages))
                intl_map_entry(&languages, trial, &associated);
        }
        free(random);
    }
    puts("PASS: itlm language order, script grouping, selectors and bounded tables");
    return 0;
}
