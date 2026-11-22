/*+===================================================================
  File:      cfc_preferences.c

  Summary:   Native preferences entry control flow and typed values; original domain, cache, file and suite-order
             workers remain.

  Classes:   None.

  Functions: ROMlib_cfc_CFPreferencesAddSuitePreferencesToApp, ROMlib_cfc_CFPreferencesAppBooleanValue,
             ROMlib_cfc_CFPreferencesAppSynchronize, ROMlib_cfc_CFPreferencesCopyAppValue,
             ROMlib_cfc_CFPreferencesCopyApplicationList, ROMlib_cfc_CFPreferencesCopyKeyList,
             ROMlib_cfc_CFPreferencesCopyMultiple, ROMlib_cfc_CFPreferencesCopyValue,
             ROMlib_cfc_CFPreferencesGetAppBooleanValue, ROMlib_cfc_CFPreferencesGetAppIntegerValue,
             ROMlib_cfc_CFPreferencesRemoveSuitePreferencesFromApp, ROMlib_cfc_CFPreferencesSetAppValue,
             ROMlib_cfc_CFPreferencesSetMultiple, ROMlib_cfc_CFPreferencesSetValue, ROMlib_cfc_CFPreferencesSynchronize

  Origin:    C reconstruction of CoreFoundationLib 1.4 offsets 0..0x1e50 and Universal Interfaces 3.4.2 CFPreferences
             declarations.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "cfc_cohort.h"

static uint32 preferences_domain(const ROMlib_carbon_reference* reference, uint32 application, uint32 user, uint32 host)
{
    uint32 arguments[] = {application, user, host}, result = 0;
    cfp_private(reference, 0x2434, arguments, 3, &result);
    return result;
}

static uint32 preferences_app(const ROMlib_carbon_reference* reference, uint32 application)
{
    uint32 result = 0;
    cfp_private(reference, 0xc54, &application, 1, &result);
    return result;
}

static uint32 preferences_copy_app(const ROMlib_carbon_reference* reference, uint32 key, uint32 application)
{
    uint32 domain = preferences_app(reference, application), result = 0;
    if (domain) {
        uint32 arguments[] = {domain, key};
        cfp_private(reference, 0x78c, arguments, 2, &result);
    }
    return result;
}

static uint32 preferences_boolean(const ROMlib_carbon_reference* reference, uint32 key, uint32 application,
                                  uint32 output)
{
    uint32 object = preferences_copy_app(reference, key, application), result = 0, valid = 0;
    if (object) {
        uint32 type = cfc_public("CFGetTypeID", &object, 1);
        if (type == cfc_public("CFStringGetTypeID", NULL, 0)) {
            uint32 literals = cfp_toc(reference, 0x3c4);
            static const unsigned offsets[] = {0, 5, 9, 15};
            for (unsigned i = 0; i < 4; ++i) {
                uint32 literal = literals + offsets[i];
                uint32 string = cfc_public("__CFStringMakeConstantString", &literal, 1);
                uint32 arguments[] = {object, string, 1};
                if (!cfc_public("CFStringCompare", arguments, 3)) {
                    valid = 1;
                    result = i < 2;
                    break;
                }
            }
        }
        else if (type == cfc_public("CFNumberGetTypeID", NULL, 0)) {
            if (!(cfc_public("CFNumberIsFloatType", &object, 1) & 255)) {
                _Alignas(16) uint32 integer = 0;
                uint32 arguments[] = {object, 9, US_TO_SYN68K(&integer)};
                if (!cfp_call("CFNumberGetValue", arguments, 3, NULL))
                    return 0;
                valid = 1;
                result = CL(integer) != 0;
            }
        }
        else if (type == cfc_public("CFBooleanGetTypeID", NULL, 0)) {
            valid = 1;
            result = object == cfp_toc(reference, 0x356c);
        }
        if (output && !cfc_byte(output, valid))
            return 0;
        cfp_call("CFRelease", &object, 1, NULL);
    }
    else if (output)
        cfc_byte(output, 0);
    return result;
}

uint32 ROMlib_cfc_CFPreferencesCopyAppValue(uint32 key, uint32 application)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {key, application}, result = 0;
    if (cfp_reference("CFPreferencesCopyAppValue", 0, arguments, 2, &reference, &result))
        result = preferences_copy_app(&reference, key, application);
    return result;
}

uint32 ROMlib_cfc_CFPreferencesAppBooleanValue(uint32 key, uint32 application, uint32 output)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {key, application, output}, result = 0;
    if (cfp_reference("CFPreferencesAppBooleanValue", 0x50, arguments, 3, &reference, &result))
        result = preferences_boolean(&reference, key, application, output);
    return result;
}

uint32 ROMlib_cfc_CFPreferencesGetAppBooleanValue(uint32 key, uint32 application, uint32 output)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {key, application, output}, result = 0;
    if (cfp_reference("CFPreferencesGetAppBooleanValue", 0x404, arguments, 3, &reference, &result))
        result = preferences_boolean(&reference, key, application, output);
    return result;
}

uint32 ROMlib_cfc_CFPreferencesGetAppIntegerValue(uint32 key, uint32 application, uint32 output)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {key, application, output}, result = 0;
    if (!cfp_reference("CFPreferencesGetAppIntegerValue", 0x424, arguments, 3, &reference, &result))
        return result;
    uint32 object = preferences_copy_app(&reference, key, application), valid = 0;
    if (object) {
        uint32 type = cfc_public("CFGetTypeID", &object, 1);
        if (type == cfc_public("CFStringGetTypeID", NULL, 0)) {
            uint32 length = cfc_public("CFStringGetLength", &object, 1);
            uint32 direct = cfc_public("CFStringGetCharactersPtr", &object, 1);
            _Alignas(256) uint32 buffer[38] = {0}, index = 0, integer = 0;
            /* CFStringInlineBuffer: 64 UTF16 units, string, direct pointer,
               range and two buffered-range indices, all in guest byte order. */
            buffer[32] = CL(object);
            buffer[33] = CL(direct);
            buffer[35] = CL(length);
            uint32 scan[] = {US_TO_SYN68K(buffer), 0, US_TO_SYN68K(&index), 0, US_TO_SYN68K(&integer)};
            uint32 parsed = 0;
            if (!cfp_private(&reference, 0x425e0, scan, 5, &parsed))
                return 0;
            valid = (parsed & 255) && CL(index) == length;
            result = valid ? CL(integer) : 0;
        }
        else if (type == cfc_public("CFNumberGetTypeID", NULL, 0)) {
            valid = !(cfc_public("CFNumberIsFloatType", &object, 1) & 255);
            if (valid) {
                _Alignas(16) uint32 integer = 0;
                uint32 convert[] = {object, 14, US_TO_SYN68K(&integer)};
                if (!cfp_call("CFNumberGetValue", convert, 3, NULL))
                    return 0;
                result = CL(integer);
            }
        }
        if (output && !cfc_byte(output, valid))
            return 0;
        cfp_call("CFRelease", &object, 1, NULL);
    }
    else if (output)
        cfc_byte(output, 0);
    return result;
}

