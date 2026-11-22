/*+===================================================================
  File:      bundles.c

  Summary:   CarbonLib 1.4 bundle state, metadata and executable-load orchestration.

  Classes:   None.

  Functions: ROMlib_carbon_cf_bundle_create, ROMlib_carbon_cf_bundle_data_pointers, ROMlib_carbon_cf_bundle_development,
             ROMlib_carbon_cf_bundle_functions, ROMlib_carbon_cf_bundle_identifier, ROMlib_carbon_cf_bundle_info,
             ROMlib_carbon_cf_bundle_info_value, ROMlib_carbon_cf_bundle_load, ROMlib_carbon_cf_bundle_loaded,
             ROMlib_carbon_cf_bundle_local_info, ROMlib_carbon_cf_bundle_plugin, ROMlib_carbon_cf_bundle_type_id,
             ROMlib_carbon_cf_bundle_unload, ROMlib_carbon_cf_bundle_url, ROMlib_carbon_cf_bundle_version

  Origin:    Existing repository source in packages/carbonlib/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* CarbonLib 1.4 bundle state, metadata and executable-load orchestration. */
#include "internal.h"

static uint32 bundle_word(const void* memory, unsigned offset)
{
    uint32 value;
    memcpy(&value, (const Byte*)memory + offset, 4);
    return CL(value);
}

static void bundle_store(void* memory, unsigned offset, uint32 value)
{
    value = CL(value);
    memcpy((Byte*)memory + offset, &value, 4);
}

static int bundle_reference(const char* name, uint32 offset, const uint32* arguments, unsigned count,
                            ROMlib_carbon_reference* reference, uint32* result)
{
    if (!ROMlib_carbon_reference_function("CoreFoundationLib", name, reference))
        return 0;
    if (reference->code_offset == offset && reference->code_size == 0x5dee0 && !reference->toc_offset &&
        reference->toc && reference->toc_size == 0x72a8)
        return 1;
    uint32 answer;
    if (ROMlib_carbon_invoke_callback(reference->function, arguments, count, &answer))
        *result = answer;
    return 0;
}

static int bundle_call(const char* name, const uint32* arguments, unsigned count, uint32* result)
{
    ROMlib_carbon_reference reference;
    return ROMlib_carbon_reference_function("CoreFoundationLib", name, &reference) &&
           ROMlib_carbon_invoke_callback(reference.function, arguments, count, result);
}

/* Only used after validating the containing image. The original dependency
   executes through an aligned transition vector, with its real live TOC. */
static int bundle_private(const ROMlib_carbon_reference* reference, uint32 offset, const uint32* arguments,
                          unsigned count, uint32* result)
{
    _Alignas(16) uint32 vector[2];
    bundle_store(vector, 0, bundle_word(reference->function, 0) - reference->code_offset + offset);
    bundle_store(vector, 4, US_TO_SYN68K(reference->toc));
    return ROMlib_carbon_invoke_callback(vector, arguments, count, result);
}

static int bundle_copy_url(void* bundle, uint32* result)
{
    uint32 url = bundle_word(bundle, 8);
    if (url && !bundle_call("CFRetain", &url, 1, &url))
        return 0;
    /* The original reloads after the retain, which can invoke guest code. */
    *result = bundle_word(bundle, 8);
    return 1;
}

static int bundle_info(const ROMlib_carbon_reference* reference, void* bundle, uint32* result)
{
    *result = bundle_word(bundle, 20);
    if (!*result) {
        uint32 object = US_TO_SYN68K(bundle), allocator;
        if (!bundle_call("CFGetAllocator", &object, 1, &allocator))
            return 0;
        uint32 arguments[] = {allocator, bundle_word(bundle, 8), ((const Byte*)bundle)[16]};
        if (!bundle_private(reference, 0x13adc, arguments, 3, result))
            return 0;
        bundle_store(bundle, 20, *result);
    }
    *result = bundle_word(bundle, 20);
    return 1;
}

uint32 ROMlib_carbon_cf_bundle_type_id(void)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (bundle_reference("CFBundleGetTypeID", 0xd7e8, NULL, 0, &reference, &result))
        result = bundle_word(reference.toc, 0x834);
    return result;
}

