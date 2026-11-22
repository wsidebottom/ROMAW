/*+===================================================================
  File:      cfx_values.c

  Summary:   Implements CoreFoundation values service entries for the PowerPC bridge.

  Classes:   None.

  Functions: ROMlib_cfx_CFDateCreate, ROMlib_cfx_CFUUIDCreateWithBytes,
             ROMlib_cfx_CFUUIDGetConstantUUIDWithBytes

  Origin:    C reconstruction of the local CoreFoundationLib 1.4 disassembly; each entry verifies its reference offset.
             Public contracts use Universal Interfaces 3.4.2; private ABI notes are in cf-services-batch.json.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "cfx_internal.h"

int ROMlib_cfx_CFDateCreate(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFDateCreate", 0x23424, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 object = cfc_allocate_instance(&reference, CFX_ARG(0), cfp_toc(&reference, 0x8d8), 8);
    if (object && (!cfp_write(cfx_address(object, 8, 8), call_frame->floating[0] >> 32) ||
                   !cfp_write(cfx_address(object, 12, 4), call_frame->floating[0])))
        return 0;
    return CFX_RESULT(object);
}

int ROMlib_cfx_CFUUIDCreateWithBytes(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFUUIDCreateWithBytes", 0x4b7ac, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 words[4] = {0};
    for (unsigned index = 0; index < 16; ++index)
        words[index / 4] = (words[index / 4] << 8) | (CFX_ARG(index + 1) & 255);
    return CFX_RESULT(CFX_PRIVATE(0x4b684, CFX_ARG(0), words[0], words[1], words[2], words[3], 0));
}

int ROMlib_cfx_CFUUIDGetConstantUUIDWithBytes(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFUUIDGetConstantUUIDWithBytes", 0x4bee8, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 words[4] = {0};
    for (unsigned index = 0; index < 16; ++index)
        words[index / 4] = (words[index / 4] << 8) | (CFX_ARG(index + 1) & 255);
    return CFX_RESULT(CFX_PRIVATE(0x4b684, CFX_ARG(0), words[0], words[1], words[2], words[3], 1));
}