void ROMlib_cfc_CFPreferencesSetAppValue(uint32 key, uint32 value, uint32 application)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {key, value, application}, ignored = 0;
    if (!cfp_reference("CFPreferencesSetAppValue", 0x444, arguments, 3, &reference, &ignored))
        return;
    uint32 domain = preferences_app(&reference, application);
    if (domain) {
        uint32 operation[] = {domain, key, value};
        cfp_private(&reference, value ? 0x828 : 0x908, operation, value ? 3 : 2, NULL);
    }
}

uint32 ROMlib_cfc_CFPreferencesAppSynchronize(uint32 application)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (!cfp_reference("CFPreferencesAppSynchronize", 0x4b4, &application, 1, &reference, &result))
        return result;
    uint32 lock = US_TO_SYN68K(reference.toc) + 0x7d8;
    if (!cfp_private(&reference, 0x1a98c, &lock, 1, NULL))
        return 0;
    uint32 dictionary = cfp_toc(&reference, 0x7d4), domain = 0;
    if (dictionary) {
        uint32 lookup[] = {dictionary, application};
        domain = cfc_public("CFDictionaryGetValue", lookup, 2);
    }
    int ok = cfp_private(&reference, domain ? 0x9dc : 0x2634, domain ? &domain : NULL, domain ? 1 : 0, &result);
    if (!cfp_private(&reference, 0x1a990, &lock, 1, NULL) || !ok)
        return 0;
    return result;
}

