/*+===================================================================
  File:      cf_string_create.c

  Summary:   CoreFoundation 1.4 string construction and mutation entry bodies. Immutable allocation/decoding (0x36eb8),
             replacement (0x38424), resizing (0x35918) and encoded appends (0x3d540) remain real private dependencies.

  Classes:   None.

  Functions: ROMlib_cfstring_append, ROMlib_cfstring_append_characters, ROMlib_cfstring_append_cstring,
             ROMlib_cfstring_append_pascal, ROMlib_cfstring_bytes, ROMlib_cfstring_characters,
             ROMlib_cfstring_characters_no_copy, ROMlib_cfstring_create_mutable, ROMlib_cfstring_cstring,
             ROMlib_cfstring_cstring_no_copy, ROMlib_cfstring_delete, ROMlib_cfstring_from_external,
             ROMlib_cfstring_insert, ROMlib_cfstring_mutable_copy, ROMlib_cfstring_mutable_external,
             ROMlib_cfstring_pascal, ROMlib_cfstring_pascal_no_copy, ROMlib_cfstring_replace,
             ROMlib_cfstring_replace_all, ROMlib_cfstring_set_external, ROMlib_cfstring_substring

  Origin:    Existing repository source in packages/carbonlib/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* CoreFoundation 1.4 string construction and mutation entry bodies.
   Immutable allocation/decoding (0x36eb8), replacement (0x38424), resizing
   (0x35918) and encoded appends (0x3d540) remain real private dependencies. */
#include "cf_batch.h"
#include <limits.h>

static int string_length(uint32 string, uint32* length)
{
    if (!string || !cfp_call("CFStringGetLength", &string, 1, length))
        return 0;
    return (int32)*length >= 0 || cfp_failure("Native Carbon string length is invalid");
}

static int string_c_length(uint32 bytes, uint32* length)
{
    for (uint32 index = 0; index < INT32_MAX; ++index) {
        if (bytes > UINT32_MAX - index)
            break;
        const uint8* byte = ROMlib_carbon_guest_memory(bytes + index, 1, 0);
        if (!byte)
            return 0;
        if (!*byte) {
            *length = index;
            return 1;
        }
    }
    return cfp_failure("Native Carbon C string exceeds the guest address range");
}

static uint32 string_funnel(const ROMlib_carbon_reference* reference, uint32 allocator, uint32 bytes, uint32 length,
                            uint32 encoding, uint32 external, uint32 reduce_unicode, uint32 has_pascal,
                            uint32 terminated, uint32 no_copy, uint32 deallocator)
{
    uint32 arguments[] = {allocator,      bytes,      length,     encoding, external,
                          reduce_unicode, has_pascal, terminated, no_copy,  deallocator},
           result = 0;
    if ((int32)length < 0)
        return 0;
    cfp_private(reference, 0x36eb8, arguments, NELEM(arguments), &result);
    return result;
}

uint32 ROMlib_cfstring_bytes(uint32 allocator, uint32 bytes, uint32 length, uint32 encoding, uint32 external)
{
    uint32 arguments[] = {allocator, bytes, length, encoding, external}, result = 0;
    ROMlib_carbon_reference reference;
    if (cfp_reference("CFStringCreateWithBytes", 0x37928, arguments, 5, &reference, &result))
        result = string_funnel(&reference, allocator, bytes, length, encoding, external, 1, 0, 0, 0, UINT32_MAX);
    return result;
}

uint32 ROMlib_cfstring_cstring(uint32 allocator, uint32 bytes, uint32 encoding)
{
    uint32 arguments[] = {allocator, bytes, encoding}, result = 0, length;
    ROMlib_carbon_reference reference;
    if (cfp_reference("CFStringCreateWithCString", 0x37748, arguments, 3, &reference, &result) &&
        string_c_length(bytes, &length))
        result = string_funnel(&reference, allocator, bytes, length, encoding, 0, 0, 0, 1, 0, UINT32_MAX);
    return result;
}

uint32 ROMlib_cfstring_cstring_no_copy(uint32 allocator, uint32 bytes, uint32 encoding, uint32 deallocator)
{
    uint32 arguments[] = {allocator, bytes, encoding, deallocator}, result = 0, length;
    ROMlib_carbon_reference reference;
    if (cfp_reference("CFStringCreateWithCStringNoCopy", 0x37814, arguments, 4, &reference, &result) &&
        string_c_length(bytes, &length))
        result = string_funnel(&reference, allocator, bytes, length, encoding, 0, 0, 0, 1, 1, deallocator);
    return result;
}

