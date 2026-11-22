/*+===================================================================
  File:      cfx_paths.c

  Summary:   Implements CoreFoundation paths service entries for the PowerPC bridge.

  Classes:   None.

  Functions: ROMlib_cfx_CFAppendPathComponent, ROMlib_cfx_CFAppendPathExtension,
             ROMlib_cfx_CFIsAbsolutePath, ROMlib_cfx_CFLengthAfterDeletingLastPathComponent,
             ROMlib_cfx_CFLengthAfterDeletingPathExtension, ROMlib_cfx_CFStartOfLastPathComponent,
             ROMlib_cfx_CFStartOfPathExtension, ROMlib_cfx_CFStripTrailingPathSlashes,
             ROMlib_cfx_CFTransmutePathSlashes

  Origin:    C reconstruction of the local CoreFoundationLib 1.4 disassembly; each entry verifies its reference offset.
             Public contracts use Universal Interfaces 3.4.2; private ABI notes are in cf-services-batch.json.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "cfx_internal.h"

static uint32 path_character(uint32 path, uint32 index)
{
    return cfx_half(cfx_address(path, (uint64)index * 2, 2));
}

static uint32 path_component_start(uint32 path, int32 length)
{
    if (length < 2)
        return 0;
    for (int32 index = length - 1; index > 0; --index)
        if (path_character(path, index - 1) == ':')
            return index;
    return 0;
}

static uint32 path_extension_start(uint32 path, int32 length)
{
    if (length < 2)
        return 0;
    for (int32 index = length - 1; index > 0; --index) {
        if (path_character(path, index - 1) == ':')
            return 0;
        if (path_character(path, index) == '.')
            return index;
    }
    return 0;
}

static uint32 path_strip(uint32 path, uint32 length_pointer)
{
    int32 original = cfp_read(length_pointer), length = original;
    while (length > 1 && path_character(path, length - 1) == ':') {
        --length;
        if (!cfp_write(length_pointer, length))
            return 0;
    }
    return length != original;
}

static int path_extent(int32 length, int32 extra, int32 capacity)
{
    if (length < 0 || extra < 0)
        return cfp_failure("Native Carbon path lengths must be nonnegative");
    return (int64)length + extra + 1 <= capacity;
}

int ROMlib_cfx_CFAppendPathComponent(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFAppendPathComponent", 0x19010, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 path = CFX_ARG(0), length_pointer = CFX_ARG(1), component = CFX_ARG(3), count = CFX_ARG(4);
    if (!count)
        return CFX_RESULT(1);
    int32 length = cfp_read(length_pointer);
    if (!path_extent(length, count, CFX_ARG(2)))
        return CFX_RESULT(0);
    if (length != 0 && (length != 1 || path_character(path, 0) != ':')) {
        if (!cfp_write(length_pointer, length + 1) || !cfx_set_half(cfx_address(path, (uint64)length * 2, 2), ':'))
            return 0;
    }
    length = cfp_read(length_pointer);
    if (!cfp_move(cfx_address(path, (uint64)(uint32)length * 2, (uint64)count * 2), component, (uint64)count * 2))
        return 0;
    if (!cfp_write(length_pointer, cfp_read(length_pointer) + count))
        return 0;
    return CFX_RESULT(1);
}

int ROMlib_cfx_CFAppendPathExtension(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFAppendPathExtension", 0x19110, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 path = CFX_ARG(0), length_pointer = CFX_ARG(1), extension = CFX_ARG(3), count = CFX_ARG(4);
    int32 length = cfp_read(length_pointer);
    if (!path_extent(length, count, CFX_ARG(2)))
        return CFX_RESULT(0);
    if ((int32)count > 0 && path_character(extension, 0) == ':')
        return CFX_RESULT(0);
    path_strip(path, length_pointer);
    length = cfp_read(length_pointer);
    if (!length || (length == 1 && (path_character(path, 0) == ':' || path_character(path, 0) == '~')))
        return CFX_RESULT(0);
    if (path_character(path, 0) == '~') {
        unsigned separator = 0;
        for (int32 index = 1; index < length; ++index)
            if (path_character(path, index) == ':') {
                separator = 1;
                break;
            }
        if (!separator)
            return CFX_RESULT(0);
    }
    if (!cfp_write(length_pointer, length + 1) || !cfx_set_half(cfx_address(path, (uint64)(uint32)length * 2, 2), '.'))
        return 0;
    length = cfp_read(length_pointer);
    if (!cfp_move(cfx_address(path, (uint64)(uint32)length * 2, (uint64)count * 2), extension, (uint64)count * 2) ||
        !cfp_write(length_pointer, cfp_read(length_pointer) + count))
        return 0;
    return CFX_RESULT(1);
}

int ROMlib_cfx_CFIsAbsolutePath(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFIsAbsolutePath", 0x18f74, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 length = CFX_ARG(1);
    return CFX_RESULT((int32)length >= 1 && path_character(CFX_ARG(0), 0) != ':');
}

int ROMlib_cfx_CFLengthAfterDeletingLastPathComponent(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFLengthAfterDeletingLastPathComponent", 0x19370, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 start = path_component_start(CFX_ARG(0), (int32)CFX_ARG(1));
    return CFX_RESULT(start > 1 ? start - 1 : start);
}

int ROMlib_cfx_CFLengthAfterDeletingPathExtension(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFLengthAfterDeletingPathExtension", 0x1942c, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 length = CFX_ARG(1), start = path_extension_start(CFX_ARG(0), (int32)length);
    return CFX_RESULT(start ? start : length);
}

int ROMlib_cfx_CFStartOfLastPathComponent(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFStartOfLastPathComponent", 0x19324, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    return CFX_RESULT(path_component_start(CFX_ARG(0), (int32)CFX_ARG(1)));
}

int ROMlib_cfx_CFStartOfPathExtension(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFStartOfPathExtension", 0x193cc, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    return CFX_RESULT(path_extension_start(CFX_ARG(0), (int32)CFX_ARG(1)));
}

int ROMlib_cfx_CFStripTrailingPathSlashes(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFStripTrailingPathSlashes", 0x18fa0, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    return CFX_RESULT(path_strip(CFX_ARG(0), CFX_ARG(1)));
}

int ROMlib_cfx_CFTransmutePathSlashes(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFTransmutePathSlashes", 0x19290, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 path = CFX_ARG(0), length_pointer = CFX_ARG(1), replacement = CFX_ARG(2);
    int32 length = cfp_read(length_pointer);
    if (length < 0)
        return cfp_failure("Native Carbon path length must be nonnegative");
    uint32 output = 0;
    for (uint32 index = 0; index < (uint32)length;) {
        uint16 character = path_character(path, index++);
        if (character == ':') {
            character = replacement;
            while (index < (uint32)length && path_character(path, index) == ':')
                ++index;
        }
        if (!cfx_set_half(cfx_address(path, (uint64)output++ * 2, 2), character))
            return 0;
    }
    if (!cfp_write(length_pointer, output))
        return 0;
    return CFX_RESULT(output != (uint32)length);
}