void ROMlib_cfc_CFPreferencesAddSuitePreferencesToApp(uint32 application, uint32 suite)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {application, suite}, ignored = 0;
    if (!cfp_reference("CFPreferencesAddSuitePreferencesToApp", 0x1040, arguments, 2, &reference, &ignored))
        return;
    uint32 operation[] = {preferences_app(&reference, application), suite};
    if (operation[0])
        cfp_private(&reference, 0x1078, operation, 2, NULL);
}

void ROMlib_cfc_CFPreferencesRemoveSuitePreferencesFromApp(uint32 application, uint32 suite)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {application, suite}, ignored = 0;
    if (!cfp_reference("CFPreferencesRemoveSuitePreferencesFromApp", 0x12d8, arguments, 2, &reference, &ignored))
        return;
    uint32 operation[] = {preferences_app(&reference, application), suite};
    if (operation[0])
        cfp_private(&reference, 0x1310, operation, 2, NULL);
}

uint32 ROMlib_cfc_CFPreferencesCopyValue(uint32 key, uint32 application, uint32 user, uint32 host)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {key, application, user, host}, result = 0;
    if (!cfp_reference("CFPreferencesCopyValue", 0x1a30, arguments, 4, &reference, &result))
        return result;
    uint32 operation[] = {preferences_domain(&reference, application, user, host), key};
    if (operation[0])
        cfp_private(&reference, 0x2c10, operation, 2, &result);
    return result;
}

uint32 ROMlib_cfc_CFPreferencesCopyMultiple(uint32 keys, uint32 application, uint32 user, uint32 host)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {keys, application, user, host}, result = 0;
    if (!cfp_reference("CFPreferencesCopyMultiple", 0x1a88, arguments, 4, &reference, &result))
        return result;
    uint32 domain = preferences_domain(&reference, application, user, host);
    if (!domain)
        return 0;
    if (!keys) {
        cfp_private(&reference, 0x2ee8, &domain, 1, &result);
        return result;
    }
    uint32 count = cfc_public("CFArrayGetCount", &keys, 1), allocator = cfc_public("CFGetAllocator", &domain, 1);
    uint32 create[] = {allocator, count, US_TO_SYN68K(reference.toc) + 0x33c0, US_TO_SYN68K(reference.toc) + 0x33f0};
    result = cfc_public("CFDictionaryCreateMutable", create, 4);
    if (!result)
        return 0;
    for (uint32 i = 0; (int32)i < (int32)count; ++i) {
        uint32 index[] = {keys, i}, key = cfc_public("CFArrayGetValueAtIndex", index, 2), value = 0;
        uint32 lookup[] = {domain, key};
        if (!cfp_private(&reference, 0x2c10, lookup, 2, &value))
            return 0;
        if (value) {
            uint32 set[] = {result, key, value};
            if (!cfp_call("CFDictionarySetValue", set, 3, NULL) || !cfp_call("CFRelease", &value, 1, NULL))
                return 0;
        }
    }
    return result;
}

void ROMlib_cfc_CFPreferencesSetValue(uint32 key, uint32 value, uint32 application, uint32 user, uint32 host)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {key, value, application, user, host}, ignored = 0;
    if (!cfp_reference("CFPreferencesSetValue", 0x1b90, arguments, 5, &reference, &ignored))
        return;
    uint32 operation[] = {preferences_domain(&reference, application, user, host), key, value};
    if (operation[0])
        cfp_private(&reference, 0x2c4c, operation, 3, NULL);
}

