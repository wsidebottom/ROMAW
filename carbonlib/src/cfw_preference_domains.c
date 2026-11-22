/*+===================================================================
  File:      cfw_preference_domains.c

  Summary:   Preference-domain allocation and callback dispatch using the real
             initialized domain class, callback records and temporary allocator.

  Classes:   None.

  Functions: ROMlib_cfw_PreferencesAllocator, ROMlib_cfw_PreferencesCreateDomainList,
             ROMlib_cfw_PreferencesDomainCopyDictionary, ROMlib_cfw_PreferencesDomainCreate,
             ROMlib_cfw_PreferencesDomainCreateValueForKey, ROMlib_cfw_PreferencesDomainExists,
             ROMlib_cfw_PreferencesDomainGetKeysAndValues, ROMlib_cfw_PreferencesDomainSet,
             ROMlib_cfw_PreferencesDomainSetDictionary, ROMlib_cfw_PreferencesDomainSetIsWorldReadable,
             ROMlib_cfw_PreferencesDomainSynchronize, ROMlib_cfw_PreferencesStandardDomain,
             ROMlib_cfw_SynchronizeDomainCache

  Origin:    C reconstruction of CoreFoundationLib 1.4 offsets 0x1650 and 0x2b70..0x2d4c. Private callback ABI follows
             the disassembled register use.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "cfw_internal.h"

static int domain_fields(uint32 domain, uint32* context, uint32* data, uint32* callbacks)
{
    return cfp_get(domain + 12, context) && cfp_get(domain + 16, data) && cfp_get(domain + 8, callbacks);
}

uint32 ROMlib_cfw_PreferencesAllocator(void)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (cfp_reference("__CFPreferencesAllocator", 0x1650, NULL, 0, &reference, &result))
        result = cfc_public("_CFTemporaryMemoryAllocator", NULL, 0);
    return result;
}

uint32 ROMlib_cfw_PreferencesDomainCreate(uint32 context, uint32 callbacks)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {context, callbacks}, result = 0;
    if (!cfp_reference("_CFPreferencesDomainCreate", 0x2b70, arguments, 2, &reference, &result))
        return result;
    uint32 allocator = cfc_public("_CFTemporaryMemoryAllocator", NULL, 0);
    result = cfc_allocate_instance(&reference, allocator, cfp_toc(&reference, 0x7dc), 12);
    if (!result || !cfp_write(result + 8, callbacks))
        return 0;
    if (context && !cfp_call("CFRetain", &context, 1, NULL))
        return 0;
    if (!cfp_write(result + 12, context))
        return 0;
    uint32 procedure, callback[] = {allocator, context}, data = 0;
    if (!cfp_get(callbacks, &procedure) || !cfp_callback(procedure, callback, 2, &data) ||
        !cfp_write(result + 16, data))
        return 0;
    return result;
}

uint32 ROMlib_cfw_PreferencesDomainCreateValueForKey(uint32 domain, uint32 key)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {domain, key}, result = 0, context, data, callbacks, procedure;
    if (!cfp_reference("_CFPreferencesDomainCreateValueForKey", 0x2c10, arguments, 2, &reference, &result))
        return result;
    if (domain_fields(domain, &context, &data, &callbacks) && cfp_get(callbacks + 8, &procedure)) {
        uint32 callback[] = {context, data, key};
        cfp_callback(procedure, callback, 3, &result);
    }
    return result;
}

void ROMlib_cfw_PreferencesDomainSet(uint32 domain, uint32 key, uint32 value)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {domain, key, value}, ignored = 0, context, data, callbacks, procedure;
    if (!cfp_reference("_CFPreferencesDomainSet", 0x2c4c, arguments, 3, &reference, &ignored))
        return;
    if (domain_fields(domain, &context, &data, &callbacks) && cfp_get(callbacks + 12, &procedure)) {
        uint32 callback[] = {context, data, key, value};
        cfp_callback(procedure, callback, 4, NULL);
    }
}

uint32 ROMlib_cfw_PreferencesDomainSynchronize(uint32 domain)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0, context, data, callbacks, procedure;
    if (!cfp_reference("_CFPreferencesDomainSynchronize", 0x2c90, &domain, 1, &reference, &result))
        return result;
    if (domain_fields(domain, &context, &data, &callbacks) && cfp_get(callbacks + 16, &procedure)) {
        uint32 callback[] = {context, data};
        cfp_callback(procedure, callback, 2, &result);
    }
    return result;
}

void ROMlib_cfw_PreferencesDomainGetKeysAndValues(uint32 allocator, uint32 domain, uint32 keys, uint32 count)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, domain, keys, count}, ignored = 0, context, data, callbacks, procedure;
    if (!cfp_reference("_CFPreferencesDomainGetKeysAndValues", 0x2cc8, arguments, 4, &reference, &ignored))
        return;
    if (domain_fields(domain, &context, &data, &callbacks) && cfp_get(callbacks + 20, &procedure)) {
        uint32 callback[] = {allocator, context, data, keys, count};
        cfp_callback(procedure, callback, 5, NULL);
    }
}

void ROMlib_cfw_PreferencesDomainSetIsWorldReadable(uint32 domain, uint32 readable)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {domain, readable}, ignored = 0, context, data, callbacks, procedure;
    if (!cfp_reference("_CFPreferencesDomainSetIsWorldReadable", 0x2d0c, arguments, 2, &reference, &ignored))
        return;
    if (domain_fields(domain, &context, &data, &callbacks) && cfp_get(callbacks + 24, &procedure) && procedure) {
        uint32 callback[] = {context, data, readable};
        cfp_callback(procedure, callback, 3, NULL);
    }
}

uint32 ROMlib_cfw_PreferencesDomainCopyDictionary(uint32 domain)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (!cfp_reference("_CFPreferencesDomainCopyDictionary", 0x2ee8, &domain, 1, &reference, &result))
        return result;
    uint32 allocator = cfc_public("_CFTemporaryMemoryAllocator", NULL, 0);
    _Alignas(16) uint32 keys = 0, count = 0;
    ROMlib_cfw_PreferencesDomainGetKeysAndValues(allocator, domain, US_TO_SYN68K(&keys), US_TO_SYN68K(&count));
    uint32 length = CL(count), values, bytes;
    if (!length || !CL(keys))
        return 0;
    if (!cfc_product(length, 4, &bytes))
        return 0;
    values = CL(keys) + bytes;
    uint32 create[] = {allocator,
                       CL(keys),
                       values,
                       length,
                       US_TO_SYN68K(reference.toc) + 0x33c0,
                       US_TO_SYN68K(reference.toc) + 0x33f0};
    result = cfc_public("CFDictionaryCreate", create, 6);
    uint32 cleanup[] = {allocator, CL(keys)};
    if (!cfp_call("CFAllocatorDeallocate", cleanup, 2, NULL))
        return 0;
    return result;
}

void ROMlib_cfw_PreferencesDomainSetDictionary(uint32 domain, uint32 dictionary)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {domain, dictionary}, ignored = 0;
    if (!cfp_reference("_CFPreferencesDomainSetDictionary", 0x2d50, arguments, 2, &reference, &ignored))
        return;
    uint32 allocator = cfc_public("_CFTemporaryMemoryAllocator", NULL, 0);
    _Alignas(128) uint32 local[32], keys = CL(US_TO_SYN68K(local)), count = CL(16);
    uint32 system = cfp_toc(&reference, 0x316c);
    ROMlib_cfw_PreferencesDomainGetKeysAndValues(system, domain, US_TO_SYN68K(&keys), US_TO_SYN68K(&count));
    if ((int32)CL(count) > 16) {
        keys = count = 0;
        ROMlib_cfw_PreferencesDomainGetKeysAndValues(allocator, domain, US_TO_SYN68K(&keys), US_TO_SYN68K(&count));
    }
    for (uint32 i = 0; (int32)i < (int32)CL(count); ++i)
        ROMlib_cfw_PreferencesDomainSet(domain, cfp_read(CL(keys) + i * 4), 0);
    if (CL(keys) != US_TO_SYN68K(local)) {
        uint32 cleanup[] = {allocator, CL(keys)};
        if (!cfp_call("CFAllocatorDeallocate", cleanup, 2, NULL))
            return;
    }
    if (!dictionary)
        return;
    uint32 length = cfc_public("CFDictionaryGetCount", &dictionary, 1);
    if (!length)
        return;
    uint32 buffer = US_TO_SYN68K(local), bytes;
    if (!cfc_product(length, 4, &bytes))
        return;
    if (length >= 32) {
        uint32 allocate[] = {allocator, bytes, 0};
        buffer = cfc_public("CFAllocatorAllocate", allocate, 3);
        if (!buffer)
            return;
    }
    uint32 collect[] = {dictionary, buffer, 0};
    if (!cfp_call("CFDictionaryGetKeysAndValues", collect, 3, NULL))
        return;
    for (uint32 i = 0; (int32)i < (int32)length; ++i) {
        uint32 key = cfp_read(buffer + i * 4), lookup[] = {dictionary, key};
        uint32 value = cfc_public("CFDictionaryGetValue", lookup, 2);
        ROMlib_cfw_PreferencesDomainSet(domain, key, value);
    }
    if (buffer != US_TO_SYN68K(local)) {
        uint32 cleanup[] = {allocator, buffer};
        cfp_call("CFAllocatorDeallocate", cleanup, 2, NULL);
    }
}

uint32 ROMlib_cfw_PreferencesDomainExists(uint32 application, uint32 user, uint32 host)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {application, user, host}, result = 0;
    if (!cfp_reference("_CFPreferencesDomainExists", 0x2f94, arguments, 3, &reference, &result))
        return result;
    uint32 domain = ROMlib_cfw_PreferencesStandardDomain(application, user, host);
    if (!domain)
        return 0;
    _Alignas(16) uint32 count = 0;
    ROMlib_cfw_PreferencesDomainGetKeysAndValues(cfp_toc(&reference, 0x316c), domain, 0, US_TO_SYN68K(&count));
    return (int32)CL(count) > 0;
}

static uint32 domain_private(const ROMlib_carbon_reference* reference, uint32 offset, uint32 first, uint32 second,
                             uint32 third)
{
    uint32 arguments[] = {first, second, third}, result = 0;
    cfp_private(reference, offset, arguments, 3, &result);
    return result;
}

uint32 ROMlib_cfw_PreferencesStandardDomain(uint32 application, uint32 user, uint32 host)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {application, user, host}, result = 0;
    if (!cfp_reference("_CFPreferencesStandardDomain", 0x2434, arguments, 3, &reference, &result))
        return result;
    uint32 any_user = cfp_toc(&reference, 0x2184), current_user = cfp_toc(&reference, 0x2190);
    uint32 current_host = cfp_toc(&reference, 0x218c);
    if (user != any_user && user != current_user)
        return 0;
    /* File-system URL and canonical cache-key construction remain real workers. */
    uint32 key = domain_private(&reference, 0x20a8, application, user, current_host);
    if (!key)
        return 0;
    uint32 cache_address = US_TO_SYN68K(reference.toc) + 0x7e0;
    uint32 cache = cfp_read(cache_address);
    if (!cache) {
        uint32 allocator = ROMlib_cfw_PreferencesAllocator();
        cache = CFW_CALL("CFDictionaryCreateMutable", allocator, 0, US_TO_SYN68K(reference.toc) + 0x33c0,
                         US_TO_SYN68K(reference.toc) + 0x33f0);
        cfp_write(cache_address, cache);
    }
    result = CFW_CALL("CFDictionaryGetValue", cache, key);
    if (!result) {
        uint32 url = domain_private(&reference, 0x21c8, application, user, host);
        if (url) {
            result = ROMlib_cfw_PreferencesDomainCreate(url, US_TO_SYN68K(reference.toc) + 0x21d4);
            if (user == any_user)
                ROMlib_cfw_PreferencesDomainSetIsWorldReadable(result, 1);
            cfp_call("CFRelease", &url, 1, NULL);
        }
        if (result && cache) {
            CFW_CALL("CFDictionarySetValue", cache, key, result);
            const unsigned char* compatibility = ROMlib_carbon_guest_memory(US_TO_SYN68K(reference.toc) + 0x7f0, 1, 0);
            if (compatibility && *compatibility) {
                uint32 alias = domain_private(&reference, 0x20a8, application,
                                              user == any_user ? current_user : any_user, current_host);
                if (alias)
                    CFW_CALL("CFDictionarySetValue", cache, alias, result);
            }
            cfp_call("CFRelease", &result, 1, NULL);
        }
    }
    cfp_call("CFRelease", &key, 1, NULL);
    return result;
}

