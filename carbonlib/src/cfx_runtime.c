/*+===================================================================
  File:      cfx_runtime.c

  Summary:   Guard the CoreFoundation service profile and transport native worker arguments without losing PPC state.

  Classes:   None.

  Functions: cfx_address, cfx_argument, cfx_byte, cfx_double, cfx_double_result, cfx_half, cfx_literal, cfx_private,
             cfx_private_frame, cfx_reference, cfx_result, cfx_set_half, cfx_varargs

  Origin:    Original native helpers using the existing CarbonLib bridge and the local CoreFoundationLib 1.4 layout.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "cfx_internal.h"

int cfx_reference(const char* name, uint32 offset, ROMlib_carbon_ppc_frame* call_frame,
                  ROMlib_carbon_reference* reference)
{
    if (!call_frame || !reference)
        return ROMlib_carbon_report_failure("Invalid native Carbon service frame");
    if (!ROMlib_carbon_reference_function("CoreFoundationLib", name, reference))
        return 0;
    if (reference->code_offset == offset && reference->code_size == 0x5dee0 && !reference->toc_offset &&
        reference->toc && reference->toc_size == 0x72a8)
        return 1;
    return ROMlib_carbon_invoke_frame(reference->function, call_frame) ? 2 : 0;
}

uint32 cfx_argument(const ROMlib_carbon_ppc_frame* call_frame, unsigned index)
{
    uint32 result = 0;
    ROMlib_carbon_frame_word(call_frame, index, &result);
    return result;
}

int cfx_result(ROMlib_carbon_ppc_frame* call_frame, uint32 result)
{
    call_frame->words[0] = result;
    return 1;
}

uint32 cfx_address(uint32 base, uint64 offset, size_t length)
{
    uint64 address = (uint64)base + offset;
    if (offset > UINT32_MAX || address > UINT32_MAX || length > UINT32_MAX ||
        length > UINT64_C(0x100000000) - address) {
        ROMlib_carbon_report_failure("Native Carbon service address overflow");
        return 0;
    }
    return address;
}

unsigned cfx_byte(uint32 address)
{
    const Byte* value = ROMlib_carbon_guest_memory(address, 1, 0);
    return value ? *value : 0;
}

uint16 cfx_half(uint32 address)
{
    const Byte* value = ROMlib_carbon_guest_memory(address, 2, 0);
    return value ? (uint16)(((uint16)value[0] << 8) | value[1]) : 0;
}

int cfx_set_half(uint32 address, uint16 value)
{
    Byte* output = ROMlib_carbon_guest_memory(address, 2, 1);
    if (!output)
        return 0;
    output[0] = value >> 8;
    output[1] = value;
    return 1;
}

uint32 cfx_private(const ROMlib_carbon_reference* reference, uint32 offset, const uint32* arguments, unsigned count)
{
    uint32 result = 0;
    cfp_private(reference, offset, arguments, count, &result);
    return result;
}

int cfx_private_frame(const ROMlib_carbon_reference* reference, uint32 offset, ROMlib_carbon_ppc_frame* call_frame)
{
    uint32 entry;
    memcpy(&entry, reference->function, sizeof entry);
    uint64 target = (uint64)CL(entry) - reference->code_offset + offset;
    if (CL(entry) < reference->code_offset || offset >= reference->code_size || target > UINT32_MAX)
        return ROMlib_carbon_report_failure("Native Carbon service worker address overflow");
    _Alignas(16) uint32 vector[2] = {CL((uint32)target), CL(US_TO_SYN68K(reference->toc))};
    return ROMlib_carbon_invoke_frame(vector, call_frame);
}

uint32 cfx_literal(const ROMlib_carbon_reference* reference, unsigned table, unsigned offset)
{
    return CFW_CALL("__CFStringMakeConstantString", cfx_address(cfp_toc(reference, table), offset, 1));
}

double cfx_double(const ROMlib_carbon_ppc_frame* call_frame, unsigned index)
{
    double value;
    memcpy(&value, call_frame->floating + index, sizeof value);
    return value;
}

int cfx_double_result(ROMlib_carbon_ppc_frame* call_frame, double result)
{
    memcpy(call_frame->floating, &result, sizeof result);
    return 1;
}

uint32 cfx_varargs(ROMlib_carbon_ppc_frame* call_frame, unsigned first)
{
    if (first > 8)
        return ROMlib_carbon_report_failure("Invalid native Carbon varargs register position");
    uint32 start = cfx_address(call_frame->stack, 24 + first * 4, (8 - first) * 4);
    /* Classic CFM varargs wrappers spill their incoming GPR arguments into
       these caller-owned homes. Later words already occupy the same area. */
    for (unsigned i = first; i < 8; ++i)
        if (!cfp_write(cfx_address(call_frame->stack, 24 + i * 4, 4), call_frame->words[i]))
            return 0;
    return start;
}
