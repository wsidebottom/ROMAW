/*+===================================================================
  File:      cf_arrays.c

  Summary:   Array control flow, callback dispatch and factory layout from CoreFoundation 1.4. CFStorage traversal,
             deque rebalancing and sorting share the original runtime.

  Classes:   None.

  Functions: ROMlib_cfarray_append, ROMlib_cfarray_append_array, ROMlib_cfarray_contains, ROMlib_cfarray_count_value,
             ROMlib_cfarray_create_copy, ROMlib_cfarray_create_mutable, ROMlib_cfarray_create_mutable_copy,
             ROMlib_cfarray_exchange, ROMlib_cfarray_first, ROMlib_cfarray_insert, ROMlib_cfarray_last,
             ROMlib_cfarray_remove, ROMlib_cfarray_remove_all, ROMlib_cfarray_replace, ROMlib_cfarray_set,
             ROMlib_cfarray_sort

  Origin:    Existing repository source in packages/carbonlib/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "cf_batch.h"
#include <limits.h>

static uint32 array_header(uint32 flags)
{
    return ((flags & 3) ? 16 : 12) + (((flags >> 2) & 3) == 3 ? 20 : 0);
}

static uint32 array_callbacks(const ROMlib_carbon_reference* reference, uint32 array)
{
    uint32 flags = cfp_read(array + 4), kind = (flags >> 2) & 3;
    if (kind == 0)
        return US_TO_SYN68K(reference->toc) + 0x3248;
    if (kind == 1)
        return US_TO_SYN68K(reference->toc) + 0x3234;
    return array + ((flags & 3) ? 16 : 12);
}

static int array_callbacks_equal(uint32 first, uint32 second)
{
    for (unsigned i = 4; i <= 16; i += 4)
        if (cfp_read(first + i) != cfp_read(second + i))
            return 0;
    return 1;
}

static uint32 array_factory(const ROMlib_carbon_reference* reference, uint32 allocator, uint32 mode, uint32 capacity,
                            uint32 callbacks)
{
    if ((int32)capacity < 0)
        return 0;
    callbacks = ROMlib_carbon_canonical_cf_callbacks(callbacks, reference->toc);
    uint32 null_callbacks = US_TO_SYN68K(reference->toc) + 0x3248;
    uint32 type_callbacks = US_TO_SYN68K(reference->toc) + 0x3234;
    unsigned kind = !callbacks || array_callbacks_equal(callbacks, null_callbacks)                    ? 0
                    : callbacks == type_callbacks || array_callbacks_equal(callbacks, type_callbacks) ? 1
                                                                                                      : 3;
    uint32 flags = (mode & 3) | (kind << 2);
    uint64 extra = array_header(flags) - 8;
    if ((mode & 3) < 2)
        extra += (uint64)capacity * 4;
    if (extra > INT32_MAX)
        return 0;
    uint32 create[] = {allocator, cfp_toc(reference, 0x8c0), (uint32)extra, 0}, array;
    if (!cfp_call("_CFRuntimeCreateInstance", create, 4, &array) || !array)
        return 0;
    cfp_write(array + 4, (cfp_read(array + 4) & ~127u) | flags);
    cfp_write(array + 8, 0);
    if ((mode & 3) == 1)
        cfp_write(array + 12, capacity);
    else if ((mode & 3) >= 2)
        cfp_write(array + 12, 0);
    if (kind == 3 && !cfp_move(array + ((mode & 3) ? 16 : 12), callbacks, 20))
        return 0;
    return array;
}

static int array_range(uint32 array, uint32 location, uint32 length, uint32* count)
{
    if (!array || !cfp_get(array + 8, count))
        return cfp_failure("Native Carbon CFArray requires an object");
    if ((int32)location < 0 || (int32)length < 0 || (int32)*count < 0 || (uint64)location + length > *count)
        return cfp_failure("Native Carbon CFArray range is invalid");
    return 1;
}

static int array_slot(const ROMlib_carbon_reference* reference, uint32 array, uint32 index, uint32* slot)
{
    uint32 flags = cfp_read(array + 4), mode = flags & 3;
    if (mode < 2) {
        *slot = array + array_header(flags) + index * 4;
        return 1;
    }
    uint32 storage = cfp_read(array + 12);
    if (mode == 2) {
        const Byte* first = ROMlib_carbon_guest_memory(storage, 1, 0);
        if (!first)
            return 0;
        *slot = storage + 4 + ((uint32)*first + index) * 4;
        return 1;
    }
    uint32 arguments[] = {storage, index, 0};
    return cfp_private(reference, 0x4ad4c, arguments, 3, slot);
}

static int array_value(const ROMlib_carbon_reference* reference, uint32 array, uint32 index, uint32* value)
{
    uint32 slot;
    return array_slot(reference, array, index, &slot) && cfp_get(slot, value);
}

static int array_builtin_callback(const ROMlib_carbon_reference* reference, uint32 callbacks, uint32 procedure,
                                  uint32 descriptor, uint32 worker)
{
    const uint32 toc = US_TO_SYN68K(reference->toc);
    if (callbacks != toc + 0x3234 || procedure != toc + descriptor)
        return 0;
    const uint32 entry = cfp_read(US_TO_SYN68K(reference->function));
    return cfp_read(procedure) == entry - reference->code_offset + worker && cfp_read(procedure + 4) == toc;
}

static int array_retain_value(const ROMlib_carbon_reference* reference, uint32 callbacks, uint32 allocator,
                              uint32* value)
{
    const uint32 procedure = cfp_read(callbacks + 4);
    if (!procedure)
        return 1;
    if (array_builtin_callback(reference, callbacks, procedure, 0x1294, 0x17a50)) {
        void* object = *value ? ROMlib_carbon_guest_memory(*value, 8, 1) : NULL;
        if (!object)
            return cfp_failure("Invalid CFArray value for the type retain callback");
        void* retained = ROMlib_carbon_cf_retain(object);
        if (!retained)
            return 0;
        *value = US_TO_SYN68K(retained);
        return 1;
    }
    const uint32 arguments[] = {allocator, *value};
    return cfp_callback(procedure, arguments, 2, value);
}

static int array_release_value(const ROMlib_carbon_reference* reference, uint32 callbacks, uint32 allocator,
                               uint32 value)
{
    const uint32 procedure = cfp_read(callbacks + 8);
    if (!procedure)
        return 1;
    if (array_builtin_callback(reference, callbacks, procedure, 0x128c, 0x17a78)) {
        void* object = value ? ROMlib_carbon_guest_memory(value, 8, 1) : NULL;
        if (!object)
            return cfp_failure("Invalid CFArray value for the type release callback");
        ROMlib_carbon_cf_release(object);
        return 1;
    }
    const uint32 arguments[] = {allocator, value};
    return cfp_callback(procedure, arguments, 2, NULL);
}

/* Preserve identity before invoking equality, searched value as the first
   callback argument, and fresh callback/storage reads after reentrant calls. */
