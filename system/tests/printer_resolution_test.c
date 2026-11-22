/*+===================================================================
  File:      printer_resolution_test.c

  Summary:   Implements printer resolution test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "rsys/common.h"
#include <assert.h>
#include "../../options/parseopt.c"
#undef main

int main(void)
{
    const char* invalid[] = {"",
                             "0x300",
                             "300x0",
                             "-1x300",
                             "+300x300",
                             "300",
                             "x300",
                             "300x",
                             "300xx300",
                             "300x300x300",
                             "300X300",
                             " 300x300",
                             "300x300 ",
                             "300x300junk",
                             "32768x300",
                             "300x32768",
                             "90000x300",
                             "300x90000",
                             "999999999999999999999x300",
                             "300x\xFF"};
    INTEGER x, y;
    int i;
    char text[64], *large;
    for (i = 1; i <= 32767; ++i) {
        sprintf(text, "%dx%d", i, 32768 - i);
        assert(parse_prres_opt(&x, &y, text));
        assert(x == i && y == 32768 - i);
    }
    assert(parse_prres_opt(&x, &y, "00300x00600") && x == 300 && y == 600);
    for (i = 0; i < sizeof invalid / sizeof *invalid; ++i) {
        x = 123;
        y = 456;
        assert(!parse_prres_opt(&x, &y, invalid[i]) && x == 123 && y == 456);
    }
    assert(!parse_prres_opt(&x, &y, NULL));
    assert(!parse_prres_opt(NULL, &y, "300x300"));
    assert(!parse_prres_opt(&x, NULL, "300x300"));
    large = malloc(1000010);
    assert(large);
    memset(large, '0', 1000000);
    strcpy(large + 1000000, "300x600");
    assert(parse_prres_opt(&x, &y, large) && x == 300 && y == 600);
    free(large);
    puts("PASS: all 32767 printer resolution values, invalid/overflow inputs and unchanged error outputs");
    return 0;
}
