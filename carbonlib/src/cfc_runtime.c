/*+===================================================================
  File:      cfc_runtime.c

  Summary:   Native runtime class registration and instance allocation with the live guest class table and allocator
             ownership.

  Classes:   None.

  Functions: ROMlib_cfc_CFRuntimeCreateInstance, ROMlib_cfc_CFRuntimeGetClassWithTypeID,
             ROMlib_cfc_CFRuntimeRegisterClass, ROMlib_cfc_CFRuntimeSetInstanceTypeID, cfc_allocate_instance, cfc_byte,
             cfc_product, cfc_public

  Origin:    C reconstruction of CoreFoundationLib 1.4 offsets 0x531d0..0x53390 from the user's local CarbonLib
             disassembly. No Apple bytes included.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "cfc_cohort.h"

uint32 cfc_public(const char* name, const uint32* arguments, unsigned count)
{
    uint32 result = 0;
    cfp_call(name, arguments, count, &result);
    return result;
}

int cfc_byte(uint32 address, unsigned value)
{
    Byte* output = ROMlib_carbon_guest_memory(address, 1, 1);
    if (!output)
        return 0;
    *output = value;
    return 1;
}

int cfc_product(uint32 first, uint32 second, uint32* result)
{
    uint64 product = (uint64)first * second;
    if (first > INT32_MAX || second > INT32_MAX || product > INT32_MAX)
        return cfp_failure("Native Carbon collection extent exceeds signed 32-bit storage");
    *result = product;
    return 1;
}

static int class_slot(const ROMlib_carbon_reference* reference, uint32 type, uint32* slot)
{
    /* The object header has only eight type bits. Do not register classes
       whose instances would alias an existing type or overwrite later globals. */
    if (type > 255 || type >= (reference->toc_size - 0x65c4) / 4)
        return cfp_failure("Native Carbon runtime class table extent exceeded");
    *slot = US_TO_SYN68K(reference->toc) + 0x65c4 + type * 4;
    return 1;
}

uint32 cfc_allocate_instance(const ROMlib_carbon_reference* reference, uint32 allocator, uint32 type, uint32 extra)
{
    uint32 slot;
    if (type <= cfp_toc(reference, 0xa6c) || type >= cfp_toc(reference, 0xa68))
        return 0;
    if (!class_slot(reference, type, &slot))
        return 0;
    void* owner = allocator ? SYN68K_TO_US_CHECK0(allocator) : ROMlib_carbon_cf_allocator_default();
    if (!owner)
        return 0;
    allocator = US_TO_SYN68K(owner);
    int system = allocator == cfp_toc(reference, 0x3164);
    uint64 total = (((uint64)extra + 3) & ~UINT64_C(3)) + 8 + (system ? 0 : 4);
    if (total > INT32_MAX)
        return 0;
    void* allocation = ROMlib_carbon_cf_allocator_allocate(owner, total, 0);
    if (!allocation)
        return 0;
    uint32 object = US_TO_SYN68K(allocation);
    if (!system) {
        void* retained = ROMlib_carbon_cf_retain(owner);
        if (!retained) {
            ROMlib_carbon_cf_allocator_deallocate(owner, allocation);
            return 0;
        }
        if (!cfp_write(object, US_TO_SYN68K(retained)))
            return 0;
        object += 4;
    }
    if (!cfp_write(object, 0) || !cfp_write(object + 4, ((type & 255) << 8) | (system ? 0x80 : 0)))
        return 0;
    uint32 klass, initialize;
    if (!cfp_get(slot, &klass) || !cfp_get(klass + 8, &initialize))
        return 0;
    if (initialize && !cfp_callback(initialize, &object, 1, NULL))
        return 0;
    return object;
}

uint32 ROMlib_cfc_CFRuntimeRegisterClass(uint32 klass)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0, slot;
    if (!cfp_reference("_CFRuntimeRegisterClass", 0x531d0, &klass, 1, &reference, &result))
        return result;
    result = cfp_toc(&reference, 0xa68);
    if (!class_slot(&reference, result, &slot))
        return 0;
    if (!cfp_write(US_TO_SYN68K(reference.toc) + 0xa68, result + 1) || !cfp_write(slot, klass))
        return 0;
    return cfp_toc(&reference, 0xa68) - 1;
}

uint32 ROMlib_cfc_CFRuntimeGetClassWithTypeID(uint32 type)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0, slot;
    if (cfp_reference("_CFRuntimeGetClassWithTypeID", 0x531f4, &type, 1, &reference, &result)) {
        if (!class_slot(&reference, type, &slot) || !cfp_get(slot, &result))
            return 0;
    }
    return result;
}

uint32 ROMlib_cfc_CFRuntimeCreateInstance(uint32 allocator, uint32 type, uint32 extra, uint32 category)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, type, extra, category}, result = 0;
    if (cfp_reference("_CFRuntimeCreateInstance", 0x53204, arguments, 4, &reference, &result))
        result = cfc_allocate_instance(&reference, allocator, type, extra);
    return result;
}

void ROMlib_cfc_CFRuntimeSetInstanceTypeID(uint32 object, uint32 type)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {object, type}, ignored = 0, flags;
    if (!cfp_reference("_CFRuntimeSetInstanceTypeID", 0x53360, arguments, 2, &reference, &ignored))
        return;
    if (type <= cfp_toc(&reference, 0xa6c) || type >= cfp_toc(&reference, 0xa68))
        return;
    if (cfp_get(object + 4, &flags))
        cfp_write(object + 4, (flags & ~0xff00U) | ((type & 255) << 8));
}
