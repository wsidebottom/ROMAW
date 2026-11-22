/*+===================================================================
  File:      native040-registers.h

  Summary:   Define raw floating-register list transfers and captured pending writeback data through the mapped bus.

  Classes:   None.

  Functions: None; build-time native packet definitions.

  Origin:    Original ARDI Executor direct 68040 register-transfer lowering.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SYN68K_NATIVE040_REGISTERS_H
#define SYN68K_NATIVE040_REGISTERS_H
#include "syn68k_private.h"
#include "native040.h"

typedef struct {
    CPUState* cpu;
    Syn68kMMU040Memory* memory;
    Syn68kNative040RAM ram;
    uint32_t address;
    unsigned mask, reverse;
    Syn68kMMU040Access access;
    int ok;
    /* Captured before a middle-word callback; valid only if that word faults. */
    uint32_t store_tail;
    unsigned store_tail_valid;
} Syn68kNative040Registers;
#endif
