/*+===================================================================
  File:      cfw_application_preferences.c

  Summary:   Native application preference records, cache ownership and ordered domain search lists.

  Classes:   None.

  Functions: ROMlib_cfw_ApplicationPreferencesAddDomain, ROMlib_cfw_ApplicationPreferencesAddSuitePreferences,
             ROMlib_cfw_ApplicationPreferencesContainsDomain, ROMlib_cfw_ApplicationPreferencesCopyRepresentation,
             ROMlib_cfw_ApplicationPreferencesCopyRepresentationWithHint,
             ROMlib_cfw_ApplicationPreferencesCreateValueForKey, ROMlib_cfw_ApplicationPreferencesCreateWithUser,
             ROMlib_cfw_ApplicationPreferencesRemove, ROMlib_cfw_ApplicationPreferencesRemoveDomain,
             ROMlib_cfw_ApplicationPreferencesRemoveSuitePreferences, ROMlib_cfw_ApplicationPreferencesSet,
             ROMlib_cfw_ApplicationPreferencesSetCacheForApp, ROMlib_cfw_ApplicationPreferencesSetSearchList,
             ROMlib_cfw_ApplicationPreferencesSetStandardSearchList, ROMlib_cfw_ApplicationPreferencesSynchronize,
             ROMlib_cfw_ApplicationPreferencesUpdate, ROMlib_cfw_DeallocateApplicationPreferences,
             ROMlib_cfw_StandardApplicationPreferences

  Origin:    C reconstruction of the local CoreFoundationLib 1.4 disassembly at 0x538..0x1650; lock helpers in this
             profile are empty.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "cfw_internal.h"

static uint32 toc_address(const ROMlib_carbon_reference* reference, unsigned offset)
{
    return US_TO_SYN68K(reference->toc) + offset;
}

static void invalidate(uint32 preferences)
{
    uint32 cached;
    if (cfp_get(preferences + 4, &cached) && cached) {
        cfp_call("CFRelease", &cached, 1, NULL);
        cfp_write(preferences + 4, 0);
    }
}

static uint32 search_list(uint32 preferences)
{
    return cfp_read(preferences);
}

static uint32 domain(const ROMlib_carbon_reference* reference, uint32 app, uint32 user, uint32 host)
{
    (void)reference;
    return ROMlib_cfw_PreferencesStandardDomain(app, user, host);
}

static uint32 ensure_cache(const ROMlib_carbon_reference* reference)
{
    uint32 cache = cfp_toc(reference, 0x7d4);
    if (!cache) {
        uint32 args[] = {0, 0, toc_address(reference, 0x33c0), 0};
        cache = cfc_public("CFDictionaryCreateMutable", args, 4);
        cfp_write(toc_address(reference, 0x7d4), cache);
    }
    return cache;
}

static uint32 representation(const ROMlib_carbon_reference* reference, uint32 preferences)
{
    uint32 cached = cfp_read(preferences + 4);
    if (!cached) {
        /* The merge worker still owns callback-sized scratch storage. */
        cfp_private(reference, 0x57c, &preferences, 1, NULL);
        cached = cfp_read(preferences + 4);
    }
    return cached;
}

void ROMlib_cfw_ApplicationPreferencesUpdate(uint32 preferences)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {preferences}, result = 0;
    if (!cfp_reference("_CFApplicationPreferencesUpdate", 0x744, arguments, 1, &reference, &result))
        return;
    invalidate(preferences);
}

uint32 ROMlib_cfw_ApplicationPreferencesCreateValueForKey(uint32 preferences, uint32 key)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {preferences, key}, result = 0;
    if (!cfp_reference("_CFApplicationPreferencesCreateValueForKey", 0x78c, arguments, 2, &reference, &result))
        return result;
    uint32 cached = representation(&reference, preferences);
    if (cached) {
        uint32 lookup[] = {cached, key};
        result = cfc_public("CFDictionaryGetValue", lookup, 2);
        if (result)
            cfp_call("CFRetain", &result, 1, NULL);
    }
    return result;
}

