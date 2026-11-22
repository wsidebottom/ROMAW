/*+===================================================================
  File:      cfx_strings.c

  Summary:   Implements CoreFoundation strings service entries for the PowerPC bridge.

  Classes:   None.

  Functions: ROMlib_cfx_CFStringAppendFormat, ROMlib_cfx_CFStringAppendFormatAndArguments,
             ROMlib_cfx_CFStringCreateWithFormat, ROMlib_cfx_CFStringCreateWithFormatAndArguments,
             ROMlib_cfx_CFStringGetDoubleValue, ROMlib_cfx_CFStringCreateWithBytesNoCopy,
             ROMlib_cfx_CFStringCreateWithFormatAndArgumentsAux,
             ROMlib_cfx_CFStringGetFileSystemRepresentation, ROMlib_cfx_CFStringIsLegalURLString

  Origin:    C reconstruction of the local CoreFoundationLib 1.4 disassembly; each entry verifies its reference offset.
             Public contracts use Universal Interfaces 3.4.2; private ABI notes are in cf-services-batch.json.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "cfx_internal.h"

int ROMlib_cfx_CFStringAppendFormat(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFStringAppendFormat", 0x3d8b4, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 object = CFX_ARG(0), options = CFX_ARG(1), format = CFX_ARG(2);
    uint32 arguments = cfx_varargs(call_frame, 3);
    uint32 append[] = {object, 0, options, format, arguments};
    ROMlib_carbon_ppc_frame nested = *call_frame;
    memcpy(nested.words, append, sizeof append);
    if (!cfx_private_frame(&reference, 0x3ee90, &nested))
        return 0;
    call_frame->fpscr = nested.fpscr;
    return 1;
}

int ROMlib_cfx_CFStringAppendFormatAndArguments(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFStringAppendFormatAndArguments", 0x3ee54, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 arguments[] = {CFX_ARG(0), 0, CFX_ARG(1), CFX_ARG(2), CFX_ARG(3)};
    ROMlib_carbon_ppc_frame nested = *call_frame;
    memcpy(nested.words, arguments, sizeof arguments);
    if (!cfx_private_frame(&reference, 0x3ee90, &nested))
        return 0;
    call_frame->fpscr = nested.fpscr;
    return 1;
}

int ROMlib_cfx_CFStringCreateWithFormat(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFStringCreateWithFormat", 0x37a90, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 allocator = CFX_ARG(0), options = CFX_ARG(1), format = CFX_ARG(2);
    uint32 arguments = cfx_varargs(call_frame, 3);
    ROMlib_carbon_ppc_frame nested = *call_frame;
    nested.words[0] = allocator;
    nested.words[1] = 0;
    nested.words[2] = options;
    nested.words[3] = format;
    nested.words[4] = arguments;
    if (!ROMlib_cfx_CFStringCreateWithFormatAndArgumentsAux(&nested))
        return 0;
    call_frame->fpscr = nested.fpscr;
    return CFX_RESULT(nested.words[0]);
}

int ROMlib_cfx_CFStringCreateWithFormatAndArguments(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFStringCreateWithFormatAndArguments", 0x3799c, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    ROMlib_carbon_ppc_frame nested = *call_frame;
    nested.words[0] = CFX_ARG(0);
    nested.words[1] = 0;
    nested.words[2] = CFX_ARG(1);
    nested.words[3] = CFX_ARG(2);
    nested.words[4] = CFX_ARG(3);
    if (!ROMlib_cfx_CFStringCreateWithFormatAndArgumentsAux(&nested))
        return 0;
    call_frame->fpscr = nested.fpscr;
    return CFX_RESULT(nested.words[0]);
}

int ROMlib_cfx_CFStringGetDoubleValue(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFStringGetDoubleValue", 0x3d078, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 string = CFX_ARG(0), length = CFW_CALL("CFStringGetLength", string);
    /* CFStringInlineBuffer: 64 UTF16 units, object/direct pointers, range and
       cached range. This fixed block fits one alias slot through its alignment. */
    _Alignas(256) uint32 buffer[38] = {0};
    buffer[32] = CL(string);
    buffer[33] = CL(CFW_CALL("CFStringGetCharactersPtr", string));
    buffer[35] = CL(length);
    _Alignas(16) uint32 index = 0, output[2] = {0};
    uint32 arguments[] = {US_TO_SYN68K(buffer), 0, US_TO_SYN68K(&index), US_TO_SYN68K(output)};
    ROMlib_carbon_ppc_frame nested = *call_frame;
    memcpy(nested.words, arguments, sizeof arguments);
    if (!cfx_private_frame(&reference, 0x42fb0, &nested))
        return 0;
    call_frame->fpscr = nested.fpscr;
    call_frame->floating[0] = (nested.words[0] & 255)
                                  ? ((uint64)CL(output[0]) << 32) | CL(output[1])
                                  : ((uint64)cfp_toc(&reference, 0x9a0) << 32) | cfp_toc(&reference, 0x9a4);
    return 1;
}

