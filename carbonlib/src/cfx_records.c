/*+===================================================================
  File:      cfx_records.c

  Summary:   Implements CoreFoundation records service entries for the PowerPC bridge.

  Classes:   None.

  Functions: ROMlib_cfx_CFCopyTypeIDDescription, ROMlib_cfx_CFStringFileSystemEncoding,
             ROMlib_cfx_CFArgv, ROMlib_cfx_CFBundleLayoutVersion,
             ROMlib_cfx_CFCopyExtensionForAbstractType, ROMlib_cfx_CFEnviron,
             ROMlib_cfx_CFGetEnvironmentVariable,
             ROMlib_cfx_CFStringEncodingSetForceASCIICompatibility,
             ROMlib_cfx_CFTemporaryMemoryAllocator, ROMlib_cfx_CFRangeMake,
             ROMlib_cfx_CFStorageGetCapacity, ROMlib_cfx_CFStorageGetValueSize,
             ROMlib_cfx_CFStringCheckMutableAndReplace,
             ROMlib_cfx_CFStringComputeEightBitStringEncoding, ROMlib_cfx_CFStringIsEightBit,
             ROMlib_cfx_CFStringIsMutable

  Origin:    C reconstruction of the local CoreFoundationLib 1.4 disassembly; each entry verifies its reference offset.
             Public contracts use Universal Interfaces 3.4.2; private ABI notes are in cf-services-batch.json.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "cfx_internal.h"

static int storage_capacity(uint32 node, unsigned depth, uint64* result)
{
    if (!node) {
        *result = 0;
        return 1;
    }
    if (depth >= 512)
        return cfp_failure("Native Carbon storage tree nesting limit exceeded");
    if (cfx_byte(cfx_address(node, 4, 1))) {
        *result = cfp_read(cfx_address(node, 8, 4));
        return 1;
    }
    uint64 total = 0;
    for (unsigned offset = 8; offset <= 16; offset += 4) {
        uint64 child = 0;
        if (!storage_capacity(cfp_read(cfx_address(node, offset, 4)), depth + 1, &child))
            return 0;
        total += child;
        if (total > INT32_MAX)
            return cfp_failure("Native Carbon storage capacity overflow");
    }
    *result = total;
    return 1;
}

int ROMlib_cfx_CFCopyTypeIDDescription(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFCopyTypeIDDescription", 0x533a0, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 type = CFX_ARG(0);
    if (type > 255 || type <= cfp_toc(&reference, 0xa6c) || type >= cfp_toc(&reference, 0xa68))
        return cfp_failure("Native Carbon type description requires a registered type");
    uint32 descriptor = cfp_toc(&reference, 0x65c4 + type * 4);
    if (!descriptor)
        return cfp_failure("Native Carbon type description class is unavailable");
    return CFX_RESULT(CFW_CALL("CFStringCreateWithCString", cfp_toc(&reference, 0x3160),
                               cfp_read(cfx_address(descriptor, 4, 4)), 0x600));
}

int ROMlib_cfx_CFStringFileSystemEncoding(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFStringFileSystemEncoding", 0x35600, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 address = cfx_address(US_TO_SYN68K(reference.toc), 0x97c, 4), encoding = cfp_read(address);
    if (encoding == UINT32_MAX) {
        encoding = ROMlib_cfstring_system_encoding();
        if (!cfp_write(address, encoding))
            return 0;
    }
    return CFX_RESULT(encoding);
}

int ROMlib_cfx_CFArgv(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFArgv", 0x19468, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    return CFX_RESULT(cfx_address(US_TO_SYN68K(reference.toc), 0x31a4, 4));
}

int ROMlib_cfx_CFBundleLayoutVersion(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFBundleLayoutVersion", 0xf9cc, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    return CFX_RESULT(cfx_byte(cfx_address(CFX_ARG(0), 16, 1)));
}

int ROMlib_cfx_CFCopyExtensionForAbstractType(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFCopyExtensionForAbstractType", 0x1848c, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 value = CFX_ARG(0);
    return CFX_RESULT(value ? US_TO_SYN68K(ROMlib_carbon_cf_retain(SYN68K_TO_US(value))) : 0);
}

int ROMlib_cfx_CFEnviron(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFEnviron", 0x19478, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    return CFX_RESULT(cfx_address(US_TO_SYN68K(reference.toc), 0x8b0, 4));
}

int ROMlib_cfx_CFGetEnvironmentVariable(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFGetEnvironmentVariable", 0x19480, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 name = CFX_ARG(0);
    if (!name)
        return CFX_RESULT(0);
    uint32 vector = cfx_address(US_TO_SYN68K(reference.toc), 0x8b0, 4);
    for (uint64 index = 0;; ++index) {
        uint32 value = cfp_read(cfx_address(vector, index * 4, 4));
        if (!value)
            return CFX_RESULT(0);
        for (uint64 character = 0;; ++character) {
            unsigned expected = cfx_byte(cfx_address(name, character, 1));
            unsigned actual = cfx_byte(cfx_address(value, character, 1));
            if (expected != actual) {
                if (!expected && actual == '=')
                    return CFX_RESULT(cfx_address(value, character + 1, 1));
                break;
            }
            /* Malformed environment entries without '=' have no value. The 1.4
               scan otherwise runs past both NULs; stop the unsupported input. */
            if (!expected)
                break;
        }
    }
}