void* ROMlib_carbon_cf_bundle_url(void* bundle)
{
    ROMlib_carbon_reference reference;
    uint32 object = US_TO_SYN68K(bundle), result = 0;
    if (bundle_reference("CFBundleCopyBundleURL", 0xdb20, &object, 1, &reference, &result) && bundle)
        if (!bundle_copy_url(bundle, &result))
            result = 0;
    return SYN68K_TO_US_CHECK0(result);
}

void* ROMlib_carbon_cf_bundle_info(void* bundle)
{
    ROMlib_carbon_reference reference;
    uint32 object = US_TO_SYN68K(bundle), result = 0;
    if (bundle_reference("CFBundleGetInfoDictionary", 0xdd2c, &object, 1, &reference, &result) && bundle)
        if (!bundle_info(&reference, bundle, &result))
            result = 0;
    return SYN68K_TO_US_CHECK0(result);
}

void* ROMlib_carbon_cf_bundle_info_value(void* bundle, const void* key)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {US_TO_SYN68K(bundle), US_TO_SYN68K(key)}, result = 0;
    if (!bundle_reference("CFBundleGetValueForInfoDictionaryKey", 0xdedc, arguments, 2, &reference, &result))
        return SYN68K_TO_US_CHECK0(result);
    if (!bundle || !key)
        return NULL;
    if (!bundle_call("CFBundleGetLocalInfoDictionary", arguments, 1, &result))
        return NULL;
    if (result) {
        arguments[0] = result;
        if (!bundle_call("CFDictionaryGetValue", arguments, 2, &result))
            return NULL;
    }
    if (!result) {
        if (!bundle_info(&reference, bundle, &result))
            return NULL;
        if (result) {
            arguments[0] = result;
            if (!bundle_call("CFDictionaryGetValue", arguments, 2, &result))
                return NULL;
        }
    }
    return SYN68K_TO_US_CHECK0(result);
}

void* ROMlib_carbon_cf_bundle_identifier(void* bundle)
{
    ROMlib_carbon_reference reference;
    uint32 object = US_TO_SYN68K(bundle), result = 0;
    if (!bundle_reference("CFBundleGetIdentifier", 0xdf74, &object, 1, &reference, &result))
        return SYN68K_TO_US_CHECK0(result);
    if (!bundle || !bundle_info(&reference, bundle, &result))
        return NULL;
    if (result) {
        uint32 arguments[] = {result, bundle_word(reference.toc, 0x227c)};
        if (!bundle_call("CFDictionaryGetValue", arguments, 2, &result))
            return NULL;
    }
    return SYN68K_TO_US_CHECK0(result);
}

uint32 ROMlib_carbon_cf_bundle_loaded(void* bundle)
{
    ROMlib_carbon_reference reference;
    uint32 object = US_TO_SYN68K(bundle), result = 0;
    if (bundle_reference("CFBundleIsExecutableLoaded", 0xf184, &object, 1, &reference, &result))
        return bundle ? ((const Byte*)bundle)[36] : 0;
    return result & 255;
}

void* ROMlib_carbon_cf_bundle_plugin(void* bundle)
{
    ROMlib_carbon_reference reference;
    uint32 object = US_TO_SYN68K(bundle), result = 0;
    if (bundle_reference("CFBundleGetPlugIn", 0xf8d0, &object, 1, &reference, &result))
        return bundle && ((const Byte*)bundle)[76] ? bundle : NULL;
    return SYN68K_TO_US_CHECK0(result);
}

/* Native control flow of the CFM load helper at 0xfe0c. Actual resource/PEF
   discovery at 0xfd2c remains original, including its error and ownership rules. */