static int array_matches(const ROMlib_carbon_reference* reference, uint32 array, uint32 callbacks, uint32 index,
                         uint32 sought, uint32* matches)
{
    uint32 value;
    if (!array_value(reference, array, index, &value))
        return 0;
    *matches = value == sought;
    if (!*matches) {
        uint32 procedure = cfp_read(callbacks + 16);
        if (procedure) {
            if (array_builtin_callback(reference, callbacks, procedure, 0x20ac, 0x5367c))
                *matches = ROMlib_cfvalue_equal(sought, value);
            else {
                uint32 arguments[] = {sought, value};
                if (!cfp_callback(procedure, arguments, 2, matches))
                    return 0;
            }
        }
    }
    return 1;
}

static int array_release_range(const ROMlib_carbon_reference* reference, uint32 array, uint32 location, uint32 length,
                               uint32 clear)
{
    const uint32 mode = cfp_read(array + 4) & 3;
    if (mode == 3) {
        /* CFStorage walks its own leaf ranges and owns their lifetime. */
        const uint32 arguments[] = {array, location, length, clear};
        return cfp_private(reference, 0x1ab04, arguments, 4, NULL);
    }
    const uint32 callbacks = array_callbacks(reference, array);
    const uint32 storage = mode == 2 ? cfp_read(array + 12) : 0;
    const int release_values = length && cfp_read(callbacks + 8) && (mode != 2 || storage);
    uint32 allocator = 0;
    if ((release_values || (clear && storage)) && !cfp_call("CFGetAllocator", &array, 1, &allocator))
        return 0;
    if (release_values) {
        uint32 start;
        if (!array_slot(reference, array, location, &start))
            return 0;
        for (uint32 offset = 0; offset < length; ++offset) {
            uint32 value;
            if (!cfp_get(start + offset * 4, &value) || !array_release_value(reference, callbacks, allocator, value))
                return 0;
        }
    }
    if (mode == 2 && clear && !location && length == cfp_read(array + 8)) {
        const uint32 arguments[] = {allocator, storage};
        if (storage && !cfp_call("CFAllocatorDeallocate", arguments, 2, NULL))
            return 0;
        return cfp_write(array + 12, 0);
    }
    return 1;
}

