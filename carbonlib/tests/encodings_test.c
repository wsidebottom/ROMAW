/*+===================================================================
  File:      encodings_test.c

  Summary:   Batch checks for encoding sizes, identifier mappings and converter lookup.

  Classes:   None.

  Functions: ROMlib_carbon_encoding_test_callback, ROMlib_carbon_encoding_test_reference, ROMlib_carbon_encoding_tests

  Origin:    Existing repository source in packages/carbonlib/tests; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Batch checks for encoding sizes, identifier mappings and converter lookup. */
#include "../src/internal.h"
#include "encodings_test.h"
#undef NDEBUG
#include <assert.h>

static const char* names[] = {"CFStringGetMaximumSizeForEncoding",
                              "CFStringConvertEncodingToNSStringEncoding",
                              "CFStringConvertNSStringEncodingToEncoding",
                              "CFStringConvertEncodingToWindowsCodepage",
                              "CFStringConvertWindowsCodepageToEncoding",
                              "CFStringIsEncodingAvailable",
                              "CFStringGetMostCompatibleMacStringEncoding",
                              "CFDictionaryGetValue",
                              "CFDictionaryCreateMutable",
                              "CFDictionarySetValue",
                              "CFStringGetSystemEncoding"};
static const uint32 offsets[] = {0x35638, 0x439a0, 0x43af8, 0x43bb8, 0x43c88, 0x43300,
                                 0x43da4, 0x21e74, 0x2198c, 0x224c8, 0x35524};
static Byte toc[0x72a8], descriptors[NELEM(names)][8], definition[48], dictionary[4], converter[48];
static ROMlib_carbon_reference references[NELEM(names)];
static int active, reference_ok, callback_ok, fail_call;
static uint32 arguments_seen[4], foreign_result, external_definition, external_converter, base_result;
static uint32 cache_keys[8], cache_values[8];
static unsigned cache_count, calls, private_calls, definitions, adapters, base_calls, dictionary_creates, sets;

static uint32 word(const void* memory, unsigned offset)
{
    uint32 value;
    memcpy(&value, (const Byte*)memory + offset, 4);
    return CL(value);
}

static void store(void* memory, unsigned offset, uint32 value)
{
    value = CL(value);
    memcpy((Byte*)memory + offset, &value, 4);
}

static void half(unsigned offset, uint16 value)
{
    value = CW(value);
    memcpy(toc + offset, &value, 2);
}

int ROMlib_carbon_encoding_test_reference(const char* library, const char* name, ROMlib_carbon_reference* reference)
{
    if (!active)
        return -1;
    assert(!strcmp(library, "CoreFoundationLib"));
    for (unsigned i = 0; i < NELEM(names); ++i)
        if (!strcmp(name, names[i])) {
            *reference = references[i];
            return reference_ok;
        }
    assert(!"Unexpected encoding reference dependency");
    return 0;
}

int ROMlib_carbon_encoding_test_callback(void* procedure, const uint32_t* arguments, unsigned count, uint32_t* result)
{
    if (!active)
        return -1;
    ++calls;
    assert(count <= NELEM(arguments_seen));
    if (count)
        memcpy(arguments_seen, arguments, count * sizeof *arguments);
    if (!callback_ok || (int)calls == fail_call)
        return 0;
    uint32 code = word(procedure, 0) - 0x10000000;
    if (code == 0x4de58 || code == 0x4dda0 || code == 0x43270) {
        ++private_calls;
        assert(word(procedure, 4) == US_TO_SYN68K(toc) && !((uintptr_t)procedure & 15));
        if (code == 0x4de58) {
            assert(count == 1 && result);
            ++definitions;
            *result = external_definition;
        }
        else if (code == 0x4dda0) {
            assert(count == 1 && result && arguments[0] == US_TO_SYN68K(definition));
            ++adapters;
            *result = external_converter;
        }
        else {
            assert(count == 3 && result && !arguments[2]);
            ++base_calls;
            store(SYN68K_TO_US(arguments[1]), 0, base_result);
            *result = (uint32)-2802;
        }
        return 1;
    }
    unsigned i;
    for (i = 0; i < NELEM(offsets) && offsets[i] != code; ++i)
        ;
    assert(i < NELEM(offsets));
    if (i < 7) {
        assert(count == (i ? 1 : 2) && result);
        *result = foreign_result;
    }
    else if (i == 7) {
        assert(count == 2 && result && arguments[0] == US_TO_SYN68K(dictionary));
        *result = 0;
        for (unsigned entry = 0; entry < cache_count; ++entry)
            if (cache_keys[entry] == arguments[1])
                *result = cache_values[entry];
    }
    else if (i == 8) {
        assert(count == 4 && result && !(arguments[0] | arguments[1] | arguments[2] | arguments[3]));
        ++dictionary_creates;
        *result = US_TO_SYN68K(dictionary);
    }
    else if (i == 9) {
        assert(count == 3 && !result && arguments[0] == US_TO_SYN68K(dictionary) && cache_count < NELEM(cache_keys));
        cache_keys[cache_count] = arguments[1];
        cache_values[cache_count++] = arguments[2];
        ++sets;
    }
    else {
        assert(!count && result);
        *result = 0x1234;
    }
    return 1;
}