int ROMlib_cfx_CFStringEncodingSetForceASCIICompatibility(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFStringEncodingSetForceASCIICompatibility", 0x40648, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    return cfp_write(cfx_address(US_TO_SYN68K(reference.toc), 0x9a8, 4), (CFX_ARG(0) & 255) != 0);
}

int ROMlib_cfx_CFTemporaryMemoryAllocator(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFTemporaryMemoryAllocator", 0x179b8, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 address = cfx_address(US_TO_SYN68K(reference.toc), 0x898, 4), allocator = cfp_read(address);
    if (!allocator) {
        _Alignas(64) uint32 context[9] = {0};
        context[5] = CL(cfp_toc(&reference, 0x45c));
        context[6] = CL(cfp_toc(&reference, 0x458));
        context[7] = CL(cfp_toc(&reference, 0x454));
        allocator = CFW_CALL("CFAllocatorCreate", 0x221, US_TO_SYN68K(context));
        if (!cfp_write(address, allocator))
            return 0;
    }
    return CFX_RESULT(cfp_read(address));
}

int ROMlib_cfx_CFRangeMake(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("__CFRangeMake", 0x17a34, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 output = CFX_ARG(0), location = CFX_ARG(1), length = CFX_ARG(2);
    return cfp_write(output, location) && cfp_write(cfx_address(output, 4, 4), length);
}

int ROMlib_cfx_CFStorageGetCapacity(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("__CFStorageGetCapacity", 0x4aac0, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 object = CFX_ARG(0), size = cfp_read(cfx_address(object, 8, 4));
    if (!size || size > INT32_MAX)
        return cfp_failure("Native Carbon storage requires a positive element size");
    uint64 capacity = 0;
    if (!storage_capacity(cfx_address(object, 24, 20), 0, &capacity))
        return 0;
    return CFX_RESULT(capacity / size);
}

int ROMlib_cfx_CFStorageGetValueSize(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("__CFStorageGetValueSize", 0x4aaf8, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    return CFX_RESULT(cfp_read(cfx_address(CFX_ARG(0), 8, 4)));
}

int ROMlib_cfx_CFStringCheckMutableAndReplace(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("__CFStringCheckMutableAndReplace", 0x3d8f8, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 object = CFX_ARG(0);
    if (!(cfp_read(cfx_address(object, 4, 4)) & 1))
        return CFX_RESULT(0);
    CFX_PRIVATE(0x38424, object, CFX_ARG(1), CFX_ARG(2), CFX_ARG(3));
    return CFX_RESULT(1);
}

int ROMlib_cfx_CFStringComputeEightBitStringEncoding(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("__CFStringComputeEightBitStringEncoding", 0x357c4, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 address = cfx_address(US_TO_SYN68K(reference.toc), 0x3a70, 4), encoding = cfp_read(address);
    if (encoding != UINT32_MAX)
        return CFX_RESULT(encoding);
    encoding = ROMlib_cfstring_system_encoding();
    if (encoding == UINT32_MAX)
        return CFX_RESULT(0x600);
    unsigned compatible = 0;
    switch (encoding) {
    case 0x100:
    case UINT32_MAX:
    case 0xbff:
    case 0x08000100:
        compatible = 0;
        break;
    case 0:
    case 0x500:
    case 0x201:
    case 0xb01:
    case 0x600:
        compatible = 1;
        break;
    default: {
        _Alignas(16) uint16 script = 0;
        uint32 status = CFX_PRIVATE(0x5a01c, encoding, US_TO_SYN68K(&script), 0, 0);
        if (!status && CFX_PRIVATE(0x5a034, (uint32)(int32)(int16)CW(script), 6))
            compatible = 0;
        else
            compatible = ROMlib_carbon_cf_maximum_encoding_size(1, encoding) == 1;
        break;
    }
    }
    if (!cfp_write(address, compatible ? encoding : 0x600))
        return 0;
    return CFX_RESULT(cfp_read(address));
}

int ROMlib_cfx_CFStringIsEightBit(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("__CFStringIsEightBit", 0x35454, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    return CFX_RESULT((cfp_read(cfx_address(CFX_ARG(0), 4, 4)) & 0x10) == 0);
}

int ROMlib_cfx_CFStringIsMutable(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("__CFStringIsMutable", 0x3543c, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    return CFX_RESULT(cfp_read(cfx_address(CFX_ARG(0), 4, 4)) & 1);
}
