/*+===================================================================
  File:      cfw_bundles.c

  Summary:   Bundle directories, resource searches, package metadata and resource map control flow.

  Classes:   None.

  Functions: ROMlib_cfw_BundleCloseBundleResourceMap, ROMlib_cfw_BundleCopyAuxiliaryExecutableURL,
             ROMlib_cfw_BundleCopyBuiltInPlugInsURL, ROMlib_cfw_BundleCopyBundleLocalizations,
             ROMlib_cfw_BundleCopyExecutableURL, ROMlib_cfw_BundleCopyInfoDictionaryInDirectory,
             ROMlib_cfw_BundleCopyLocalizationForLocalizationInfo, ROMlib_cfw_BundleCopyLocalizedString,
             ROMlib_cfw_BundleCopyPreferredLocalizationsFromArray, ROMlib_cfw_BundleCopyPrivateFrameworksURL,
             ROMlib_cfw_BundleCopyResourceURL, ROMlib_cfw_BundleCopyResourceURLForLocalization,
             ROMlib_cfw_BundleCopyResourceURLInDirectory, ROMlib_cfw_BundleCopyResourceURLsOfType,
             ROMlib_cfw_BundleCopyResourceURLsOfTypeForLocalization, ROMlib_cfw_BundleCopyResourceURLsOfTypeInDirectory,
             ROMlib_cfw_BundleCopyResourcesDirectoryURL, ROMlib_cfw_BundleCopySharedFrameworksURL,
             ROMlib_cfw_BundleCopySharedSupportURL, ROMlib_cfw_BundleCopySupportFilesDirectoryURL,
             ROMlib_cfw_BundleCreateBundlesFromDirectory, ROMlib_cfw_BundleGetAllBundles,
             ROMlib_cfw_BundleGetBundleWithIdentifier, ROMlib_cfw_BundleGetLocalizationInfoForLocalization,
             ROMlib_cfw_BundleGetMainBundle, ROMlib_cfw_BundleGetPackageInfo,
             ROMlib_cfw_BundleGetPackageInfoInDirectory, ROMlib_cfw_BundleOpenBundleResourceFiles,
             ROMlib_cfw_BundleOpenBundleResourceMap

  Origin:    C reconstruction of the local CoreFoundationLib 1.4 bundle disassembly. Filesystem, executable discovery
             and localization workers remain PPC.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "cfw_internal.h"

static uint32 bundle_private(const ROMlib_carbon_reference* reference, uint32 offset, const uint32* arguments,
                             unsigned count)
{
    uint32 result = 0;
    cfp_private(reference, offset, arguments, count, &result);
    return result;
}

static unsigned bundle_byte(uint32 address)
{
    const unsigned char* memory = ROMlib_carbon_guest_memory(address, 1, 0);
    return memory ? *memory : 0;
}

static uint32 literal(const ROMlib_carbon_reference* reference, unsigned table, unsigned offset)
{
    return CFW_CALL("__CFStringMakeConstantString", cfp_toc(reference, table) + offset);
}

static uint32 path_url(const ROMlib_carbon_reference* reference, uint32 allocator, uint32 base, uint32 path)
{
    uint32 arguments[] = {allocator, path, base};
    return bundle_private(reference, 0x7ac0, arguments, 3);
}

static uint32 directory_url(const ROMlib_carbon_reference* reference, uint32 bundle, unsigned path)
{
    unsigned version = bundle_byte(bundle + 16);
    if (version != 1 && version != 2)
        return 0;
    return path_url(reference, CFW_CALL("CFGetAllocator", bundle), cfp_read(bundle + 8),
                    literal(reference, 0x408, path));
}

static uint32 one_type(const ROMlib_carbon_reference* reference, uint32 allocator, uint32 type)
{
    if (!type)
        return 0;
    uint32 array = CFW_CALL("CFArrayCreateMutable", allocator, 1, US_TO_SYN68K(reference->toc) + 0x3234);
    CFW_CALL("CFArrayAppendValue", array, type);
    return array;
}

static uint32 first_resource(uint32 resources)
{
    uint32 result = 0;
    if (resources) {
        if ((int32)CFW_CALL("CFArrayGetCount", resources) > 0) {
            result = CFW_CALL("CFArrayGetValueAtIndex", resources, 0);
            cfp_call("CFRetain", &result, 1, NULL);
        }
        cfp_call("CFRelease", &resources, 1, NULL);
    }
    return result;
}

static uint32 bundle_resources(const ROMlib_carbon_reference* reference, uint32 bundle, uint32 name, uint32 type,
                               uint32 subdirectory, uint32 localization, int localized, int single)
{
    uint32 allocator = CFW_CALL("CFGetAllocator", bundle);
    uint32 localizations =
        localized ? one_type(reference, allocator, localization) : bundle_private(reference, 0xdb60, &bundle, 1);
    /* The localized entry creates an empty list even when no localization is supplied. */
    if (localized && !localizations)
        localizations = CFW_CALL("CFArrayCreateMutable", allocator, 1, US_TO_SYN68K(reference->toc) + 0x3234);
    uint32 types = one_type(reference, allocator, type);
    uint32 search[] = {bundle, 0,     subdirectory,         localizations,
                       name,   types, single ? 1 : 1000000, bundle_byte(bundle + 16)};
    uint32 result = bundle_private(reference, 0x11174, search, 8);
    if (types)
        cfp_call("CFRelease", &types, 1, NULL);
    if (localized)
        cfp_call("CFRelease", &localizations, 1, NULL);
    return single ? first_resource(result) : result;
}

