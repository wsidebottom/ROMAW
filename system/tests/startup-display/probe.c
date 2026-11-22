/*+===================================================================
  File:      probe.c

  Summary:   Implements probe support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests/startup-display; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include <Quickdraw.h>

int main(void)
{
    InitGraf(&qd.thePort);
    return 0;
}
