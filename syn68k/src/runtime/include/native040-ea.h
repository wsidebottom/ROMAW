/*+===================================================================
  File:      native040-ea.h

  Summary:   Define invocation-local CPU and mapped addressing state for native effective addresses.

  Classes:   None.

  Functions: None; private packet definitions.

  Origin:    Original ARDI Executor direct 68040 lowering.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SYN68K_NATIVE040_EA_H
#define SYN68K_NATIVE040_EA_H
#include "ea.h"
#include "native040.h"
#include "integer.h"

typedef struct Syn68kNative040EA {
    CPUState* cpu;
    Syn68kEAContext context;
    unsigned code, size;
    int writing, registers;
    Syn68kEA result;
    Syn68kNative040RAM ram;
} Syn68kNative040EA;

typedef struct {
    Syn68kNative040EA ea;
    uint32 pc, resume_address;
    unsigned opcode, resuming;
    Syn68kIntegerStep step;
} Syn68kNative040IntegerEA;
#endif