static uint32 directory_resources(const ROMlib_carbon_reference* reference, uint32 url, uint32 name, uint32 type,
                                  uint32 subdirectory, int single)
{
    _Alignas(256) unsigned char path[256];
    uint32 filesystem[] = {url, 1, US_TO_SYN68K(path), sizeof(path)};
    if (!(bundle_private(reference, 0xa78c, filesystem, 4) & 255))
        return 0;
    const void* terminator = memchr(path, 0, sizeof(path));
    if (!terminator) {
        cfp_failure("Bundle filesystem representation has no terminator");
        return 0;
    }
    uint32 create[] = {0, US_TO_SYN68K(path), (uint32)((const unsigned char*)terminator - path), 1};
    uint32 directory = bundle_private(reference, 0xa880, create, 4);
    if (!directory) {
        directory = url;
        cfp_call("CFRetain", &directory, 1, NULL);
    }
    uint32 result = 0;
    if (bundle_private(reference, 0xf8ec, &directory, 1) & 255) {
        _Alignas(16) unsigned char version = 0;
        uint32 languages_args[] = {0, directory, US_TO_SYN68K(&version)};
        uint32 languages = bundle_private(reference, 0x13550, languages_args, 3);
        uint32 types = one_type(reference, 0, type);
        uint32 search[] = {0, directory, subdirectory, languages, name, types, single ? 1 : 1000000, version};
        result = bundle_private(reference, 0x11174, search, 8);
        if (types)
            cfp_call("CFRelease", &types, 1, NULL);
        cfp_call("CFRelease", &languages, 1, NULL);
        if (single)
            result = first_resource(result);
    }
    cfp_call("CFRelease", &directory, 1, NULL);
    return result;
}

uint32 ROMlib_cfw_BundleGetMainBundle(void)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (!cfp_reference("CFBundleGetMainBundle", 0xd0b8, NULL, 0, &reference, &result))
        return result;
    return bundle_private(&reference, 0xcebc, NULL, 0);
}

uint32 ROMlib_cfw_BundleGetAllBundles(void)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (!cfp_reference("CFBundleGetAllBundles", 0xf990, NULL, 0, &reference, &result))
        return result;
    bundle_private(&reference, 0xf970, NULL, 0);
    return cfp_toc(&reference, 0x81c);
}

uint32 ROMlib_cfw_BundleGetBundleWithIdentifier(uint32 identifier)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {identifier}, result = 0;
    if (!cfp_reference("CFBundleGetBundleWithIdentifier", 0xd100, arguments, 1, &reference, &result))
        return result;
    if (!identifier)
        return 0;
    bundle_private(&reference, 0xcebc, NULL, 0);
    uint32 cache = cfp_toc(&reference, 0x828);
    if (cache)
        result = CFW_CALL("CFDictionaryGetValue", cache, identifier);
    if (!result) {
        bundle_private(&reference, 0xf970, NULL, 0);
        cache = cfp_toc(&reference, 0x828);
        if (cache)
            result = CFW_CALL("CFDictionaryGetValue", cache, identifier);
    }
    return result;
}

