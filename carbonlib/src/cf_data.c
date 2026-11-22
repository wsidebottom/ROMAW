/*+===================================================================
  File:      cf_data.c

  Summary:   CFData 1.4 factory and mutation control flow (0x1e204..0x1e8cc). Runtime class registration, object
             allocation and allocator callbacks remain real dependencies. This module is part of the 102-entry
             collection/value cohort.

  Classes:   None.

  Functions: ROMlib_cfdata_append, ROMlib_cfdata_create, ROMlib_cfdata_create_copy, ROMlib_cfdata_create_mutable,
             ROMlib_cfdata_create_mutable_copy, ROMlib_cfdata_create_no_copy, ROMlib_cfdata_delete,
             ROMlib_cfdata_increase_length, ROMlib_cfdata_replace, ROMlib_cfdata_set_length, ROMlib_cfdata_type_id

  Origin:    Existing repository source in packages/carbonlib/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* CFData 1.4 factory and mutation control flow (0x1e204..0x1e8cc).
   Runtime class registration, object allocation and allocator callbacks remain
   real dependencies. This module is part of the 102-entry collection/value cohort. */
#include "cf_batch.h"
#include <limits.h>

static int data_state(uint32 object, uint32* length, uint32* capacity, uint32* mode)
{
    if (!object)
        return cfp_failure("Native Carbon CFData requires an object");
    *length = cfp_read(object + 8);
    *capacity = cfp_read(object + 12);
    *mode = (cfp_read(object + 4) >> 2) & 3;
    if ((int32)*length < 0 || (int32)*capacity < 0 || *length > *capacity)
        return cfp_failure("Native Carbon CFData has an invalid length or capacity");
    return 1;
}

static int data_grow(uint32 object, uint32 additional)
{
    uint32 length, old_capacity, mode;
    if (!data_state(object, &length, &old_capacity, &mode))
        return 0;
    uint64 required = (uint64)length + additional;
    uint64 capacity = 16;
    if (required >= 16)
        while (capacity <= required)
            capacity *= 2;
    if (capacity > INT32_MAX)
        return cfp_failure("Native Carbon CFData capacity overflow");
    uint32 allocator, replacement;
    if (!cfp_call("CFGetAllocator", &object, 1, &allocator))
        return 0;
    uint32 arguments[] = {allocator, cfp_read(object + 20), (uint32)capacity, 0};
    if (!cfp_call("CFAllocatorReallocate", arguments, 4, &replacement))
        return 0;
    if (!replacement)
        return cfp_failure("Native Carbon CFData growth allocation failed");
    /* The original publishes capacity and overwrites its buffer before checking
       NULL; its fatal worker is not a safe recovery path. Keep valid old state
       until reallocation succeeds. */
    cfp_write(object + 12, (uint32)capacity);
    cfp_write(object + 20, replacement);
    return 1;
}

static int data_replace(uint32 object, uint32 location, uint32 removed, uint32 bytes, uint32 inserted)
{
    uint32 length, capacity, mode;
    if (!data_state(object, &length, &capacity, &mode))
        return 0;
    if ((int32)location < 0 || (int32)removed < 0 || (int32)inserted < 0 || (uint64)location + removed > length)
        return cfp_failure("Native Carbon CFData replacement range is invalid");
    uint64 required = (uint64)length - removed + inserted;
    if (required > INT32_MAX)
        return cfp_failure("Native Carbon CFData replacement length overflow");
    if (required > capacity) {
        if (mode != 1)
            return cfp_failure("Native Carbon CFData replacement exceeds fixed capacity");
        if (!data_grow(object, inserted - removed))
            return 0;
    }
    uint32 storage = cfp_read(object + 20);
    uint32 tail = length - location - removed;
    /* Preserve tail movement before reading replacement bytes, including
       observable aliases. A reallocating operation retains the original raw
       source word, just like the reference. */
    if (inserted != removed && tail && !cfp_move(storage + location + inserted, storage + location + removed, tail))
        return 0;
    if (inserted && !cfp_move(storage + location, bytes, inserted))
        return 0;
    cfp_write(object + 8, (uint32)required);
    return 1;
}

static uint32 data_factory(const ROMlib_carbon_reference* reference, uint32 allocator, uint32 mode, uint32 capacity,
                           uint32 bytes, uint32 length, uint32 deallocator)
{
    mode &= 3;
    uint32 copy_marker = cfp_toc(reference, 0x316c);
    if ((int32)capacity < 0 || (int32)length < 0 || (mode != 1 && deallocator == copy_marker && length > capacity))
        return 0;
    uint64 extra = 16;
    if (mode != 1 && deallocator == copy_marker)
        extra += capacity;
    if (extra > INT32_MAX)
        return 0;
    uint32 create[] = {allocator, cfp_toc(reference, 0x8c4), (uint32)extra, 0}, object;
    if (!cfp_call("_CFRuntimeCreateInstance", create, 4, &object) || !object)
        return 0;
    cfp_write(object + 8, 0);
    if (mode == 1) {
        cfp_write(object + 12, 16);
        uint32 allocate[] = {allocator, 16, 0}, buffer;
        if (!cfp_call("CFAllocatorAllocate", allocate, 3, &buffer))
            return 0;
        cfp_write(object + 20, buffer);
        if (!buffer) {
            cfp_call("CFRelease", &object, 1, NULL);
            return 0;
        }
        cfp_write(object + 16, 0);
    }
    else if (mode == 3) {
        cfp_write(object + 12, capacity);
        cfp_write(object + 20, object + 24);
        cfp_write(object + 16, 0);
    }
    else {
        cfp_write(object + 12, capacity);
        if (deallocator != copy_marker) {
            cfp_write(object + 20, bytes);
            uint32 owner = deallocator;
            if (!owner && !cfp_call("CFAllocatorGetDefault", NULL, 0, &owner))
                return 0;
            if (!cfp_call("CFRetain", &owner, 1, &owner))
                return 0;
            cfp_write(object + 16, owner);
            cfp_write(object + 8, length);
        }
        else {
            cfp_write(object + 20, object + 24);
            cfp_write(object + 16, 0);
        }
    }
    if (deallocator == copy_marker) {
        cfp_write(object + 4, (cfp_read(object + 4) & ~12u) | (mode == 1 ? 4 : 12));
        if (!data_replace(object, 0, 0, bytes, length))
            return 0;
    }
    cfp_write(object + 4, (cfp_read(object + 4) & ~12u) | (mode << 2));
    return object;
}

