/*+===================================================================
  File:      encodings.c

  Summary:   CarbonLib 1.4 encoding queries and identifier conversion. Live mapping tables, converter caches and
             original external-codec dependencies remain shared.

  Classes:   None.

  Functions: ROMlib_carbon_cf_compatible_mac_encoding, ROMlib_carbon_cf_encoding_available,
             ROMlib_carbon_cf_encoding_to_ns, ROMlib_carbon_cf_encoding_to_windows,
             ROMlib_carbon_cf_maximum_encoding_size, ROMlib_carbon_cf_ns_to_encoding,
             ROMlib_carbon_cf_windows_to_encoding

  Origin:    Existing repository source in packages/carbonlib/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* CarbonLib 1.4 encoding queries and identifier conversion. Live mapping tables,
   converter caches and original external-codec dependencies remain shared. */
#include "internal.h"

static uint32 encoding_word(const void* data, size_t offset)
{
    uint32 word;
    memcpy(&word, (const Byte*)data + offset, 4);
    return CL(word);
}

static uint16 encoding_half(const void* data, size_t offset)
{
    uint16 word;
    memcpy(&word, (const Byte*)data + offset, 2);
    return CW(word);
}

static void encoding_store(void* data, size_t offset, uint32 value)
{
    value = CL(value);
    memcpy((Byte*)data + offset, &value, 4);
}

static int encoding_query(const char* name, uint32 offset, const uint32* arguments, unsigned count,
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

static int encoding_call(const char* name, const uint32* arguments, unsigned count, uint32* result)
{
    ROMlib_carbon_reference reference;
    return ROMlib_carbon_reference_function("CoreFoundationLib", name, &reference) &&
           ROMlib_carbon_invoke_callback(reference.function, arguments, count, result);
}

/* Call a still-original private dependency in the verified image. An aligned
   vector fits within one host-alias page; no guest module state is fabricated. */
static int encoding_private(const ROMlib_carbon_reference* reference, uint32 offset, const uint32* arguments,
                            unsigned count, uint32* result)
{
    if ((offset & 3) || offset >= reference->code_size)
        return 0;
    _Alignas(16) uint32 vector[2];
    encoding_store(vector, 0, encoding_word(reference->function, 0) - reference->code_offset + offset);
    encoding_store(vector, 4, US_TO_SYN68K(reference->toc));
    return ROMlib_carbon_invoke_callback(vector, arguments, count, result);
}

static uint32 encoding_builtin_definition(uint32 encoding)
{
    switch (encoding) {
    case UINT32_MAX:
    case 0x600:
        return 0x3d98;
    case 0x201:
        return 0x3dc8;
    case 0:
        return 0x3df8;
    case 0x500:
        return 0x3e28;
    case 0xb01:
        return 0x3e58;
    case 0x8000100:
        return 0x3e88;
    default:
        return 0;
    }
}

/* Shared definition lookup from 0x45a48/0x45d78. External definitions still
   come from the original TEC bridge at 0x4de58; cache operations run in C. */
static int encoding_definition(const ROMlib_carbon_reference* reference, uint32 encoding, uint32* definition)
{
    uint32 offset = encoding_builtin_definition(encoding);
    if (offset) {
        *definition = US_TO_SYN68K((const Byte*)reference->toc + offset);
        return 1;
    }
    uint32 dictionary = encoding_word(reference->toc, 0x9d8), arguments[] = {dictionary, encoding, 0, 0};
    if (dictionary) {
        if (!encoding_call("CFDictionaryGetValue", arguments, 2, definition))
            return 0;
        if (*definition)
            return 1;
    }
    if (!encoding_private(reference, 0x4de58, &encoding, 1, definition))
        return 0;
    if (!*definition)
        return 1;
    dictionary = encoding_word(reference->toc, 0x9d8);
    if (!dictionary) {
        const uint32 create[] = {0, 0, 0, 0};
        if (!encoding_call("CFDictionaryCreateMutable", create, 4, &dictionary))
            return 0;
        encoding_store((void*)reference->toc, 0x9d8, dictionary);
    }
    if (dictionary) {
        arguments[0] = dictionary;
        arguments[2] = *definition;
        if (!encoding_call("CFDictionarySetValue", arguments, 3, NULL))
            return 0;
    }
    return 1;
}

static int encoding_converter(const ROMlib_carbon_reference* reference, uint32 encoding, uint32* converter)
{
    uint32 definition;
    if (!encoding_definition(reference, encoding, &definition))
        return 0;
    if (!definition) {
        *converter = 0;
        return 1;
    }
    uint32 offset;
    switch (encoding_word(SYN68K_TO_US_CHECK0(definition), 0)) {
    case 0x600:
        offset = 0x50a0;
        break;
    case 0x201:
        offset = 0x50c8;
        break;
    case 0:
        offset = 0x53f8;
        break;
    case 0x500:
        offset = 0x54cc;
        break;
    case 0xb01:
        offset = 0x57f4;
        break;
    case 0x8000100:
        offset = 0x593c;
        break;
    default:
        return encoding_private(reference, 0x4dda0, &definition, 1, converter);
    }
    *converter = US_TO_SYN68K((const Byte*)reference->toc + offset);
    return 1;
}

LONGINT ROMlib_carbon_cf_maximum_encoding_size(LONGINT length, uint32 encoding)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {(uint32)length, encoding}, result = UINT32_MAX, factor = 1;
    if (!encoding_query("CFStringGetMaximumSizeForEncoding", 0x35638, arguments, 2, &reference, &result))
        return result;
    if (encoding == 0x100)
        factor = 2;
    else if (encoding == 0xbff || encoding == 0x8000100)
        factor = 6;
    else {
        uint32 group = encoding & 0xff00;
        if ((!group && ((encoding >= 1 && encoding <= 3) || encoding == 25)) ||
            (group == 0x400 && (encoding & 255) >= 32) || encoding == 0x510 || (group == 0x600 && encoding != 0x600) ||
            (group == 0xa00 && (encoding == 0xa01 || encoding == 0xa03 || encoding == 0xa05)))
            factor = 2;
        else if (group == 0x900)
            factor = encoding == 0x931 ? 4 : 2;
        /* The 1.4 group-0x800 branch tests the masked group, not the full
           encoding nibble, so its generic six/eight/three multipliers never run. */
    }
    return (uint32)length * factor;
}