static int array_storage_resize(const ROMlib_carbon_reference* reference, uint32 storage, uint32 location,
                                uint32 removed, uint32 inserted)
{
    if (inserted == removed)
        return 1;
    uint32 arguments[] = {storage, location + (inserted > removed ? removed : inserted),
                          inserted > removed ? inserted - removed : removed - inserted};
    return cfp_private(reference, inserted > removed ? 0x4ae28 : 0x4af34, arguments, 3, NULL);
}

static int array_replace(const ROMlib_carbon_reference* reference, uint32 array, uint32 location, uint32 removed,
                         uint32 incoming, uint32 inserted)
{
    uint32 count;
    if (!array_range(array, location, removed, &count))
        return 0;
    if ((int32)inserted < 0)
        return cfp_failure("Native Carbon CFArray replacement count is negative");
    uint64 required = (uint64)count - removed + inserted;
    if (required > INT32_MAX / 4)
        return cfp_failure("Native Carbon CFArray replacement length overflow");
    uint32 mode = cfp_read(array + 4) & 3;
    if (!mode)
        return cfp_failure("Native Carbon cannot resize an immutable CFArray");
    if (mode == 1 && required > cfp_read(array + 12))
        return cfp_failure("Native Carbon CFArray replacement exceeds fixed capacity");
    uint32 callbacks = array_callbacks(reference, array), allocator;
    if (!cfp_call("CFGetAllocator", &array, 1, &allocator))
        return 0;
    _Alignas(1024) uint32 local[256];
    uint32 temporary = US_TO_SYN68K(local);
    int allocated = inserted > NELEM(local);
    if (allocated) {
        uint32 arguments[] = {allocator, inserted * 4, 0};
        if (!cfp_call("CFAllocatorAllocate", arguments, 3, &temporary))
            return 0;
        if (!temporary)
            return cfp_failure("Native Carbon CFArray replacement allocation failed");
    }
    if (!cfp_move(temporary, incoming, inserted * 4))
        return 0;
    if (cfp_read(callbacks + 4)) {
        for (uint32 i = 0; i < inserted; ++i) {
            uint32 value;
            if (!cfp_get(temporary + i * 4, &value) || !array_retain_value(reference, callbacks, allocator, &value) ||
                !cfp_write(temporary + i * 4, value))
                return 0;
        }
    }
    /* Retains may change flags. The reference chooses its fixed/variable path
       only after all incoming values have been copied and retained. */
    mode = cfp_read(array + 4) & 3;
    if (mode == 1) {
        uint32 base = array + array_header(cfp_read(array + 4));
        if (!array_release_range(reference, array, location, removed, 0))
            return 0;
        uint32 tail = count - location - removed;
        if (inserted != removed && tail &&
            !cfp_move(base + (location + inserted) * 4, base + (location + removed) * 4, tail * 4))
            return 0;
        if (!cfp_move(base + location * 4, temporary, inserted * 4))
            return 0;
    }
    else {
        if (!array_release_range(reference, array, location, removed, 0))
            return 0;
        mode = cfp_read(array + 4) & 3;
        uint32 storage = cfp_read(array + 12);
        if (mode == 3) {
            if (!array_storage_resize(reference, storage, location, removed, inserted))
                return 0;
            if (required <= 127 && !cfp_private(reference, 0x1d0f0, &array, 1, NULL))
                return 0;
        }
        else if (!storage) {
            if (required >= 254) {
                uint32 arguments[] = {allocator, 4};
                if (!cfp_private(reference, 0x4acb0, arguments, 2, &storage) || !storage)
                    return cfp_failure("Native Carbon CFArray storage allocation failed");
                cfp_write(array + 12, storage);
                uint32 insert[] = {storage, 0, inserted};
                if (!cfp_private(reference, 0x4ae28, insert, 3, NULL))
                    return 0;
            }
            else {
                uint32 capacity = 4;
                if (required >= 4) {
                    while (capacity <= required)
                        capacity *= 2;
                    if (capacity >= 254)
                        capacity = 254;
                }
                uint32 allocate[] = {allocator, capacity * 4 + 4, 0};
                if (!cfp_call("CFAllocatorAllocate", allocate, 3, &storage) || !storage)
                    return cfp_failure("Native Carbon CFArray deque allocation failed");
                Byte* header = ROMlib_carbon_guest_memory(storage, 2, 1);
                if (!header)
                    return 0;
                header[0] = (Byte)((capacity - inserted) / 2);
                header[1] = (Byte)capacity;
                cfp_write(array + 12, storage);
            }
        }
        else if (required >= 254) {
            if (!cfp_private(reference, 0x1d004, &array, 1, NULL) ||
                !array_storage_resize(reference, cfp_read(array + 12), location, removed, inserted))
                return 0;
        }
        else if (inserted != removed) {
            uint32 arguments[] = {array, location, removed, inserted};
            if (!cfp_private(reference, 0x1d224, arguments, 4, NULL))
                return 0;
        }
        if (inserted) {
            if ((cfp_read(array + 4) & 3) == 3) {
                uint32 arguments[] = {cfp_read(array + 12), location, inserted, temporary};
                if (!cfp_private(reference, 0x4b1d8, arguments, 4, NULL))
                    return 0;
            }
            else {
                uint32 slot;
                if (!array_slot(reference, array, location, &slot) || !cfp_move(slot, temporary, inserted * 4))
                    return 0;
            }
        }
    }
    cfp_write(array + 8, (uint32)required);
    if (allocated) {
        uint32 arguments[] = {allocator, temporary};
        if (!cfp_call("CFAllocatorDeallocate", arguments, 2, NULL))
            return 0;
    }
    return 1;
}