uint32 ROMlib_cfw_SynchronizeDomainCache(void)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (!cfp_reference("_CFSynchronizeDomainCache", 0x2634, NULL, 0, &reference, &result))
        return result;
    uint32 cache = cfp_toc(&reference, 0x7e0);
    if (!cache)
        return 1;
    uint32 count = CFW_CALL("CFDictionaryGetCount", cache), bytes;
    if (!cfc_product(count, 4, &bytes))
        return 0;
    uint32 allocator = ROMlib_cfw_PreferencesAllocator();
    uint32 values = CFW_CALL("CFAllocatorAllocate", allocator, bytes, 0);
    if (bytes && !values)
        return 0;
    CFW_CALL("CFDictionaryGetKeysAndValues", cache, 0, values);
    result = 1;
    for (uint32 i = 0; i < count; ++i) {
        uint32 value;
        if (!cfp_get(values + 4 * i, &value)) {
            result = 0;
            break;
        }
        if (!(ROMlib_cfw_PreferencesDomainSynchronize(value) & 255))
            result = 0;
    }
    CFW_CALL("CFAllocatorDeallocate", allocator, values);
    return result;
}

uint32 ROMlib_cfw_PreferencesCreateDomainList(uint32 user, uint32 host)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {user, host}, result = 0;
    if (!cfp_reference("_CFPreferencesCreateDomainList", 0x2694, arguments, 2, &reference, &result))
        return result;
    uint32 allocator = ROMlib_cfw_PreferencesAllocator();
    uint32 directory = 0;
    cfp_private(&reference, 0x1970, arguments, 2, &directory);
    if (!directory || (host != cfp_toc(&reference, 0x2180) && host != cfp_toc(&reference, 0x218c)))
        return 0;
    uint32 literals = cfp_toc(&reference, 0x3d0);
    uint32 extension = CFW_CALL("CFStringCreateWithCString", allocator, literals + 0x104, 0x600);
    uint32 extension_length = CFW_CALL("CFStringGetLength", extension);
    uint32 files = CFW_CALL("CFURLCreatePropertyFromResource", allocator, directory, cfp_toc(&reference, 0x21f8), 0);
    cfp_call("CFRelease", &directory, 1, NULL);
    if (files) {
        result = CFW_CALL("CFArrayCreateMutableCopy", allocator, 0, files);
        cfp_call("CFRelease", &files, 1, NULL);
    }
    else
        result = CFW_CALL("CFArrayCreateMutable", allocator, 0, US_TO_SYN68K(reference.toc) + 0x3234);
    int32 count = (int32)CFW_CALL("CFArrayGetCount", result);
    uint32 any_application = cfp_toc(&reference, 0x217c);
    for (int32 i = count - 1; i >= 0; --i) {
        uint32 url = CFW_CALL("CFArrayGetValueAtIndex", result, (uint32)i);
        uint32 string = CFW_CALL("CFURLGetString", url);
        if (!(CFW_CALL("CFStringHasSuffix", string, extension) & 255)) {
            CFW_CALL("CFArrayRemoveValueAtIndex", result, (uint32)i);
            continue;
        }
        uint32 length = CFW_CALL("CFStringGetLength", string);
        uint32 app = CFW_CALL("CFStringCreateWithSubstring", allocator, string, 0, length - extension_length);
        uint32 global = CFW_CALL("__CFStringMakeConstantString", literals + 0xe4);
        uint32 replacement = (CFW_CALL("CFEqual", app, global) & 255) ? any_application : app;
        CFW_CALL("CFArraySetValueAtIndex", result, (uint32)i, replacement);
        cfp_call("CFRelease", &app, 1, NULL);
    }
    cfp_call("CFRelease", &extension, 1, NULL);
    uint32 cache = cfp_toc(&reference, 0x7e0);
    if (!cache)
        return result;
    count = (int32)CFW_CALL("CFDictionaryGetCount", cache);
    uint32 bytes;
    if (!cfc_product((uint32)count, 8, &bytes))
        return result;
    uint32 storage = CFW_CALL("CFAllocatorAllocate", allocator, bytes, 0);
    if (bytes && !storage)
        return result;
    uint32 values = storage + 4 * (uint32)count;
    CFW_CALL("CFDictionaryGetKeysAndValues", cache, storage, values);
    uint32 prefix = domain_private(&reference, 0x1fb8, user, host, 0);
    uint32 prefix_length = CFW_CALL("CFStringGetLength", prefix);
    for (int32 i = 0; i < count; ++i) {
        uint32 key = cfp_read(storage + 4 * (uint32)i);
        if (!(CFW_CALL("CFStringHasPrefix", key, prefix) & 255))
            continue;
        uint32 length = CFW_CALL("CFStringGetLength", key);
        uint32 app = CFW_CALL("CFStringCreateWithSubstring", allocator, key, prefix_length, length - prefix_length);
        uint32 global = CFW_CALL("__CFStringMakeConstantString", literals + 0xcc);
        if (CFW_CALL("CFEqual", app, global) & 255) {
            cfp_call("CFRelease", &app, 1, NULL);
            app = any_application;
            cfp_call("CFRetain", &app, 1, NULL);
        }
        else if (CFW_CALL("CFEqual", app, cfp_toc(&reference, 0x2188)) & 255) {
            cfp_call("CFRelease", &app, 1, NULL);
            app = cfc_public("_CFProcessNameString", NULL, 0);
            cfp_call("CFRetain", &app, 1, NULL);
        }
        _Alignas(16) uint32 entries = 0;
        ROMlib_cfw_PreferencesDomainGetKeysAndValues(cfp_toc(&reference, 0x316c), cfp_read(values + 4 * (uint32)i), 0,
                                                     US_TO_SYN68K(&entries));
        uint32 array_count = CFW_CALL("CFArrayGetCount", result);
        if (!CL(entries)) {
            uint32 index = CFW_CALL("CFArrayGetFirstIndexOfValue", result, 0, array_count, app);
            if ((int32)index >= 0)
                CFW_CALL("CFArrayRemoveValueAtIndex", result, index);
        }
        else if (!(CFW_CALL("CFArrayContainsValue", result, 0, array_count, app) & 255))
            CFW_CALL("CFArrayAppendValue", result, app);
        cfp_call("CFRelease", &app, 1, NULL);
    }
    cfp_call("CFRelease", &prefix, 1, NULL);
    CFW_CALL("CFAllocatorDeallocate", allocator, storage);
    return result;
}
