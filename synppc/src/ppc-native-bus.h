/*+===================================================================
  File:      ppc-native-bus.h

  Summary:   Keep checked physical callbacks and floating-state restoration outside emitted native frames.

  Classes:   romaw_ppc_native_bus_scope; uses floating_environment.

  Functions: Interface declarations; see romaw_ppc_native_bus_scope.

  Origin:    Original ARDI Executor native PowerPC memory callback boundary.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ROMAW_PPC_NATIVE_BUS_H
#define ROMAW_PPC_NATIVE_BUS_H
#include "synppc/bus750.h"
#include "ppc-floating.h"
#include <exception>

class romaw_ppc_native_bus_scope {
    const ROMlib_ppc_physical_bus* original;
    ROMlib_ppc_physical_bus checked;
    const uint32_t* serial;
    unsigned aborted = 0;
    std::exception_ptr exception;
    floating_environment host;
    uint32_t host_serial = 0;
    bool host_valid = false;

    template <class Callback> int invoke(Callback callback) noexcept;
    static int transfer(void*, uint32_t, void*, unsigned, unsigned, unsigned, uint32_t*);
    static int history(void*, uint32_t, uint32_t, uint32_t*);

  public:
    romaw_ppc_native_bus_scope(const ROMlib_ppc_physical_bus*, const uint32_t* packet_serial) noexcept;
    ~romaw_ppc_native_bus_scope();

    const ROMlib_ppc_physical_bus* bus() const noexcept
    {
        return original ? &checked : nullptr;
    }

    void rethrow() const;
    romaw_ppc_native_bus_scope(const romaw_ppc_native_bus_scope&) = delete;
    romaw_ppc_native_bus_scope& operator=(const romaw_ppc_native_bus_scope&) = delete;
};
#endif
