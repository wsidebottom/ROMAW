/*+===================================================================
  File:      cfx_plists.c

  Summary:   Implements CoreFoundation plists service entries for the PowerPC bridge.

  Classes:   None.

  Functions: ROMlib_cfx_CFPropertyListCreateFromXMLData

  Origin:    C reconstruction of the local CoreFoundationLib 1.4 disassembly; each entry verifies its reference offset.
             Public contracts use Universal Interfaces 3.4.2; private ABI notes are in cf-services-batch.json.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "cfx_internal.h"

int ROMlib_cfx_CFPropertyListCreateFromXMLData(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFPropertyListCreateFromXMLData", 0x29f94, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 allocator = CFX_ARG(0), data = CFX_ARG(1), mutability = CFX_ARG(2);
    uint32 error_out = CFX_ARG(3), allow_new = CFX_ARG(4), result = 0;
    if (!data || !CFW_CALL("CFDataGetLength", data)) {
        if (error_out) {
            uint32 error = cfx_literal(&reference, 0x510, 0x763);
            CFW_CALL("CFRetain", error);
            if (!cfp_write(error_out, error))
                return 0;
        }
        return CFX_RESULT(0);
    }
    if (!allocator)
        allocator = CFW_CALL("CFAllocatorGetDefault");
    CFW_CALL("CFRetain", allocator);
    if (error_out && !cfp_write(error_out, 0)) {
        CFW_CALL("CFRelease", allocator);
        return 0;
    }
    uint32 encoding = CFX_PRIVATE(0x29c80, data, error_out);
    uint32 text = 0, characters = 0, allocated_characters = 0;
    _Alignas(64) uint32 state[9] = {0};
    if (!encoding) {
        if (error_out) {
            uint32 old_error = cfp_read(error_out);
            uint32 error = CFW_CALL("CFStringCreateWithFormat", allocator, 0, cfx_literal(&reference, 0x510, 0x78b));
            if (old_error)
                CFW_CALL("CFRelease", old_error);
            cfp_write(error_out, error);
        }
        goto cleanup;
    }
    uint32 byte_length = CFW_CALL("CFDataGetLength", data), bytes = CFW_CALL("CFDataGetBytePtr", data);
    text = CFW_CALL("CFStringCreateWithBytes", allocator, bytes, byte_length, encoding, 1);
    uint32 length = text ? CFW_CALL("CFStringGetLength", text) : 0;
    if (!length) {
        if (error_out)
            cfp_write(error_out, CFW_CALL("CFStringCreateWithFormat", allocator, 0,
                                          cfx_literal(&reference, 0x510, 0x7d0), encoding));
        goto cleanup;
    }
    if (length > INT32_MAX / 2) {
        cfp_failure("Native Carbon property-list character extent exceeds CFIndex");
        goto cleanup;
    }
    characters = CFW_CALL("CFStringGetCharactersPtr", text);
    if (!characters) {
        characters = CFW_CALL("CFAllocatorAllocate", allocator, length * 2, 0);
        if (!characters)
            goto cleanup;
        allocated_characters = 1;
        CFW_CALL("CFStringGetCharacters", text, 0, length, characters);
        CFW_CALL("CFRelease", text);
        text = 0;
    }
    state[0] = state[1] = CL(characters);
    state[2] = CL(cfx_address(characters, (uint64)length * 2, 0));
    state[4] = CL(allocator);
    state[5] = CL(mutability);
    ((Byte*)state)[32] = allow_new;
    result = CFX_PRIVATE(0x29a84, US_TO_SYN68K(state));
    if (!result) {
        uint32 xml_error = CL(state[3]);
        state[1] = state[0];
        state[3] = 0;
        result = CFX_PRIVATE(0x2afc4, US_TO_SYN68K(state));
        uint32 legacy_error = CL(state[3]);
        state[3] = 0;
        if (legacy_error)
            CFW_CALL("CFRelease", legacy_error);
        if (!result && error_out) {
            cfp_write(error_out, xml_error);
            xml_error = 0;
        }
        if (xml_error)
            CFW_CALL("CFRelease", xml_error);
    }
cleanup:
    if (text)
        CFW_CALL("CFRelease", text);
    if (allocated_characters)
        CFW_CALL("CFAllocatorDeallocate", allocator, characters);
    if (CL(state[3]))
        CFW_CALL("CFRelease", CL(state[3]));
    if (CL(state[6]))
        CFW_CALL("CFRelease", CL(state[6]));
    if (CL(state[7]))
        CFW_CALL("CFRelease", CL(state[7]));
    CFW_CALL("CFRelease", allocator);
    return CFX_RESULT(result);
}
