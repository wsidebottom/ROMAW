/*+===================================================================
  File:      component_entries.h

  Summary:   Adapts CarbonLib Component Manager calls to the shared component registry.

  Classes:   None.

  Functions: Private PowerPC frame adapters for the export registry.

  Origin:    CarbonLib 1.4 InterfaceLib re-exports, Universal Interfaces 3.4.2
             Components.h and Executor's classic Component Manager.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_COMPONENT_ENTRIES_H
#define ROMAW_CARBON_COMPONENT_ENTRIES_H

#include "rsys/component.h"
#include "rsys/mixed_mode.h"

static int carbon_component_call(ROMlib_carbon_ppc_frame* call, unsigned selector)
{
    unsigned output_index = 0;
    size_t output_size = 0;
    switch (selector) {
    case 47: /* OpenAComponentResFile */
        output_index = 1;
        output_size = sizeof(INTEGER);
        break;
    case 41: /* GetComponentIconSuite */
        output_index = 1;
        output_size = sizeof(uint32_t);
        break;
    case 53: /* GetComponentResource */
    case 56: /* GetComponentPublicResource */
        output_index = 3;
        output_size = sizeof(uint32_t);
        break;
    case 54: /* GetComponentIndString */
    case 58: /* GetComponentPublicIndString */
        output_index = 1;
        output_size = sizeof(Str255);
        break;
    }
    if (output_size && (!call->words[output_index] ||
                        !ROMlib_carbon_try_guest_memory(call->words[output_index], output_size, 1))) {
        call->words[0] = paramErr;
        return 1;
    }
    const char* signature = ROMlib_component_signature(selector);
    uint32_t arguments[6] = {0}, result;
    for (unsigned index = 1; signature[index]; ++index) {
        uint32_t word = call->words[index - 1];
        arguments[index - 1] = signature[index] == '2' ? (uint16_t)word : word;
    }
    if (!ROMlib_component_service(selector, arguments, &result))
        return 0;
    switch (signature[0]) {
    case '2':
        call->words[0] = (int16_t)result;
        break;
    case '4':
        call->words[0] = result;
        break;
    }
    return 1;
}

#define CARBON_COMPONENT(name, selector)                                    \
    static int carbon_component_##name(ROMlib_carbon_ppc_frame* call_frame) \
    {                                                                       \
        return carbon_component_call(call_frame, selector);                 \
    }

CARBON_COMPONENT(RegisterComponent, 1)
CARBON_COMPONENT(UnregisterComponent, 2)
CARBON_COMPONENT(CountComponents, 3)
CARBON_COMPONENT(FindNextComponent, 4)
CARBON_COMPONENT(GetComponentInfo, 5)
CARBON_COMPONENT(GetComponentListModSeed, 6)
CARBON_COMPONENT(OpenComponent, 7)
CARBON_COMPONENT(CloseComponent, 8)
CARBON_COMPONENT(GetComponentInstanceError, 10)
CARBON_COMPONENT(SetComponentInstanceError, 11)
CARBON_COMPONENT(GetComponentInstanceStorage, 12)
CARBON_COMPONENT(SetComponentInstanceStorage, 13)
CARBON_COMPONENT(GetComponentRefcon, 16)
CARBON_COMPONENT(SetComponentRefcon, 17)
CARBON_COMPONENT(RegisterComponentResource, 18)
CARBON_COMPONENT(CountComponentInstances, 19)
CARBON_COMPONENT(RegisterComponentResourceFile, 20)
CARBON_COMPONENT(SetDefaultComponent, 30)
CARBON_COMPONENT(OpenDefaultComponent, 33)
CARBON_COMPONENT(GetComponentTypeModSeed, 44)
CARBON_COMPONENT(OpenAComponent, 45)
CARBON_COMPONENT(OpenADefaultComponent, 46)

CARBON_COMPONENT(OpenComponentResFile, 21)
CARBON_COMPONENT(CloseComponentResFile, 24)
CARBON_COMPONENT(OpenAComponentResFile, 47)
CARBON_COMPONENT(GetComponentResource, 53)
CARBON_COMPONENT(GetComponentIndString, 54)

#undef CARBON_COMPONENT

static int carbon_component_dispatch_reference(ROMlib_carbon_ppc_frame* call, const char* name, unsigned selector)
{
    if (!ROMlib_component_owns_token(call->words[0]) &&
        (selector != 28 || !ROMlib_component_owns_token(call->words[1]))) {
        ROMlib_carbon_reference original;
        if (ROMlib_carbon_load_reference("InterfaceLib", name, &original))
            return ROMlib_carbon_invoke_frame(original.function, call);
    }
    const bool capture = selector == 28;
    const uint32_t info = capture ? 0xff8 : 0x3e8;
    if (selector == 41 || selector == 56 || selector == 58)
        return carbon_component_call(call, selector);
    uint32_t result;
    if (!ROMlib_carbon_call_toolbox_vector(0x0ea8, info, selector, call, &result))
        return 0;
    call->words[0] = capture ? result : (uint32_t)(int32_t)(int16_t)result;
    return 1;
}