static int bundle_load_cfm(const ROMlib_carbon_reference* reference, void* bundle, uint32* result)
{
    uint32 object = US_TO_SYN68K(bundle), url, allocator, arguments[4];
    if (((Byte*)bundle)[36]) {
        *result = ((Byte*)bundle)[36];
        return 1;
    }
    if (!bundle_copy_url(bundle, &url))
        return 0;
    bundle_store(bundle, 40, 0);
    if (url) {
        if (!bundle_call("CFGetAllocator", &object, 1, &allocator))
            return 0;
        arguments[0] = allocator;
        arguments[1] = url;
        arguments[2] = arguments[3] = 0;
        if (!bundle_private(reference, 0xfd2c, arguments, 4, result))
            return 0;
        bundle_store(bundle, 40, *result);
        if (!bundle_call("CFRelease", &url, 1, NULL))
            return 0;
    }
    if (!bundle_word(bundle, 40)) {
        if (!bundle_call("CFBundleCopyExecutableURL", &object, 1, &url))
            return 0;
        if (url) {
            if (!bundle_call("CFGetAllocator", &object, 1, &allocator))
                return 0;
            arguments[0] = allocator;
            arguments[1] = url;
            arguments[2] = 0;
            arguments[3] = 1;
            if (!bundle_private(reference, 0xfd2c, arguments, 4, result))
                return 0;
            bundle_store(bundle, 40, *result);
            if (!bundle_call("CFRelease", &url, 1, NULL))
                return 0;
        }
    }
    if (!bundle_word(bundle, 40))
        bundle_store(bundle, 32, 6);
    else
        ((Byte*)bundle)[36] = 1;
    *result = ((Byte*)bundle)[36];
    return 1;
}

uint32 ROMlib_carbon_cf_bundle_load(void* bundle)
{
    ROMlib_carbon_reference reference;
    uint32 object = US_TO_SYN68K(bundle), result = 0, url;
    if (!bundle_reference("CFBundleLoadExecutable", 0xf18c, &object, 1, &reference, &result))
        return result & 255;
    if (!bundle)
        return 0;
    uint32 messages = bundle_word(reference.toc, 0x408);
    if (!bundle_call("CFBundleCopyExecutableURL", &object, 1, &url))
        return 0;
    if (!url)
        bundle_store(bundle, 32, 7);
    else if (!bundle_call("CFRelease", &url, 1, NULL))
        return 0;
    if (((Byte*)bundle)[36]) {
        uint32 lock = US_TO_SYN68K((const Byte*)reference.toc + 0x830);
        if (!bundle_private(&reference, 0x1a98c, &lock, 1, NULL))
            return 0;
        uint32 arguments[] = {bundle_word(reference.toc, 0x824), object};
        if (arguments[0] && !bundle_call("CFSetRemoveValue", arguments, 2, NULL))
            return 0;
        if (!bundle_private(&reference, 0x1a990, &lock, 1, NULL))
            return 0;
        return 1;
    }
    if (!((const Byte*)reference.toc)[0x820] && !bundle_private(&reference, 0xf4d0, NULL, 0, NULL))
        return 0;
    uint32 kind = bundle_word(bundle, 32);
    if (kind == 1)
        return bundle_load_cfm(&reference, bundle, &result) ? result & 255 : 0;
    uint32 text = messages + (kind == 7 ? 0x3fe : 0x41c);
    if (!bundle_call("__CFStringMakeConstantString", &text, 1, &result))
        return 0;
    uint32 arguments[] = {0x15, result, object};
    bundle_call("CFLog", arguments, 3, NULL);
    return 0;
}

/* Native constructor worker (0xd7f0), not just the public forwarding entry.
   Filesystem probing, cache registration and plug-in setup retain their real
   initialized dependencies until those subsystems are migrated. */
