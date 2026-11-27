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
#include "integer.h"
#include "native040.h"

typedef struct {
    CPUState* cpu;
    Syn68kMMU040Memory* memory;
    unsigned command;
    int ok;
} Syn68kNative040Control;

typedef struct {
    CPUState* cpu;
    Syn68kNative040MMU* memory;
    uint32 pc;
    Syn68kIntegerStep step;
} Syn68kNative040RTE;

typedef struct {
    Syn68kNative040FPU arithmetic;
    CPUState* cpu;
    unsigned size, applied, entry_fpcr;
    uint8 image[100];
} Syn68kNative040Restore;
#endif