uint32 ROMlib_cfw_BundleCopyInfoDictionaryInDirectory(uint32 url)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {url}, result = 0;
    if (!cfp_reference("CFBundleCopyInfoDictionaryInDirectory", 0xdcfc, arguments, 1, &reference, &result))
        return result;
    uint32 copy[] = {0, url, 0};
    return bundle_private(&reference, 0x13a54, copy, 3);
}

uint32 ROMlib_cfw_BundleCreateBundlesFromDirectory(uint32 allocator, uint32 directory, uint32 type)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, directory, type}, result = 0;
    if (!cfp_reference("CFBundleCreateBundlesFromDirectory", 0xda54, arguments, 3, &reference, &result))
        return result;
    result = CFW_CALL("CFArrayCreateMutable", allocator, 0, US_TO_SYN68K(reference.toc) + 0x3234);
    uint32 enumerate[] = {allocator, 0, 0, directory, type};
    uint32 files = bundle_private(&reference, 0x1897c, enumerate, 5);
    if (files) {
        uint32 count = CFW_CALL("CFArrayGetCount", files);
        for (int32 i = 0; i < (int32)count; ++i) {
            uint32 url = CFW_CALL("CFArrayGetValueAtIndex", files, (uint32)i);
            uint32 bundle = CFW_CALL("CFBundleCreate", allocator, url);
            if (bundle)
                CFW_CALL("CFArrayAppendValue", result, bundle);
        }
        cfp_call("CFRelease", &files, 1, NULL);
    }
    return result;
}

uint32 ROMlib_cfw_BundleCopySupportFilesDirectoryURL(uint32 bundle)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {bundle}, result = 0;
    if (!cfp_reference("CFBundleCopySupportFilesDirectoryURL", 0xe7f8, arguments, 1, &reference, &result))
        return result;
    uint32 copy[] = {CFW_CALL("CFGetAllocator", bundle), cfp_read(bundle + 8), bundle_byte(bundle + 16)};
    return bundle_private(&reference, 0xe750, copy, 3);
}

uint32 ROMlib_cfw_BundleCopyResourcesDirectoryURL(uint32 bundle)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {bundle}, result = 0;
    if (!cfp_reference("CFBundleCopyResourcesDirectoryURL", 0xe904, arguments, 1, &reference, &result))
        return result;
    uint32 copy[] = {CFW_CALL("CFGetAllocator", bundle), cfp_read(bundle + 8), bundle_byte(bundle + 16)};
    return bundle_private(&reference, 0xe834, copy, 3);
}

uint32 ROMlib_cfw_BundleCopyExecutableURL(uint32 bundle)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {bundle}, result = 0;
    if (!cfp_reference("CFBundleCopyExecutableURL", 0xf0f8, arguments, 1, &reference, &result))
        return result;
    uint32 copy[] = {CFW_CALL("CFGetAllocator", bundle), bundle, cfp_read(bundle + 8), 0};
    return bundle_private(&reference, 0xecb0, copy, 4);
}

uint32 ROMlib_cfw_BundleCopyAuxiliaryExecutableURL(uint32 bundle, uint32 name)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {bundle, name}, result = 0;
    if (!cfp_reference("CFBundleCopyAuxiliaryExecutableURL", 0xf138, arguments, 2, &reference, &result))
        return result;
    uint32 copy[] = {CFW_CALL("CFGetAllocator", bundle), bundle, cfp_read(bundle + 8), name};
    return bundle_private(&reference, 0xecb0, copy, 4);
}

uint32 ROMlib_cfw_BundleCopyPrivateFrameworksURL(uint32 bundle)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {bundle}, result = 0;
    if (!cfp_reference("CFBundleCopyPrivateFrameworksURL", 0xfa40, arguments, 1, &reference, &result))
        return result;
    return directory_url(&reference, bundle, 0x469);
}

