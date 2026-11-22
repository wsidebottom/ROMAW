/*+===================================================================
  File:      cfx_logs.c

  Summary:   Implements CoreFoundation logs service entries for the PowerPC bridge.

  Classes:   None.

  Functions: ROMlib_cfx_CFLog, ROMlib_cfx_CFShow, ROMlib_cfx_CFShowStr

  Origin:    C reconstruction of the local CoreFoundationLib 1.4 disassembly; each entry verifies its reference offset.
             Public contracts use Universal Interfaces 3.4.2; private ABI notes are in cf-services-batch.json.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "cfx_internal.h"

#define SERVICE_PRINT(offset, ...) \
    CFX_PRIVATE(0x584e8, cfx_address(cfp_toc(&reference, 0x5a4), offset, 1), ##__VA_ARGS__)

int ROMlib_cfx_CFLog(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFLog", 0x403fc, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 priority = CFX_ARG(0), format = CFX_ARG(1), arguments = cfx_varargs(call_frame, 2);
    uint32 initialized = cfx_address(US_TO_SYN68K(reference.toc), 0x984, 1);
    uint32 name_address = cfx_address(US_TO_SYN68K(reference.toc), 0x6f34, 32);
    uint32 serial_address = cfx_address(US_TO_SYN68K(reference.toc), 0x6f54, 8);
    if (!cfx_byte(initialized)) {
        _Alignas(256) Byte name[256] = {0};
        _Alignas(128) FSSpec spec = {0};
        _Alignas(64) ProcessInfoRec info = {0};
        info.processInfoLength = CL(sizeof info);
        PP_ASSIGN(info.processName, (Byte*)name);
        PP_ASSIGN(info.processAppSpec, &spec);
        ProcessSerialNumber* serial = ROMlib_carbon_guest_memory(serial_address, 8, 1);
        if (!serial)
            return 0;
        /* Preserve the reference's two Process Manager reads. */
        C_GetCurrentProcess(serial);
        unsigned count = 0;
        if (!C_GetCurrentProcess(serial) && !C_GetProcessInformation(serial, &info)) {
            count = name[0] < 32 ? name[0] : 31;
            if (!cfp_move(name_address, US_TO_SYN68K(name + 1), count))
                return 0;
        }
        if (!cfc_byte(cfx_address(name_address, count, 1), 0) || !cfc_byte(initialized, 1))
            return 0;
    }
    ROMlib_carbon_ppc_frame nested = *call_frame;
    nested.words[0] = 0;
    nested.words[1] = 0;
    nested.words[2] = format;
    nested.words[3] = arguments;
    if (!ROMlib_cfx_CFStringCreateWithFormatAndArguments(&nested))
        return 0;
    call_frame->fpscr = nested.fpscr;
    uint32 message = nested.words[0];
    SERVICE_PRINT(0x2b2, priority, name_address, cfp_read(cfx_address(serial_address, 4, 4)));
    nested = *call_frame;
    nested.words[0] = message;
    int success = ROMlib_cfx_CFShow(&nested);
    if (message)
        CFW_CALL("CFRelease", message);
    return success;
}

int ROMlib_cfx_CFShow(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFShow", 0x3feb8, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 object = CFX_ARG(0), description = 0;
    if (object) {
        if (CFW_CALL("CFGetTypeID", object) == CFW_CALL("CFStringGetTypeID"))
            description = CFX_PRIVATE(0x537fc, object, 0);
        if (!description)
            description = CFW_CALL("CFCopyDescription", object);
    }
    else {
        description = cfx_literal(&reference, 0x5a4, 0x178);
        CFW_CALL("CFRetain", description);
    }
    if (!description)
        return cfp_failure("Native Carbon description allocation failed");
    uint32 length = CFW_CALL("CFStringGetLength", description);
    uint32 stream = cfx_address(US_TO_SYN68K(reference.toc), 0x69ec + 0xa8, 1);
    _Alignas(64) uint16 characters[32];
    for (uint32 position = 0; position < length;) {
        uint32 count = length - position;
        if (count > 32)
            count = 32;
        CFW_CALL("CFStringGetCharacters", description, position, count, US_TO_SYN68K(characters));
        unsigned finished = 0;
        for (uint32 index = 0; index < count; ++index) {
            uint16 character = CW(characters[index]);
            /* The reference inline iterator uses U+FFFF as its terminator. */
            if (character == 0xffff) {
                finished = 1;
                break;
            }
            uint32 format = cfx_address(cfp_toc(&reference, 0x5a4), character < 128 ? 0x192 : 0x195, 1);
            CFX_PRIVATE(0x58568, stream, format, character);
        }
        if (finished)
            break;
        position += count;
    }
    CFX_PRIVATE(0x58568, stream, cfx_address(cfp_toc(&reference, 0x5a4), 0x19c, 1));
    CFW_CALL("CFRelease", description);
    return 1;
}

int ROMlib_cfx_CFShowStr(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFShowStr", 0x40130, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 object = CFX_ARG(0);
    if (!object) {
        SERVICE_PRINT(0x19e);
        return 1;
    }
    uint32 allocator = CFW_CALL("CFGetAllocator", object);
    uint32 length = CFW_CALL("CFStringGetLength", object);
    uint32 flags = cfp_read(cfx_address(object, 4, 4));
    SERVICE_PRINT(0x1a6, length, !(flags & 0x10));
    SERVICE_PRINT(0x1c0, !!(flags & 4), !!(flags & 8), !(flags & 0x60));
    SERVICE_PRINT(0x1f3);
    if (allocator == cfp_toc(&reference, 0x3164))
        SERVICE_PRINT(0x202);
    else
        SERVICE_PRINT(0x1fe, allocator);
    SERVICE_PRINT(0x211, flags & 1);
    if (!(flags & 1) && (flags & 0x60) == 0x60) {
        uint32 slot = CFX_PRIVATE(0x35414, object), deallocator = cfp_read(slot);
        if (deallocator)
            SERVICE_PRINT(0x21d, deallocator);
        else
            SERVICE_PRINT(0x239);
    }
    else if ((flags & 1) && (flags & 0x60) == 0x60) {
        uint32 slot = CFX_PRIVATE(0x35434, object);
        SERVICE_PRINT(0x257, cfp_read(slot));
    }
    if (flags & 1) {
        uint32 capacity = cfp_read(cfx_address(object, 20, 4));
        uint32 kind = cfx_address(cfp_toc(&reference, 0x5a4), capacity & 0x80000000U ? 0x275 : 0x27b, 1);
        SERVICE_PRINT(0x283, cfp_read(cfx_address(object, 16, 4)), kind, capacity & 0x1ffffff);
    }
    uint32 contents =
        flags & 0x60 ? cfp_read(cfx_address(object, 8, 4)) : cfx_address(object, (flags & 5) == 4 ? 8 : 12, 1);
    SERVICE_PRINT(0x2a5, contents);
    return 1;
}
