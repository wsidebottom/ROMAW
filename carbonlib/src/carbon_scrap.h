/*+===================================================================
  File:      carbon_scrap.h

  Summary:   Declares native Carbon Scrap Manager operations.

  Classes:   None.

  Functions: Declares ROMlib_carbon_scrap and the export wrappers.

  Origin:    CarbonLib 1.4 Scrap Manager and Universal Interfaces 3.4.2 Scrap.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_SCRAP_H
#define ROMAW_CARBON_SCRAP_H

#include "internal.h"

enum {
    carbon_scrap_get,
    carbon_scrap_clear,
    carbon_scrap_put,
    carbon_scrap_promises,
    carbon_scrap_keeper,
    carbon_scrap_count,
    carbon_scrap_list,
    carbon_scrap_flags,
    carbon_scrap_size,
    carbon_scrap_data,
};

int ROMlib_carbon_scrap(const char* name, uint32_t offset, int operation, ROMlib_carbon_ppc_frame* call);

#ifdef CARBON_SCRAP_EXPORTS

#define CARBON_SCRAP(name, offset, operation)                       \
    static int carbon_scrap_##name(ROMlib_carbon_ppc_frame* call)   \
    {                                                               \
        return ROMlib_carbon_scrap(#name, offset, operation, call); \
    }

CARBON_SCRAP(GetCurrentScrap, 0x52290, carbon_scrap_get)
CARBON_SCRAP(ClearCurrentScrap, 0x52310, carbon_scrap_clear)
CARBON_SCRAP(PutScrapFlavor, 0x52460, carbon_scrap_put)
CARBON_SCRAP(CallInScrapPromises, 0x51d78, carbon_scrap_promises)
CARBON_SCRAP(SetScrapPromiseKeeper, 0x52608, carbon_scrap_keeper)
CARBON_SCRAP(GetScrapFlavorCount, 0x526a4, carbon_scrap_count)
CARBON_SCRAP(GetScrapFlavorInfoList, 0x527e8, carbon_scrap_list)
CARBON_SCRAP(GetScrapFlavorFlags, 0x529cc, carbon_scrap_flags)
CARBON_SCRAP(GetScrapFlavorSize, 0x52b30, carbon_scrap_size)
CARBON_SCRAP(GetScrapFlavorData, 0x52c98, carbon_scrap_data)

#undef CARBON_SCRAP
#endif

#endif