static int carbon_component_CaptureComponent(ROMlib_carbon_ppc_frame* call)
{
    return carbon_component_dispatch_reference(call, "CaptureComponent", 28);
}

static int carbon_component_UncaptureComponent(ROMlib_carbon_ppc_frame* call)
{
    return carbon_component_dispatch_reference(call, "UncaptureComponent", 29);
}

static int carbon_component_GetComponentIconSuite(ROMlib_carbon_ppc_frame* call)
{
    return carbon_component_dispatch_reference(call, "GetComponentIconSuite", 41);
}

static int carbon_component_GetComponentPublicResource(ROMlib_carbon_ppc_frame* call)
{
    return carbon_component_dispatch_reference(call, "GetComponentPublicResource", 56);
}

static int carbon_component_GetComponentPublicIndString(ROMlib_carbon_ppc_frame* call)
{
    return carbon_component_dispatch_reference(call, "GetComponentPublicIndString", 58);
}

static int carbon_component_invoke(ROMlib_carbon_ppc_frame* call, const char* name, bool storage, bool explicit_info)
{
    const unsigned first = storage ? 1 : 0;
    const uint32_t params_address = call->words[first];
    const uint32_t procedure = call->words[first + 1];
    const Byte* params = ROMlib_carbon_try_guest_memory(params_address, 4, 0);
    uint32_t info = explicit_info ? call->words[3] : 0;
    bool ppc = true;

    if (!params_address || !params || !procedure)
        goto invalid;

    const unsigned size = params[1];
    params = ROMlib_carbon_try_guest_memory(params_address, 4 + size, 0);
    if (!params || (size & 1))
        goto invalid;

    if (!explicit_info) {
        const RoutineDescriptor* descriptor = ROMlib_carbon_try_guest_memory(procedure, sizeof *descriptor, 0);
        if (!descriptor || descriptor->goMixedModeTrap != CWC(MIXED_MODE_TRAP) ||
            descriptor->version != kRoutineDescriptorVersion || descriptor->routineCount ||
            descriptor->selectorInfo || descriptor->routineDescriptorFlags)
            goto fallback;
        const RoutineRecord* routine = &descriptor->routineRecords[0];
        if (routine->ISA != kPowerPCISA && routine->ISA != kM68kISA)
            goto fallback;
        ppc = routine->ISA == kPowerPCISA;
        info = CL(routine->procInfo);
    }

    const unsigned convention = info & 15;
    if (convention != kPascalStackBased && convention != kCStackBased)
        goto fallback;

    // ComponentParameters stores Pascal arguments in reverse order, with
    // one-byte values in the high byte of their two-byte stack slots.
    uint32_t arguments[13];
    unsigned count = 0;
    uint32_t widths = info >> kStackParameterPhase;
    for (uint32_t fields = widths; fields; fields >>= 2) {
        if (!(fields & 3))
            goto invalid;
        ++count;
    }
    if (storage && (!count || (widths & 3) != kFourByteCode))
        goto invalid;
    if (storage)
        arguments[0] = call->words[0];

    unsigned remaining = size;
    for (unsigned index = first; index < count; ++index) {
        const unsigned width = (widths >> (2 * index)) & 3;
        const unsigned bytes = width == kFourByteCode ? 4 : 2;
        if (remaining < bytes)
            goto invalid;
        remaining -= bytes;
        const Byte* value = params + 4 + remaining;
        if (width == kOneByteCode)
            arguments[index] = (int8_t)value[0];
        else if (width == kTwoByteCode) {
            uint16_t word;
            memcpy(&word, value, sizeof word);
            arguments[index] = (int16_t)CW(word);
        }
        else {
            uint32_t word;
            memcpy(&word, value, sizeof word);
            arguments[index] = CL(word);
        }
    }
    if (remaining)
        goto invalid;

    uint32_t result;
    if (ppc) {
        const void* entry = ROMlib_carbon_try_guest_memory(procedure, 8, 0);
        if (!entry)
            goto invalid;
        if (!ROMlib_carbon_invoke_callback((void*)entry, arguments, count, &result))
            return 0;
    }
    else {
        ROMlib_carbon_ppc_frame forwarded = {0};
        if (count > NELEM(forwarded.words))
            goto fallback;
        memcpy(forwarded.words, arguments, count * sizeof arguments[0]);
        const int called = convention == kPascalStackBased
                               ? ROMlib_carbon_invoke_pascal(procedure, info, &forwarded, &result)
                               : ROMlib_carbon_invoke_mixed_upp(procedure, info, &forwarded, &result);
        if (!called)
            return 0;
    }
    call->words[0] = result;
    return 1;

fallback: {
        ROMlib_carbon_reference original;
        if (ROMlib_carbon_load_reference("InterfaceLib", name, &original))
            return ROMlib_carbon_invoke_frame(original.function, call);
    }
invalid:
    call->words[0] = paramErr;
    return 1;
}