uint32 ROMlib_cfarray_create_mutable(uint32 allocator, uint32 capacity, uint32 callbacks)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, capacity, callbacks}, result = 0;
    if (cfp_reference("CFArrayCreateMutable", 0x1ba48, arguments, 3, &reference, &result))
        result = array_factory(&reference, allocator, capacity ? 1 : 2, capacity, callbacks);
    return result;
}

uint32 ROMlib_cfarray_create_copy(uint32 allocator, uint32 source)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, source}, result = 0;
    if (!cfp_reference("CFArrayCreateCopy", 0x1ba84, arguments, 2, &reference, &result))
        return result;
    uint32 count = cfp_read(source + 8), callbacks = array_callbacks(&reference, source);
    result = array_factory(&reference, allocator, 0, count, callbacks);
    if (!result)
        return 0;
    uint32 destination = result + array_header(cfp_read(result + 4));
    for (uint32 i = 0; i < count; ++i) {
        uint32 value;
        if (!array_value(&reference, source, i, &value))
            return 0;
        if (!array_retain_value(&reference, callbacks, allocator, &value))
            return 0;
        if (!cfp_write(destination + i * 4, value))
            return 0;
    }
    cfp_write(result + 8, count);
    return result;
}

uint32 ROMlib_cfarray_create_mutable_copy(uint32 allocator, uint32 capacity, uint32 source)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, capacity, source}, result = 0;
    if (!cfp_reference("CFArrayCreateMutableCopy", 0x1bc38, arguments, 3, &reference, &result))
        return result;
    uint32 count = cfp_read(source + 8);
    if ((int32)count < 0 || (capacity && count > capacity))
        return 0;
    result = array_factory(&reference, allocator, capacity ? 1 : 2, capacity, array_callbacks(&reference, source));
    if (!result)
        return 0;
    for (uint32 i = 0; i < count; ++i) {
        uint32 value;
        if (!array_value(&reference, source, i, &value))
            return 0;
        _Alignas(16) uint32 retained = CL(value);
        if (!array_replace(&reference, result, cfp_read(result + 8), 0, US_TO_SYN68K(&retained), 1))
            return 0;
    }
    return result;
}

