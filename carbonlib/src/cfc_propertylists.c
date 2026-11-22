/*+===================================================================
  File:      cfc_propertylists.c

  Summary:   Native property-list entry control flow and dictionary/scalar deep copies; original XML and array-copy
             workers remain.

  Classes:   None.

  Functions: ROMlib_cfc_CFPropertyListCreateDeepCopy, ROMlib_cfc_CFPropertyListCreateFromXMLData,
             ROMlib_cfc_CFPropertyListCreateXMLData

  Origin:    C reconstruction of CoreFoundationLib 1.4 offsets 0x26bc4, 0x2a2ec and 0x2b2cc..0x2b71c, with Universal
             Interfaces declarations.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "cfc_cohort.h"

static uint32 plist_copy(const ROMlib_carbon_reference* reference, uint32 allocator, uint32 object, uint32 options,
                         unsigned depth);

static uint32 plist_dictionary(const ROMlib_carbon_reference* reference, uint32 allocator, uint32 object,
                               uint32 options, unsigned depth)
{
    uint32 count = cfc_public("CFDictionaryGetCount", &object, 1);
    uint32 callbacks = US_TO_SYN68K(reference->toc) + 0x33c0, value_callbacks = US_TO_SYN68K(reference->toc) + 0x33f0;
    uint32 create_mutable[] = {allocator, 0, callbacks, value_callbacks};
    if (!count) {
        uint32 create[] = {allocator, 0, 0, 0, callbacks, value_callbacks};
        return options ? cfc_public("CFDictionaryCreateMutable", create_mutable, 4)
                       : cfc_public("CFDictionaryCreate", create, 6);
    }
    uint32 bytes;
    if (!cfc_product(count, 8, &bytes))
        return 0;
    uint32 allocate[] = {allocator, bytes, 0}, buffer = cfc_public("CFAllocatorAllocate", allocate, 3);
    if (!buffer)
        return 0;
    uint32 values = buffer + count * 4, collect[] = {object, buffer, values};
    if (!cfp_call("CFDictionaryGetKeysAndValues", collect, 3, NULL))
        return 0;
    uint32 completed = 0, result = 0;
    for (; completed < count; ++completed) {
        uint32 copy_key[] = {allocator, cfp_read(buffer + completed * 4)};
        uint32 key = cfc_public("CFStringCreateCopy", copy_key, 2);
        if (!cfp_write(buffer + completed * 4, key) || !key)
            break;
        uint32 child = plist_copy(reference, allocator, cfp_read(values + completed * 4), options, depth + 1);
        if (!cfp_write(values + completed * 4, child))
            return 0;
        if (!child) {
            cfp_call("CFRelease", &key, 1, NULL);
            break;
        }
    }
    if (completed == count) {
        uint32 create[] = {allocator, buffer, values, count, callbacks, value_callbacks};
        result = options ? cfc_public("CFDictionaryCreateMutable", create_mutable, 4)
                         : cfc_public("CFDictionaryCreate", create, 6);
    }
    for (uint32 i = 0; i < completed; ++i) {
        uint32 key = cfp_read(buffer + i * 4), value = cfp_read(values + i * 4);
        if (result && options) {
            uint32 set[] = {result, key, value};
            if (!cfp_call("CFDictionarySetValue", set, 3, NULL))
                return 0;
        }
        if (!cfp_call("CFRelease", &key, 1, NULL) || !cfp_call("CFRelease", &value, 1, NULL))
            return 0;
    }
    uint32 cleanup[] = {allocator, buffer};
    if (!cfp_call("CFAllocatorDeallocate", cleanup, 2, NULL))
        return 0;
    return result;
}

static uint32 plist_copy(const ROMlib_carbon_reference* reference, uint32 allocator, uint32 object, uint32 options,
                         unsigned depth)
{
    if (depth > 512) {
        cfp_failure("Native Carbon property-list recursion exceeds supported depth");
        return 0;
    }
    if (!allocator)
        allocator = cfc_public("CFAllocatorGetDefault", NULL, 0);
    if (!allocator || !cfp_call("CFRetain", &allocator, 1, &allocator))
        return 0;
    uint32 type = cfc_public("CFGetTypeID", &object, 1), result = 0;
    if (type == cfc_public("CFDictionaryGetTypeID", NULL, 0))
        result = plist_dictionary(reference, allocator, object, options, depth);
    else if (type == cfc_public("CFArrayGetTypeID", NULL, 0)) {
        uint32 copy[] = {allocator, object, options};
        cfp_private(reference, options ? 0x2b1f8 : 0x2b08c, copy, 3, &result);
    }
    else if (type == cfc_public("CFDataGetTypeID", NULL, 0)) {
        uint32 mutable[] = {allocator, 0, object}, immutable[] = {allocator, object};
        result = options == 2 ? cfc_public("CFDataCreateMutableCopy", mutable, 3)
                              : cfc_public("CFDataCreateCopy", immutable, 2);
    }
    else if (type == cfc_public("CFNumberGetTypeID", NULL, 0)) {
        uint32 number_type = cfc_public("CFNumberGetType", &object, 1);
        _Alignas(16) uint32 bytes[2] = {0};
        uint32 convert[] = {object, number_type, US_TO_SYN68K(bytes)};
        if (!cfp_call("CFNumberGetValue", convert, 3, NULL))
            return 0;
        uint32 create[] = {allocator, number_type, US_TO_SYN68K(bytes)};
        result = cfc_public("CFNumberCreate", create, 3);
    }
    else if (type == cfc_public("CFBooleanGetTypeID", NULL, 0)) {
        if (!cfp_call("CFRetain", &object, 1, NULL))
            return 0;
        result = object;
    }
    else if (type == cfc_public("CFDateGetTypeID", NULL, 0)) {
        /* DateCreate takes f1, outside the word-only callback bridge. Its 1.4
           body is runtime allocation followed by an unchanged eight-byte store. */
        uint32 first, second;
        if (!cfp_get(object + 8, &first) || !cfp_get(object + 12, &second))
            return 0;
        result = cfc_allocate_instance(reference, allocator, cfp_toc(reference, 0x8d8), 8);
        if (result && (!cfp_write(result + 8, first) || !cfp_write(result + 12, second)))
            return 0;
    }
    else if (type == cfc_public("CFStringGetTypeID", NULL, 0)) {
        uint32 mutable[] = {allocator, 0, object}, immutable[] = {allocator, object};
        result = options == 2 ? cfc_public("CFStringCreateMutableCopy", mutable, 3)
                              : cfc_public("CFStringCreateCopy", immutable, 2);
    }
    if (!cfp_call("CFRelease", &allocator, 1, NULL))
        return 0;
    return result;
}

