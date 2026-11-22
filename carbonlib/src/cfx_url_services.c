/*+===================================================================
  File:      cfx_url_services.c

  Summary:   Implements CoreFoundation url services service entries for the PowerPC bridge.

  Classes:   None.

  Functions: ROMlib_cfx_CFURLCreateDataAndPropertiesFromResource,
             ROMlib_cfx_CFURLCreatePropertyFromResource,
             ROMlib_cfx_CFURLCreateStringByAddingPercentEscapes,
             ROMlib_cfx_CFURLCreateStringByReplacingPercentEscapes, ROMlib_cfx_CFURLDestroyResource,
             ROMlib_cfx_CFURLWriteDataAndPropertiesToResource

  Origin:    C reconstruction of the local CoreFoundationLib 1.4 disassembly; each entry verifies its reference offset.
             Public contracts use Universal Interfaces 3.4.2; private ABI notes are in cf-services-batch.json.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "cfx_internal.h"

static int service_url_error(uint32 output, int32 code)
{
    return !output || cfp_write(output, (uint32)code);
}

static int service_url_scheme(const ROMlib_carbon_reference* reference, uint32 url, uint32 error, uint32 data_out,
                              uint32 properties_out)
{
    uint32 scheme = CFW_CALL("CFURLCopyScheme", url);
    int32 code = -15;
    if (scheme) {
        uint32 file = cfx_literal(reference, 0x3d8, 0xad);
        int match = !CFW_CALL("CFStringCompare", scheme, file, 0);
        CFW_CALL("CFRelease", scheme);
        if (match)
            return 1;
        code = -11;
    }
    if (data_out)
        cfp_write(data_out, 0);
    if (properties_out)
        cfp_write(properties_out, 0);
    service_url_error(error, code);
    return 0;
}

static int service_url_hex(unsigned character)
{
    if (character >= '0' && character <= '9')
        return character - '0';
    if (character >= 'a' && character <= 'f')
        return character - 'a' + 10;
    if (character >= 'A' && character <= 'F')
        return character - 'A' + 10;
    return -1;
}

static unsigned service_url_character(uint32 text, uint32 index)
{
    return CFW_CALL("CFStringGetCharacterAtIndex", text, index) & 0xffff;
}

static int service_url_append_range(uint32 allocator, uint32 output, uint32 text, uint32 start, uint32 length)
{
    if (!length)
        return 1;
    uint32 slice = CFW_CALL("CFStringCreateWithSubstring", allocator, text, start, length);
    if (!slice)
        return 0;
    CFW_CALL("CFStringAppend", output, slice);
    CFW_CALL("CFRelease", slice);
    return 1;
}

int ROMlib_cfx_CFURLCreateDataAndPropertiesFromResource(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFURLCreateDataAndPropertiesFromResource", 0x4590, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 allocator = CFX_ARG(0), url = CFX_ARG(1), data_out = CFX_ARG(2), properties_out = CFX_ARG(3);
    uint32 desired = CFX_ARG(4), error = CFX_ARG(5);
    if (!service_url_scheme(&reference, url, error, data_out, properties_out))
        return CFX_RESULT(0);
    if (!service_url_error(error, 0))
        return 0;
    unsigned success = 1;
    if (data_out) {
        unsigned retained_allocator = !allocator;
        if (!allocator) {
            allocator = CFW_CALL("CFAllocatorGetDefault");
            CFW_CALL("CFRetain", allocator);
        }
        _Alignas(16) uint32 bytes = 0, length = 0;
        ROMlib_carbon_ppc_frame nested = *call_frame;
        uint32 arguments[] = {allocator, url, US_TO_SYN68K(&bytes), US_TO_SYN68K(&length), 0};
        memcpy(nested.words, arguments, sizeof arguments);
        if (!ROMlib_cfx_CFReadBytesFromFile(&nested)) {
            if (retained_allocator)
                CFW_CALL("CFRelease", allocator);
            return 0;
        }
        if (!(nested.words[0] & 255)) {
            service_url_error(error, -10);
            cfp_write(data_out, 0);
            success = 0;
        }
        else {
            uint32 data = CFW_CALL("CFDataCreateWithBytesNoCopy", allocator, CL(bytes), CL(length), allocator);
            if (!cfp_write(data_out, data)) {
                if (data)
                    CFW_CALL("CFRelease", data);
                if (retained_allocator)
                    CFW_CALL("CFRelease", allocator);
                return 0;
            }
            if (!data && CL(bytes))
                CFW_CALL("CFAllocatorDeallocate", allocator, CL(bytes));
        }
        if (retained_allocator)
            CFW_CALL("CFRelease", allocator);
    }
    if (properties_out) {
        uint32 properties = CFX_PRIVATE(0x3f04, allocator, url, desired, error);
        if (!cfp_write(properties_out, properties))
            return 0;
        if (!properties)
            success = 0;
    }
    return CFX_RESULT(success);
}

int ROMlib_cfx_CFURLCreatePropertyFromResource(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFURLCreatePropertyFromResource", 0x46ac, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 allocator = CFX_ARG(0), url = CFX_ARG(1), property = CFX_ARG(2), error = CFX_ARG(3);
    _Alignas(16) uint32 key = CL(property), properties = 0;
    uint32 desired = CFW_CALL("CFArrayCreate", allocator, US_TO_SYN68K(&key), 1,
                              cfx_address(US_TO_SYN68K(reference.toc), 0x3234, 16));
    if (!desired)
        return CFX_RESULT(0);
    ROMlib_carbon_ppc_frame nested = *call_frame;
    uint32 arguments[] = {allocator, url, 0, US_TO_SYN68K(&properties), desired, error};
    memcpy(nested.words, arguments, sizeof arguments);
    int completed = ROMlib_cfx_CFURLCreateDataAndPropertiesFromResource(&nested);
    uint32 result = 0;
    if (completed && (nested.words[0] & 255) && CL(properties)) {
        result = CFW_CALL("CFDictionaryGetValue", CL(properties), property);
        if (result)
            CFW_CALL("CFRetain", result);
    }
    if (CL(properties))
        CFW_CALL("CFRelease", CL(properties));
    CFW_CALL("CFRelease", desired);
    if (!completed)
        return 0;
    return CFX_RESULT(result);
}

int ROMlib_cfx_CFURLCreateStringByAddingPercentEscapes(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFURLCreateStringByAddingPercentEscapes", 0x5cec, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 allocator = CFX_ARG(0), text = CFX_ARG(1), leave = CFX_ARG(2), force = CFX_ARG(3), encoding = CFX_ARG(4);
    _Alignas(16) uint32 context[2] = {CL(leave), CL(force)};
    return CFX_RESULT(
        CFX_PRIVATE(0x56fc, allocator, text, cfp_toc(&reference, 0x3f0), 0, encoding, US_TO_SYN68K(context)));
}

int ROMlib_cfx_CFURLCreateStringByReplacingPercentEscapes(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFURLCreateStringByReplacingPercentEscapes", 0x504c, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 allocator = CFX_ARG(0), text = CFX_ARG(1), leave = CFX_ARG(2);
    if (!text)
        return CFX_RESULT(0);
    if (!leave)
        return CFX_RESULT(CFW_CALL("CFStringCreateCopy", allocator, text));
    uint32 leave_length = CFW_CALL("CFStringGetLength", leave), length = CFW_CALL("CFStringGetLength", text);
    uint32 output = 0, copied = 0, first_decoded = UINT32_MAX;
    for (uint32 position = 0; position < length;) {
        if (service_url_character(text, position) != '%') {
            ++position;
            continue;
        }
        _Alignas(16) Byte bytes[4] = {0};
        unsigned count = 1;
        for (unsigned index = 0; index < count; ++index) {
            uint64 offset = (uint64)position + index * 3;
            if (offset + 2 >= length || service_url_character(text, offset) != '%')
                goto invalid;
            int high = service_url_hex(service_url_character(text, offset + 1));
            int low = service_url_hex(service_url_character(text, offset + 2));
            if (high < 0 || low < 0)
                goto invalid;
            bytes[index] = (high << 4) | low;
            if (!index && (bytes[0] & 0x80))
                count = !(bytes[0] & 0x20) ? 2 : !(bytes[0] & 0x10) ? 3 : 4;
        }
        unsigned character = bytes[0];
        if (count > 1) {
            uint32 decoded = CFW_CALL("CFStringCreateWithBytes", allocator, US_TO_SYN68K(bytes), count, 0x08000100, 0);
            if (!decoded)
                goto invalid;
            if (CFW_CALL("CFStringGetLength", decoded) != 1) {
                CFW_CALL("CFRelease", decoded);
                goto invalid;
            }
            character = service_url_character(decoded, 0);
            CFW_CALL("CFRelease", decoded);
        }
        unsigned preserve = 0;
        if (leave_length) {
            /* The reference reuses its first one-character search string. */
            if (first_decoded == UINT32_MAX)
                first_decoded = character;
            for (uint32 index = 0; index < leave_length; ++index)
                if (service_url_character(leave, index) == first_decoded) {
                    preserve = 1;
                    break;
                }
        }
        if (!preserve) {
            if (!output) {
                output = CFW_CALL("CFStringCreateMutable", allocator, length);
                if (!output)
                    goto invalid;
            }
            if (!service_url_append_range(allocator, output, text, copied, position - copied))
                goto invalid;
            _Alignas(16) uint16 unit = CW((uint16)character);
            CFW_CALL("CFStringAppendCharacters", output, US_TO_SYN68K(&unit), 1);
            copied = position + count * 3;
        }
        position += count * 3;
    }
    if (!output)
        return CFX_RESULT(CFW_CALL("CFStringCreateCopy", allocator, text));
    if (!service_url_append_range(allocator, output, text, copied, length - copied))
        goto invalid;
    return CFX_RESULT(output);