uint32 ROMlib_cfw_BundleCopySharedFrameworksURL(uint32 bundle)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {bundle}, result = 0;
    if (!cfp_reference("CFBundleCopySharedFrameworksURL", 0xfadc, arguments, 1, &reference, &result))
        return result;
    return directory_url(&reference, bundle, 0x47e);
}

uint32 ROMlib_cfw_BundleCopySharedSupportURL(uint32 bundle)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {bundle}, result = 0;
    if (!cfp_reference("CFBundleCopySharedSupportURL", 0xfb78, arguments, 1, &reference, &result))
        return result;
    return directory_url(&reference, bundle, 0x499);
}

uint32 ROMlib_cfw_BundleCopyBuiltInPlugInsURL(uint32 bundle)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {bundle}, result = 0;
    if (!cfp_reference("CFBundleCopyBuiltInPlugInsURL", 0xfc14, arguments, 1, &reference, &result))
        return result;
    result = directory_url(&reference, bundle, 0x4b1);
    unsigned version = bundle_byte(bundle + 16);
    if (version != 1 && version != 2)
        return result;
    uint32 allocator = CFW_CALL("CFGetAllocator", bundle);
    uint32 exists[] = {allocator, result};
    if (result && (bundle_private(&reference, 0xe6e4, exists, 2) & 255))
        return result;
    uint32 alternate = directory_url(&reference, bundle, 0x4c3);
    exists[1] = alternate;
    if (alternate && (bundle_private(&reference, 0xe6e4, exists, 2) & 255)) {
        if (result)
            cfp_call("CFRelease", &result, 1, NULL);
        return alternate;
    }
    if (alternate)
        cfp_call("CFRelease", &alternate, 1, NULL);
    return result;
}

uint32 ROMlib_cfw_BundleCopyResourceURL(uint32 bundle, uint32 name, uint32 type, uint32 subdirectory)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {bundle, name, type, subdirectory}, result = 0;
    if (!cfp_reference("CFBundleCopyResourceURL", 0x11538, arguments, 4, &reference, &result))
        return result;
    return bundle_resources(&reference, bundle, name, type, subdirectory, 0, 0, 1);
}

uint32 ROMlib_cfw_BundleCopyResourceURLsOfType(uint32 bundle, uint32 type, uint32 subdirectory)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {bundle, type, subdirectory}, result = 0;
    if (!cfp_reference("CFBundleCopyResourceURLsOfType", 0x11644, arguments, 3, &reference, &result))
        return result;
    return bundle_resources(&reference, bundle, 0, type, subdirectory, 0, 0, 0);
}

uint32 ROMlib_cfw_BundleCopyResourceURLForLocalization(uint32 bundle, uint32 name, uint32 type, uint32 subdirectory,
                                                       uint32 localization)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {bundle, name, type, subdirectory, localization}, result = 0;
    if (!cfp_reference("CFBundleCopyResourceURLForLocalization", 0x1172c, arguments, 5, &reference, &result))
        return result;
    return bundle_resources(&reference, bundle, name, type, subdirectory, localization, 1, 1);
}

uint32 ROMlib_cfw_BundleCopyResourceURLsOfTypeForLocalization(uint32 bundle, uint32 type, uint32 subdirectory,
                                                              uint32 localization)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {bundle, type, subdirectory, localization}, result = 0;
    if (!cfp_reference("CFBundleCopyResourceURLsOfTypeForLocalization", 0x11898, arguments, 4, &reference, &result))
        return result;
    return bundle_resources(&reference, bundle, 0, type, subdirectory, localization, 1, 0);
}

uint32 ROMlib_cfw_BundleCopyResourceURLInDirectory(uint32 url, uint32 name, uint32 type, uint32 subdirectory)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {url, name, type, subdirectory}, result = 0;
    if (!cfp_reference("CFBundleCopyResourceURLInDirectory", 0x11c80, arguments, 4, &reference, &result))
        return result;
    return directory_resources(&reference, url, name, type, subdirectory, 1);
}

uint32 ROMlib_cfw_BundleCopyResourceURLsOfTypeInDirectory(uint32 url, uint32 type, uint32 subdirectory)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {url, type, subdirectory}, result = 0;
    if (!cfp_reference("CFBundleCopyResourceURLsOfTypeInDirectory", 0x11e20, arguments, 3, &reference, &result))
        return result;
    return directory_resources(&reference, url, 0, type, subdirectory, 0);
}

