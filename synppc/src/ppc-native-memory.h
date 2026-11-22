/*+===================================================================
  File:      ppc-native-memory.h

  Summary:   Define decoded operand packets for closed native 750 memory templates.

  Classes:   ppc_native_memory_frame, ppc_native_memory_relocation.

  Functions: No externally linked definitions.

  Origin:    Original ARDI Executor native PowerPC memory lowering.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ROMAW_PPC_NATIVE_MEMORY_H
#define ROMAW_PPC_NATIVE_MEMORY_H
#include "synppc/bus750.h"

// Roots consume decoded operands, never PPC instructions. Device callbacks must
// catch embedding exceptions before returning through these unwind-free frames.
struct ppc_native_memory_frame {
    ROMlib_ppc_system750* system;
    const ROMlib_ppc_physical_bus* bus;
    uint32_t address, size, access, selector;
    uint64_t value;
    ROMlib_ppc_bus_result result;
    int ok;
    uint32_t serial;
};

struct ppc_native_memory_relocation {
    uint32_t offset, target;
};
#endif