static uint32 array_search(const ROMlib_carbon_reference* reference, uint32 array, uint32 location, uint32 length,
                           uint32 value, unsigned kind)
{
    uint32 count;
    if (!array_range(array, location, length, &count))
        return 0;
    uint32 callbacks = array_callbacks(reference, array), matches = 0, total = 0;
    for (uint32 i = 0; i < length; ++i) {
        uint32 index = location + (kind == 3 ? length - i - 1 : i);
        if (!array_matches(reference, array, callbacks, index, value, &matches))
            return 0;
        if (matches) {
            if (kind == 1)
                return 1;
            if (kind >= 2)
                return index;
            ++total;
        }
    }
    return kind >= 2 ? UINT32_MAX : total;
}

uint32 ROMlib_cfarray_count_value(uint32 array, uint32 location, uint32 length, uint32 value)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {array, location, length, value}, result = 0;
    if (cfp_reference("CFArrayGetCountOfValue", 0x1bd74, arguments, 4, &reference, &result))
        result = array_search(&reference, array, location, length, value, 0);
    return result;
}

uint32 ROMlib_cfarray_contains(uint32 array, uint32 location, uint32 length, uint32 value)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {array, location, length, value}, result = 0;
    if (cfp_reference("CFArrayContainsValue", 0x1bf54, arguments, 4, &reference, &result))
        result = array_search(&reference, array, location, length, value, 1);
    return result;
}

uint32 ROMlib_cfarray_first(uint32 array, uint32 location, uint32 length, uint32 value)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {array, location, length, value}, result = 0;
    if (cfp_reference("CFArrayGetFirstIndexOfValue", 0x1c468, arguments, 4, &reference, &result))
        result = array_search(&reference, array, location, length, value, 2);
    return result;
}

uint32 ROMlib_cfarray_last(uint32 array, uint32 location, uint32 length, uint32 value)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {array, location, length, value}, result = 0;
    if (cfp_reference("CFArrayGetLastIndexOfValue", 0x1c64c, arguments, 4, &reference, &result))
        result = array_search(&reference, array, location, length, value, 3);
    return result;
}

void ROMlib_cfarray_replace(uint32 array, uint32 location, uint32 length, uint32 values, uint32 count)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {array, location, length, values, count}, ignored = 0;
    if (cfp_reference("CFArrayReplaceValues", 0x1d4a0, arguments, 5, &reference, &ignored))
        array_replace(&reference, array, location, length, values, count);
}

static void array_replace_single(const char* name, uint32 offset, uint32 array, uint32 index, uint32 value,
                                 uint32 append)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {array, append ? value : index, value}, ignored = 0;
    if (!cfp_reference(name, offset, arguments, append ? 2 : 3, &reference, &ignored))
        return;
    if (append)
        index = cfp_read(array + 8);
    _Alignas(16) uint32 word = CL(value);
    array_replace(&reference, array, index, 0, US_TO_SYN68K(&word), 1);
}

void ROMlib_cfarray_append(uint32 array, uint32 value)
{
    array_replace_single("CFArrayAppendValue", 0x1c830, array, 0, value, 1);
}

void ROMlib_cfarray_insert(uint32 array, uint32 index, uint32 value)
{
    array_replace_single("CFArrayInsertValueAtIndex", 0x1cb74, array, index, value, 0);
}

void ROMlib_cfarray_set(uint32 array, uint32 index, uint32 value)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {array, index, value}, ignored = 0, count;
    if (!cfp_reference("CFArraySetValueAtIndex", 0x1c888, arguments, 3, &reference, &ignored))
        return;
    if (!array_range(array, index, 0, &count))
        return;
    if (index == count) {
        _Alignas(16) uint32 word = CL(value);
        array_replace(&reference, array, index, 0, US_TO_SYN68K(&word), 1);
        return;
    }
    uint32 callbacks = array_callbacks(&reference, array), allocator;
    if (!cfp_call("CFGetAllocator", &array, 1, &allocator))
        return;
    if (!array_retain_value(&reference, callbacks, allocator, &value))
        return;
    uint32 slot, old;
    if (!array_slot(&reference, array, index, &slot) || !cfp_get(slot, &old) || !cfp_write(slot, value))
        return;
    array_release_value(&reference, callbacks, allocator, old);
}

