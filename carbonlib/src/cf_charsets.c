/*+===================================================================
  File:      cf_charsets.c

  Summary:   CoreFoundation 1.4 character sets share the original registered class. Native mutations use the compatible
             8192-byte bitmap representation. The original compact/predefined Unicode-to-bitmap worker remains a
             dependency; public constructors, membership and set operations run here.

  Classes:   None.

  Functions: ROMlib_cfcharset_add_range, ROMlib_cfcharset_add_string, ROMlib_cfcharset_bitmap,
             ROMlib_cfcharset_create_bitmap, ROMlib_cfcharset_create_mutable, ROMlib_cfcharset_intersect,
             ROMlib_cfcharset_invert, ROMlib_cfcharset_member, ROMlib_cfcharset_mutable_copy,
             ROMlib_cfcharset_predefined, ROMlib_cfcharset_range, ROMlib_cfcharset_remove_range,
             ROMlib_cfcharset_remove_string, ROMlib_cfcharset_string, ROMlib_cfcharset_union

  Origin:    Existing repository source in packages/carbonlib/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* CoreFoundation 1.4 character sets share the original registered class.
   Native mutations use the compatible 8192-byte bitmap representation. The
   original compact/predefined Unicode-to-bitmap worker remains a dependency;
   public constructors, membership and set operations run here. */
#include "cf_batch.h"

enum { charset_bitmap_size = 8192 };

static int charset_owner(uint32 object, uint32* allocator)
{
    return cfp_call("CFGetAllocator", &object, 1, allocator);
}

static uint32 charset_allocate(const ROMlib_carbon_reference* reference, uint32 allocator, uint32 flags)
{
    uint32 arguments[] = {allocator, cfp_toc(reference, 0x96c), 12, 0};
    uint32 object = 0;
    if (!cfp_call("_CFRuntimeCreateInstance", arguments, 4, &object) || !object)
        return 0;
    if (!cfp_write(object + 4, cfp_read(object + 4) | flags) || !cfp_write(object + 8, 0))
        return 0;
    return object;
}

static int charset_buffer(uint32 object, uint32 size, uint32* buffer)
{
    uint32 allocator;
    if (!charset_owner(object, &allocator))
        return 0;
    uint32 arguments[] = {allocator, size, 0};
    if (!cfp_call("CFAllocatorAllocate", arguments, 3, buffer))
        return 0;
    return *buffer || cfp_failure("Native Carbon character-set allocation failed");
}

static int charset_free(uint32 object, uint32 buffer)
{
    uint32 allocator, ignored;
    if (!charset_owner(object, &allocator))
        return 0;
    uint32 arguments[] = {allocator, buffer};
    return cfp_call("CFAllocatorDeallocate", arguments, 2, &ignored);
}

/* This is the real 1.4 worker at 0x2fbf4, which understands all five existing
   representations, inversion and the initialized Unicode tables. */
static int charset_export_bitmap(const ROMlib_carbon_reference* reference, uint32 object, uint32 buffer)
{
    uint32 arguments[] = {object, buffer}, ignored;
    return cfp_private(reference, 0x2fbf4, arguments, 2, &ignored);
}

static uint32 charset_mutable(const ROMlib_carbon_reference* reference, uint32 allocator)
{
    uint32 object = charset_allocate(reference, allocator, 0x31);
    if (!object || !cfp_write(object + 12, 0) || !cfp_write(object + 4, cfp_read(object + 4) | 4))
        return 0;
    return object;
}

static int charset_materialize(const ROMlib_carbon_reference* reference, uint32 object, uint32* buffer)
{
    uint32 flags;
    if (!object || !cfp_get(object + 4, &flags))
        return cfp_failure("Native Carbon character set requires an object");
    if (!(flags & 1))
        return cfp_failure("Native Carbon character-set mutation requires a mutable object");
    uint32 kind = flags & 0x70, old_buffer;
    if (!cfp_get(object + 12, &old_buffer))
        return 0;
    if (kind == 0x30 && old_buffer) {
        *buffer = old_buffer;
        return cfp_write(object + 4, flags & ~4U);
    }
    uint32 replacement;
    if (!charset_buffer(object, charset_bitmap_size, &replacement) ||
        !charset_export_bitmap(reference, object, replacement))
        return 0;
    /* Bitmap bits already contain the inversion. Clear the separate inversion
       flag, invalidate the cached hash and publish before releasing old storage. */
    if (!cfp_write(object + 12, replacement) || !cfp_write(object + 4, (flags & ~0x7cU) | 0x30))
        return 0;
    *buffer = replacement;
    if (old_buffer && (kind == 0x20 || kind == 0x40))
        return charset_free(object, old_buffer);
    return 1;
}