uint32 ROMlib_cfw_BundleGetPackageInfoInDirectory(uint32 url, uint32 type, uint32 creator)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {url, type, creator}, result = 0;
    if (!cfp_reference("CFBundleGetPackageInfoInDirectory", 0x1457c, arguments, 3, &reference, &result))
        return result;
    uint32 query[] = {0, url, type, creator};
    return bundle_private(&reference, 0x13ed8, query, 4);
}

void ROMlib_cfw_BundleGetPackageInfo(uint32 bundle, uint32 type, uint32 creator)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {bundle, type, creator}, result = 0;
    if (!cfp_reference("CFBundleGetPackageInfo", 0x144c8, arguments, 3, &reference, &result))
        return;
    uint32 url = CFW_CALL("CFBundleCopyBundleURL", bundle);
    uint32 query[] = {CFW_CALL("CFGetAllocator", bundle), url, type, creator};
    if (!(bundle_private(&reference, 0x13ed8, query, 4) & 255)) {
        if (type)
            cfp_write(type, 0x424e444c);
        if (creator)
            cfp_write(creator, 0x3f3f3f3f);
    }
    if (url)
        cfp_call("CFRelease", &url, 1, NULL);
}

uint32 ROMlib_cfw_BundleOpenBundleResourceFiles(uint32 bundle, uint32 reference_number, uint32 localized_number)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {bundle, reference_number, localized_number}, result = 0;
    if (!cfp_reference("CFBundleOpenBundleResourceFiles", 0x14dd0, arguments, 3, &reference, &result))
        return result;
    uint32 dictionary = CFW_CALL("CFBundleGetInfoDictionary", bundle);
    uint32 setting = dictionary ? CFW_CALL("CFDictionaryGetValue", dictionary, cfp_toc(&reference, 0x2308)) : 0;
    uint32 map = 0;
    if (setting) {
        uint32 type = CFW_CALL("CFGetTypeID", setting);
        if (type == cfc_public("CFBooleanGetTypeID", NULL, 0))
            map = CFW_CALL("CFBooleanGetValue", setting);
        else if (type == cfc_public("CFStringGetTypeID", NULL, 0)) {
            uint32 yes = literal(&reference, 0x424, 0x897), true_string = literal(&reference, 0x424, 0x89c);
            map =
                !CFW_CALL("CFStringCompare", setting, yes, 1) || !CFW_CALL("CFStringCompare", setting, true_string, 1);
        }
        else if (type == cfc_public("CFNumberGetTypeID", NULL, 0)) {
            _Alignas(16) uint32 integer = 0;
            if (CFW_CALL("CFNumberGetValue", setting, 3, US_TO_SYN68K(&integer)) & 255)
                map = CL(integer) != 0;
        }
    }
    _Alignas(16) uint16 first = CW((uint16)0xffff), second = CW((uint16)0xffff);
    uint32 open[] = {bundle, 1, map, US_TO_SYN68K(&first)};
    uint32 first_error = bundle_private(&reference, 0x14b98, open, 4);
    uint32 local[] = {bundle, map, US_TO_SYN68K(&second)};
    uint32 second_error = bundle_private(&reference, 0x14ce0, local, 3);
    if (reference_number && !cfp_move(reference_number, US_TO_SYN68K(&first), 2))
        return 0;
    if (localized_number && !cfp_move(localized_number, US_TO_SYN68K(&second), 2))
        return 0;
    if (CW(first) == 0xffff && CW(second) == 0xffff)
        return first_error ? first_error : second_error ? second_error : (uint32)-193;
    return 0;
}

uint32 ROMlib_cfw_BundleOpenBundleResourceMap(uint32 bundle)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {bundle}, result = 0;
    if (!cfp_reference("CFBundleOpenBundleResourceMap", 0x14ff8, arguments, 1, &reference, &result))
        return result;
    _Alignas(16) uint16 first = CW((uint16)0xffff), second = CW((uint16)0xffff);
    ROMlib_cfw_BundleOpenBundleResourceFiles(bundle, US_TO_SYN68K(&first), US_TO_SYN68K(&second));
    uint32 local = (int32)(int16)CW(second), base = (int32)(int16)CW(first);
    if (local != UINT32_MAX && base != UINT32_MAX) {
        uint32 insert[] = {local, base};
        bundle_private(&reference, 0x103b8, insert, 2);
    }
    return local != UINT32_MAX && base == UINT32_MAX ? local : base;
}

