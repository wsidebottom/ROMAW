/*+===================================================================
  File:      power_manager.h

  Summary:   Marshal native Carbon Power Manager activity calls through live OS trap vectors.

  Classes:   None.

  Functions: Private frame adapters for the Carbon export registry.

  Origin:    Universal Interfaces 3.4.2 Power.h and the System 9 PowerMgrLib guest wrappers.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_POWER_MANAGER_H
#define ROMAW_CARBON_POWER_MANAGER_H

#include "carbonlib.h"

static int carbon_IdleUpdate(ROMlib_carbon_ppc_frame* call)
{
    const ROMlib_carbon_ppc_frame arguments = {0};
    return ROMlib_carbon_call_register_vector(0x0614, 0x71832, 0xa285, 1, &arguments, &call->words[0]);
}

static int carbon_UpdateSystemActivity(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_ppc_frame arguments = {0};
    arguments.words[0] = ((uint32_t)(uint8_t)call->words[0] << 16) | 0x24;
    return ROMlib_carbon_call_register_vector(0x0678, 0x71822, 0xa09e, 1, &arguments, &call->words[0]);
}

static int carbon_GetLastActivity(ROMlib_carbon_ppc_frame* call)
{
    if (!call->words[0] || !ROMlib_carbon_try_guest_memory(call->words[0], 6, 1)) {
        call->words[0] = paramErr;
        return 1;
    }
    ROMlib_carbon_ppc_frame arguments = {0};
    arguments.words[0] = 0x28;
    arguments.words[1] = call->words[0];
    return ROMlib_carbon_call_register_vector(0x0678, 0x2671822, 0xa09e, 1, &arguments, &call->words[0]);
}

#endif
