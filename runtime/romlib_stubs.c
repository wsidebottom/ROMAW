/*+===================================================================
  File:      romlib_stubs.c

  Summary:   Implements romlib stubs support for Macintosh startup, traps and runtime services.

  Classes:   None.

  Functions: ROMlib_hook, ROMlib_printostype, ROMlib_printstring

  Origin:    Existing repository source in executor/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1988, 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"
#include "rsys/glue.h"

#if !defined(NDEBUG)

#undef putchar

A1(PUBLIC, void, ROMlib_printstring, unsigned char*, p)
{
    int i;

    for (i = *p++; --i >= 0; putchar(*p++))
        ;
    putchar('\n');
}

A1(PUBLIC, void, ROMlib_printostype, OSType, t)
{
    putchar(t >> 24);
    putchar(t >> 16);
    putchar(t >> 8);
    putchar(t >> 0);
    putchar('\n');
}
#endif

#if !defined(NDEBUG)
A1(PUBLIC, void, ROMlib_hook, LONGINT, hn)
{
    /* don't do anything; This is just here for gdbing */
}
#endif /* !defined (NDEBUG) */