uint32 ROMlib_cfcharset_predefined(uint32 kind)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (!cfp_reference("CFCharacterSetGetPredefined", 0x31ba4, &kind, 1, &reference, &result))
        return result;
    if (kind < 1 || kind > 12)
        return cfp_failure("Native Carbon predefined character-set identifier is invalid");
    uint32 cache = cfp_toc(&reference, 0x974);
    if (cache && cfp_get(cache + (kind - 1) * 4, &result) && result)
        return result;
    uint32 object = charset_allocate(&reference, cfp_toc(&reference, 0x3164), 0);
    if (!object || !cfp_write(object + 12, kind))
        return 0;
    if (!cache) {
        uint32 lock = US_TO_SYN68K(reference.toc) + 0x970, ignored, allocator;
        if (!cfp_private(&reference, 0x1a98c, &lock, 1, &ignored) ||
            !cfp_call("CFAllocatorGetDefault", NULL, 0, &allocator) || !cfp_call("CFRetain", &allocator, 1, &allocator))
            return 0;
        uint32 arguments[] = {allocator, 48, 0};
        if (!cfp_call("CFAllocatorAllocate", arguments, 3, &cache) || !cache || !cfp_clear(cache, 48) ||
            !cfp_write(US_TO_SYN68K(reference.toc) + 0x974, cache) ||
            !cfp_private(&reference, 0x1a990, &lock, 1, &ignored))
            return 0;
    }
    return cfp_write(cache + (kind - 1) * 4, object) ? object : 0;
}

uint32 ROMlib_cfcharset_range(uint32 allocator, uint32 location, uint32 length)
{
    uint32 arguments[] = {allocator, location, length}, result = 0;
    ROMlib_carbon_reference reference;
    if (!cfp_reference("CFCharacterSetCreateWithCharactersInRange", 0x31cb4, arguments, 3, &reference, &result))
        return result;
    if ((int32)location < 0 || (int32)length < 0 || (uint64)location + length > 65536)
        return 0;
    result = charset_allocate(&reference, allocator, 0x10 | (length ? 0 : 4));
    if (!result)
        return 0;
    uint16* start = ROMlib_carbon_guest_memory(result + 12, 2, 1);
    if (!start)
        return 0;
    uint16 encoded = CW((uint16)location);
    memcpy(start, &encoded, 2);
    return cfp_write(result + 16, length) ? result : 0;
}

uint32 ROMlib_cfcharset_create_mutable(uint32 allocator)
{
    uint32 result = 0;
    ROMlib_carbon_reference reference;
    if (cfp_reference("CFCharacterSetCreateMutable", 0x32428, &allocator, 1, &reference, &result))
        result = charset_mutable(&reference, allocator);
    return result;
}

uint32 ROMlib_cfcharset_bitmap(uint32 allocator, uint32 data)
{
    uint32 arguments[] = {allocator, data}, result = 0;
    ROMlib_carbon_reference reference;
    if (!cfp_reference("CFCharacterSetCreateWithBitmapRepresentation", 0x322b8, arguments, 2, &reference, &result))
        return result;
    uint32 object = charset_allocate(&reference, allocator, 0x30), length = 0;
    if (!object)
        return 0;
    if (data && !cfp_call("CFDataGetLength", &data, 1, &length))
        return 0;
    if ((int32)length < 0 || length > charset_bitmap_size)
        return cfp_failure("Native Carbon character-set bitmap exceeds the Unicode BMP");
    if (!length)
        return cfp_write(object + 4, cfp_read(object + 4) | 4) ? object : 0;
    uint32 buffer, source;
    if (!charset_buffer(object, charset_bitmap_size, &buffer) || !cfp_call("CFDataGetBytePtr", &data, 1, &source) ||
        !cfp_move(buffer, source, length) || !cfp_clear(buffer + length, charset_bitmap_size - length) ||
        !cfp_write(object + 12, buffer))
        return 0;
    return object;
}