void ROMlib_cfw_ApplicationPreferencesSet(uint32 preferences, uint32 key, uint32 value)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {preferences, key, value}, result = 0;
    if (!cfp_reference("_CFApplicationPreferencesSet", 0x828, arguments, 3, &reference, &result))
        return;
    uint32 target =
        domain(&reference, cfp_read(preferences + 8), cfp_toc(&reference, 0x2190), cfp_toc(&reference, 0x2180));
    ROMlib_cfw_PreferencesDomainSet(target, key, value);
    if (ROMlib_cfw_ApplicationPreferencesContainsDomain(preferences, target) & 255)
        invalidate(preferences);
}

void ROMlib_cfw_ApplicationPreferencesRemove(uint32 preferences, uint32 key)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {preferences, key}, result = 0;
    if (!cfp_reference("_CFApplicationPreferencesRemove", 0x908, arguments, 2, &reference, &result))
        return;
    uint32 target =
        domain(&reference, cfp_read(preferences + 8), cfp_toc(&reference, 0x2190), cfp_toc(&reference, 0x2180));
    ROMlib_cfw_PreferencesDomainSet(target, key, 0);
    if (ROMlib_cfw_ApplicationPreferencesContainsDomain(preferences, target) & 255)
        invalidate(preferences);
}

uint32 ROMlib_cfw_ApplicationPreferencesSynchronize(uint32 preferences)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {preferences}, result = 0;
    if (!cfp_reference("_CFApplicationPreferencesSynchronize", 0xa38, arguments, 1, &reference, &result))
        return result;
    cfp_private(&reference, 0x2634, NULL, 0, &result);
    invalidate(preferences);
    return result;
}

uint32 ROMlib_cfw_ApplicationPreferencesCreateWithUser(uint32 user, uint32 application)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {user, application}, result = 0;
    if (!cfp_reference("_CFApplicationPreferencesCreateWithUser", 0xa88, arguments, 2, &reference, &result))
        return result;
    uint32 allocator = ROMlib_cfw_PreferencesAllocator();
    uint32 allocation[] = {allocator, 12, 0};
    result = cfc_public("CFAllocatorAllocate", allocation, 3);
    if (!result)
        return 0;
    cfp_write(result + 4, 0);
    cfp_call("CFRetain", &application, 1, NULL);
    cfp_write(result + 8, application);
    uint32 create[] = {allocator, 0, toc_address(&reference, 0x3234)};
    uint32 list = cfc_public("CFArrayCreateMutable", create, 3);
    cfp_write(result, list);
    if (!list) {
        uint32 dispose[] = {allocator, result};
        cfp_call("CFAllocatorDeallocate", dispose, 2, NULL);
        cfp_call("CFRelease", &application, 1, NULL);
        result = 0;
    }
    return result;
}

void ROMlib_cfw_ApplicationPreferencesSetStandardSearchList(uint32 preferences)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {preferences}, result = 0;
    if (!cfp_reference("_CFApplicationPreferencesSetStandardSearchList", 0xb44, arguments, 1, &reference, &result))
        return;
    uint32 create[] = {0, 8, toc_address(&reference, 0x3234)};
    uint32 list = cfc_public("CFArrayCreateMutable", create, 3);
    uint32 app = cfp_read(preferences + 8), any_app = cfp_toc(&reference, 0x217c);
    uint32 current_user = cfp_toc(&reference, 0x2190), any_user = cfp_toc(&reference, 0x2184);
    uint32 host = cfp_toc(&reference, 0x218c);
    uint32 applications[] = {app, any_app, app, any_app};
    for (unsigned i = 0; i < 4; ++i) {
        uint32 item = domain(&reference, applications[i], i < 2 ? current_user : any_user, host);
        if (item) {
            uint32 append[] = {list, item};
            cfp_call("CFArrayAppendValue", append, 2, NULL);
        }
    }
    ROMlib_cfw_ApplicationPreferencesSetSearchList(preferences, list);
    cfp_call("CFRelease", &list, 1, NULL);
}

uint32 ROMlib_cfw_StandardApplicationPreferences(uint32 application)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {application}, result = 0;
    if (!cfp_reference("_CFStandardApplicationPreferences", 0xc54, arguments, 1, &reference, &result))
        return result;
    uint32 cache = ensure_cache(&reference);
    if (!cache)
        return 0;
    uint32 lookup[] = {cache, application};
    result = cfc_public("CFDictionaryGetValue", lookup, 2);
    if (!result) {
        result = ROMlib_cfw_ApplicationPreferencesCreateWithUser(cfp_toc(&reference, 0x2190), application);
        uint32 insert[] = {cache, application, result};
        cfp_call("CFDictionarySetValue", insert, 3, NULL);
        ROMlib_cfw_ApplicationPreferencesSetStandardSearchList(result);
    }
    return result;
}

