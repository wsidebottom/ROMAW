/*+===================================================================
  File:      fpu-oracle.c

  Summary:   Implements fpu oracle support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include <stdio.h>
extern unsigned fpu_oracle(void* output);

int main(void)
{
    unsigned char output[4096] = {0};
    unsigned count = fpu_oracle(output);
    if (count > sizeof output)
        return 1;
    return fwrite(output, 1, count, stdout) != count;
}