uint32 ROMlib_cfcharset_mutable_copy(uint32 allocator, uint32 source)
{
    uint32 arguments[] = {allocator, source}, result = 0;
    ROMlib_carbon_reference reference;
    if (!cfp_reference("CFCharacterSetCreateMutableCopy", 0x324b4, arguments, 2, &reference, &result))
        return result;
    uint32 object = charset_mutable(&reference, allocator), flags;
    if (!object || !source || !cfp_get(source + 4, &flags))
        return 0;
    uint32 kind = flags & 0x70;
    if (kind == 0 || kind == 0x10) {
        if (!cfp_move(object + 12, source + 12, 8))
            return 0;
    }
    else {
        uint32 old_buffer;
        if (!cfp_get(source + 12, &old_buffer))
            return 0;
        if (old_buffer) {
            uint32 buffer, size = kind == 0x20 ? cfp_read(source + 16) * 2 : charset_bitmap_size;
            if (kind == 0x20 && size < 64)
                size = 64;
            if (!charset_buffer(object, size, &buffer))
                return 0;
            if (kind == 0x40) {
                if (!charset_export_bitmap(&reference, source, buffer))
                    return 0;
                flags = (flags & ~0x78U) | 0x30;
            }
            else if (!cfp_move(buffer, old_buffer, kind == 0x20 ? cfp_read(source + 16) * 2 : size))
                return 0;
            if (!cfp_write(object + 12, buffer) || !cfp_write(object + 16, cfp_read(source + 16)))
                return 0;
        }
    }
    if (!cfp_write(object + 4, (cfp_read(object + 4) & ~0x7cU) | (flags & 0x7c)) ||
        !cfp_write(object + 8, cfp_read(source + 8)))
        return 0;
    return object;
}

static int charset_u16(uint32 address, uint32* value)
{
    const void* memory = ROMlib_carbon_guest_memory(address, 2, 0);
    uint16 encoded;
    if (!memory)
        return 0;
    memcpy(&encoded, memory, 2);
    *value = CW(encoded);
    return 1;
}

uint32 ROMlib_cfcharset_member(uint32 object, uint32 character)
{
    uint32 arguments[] = {object, character}, result = 0, flags, payload;
    ROMlib_carbon_reference reference;
    if (!cfp_reference("CFCharacterSetIsCharacterMember", 0x326b0, arguments, 2, &reference, &result))
        return result;
    if (!cfp_get(object + 4, &flags) || !cfp_get(object + 12, &payload))
        return 0;
    character &= 65535;
    int found = 0;
    switch (flags & 0x70) {
    case 0: {
        uint32 unicode_arguments[] = {character, payload};
        if (!cfp_private(&reference, 0x461fc, unicode_arguments, 2, &result))
            return 0;
        found = (result & 255) != 0;
        break;
    }
    case 0x10: {
        uint32 start, length;
        if (!charset_u16(object + 12, &start) || !cfp_get(object + 16, &length))
            return 0;
        found = length && character >= start && character < start + length;
        break;
    }
    case 0x20: {
        uint32 count;
        if (!cfp_get(object + 16, &count) || count > 65536)
            return cfp_failure("Native Carbon character-set list is invalid");
        uint32 lo = 0, hi = count;
        while (lo < hi) {
            uint32 middle = lo + (hi - lo) / 2, value;
            if (!charset_u16(payload + middle * 2, &value))
                return 0;
            if (value == character) {
                found = 1;
                break;
            }
            if (value < character)
                lo = middle + 1;
            else
                hi = middle;
        }
        break;
    }
    case 0x30:
        if (payload) {
            const uint8* byte = ROMlib_carbon_guest_memory(payload + character / 8, 1, 0);
            return byte ? ((*byte >> (character & 7)) & 1) : 0;
        }
        break;
    case 0x40:
        if (payload && cfp_read(payload) == 1) {
            uint32 singles, ranges;
            if (!cfp_get(payload + 20, &singles) || !cfp_get(payload + 24, &ranges) || singles > 65536 ||
                ranges > 65536)
                return cfp_failure("Native Carbon compact character set is invalid");
            for (uint32 i = 0; i < singles; ++i) {
                uint32 value;
                if (!cfp_get(payload + 36 + i * 4, &value))
                    return 0;
                if (value == character) {
                    found = 1;
                    break;
                }
            }
            for (uint32 i = 0; !found && i < ranges; ++i) {
                uint32 start, end, address = payload + 36 + singles * 4 + i * 8;
                if (!cfp_get(address, &start) || !cfp_get(address + 4, &end))
                    return 0;
                found = character >= start && character <= end;
            }
        }
        break;
    default:
        return cfp_failure("Native Carbon character-set representation is invalid");
    }
    return found ^ ((flags & 8) != 0);
}

