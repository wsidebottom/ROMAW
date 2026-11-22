/*+===================================================================
  File:      cf_string_encodings.c

  Summary:   Native encoding queries and caches. Converter registries, Unicode encoding and TextCommon/TEC import
             workers still execute in the real initialized PEF.

  Classes:   None.

  Functions: ROMlib_cfstring_encoding_iana, ROMlib_cfstring_encoding_list, ROMlib_cfstring_encoding_name,
             ROMlib_cfstring_external_representation, ROMlib_cfstring_iana_encoding, ROMlib_cfstring_smallest_encoding,
             ROMlib_cfstring_system_encoding

  Origin:    Existing repository source in packages/carbonlib/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Native encoding queries and caches. Converter registries, Unicode encoding
   and TextCommon/TEC import workers still execute in the real initialized PEF. */
#include "cf_batch.h"
#include <limits.h>

static uint32 encoding_constant(const ROMlib_carbon_reference* reference, uint32 offset)
{
    uint32 literal = cfp_toc(reference, 0x5c0) + offset, result = 0;
    cfp_call("__CFStringMakeConstantString", &literal, 1, &result);
    return result;
}

uint32 ROMlib_cfstring_system_encoding(void)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0, ignored;
    if (!cfp_reference("CFStringGetSystemEncoding", 0x35524, NULL, 0, &reference, &result))
        return result;
    result = cfp_toc(&reference, 0x978);
    if (result != UINT32_MAX)
        return result;
    uint32 selector = 40, script;
    _Alignas(16) uint32 encoding = 0;
    if (!cfp_private(&reference, 0x59fa4, &selector, 1, &script))
        return 0;
    uint32 upgrade[] = {UINT32_MAX, (uint32)-128, (uint32)(int32)(int16)script, 0, US_TO_SYN68K(&encoding)};
    if (!cfp_private(&reference, 0x59fbc, upgrade, 5, &ignored))
        return 0;
    result = 0;
    if (!ignored) {
        uint32 composite = CL(encoding);
        if (!cfp_private(&reference, 0x59fd4, &composite, 1, &result))
            return 0;
    }
    uint32 converter;
    if (!cfp_private(&reference, 0x45d78, &result, 1, &converter))
        return 0;
    if (!converter) {
        result = 0;
        if (!cfp_private(&reference, 0x45d78, &result, 1, &converter))
            return 0;
    }
    if (!converter)
        return cfp_failure("Native Carbon system string converter is unavailable");
    const uint8* width = ROMlib_carbon_guest_memory(converter + 12, 1, 0);
    if (!width || !cfp_write(US_TO_SYN68K(reference.toc) + 0x978, result) ||
        !cfp_write(US_TO_SYN68K(reference.toc) + 0x6f5c, *width == 1 ? cfp_read(converter + 4) : 0))
        return 0;
    return result;
}

uint32 ROMlib_cfstring_encoding_list(void)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (cfp_reference("CFStringGetListOfAvailableEncodings", 0x43388, NULL, 0, &reference, &result))
        cfp_private(&reference, 0x45f6c, NULL, 0, &result);
    return result;
}

static uint32 encoding_cached(const ROMlib_carbon_reference* reference, uint32 toc_offset, uint32 key)
{
    uint32 dictionary = cfp_toc(reference, toc_offset), result = 0;
    if (dictionary) {
        uint32 lookup[] = {dictionary, key};
        cfp_call("CFDictionaryGetValue", lookup, 2, &result);
    }
    return result;
}

static int encoding_cache(const ROMlib_carbon_reference* reference, uint32 toc_offset, uint32 key, uint32 value)
{
    uint32 dictionary = cfp_toc(reference, toc_offset), ignored;
    if (!dictionary) {
        uint32 create[] = {0, 0, 0, US_TO_SYN68K(reference->toc) + 0x33f0};
        if (!cfp_call("CFDictionaryCreateMutable", create, 4, &dictionary) || !dictionary ||
            !cfp_write(US_TO_SYN68K(reference->toc) + toc_offset, dictionary))
            return 0;
    }
    uint32 insert[] = {dictionary, key, value};
    if (!cfp_call("CFDictionarySetValue", insert, 3, &ignored))
        return 0;
    return cfp_call("CFRelease", &value, 1, &ignored);
}