static const struct {
    uint32 cf, ns;
} ns_encodings[] = {{0x600, 1},  {0xb01, 2},  {0x920, 3},  {0x8000100, 4}, {0x201, 5},  {0x21, 6},
                    {0xbff, 7},  {0x420, 8},  {0x202, 9},  {0x100, 10},    {0x502, 11}, {0x500, 12},
                    {0x503, 13}, {0x504, 14}, {0x501, 15}, {0x820, 21},    {0, 30}};

uint32 ROMlib_carbon_cf_encoding_to_ns(uint32 encoding)
{
    ROMlib_carbon_reference reference;
    uint32 result = UINT32_MAX;
    if (!encoding_query("CFStringConvertEncodingToNSStringEncoding", 0x439a0, &encoding, 1, &reference, &result))
        return result;
    for (unsigned i = 0; i < NELEM(ns_encodings); ++i)
        if (ns_encodings[i].cf == encoding)
            return ns_encodings[i].ns;
    return encoding | UINT32_C(0x80000000);
}

uint32 ROMlib_carbon_cf_ns_to_encoding(uint32 encoding)
{
    ROMlib_carbon_reference reference;
    uint32 result = UINT32_MAX;
    if (!encoding_query("CFStringConvertNSStringEncodingToEncoding", 0x43af8, &encoding, 1, &reference, &result))
        return result;
    for (unsigned i = 0; i < NELEM(ns_encodings); ++i)
        if (ns_encodings[i].ns == encoding)
            return ns_encodings[i].cf;
    return encoding & UINT32_C(0x80000000) ? encoding & UINT32_C(0x7fffffff) : UINT32_MAX;
}

