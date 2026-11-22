/*+===================================================================
  File:      ppc-native-memory-code.h

  Summary:   Own executable storage and entry points for a closed native 750 memory kernel.

  Classes:   ppc_native_memory_kernel.

  Functions: Interface declarations; see ppc_native_memory_kernel.

  Origin:    Original ARDI Executor native PowerPC memory lowering.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ROMAW_PPC_NATIVE_MEMORY_CODE_H
#define ROMAW_PPC_NATIVE_MEMORY_CODE_H
#include "ppc-native-memory.h"
#include <cstddef>

class ppc_native_memory_kernel {
    void* code = nullptr;
    size_t size = 0;

  public:
    enum operation {
        read,
        write,
        reserve,
        conditional,
        external,
        translate,
        cache,
        sync,
        snoop,
        msr,
        spr,
        operation_count
    };

    using entry = void (*)(ppc_native_memory_frame*);
    ppc_native_memory_kernel() noexcept;
    ~ppc_native_memory_kernel();
    entry get(operation) const noexcept;
    ppc_native_memory_kernel(const ppc_native_memory_kernel&) = delete;
    ppc_native_memory_kernel& operator=(const ppc_native_memory_kernel&) = delete;
};
#endif