void* ROMlib_carbon_cf_bundle_create(uint32 allocator, const void* url)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[8] = {allocator, US_TO_SYN68K(url)}, result = 0;
    if (!bundle_reference("CFBundleCreate", 0xda30, arguments, 2, &reference, &result))
        return SYN68K_TO_US_CHECK0(result);
    if (!url)
        return NULL;
    uint32 lock = US_TO_SYN68K((const Byte*)reference.toc + 0x830);
    if (!bundle_private(&reference, 0x1a98c, &lock, 1, NULL))
        return NULL;
    arguments[0] = bundle_word(reference.toc, 0x82c);
    arguments[1] = US_TO_SYN68K(url);
    if (arguments[0] && !bundle_call("CFDictionaryGetValue", arguments, 2, &result))
        return NULL;
    if (!bundle_private(&reference, 0x1a990, &lock, 1, NULL))
        return NULL;
    if (result) {
        uint32 cached = result;
        if (!bundle_call("CFRetain", &cached, 1, &result))
            return NULL;
        return SYN68K_TO_US_CHECK0(cached);
    }
    /* Host aliases expose 128 KiB windows; keep guest callback buffers within one. */
    _Alignas(256) Byte path[256];
    arguments[0] = US_TO_SYN68K(url);
    arguments[1] = 1;
    arguments[2] = US_TO_SYN68K(path);
    arguments[3] = sizeof path;
    if (!bundle_call("CFURLGetFileSystemRepresentation", arguments, 4, &result) || !(result & 255))
        return NULL;
    const Byte* end = memchr(path, 0, sizeof path);
    if (!end)
        return NULL;
    arguments[0] = allocator;
    arguments[1] = US_TO_SYN68K(path);
    arguments[2] = end - path;
    arguments[3] = 1;
    if (!bundle_call("CFURLCreateFromFileSystemRepresentation", arguments, 4, &result))
        return NULL;
    uint32 canonical = result;
    if (!canonical) {
        uint32 original = US_TO_SYN68K(url);
        if (!bundle_call("CFRetain", &original, 1, &canonical))
            return NULL;
    }
    Byte kind = 0, directory = 0;
    uint32 flags = 0, properties = 0;
    arguments[0] = canonical;
    arguments[1] = US_TO_SYN68K(&kind);
    if (!bundle_private(&reference, 0x13738, arguments, 2, &result))
        return NULL;
    if (!(result & 255)) {
        kind = 3;
        uint32 file_arguments[] = {
            allocator, canonical, US_TO_SYN68K(&directory), US_TO_SYN68K(&flags), 0, US_TO_SYN68K(&properties), 0, 0};
        if (!bundle_call("_CFGetFileProperties", file_arguments, 8, &result))
            return NULL;
        if ((int32)result || !directory || (CL(flags) & 0xf000) != 0x4000) {
            if (!(int32)result && properties) {
                uint32 property = CL(properties);
                if (!bundle_call("CFRelease", &property, 1, NULL))
                    return NULL;
            }
            bundle_call("CFRelease", &canonical, 1, NULL);
            return NULL;
        }
    }
    uint32 create_arguments[] = {allocator, bundle_word(reference.toc, 0x834), 80, 0};
    if (!bundle_call("_CFRuntimeCreateInstance", create_arguments, 4, &result))
        return NULL;
    if (!result) {
        bundle_call("CFRelease", &canonical, 1, NULL);
        return NULL;
    }
    void* bundle = SYN68K_TO_US(result);
    memset((Byte*)bundle + 8, 0, 80);
    bundle_store(bundle, 8, canonical);
    memcpy((Byte*)bundle + 12, &properties, 4);
    ((Byte*)bundle)[16] = kind;
    bundle_store(bundle, 32, 1);
    uint32 object = result;
    if (!bundle_info(&reference, bundle, &result))
        return NULL;
    arguments[0] = object;
    arguments[1] = 0;
    if (!bundle_private(&reference, 0xc674, arguments, 2, NULL) ||
        !bundle_private(&reference, 0x165ec, &object, 1, NULL))
        return NULL;
    return bundle;
}

