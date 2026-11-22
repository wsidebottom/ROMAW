/*+===================================================================
  File:      ppc-physical-bus.h

  Summary:   Share direct physical RAM access and checked callback failure propagation.

  Classes:   Uses ROMlib_ppc_physical_bus.

  Functions: Internal inline physical RAM, transfer, history and failure helpers.

  Origin:    Original ARDI Executor 750 physical RAM/native memory boundary.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ROMAW_PPC_PHYSICAL_BUS_H
#define ROMAW_PPC_PHYSICAL_BUS_H
#include "synppc/bus750.h"
#include <cstring>

inline unsigned char* romaw_ppc_physical_ram(const ROMlib_ppc_physical_bus* bus, uint32_t address, unsigned size)
{
    if (!bus || !bus->linear_memory || !size || address < bus->linear_address ||
        uint64_t(bus->linear_address) + bus->linear_size > UINT64_C(0x100000000) ||
        uint64_t(address) + size > UINT64_C(0x100000000))
        return nullptr;
    uint32_t relative = address - bus->linear_address;
    if (relative > bus->linear_size || size > bus->linear_size - relative)
        return nullptr;
    return bus->linear_memory + relative;
}

inline bool romaw_ppc_physical_transfer(const ROMlib_ppc_physical_bus* bus, uint32_t address, void* bytes,
                                        unsigned size, unsigned access, unsigned wimg, uint32_t* cause)
{
    auto* ram = !(access & (ROMLIB_PPC_PHYSICAL_EXTERNAL | ROMLIB_PPC_PHYSICAL_CONTROL)) && bytes
                    ? romaw_ppc_physical_ram(bus, address, size)
                    : nullptr;
    if (ram) {
        if (access & ROMLIB_PPC_MMU_WRITE)
            memcpy(ram, bytes, size);
        else
            memcpy(bytes, ram, size);
        *cause = 0;
        return true;
    }
    return bus && bus->transfer && bus->transfer(bus->opaque, address, bytes, size, access, wimg, cause);
}

inline bool romaw_ppc_physical_history(const ROMlib_ppc_physical_bus* bus, uint32_t pte, uint32_t mask, uint32_t* cause)
{
#if defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_4)
    auto* ram = uint64_t(pte) + 8 <= UINT64_C(0x100000000) ? romaw_ppc_physical_ram(bus, pte + 4, 4) : nullptr;
    if (ram && !(reinterpret_cast<uintptr_t>(ram) & 3)) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        mask = __builtin_bswap32(mask);
#endif
        __atomic_fetch_or(reinterpret_cast<uint32_t*>(ram), mask, __ATOMIC_RELAXED);
        *cause = 0;
        return true;
    }
#endif
    return bus && bus->history && bus->history(bus->opaque, pte, mask, cause);
}

inline unsigned romaw_ppc_physical_failure(const ROMlib_ppc_physical_bus* bus, uint32_t& cause)
{
    if (bus && bus->callback_aborted && *bus->callback_aborted) {
        cause = 0;
        return ROMLIB_PPC_MMU_CONFIGURATION;
    }
    return ROMLIB_PPC_MMU_BUS;
}

#endif
