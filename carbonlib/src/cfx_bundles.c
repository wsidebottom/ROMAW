/*+===================================================================
  File:      cfx_bundles.c

  Summary:   Implements CoreFoundation bundles service entries for the PowerPC bridge.

  Classes:   None.

  Functions: ROMlib_cfx_CFBundleCopyBuiltInPlugInsURL, ROMlib_cfx_CFBundleCopyBundleRegionsArray,
             ROMlib_cfx_CFBundleCopyBundleURLForExecutableURL,
             ROMlib_cfx_CFBundleCopyExecutableURLInDirectory,
             ROMlib_cfx_CFBundleCopyInfoDictionaryInResourceFork, ROMlib_cfx_CFBundleCopyInfoPlistURL,
             ROMlib_cfx_CFBundleCopyLanguageSearchListInDirectory,
             ROMlib_cfx_CFBundleCopyPrivateFrameworksURL, ROMlib_cfx_CFBundleCopyResourceForkURL,
             ROMlib_cfx_CFBundleCopyResourceURLForLanguage,
             ROMlib_cfx_CFBundleCopyResourceURLsOfTypeForLanguage,
             ROMlib_cfx_CFBundleCopySharedFrameworksURL, ROMlib_cfx_CFBundleCopySharedSupportURL,
             ROMlib_cfx_CFBundleCreateIfLooksLikeBundle,
             ROMlib_cfx_CFBundleCreateWithExecutableURLIfLooksLikeBundle,
             ROMlib_cfx_CFBundleGetCFMFunctionPointerForName,
             ROMlib_cfx_CFBundleGetCFMFunctionPointersForNames, ROMlib_cfx_CFBundleGetHasChanged,
             ROMlib_cfx_CFBundleGetLanguageAndRegionCodes, ROMlib_cfx_CFBundleGetLanguageSearchList,
             ROMlib_cfx_CFBundleGetLocalInfoDictionary,
             ROMlib_cfx_CFBundleGetMainBundleIfLooksLikeBundle,
             ROMlib_cfx_CFBundleGetPackageInfoInDirectory, ROMlib_cfx_CFBundleGetValueForInfoKey,
             ROMlib_cfx_CFBundleOpenBundleResourceFork, ROMlib_cfx_CFBundleURLLooksLikeBundle

  Origin:    C reconstruction of the local CoreFoundationLib 1.4 disassembly; each entry verifies its reference offset.
             Public contracts use Universal Interfaces 3.4.2; private ABI notes are in cf-services-batch.json.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "cfx_internal.h"

int ROMlib_cfx_CFBundleCopyBuiltInPlugInsURL(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFBundleCopyBuiltInPlugInsURL", 0xfbf0, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    return CFX_RESULT(ROMlib_cfw_BundleCopyBuiltInPlugInsURL(CFX_ARG(0)));
}

int ROMlib_cfx_CFBundleCopyBundleRegionsArray(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFBundleCopyBundleRegionsArray", 0x14664, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    return CFX_RESULT(ROMlib_cfw_BundleCopyBundleLocalizations(CFX_ARG(0)));
}

int ROMlib_cfx_CFBundleCopyBundleURLForExecutableURL(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFBundleCopyBundleURLForExecutableURL", 0xcc80, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 absolute = CFW_CALL("CFURLCopyAbsoluteURL", CFX_ARG(0));
    if (!absolute)
        return CFX_RESULT(0);
    uint32 path = CFW_CALL("CFURLCopyFileSystemPath", absolute, 1), result = 0;
    if (path) {
        result = CFX_PRIVATE(0xca00, path);
        CFW_CALL("CFRelease", path);
    }
    CFW_CALL("CFRelease", absolute);
    return CFX_RESULT(result);
}

int ROMlib_cfx_CFBundleCopyExecutableURLInDirectory(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFBundleCopyExecutableURLInDirectory", 0xf0c8, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    return CFX_RESULT(CFX_PRIVATE(0xecb0, 0, 0, CFX_ARG(0), 0));
}

int ROMlib_cfx_CFBundleCopyInfoDictionaryInResourceFork(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFBundleCopyInfoDictionaryInResourceFork", 0x15324, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    return CFX_RESULT(CFX_PRIVATE(0x15194, 0, CFX_ARG(0)));
}

int ROMlib_cfx_CFBundleCopyInfoPlistURL(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFBundleCopyInfoPlistURL", 0xf9d4, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 dictionary = US_TO_SYN68K(ROMlib_carbon_cf_bundle_info(SYN68K_TO_US(CFX_ARG(0))));
    uint32 value = dictionary ? CFW_CALL("CFDictionaryGetValue", dictionary, cfp_toc(&reference, 0x2300)) : 0;
    return CFX_RESULT(value ? US_TO_SYN68K(ROMlib_carbon_cf_retain(SYN68K_TO_US(value))) : 0);
}

int ROMlib_cfx_CFBundleCopyLanguageSearchListInDirectory(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFBundleCopyLanguageSearchListInDirectory", 0x13550, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 allocator = CFX_ARG(0), url = CFX_ARG(1), version_out = CFX_ARG(2);
    uint32 callbacks = cfx_address(US_TO_SYN68K(reference.toc), 0x3234, 20);
    uint32 result = CFW_CALL("CFArrayCreateMutable", allocator, 0, callbacks);
    if (!result)
        return CFX_RESULT(0);
    _Alignas(16) Byte version = 0;
    uint32 information = CFX_PRIVATE(0x13a54, allocator, url, US_TO_SYN68K(&version));
    uint32 development = information ? CFW_CALL("CFDictionaryGetValue", information, cfp_toc(&reference, 0x2284)) : 0;
    if (development && (CFW_CALL("CFGetTypeID", development) != ROMlib_cfvalue_string_type() ||
                        !CFW_CALL("CFStringGetLength", development)))
        development = 0;
    CFX_PRIVATE(0x12e18, allocator, url, version, result, development);
    uint32 count = CFW_CALL("CFArrayGetCount", result);
    if (development && (int32)CFW_CALL("CFArrayGetFirstIndexOfValue", result, 0, count, development) < 0)
        CFW_CALL("CFArrayAppendValue", result, development);
    if (!CFW_CALL("CFArrayGetCount", result))
        CFW_CALL("CFArrayAppendValue", result, cfx_literal(&reference, 0x424, 0x1af));
    if (information)
        CFW_CALL("CFRelease", information);
    if (version_out) {
        Byte* output = ROMlib_carbon_guest_memory(version_out, 1, 1);
        if (!output)
            return 0;
        *output = version;
    }
    return CFX_RESULT(result);
}

int ROMlib_cfx_CFBundleCopyPrivateFrameworksURL(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFBundleCopyPrivateFrameworksURL", 0xfa1c, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    return CFX_RESULT(ROMlib_cfw_BundleCopyPrivateFrameworksURL(CFX_ARG(0)));
}

int ROMlib_cfx_CFBundleCopyResourceForkURL(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFBundleCopyResourceForkURL", 0xec8c, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    return CFX_RESULT(CFX_PRIVATE(0xebcc, CFX_ARG(0), 1));
}

int ROMlib_cfx_CFBundleCopyResourceURLForLanguage(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFBundleCopyResourceURLForLanguage", 0x11708, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    return CFX_RESULT(
        ROMlib_cfw_BundleCopyResourceURLForLocalization(CFX_ARG(0), CFX_ARG(1), CFX_ARG(2), CFX_ARG(3), CFX_ARG(4)));
}

int ROMlib_cfx_CFBundleCopyResourceURLsOfTypeForLanguage(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFBundleCopyResourceURLsOfTypeForLanguage", 0x11874, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    return CFX_RESULT(
        ROMlib_cfw_BundleCopyResourceURLsOfTypeForLocalization(CFX_ARG(0), CFX_ARG(1), CFX_ARG(2), CFX_ARG(3)));
}

int ROMlib_cfx_CFBundleCopySharedFrameworksURL(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFBundleCopySharedFrameworksURL", 0xfab8, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    return CFX_RESULT(ROMlib_cfw_BundleCopySharedFrameworksURL(CFX_ARG(0)));
}

int ROMlib_cfx_CFBundleCopySharedSupportURL(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFBundleCopySharedSupportURL", 0xfb54, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    return CFX_RESULT(ROMlib_cfw_BundleCopySharedSupportURL(CFX_ARG(0)));
}

int ROMlib_cfx_CFBundleCreateIfLooksLikeBundle(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFBundleCreateIfLooksLikeBundle", 0xcd00, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 result = US_TO_SYN68K(ROMlib_carbon_cf_bundle_create(CFX_ARG(0), SYN68K_TO_US_CHECK0(CFX_ARG(1))));
    if (result && !cfx_byte(cfx_address(result, 16, 1))) {
        uint32 information = US_TO_SYN68K(ROMlib_carbon_cf_bundle_info(SYN68K_TO_US(result)));
        if (!information || !CFW_CALL("CFDictionaryGetCount", information)) {
            Byte* version = ROMlib_carbon_guest_memory(cfx_address(result, 16, 1), 1, 1);
            if (!version)
                return 0;
            *version = 4;
        }
    }
    if (result) {
        unsigned version = cfx_byte(cfx_address(result, 16, 1));
        if (version == 3 || version == 4) {
            ROMlib_carbon_cf_release(SYN68K_TO_US(result));
            result = 0;
        }
    }
    return CFX_RESULT(result);
}

int ROMlib_cfx_CFBundleCreateWithExecutableURLIfLooksLikeBundle(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition =
        cfx_reference("_CFBundleCreateWithExecutableURLIfLooksLikeBundle", 0xcde0, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 allocator = CFX_ARG(0), executable = CFX_ARG(1);
    ROMlib_carbon_ppc_frame nested = *call_frame;
    nested.words[0] = executable;
    if (!ROMlib_cfx_CFBundleCopyBundleURLForExecutableURL(&nested))
        return 0;
    uint32 directory = nested.words[0];
    if (!directory)
        return CFX_RESULT(0);
    nested.words[0] = allocator;
    nested.words[1] = directory;
    if (!ROMlib_cfx_CFBundleCreateIfLooksLikeBundle(&nested)) {
        CFW_CALL("CFRelease", directory);
        return 0;
    }
    CFW_CALL("CFRelease", directory);
    uint32 bundle = nested.words[0];
    if (!bundle)
        return CFX_RESULT(0);
    uint32 resolved = ROMlib_cfw_BundleCopyExecutableURL(bundle);
    _Alignas(256) Byte requested_path[256], resolved_path[256];
    uint32 first = CFW_CALL("CFURLGetFileSystemRepresentation", executable, 1, US_TO_SYN68K(requested_path), 256);
    uint32 second =
        first ? CFW_CALL("CFURLGetFileSystemRepresentation", resolved, 1, US_TO_SYN68K(resolved_path), 256) : 0;
    if (!(first & 255) || !(second & 255) || strcmp((const char*)requested_path, (const char*)resolved_path)) {
        CFW_CALL("CFRelease", bundle);
        bundle = 0;
    }
    if (resolved)
        CFW_CALL("CFRelease", resolved);
    return CFX_RESULT(bundle);
}

int ROMlib_cfx_CFBundleGetCFMFunctionPointerForName(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFBundleGetCFMFunctionPointerForName", 0xf640, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 bundle = CFX_ARG(0), name = CFX_ARG(1);
    if (!cfx_byte(cfx_address(bundle, 36, 1)) && !(ROMlib_carbon_cf_bundle_load(SYN68K_TO_US(bundle)) & 255))
        return CFX_RESULT(0);
    if (cfp_read(cfx_address(bundle, 32, 4)) != 1)
        return CFX_RESULT(0);
    return CFX_RESULT(CFX_PRIVATE(0x10000, bundle, name, 2));
}

int ROMlib_cfx_CFBundleGetCFMFunctionPointersForNames(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFBundleGetCFMFunctionPointersForNames", 0xf740, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 bundle = CFX_ARG(0), names = CFX_ARG(1), output = CFX_ARG(2);
    if (!output)
        return 1;
    uint32 count = CFW_CALL("CFArrayGetCount", names);
    for (uint32 index = 0; (int32)index < (int32)count; ++index) {
        ROMlib_carbon_ppc_frame nested = *call_frame;
        nested.words[0] = bundle;
        nested.words[1] = CFW_CALL("CFArrayGetValueAtIndex", names, index);
        if (!ROMlib_cfx_CFBundleGetCFMFunctionPointerForName(&nested) ||
            !cfp_write(cfx_address(output, (uint64)index * 4, 4), nested.words[0]))
            return 0;
    }
    return 1;
}

int ROMlib_cfx_CFBundleGetHasChanged(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFBundleGetHasChanged", 0xe614, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 bundle = CFX_ARG(0), allocator = CFW_CALL("CFGetAllocator", bundle);
    _Alignas(16) Byte exists = 0;
    _Alignas(16) uint32 mode = 0, modification = 0;
    uint32 status = CFX_PRIVATE(0x18ce4, allocator, cfp_read(cfx_address(bundle, 8, 4)), US_TO_SYN68K(&exists),
                                US_TO_SYN68K(&mode), 0, US_TO_SYN68K(&modification), 0, 0);
    uint32 changed = status || !exists || (CL(mode) & 0xf000) != 0x4000;
    uint32 previous = cfp_read(cfx_address(bundle, 12, 4)), current = CL(modification);
    if (previous && (!current || !ROMlib_cfvalue_equal(previous, current)))
        changed = 1;
    if (current)
        CFW_CALL("CFRelease", current);
    return CFX_RESULT(changed);
}

int ROMlib_cfx_CFBundleGetLanguageAndRegionCodes(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFBundleGetLanguageAndRegionCodes", 0x12970, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 language_out = CFX_ARG(0), region_out = CFX_ARG(1), list = 0;
    uint32 main_bundle = ROMlib_cfw_BundleGetMainBundle();
    if (main_bundle) {
        ROMlib_carbon_ppc_frame nested = *call_frame;
        nested.words[0] = main_bundle;
        if (!ROMlib_cfx_CFBundleGetLanguageSearchList(&nested))
            return 0;
        if (nested.words[0])
            list = CFW_CALL("CFRetain", nested.words[0]);
    }
    if (!list)
        list = CFX_PRIVATE(0x127b0);
    uint32 language, region;
    if (list && (int32)CFW_CALL("CFArrayGetCount", list) > 0) {
        uint32 first = CFW_CALL("CFArrayGetValueAtIndex", list, 0);
        language = CFX_PRIVATE(0x11f7c, first);
        region = CFX_PRIVATE(0x123ac, first);
    }
    else {
        language = CFX_PRIVATE(0x5a034, UINT32_MAX, 0x1c);
        region = CFX_PRIVATE(0x59fa4, 0x28);
    }
    if (language == UINT32_MAX && region != UINT32_MAX)
        language = CFX_PRIVATE(0x12284, region);
    if (region == UINT32_MAX && language != UINT32_MAX)
        region = CFX_PRIVATE(0x12318, language);
    if (list)
        CFW_CALL("CFRelease", list);
    if ((language_out && !cfp_write(language_out, language)) || (region_out && !cfp_write(region_out, region)))
        return 0;
    return 1;
}

int ROMlib_cfx_CFBundleGetLanguageSearchList(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFBundleGetLanguageSearchList", 0xdb60, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 bundle = CFX_ARG(0), address = cfx_address(bundle, 28, 4), result = cfp_read(address);
    if (result)
        return CFX_RESULT(result);
    uint32 callbacks = cfx_address(US_TO_SYN68K(reference.toc), 0x3234, 20);
    result = CFW_CALL("CFArrayCreateMutable", 0, 0, callbacks);
    if (!result)
        return CFX_RESULT(0);
    uint32 development = CFW_CALL("CFBundleGetDevelopmentRegion", bundle);
    uint32 allocator = CFW_CALL("CFGetAllocator", bundle), url = cfp_read(cfx_address(bundle, 8, 4));
    uint32 version = cfx_byte(cfx_address(bundle, 16, 1));
    CFX_PRIVATE(0x12e18, allocator, url, version, result, development);
    if (!CFW_CALL("CFArrayGetCount", result)) {
        uint32 regions = ROMlib_cfw_BundleCopyBundleLocalizations(bundle);
        if (regions) {
            if ((int32)CFW_CALL("CFArrayGetCount", regions) > 0)
                CFX_PRIVATE(0x12e18, allocator, url, version, result, CFW_CALL("CFArrayGetValueAtIndex", regions, 0));
            CFW_CALL("CFRelease", regions);
        }
    }
    uint32 count = CFW_CALL("CFArrayGetCount", result);
    if (development && (int32)CFW_CALL("CFArrayGetFirstIndexOfValue", result, 0, count, development) < 0)
        CFW_CALL("CFArrayAppendValue", result, development);
    if (!CFW_CALL("CFArrayGetCount", result))
        CFW_CALL("CFArrayAppendValue", result, cfx_literal(&reference, 0x408, 0x336));
    if (!cfp_write(address, result))
        return 0;
    return CFX_RESULT(result);
}

int ROMlib_cfx_CFBundleGetLocalInfoDictionary(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFBundleGetLocalInfoDictionary", 0xdd84, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 bundle = CFX_ARG(0), address = cfx_address(bundle, 24, 4), result = cfp_read(address);
    if (!result) {
        uint32 name = cfx_literal(&reference, 0x408, 0x339), type = cfx_literal(&reference, 0x408, 0x343);
        uint32 resource = ROMlib_cfw_BundleCopyResourceURL(bundle, name, type, 0);
        if (resource) {
            _Alignas(16) uint32 data = 0, error = 0, parsing_error = 0;
            uint32 allocator = CFW_CALL("CFGetAllocator", bundle);
            if (CFW_CALL("CFURLCreateDataAndPropertiesFromResource", allocator, resource, US_TO_SYN68K(&data), 0, 0,
                         US_TO_SYN68K(&error))) {
                result =
                    CFW_CALL("CFPropertyListCreateFromXMLData", allocator, CL(data), 0, US_TO_SYN68K(&parsing_error));
                if (!cfp_write(address, result))
                    return 0;
                if (CL(parsing_error))
                    CFW_CALL("CFRelease", CL(parsing_error));
                if (CL(data))
                    CFW_CALL("CFRelease", CL(data));
            }
            CFW_CALL("CFRelease", resource);
        }
    }
    return CFX_RESULT(cfp_read(address));
}

int ROMlib_cfx_CFBundleGetMainBundleIfLooksLikeBundle(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFBundleGetMainBundleIfLooksLikeBundle", 0xcd9c, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 result = ROMlib_cfw_BundleGetMainBundle();
    if (result) {
        unsigned version = cfx_byte(cfx_address(result, 16, 1));
        if (version == 3 || version == 4)
            result = 0;
    }
    return CFX_RESULT(result);
}

int ROMlib_cfx_CFBundleGetPackageInfoInDirectory(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFBundleGetPackageInfoInDirectory", 0x13ed8, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 allocator = CFX_ARG(0), directory = CFX_ARG(1), type_out = CFX_ARG(2), creator_out = CFX_ARG(3);
    _Alignas(16) uint32 data = 0;
    const unsigned paths[] = {0x832, 0x843, 0x85b};
    for (unsigned index = 0; index < NELEM(paths) && !CL(data); ++index) {
        uint32 url =
            CFW_CALL("CFURLCreateWithString", allocator, cfx_literal(&reference, 0x424, paths[index]), directory);
        CFW_CALL("CFURLCreateDataAndPropertiesFromResource", allocator, url, US_TO_SYN68K(&data), 0, 0, 0);
        if (url)
            CFW_CALL("CFRelease", url);
    }
    uint32 bytes = CL(data);
    if (bytes) {
        if (CFW_CALL("CFDataGetLength", bytes) == 8) {
            uint32 source = CFW_CALL("CFDataGetBytePtr", bytes);
            if ((type_out && !cfp_move(type_out, source, 4)) ||
                (creator_out && !cfp_move(creator_out, cfx_address(source, 4, 4), 4)))
                return 0;
            CFW_CALL("CFRelease", bytes);
            return CFX_RESULT(1);
        }
        /* The original leaks a malformed-size PkgInfo object; release our owned
           read result while preserving its false result and unchanged outputs. */
        CFW_CALL("CFRelease", bytes);
        return CFX_RESULT(0);
    }
    ROMlib_carbon_ppc_frame nested = *call_frame;
    nested.words[0] = directory;
    if (!ROMlib_cfx_CFBundleURLLooksLikeBundle(&nested))
        return 0;
    if (!nested.words[0])
        return CFX_RESULT(0);
    if (creator_out && !cfp_write(creator_out, 0x3f3f3f3f))
        return 0;
    if (type_out) {
        uint32 absolute = CFW_CALL("CFURLCopyAbsoluteURL", directory);
        uint32 path = absolute ? CFW_CALL("CFURLCopyFileSystemPath", absolute, 1) : 0;
        if (absolute)
            CFW_CALL("CFRelease", absolute);
        uint32 type = 0x424e444c;
        if (path) {
            uint32 length = CFW_CALL("CFStringGetLength", path), start = 0;
            for (uint32 index = length; index > 1;) {
                --index;
                if (CFW_CALL("CFStringGetCharacterAtIndex", path, index - 1) == ':')
                    break;
                if (CFW_CALL("CFStringGetCharacterAtIndex", path, index) == '.') {
                    start = index;
                    break;
                }
            }
            if (start) {
                static const char* const extensions[] = {".app", ".debug", ".profile", ".service", ".framework"};
                for (unsigned extension = 0; extension < NELEM(extensions); ++extension) {
                    size_t size = strlen(extensions[extension]);
                    if (length - start != size && length - start != size + 1)
                        continue;
                    unsigned match = 1;
                    for (size_t character = 0; character < size; ++character)
                        if (CFW_CALL("CFStringGetCharacterAtIndex", path, start + character) !=
                            (unsigned char)extensions[extension][character]) {
                            match = 0;
                            break;
                        }
                    if (match && (length - start == size ||
                                  CFW_CALL("CFStringGetCharacterAtIndex", path, start + size) == '/')) {
                        type = extension == 4 ? 0x464d574b : 0x4150504c;
                        break;
                    }
                }
            }
            CFW_CALL("CFRelease", path);
        }
        if (!cfp_write(type_out, type))
            return 0;
    }
    return CFX_RESULT(1);
}