void ROMlib_cfw_BundleCloseBundleResourceMap(uint32 bundle, uint32 number)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {bundle, number}, result = 0;
    if (!cfp_reference("CFBundleCloseBundleResourceMap", 0x15074, arguments, 2, &reference, &result))
        return;
    number = (int32)(int16)number;
    bundle_private(&reference, 0x14b38, &number, 1);
}

uint32 ROMlib_cfw_BundleGetLocalizationInfoForLocalization(uint32 localization, uint32 language_out, uint32 region_out,
                                                           uint32 script_out, uint32 encoding_out)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {localization, language_out, region_out, script_out, encoding_out}, result = 0;
    if (!cfp_reference("CFBundleGetLocalizationInfoForLocalization", 0x12508, arguments, 5, &reference, &result))
        return result;
    uint32 language = UINT32_MAX, region = UINT32_MAX;
    if (localization) {
        language = bundle_private(&reference, 0x11f7c, &localization, 1);
        region = bundle_private(&reference, 0x123ac, &localization, 1);
    }
    else {
        _Alignas(16) uint32 lang = CL(UINT32_MAX), reg = CL(UINT32_MAX);
        uint32 lookup[] = {US_TO_SYN68K(&lang), US_TO_SYN68K(&reg)};
        bundle_private(&reference, 0x12970, lookup, 2);
        language = CL(lang);
        region = CL(reg);
    }
    if (((int32)language < 0 || language > 151) && region != UINT32_MAX)
        language = bundle_private(&reference, 0x12284, &region, 1);
    if (region == UINT32_MAX && language != UINT32_MAX)
        region = bundle_private(&reference, 0x12318, &language, 1);
    uint32 script = 0, encoding = 0;
    if ((int32)language >= 0 && language < 151) {
        script = cfp_toc(&reference, 0x29a4 + 4 * language);
        encoding = cfp_toc(&reference, 0x2c00 + 4 * language);
    }
    if (language_out)
        cfp_write(language_out, language);
    if (region_out)
        cfp_write(region_out, region);
    if (script_out)
        cfp_write(script_out, script);
    if (encoding_out)
        cfp_write(encoding_out, encoding);
    return language != UINT32_MAX || region != UINT32_MAX;
}

uint32 ROMlib_cfw_BundleCopyLocalizationForLocalizationInfo(uint32 language, uint32 region, uint32 script,
                                                            uint32 encoding)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {language, region, script, encoding}, result = 0;
    if (!cfp_reference("CFBundleCopyLocalizationForLocalizationInfo", 0x12668, arguments, 4, &reference, &result))
        return result;
    result = bundle_private(&reference, 0x12494, &region, 1);
    if (!result)
        result = bundle_private(&reference, 0x12090, &language, 1);
    if (result)
        return result;
    uint32 both = UINT32_MAX, script_match = UINT32_MAX, encoding_match = UINT32_MAX;
    for (uint32 i = 0; i < 151; ++i) {
        uint32 item_script = cfp_toc(&reference, 0x29a4 + 4 * i);
        uint32 item_encoding = cfp_toc(&reference, 0x2c00 + 4 * i);
        if (both == UINT32_MAX && item_script == script && item_encoding == encoding)
            both = i;
        if (script_match == UINT32_MAX && item_script == script)
            script_match = i;
        if (encoding_match == UINT32_MAX && item_encoding == encoding)
            encoding_match = i;
    }
    result = bundle_private(&reference, 0x12090, &both, 1);
    if (!result)
        result = bundle_private(&reference, 0x12090, &encoding_match, 1);
    if (!result)
        result = bundle_private(&reference, 0x12090, &script_match, 1);
    return result;
}