invalid:
    if (output)
        CFW_CALL("CFRelease", output);
    return CFX_RESULT(0);
}

int ROMlib_cfx_CFURLDestroyResource(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFURLDestroyResource", 0x4968, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 url = CFX_ARG(0), error = CFX_ARG(1);
    if (!service_url_scheme(&reference, url, error, 0, 0))
        return CFX_RESULT(0);
    _Alignas(256) Byte path[256];
    if (!(CFW_CALL("CFURLGetFileSystemRepresentation", url, 1, US_TO_SYN68K(path), sizeof path) & 255)) {
        service_url_error(error, -15);
        return CFX_RESULT(0);
    }
    ROMlib_carbon_ppc_frame nested = *call_frame;
    nested.words[0] = US_TO_SYN68K(path);
    int completed = CFW_CALL("CFURLHasDirectoryPath", url) & 255 ? ROMlib_cfx_CFRemoveDirectory(&nested)
                                                                 : ROMlib_cfx_CFDeleteFile(&nested);
    if (!completed)
        return 0;
    unsigned success = nested.words[0] & 255;
    if (!service_url_error(error, success ? 0 : -10))
        return 0;
    return CFX_RESULT(success);
}

int ROMlib_cfx_CFURLWriteDataAndPropertiesToResource(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFURLWriteDataAndPropertiesToResource", 0x479c, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 url = CFX_ARG(0), data = CFX_ARG(1), properties = CFX_ARG(2), error = CFX_ARG(3);
    if (!service_url_scheme(&reference, url, error, 0, 0))
        return CFX_RESULT(0);
    if (!service_url_error(error, 0))
        return 0;
    unsigned success = 1;
    if (data) {
        ROMlib_carbon_ppc_frame nested = *call_frame;
        if (CFW_CALL("CFURLHasDirectoryPath", url) & 255) {
            _Alignas(256) Byte path[256];
            if (!(CFW_CALL("CFURLGetFileSystemRepresentation", url, 1, US_TO_SYN68K(path), sizeof path) & 255)) {
                service_url_error(error, -15);
                success = 0;
            }
            else {
                nested.words[0] = US_TO_SYN68K(path);
                if (!ROMlib_cfx_CFCreateDirectory(&nested))
                    return 0;
                success = nested.words[0] & 255;
                if (!success)
                    service_url_error(error, -10);
            }
        }
        else {
            uint32 length = CFW_CALL("CFDataGetLength", data);
            nested.words[0] = url;
            nested.words[1] =
                length ? CFW_CALL("CFDataGetBytePtr", data) : cfx_address(cfp_toc(&reference, 0x3d8), 0xb2, 1);
            nested.words[2] = length;
            if (!ROMlib_cfx_CFWriteBytesToFile(&nested))
                return 0;
            success = nested.words[0] & 255;
            if (!success)
                service_url_error(error, -10);
        }
    }
    /* The 1.4 property writer at 0x4468 always fails. Preserve that supported
       surface explicitly without pretending to implement property mutation. */
    if (properties)
        success = 0;
    return CFX_RESULT(success);
}
