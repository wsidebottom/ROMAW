/*+===================================================================
  File:      cf_values.c

  Summary:   Primitive value/class dispatch from the initialized CoreFoundation 1.4 image. The class table and singleton
             pointers remain shared with real objects.

  Classes:   None.

  Functions: ROMlib_cfvalue_boolean, ROMlib_cfvalue_boolean_type, ROMlib_cfvalue_charset_type, ROMlib_cfvalue_date_type,
             ROMlib_cfvalue_description, ROMlib_cfvalue_equal, ROMlib_cfvalue_hash, ROMlib_cfvalue_number_float,
             ROMlib_cfvalue_number_size, ROMlib_cfvalue_number_type, ROMlib_cfvalue_number_type_id,
             ROMlib_cfvalue_set_type, ROMlib_cfvalue_string_type

  Origin:    Existing repository source in packages/carbonlib/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Primitive value/class dispatch from the initialized CoreFoundation 1.4 image.
   The class table and singleton pointers remain shared with real objects. */
#include "cf_batch.h"

uint32 ROMlib_cfvalue_boolean_type(void)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (cfp_reference("CFBooleanGetTypeID", 0x24610, NULL, 0, &reference, &result))
        result = cfp_toc(&reference, 0x924);
    return result;
}

uint32 ROMlib_cfvalue_boolean(uint32 object)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (cfp_reference("CFBooleanGetValue", 0x24618, &object, 1, &reference, &result))
        result = object == cfp_toc(&reference, 0x356c);
    return result;
}

uint32 ROMlib_cfvalue_date_type(void)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (cfp_reference("CFDateGetTypeID", 0x2341c, NULL, 0, &reference, &result))
        result = cfp_toc(&reference, 0x8d8);
    return result;
}

uint32 ROMlib_cfvalue_string_type(void)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (cfp_reference("CFStringGetTypeID", 0x36e98, NULL, 0, &reference, &result))
        result = cfp_toc(&reference, 0x990);
    return result;
}

uint32 ROMlib_cfvalue_charset_type(void)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (cfp_reference("CFCharacterSetGetTypeID", 0x31b9c, NULL, 0, &reference, &result))
        result = cfp_toc(&reference, 0x96c);
    return result;
}

uint32 ROMlib_cfvalue_set_type(void)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (cfp_reference("CFSetGetTypeID", 0x1ebe8, NULL, 0, &reference, &result))
        result = cfp_toc(&reference, 0x8d0);
    return result;
}

uint32 ROMlib_cfvalue_number_type_id(void)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (cfp_reference("CFNumberGetTypeID", 0x24d50, NULL, 0, &reference, &result))
        result = cfp_toc(&reference, 0x920);
    return result;
}

uint32 ROMlib_cfvalue_number_type(uint32 object)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (cfp_reference("CFNumberGetType", 0x24ed0, &object, 1, &reference, &result))
        result = cfp_read(object + 4) & 31;
    return result;
}

uint32 ROMlib_cfvalue_number_size(uint32 object)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (cfp_reference("CFNumberGetByteSize", 0x24edc, &object, 1, &reference, &result)) {
        static const unsigned sizes[] = {0, 1, 2, 4, 8, 4, 8};
        unsigned type = cfp_read(object + 4) & 31;
        result = type < NELEM(sizes) ? sizes[type] : 0;
    }
    return result;
}

uint32 ROMlib_cfvalue_number_float(uint32 object)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (cfp_reference("CFNumberIsFloatType", 0x24f64, &object, 1, &reference, &result)) {
        unsigned type = cfp_read(object + 4) & 31;
        result = type == 5 || type == 6 || type == 12 || type == 13;
    }
    return result;
}

static uint32 value_class(const ROMlib_carbon_reference* reference, uint32 object)
{
    unsigned type = (cfp_read(object + 4) >> 8) & 255;
    uint32 descriptor = cfp_toc(reference, 0x65c4 + type * 4);
    if (!descriptor)
        cfp_failure("Native Carbon object class is not registered");
    return descriptor;
}

uint32 ROMlib_cfvalue_equal(uint32 first, uint32 second)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {first, second}, result = 0;
    if (!cfp_reference("CFEqual", 0x5367c, arguments, 2, &reference, &result))
        return result;
    if (first == second)
        return 1;
    if (((cfp_read(first + 4) ^ cfp_read(second + 4)) & 0xff00) != 0)
        return 0;
    uint32 descriptor = value_class(&reference, first);
    if (!descriptor)
        return 0;
    uint32 procedure = cfp_read(descriptor + 20);
    if (procedure && !cfp_callback(procedure, arguments, 2, &result))
        return 0;
    return result;
}

uint32 ROMlib_cfvalue_hash(uint32 object)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (!cfp_reference("CFHash", 0x536f0, &object, 1, &reference, &result))
        return result;
    uint32 descriptor = value_class(&reference, object);
    if (!descriptor)
        return 0;
    uint32 procedure = cfp_read(descriptor + 24);
    if (!procedure)
        return object;
    return cfp_callback(procedure, &object, 1, &result) ? result : 0;
}

uint32 ROMlib_cfvalue_description(uint32 object)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (!cfp_reference("CFCopyDescription", 0x53730, &object, 1, &reference, &result))
        return result;
    uint32 descriptor = value_class(&reference, object);
    if (!descriptor)
        return 0;
    uint32 procedure = cfp_read(descriptor + 32);
    if (procedure) {
        if (!cfp_callback(procedure, &object, 1, &result))
            return 0;
        if (result)
            return result;
    }
    unsigned type = (cfp_read(object + 4) >> 8) & 255;
    uint32 owner;
    if (!cfp_call("CFGetAllocator", &object, 1, &owner))
        return 0;
    uint32 literal = cfp_toc(&reference, 0x7a0) + 0x12, format;
    if (!cfp_call("__CFStringMakeConstantString", &literal, 1, &format))
        return 0;
    descriptor = cfp_toc(&reference, 0x65c4 + type * 4);
    if (!descriptor) {
        cfp_failure("Native Carbon description class is not registered");
        return 0;
    }
    uint32 create[] = {cfp_toc(&reference, 0x3160), 0, format, cfp_read(descriptor + 4), object, owner};
    return cfp_call("CFStringCreateWithFormat", create, 6, &result) ? result : 0;
}