uint32 ROMlib_cfcharset_create_bitmap(uint32 allocator, uint32 object)
{
    uint32 arguments[] = {allocator, object}, result = 0;
    ROMlib_carbon_reference reference;
    if (!cfp_reference("CFCharacterSetCreateBitmapRepresentation", 0x32ab0, arguments, 2, &reference, &result))
        return result;
    uint32 create[] = {allocator, charset_bitmap_size};
    if (!cfp_call("CFDataCreateMutable", create, 2, &result) || !result)
        return 0;
    uint32 resize[] = {result, charset_bitmap_size}, ignored, bytes;
    if (!cfp_call("CFDataSetLength", resize, 2, &ignored) || !cfp_call("CFDataGetMutableBytePtr", &result, 1, &bytes) ||
        !charset_export_bitmap(&reference, object, bytes))
        return 0;
    return result;
}

static int charset_change_range(const ROMlib_carbon_reference* reference, uint32 object, uint32 location, uint32 length,
                                int add)
{
    if ((int32)location < 0 || (int32)length < 0 || (uint64)location + length > 65536)
        return cfp_failure("Native Carbon character-set range is invalid");
    if (!length)
        return 1;
    uint32 buffer;
    if (!charset_materialize(reference, object, &buffer))
        return 0;
    uint8* bytes = ROMlib_carbon_guest_memory(buffer, charset_bitmap_size, 1);
    if (!bytes)
        return 0;
    for (uint32 character = location; character < location + length; ++character) {
        uint8 mask = 1U << (character & 7);
        if (add)
            bytes[character / 8] |= mask;
        else
            bytes[character / 8] &= (uint8)~mask;
    }
    return 1;
}

void ROMlib_cfcharset_add_range(uint32 object, uint32 location, uint32 length)
{
    uint32 arguments[] = {object, location, length}, ignored = 0;
    ROMlib_carbon_reference reference;
    if (cfp_reference("CFCharacterSetAddCharactersInRange", 0x32b18, arguments, 3, &reference, &ignored))
        charset_change_range(&reference, object, location, length, 1);
}

void ROMlib_cfcharset_remove_range(uint32 object, uint32 location, uint32 length)
{
    uint32 arguments[] = {object, location, length}, ignored = 0;
    ROMlib_carbon_reference reference;
    if (cfp_reference("CFCharacterSetRemoveCharactersInRange", 0x330b8, arguments, 3, &reference, &ignored))
        charset_change_range(&reference, object, location, length, 0);
}

static int charset_change_string(const ROMlib_carbon_reference* reference, uint32 object, uint32 string, int add)
{
    uint32 length, buffer;
    if (!cfp_call("CFStringGetLength", &string, 1, &length))
        return 0;
    if (!length)
        return 1;
    if ((int32)length < 0 || !charset_materialize(reference, object, &buffer))
        return 0;
    for (uint32 index = 0; index < length;) {
        _Alignas(512) uint16 characters[256];
        uint32 count = length - index < 256 ? length - index : 256, ignored;
        uint32 arguments[] = {string, index, count, US_TO_SYN68K(characters)};
        if (!cfp_call("CFStringGetCharacters", arguments, 4, &ignored))
            return 0;
        uint8* bytes = ROMlib_carbon_guest_memory(buffer, charset_bitmap_size, 1);
        if (!bytes)
            return 0;
        for (uint32 i = 0; i < count; ++i) {
            uint32 character = CW(characters[i]);
            uint8 mask = 1U << (character & 7);
            if (add)
                bytes[character / 8] |= mask;
            else
                bytes[character / 8] &= (uint8)~mask;
        }
        index += count;
    }
    return 1;
}

