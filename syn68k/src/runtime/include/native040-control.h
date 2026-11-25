/*+===================================================================
  File:      native040-control.h

  Summary:   Define the invocation packet for closed native MOVEC and STOP status transfers.

  Classes:   None.

  Functions: Private invocation data declarations.

  Origin:    Original ARDI Executor direct 68040 control-register lowering.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SYN68K_NATIVE040_CONTROL_H
#define SYN68K_NATIVE040_CONTROL_H
#include "syn68k_private.h"
#include "mmu040-memory.h"

typedef struct {
    CPUState* cpu;
    Syn68kMMU040Memory* memory;
    unsigned command;
    int ok;
} Syn68kNative040Control;
#endif