uint32 ROMlib_cfw_BundleCopyBundleLocalizations(uint32 bundle)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {bundle}, result = 0;
    if (!cfp_reference("CFBundleCopyBundleLocalizations", 0x14688, arguments, 1, &reference, &result))
        return result;
    uint32 resources = ROMlib_cfw_BundleCopyResourcesDirectoryURL(bundle);
    uint32 allocator = CFW_CALL("CFGetAllocator", bundle);
    uint32 enumerate[] = {allocator, 0, 0, resources, literal(&reference, 0x424, 0xcb)};
    uint32 files = bundle_private(&reference, 0x1897c, enumerate, 5);
    if (files) {
        uint32 count = CFW_CALL("CFArrayGetCount", files);
        if ((int32)count > 0)
            result = CFW_CALL("CFArrayCreateMutable", allocator, 0, US_TO_SYN68K(reference.toc) + 0x3234);
        for (int32 i = 0; i < (int32)count; ++i) {
            uint32 url = CFW_CALL("CFArrayGetValueAtIndex", files, (uint32)i);
            uint32 absolute = CFW_CALL("CFURLCopyAbsoluteURL", url);
            uint32 text = CFW_CALL("CFURLCopyFileSystemPath", absolute, 1);
            cfp_call("CFRelease", &absolute, 1, NULL);
            uint32 length = CFW_CALL("CFStringGetLength", text), bytes;
            if (!cfc_product(length, 2, &bytes))
                break;
            uint32 characters = CFW_CALL("CFAllocatorAllocate", allocator, bytes, 0);
            if (bytes && !characters)
                break;
            CFW_CALL("CFStringGetCharacters", text, 0, length, characters);
            uint32 last_component[] = {characters, length};
            uint32 start = bundle_private(&reference, 0x19324, last_component, 2);
            uint32 extension_length[] = {characters + 2 * start, length - start};
            uint32 trimmed = bundle_private(&reference, 0x1942c, extension_length, 2);
            uint32 app = CFW_CALL("CFStringCreateWithCharacters", allocator, characters + 2 * start, trimmed);
            CFW_CALL("CFArrayAppendValue", result, app);
            cfp_call("CFRelease", &app, 1, NULL);
            cfp_call("CFRelease", &text, 1, NULL);
            CFW_CALL("CFAllocatorDeallocate", allocator, characters);
        }
        cfp_call("CFRelease", &files, 1, NULL);
    }
    cfp_call("CFRelease", &resources, 1, NULL);
    return result;
}

uint32 ROMlib_cfw_BundleCopyLocalizedString(uint32 bundle, uint32 key, uint32 value, uint32 table)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {bundle, key, value, table}, result = 0;
    if (!cfp_reference("CFBundleCopyLocalizedString", 0x11998, arguments, 4, &reference, &result))
        return result;
    uint32 empty = literal(&reference, 0x424, 0xef);
    if (!key) {
        result = value ? value : empty;
        cfp_call("CFRetain", &result, 1, NULL);
        return result;
    }
    if (!table || (CFW_CALL("CFEqual", table, empty) & 255))
        table = literal(&reference, 0x424, 0xf0);
    uint32 cache = cfp_read(bundle + 0x48);
    uint32 dictionary = cache ? CFW_CALL("CFDictionaryGetValue", cache, table) : 0;
    if (!dictionary) {
        uint32 url = ROMlib_cfw_BundleCopyResourceURL(bundle, table, literal(&reference, 0x424, 0xfc), 0);
        _Alignas(16) uint32 data = 0, error = 0, parse_error = 0;
        uint32 allocator = CFW_CALL("CFGetAllocator", bundle);
        if (url && (CFW_CALL("CFURLCreateDataAndPropertiesFromResource", allocator, url, US_TO_SYN68K(&data), 0, 0,
                             US_TO_SYN68K(&error)) &
                    255)) {
            dictionary =
                CFW_CALL("CFPropertyListCreateFromXMLData", allocator, CL(data), 0, US_TO_SYN68K(&parse_error));
            uint32 message = CL(parse_error), bytes = CL(data);
            if (message)
                cfp_call("CFRelease", &message, 1, NULL);
            cfp_call("CFRelease", &bytes, 1, NULL);
        }
        if (url)
            cfp_call("CFRelease", &url, 1, NULL);
        if (!dictionary)
            dictionary = CFW_CALL("CFDictionaryCreate", allocator, 0, 0, 0, US_TO_SYN68K(reference.toc) + 0x33c0,
                                  US_TO_SYN68K(reference.toc) + 0x33f0);
        cache = cfp_read(bundle + 0x48);
        if (!cache) {
            cache = CFW_CALL("CFDictionaryCreateMutable", allocator, 0, US_TO_SYN68K(reference.toc) + 0x33d8,
                             US_TO_SYN68K(reference.toc) + 0x33f0);
            cfp_write(bundle + 0x48, cache);
        }
        CFW_CALL("CFDictionarySetValue", cache, table, dictionary);
        cfp_call("CFRelease", &dictionary, 1, NULL);
    }
    result = CFW_CALL("CFDictionaryGetValue", dictionary, key);
    if (!result)
        result = !value || (CFW_CALL("CFEqual", value, empty) & 255) ? key : value;
    cfp_call("CFRetain", &result, 1, NULL);
    return result;
}

