/*+===================================================================
  File:      cfc_storage.c

  Summary:   Native CFStorage cache, range copying and iteration; original balanced-node workers remain dependencies.

  Classes:   None.

  Functions: ROMlib_cfc_CFStorageApplyFunction, ROMlib_cfc_CFStorageCreate, ROMlib_cfc_CFStorageDeleteValues,
             ROMlib_cfc_CFStorageGetCount, ROMlib_cfc_CFStorageGetTypeID, ROMlib_cfc_CFStorageGetValueAtIndex,
             ROMlib_cfc_CFStorageGetValues, ROMlib_cfc_CFStorageInsertValues, ROMlib_cfc_CFStorageReplaceValues

  Origin:    C reconstruction of the local CoreFoundationLib 1.4 disassembly at offsets 0x4acb0..0x4b298; private ABI
             taken from register use.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "cfc_cohort.h"

static int storage_size(uint32 object, uint32* size)
{
    if (!cfp_get(object + 8, size))
        return 0;
    if (!*size || *size > INT32_MAX)
        return cfp_failure("Native Carbon CFStorage requires a positive value size");
    return 1;
}

static int storage_invalidate(uint32 object)
{
    return cfp_write(object + 20, 0) && cfp_write(object + 12, 0) && cfp_write(object + 16, 0);
}

static uint32 storage_index(const ROMlib_carbon_reference* reference, uint32 object, uint32 index, uint32 output,
                            uint32* native_range)
{
    uint32 start, count, size, bytes, result;
    if (!storage_size(object, &size) || !cfp_get(object + 12, &start) || !cfp_get(object + 16, &count))
        return 0;
    if ((int32)index >= (int32)start && (int64)(int32)index < (int64)(int32)start + (int32)count) {
        if (!cfc_product(index - start, size, &bytes) || !cfp_get(object + 20, &result))
            return 0;
        result += bytes;
    }
    else {
        if (!cfc_product(index, size, &bytes))
            return 0;
        _Alignas(16) uint32 range[2] = {0, 0};
        uint32 arguments[] = {object, object + 24, bytes, US_TO_SYN68K(range)};
        if (!cfp_private(reference, 0x49ca0, arguments, 4, &result))
            return 0;
        start = CL(range[0]) / size;
        count = CL(range[1]) / size;
        uint32 displacement;
        if (index < start || !cfc_product(index - start, size, &displacement) || !cfp_write(object + 12, start) ||
            !cfp_write(object + 16, count) || !cfp_write(object + 20, result - displacement))
            return 0;
    }
    if (output && (!cfp_write(output, start) || !cfp_write(output + 4, count)))
        return 0;
    if (native_range) {
        native_range[0] = start;
        native_range[1] = count;
    }
    return result;
}

uint32 ROMlib_cfc_CFStorageCreate(uint32 allocator, uint32 size)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, size}, result = 0;
    if (!cfp_reference("CFStorageCreate", 0x4acb0, arguments, 2, &reference, &result))
        return result;
    result = cfc_allocate_instance(&reference, allocator, cfp_toc(&reference, 0x9ec), 36);
    if (!result)
        return 0;
    if (!cfp_write(result + 8, size) || !storage_invalidate(result) || !cfp_clear(result + 24, 20) ||
        !cfc_byte(result + 28, 1))
        return 0;
    return result;
}

uint32 ROMlib_cfc_CFStorageGetTypeID(void)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (cfp_reference("CFStorageGetTypeID", 0x4ad34, NULL, 0, &reference, &result))
        result = cfp_toc(&reference, 0x9ec);
    return result;
}

uint32 ROMlib_cfc_CFStorageGetCount(uint32 object)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0, size;
    if (cfp_reference("CFStorageGetCount", 0x4ad3c, &object, 1, &reference, &result)) {
        if (!storage_size(object, &size))
            return 0;
        result = (int32)cfp_read(object + 24) / (int32)size;
    }
    return result;
}

uint32 ROMlib_cfc_CFStorageGetValueAtIndex(uint32 object, uint32 index, uint32 range)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, index, range}, result = 0;
    if (cfp_reference("CFStorageGetValueAtIndex", 0x4ad4c, arguments, 3, &reference, &result))
        result = storage_index(&reference, object, index, range, NULL);
    return result;
}

void ROMlib_cfc_CFStorageInsertValues(uint32 object, uint32 location, uint32 length)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, location, length}, ignored = 0, size, position, remaining;
    if (!cfp_reference("CFStorageInsertValues", 0x4ae28, arguments, 3, &reference, &ignored) ||
        !storage_size(object, &size) || !cfc_product(location, size, &position) ||
        !cfc_product(length, size, &remaining))
        return;
    while (remaining) {
        uint32 chunk = remaining;
        if (chunk > 65536)
            chunk = (65536 / size) * size;
        if (!chunk) {
            cfp_failure("Native Carbon CFStorage insertion value exceeds node extent");
            return;
        }
        uint32 insert[] = {object, object + 24, position, chunk}, node = 0;
        if (!cfp_private(&reference, 0x4a484, insert, 4, &node))
            return;
        if (node) {
            uint32 allocate[] = {object, 0, 0}, root = 0;
            if (!cfp_private(&reference, 0x49c24, allocate, 3, &root) || !root || !cfp_move(root, object + 24, 20) ||
                !cfc_byte(object + 28, 0) || !cfp_write(object + 32, root) || !cfp_write(object + 36, node) ||
                !cfp_write(object + 40, 0) || !cfp_write(object + 24, cfp_read(root) + cfp_read(node)))
                return;
        }
        remaining -= chunk;
        position += chunk;
    }
    storage_invalidate(object);
}

void ROMlib_cfc_CFStorageDeleteValues(uint32 object, uint32 location, uint32 length)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, location, length}, ignored = 0, size, position, bytes;
    if (!cfp_reference("CFStorageDeleteValues", 0x4af34, arguments, 3, &reference, &ignored) ||
        !storage_size(object, &size) || !cfc_product(location, size, &position) || !cfc_product(length, size, &bytes))
        return;
    uint32 remove[] = {object, object + 24, position, bytes, 1};
    if (!cfp_private(&reference, 0x49e20, remove, 5, NULL))
        return;
    uint32 root = object + 24;
    for (;;) {
        uint32 children = 0;
        if (!cfp_private(&reference, 0x49dc0, &root, 1, &children))
            return;
        if (children != 1)
            break;
        uint32 node;
        if (!cfp_get(object + 32, &node) || !cfp_move(root, node, 20))
            return;
        uint32 owner = cfc_public("CFGetAllocator", &object, 1), cleanup[] = {owner, node};
        if (!cfp_call("CFAllocatorDeallocate", cleanup, 2, NULL))
            return;
    }
    uint32 children = 0;
    if (!cfp_private(&reference, 0x49dc0, &root, 1, &children))
        return;
    const Byte* leaf = ROMlib_carbon_guest_memory(object + 28, 1, 0);
    if (!leaf)
        return;
    if (!children && !*leaf && (!cfc_byte(object + 28, 1) || !cfp_write(object + 32, 0) || !cfp_write(object + 36, 0)))
        return;
    storage_invalidate(object);
}

static void storage_transfer(const ROMlib_carbon_reference* reference, uint32 object, uint32 location, uint32 length,
                             uint32 buffer, int replace)
{
    uint32 size;
    if (!storage_size(object, &size))
        return;
    while ((int32)length > 0) {
        _Alignas(16) uint32 range[2] = {0, 0};
        uint32 pointer = storage_index(reference, object, location, 0, range);
        uint32 start = range[0], count = range[1];
        if (!pointer || location < start || location - start >= count) {
            cfp_failure("Native Carbon CFStorage transfer made no progress");
            return;
        }
        uint32 chunk = MIN(length, count - (location - start)), bytes;
        if (!cfc_product(chunk, size, &bytes) ||
            !cfp_move(replace ? pointer : buffer, replace ? buffer : pointer, bytes))
            return;
        buffer += bytes;
        location += chunk;
        length -= chunk;
    }
}

void ROMlib_cfc_CFStorageGetValues(uint32 object, uint32 location, uint32 length, uint32 output)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, location, length, output}, ignored = 0;
    if (cfp_reference("CFStorageGetValues", 0x4b048, arguments, 4, &reference, &ignored))
        storage_transfer(&reference, object, location, length, output, 0);
}

void ROMlib_cfc_CFStorageReplaceValues(uint32 object, uint32 location, uint32 length, uint32 input)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, location, length, input}, ignored = 0;
    if (cfp_reference("CFStorageReplaceValues", 0x4b1d8, arguments, 4, &reference, &ignored))
        storage_transfer(&reference, object, location, length, input, 1);
}

void ROMlib_cfc_CFStorageApplyFunction(uint32 object, uint32 location, uint32 length, uint32 procedure, uint32 context)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, location, length, procedure, context}, ignored = 0, size;
    if (!cfp_reference("CFStorageApplyFunction", 0x4b110, arguments, 5, &reference, &ignored) ||
        !storage_size(object, &size))
        return;
    while ((int32)length > 0) {
        _Alignas(16) uint32 range[2] = {0, 0};
        uint32 pointer = storage_index(&reference, object, location, 0, range);
        uint32 start = range[0], count = range[1];
        if (!pointer || location < start || location - start >= count) {
            cfp_failure("Native Carbon CFStorage iteration made no progress");
            return;
        }
        uint32 chunk = MIN(length, count - (location - start));
        for (uint32 i = 0; i < chunk; ++i) {
            uint32 callback[] = {pointer, context};
            if (!cfp_callback(procedure, callback, 2, NULL))
                return;
            pointer += size;
        }
        location += chunk;
        length -= chunk;
    }
}