void ROMlib_cfcharset_add_string(uint32 object, uint32 string)
{
    uint32 arguments[] = {object, string}, ignored = 0;
    ROMlib_carbon_reference reference;
    if (cfp_reference("CFCharacterSetAddCharactersInString", 0x33658, arguments, 2, &reference, &ignored))
        charset_change_string(&reference, object, string, 1);
}

void ROMlib_cfcharset_remove_string(uint32 object, uint32 string)
{
    uint32 arguments[] = {object, string}, ignored = 0;
    ROMlib_carbon_reference reference;
    if (cfp_reference("CFCharacterSetRemoveCharactersInString", 0x33cf0, arguments, 2, &reference, &ignored))
        charset_change_string(&reference, object, string, 0);
}

uint32 ROMlib_cfcharset_string(uint32 allocator, uint32 string)
{
    uint32 arguments[] = {allocator, string}, result = 0;
    ROMlib_carbon_reference reference;
    if (!cfp_reference("CFCharacterSetCreateWithCharactersInString", 0x31d74, arguments, 2, &reference, &result))
        return result;
    result = charset_mutable(&reference, allocator);
    if (!result || !charset_change_string(&reference, result, string, 1) ||
        !cfp_write(result + 4, cfp_read(result + 4) & ~1U))
        return 0;
    return result;
}

static void charset_combine(const ROMlib_carbon_reference* reference, uint32 object, uint32 other, int intersect)
{
    /* Snapshot before materializing the receiver, including self operations.
       A host stack array may cross a 128-KiB alias slot: guest pointer arithmetic
       would then enter an unrelated mapping. Give the PPC worker real guest
       storage and validate its entire extent before allowing it to write. */
    uint32 snapshot, buffer;
    if (!charset_buffer(object, charset_bitmap_size, &snapshot))
        return;
    if (!ROMlib_carbon_guest_memory(snapshot, charset_bitmap_size, 1) ||
        !charset_export_bitmap(reference, other, snapshot) || !charset_materialize(reference, object, &buffer)) {
        charset_free(object, snapshot);
        return;
    }
    const uint8* input = ROMlib_carbon_guest_memory(snapshot, charset_bitmap_size, 0);
    uint8* bytes = ROMlib_carbon_guest_memory(buffer, charset_bitmap_size, 1);
    if (input && bytes)
        for (unsigned i = 0; i < charset_bitmap_size; ++i)
            bytes[i] = intersect ? bytes[i] & input[i] : bytes[i] | input[i];
    charset_free(object, snapshot);
}

void ROMlib_cfcharset_union(uint32 object, uint32 other)
{
    uint32 arguments[] = {object, other}, ignored = 0;
    ROMlib_carbon_reference reference;
    if (cfp_reference("CFCharacterSetUnion", 0x34388, arguments, 2, &reference, &ignored))
        charset_combine(&reference, object, other, 0);
}

void ROMlib_cfcharset_intersect(uint32 object, uint32 other)
{
    uint32 arguments[] = {object, other}, ignored = 0;
    ROMlib_carbon_reference reference;
    if (cfp_reference("CFCharacterSetIntersect", 0x348a4, arguments, 2, &reference, &ignored))
        charset_combine(&reference, object, other, 1);
}

void ROMlib_cfcharset_invert(uint32 object)
{
    uint32 ignored = 0, flags;
    ROMlib_carbon_reference reference;
    if (!cfp_reference("CFCharacterSetInvert", 0x34f74, &object, 1, &reference, &ignored) ||
        !cfp_get(object + 4, &flags))
        return;
    if (!(flags & 1)) {
        cfp_failure("Native Carbon character-set inversion requires a mutable object");
        return;
    }
    if ((flags & 0x70) == 0x30) {
        uint32 buffer;
        if (!charset_materialize(&reference, object, &buffer))
            return;
        uint8* bytes = ROMlib_carbon_guest_memory(buffer, charset_bitmap_size, 1);
        if (!bytes)
            return;
        for (unsigned i = 0; i < charset_bitmap_size; ++i)
            bytes[i] = (uint8)~bytes[i];
    }
    else
        cfp_write(object + 4, (flags ^ 8) & ~4U);
}