void* ROMlib_carbon_cf_bundle_local_info(void* bundle)
{
    ROMlib_carbon_reference reference;
    uint32 object = US_TO_SYN68K(bundle), result = 0;
    if (!bundle_reference("CFBundleGetLocalInfoDictionary", 0xdda8, &object, 1, &reference, &result))
        return SYN68K_TO_US_CHECK0(result);
    if (!bundle)
        return NULL;
    if (!bundle_word(bundle, 24)) {
        uint32 base = bundle_word(reference.toc, 0x408), suffix = base + 0x343, name = base + 0x339;
        if (!bundle_call("__CFStringMakeConstantString", &suffix, 1, &suffix) ||
            !bundle_call("__CFStringMakeConstantString", &name, 1, &name))
            return NULL;
        uint32 arguments[] = {object, name, suffix, 0}, url;
        if (!bundle_call("CFBundleCopyResourceURL", arguments, 4, &url))
            return NULL;
        if (url) {
            uint32 allocator, data = 0, error = 0, read_error = 0;
            if (!bundle_call("CFGetAllocator", &object, 1, &allocator))
                return NULL;
            uint32 read_arguments[] = {allocator, url, US_TO_SYN68K(&data), 0, 0, US_TO_SYN68K(&read_error)};
            if (!bundle_call("CFURLCreateDataAndPropertiesFromResource", read_arguments, 6, &result))
                return NULL;
            if (result) {
                if (!bundle_call("CFGetAllocator", &object, 1, &allocator))
                    return NULL;
                uint32 parse_arguments[] = {allocator, CL(data), 0, US_TO_SYN68K(&error)};
                if (!bundle_call("CFPropertyListCreateFromXMLData", parse_arguments, 4, &result))
                    return NULL;
                bundle_store(bundle, 24, result);
                uint32 value = CL(error);
                if (value && !bundle_call("CFRelease", &value, 1, NULL))
                    return NULL;
                value = CL(data);
                if (!bundle_call("CFRelease", &value, 1, NULL))
                    return NULL;
            }
            if (!bundle_call("CFRelease", &url, 1, NULL))
                return NULL;
        }
    }
    return SYN68K_TO_US_CHECK0(bundle_word(bundle, 24));
}

static int bundle_digit(uint16 character)
{
    return character >= '0' && character <= '9';
}

/* Preserve 1.4's finite grammar, including single-component BCD quirks and
   accepted trailing separators/stage letters. This is not a modern version parser. */
static uint32 bundle_version_text(const uint16* text, unsigned length)
{
    unsigned at = 0, high = 0, low = 0, minor = 0, patch = 0, stage = 0x80, build = 0;
    int qualifier = 0;
#define VERSION_CHAR(i) CW(text[i])
    if (!length || length > 10)
        return 0;
    if (bundle_digit(VERSION_CHAR(at))) {
        high = VERSION_CHAR(at++) - '0';
        if (at < length) {
            if (bundle_digit(VERSION_CHAR(at))) {
                low = VERSION_CHAR(at++) - '0';
                if (at < length) {
                    if (VERSION_CHAR(at) == '.')
                        ++at;
                    else
                        qualifier = 1;
                }
            }
            else if (VERSION_CHAR(at) == '.') {
                low = high;
                high = 0;
                ++at;
            }
            else
                qualifier = 1;
        }
    }
    else if (VERSION_CHAR(at) == '.')
        ++at;
    else
        qualifier = 1;
    if (at < length && !qualifier) {
        if (bundle_digit(VERSION_CHAR(at))) {
            minor = VERSION_CHAR(at++) - '0';
            if (at < length) {
                if (VERSION_CHAR(at) == '.')
                    ++at;
                else
                    qualifier = 1;
            }
        }
        else
            qualifier = 1;
    }
    if (at < length && !qualifier && bundle_digit(VERSION_CHAR(at)))
        patch = VERSION_CHAR(at++) - '0';
    if (at < length) {
        uint16 character = VERSION_CHAR(at++);
        if (character == 'd')
            stage = 0x20;
        else if (character == 'a')
            stage = 0x40;
        else if (character == 'b')
            stage = 0x60;
        else
            return 0;
    }
    unsigned digits = 0;
    while (at < length && digits++ < 3) {
        uint16 character = VERSION_CHAR(at++);
        if (!bundle_digit(character))
            return 0;
        build = build * 10 + character - '0';
    }
    if (at < length || build > 255)
        return 0;
#undef VERSION_CHAR
    return (high << 28) + (low << 24) + (minor << 20) + (patch << 16) + (stage << 8) + build;
}

