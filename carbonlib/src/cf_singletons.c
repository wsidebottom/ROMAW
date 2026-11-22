/*+===================================================================
  File:      cf_singletons.c

  Summary:   Initializes shared CoreFoundation allocator, boolean and distinguished number objects.

  Classes:   None.

  Functions: ROMlib_carbon_cf_allocator_constants, ROMlib_carbon_cf_boolean_constants,
             ROMlib_carbon_cf_number_constants

  Origin:    CoreFoundationLib 1.4 runtime initializers at 0x17280, 0x24598 and 0x24c1c.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "cfc_cohort.h"
#include "MemoryMgr.h"

static uint32 register_class(const ROMlib_carbon_reference* reference, uint32 descriptor, uint32 type_slot)
{
    uint32 toc = US_TO_SYN68K(reference->toc);
    uint32 type = ROMlib_cfc_CFRuntimeRegisterClass(toc + descriptor);
    return type && cfp_write(toc + type_slot, type) ? type : 0;
}

int ROMlib_carbon_cf_allocator_constants(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference reference;
    if (!ROMlib_carbon_reference_function("CoreFoundationLib", "CFAllocatorGetDefault", &reference))
        return 0;
    uint32 toc = US_TO_SYN68K(reference.toc);
    if (!cfp_toc(&reference, 0x8a0) && !cfp_write(toc + 0x8a0, US_TO_SYN68K(MR(ApplZone))))
        return 0;
    uint32 type = register_class(&reference, 0x3174, 0x89c);
    if (!type)
        return 0;
    const uint32 system = toc + 0x30d0;
    for (uint32 object = system; object <= toc + 0x3130; object += 0x30) {
        ROMlib_cfc_CFRuntimeSetInstanceTypeID(object, type);
        if (!cfp_write(object, 0) || !cfp_write(object + 8, system))
            return 0;
    }
    return 1;
}

int ROMlib_carbon_cf_boolean_constants(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference reference;
    if (!ROMlib_carbon_reference_function("CoreFoundationLib", "CFBooleanGetTypeID", &reference))
        return 0;
    uint32 toc = US_TO_SYN68K(reference.toc);
    uint32 type = register_class(&reference, 0x3574, 0x924);
    if (!type)
        return 0;
    const uint32 allocator = cfp_toc(&reference, 0x3164);
    for (uint32 slot = 0x356c; slot <= 0x3570; slot += 4) {
        uint32 object = cfc_allocate_instance(&reference, allocator, type, 0);
        if (!object || !cfp_write(toc + slot, object))
            return 0;
    }
    return 1;
}

int ROMlib_carbon_cf_number_constants(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference reference;
    if (!ROMlib_carbon_reference_function("CoreFoundationLib", "CFNumberGetTypeID", &reference))
        return 0;
    uint32 toc = US_TO_SYN68K(reference.toc);
    uint32 type = register_class(&reference, 0x35c4, 0x920);
    if (!type)
        return 0;
    const uint32 allocator = cfp_toc(&reference, 0x3164);

    static const struct {
        uint32 high, flags;
    } numbers[] = {{0x7ff80000, 0x66}, {0xfff00000, 0x46}, {0x7ff00000, 0x26}};

    for (unsigned index = 0; index < NELEM(numbers); ++index) {
        uint32 object = cfc_allocate_instance(&reference, allocator, type, 8);
        if (!object || !cfp_write(object + 4, (cfp_read(object + 4) & ~0x7fU) | numbers[index].flags) ||
            !cfp_write(object + 8, numbers[index].high) || !cfp_write(object + 12, 0) ||
            !cfp_write(toc + 0x3598 + 4 * index, object))
            return 0;
    }
    return 1;
}
