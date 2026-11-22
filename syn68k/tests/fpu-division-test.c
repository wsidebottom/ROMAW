/*+===================================================================
  File:      fpu-division-test.c

  Summary:   Implements fpu division test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Protocol fixture for the independent Python integer-rational oracle. */
#include "fpu_exception.h"
#include <stdio.h>

int main(void)
{
    unsigned ah, bh, precision, mode;
    unsigned long long al, bl;
    while (scanf("%x %llx %x %llx %u %u", &ah, &al, &bh, &bl, &precision, &mode) == 6) {
        floatx80 a = {ah, al}, b = {bh, bl};
        float_exception_flags = 0;
        float_rounding_mode = mode;
        floatx80_rounding_precision = precision;
        FPUdivision d = fpu_divide(a, b, precision);
        printf("%04x %016llx %04x %016llx %u %u %u %u\n", d.result.high, (unsigned long long)d.result.low,
               d.writeback.high, (unsigned long long)d.writeback.low, d.status, d.grs, d.sticky, d.exponent15);
    }
    return ferror(stdin) || ferror(stdout);
}