uint32 ROMlib_carbon_cf_encoding_to_windows(uint32 encoding)
{
    ROMlib_carbon_reference reference;
    uint32 result = UINT32_MAX;
    if (!encoding_query("CFStringConvertEncodingToWindowsCodepage", 0x43bb8, &encoding, 1, &reference, &result))
        return result;
    if (encoding >= 0x400 && encoding <= 0x423)
        return encoding_half(reference.toc, 0x3cc0 + (encoding - 0x400) * 2);
    if (encoding >= 0x500 && encoding <= 0x510)
        return encoding_half(reference.toc, 0x3d08 + (encoding - 0x500) * 2);
    if (encoding < 255)
        return encoding + 10000;
    if ((encoding & 0xff00) == 0x200)
        return encoding + 0x6dae;
    switch (encoding) {
    case 0x8000100:
        return 65001;
    case 0x600:
        return 20127;
    case 0xa02:
        return 20866;
    case 0xc02:
        return 37;
    default:
        return UINT32_MAX;
    }
}

uint32 ROMlib_carbon_cf_windows_to_encoding(uint32 page)
{
    ROMlib_carbon_reference reference;
    uint32 result = UINT32_MAX;
    if (!encoding_query("CFStringConvertWindowsCodepageToEncoding", 0x43c88, &page, 1, &reference, &result))
        return result;
    if (page <= 1)
        return encoding_call("CFStringGetSystemEncoding", NULL, 0, &result) ? result : UINT32_MAX;
    if (page < 10000) {
        int low = 0, high = 30;
        while (low <= high) {
            int middle = low + (high - low) / 2;
            uint32 key = encoding_half(reference.toc, 0x3d1a + middle * 4);
            if (key == page)
                return encoding_half(reference.toc, 0x3d1c + middle * 4);
            if (key > page)
                high = middle - 1;
            else if (encoding_half(reference.toc, 0x3d1e + middle * 4) > page)
                return encoding_half(reference.toc, 0x3d1c + middle * 4);
            else
                low = middle + 1;
        }
        return UINT32_MAX;
    }
    if (page < 20000)
        return page - 10000;
    if (page - 0x6fae < 10)
        return page - 0x6dae;
    switch (page) {
    case 65001:
        return 0x8000100;
    case 20127:
        return 0x600;
    case 20866:
        return 0xa02;
    default:
        return UINT32_MAX;
    }
}

uint32 ROMlib_carbon_cf_encoding_available(uint32 encoding)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (!encoding_query("CFStringIsEncodingAvailable", 0x43300, &encoding, 1, &reference, &result))
        return result & 255;
    switch (encoding) {
    case 0:
    case 0x100:
    case 0x500:
    case 0x600:
    case 0xb01:
    case 0xbff:
    case 0x8000100:
        return 1;
    default:
        return encoding_converter(&reference, encoding, &result) && result != 0;
    }
}

uint32 ROMlib_carbon_cf_compatible_mac_encoding(uint32 encoding)
{
    ROMlib_carbon_reference reference;
    uint32 result = UINT32_MAX, definition;
    if (!encoding_query("CFStringGetMostCompatibleMacStringEncoding", 0x43da4, &encoding, 1, &reference, &result))
        return result;
    if (!encoding_definition(&reference, encoding, &definition))
        return UINT32_MAX;
    if (definition)
        result = encoding_word(SYN68K_TO_US_CHECK0(definition), 44);
    else if (encoding == 0x100 || encoding < 255)
        result = encoding;
    if (result == UINT32_MAX) {
        _Alignas(16) uint32 base = CL(UINT32_MAX);
        uint32 arguments[] = {encoding, US_TO_SYN68K(&base), 0}, ignored;
        if (!encoding_private(&reference, 0x43270, arguments, 3, &ignored))
            return UINT32_MAX;
        result = CL(base);
        if (result == 0xffff)
            result = UINT32_MAX;
    }
    return result;
}