uint32 ROMlib_carbon_cf_bundle_version(void* bundle)
{
    ROMlib_carbon_reference reference;
    uint32 object = US_TO_SYN68K(bundle), result = 0;
    if (!bundle_reference("CFBundleGetVersionNumber", 0xe3cc, &object, 1, &reference, &result))
        return result;
    if (!bundle || !bundle_info(&reference, bundle, &result))
        return 0;
    uint32 dictionary = result, key = bundle_word(reference.toc, 0x2304);
    uint32 arguments[] = {dictionary, key, 0}, value;
    if (!bundle_call("CFDictionaryGetValue", arguments, 2, &value))
        return 0;
    if (!value) {
        arguments[1] = bundle_word(reference.toc, 0x2280);
        if (!bundle_call("CFDictionaryGetValue", arguments, 2, &value))
            return 0;
    }
    uint32 number = 0, string_type, type;
    if (!value)
        return 0;
    if (!bundle_call("CFStringGetTypeID", NULL, 0, &string_type) || !bundle_call("CFGetTypeID", &value, 1, &type))
        return 0;
    if (type == string_type) {
        uint32 length;
        if (!bundle_call("CFStringGetLength", &value, 1, &length))
            return 0;
        if (length && length <= 10) {
            _Alignas(32) uint16 text[10];
            uint32 text_arguments[] = {value, 0, length, US_TO_SYN68K(text)};
            if (!bundle_call("CFStringGetCharacters", text_arguments, 4, NULL))
                return 0;
            number = bundle_version_text(text, length);
        }
        uint32 allocator;
        if (!bundle_call("CFGetAllocator", &object, 1, &allocator))
            return 0;
        uint32 encoded = CL(number), create_arguments[] = {allocator, 3, US_TO_SYN68K(&encoded)};
        if (!bundle_call("CFNumberCreate", create_arguments, 3, &value))
            return 0;
        arguments[1] = bundle_word(reference.toc, 0x2304);
        arguments[2] = value;
        if (!bundle_call("CFDictionarySetValue", arguments, 3, NULL) || !bundle_call("CFRelease", &value, 1, NULL))
            return 0;
    }
    else {
        uint32 number_type;
        if (!bundle_call("CFNumberGetTypeID", NULL, 0, &number_type) || !bundle_call("CFGetTypeID", &value, 1, &type))
            return 0;
        if (type == number_type) {
            uint32 encoded = 0, get_arguments[] = {value, 3, US_TO_SYN68K(&encoded)};
            if (!bundle_call("CFNumberGetValue", get_arguments, 3, &result))
                return 0;
            number = CL(encoded);
        }
        else {
            arguments[1] = bundle_word(reference.toc, 0x2304);
            if (!bundle_call("CFDictionaryRemoveValue", arguments, 2, NULL))
                return 0;
        }
    }
    return number;
}

void* ROMlib_carbon_cf_bundle_development(void* bundle)
{
    ROMlib_carbon_reference reference;
    uint32 object = US_TO_SYN68K(bundle), result = 0;
    if (!bundle_reference("CFBundleGetDevelopmentRegion", 0xe4f8, &object, 1, &reference, &result))
        return SYN68K_TO_US_CHECK0(result);
    if (!bundle || !bundle_info(&reference, bundle, &result))
        return NULL;
    uint32 dictionary = result, key = bundle_word(reference.toc, 0x2284), region = 0;
    if (dictionary) {
        uint32 arguments[] = {dictionary, key, 0}, type, string_type, length;
        if (!bundle_call("CFDictionaryGetValue", arguments, 2, &region))
            return NULL;
        if (region) {
            if (!bundle_call("CFStringGetTypeID", NULL, 0, &string_type) ||
                !bundle_call("CFGetTypeID", &region, 1, &type))
                return NULL;
            length = 0;
            if (type == string_type && !bundle_call("CFStringGetLength", &region, 1, &length))
                return NULL;
            if (type != string_type || !length) {
                region = 0;
                if (!bundle_call("CFDictionaryRemoveValue", arguments, 2, NULL))
                    return NULL;
            }
        }
        if (!region && object == bundle_word(reference.toc, 0x814)) {
            if (!bundle_private(&reference, 0x15098, &object, 1, &region))
                return NULL;
            if (region) {
                arguments[2] = region;
                if (!bundle_call("CFDictionarySetValue", arguments, 3, NULL) ||
                    !bundle_call("CFRelease", &region, 1, NULL))
                    return NULL;
            }
            else {
                uint32 text = bundle_word(reference.toc, 0x408) + 0x381;
                if (!bundle_call("__CFStringMakeConstantString", &text, 1, &region))
                    return NULL;
                arguments[2] = region;
                if (!bundle_call("CFDictionarySetValue", arguments, 3, NULL))
                    return NULL;
            }
        }
    }
    return SYN68K_TO_US_CHECK0(region);
}