int ROMlib_cfx_CFStringCreateWithBytesNoCopy(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFStringCreateWithBytesNoCopy", 0x37964, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    return CFX_RESULT(
        CFX_PRIVATE(0x36eb8, CFX_ARG(0), CFX_ARG(1), CFX_ARG(2), CFX_ARG(3), CFX_ARG(4), 1, 0, 0, 1, CFX_ARG(5)));
}

int ROMlib_cfx_CFStringCreateWithFormatAndArgumentsAux(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFStringCreateWithFormatAndArgumentsAux", 0x379d8, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 allocator = CFX_ARG(0), auxiliary = CFX_ARG(1), options = CFX_ARG(2), format = CFX_ARG(3),
           arguments = CFX_ARG(4);
    uint32 temporary = CFW_CALL("CFStringCreateMutable", CFW_CALL("CFAllocatorGetDefault"), 0);
    if (!temporary)
        return CFX_RESULT(0);
    uint32 length = CFW_CALL("CFStringGetLength", format);
    uint32 capacity_address = cfx_address(temporary, 20, 4), flags = cfp_read(capacity_address);
    if (length > (flags & 0x01ffffff) && !cfp_write(capacity_address, (flags & 0xfe000000) | (length & 0x01ffffff)))
        return 0;
    uint32 append[] = {temporary, auxiliary, options, format, arguments};
    ROMlib_carbon_ppc_frame nested = *call_frame;
    memcpy(nested.words, append, sizeof append);
    if (!cfx_private_frame(&reference, 0x3ee90, &nested)) {
        CFW_CALL("CFRelease", temporary);
        return 0;
    }
    call_frame->fpscr = nested.fpscr;
    uint32 result = CFW_CALL("CFStringCreateCopy", allocator, temporary);
    CFW_CALL("CFRelease", temporary);
    return CFX_RESULT(result);
}

int ROMlib_cfx_CFStringGetFileSystemRepresentation(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFStringGetFileSystemRepresentation", 0x42580, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    ROMlib_carbon_ppc_frame nested = *call_frame;
    if (!ROMlib_cfx_CFStringFileSystemEncoding(&nested))
        return 0;
    return CFX_RESULT(CFW_CALL("CFStringGetCString", CFX_ARG(0), CFX_ARG(1), CFX_ARG(2), nested.words[0]));
}

int ROMlib_cfx_CFStringIsLegalURLString(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFStringIsLegalURLString", 0x6bac, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 text = CFX_ARG(0);
    if (!text)
        return CFX_RESULT(0);
    uint32 length = CFW_CALL("CFStringGetLength", text);
    unsigned fragment = 0;
    for (uint32 index = 0; index < length;) {
        uint32 character = CFW_CALL("CFStringGetCharacterAtIndex", text, index++);
        if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            (character && character < 128 && strchr("_-.!~'()*/:;?@&=$+,", character)))
            continue;
        if (character == '#' && !fragment) {
            fragment = 1;
            continue;
        }
        if (character != '%')
            return CFX_RESULT(index >= length);
        if (length - index < 2)
            return CFX_RESULT(0);
        for (unsigned digit = 0; digit < 2; ++digit) {
            character = CFW_CALL("CFStringGetCharacterAtIndex", text, index++);
            if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
                  (character >= 'A' && character <= 'F')))
                return CFX_RESULT(0);
        }
    }
    return CFX_RESULT(1);
}