void ROMlib_cfw_ApplicationPreferencesSetCacheForApp(uint32 preferences, uint32 application)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {preferences, application}, result = 0;
    if (!cfp_reference("_CFApplicationPreferencesSetCacheForApp", 0xd3c, arguments, 2, &reference, &result))
        return;
    uint32 cache = cfp_toc(&reference, 0x7d4), old = 0;
    if (cache) {
        uint32 lookup[] = {cache, application};
        old = cfc_public("CFDictionaryGetValue", lookup, 2);
    }
    else
        cache = ensure_cache(&reference);
    uint32 insert[] = {cache, application, preferences};
    cfp_call("CFDictionarySetValue", insert, 3, NULL);
    if (old)
        ROMlib_cfw_DeallocateApplicationPreferences(old);
}

void ROMlib_cfw_DeallocateApplicationPreferences(uint32 preferences)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {preferences}, result = 0;
    if (!cfp_reference("_CFDeallocateApplicationPreferences", 0xe14, arguments, 1, &reference, &result))
        return;
    uint32 allocator = ROMlib_cfw_PreferencesAllocator();
    uint32 cache = cfp_toc(&reference, 0x7d4), app = cfp_read(preferences + 8);
    if (cache) {
        uint32 lookup[] = {cache, app};
        if (cfc_public("CFDictionaryGetValue", lookup, 2) == preferences)
            cfp_call("CFDictionaryRemoveValue", lookup, 2, NULL);
    }
    invalidate(preferences);
    uint32 list = search_list(preferences);
    cfp_call("CFRelease", &list, 1, NULL);
    cfp_call("CFRelease", &app, 1, NULL);
    uint32 dispose[] = {allocator, preferences};
    cfp_call("CFAllocatorDeallocate", dispose, 2, NULL);
}

uint32 ROMlib_cfw_ApplicationPreferencesCopyRepresentationWithHint(uint32 preferences, uint32 hint)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {preferences, hint}, result = 0;
    if (!cfp_reference("_CFApplicationPreferencesCopyRepresentationWithHint", 0xee4, arguments, 2, &reference, &result))
        return result;
    result = representation(&reference, preferences);
    if (result && result != hint)
        cfp_call("CFRetain", &result, 1, NULL);
    return result;
}

uint32 ROMlib_cfw_ApplicationPreferencesCopyRepresentation(uint32 preferences)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {preferences}, result = 0;
    if (!cfp_reference("_CFApplicationPreferencesCopyRepresentation", 0xf68, arguments, 1, &reference, &result))
        return result;
    return ROMlib_cfw_ApplicationPreferencesCopyRepresentationWithHint(preferences, 0);
}

void ROMlib_cfw_ApplicationPreferencesSetSearchList(uint32 preferences, uint32 list)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {preferences, list}, result = 0;
    if (!cfp_reference("_CFApplicationPreferencesSetSearchList", 0xf8c, arguments, 2, &reference, &result))
        return;
    uint32 target = search_list(preferences);
    cfp_call("CFArrayRemoveAllValues", &target, 1, NULL);
    uint32 count = cfc_public("CFArrayGetCount", &list, 1);
    for (int32 i = 0; i < (int32)count; ++i) {
        uint32 lookup[] = {list, (uint32)i};
        uint32 append[] = {target, cfc_public("CFArrayGetValueAtIndex", lookup, 2)};
        if (!cfp_call("CFArrayAppendValue", append, 2, NULL))
            return;
    }
    invalidate(preferences);
}

void ROMlib_cfw_ApplicationPreferencesAddDomain(uint32 preferences, uint32 value, uint32 front)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {preferences, value, front}, result = 0;
    if (!cfp_reference("_CFApplicationPreferencesAddDomain", 0x146c, arguments, 3, &reference, &result))
        return;
    uint32 list = search_list(preferences);
    if (front & 255) {
        uint32 insert[] = {list, 0, value};
        cfp_call("CFArrayInsertValueAtIndex", insert, 3, NULL);
    }
    else {
        uint32 append[] = {list, value};
        cfp_call("CFArrayAppendValue", append, 2, NULL);
    }
    invalidate(preferences);
}