void ROMlib_cfc_CFPreferencesSetMultiple(uint32 values, uint32 removed, uint32 application, uint32 user, uint32 host)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {values, removed, application, user, host}, ignored = 0;
    if (!cfp_reference("CFPreferencesSetMultiple", 0x1bf0, arguments, 5, &reference, &ignored))
        return;
    uint32 domain = preferences_domain(&reference, application, user, host);
    if (!domain)
        return;
    uint32 count = values ? cfc_public("CFDictionaryGetCount", &values, 1) : 0;
    if (count) {
        uint32 bytes;
        if (!cfc_product(count, 8, &bytes))
            return;
        uint32 allocator = cfc_public("CFGetAllocator", &domain, 1), allocate[] = {allocator, bytes, 0};
        uint32 buffer = cfc_public("CFAllocatorAllocate", allocate, 3);
        if (buffer) {
            uint32 keys = buffer, items = buffer + count * 4, collect[] = {values, keys, items};
            if (!cfp_call("CFDictionaryGetKeysAndValues", collect, 3, NULL))
                return;
            for (uint32 i = 0; (int32)i < (int32)count; ++i) {
                uint32 set[] = {domain, cfp_read(keys + i * 4), cfp_read(items + i * 4)};
                if (!cfp_private(&reference, 0x2c4c, set, 3, NULL))
                    return;
            }
            uint32 cleanup[] = {allocator, buffer};
            if (!cfp_call("CFAllocatorDeallocate", cleanup, 2, NULL))
                return;
        }
    }
    count = removed ? cfc_public("CFArrayGetCount", &removed, 1) : 0;
    for (uint32 i = 0; (int32)i < (int32)count; ++i) {
        uint32 index[] = {removed, i}, key = cfc_public("CFArrayGetValueAtIndex", index, 2);
        uint32 set[] = {domain, key, 0};
        if (!cfp_private(&reference, 0x2c4c, set, 3, NULL))
            return;
    }
}

uint32 ROMlib_cfc_CFPreferencesSynchronize(uint32 application, uint32 user, uint32 host)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {application, user, host}, result = 0;
    if (!cfp_reference("CFPreferencesSynchronize", 0x1d38, arguments, 3, &reference, &result))
        return result;
    uint32 domain = preferences_domain(&reference, application, user, host);
    if (domain)
        cfp_private(&reference, 0x2c90, &domain, 1, &result);
    return result & 255;
}

uint32 ROMlib_cfc_CFPreferencesCopyApplicationList(uint32 user, uint32 host)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {user, host}, result = 0;
    if (cfp_reference("CFPreferencesCopyApplicationList", 0x1d7c, arguments, 2, &reference, &result))
        cfp_private(&reference, 0x2694, arguments, 2, &result);
    return result;
}

uint32 ROMlib_cfc_CFPreferencesCopyKeyList(uint32 application, uint32 user, uint32 host)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {application, user, host}, result = 0;
    if (!cfp_reference("CFPreferencesCopyKeyList", 0x1da0, arguments, 3, &reference, &result))
        return result;
    uint32 domain = preferences_domain(&reference, application, user, host);
    if (!domain)
        return 0;
    uint32 allocator = cfc_public("_CFTemporaryMemoryAllocator", NULL, 0);
    _Alignas(16) uint32 keys = 0, count = 0;
    uint32 collect[] = {allocator, domain, US_TO_SYN68K(&keys), US_TO_SYN68K(&count)};
    if (!cfp_private(&reference, 0x2cc8, collect, 4, NULL) || !CL(count))
        return 0;
    uint32 create[] = {allocator, CL(keys), CL(count), US_TO_SYN68K(reference.toc) + 0x3234};
    result = cfc_public("CFArrayCreate", create, 4);
    uint32 cleanup[] = {allocator, CL(keys)};
    if (!cfp_call("CFAllocatorDeallocate", cleanup, 2, NULL))
        return 0;
    return result;
}
