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

#define CARBON_POWER(name, selector, info, argument)                                                 \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call)                                          \
    {                                                                                               \
        uint32_t result;                                                                            \
        if (!power_dispatch((selector) | ((uint32_t)(argument) << 16), info, 0, &result))              \
            return 0;                                                                               \
        call->words[0] = ((info) & 0x30) == 0x10 ? (uint8_t)result : result;                          \
        return 1;                                                                                   \
    }

CARBON_POWER(PMFeatures, 0x01, 0x71832, 0)
CARBON_POWER(GetSleepTimeout, 0x02, 0x71812, 0)
CARBON_POWER(SetSleepTimeout, 0x03, 0x71802, (uint8_t)call->words[0])
CARBON_POWER(GetHardDiskTimeout, 0x04, 0x71812, 0)
CARBON_POWER(SetHardDiskTimeout, 0x05, 0x71802, (uint8_t)call->words[0])
CARBON_POWER(IsSpindownDisabled, 0x08, 0x71812, 0)
CARBON_POWER(SetSpindownDisable, 0x09, 0x71802, (uint8_t)call->words[0])
CARBON_POWER(AutoSleepControl, 0x0d, 0x71802, (uint8_t)call->words[0])
CARBON_POWER(GetIntModemInfo, 0x0e, 0x71832, 0)
CARBON_POWER(MaximumProcessorSpeed, 0x10, 0x71822, 0)
CARBON_POWER(CurrentProcessorSpeed, 0x11, 0x71822, 0)
CARBON_POWER(FullProcessorSpeed, 0x12, 0x71812, 0)
CARBON_POWER(SetProcessorSpeed, 0x13, 0x71812, (uint8_t)call->words[0])
CARBON_POWER(IsProcessorCyclingEnabled, 0x18, 0x71812, 0)
CARBON_POWER(BatteryCount, 0x1a, 0x71822, 0)
CARBON_POWER(GetBatteryVoltage, 0x1b, 0x71832, (uint16_t)call->words[0])
CARBON_POWER(GetDimmingTimeout, 0x1d, 0x71812, 0)
CARBON_POWER(SetDimmingTimeout, 0x1e, 0x71802, (uint8_t)call->words[0])
CARBON_POWER(DimmingControl, 0x1f, 0x71802, (uint8_t)call->words[0])
CARBON_POWER(IsDimmingControlDisabled, 0x20, 0x71812, 0)
CARBON_POWER(IsAutoSlpControlDisabled, 0x21, 0x71812, 0)
CARBON_POWER(GetDimSuspendState, 0x2b, 0x71812, 0)
CARBON_POWER(SetDimSuspendState, 0x2c, 0x71802, (uint8_t)call->words[0])

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

static int carbon_MinimumProcessorSpeed(ROMlib_carbon_ppc_frame* call)
{
    uint32_t result;
    if (!power_dispatch(0x36, 0x71822, 0, &result))
        return 0;
    call->words[0] = (int16_t)result;
    return 1;
}

static int power_timer_dispatch(ROMlib_carbon_ppc_frame* call, uint32_t selector, int setting, int returns_status)
{
    const uint32_t record = call->words[0];
    uint32_t result = paramErr;
    const uint32_t info = returns_status ? 0x2671822 : 0x2671802;
    if (record && ROMlib_carbon_try_guest_memory(record, 6, !setting) &&
        !power_dispatch(selector, info, record, &result))
        return 0;
    if (returns_status)
        call->words[0] = (int16_t)result;
    return 1;
}

static int carbon_GetWakeupTimer(ROMlib_carbon_ppc_frame* call)
{
    return power_timer_dispatch(call, 0x16, 0, 0);
}

static int carbon_SetWakeupTimer(ROMlib_carbon_ppc_frame* call)
{
    return power_timer_dispatch(call, 0x17, 1, 0);
}

static int carbon_GetStartupTimer(ROMlib_carbon_ppc_frame* call)
{
    return power_timer_dispatch(call, 0x26, 0, 1);
}

static int carbon_SetStartupTimer(ROMlib_carbon_ppc_frame* call)
{
    return power_timer_dispatch(call, 0x27, 1, 1);
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
