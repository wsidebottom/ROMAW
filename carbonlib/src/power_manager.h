/*+===================================================================
  File:      power_manager.h

  Summary:   Marshal Carbon Power Manager calls through live OS trap vectors.

  Classes:   None.

  Functions: Private frame adapters for the Carbon export registry.

  Origin:    Universal Interfaces 3.4.2 Power.h and the System 9 PowerMgrLib guest wrappers.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_POWER_MANAGER_H
#define ROMAW_CARBON_POWER_MANAGER_H

#include "carbonlib.h"
#include <string.h>

static int power_dispatch(uint32_t selector, uint32_t info, uint32_t pointer, uint32_t* result)
{
    const ROMlib_carbon_ppc_frame arguments = {.words = {selector, pointer}};
    return ROMlib_carbon_call_register_vector(0x0678, info, 0xa09e, 1, &arguments, result);
}

#define CARBON_POWER(name, selector, info, argument)                                                \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call)                                         \
    {                                                                                               \
        return power_dispatch((selector) | ((uint32_t)(argument) << 16), info, 0, &call->words[0]); \
    }

CARBON_POWER(PMFeatures, 0x01, 0x71832, 0)
CARBON_POWER(GetIntModemInfo, 0x0e, 0x71832, 0)
CARBON_POWER(MaximumProcessorSpeed, 0x10, 0x71822, 0)
CARBON_POWER(CurrentProcessorSpeed, 0x11, 0x71822, 0)
CARBON_POWER(FullProcessorSpeed, 0x12, 0x71812, 0)
CARBON_POWER(SetProcessorSpeed, 0x13, 0x71812, (uint8_t)call->words[0])
CARBON_POWER(IsProcessorCyclingEnabled, 0x18, 0x71812, 0)
CARBON_POWER(BatteryCount, 0x1a, 0x71822, 0)
CARBON_POWER(GetBatteryVoltage, 0x1b, 0x71832, (uint16_t)call->words[0])

#undef CARBON_POWER

static int carbon_GetScaledBatteryInfo(ROMlib_carbon_ppc_frame* call)
{
    uint32_t* output = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], sizeof *output, 1) : NULL;
    if (!output)
        return 1;
    uint32_t result;
    const uint32_t selector = (call->words[0] << 16) | 0x0c;
    if (!power_dispatch(selector, 0x2671832, call->words[1], &result))
        return 0;
    const uint32_t encoded = CL(result);
    memcpy(output, &encoded, sizeof encoded);
    return 1;
}

static int carbon_GetBatteryTimes(ROMlib_carbon_ppc_frame* call)
{
    uint32_t result;
    const uint32_t output = call->words[1];
    if (!output || !ROMlib_carbon_try_guest_memory(output, 4 * sizeof(uint32_t), 1))
        return 1;
    return power_dispatch((call->words[0] << 16) | 0x1c, 0x2671802, output, &result);
}

static int carbon_IdleUpdate(ROMlib_carbon_ppc_frame* call)
{
    const ROMlib_carbon_ppc_frame arguments = {0};
    return ROMlib_carbon_call_register_vector(0x0614, 0x71832, 0xa285, 1, &arguments, &call->words[0]);
}

static int carbon_UpdateSystemActivity(ROMlib_carbon_ppc_frame* call)
{
    return power_dispatch(((uint32_t)(uint8_t)call->words[0] << 16) | 0x24, 0x71822, 0, &call->words[0]);
}

static int carbon_GetLastActivity(ROMlib_carbon_ppc_frame* call)
{
    if (!call->words[0] || !ROMlib_carbon_try_guest_memory(call->words[0], 6, 1)) {
        call->words[0] = paramErr;
        return 1;
    }
    return power_dispatch(0x28, 0x2671822, call->words[0], &call->words[0]);
}

#endif
