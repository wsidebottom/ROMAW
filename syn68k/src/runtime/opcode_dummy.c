/*+===================================================================
  File:      opcode_dummy.c

  Summary:   Implements opcode dummy support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: s68k_handle_opcode_dummy

  Origin:    Existing repository source in syn68k/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#include <stdio.h>
#include <stdlib.h>

/*
 * s68k_handle_opcode_dummy is used to detect portions of assembly in
 * syn68k.s that are unreachable and should be removed via
 * i486-cleanup.pl.  The call to s68k_handle_opcode_dummy should never
 * be made.  It won't be reachable even if i486-cleanup.pl isn't run,
 * but it will be assembled and linked in, so that's why we actually
 * supply s68k_handle_opcode_dummy but expect it to never be called.
 */

void s68k_handle_opcode_dummy(void)
{
    fprintf(stderr, "This function should never be called\n");
    abort();
}