uint32 ROMlib_cfw_BundleCopyPreferredLocalizationsFromArray(uint32 localizations)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {localizations}, result = 0;
    if (!cfp_reference("CFBundleCopyPreferredLocalizationsFromArray", 0x132d4, arguments, 1, &reference, &result))
        return result;
    result = CFW_CALL("CFArrayCreateMutable", 0, 0, US_TO_SYN68K(reference.toc) + 0x3234);
    uint32 found = 0, main = ROMlib_cfw_BundleGetMainBundle();
    if (main) {
        uint32 languages = bundle_private(&reference, 0xdb60, &main, 1);
        if (languages && (int32)CFW_CALL("CFArrayGetCount", languages) > 0) {
            uint32 choose[] = {localizations, CFW_CALL("CFArrayGetValueAtIndex", languages, 0), result};
            found = bundle_private(&reference, 0x1315c, choose, 3);
        }
    }
    if (!(found & 255)) {
        uint32 preferred = bundle_private(&reference, 0x127b0, NULL, 0);
        if (!preferred || !CFW_CALL("CFArrayGetCount", preferred)) {
            uint32 script_selector = 40, region_selectors[] = {UINT32_MAX, 28};
            uint32 script = bundle_private(&reference, 0x59fa4, &script_selector, 1);
            uint32 region = bundle_private(&reference, 0x5a034, region_selectors, 2);
            uint32 script_name = bundle_private(&reference, 0x12494, &script, 1);
            uint32 region_name = bundle_private(&reference, 0x12090, &region, 1);
            if (preferred)
                cfp_call("CFRelease", &preferred, 1, NULL);
            preferred = CFW_CALL("CFArrayCreateMutable", 0, 0, US_TO_SYN68K(reference.toc) + 0x3234);
            if (script_name) {
                CFW_CALL("CFArrayAppendValue", preferred, script_name);
                cfp_call("CFRelease", &script_name, 1, NULL);
            }
            if (region_name) {
                CFW_CALL("CFArrayAppendValue", preferred, region_name);
                cfp_call("CFRelease", &region_name, 1, NULL);
            }
        }
        uint32 count = preferred ? CFW_CALL("CFArrayGetCount", preferred) : 0;
        for (int32 i = 0; !(found & 255) && i < (int32)count; ++i) {
            uint32 choose[] = {localizations, CFW_CALL("CFArrayGetValueAtIndex", preferred, (uint32)i), result};
            found = bundle_private(&reference, 0x1315c, choose, 3);
        }
        if (!(found & 255)) {
            uint32 choose[] = {localizations, literal(&reference, 0x424, 0x104), result};
            found = bundle_private(&reference, 0x1315c, choose, 3);
        }
        /* Preserve the reference's result-count condition before consulting input[0]. */
        if (!(found & 255) && (int32)CFW_CALL("CFArrayGetCount", result) > 0) {
            uint32 choose[] = {localizations, CFW_CALL("CFArrayGetValueAtIndex", localizations, 0), result};
            bundle_private(&reference, 0x1315c, choose, 3);
        }
        if (preferred)
            cfp_call("CFRelease", &preferred, 1, NULL);
    }
    if (!CFW_CALL("CFArrayGetCount", result))
        CFW_CALL("CFArrayAppendValue", result, literal(&reference, 0x424, 0x1af));
    return result;
}
