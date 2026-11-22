/*+===================================================================
  File:      ppc-native-fused-state.h

  Summary:   Defines the private operand and result frame for emitted exact fused arithmetic.

  Classes:   ppc_native_fused_state.

  Functions: None.

  Origin:    Original ARDI Executor native PowerPC instruction lowering.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ROMAW_PPC_NATIVE_FUSED_STATE_H
#define ROMAW_PPC_NATIVE_FUSED_STATE_H
#include <stdint.h>

struct ppc_native_fused_state {
    uint64_t a, b, c;
    uint32_t fpscr;
    uint64_t result;
};
#endif
