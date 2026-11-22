/*+===================================================================
  File:      cfx_internal.h

  Summary:   Shared call-frame and memory helpers for the CoreFoundation service cohort.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Native reconstruction using the local CoreFoundationLib 1.4 disassembly and Universal Interfaces 3.4.2.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_CFX_INTERNAL_H
#define ROMAW_CARBON_CFX_INTERNAL_H

#include "cfw_internal.h"

int cfx_reference(const char* name, uint32 offset, ROMlib_carbon_ppc_frame* call_frame,
                  ROMlib_carbon_reference* reference);
uint32 cfx_argument(const ROMlib_carbon_ppc_frame* call_frame, unsigned index);
int cfx_result(ROMlib_carbon_ppc_frame* call_frame, uint32 result);
uint32 cfx_address(uint32 base, uint64 offset, size_t length);
unsigned cfx_byte(uint32 address);
uint16 cfx_half(uint32 address);
int cfx_set_half(uint32 address, uint16 value);
uint32 cfx_private(const ROMlib_carbon_reference* reference, uint32 offset, const uint32* arguments, unsigned count);
int cfx_private_frame(const ROMlib_carbon_reference* reference, uint32 offset, ROMlib_carbon_ppc_frame* call_frame);
uint32 cfx_literal(const ROMlib_carbon_reference* reference, unsigned table, unsigned offset);
double cfx_double(const ROMlib_carbon_ppc_frame* call_frame, unsigned index);
int cfx_double_result(ROMlib_carbon_ppc_frame* call_frame, double result);
uint32 cfx_varargs(ROMlib_carbon_ppc_frame* call_frame, unsigned first);

int ROMlib_cfx_CFTimeZoneCopyAbbreviation(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFTimeZoneCopyDefault(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFTimeZoneCopySystem(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFTimeZoneCreate(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFTimeZoneCreateWithTimeIntervalFromGMT(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFTimeZoneGetData(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFTimeZoneGetName(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFTimeZoneGetSecondsFromGMT(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFTimeZoneGetTypeID(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFTimeZoneIsDaylightSavingTime(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFTimeZoneResetSystem(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_cfx_CFTimeZoneSetDefault(ROMlib_carbon_ppc_frame* call_frame);

#define CFX_ARG(index) cfx_argument(call_frame, index)
#define CFX_RESULT(value) cfx_result(call_frame, (uint32)(value))
#define CFX_PRIVATE(offset, ...)                                   \
    cfx_private(&reference, offset, (const uint32[]){__VA_ARGS__}, \
                sizeof((const uint32[]){__VA_ARGS__}) / sizeof(uint32))

/* Export declarations are kept separate until all 100 implementations and their
   grouped validation are ready. These declarations do not register bindings. */
#include "cfx_entries.h"

#endif