uint32 ROMlib_cfstring_pascal(uint32 allocator, uint32 bytes, uint32 encoding)
{
    uint32 arguments[] = {allocator, bytes, encoding}, result = 0;
    ROMlib_carbon_reference reference;
    if (cfp_reference("CFStringCreateWithPascalString", 0x376fc, arguments, 3, &reference, &result)) {
        const uint8* length = ROMlib_carbon_guest_memory(bytes, 1, 0);
        if (length)
            result = string_funnel(&reference, allocator, bytes, *length + 1, encoding, 0, 0, 1, 0, 0, UINT32_MAX);
    }
    return result;
}

uint32 ROMlib_cfstring_pascal_no_copy(uint32 allocator, uint32 bytes, uint32 encoding, uint32 deallocator)
{
    uint32 arguments[] = {allocator, bytes, encoding, deallocator}, result = 0;
    ROMlib_carbon_reference reference;
    if (cfp_reference("CFStringCreateWithPascalStringNoCopy", 0x377c8, arguments, 4, &reference, &result)) {
        const uint8* length = ROMlib_carbon_guest_memory(bytes, 1, 0);
        if (length)
            result = string_funnel(&reference, allocator, bytes, *length + 1, encoding, 0, 0, 1, 0, 1, deallocator);
    }
    return result;
}

uint32 ROMlib_cfstring_characters(uint32 allocator, uint32 characters, uint32 length)
{
    uint32 arguments[] = {allocator, characters, length}, result = 0;
    ROMlib_carbon_reference reference;
    if (cfp_reference("CFStringCreateWithCharacters", 0x3789c, arguments, 3, &reference, &result) &&
        length <= INT32_MAX / 2)
        result = string_funnel(&reference, allocator, characters, length * 2, 0x100, 0, 1, 0, 0, 0, UINT32_MAX);
    return result;
}

uint32 ROMlib_cfstring_characters_no_copy(uint32 allocator, uint32 characters, uint32 length, uint32 deallocator)
{
    uint32 arguments[] = {allocator, characters, length, deallocator}, result = 0;
    ROMlib_carbon_reference reference;
    if (cfp_reference("CFStringCreateWithCharactersNoCopy", 0x378e4, arguments, 4, &reference, &result) &&
        length <= INT32_MAX / 2)
        result = string_funnel(&reference, allocator, characters, length * 2, 0x100, 0, 0, 0, 0, 1, deallocator);
    return result;
}

static uint32 string_mutable(const ROMlib_carbon_reference* reference, uint32 allocator, uint32 capacity)
{
    /* The original mutable-info word reserves its upper seven bits for flags. */
    if ((int32)capacity < 0 || capacity > 0x01ffffff)
        return 0;
    if (!allocator && !cfp_call("CFAllocatorGetDefault", NULL, 0, &allocator))
        return 0;
    uint32 create[] = {allocator, cfp_toc(reference, 0x990), 20, 0}, object = 0;
    if (!cfp_call("_CFRuntimeCreateInstance", create, 4, &object) || !object)
        return 0;
    if (!cfp_write(object + 4, (cfp_read(object + 4) & ~0x7fU) | 0x21) || !cfp_write(object + 8, 0) ||
        !cfp_write(object + 12, 0) || !cfp_write(object + 16, 0) ||
        !cfp_write(object + 20, capacity ? 0x80000000U | capacity : 32))
        return 0;
    return object;
}

uint32 ROMlib_cfstring_create_mutable(uint32 allocator, uint32 capacity)
{
    uint32 arguments[] = {allocator, capacity}, result = 0;
    ROMlib_carbon_reference reference;
    if (cfp_reference("CFStringCreateMutable", 0x38720, arguments, 2, &reference, &result))
        result = string_mutable(&reference, allocator, capacity);
    return result;
}

uint32 ROMlib_cfstring_mutable_copy(uint32 allocator, uint32 capacity, uint32 source)
{
    uint32 arguments[] = {allocator, capacity, source}, result = 0, ignored;
    ROMlib_carbon_reference reference;
    if (!cfp_reference("CFStringCreateMutableCopy", 0x387ec, arguments, 3, &reference, &result))
        return result;
    result = string_mutable(&reference, allocator, capacity);
    uint32 replace[] = {result, 0, 0, source};
    if (!result || !cfp_private(&reference, 0x38424, replace, 4, &ignored))
        return 0;
    return result;
}