uint32 ROMlib_cfstring_encoding_name(uint32 encoding)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (!cfp_reference("CFStringGetNameOfEncoding", 0x433ac, &encoding, 1, &reference, &result))
        return result;
    result = encoding_cached(&reference, 0x9bc, encoding);
    if (result)
        return result;
    if (encoding == 0x100)
        return encoding_constant(&reference, 0x43);
    if (encoding == 0x08000100)
        return encoding_constant(&reference, 0x4b);
    if (encoding == 0xbff)
        return encoding_constant(&reference, 0x51);
    uint32 name;
    if (!cfp_private(&reference, 0x457a8, &encoding, 1, &name))
        return 0;
    if (name)
        result = ROMlib_cfstring_cstring(0, name, ROMlib_cfstring_system_encoding());
    else {
        uint32 selector = 40, script, system = ROMlib_cfstring_system_encoding(), status;
        _Alignas(512) uint8 buffer[257];
        _Alignas(16) uint32 length = 0, region = 0, name_encoding = 0;
        if (!cfp_private(&reference, 0x59fa4, &selector, 1, &script))
            return 0;
        uint32 query[] = {encoding,
                          0,
                          (uint32)(int32)(int16)script,
                          system,
                          256,
                          US_TO_SYN68K(&length),
                          US_TO_SYN68K(&region),
                          US_TO_SYN68K(&name_encoding),
                          US_TO_SYN68K(buffer)};
        if (!cfp_private(&reference, 0x5a634, query, 9, &status))
            return 0;
        if (!status) {
            uint32 count = CL(length);
            if (count > 256)
                return cfp_failure("Native Carbon encoding name exceeds the supplied buffer");
            buffer[count] = 0;
            result = ROMlib_cfstring_cstring(0, US_TO_SYN68K(buffer), CL(name_encoding));
        }
    }
    if (result && !encoding_cache(&reference, 0x9bc, encoding, result))
        return 0;
    return result;
}

uint32 ROMlib_cfstring_encoding_iana(uint32 encoding)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (!cfp_reference("CFStringConvertEncodingToIANACharSetName", 0x43850, &encoding, 1, &reference, &result))
        return result;
    result = encoding_cached(&reference, 0x9b4, encoding);
    if (result)
        return result;
    if (encoding == 0x100)
        result = encoding_constant(&reference, 0x85);
    else {
        uint32 aliases;
        if (!cfp_private(&reference, 0x458f8, &encoding, 1, &aliases))
            return 0;
        if (aliases) {
            uint32 first;
            if (!cfp_get(aliases, &first))
                return 0;
            if (first)
                result = ROMlib_cfstring_cstring(0, first, ROMlib_cfstring_system_encoding());
        }
        if (!result) {
            _Alignas(256) uint8 name[256];
            uint32 query[] = {encoding, US_TO_SYN68K(name)}, status;
            if (!cfp_private(&reference, 0x5a61c, query, 2, &status))
                return 0;
            if (!status)
                result = ROMlib_cfstring_pascal(0, US_TO_SYN68K(name), 0x600);
        }
    }
    if (result && !encoding_cache(&reference, 0x9b4, encoding, result))
        return 0;
    return result;
}

static int encoding_alias_insert(uint32 dictionary, uint32 name, uint32 encoding)
{
    uint32 insert[] = {dictionary, name, encoding}, ignored;
    return cfp_call("CFDictionarySetValue", insert, 3, &ignored);
}

uint32 ROMlib_cfstring_iana_encoding(uint32 name)
{
    ROMlib_carbon_reference reference;
    uint32 result = UINT32_MAX, ignored;
    if (!cfp_reference("CFStringConvertIANACharSetNameToEncoding", 0x43574, &name, 1, &reference, &result))
        return result;
    uint32 normalized = ROMlib_cfstring_mutable_copy(0, 0, name);
    if (!normalized)
        return UINT32_MAX;
    ROMlib_cfstring_lowercase(normalized, 0);
    const uint32 literals[] = {0x61, 0x67, 0x72};
    const uint32 encodings[] = {0x08000100, 0x201, 0x100};
    for (unsigned i = 0; i < NELEM(literals); ++i) {
        uint32 literal = encoding_constant(&reference, literals[i]);
        if (!ROMlib_carbon_cf_string_compare(SYN68K_TO_US_CHECK0(normalized), SYN68K_TO_US_CHECK0(literal), 1)) {
            cfp_call("CFRelease", &normalized, 1, &ignored);
            return encodings[i];
        }
    }
    uint32 dictionary = cfp_toc(&reference, 0x9b8);
    if (!dictionary) {
        uint32 create[] = {0, 0, US_TO_SYN68K(reference.toc) + 0x33c0, 0};
        if (!cfp_call("CFDictionaryCreateMutable", create, 4, &dictionary) || !dictionary)
            return UINT32_MAX;
        uint32 available = ROMlib_cfstring_encoding_list();
        for (uint32 offset = 0;; offset += 4) {
            uint32 encoding, aliases;
            if (!cfp_get(available + offset, &encoding))
                return UINT32_MAX;
            if (encoding == UINT32_MAX)
                break;
            if (!cfp_private(&reference, 0x458f8, &encoding, 1, &aliases))
                return UINT32_MAX;
            if (!aliases)
                continue;
            for (uint32 index = 0;; index += 4) {
                uint32 text;
                if (!cfp_get(aliases + index, &text))
                    return UINT32_MAX;
                if (!text)
                    break;
                uint32 alias = ROMlib_cfstring_cstring(0, text, ROMlib_cfstring_system_encoding());
                if (!alias || !encoding_alias_insert(dictionary, alias, encoding) ||
                    !cfp_call("CFRelease", &alias, 1, &ignored))
                    return UINT32_MAX;
            }
        }
        const uint32 unicode_literals[] = {0x79, 0x72, 0x85};
        for (unsigned i = 0; i < NELEM(unicode_literals); ++i)
            if (!encoding_alias_insert(dictionary, encoding_constant(&reference, unicode_literals[i]), 0x100))
                return UINT32_MAX;
        if (!cfp_write(US_TO_SYN68K(reference.toc) + 0x9b8, dictionary))
            return UINT32_MAX;
    }
    uint32 lookup[] = {dictionary, normalized}, contains;
    if (!cfp_call("CFDictionaryContainsKey", lookup, 2, &contains))
        return UINT32_MAX;
    if (contains)
        cfp_call("CFDictionaryGetValue", lookup, 2, &result);
    else {
        _Alignas(256) uint8 buffer[256];
        _Alignas(16) uint32 composite = 0;
        uint32 convert[] = {normalized, US_TO_SYN68K(buffer), sizeof buffer, 0x08000100}, converted;
        if (!cfp_call("CFStringGetCString", convert, 4, &converted))
            return UINT32_MAX;
        if (converted) {
            uint32 query[] = {US_TO_SYN68K(&composite), US_TO_SYN68K(buffer)}, status;
            if (!cfp_private(&reference, 0x5a40c, query, 2, &status))
                return UINT32_MAX;
            if (!status) {
                uint32 full = CL(composite), base, format;
                if (!cfp_private(&reference, 0x59fd4, &full, 1, &base) ||
                    !cfp_private(&reference, 0x5a424, &full, 1, &format))
                    return UINT32_MAX;
                if (base > 0x100 && base < 0x200)
                    base = 0x100;
                uint32 compose[] = {base, 0, format};
                if (!cfp_private(&reference, 0x5a43c, compose, 3, &result))
                    return UINT32_MAX;
            }
        }
    }
    cfp_call("CFRelease", &normalized, 1, &ignored);
    return result;
}