void ROMlib_carbon_encoding_tests(void)
{
    active = reference_ok = callback_ok = 1;
    fail_call = -1;
    for (unsigned i = 0; i < NELEM(names); ++i) {
        store(descriptors[i], 0, 0x10000000 + offsets[i]);
        store(descriptors[i], 4, US_TO_SYN68K(toc));
        references[i] = (ROMlib_carbon_reference){descriptors[i], toc, offsets[i], 0x5dee0, 0, sizeof toc};
    }
    typedef uint32 (*query_function)(uint32);
    query_function queries[7] = {0};
    for (unsigned i = 1; i < 7; ++i)
        queries[i] = ROMlib_carbon_find_export(names[i], 2)->function;
    LONGINT (*maximum)(LONGINT, uint32) = ROMlib_carbon_find_export(names[0], 2)->function;

    static const struct {
        uint32 encoding, factor;
    } sizes[] = {{0, 1},         {1, 2},          {2, 2},          {3, 2},          {4, 1},         {24, 1},
                 {25, 2},        {26, 1},         {0x100, 2},      {0x101, 1},      {0x201, 1},     {0x400, 1},
                 {0x41f, 1},     {0x420, 2},      {0x4ff, 2},      {0x500, 1},      {0x50f, 1},     {0x510, 2},
                 {0x511, 1},     {0x600, 1},      {0x601, 2},      {0x6ff, 2},      {0x800, 1},     {0x820, 1},
                 {0x900, 2},     {0x930, 2},      {0x931, 4},      {0x932, 2},      {0xa00, 1},     {0xa01, 2},
                 {0xa02, 1},     {0xa03, 2},      {0xa05, 2},      {0xa06, 1},      {0xb01, 1},     {0xbff, 6},
                 {0x8000100, 6}, {0xffff0001, 1}, {0x80000420, 2}, {0xffff0931, 2}, {UINT32_MAX, 1}};

    const uint32 lengths[] = {0, 1, 2, 17, 0x10000000, 0x7fffffff, 0x80000000, UINT32_MAX};
    for (unsigned i = 0; i < NELEM(sizes); ++i)
        for (unsigned j = 0; j < NELEM(lengths); ++j)
            assert((uint32)maximum(lengths[j], sizes[i].encoding) == lengths[j] * sizes[i].factor);
    const uint32 ns[] = {UINT32_MAX, 0x600,      0xb01,      0x920,      0x8000100,  0x201,      0x21,       0xbff,
                         0x420,      0x202,      0x100,      0x502,      0x500,      0x503,      0x504,      0x501,
                         UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, 0x820,      UINT32_MAX, UINT32_MAX,
                         UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, 0};
    for (unsigned i = 0; i < NELEM(ns); ++i) {
        assert(queries[2](i) == ns[i]);
        if (ns[i] != UINT32_MAX)
            assert(queries[1](ns[i]) == i);
    }
    for (uint32 i = 0; i < 65536; i += 17) {
        assert(queries[1](0x10000000 | i) == (0x90000000 | i));
        assert(queries[2](0x80000000 | i) == i);
    }
    for (unsigned i = 0; i < 36; ++i)
        half(0x3cc0 + i * 2, 1000 + i);
    for (unsigned i = 0; i < 17; ++i)
        half(0x3d08 + i * 2, 2000 + i);
    for (unsigned i = 0; i < 30; ++i) {
        half(0x3d1a + i * 4, 100 + i * 10);
        half(0x3d1c + i * 4, 0x400 + i);
    }
    half(0x3d1a + 30 * 4, 0xffff);
    half(0x3d1c + 30 * 4, 0xffff);
    for (uint32 i = 0; i < 65536; ++i) {
        uint32 expected = UINT32_MAX;
        if (i <= 1)
            expected = 0x1234;
        else if (i < 10000 && i >= 100)
            expected = 0x400 + MIN((i - 100) / 10, 29U);
        else if (i >= 10000 && i < 20000)
            expected = i - 10000;
        else if (i >= 28590 && i < 28600)
            expected = i - 28078;
        else if (i == 20127)
            expected = 0x600;
        else if (i == 20866)
            expected = 0xa02;
        else if (i == 65001)
            expected = 0x8000100;
        assert(queries[4](i) == expected);
    }
    for (uint32 i = 0; i < 36; ++i)
        assert(queries[3](0x400 + i) == 1000 + i);
    for (uint32 i = 0; i < 17; ++i) {
        uint32 j = i - 9;
        uint32 expected = i < 9 ? 2000 + i : (j & 1) ? 0x400 + j / 2 : 100 + j / 2 * 10;
        assert(queries[3](0x500 + i) == expected);
    }
    assert(queries[3](254) == 10254 && queries[3](255) == UINT32_MAX);
    assert(queries[3](0x80000201) == 0x80006faf && queries[3](0x8000100) == 65001);
    const uint32 encodings[] = {UINT32_MAX, 0x600, 0x201, 0, 0x500, 0xb01, 0x8000100};
    const uint32 records[] = {0x3d98, 0x3d98, 0x3dc8, 0x3df8, 0x3e28, 0x3e58, 0x3e88};
    for (unsigned i = 0; i < NELEM(encodings); ++i) {
        store(toc, records[i], encodings[i] == UINT32_MAX ? 0x600 : encodings[i]);
        store(toc, records[i] + 44, 0x20 + i);
        assert(queries[5](encodings[i]) == 1 && queries[6](encodings[i]) == 0x20 + i);
    }
    assert(!private_calls && queries[5](0x100) == 1 && queries[5](0xbff) == 1);
    external_definition = US_TO_SYN68K(definition);
    external_converter = US_TO_SYN68K(converter);
    store(definition, 0, 0x12345678);
    store(definition, 44, 17);
    assert(queries[5](0x12345678) == 1 && definitions == 1 && adapters == 1 && sets == 1);
    assert(dictionary_creates == 1 && word(toc, 0x9d8) == US_TO_SYN68K(dictionary));
    assert(queries[6](0x12345678) == 17 && definitions == 1 && cache_count == 1);
    external_converter = 0;
    assert(queries[5](0x12345678) == 0 && adapters == 2 && definitions == 1);
    external_definition = 0;
    base_result = 0xffff;
    assert(queries[6](0x100) == 0x100 && queries[6](0x21) == 0x21);
    assert(queries[6](0xabcdef) == UINT32_MAX && base_calls == 1);
    base_result = 9;
    assert(queries[6](0xabcdef) == 9 && base_calls == 2);
    /* A known input can select a different live built-in definition. */
    store(toc, 0x3dc8, 0x8000100);
    unsigned old_adapters = adapters;
    assert(queries[5](0x201) == 1 && adapters == old_adapters);
    callback_ok = 0;
    assert(!queries[5](0xabcdef) && queries[6](0xabcdef) == UINT32_MAX && queries[4](0) == UINT32_MAX);
    callback_ok = 1;
    foreign_result = 0x89abcdef;
    for (unsigned i = 0; i < 7; ++i) {
        ROMlib_carbon_reference known = references[i];
        for (unsigned variant = 0; variant < 5; ++variant) {
            references[i] = known;
            if (!variant)
                ++references[i].code_offset;
            else if (variant == 1)
                ++references[i].code_size;
            else if (variant == 2)
                ++references[i].toc_offset;
            else if (variant == 3)
                --references[i].toc_size;
            else
                references[i].toc = NULL;
            uint32 answer = i ? queries[i](0xdeadbeef) : (uint32)maximum(-123, 0xdeadbeef);
            assert(answer == (i == 5 ? 0xef : foreign_result));
            assert(arguments_seen[i ? 0 : 1] == 0xdeadbeef);
        }
        references[i] = known;
    }
    reference_ok = 0;
    assert((uint32)maximum(1, 0) == UINT32_MAX);
    for (unsigned i = 1; i < 7; ++i)
        assert(queries[i](0) == (i == 5 ? 0 : UINT32_MAX));
    active = 0;
}