void ROMlib_cfarray_exchange(uint32 array, uint32 first, uint32 second)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {array, first, second}, ignored = 0, count;
    if (!cfp_reference("CFArrayExchangeValuesAtIndices", 0x1cbc8, arguments, 3, &reference, &ignored) ||
        !array_range(array, first, 1, &count) || !array_range(array, second, 1, &count))
        return;
    uint32 value, first_slot, second_slot, other;
    if (!array_value(&reference, array, first, &value) || !array_slot(&reference, array, first, &first_slot) ||
        !array_value(&reference, array, second, &other) || !cfp_write(first_slot, other) ||
        !array_slot(&reference, array, second, &second_slot))
        return;
    cfp_write(second_slot, value);
}

void ROMlib_cfarray_remove(uint32 array, uint32 index)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {array, index}, ignored = 0;
    if (cfp_reference("CFArrayRemoveValueAtIndex", 0x1cf50, arguments, 2, &reference, &ignored))
        array_replace(&reference, array, index, 1, 0, 0);
}

void ROMlib_cfarray_remove_all(uint32 array)
{
    ROMlib_carbon_reference reference;
    uint32 ignored = 0;
    if (cfp_reference("CFArrayRemoveAllValues", 0x1cfa0, &array, 1, &reference, &ignored) &&
        array_release_range(&reference, array, 0, cfp_read(array + 8), 1))
        cfp_write(array + 8, 0);
}

void ROMlib_cfarray_append_array(uint32 array, uint32 source, uint32 location, uint32 length)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {array, source, location, length}, ignored = 0, count;
    if (!cfp_reference("CFArrayAppendArray", 0x1de94, arguments, 4, &reference, &ignored) ||
        !array_range(source, location, length, &count))
        return;
    for (uint32 i = 0; i < length; ++i) {
        uint32 value;
        if (!array_value(&reference, source, location + i, &value))
            return;
        _Alignas(16) uint32 word = CL(value);
        if (!array_replace(&reference, array, cfp_read(array + 8), 0, US_TO_SYN68K(&word), 1))
            return;
    }
}

void ROMlib_cfarray_sort(uint32 array, uint32 location, uint32 length, uint32 comparator, uint32 context)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {array, location, length, comparator, context}, ignored = 0, count;
    if (!cfp_reference("CFArraySortValues", 0x1da74, arguments, 5, &reference, &ignored) ||
        !array_range(array, location, length, &count) || !length)
        return;
    uint32 mode = cfp_read(array + 4) & 3;
    if (!mode)
        return;
    if (length > INT32_MAX / 4) {
        cfp_failure("Native Carbon CFArray sort length overflow");
        return;
    }
    _Alignas(16) uint32 callback_context[2] = {CL(comparator), CL(context)};
    _Alignas(1024) uint32 local[256];
    uint32 values, allocator = 0, storage = 0;
    int allocated = mode == 3 && length > NELEM(local);
    if (mode == 3) {
        storage = cfp_read(array + 12);
        if (!cfp_call("CFGetAllocator", &array, 1, &allocator))
            return;
        values = US_TO_SYN68K(local);
        if (allocated) {
            uint32 allocate[] = {allocator, length * 4, 0};
            if (!cfp_call("CFAllocatorAllocate", allocate, 3, &values) || !values) {
                cfp_failure("Native Carbon CFArray sort allocation failed");
                return;
            }
        }
        uint32 copy[] = {storage, location, length, values};
        if (!cfp_private(&reference, 0x4b048, copy, 4, NULL))
            return;
    }
    else if (!array_slot(&reference, array, location, &values))
        return;
    uint32 sort[] = {values, length, 4, cfp_toc(&reference, 0x48c), US_TO_SYN68K(callback_context)};
    if (!cfp_private(&reference, 0x4e960, sort, 5, NULL))
        return;
    if (mode == 3) {
        uint32 copy[] = {storage, location, length, values};
        if (!cfp_private(&reference, 0x4b1d8, copy, 4, NULL))
            return;
    }
    if (allocated) {
        uint32 release[] = {allocator, values};
        cfp_call("CFAllocatorDeallocate", release, 2, NULL);
    }
}