void ROMlib_carbon_cf_bundle_unload(void* bundle)
{
    ROMlib_carbon_reference reference;
    uint32 object = US_TO_SYN68K(bundle), result = 0;
    if (!bundle_reference("CFBundleUnloadExecutable", 0xf2c0, &object, 1, &reference, &result) || !bundle)
        return;
    if (!((const Byte*)reference.toc)[0x820] && !bundle_private(&reference, 0xf4d0, NULL, 0, NULL))
        return;
    if (!((Byte*)bundle)[36])
        return;
    uint32 lock = US_TO_SYN68K((const Byte*)reference.toc + 0x830);
    if (!((const Byte*)reference.toc)[0x820] && !bundle_private(&reference, 0x1a98c, &lock, 1, NULL))
        return;
    uint32 arguments[] = {bundle_word(reference.toc, 0x824), object, 0};
    if (arguments[0] && !bundle_call("CFSetRemoveValue", arguments, 2, NULL))
        return;
    if (!((const Byte*)reference.toc)[0x820] && !bundle_private(&reference, 0x1a990, &lock, 1, NULL))
        return;
    if (!bundle_private(&reference, 0x16b20, &object, 1, NULL))
        return;
    if (bundle_word(bundle, 32) == 1 && ((Byte*)bundle)[36]) {
        uint32 connection = bundle_word(bundle, 40);
        /* The original import stub invokes the host CFM CloseConnection bridge. */
        if (!bundle_private(&reference, 0x5a46c, &connection, 1, NULL))
            return;
        bundle_store(bundle, 40, 0);
        ((Byte*)bundle)[36] = 0;
    }
    if (!((Byte*)bundle)[36] && bundle_word(bundle, 64)) {
        uint32 allocator;
        if (!bundle_call("CFGetAllocator", &object, 1, &allocator))
            return;
        arguments[0] = bundle_word(bundle, 64);
        arguments[1] = bundle_word(reference.toc, 0x404);
        arguments[2] = allocator;
        if (!bundle_call("CFDictionaryApplyFunction", arguments, 3, NULL))
            return;
        uint32 cache = bundle_word(bundle, 64);
        if (!bundle_call("CFRelease", &cache, 1, NULL))
            return;
        bundle_store(bundle, 64, 0);
    }
}

static void bundle_pointers(void* bundle, const void* names, void* output, const char* name, uint32 offset, int data)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {US_TO_SYN68K(bundle), US_TO_SYN68K(names), US_TO_SYN68K(output)}, result = 0;
    if (!bundle_reference(name, offset, arguments, 3, &reference, &result) || !output)
        return;
    uint32 array = US_TO_SYN68K(names), count;
    if (!bundle_call("CFArrayGetCount", &array, 1, &count))
        return;
    for (int32 i = 0; i < (int32)count; ++i) {
        uint32 get_arguments[] = {array, i};
        if (!bundle_call("CFArrayGetValueAtIndex", get_arguments, 2, &result))
            return;
        const void* symbol_name = SYN68K_TO_US_CHECK0(result);
        void* address = data ? ROMlib_carbon_cf_bundle_data(bundle, symbol_name)
                             : ROMlib_carbon_cf_bundle_function(bundle, symbol_name);
        bundle_store(output, (uint32)i * 4, US_TO_SYN68K(address));
    }
}

void ROMlib_carbon_cf_bundle_functions(void* bundle, const void* names, void* output)
{
    bundle_pointers(bundle, names, output, "CFBundleGetFunctionPointersForNames", 0xf6c4, 0);
}

void ROMlib_carbon_cf_bundle_data_pointers(void* bundle, const void* names, void* output)
{
    bundle_pointers(bundle, names, output, "CFBundleGetDataPointersForNames", 0xf84c, 1);
}
