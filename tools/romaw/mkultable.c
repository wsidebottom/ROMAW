/*+===================================================================
  File:      mkultable.c

  Summary:   Implements mkultable support for development and source-generation tools.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/src/tools; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>

/*
 * This program builds the replacement table used for quick underlining
 * of text.
 */

typedef unsigned int ULONGINT;

int main()
{
    ULONGINT i, j;
    ULONGINT out;

    printf("static unsigned char ultable[256] = {\n");
    for (i = 0; i < 256; i++) {
        if (!(i & 7))
            printf("    ");
        out = (i << 8) ^ (0xFF << 8);
        for (j = 0; j < 8; j++) {
            if (i & (1 << j)) {
                out &= ~((1 << (j + 9)) | (1 << (j + 7)));
            }
        }
        printf("0x%02x,", out >> 8);
        putchar(((i & 7) != 7) ? ' ' : '\n');
    }
    printf("};\n");
    exit(0);
}
