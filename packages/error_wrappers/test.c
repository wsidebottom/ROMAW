/*+===================================================================
  File:      test.c

  Summary:   Implements test support for error_wrappers package support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in packages/error_wrappers; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include <stdio.h>

#include "error.h"

int main(void)
{
    struct foo** h;

    h = NewHandle(100);

    HANDLE_CHECK(h);
    DisposHandle(h);

    if (1)
        ERROR_CHECK_INHIBIT();
    fprintf(stderr, "neato\n");
    ERROR_CHECK_RESTORE();
    else fprintf(stderr, "ick\n");

    return 0;
}