uint32 ROMlib_cfdata_type_id(void)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (cfp_reference("CFDataGetTypeID", 0x1e204, NULL, 0, &reference, &result))
        result = cfp_toc(&reference, 0x8c4);
    return result;
}

uint32 ROMlib_cfdata_create(uint32 allocator, uint32 bytes, uint32 length)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, bytes, length}, result = 0;
    if (cfp_reference("CFDataCreate", 0x1e3f0, arguments, 3, &reference, &result))
        result = data_factory(&reference, allocator, 0, length, bytes, length, cfp_toc(&reference, 0x316c));
    return result;
}

uint32 ROMlib_cfdata_create_no_copy(uint32 allocator, uint32 bytes, uint32 length, uint32 deallocator)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, bytes, length, deallocator}, result = 0;
    if (cfp_reference("CFDataCreateWithBytesNoCopy", 0x1e424, arguments, 4, &reference, &result))
        result = data_factory(&reference, allocator, 0, length, bytes, length, deallocator);
    return result;
}

uint32 ROMlib_cfdata_create_copy(uint32 allocator, uint32 data)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, data}, result = 0;
    if (cfp_reference("CFDataCreateCopy", 0x1e458, arguments, 2, &reference, &result)) {
        uint32 length = cfp_read(data + 8), bytes = cfp_read(data + 20);
        result = data_factory(&reference, allocator, 0, length, bytes, length, cfp_toc(&reference, 0x316c));
    }
    return result;
}

uint32 ROMlib_cfdata_create_mutable(uint32 allocator, uint32 capacity)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, capacity}, result = 0;
    if (cfp_reference("CFDataCreateMutable", 0x1e4d0, arguments, 2, &reference, &result))
        result = data_factory(&reference, allocator, capacity ? 3 : 1, capacity, 0, 0, cfp_toc(&reference, 0x316c));
    return result;
}

uint32 ROMlib_cfdata_create_mutable_copy(uint32 allocator, uint32 capacity, uint32 data)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, capacity, data}, result = 0;
    if (cfp_reference("CFDataCreateMutableCopy", 0x1e514, arguments, 3, &reference, &result)) {
        uint32 length = cfp_read(data + 8), bytes = cfp_read(data + 20);
        result =
            data_factory(&reference, allocator, capacity ? 3 : 1, capacity, bytes, length, cfp_toc(&reference, 0x316c));
    }
    return result;
}

void ROMlib_cfdata_replace(uint32 data, uint32 location, uint32 length, uint32 bytes, uint32 count)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {data, location, length, bytes, count}, ignored = 0;
    if (cfp_reference("CFDataReplaceBytes", 0x1e7dc, arguments, 5, &reference, &ignored))
        data_replace(data, location, length, bytes, count);
}

static int data_set_length(uint32 data, uint32 length)
{
    uint32 old_length, capacity, mode;
    if (!data_state(data, &old_length, &capacity, &mode))
        return 0;
    if ((int32)length < 0)
        return cfp_failure("Native Carbon CFData length is negative");
    /* Unlike replace, the reference set-length path reallocates on every
       variable-length increase, even when the old capacity is sufficient. */
    if (mode == 1 && length > old_length) {
        if (!data_grow(data, length - old_length))
            return 0;
    }
    else if (length > capacity)
        return cfp_failure("Native Carbon CFData length exceeds fixed capacity");
    if (length > old_length && !cfp_clear(cfp_read(data + 20) + old_length, length - old_length))
        return 0;
    cfp_write(data + 8, length);
    return 1;
}

void ROMlib_cfdata_set_length(uint32 data, uint32 length)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {data, length}, ignored = 0;
    if (cfp_reference("CFDataSetLength", 0x1e68c, arguments, 2, &reference, &ignored))
        data_set_length(data, length);
}

void ROMlib_cfdata_increase_length(uint32 data, uint32 extra)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {data, extra}, ignored = 0;
    if (cfp_reference("CFDataIncreaseLength", 0x1e724, arguments, 2, &reference, &ignored)) {
        uint32 length = cfp_read(data + 8);
        int64 required = (int64)(int32)length + (int32)extra;
        if (required < 0 || required > INT32_MAX) {
            cfp_failure("Native Carbon CFData length increase overflow");
            return;
        }
        data_set_length(data, (uint32)required);
    }
}

void ROMlib_cfdata_append(uint32 data, uint32 bytes, uint32 count)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {data, bytes, count}, ignored = 0;
    if (cfp_reference("CFDataAppendBytes", 0x1e74c, arguments, 3, &reference, &ignored))
        data_replace(data, cfp_read(data + 8), 0, bytes, count);
}

void ROMlib_cfdata_delete(uint32 data, uint32 location, uint32 length)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {data, location, length}, ignored = 0;
    if (cfp_reference("CFDataDeleteBytes", 0x1e7a0, arguments, 3, &reference, &ignored))
        data_replace(data, location, length, 0, 0);
}