static int string_set_external(uint32 object, uint32 characters, uint32 length, uint32 capacity)
{
    if ((int32)length < 0 || length > capacity || capacity > INT32_MAX / 2)
        return cfp_failure("Native Carbon external string length or capacity is invalid");
    if (!cfp_write(object + 8, characters))
        return 0;
    uint32 flags;
    if (!cfp_get(object + 4, &flags) || !cfp_write(object + ((flags & 0x60) ? 12 : 8), length) ||
        !cfp_write(object + 16, capacity * 2))
        return 0;
    return cfp_write(object + 20, cfp_read(object + 20) | 0x20000000U);
}

void ROMlib_cfstring_set_external(uint32 object, uint32 characters, uint32 length, uint32 capacity)
{
    uint32 arguments[] = {object, characters, length, capacity}, ignored = 0;
    ROMlib_carbon_reference reference;
    if (cfp_reference("CFStringSetExternalCharactersNoCopy", 0x3d110, arguments, 4, &reference, &ignored))
        string_set_external(object, characters, length, capacity);
}

uint32 ROMlib_cfstring_mutable_external(uint32 allocator, uint32 characters, uint32 length, uint32 capacity,
                                        uint32 deallocator)
{
    uint32 arguments[] = {allocator, characters, length, capacity, deallocator}, result = 0;
    ROMlib_carbon_reference reference;
    if (!cfp_reference("CFStringCreateMutableWithExternalCharactersNoCopy", 0x385d0, arguments, 5, &reference, &result))
        return result;
    if ((int32)length < 0 || length > capacity || capacity > INT32_MAX / 2)
        return 0;
    uint32 mode = !deallocator ? 0x20 : deallocator == cfp_toc(&reference, 0x316c) ? 0x40 : 0x60;
    if (!allocator && !cfp_call("CFAllocatorGetDefault", NULL, 0, &allocator))
        return 0;
    uint32 create[] = {allocator, cfp_toc(&reference, 0x990), 20, 0};
    if (!cfp_call("_CFRuntimeCreateInstance", create, 4, &result) || !result ||
        !cfp_write(result + 4, (cfp_read(result + 4) & ~0x7fU) | mode | 0x13) || !cfp_write(result + 8, 0) ||
        !cfp_write(result + 12, 0) || !cfp_write(result + 16, 0) || !cfp_write(result + 20, 32))
        return 0;
    if (mode == 0x60) {
        uint32 retained, slot;
        if (!cfp_call("CFRetain", &deallocator, 1, &retained) || !cfp_private(&reference, 0x35434, &result, 1, &slot) ||
            !cfp_write(slot, retained))
            return 0;
    }
    return string_set_external(result, characters, length, capacity) ? result : 0;
}

uint32 ROMlib_cfstring_from_external(uint32 allocator, uint32 data, uint32 encoding)
{
    uint32 arguments[] = {allocator, data, encoding}, result = 0, bytes, length;
    ROMlib_carbon_reference reference;
    if (cfp_reference("CFStringCreateFromExternalRepresentation", 0x3c61c, arguments, 3, &reference, &result) &&
        cfp_call("CFDataGetLength", &data, 1, &length) && cfp_call("CFDataGetBytePtr", &data, 1, &bytes))
        result = string_funnel(&reference, allocator, bytes, length, encoding, 1, 1, 0, 0, 0, UINT32_MAX);
    return result;
}

uint32 ROMlib_cfstring_substring(uint32 allocator, uint32 source, uint32 location, uint32 length)
{
    uint32 arguments[] = {allocator, source, location, length}, result = 0, source_length;
    ROMlib_carbon_reference reference;
    if (!cfp_reference("CFStringCreateWithSubstring", 0x37ad0, arguments, 4, &reference, &result))
        return result;
    if (!string_length(source, &source_length) || (uint64)location + length > source_length || length > INT32_MAX / 2)
        return 0;
    if (!location && length == source_length) {
        uint32 copy[] = {allocator, source};
        cfp_call("CFStringCreateCopy", copy, 2, &result);
        return result;
    }
    _Alignas(512) uint16 local[256];
    uint32 buffer = US_TO_SYN68K(local), owner = allocator, ignored;
    if (length > NELEM(local)) {
        if (!owner && !cfp_call("CFAllocatorGetDefault", NULL, 0, &owner))
            return 0;
        uint32 allocate[] = {owner, length * 2, 0};
        if (!cfp_call("CFAllocatorAllocate", allocate, 3, &buffer) || !buffer)
            return 0;
    }
    uint32 extract[] = {source, location, length, buffer};
    if (!cfp_call("CFStringGetCharacters", extract, 4, &ignored))
        return 0;
    result = string_funnel(&reference, allocator, buffer, length * 2, 0x100, 0, 1, 0, 0, 0, UINT32_MAX);
    if (length > NELEM(local)) {
        uint32 release[] = {owner, buffer};
        cfp_call("CFAllocatorDeallocate", release, 2, &ignored);
    }
    return result;
}

