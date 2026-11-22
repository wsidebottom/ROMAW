/*+===================================================================
  File:      ppc_fused_test.cpp

  Summary:   Implements ppc fused test support for synppc PowerPC execution and fragment loading.

  Classes:   None identified in this file.

  Functions: main

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include "../src/ppc-fused.h"
#include <fenv.h>
#include <stdio.h>
#include <assert.h>

int main()
{
    const int rounding[] = {FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD};
    unsigned single, mode, count = 0;
    unsigned long long a, b, c, expected;
    while (scanf("%u %u %llx %llx %llx %llx", &single, &mode, &a, &b, &c, &expected) == 6) {
        assert(single < 2 && mode < 4);
        assert(!fesetround(rounding[mode]));
        uint64_t actual = romaw_ppc_fused_bits(single, a, b, c);
        if (actual != expected) {
            fprintf(stderr,
                    "Fused case %u (%u/%u) %llx * %llx + %llx: "
                    "%llx expected %llx\n",
                    count, single, mode, a, b, c, (unsigned long long)actual, expected);
            return 1;
        }
        ++count;
    }
    assert(feof(stdin) && count > 10000);
    printf("%u exact-rational fused cases passed.\n", count);
}