static int carbon_component_CallComponentFunction(ROMlib_carbon_ppc_frame* call)
{
    return carbon_component_invoke(call, "CallComponentFunction", false, false);
}

static int carbon_component_CallComponentFunctionWithStorage(ROMlib_carbon_ppc_frame* call)
{
    return carbon_component_invoke(call, "CallComponentFunctionWithStorage", true, false);
}

static int carbon_component_CallComponentFunctionWithStorageProcInfo(ROMlib_carbon_ppc_frame* call)
{
    return carbon_component_invoke(call, "CallComponentFunctionWithStorageProcInfo", true, true);
}

static int carbon_component_message(ROMlib_carbon_ppc_frame* call, int16_t selector, unsigned layout)
{
    /* Parameters appear in Pascal stack order, with the last argument first. */
    _Alignas(16) unsigned char parameters[4 + sizeof call->words - sizeof call->words[0]] = {0};
    const uint16_t guest_selector = CW(selector);
    memcpy(parameters + 2, &guest_selector, 2);
    unsigned count = 0;
    for (unsigned spec = layout; spec; spec >>= 3)
        ++count;
    for (unsigned index = count; index; --index, layout >>= 3) {
        uint32_t word = call->words[index];
        const unsigned type = layout & 7;
        const unsigned width = type == 1 ? 2 : type;
        if (type == 1)
            word = (word & 0xff) << 8;
        unsigned end = 4 + parameters[1] + width;
        parameters[1] += width;
        for (unsigned byte = 0; byte < width; ++byte, word >>= 8)
            parameters[--end] = (uint8_t)word;
    }
    call->words[0] = ROMlib_component_call(call->words[0], parameters);
    return 1;
}

#define CARBON_COMPONENT_MESSAGE(name, selector, layout)                    \
    static int carbon_component_##name(ROMlib_carbon_ppc_frame* call_frame) \
    {                                                                       \
        return carbon_component_message(call_frame, selector, layout);      \
    }

CARBON_COMPONENT_MESSAGE(CallComponentOpen, -1, 4)
CARBON_COMPONENT_MESSAGE(CallComponentClose, -2, 4)
CARBON_COMPONENT_MESSAGE(CallComponentCanDo, -3, 2)
CARBON_COMPONENT_MESSAGE(CallComponentVersion, -4, 0)
CARBON_COMPONENT_MESSAGE(CallComponentRegister, -5, 0)
CARBON_COMPONENT_MESSAGE(CallComponentTarget, -6, 4)
CARBON_COMPONENT_MESSAGE(CallComponentUnregister, -7, 0)
CARBON_COMPONENT_MESSAGE(CallComponentGetMPWorkFunction, -8, 044)
CARBON_COMPONENT_MESSAGE(CallComponentGetPublicResource, -10, 0424)
CARBON_COMPONENT_MESSAGE(ComponentFunctionImplemented, -3, 2)
CARBON_COMPONENT_MESSAGE(GetComponentVersion, -4, 0)
CARBON_COMPONENT_MESSAGE(ComponentSetTarget, -6, 4)