uint32 ROMlib_cfw_ApplicationPreferencesContainsDomain(uint32 preferences, uint32 value)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {preferences, value}, result = 0;
    if (!cfp_reference("_CFApplicationPreferencesContainsDomain", 0x14fc, arguments, 2, &reference, &result))
        return result;
    uint32 list = search_list(preferences), count = cfc_public("CFArrayGetCount", &list, 1);
    uint32 search[] = {list, 0, count, value};
    return cfc_public("CFArrayContainsValue", search, 4);
}

void ROMlib_cfw_ApplicationPreferencesRemoveDomain(uint32 preferences, uint32 value)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {preferences, value}, result = 0;
    if (!cfp_reference("_CFApplicationPreferencesRemoveDomain", 0x1590, arguments, 2, &reference, &result))
        return;
    uint32 list = search_list(preferences), count = cfc_public("CFArrayGetCount", &list, 1);
    uint32 start = 0;
    for (;;) {
        uint32 search[] = {list, start, count, value};
        uint32 index = cfc_public("CFArrayGetFirstIndexOfValue", search, 4);
        if (index == UINT32_MAX)
            break;
        uint32 remove[] = {list, index};
        if (!cfp_call("CFArrayRemoveValueAtIndex", remove, 2, NULL))
            return;
        count -= index - start + 1;
        start = index;
    }
    invalidate(preferences);
}

void ROMlib_cfw_ApplicationPreferencesRemoveSuitePreferences(uint32 preferences, uint32 suite)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {preferences, suite}, result = 0;
    if (!cfp_reference("_CFApplicationPreferencesRemoveSuitePreferences", 0x1310, arguments, 2, &reference, &result))
        return;
    uint32 users[] = {cfp_toc(&reference, 0x2190), cfp_toc(&reference, 0x2184)};
    uint32 hosts[] = {cfp_toc(&reference, 0x2180), cfp_toc(&reference, 0x218c)};
    for (unsigned u = 0; u < 2; ++u)
        for (unsigned h = 0; h < 2; ++h) {
            uint32 item = domain(&reference, suite, users[u], hosts[h]);
            if (item)
                ROMlib_cfw_ApplicationPreferencesRemoveDomain(preferences, item);
        }
}

void ROMlib_cfw_ApplicationPreferencesAddSuitePreferences(uint32 preferences, uint32 suite)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {preferences, suite}, result = 0;
    if (!cfp_reference("_CFApplicationPreferencesAddSuitePreferences", 0x1078, arguments, 2, &reference, &result))
        return;
    uint32 list = search_list(preferences), app = cfp_read(preferences + 8);
    uint32 users[] = {cfp_toc(&reference, 0x2190), cfp_toc(&reference, 0x2184)};
    uint32 hosts[] = {cfp_toc(&reference, 0x2180), cfp_toc(&reference, 0x218c)};
    uint32 any_app = cfp_toc(&reference, 0x217c);
    for (unsigned u = 0; u < 2; ++u) {
        uint32 count = cfc_public("CFArrayGetCount", &list, 1);
        uint32 anchor = domain(&reference, app, users[u], hosts[0]);
        uint32 search[] = {list, 0, count, anchor};
        uint32 index = anchor ? cfc_public("CFArrayGetFirstIndexOfValue", search, 4) : UINT32_MAX;
        if (u && index == UINT32_MAX) {
            for (unsigned h = 0; h < 2 && index == UINT32_MAX; ++h) {
                search[3] = domain(&reference, any_app, users[0], hosts[h]);
                if (search[3])
                    index = cfc_public("CFArrayGetFirstIndexOfValue", search, 4);
            }
            if (index == UINT32_MAX)
                index = cfc_public("CFArrayGetCount", &list, 1) - 1;
        }
        for (unsigned h = 0; h < 2; ++h) {
            uint32 item = domain(&reference, suite, users[u], hosts[h]);
            if (item) {
                uint32 insert[] = {list, index + 1, item};
                if (!cfp_call("CFArrayInsertValueAtIndex", insert, 3, NULL))
                    return;
                /* Both host variants use the same insertion index. */
            }
        }
    }
    invalidate(preferences);
}