uint32 ROMlib_cfc_CFPropertyListCreateDeepCopy(uint32 allocator, uint32 object, uint32 options)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, object, options}, result = 0;
    if (cfp_reference("CFPropertyListCreateDeepCopy", 0x2b2cc, arguments, 3, &reference, &result))
        result = plist_copy(&reference, allocator, object, options, 0);
    return result;
}

uint32 ROMlib_cfc_CFPropertyListCreateXMLData(uint32 allocator, uint32 object)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, object}, result = 0;
    if (!cfp_reference("CFPropertyListCreateXMLData", 0x26bc4, arguments, 2, &reference, &result))
        return result;
    uint32 create[] = {allocator, 0}, string = cfc_public("CFStringCreateMutable", create, 2);
    if (!string)
        return 0;
    uint32 serialize[] = {string, object};
    if (!cfp_private(&reference, 0x26b34, serialize, 2, NULL))
        return 0;
    uint32 encode[] = {allocator, string, 0x08000100, 0};
    result = cfc_public("CFStringCreateExternalRepresentation", encode, 4);
    if (!cfp_call("CFRelease", &string, 1, NULL))
        return 0;
    return result;
}

uint32 ROMlib_cfc_CFPropertyListCreateFromXMLData(uint32 allocator, uint32 data, uint32 options, uint32 error)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, data, options, error}, result = 0;
    if (cfp_reference("CFPropertyListCreateFromXMLData", 0x2a2ec, arguments, 4, &reference, &result)) {
        uint32 parse[] = {allocator, data, options, error, 1};
        cfp_private(&reference, 0x29f94, parse, 5, &result);
    }
    return result;
}
