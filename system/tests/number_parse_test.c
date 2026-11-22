/*+===================================================================
  File:      number_parse_test.c

  Summary:   Implements number parse test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "rsys/common.h"
#include <assert.h>
#include "../../options/parsenum.c"
#undef main

int main(void)
{
    struct {
        const char* text;
        unsigned round;
        int32 expected;
    } valid[] = {{"0", 1, 0},
                 {"+12", 1, 12},
                 {"-12", 1, -12},
                 {"1.5K", 1, 1536},
                 {"0x12AC", 1, 4780},
                 {"0b010.10K", 1, 2560},
                 {".5", 1, 0},
                 {"5.", 1, 5},
                 {"1.5M", 1, 1572864},
                 {"-1.5K", 1024, -2048},
                 {"12", 0, 12},
                 {"2147483647", 1, 2147483647},
                 {"-2147483648", 1, (-2147483647 - 1)},
                 {"0x7FFFFFFF", 1, 2147483647},
                 {"-0x80000000", 1, (-2147483647 - 1)},
                 {"1073741824", 1073741824, 1073741824},
                 {"0", 4294967295U, 0}};

    const char* invalid[] = {"",
                             "+",
                             "-",
                             "K",
                             "+K",
                             "-M",
                             ".",
                             ".K",
                             "0x",
                             "0b",
                             "0x.",
                             "1..2",
                             " 1",
                             "1 ",
                             "1junk",
                             "\xFF",
                             "2147483648",
                             "-2147483649",
                             "9223372036854775808K",
                             "18446744073709551616",
                             "9999999999999999999999999999",
                             "0.00000000000000000000000001",
                             "2147483647M"};
    int32 result;
    int i;
    char* large;
    for (i = 0; i < sizeof valid / sizeof *valid; ++i) {
        assert(parse_number(valid[i].text, &result, valid[i].round));
        assert(result == valid[i].expected);
    }
    for (i = 0; i < sizeof invalid / sizeof *invalid; ++i) {
        result = 123;
        assert(!parse_number(invalid[i], &result, 1) && result == 0);
    }
    assert(!parse_number("2147483647", &result, 4096) && result == 0);
    assert(!parse_number("1", &result, 4294967295U) && result == 0);
    assert(!parse_number(NULL, &result, 1) && result == 0);
    assert(!parse_number("1", NULL, 1));
    large = malloc(1000002);
    assert(large);
    memset(large, '0', 1000000);
    strcpy(large + 1000000, "1");
    assert(parse_number(large, &result, 1) && result == 1);
    free(large);
    puts("PASS: numeric bases, fractions, suffixes, signed boundaries, rounding overflow and long inputs");
    return 0;
}