/* Internet Config places byte flags in the first byte of a two-byte Pascal slot. */
#define CARBON_IC_MESSAGE(name, selector, layout) CARBON_COMPONENT_MESSAGE(name, selector, layout)
CARBON_IC_MESSAGE(ICGetVersion, 0x32, 044)
CARBON_IC_MESSAGE(ICGetConfigName, 0x23, 014)
CARBON_IC_MESSAGE(ICGetSeed, 0x4, 04)
CARBON_IC_MESSAGE(ICGetPerm, 0xd, 04)
CARBON_IC_MESSAGE(ICBegin, 0x5, 01)
CARBON_IC_MESSAGE(ICGetPref, 0x6, 04444)
CARBON_IC_MESSAGE(ICSetPref, 0x7, 04444)
CARBON_IC_MESSAGE(ICFindPrefHandle, 0x24, 0444)
CARBON_IC_MESSAGE(ICGetPrefHandle, 0x1a, 0444)
CARBON_IC_MESSAGE(ICSetPrefHandle, 0x1b, 0444)
CARBON_IC_MESSAGE(ICCountPref, 0x8, 04)
CARBON_IC_MESSAGE(ICGetIndPref, 0x9, 044)
CARBON_IC_MESSAGE(ICDeletePref, 0xc, 04)
CARBON_IC_MESSAGE(ICEnd, 0xa, 0)
CARBON_IC_MESSAGE(ICGetDefaultPref, 0x31, 044)
CARBON_IC_MESSAGE(ICEditPreferences, 0xf, 04)
CARBON_IC_MESSAGE(ICLaunchURL, 0x11, 044444)
CARBON_IC_MESSAGE(ICParseURL, 0x10, 0444444)
CARBON_IC_MESSAGE(ICCreateGURLEvent, 0x33, 0444)
CARBON_IC_MESSAGE(ICSendGURLEvent, 0x34, 04)
CARBON_IC_MESSAGE(ICMapFilename, 0x18, 044)
CARBON_IC_MESSAGE(ICMapTypeCreator, 0x19, 04444)
CARBON_IC_MESSAGE(ICMapEntriesFilename, 0x1c, 0444)
CARBON_IC_MESSAGE(ICMapEntriesTypeCreator, 0x1d, 044444)
CARBON_IC_MESSAGE(ICCountMapEntries, 0x12, 044)
CARBON_IC_MESSAGE(ICGetIndMapEntry, 0x13, 04444)
CARBON_IC_MESSAGE(ICGetMapEntry, 0x14, 0444)
CARBON_IC_MESSAGE(ICSetMapEntry, 0x15, 0444)
CARBON_IC_MESSAGE(ICDeleteMapEntry, 0x16, 044)
CARBON_IC_MESSAGE(ICAddMapEntry, 0x17, 044)
CARBON_IC_MESSAGE(ICGetCurrentProfile, 0x25, 04)
CARBON_IC_MESSAGE(ICSetCurrentProfile, 0x26, 04)
CARBON_IC_MESSAGE(ICCountProfiles, 0x27, 04)
CARBON_IC_MESSAGE(ICGetIndProfile, 0x28, 044)
CARBON_IC_MESSAGE(ICGetProfileName, 0x29, 044)
CARBON_IC_MESSAGE(ICSetProfileName, 0x2a, 044)
CARBON_IC_MESSAGE(ICAddProfile, 0x2b, 044)
CARBON_IC_MESSAGE(ICDeleteProfile, 0x2c, 04)
#undef CARBON_IC_MESSAGE

static int carbon_component_ICStart(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t output = call->words[0];
    if (!output || !ROMlib_carbon_try_guest_memory(output, 4, 1)) {
        call->words[0] = paramErr;
        return 1;
    }
    if (!cfp_write(output, 0))
        return 0;
    const uint32_t component[] = {TICK("PREF"), TICK("ICAp")};
    uint32_t instance;
    if (!ROMlib_component_service(33, component, &instance))
        return 0;
    if (!instance) {
        ROMlib_carbon_reference saved;
        if (ROMlib_carbon_probe_reference("CarbonLib", "ICStart", &saved))
            return ROMlib_carbon_invoke_frame(saved.function, call);
        call->words[0] = UINT32_C(0x80008001);
        return 1;
    }

    if (!cfp_write(output, instance))
        return 0;
    ROMlib_carbon_ppc_frame message = {0};
    message.words[0] = instance;
    message.words[1] = call->words[1];
    if (!carbon_component_message(&message, 0, 4))
        return 0;
    uint32_t result = message.words[0];
    if (!result) {
        _Alignas(32) uint32_t version = 0;
        message.words[0] = instance;
        message.words[1] = 0;
        message.words[2] = US_TO_SYN68K(&version);
        if (!carbon_component_message(&message, 0x32, 044))
            return 0;
        result = message.words[0];
        if (result == UINT32_C(0x80008002)) {
            message.words[0] = instance;
            message.words[1] = 0;
            message.words[2] = 0;
            if (!carbon_component_message(&message, 2, 041))
                return 0;
            result = message.words[0];
        }
    }
    if (result) {
        uint32_t ignored;
        ROMlib_component_service(8, &instance, &ignored);
        if (!cfp_write(output, 0))
            return 0;
    }
    call->words[0] = result;
    return 1;
}

static int carbon_component_ICStop(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t instance = call->words[0];
    uint32_t close_result;
    if (!carbon_component_message(call, 1, 0) || !ROMlib_component_service(8, &instance, &close_result))
        return 0;
    if (!call->words[0])
        call->words[0] = (int16_t)close_result;
    return 1;
}

#undef CARBON_COMPONENT_MESSAGE

static int carbon_component_DelegateComponentCall(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t address = call->words[0];
    const unsigned char* parameters = address ? ROMlib_carbon_guest_memory(address, 4, 0) : NULL;
    if (!parameters || (parameters[1] & 1) || !ROMlib_carbon_guest_memory(address, 4 + parameters[1], 0)) {
        call->words[0] = paramErr;
        return 1;
    }
    call->words[0] = ROMlib_component_call(call->words[1], parameters);
    return 1;
}

#endif
