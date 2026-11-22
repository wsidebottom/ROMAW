/*+===================================================================
  File:      fpu-arithmetic-test.c

  Summary:   Implements fpu arithmetic test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Protocol fixture for the independent Python integer-rational oracle. */
#include "fpu_arithmetic.h"
#include <stdio.h>

int main(void)
{
    unsigned ah, bh, precision, mode, operation;
    unsigned long long al, bl;
    while (scanf("%x %llx %x %llx %u %u %x", &ah, &al, &bh, &bl, &precision, &mode, &operation) == 7) {
        floatx80 a = {ah, al}, b = {bh, bl};
        float_exception_flags = 0;
        float_rounding_mode = mode;
        floatx80_rounding_precision = precision;
        int a_exponent = (ah & 0x7fff) - (ah & 0x10000 ? 0x8000 : 0);
        int b_exponent = (bh & 0x7fff) - (bh & 0x10000 ? 0x8000 : 0);
        FPUdivision d = fpu_calculate_ex(a, b, operation, precision, a_exponent, b_exponent);
        printf("%04x %016llx %04x %016llx %u %u %u %u %u\n", d.result.high, (unsigned long long)d.result.low,
               d.writeback.high, (unsigned long long)d.writeback.low, d.status, d.grs, d.sticky, d.exponent15,
               (unsigned)float_exception_flags);
    }
    return ferror(stdin) || ferror(stdout);
}