int ROMlib_cfx_CFBundleGetValueForInfoKey(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFBundleGetValueForInfoKey", 0xdeb8, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 bundle = CFX_ARG(0), key = CFX_ARG(1), result = 0;
    if (bundle && key) {
        ROMlib_carbon_ppc_frame nested = *call_frame;
        if (!ROMlib_cfx_CFBundleGetLocalInfoDictionary(&nested))
            return 0;
        if (nested.words[0])
            result = CFW_CALL("CFDictionaryGetValue", nested.words[0], key);
        if (!result) {
            uint32 dictionary = US_TO_SYN68K(ROMlib_carbon_cf_bundle_info(SYN68K_TO_US(bundle)));
            if (dictionary)
                result = CFW_CALL("CFDictionaryGetValue", dictionary, key);
        }
    }
    return CFX_RESULT(result);
}

int ROMlib_cfx_CFBundleOpenBundleResourceFork(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFBundleOpenBundleResourceFork", 0x14fc0, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    Byte* compatibility = ROMlib_carbon_guest_memory(cfx_address(US_TO_SYN68K(reference.toc), 0x839, 1), 1, 1);
    if (!compatibility)
        return 0;
    *compatibility = 1;
    return CFX_RESULT(ROMlib_cfw_BundleOpenBundleResourceMap(CFX_ARG(0)));
}

int ROMlib_cfx_CFBundleURLLooksLikeBundle(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("_CFBundleURLLooksLikeBundle", 0x136e8, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    ROMlib_carbon_ppc_frame nested = *call_frame;
    nested.words[0] = 0;
    nested.words[1] = CFX_ARG(0);
    if (!ROMlib_cfx_CFBundleCreateIfLooksLikeBundle(&nested))
        return 0;
    uint32 result = nested.words[0];
    if (result)
        ROMlib_carbon_cf_release(SYN68K_TO_US(result));
    return CFX_RESULT(result != 0);
}