uint32 ROMlib_cfstring_smallest_encoding(uint32 object)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0, flags, length;
    if (!cfp_reference("CFStringGetSmallestEncoding", 0x3cd94, &object, 1, &reference, &result))
        return result;
    if (!cfp_get(object + 4, &flags))
        return 0;
    uint32 internal = cfp_toc(&reference, 0x3a70), ignored;
    if (internal == UINT32_MAX) {
        if (!cfp_private(&reference, 0x357c4, NULL, 0, &ignored))
            return 0;
        internal = cfp_toc(&reference, 0x3a70);
    }
    if (!(flags & 0x10))
        return internal;
    if (!cfp_call("CFStringGetLength", &object, 1, &length))
        return 0;
    uint32 candidates[] = {internal, ROMlib_cfstring_system_encoding()};
    for (unsigned i = 0; i < NELEM(candidates); ++i) {
        if (i && candidates[0] == candidates[1])
            break;
        _Alignas(16) uint32 used = 0;
        uint32 encode[] = {object, 0, length, 0, candidates[i], 0, 0, INT32_MAX, US_TO_SYN68K(&used)}, consumed;
        if (!cfp_private(&reference, 0x419b8, encode, 9, &consumed))
            return 0;
        if (consumed == length)
            return candidates[i];
    }
    return 0x100;
}

uint32 ROMlib_cfstring_external_representation(uint32 allocator, uint32 object, uint32 encoding, uint32 loss)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, object, encoding, loss}, result = 0, length, consumed, ignored;
    if (!cfp_reference("CFStringCreateExternalRepresentation", 0x3c698, arguments, 4, &reference, &result))
        return result;
    if (!cfp_call("CFStringGetLength", &object, 1, &length))
        return 0;
    _Alignas(16) uint32 used = 0;
    uint32 encode[] = {object, 0, length, 1, encoding, loss & 255, 0, INT32_MAX, US_TO_SYN68K(&used)};
    if (!cfp_private(&reference, 0x419b8, encode, 9, &consumed) || (consumed != length && (!consumed || !(loss & 255))))
        return 0;
    uint32 size = CL(used);
    if (size > INT32_MAX)
        return 0;
    if (!allocator && !cfp_call("CFAllocatorGetDefault", NULL, 0, &allocator))
        return 0;
    uint32 allocate[] = {allocator, size ? size : 1, 0}, buffer;
    if (!cfp_call("CFAllocatorAllocate", allocate, 3, &buffer) || !buffer)
        return 0;
    encode[6] = buffer;
    encode[7] = size;
    if (!cfp_private(&reference, 0x419b8, encode, 9, &consumed))
        return 0;
    if (consumed == length || (consumed && (loss & 255)))
        result = ROMlib_cfdata_create_no_copy(allocator, buffer, CL(used), allocator);
    if (!result) {
        uint32 release[] = {allocator, buffer};
        cfp_call("CFAllocatorDeallocate", release, 2, &ignored);
    }
    return result;
}
