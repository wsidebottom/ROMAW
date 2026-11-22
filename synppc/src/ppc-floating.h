/*+===================================================================
  File:      ppc-floating.h

  Summary:   Declare saved floating state shared by guest execution and checked native callbacks.

  Classes:   floating_environment.

  Functions: floating_environment capture and restore declarations.

  Origin:    Existing floating environment adapter extracted from synppc/src/ppc-runtime.cpp.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ROMAW_PPC_FLOATING_H
#define ROMAW_PPC_FLOATING_H
#include <fenv.h>

struct floating_environment {
    fenv_t environment;
    unsigned mxcsr;
    bool mxcsr_valid;
    int capture(bool hold);
    int restore() const;
};
#endif