void ROMlib_cfstring_insert(uint32 object, uint32 index, uint32 source)
{
    uint32 arguments[] = {object, index, source}, ignored = 0;
    ROMlib_carbon_reference reference;
    if (cfp_reference("CFStringInsert", 0x3d148, arguments, 3, &reference, &ignored)) {
        uint32 replace[] = {object, index, 0, source};
        cfp_private(&reference, 0x38424, replace, 4, &ignored);
    }
}

void ROMlib_cfstring_delete(uint32 object, uint32 location, uint32 length)
{
    uint32 arguments[] = {object, location, length}, ignored = 0;
    ROMlib_carbon_reference reference;
    if (cfp_reference("CFStringDelete", 0x3d190, arguments, 3, &reference, &ignored)) {
        uint32 resize[] = {object, location, length, 0, 0};
        cfp_private(&reference, 0x35918, resize, 5, &ignored);
    }
}

void ROMlib_cfstring_replace(uint32 object, uint32 location, uint32 length, uint32 source)
{
    uint32 arguments[] = {object, location, length, source}, ignored = 0;
    ROMlib_carbon_reference reference;
    if (cfp_reference("CFStringReplace", 0x3d1c8, arguments, 4, &reference, &ignored))
        cfp_private(&reference, 0x38424, arguments, 4, &ignored);
}

void ROMlib_cfstring_replace_all(uint32 object, uint32 source)
{
    uint32 arguments[] = {object, source}, ignored = 0, length;
    ROMlib_carbon_reference reference;
    if (cfp_reference("CFStringReplaceAll", 0x3d1f8, arguments, 2, &reference, &ignored) &&
        string_length(object, &length)) {
        uint32 replace[] = {object, 0, length, source};
        cfp_private(&reference, 0x38424, replace, 4, &ignored);
    }
}

void ROMlib_cfstring_append(uint32 object, uint32 source)
{
    uint32 arguments[] = {object, source}, ignored = 0, length;
    ROMlib_carbon_reference reference;
    if (cfp_reference("CFStringAppend", 0x3d2f8, arguments, 2, &reference, &ignored) &&
        string_length(object, &length)) {
        uint32 replace[] = {object, length, 0, source};
        cfp_private(&reference, 0x38424, replace, 4, &ignored);
    }
}

void ROMlib_cfstring_append_characters(uint32 object, uint32 characters, uint32 count)
{
    uint32 arguments[] = {object, characters, count}, ignored = 0, length;
    ROMlib_carbon_reference reference;
    if (!cfp_reference("CFStringAppendCharacters", 0x3d3f8, arguments, 3, &reference, &ignored) ||
        !string_length(object, &length))
        return;
    if (count > INT32_MAX / 2 || (uint64)length + count > INT32_MAX / 2) {
        cfp_failure("Native Carbon string append length overflow");
        return;
    }
    uint32 resize[] = {object, length, 0, count, 1};
    if (!cfp_private(&reference, 0x35918, resize, 5, &ignored))
        return;
    uint32 flags;
    if (!cfp_get(object + 4, &flags))
        return;
    uint32 bytes = (flags & 0x60) ? cfp_read(object + 8) : object + 8 + ((flags & 5) == 4 ? 0 : 4);
    cfp_move(bytes + length * 2, characters, count * 2);
}

void ROMlib_cfstring_append_pascal(uint32 object, uint32 bytes, uint32 encoding)
{
    uint32 arguments[] = {object, bytes, encoding}, ignored = 0;
    ROMlib_carbon_reference reference;
    if (cfp_reference("CFStringAppendPascalString", 0x3d824, arguments, 3, &reference, &ignored)) {
        const uint8* length = ROMlib_carbon_guest_memory(bytes, 1, 0);
        if (length) {
            uint32 append[] = {object, bytes + 1, *length, encoding};
            cfp_private(&reference, 0x3d540, append, 4, &ignored);
        }
    }
}

void ROMlib_cfstring_append_cstring(uint32 object, uint32 bytes, uint32 encoding)
{
    uint32 arguments[] = {object, bytes, encoding}, ignored = 0, length;
    ROMlib_carbon_reference reference;
    if (cfp_reference("CFStringAppendCString", 0x3d854, arguments, 3, &reference, &ignored) &&
        string_c_length(bytes, &length)) {
        uint32 append[] = {object, bytes, length, encoding};
        cfp_private(&reference, 0x3d540, append, 4, &ignored);
    }
}
