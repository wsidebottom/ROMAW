/*+===================================================================
  File:      cfstrings_test.c

  Summary:   Original string comparison, record, cache and dependency protocol tests.

  Classes:   None.

  Functions: ROMlib_carbon_cfstrings_test_callback, ROMlib_carbon_cfstrings_test_reference,
             ROMlib_carbon_cfstrings_tests

  Origin:    Existing repository source in packages/carbonlib/tests; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Original string comparison, record, cache and dependency protocol tests. */
#include "../src/internal.h"
#include "cfstrings_test.h"
#undef NDEBUG
#include <assert.h>

typedef struct {
    _Alignas(256) Byte object[1024];
    uint16 text[256];
    unsigned length, owner;
    int references;
} test_string;

static const struct {
    const char* name;
    uint32 offset;
} entries[] = {
    {"CFStringCompare", 0x3a880},
    {"CFStringCompareWithOptions", 0x39fa8},
    {"__CFStringMakeConstantString", 0x38110},
    {"CFStringCreateCopy", 0x37d08},
    {"CFStringHasPrefix", 0x3b5ac},
    {"CFStringHasSuffix", 0x3b61c},
    {"CFStringFind", 0x3b4e0},
    {"CFStringGetIntValue", 0x3cfe0},
    {"CFStringGetLength", 0x38858},
    {"CFStringGetCharacters", 0x38a0c},
    {"CFStringFindWithOptions", 0x3a8f8},
    {"CFUniCharIsMemberOf", 0x461fc},
    {"CFAllocatorGetDefault", 0x17344},
    {"CFGetAllocator", 0x53844},
    {"CFRetain", 0x533e4},
    {"CFRelease", 0x53488},
    {"__CFStringComputeEightBitStringEncoding", 0x357c4},
    {"__CFStringCreateImmutableFunnel2", 0x36eb8},
    {"CFDictionaryCreateMutable", 0x2198c},
    {"CFDictionaryGetValue", 0x21e74},
    {"CFDictionarySetValue", 0x224c8},
    {"CFStringCreateWithCString", 0x37748},
    {"CFAllocatorAllocate", 0x1752c},
    {"CFStringCreateMutable", 0x38720},
    {"CFStringAppendFormat", 0x3d8b4},
    {"CFLog", 0x403fc},
};

static test_string strings[64];
static Byte toc[0x72a8], vectors[NELEM(entries)][8], messages[512], dictionary[32], copied[32], allocated_keys[16][256];

static struct {
    const char* key;
    test_string* value;
} cache[64];

static unsigned active, foreign, calls, fail_at, reference_ok, foreign_calls, range_calls, localized_calls, folds,
    decomposes;
static unsigned invalid_callbacks, oversize, cache_count, cache_creates, dict_null, create_null, unicode_factory,
    allocation_null;
static unsigned factories, releases, retains, allocations, logs, lookups, second_hit, encoding_calls, funnel_calls;
static uint32 foreign_answer, last_words[11], last_count, funnel[10], default_allocator;
static test_string* raced;

static uint32 word(const void* p, unsigned offset)
{
    uint32 v;
    memcpy(&v, (const Byte*)p + offset, 4);
    return CL(v);
}

static void store(void* p, unsigned offset, uint32 v)
{
    v = CL(v);
    memcpy((Byte*)p + offset, &v, 4);
}

static test_string* allocate_string(void)
{
    for (unsigned i = 0; i < NELEM(strings); ++i)
        if (!strings[i].references) {
            memset(&strings[i], 0, sizeof strings[i]);
            strings[i].references = 1;
            strings[i].owner = default_allocator;
            return strings + i;
        }
    assert(!"String test pool exhausted");
    return NULL;
}

static test_string* lookup(uint32 object)
{
    for (unsigned i = 0; i < NELEM(strings); ++i)
        if (US_TO_SYN68K(strings[i].object) == object) {
            assert(strings[i].references);
            return strings + i;
        }
    assert(!"Unknown test string");
    return NULL;
}

static test_string* text(const char* value)
{
    test_string* s = allocate_string();
    s->length = strlen(value);
    assert(s->length < 255);
    store(s->object, 4, 4);
    s->object[8] = s->length;
    memcpy(s->object + 9, value, s->length + 1);
    for (unsigned i = 0; i < s->length; ++i)
        s->text[i] = (Byte)value[i];
    return s;
}

static int equal_text(const test_string* s, const char* value)
{
    if (s->length != strlen(value))
        return 0;
    for (unsigned i = 0; i < s->length; ++i)
        if (s->text[i] != (Byte)value[i])
            return 0;
    return 1;
}

int ROMlib_carbon_cfstrings_test_reference(const char* library, const char* name, ROMlib_carbon_reference* reference)
{
    if (!active)
        return -1;
    assert(!strcmp(library, "CoreFoundationLib"));
    for (unsigned i = 0; i < NELEM(entries); ++i)
        if (!strcmp(name, entries[i].name)) {
            *reference = (ROMlib_carbon_reference){vectors[i], toc, entries[i].offset, foreign ? 0x5dee4 : 0x5dee0, 0,
                                                   sizeof toc};
            return reference_ok;
        }
    assert(!"Unknown string dependency");
    return 0;
}

int ROMlib_carbon_cfstrings_test_callback(void* procedure, const uint32* a, unsigned n, uint32* result)
{
    if (!active)
        return -1;
    ++calls;
    if (fail_at && calls >= fail_at)
        return 0;
    if (!procedure) {
        ++invalid_callbacks;
        return 0;
    }
    uint32 code = word(procedure, 0) - 0x10000000;
    unsigned index;
    for (index = 0; index < NELEM(entries) && entries[index].offset != code; ++index)
        ;
    if (index < 8) {
        assert(foreign);
        ++foreign_calls;
        last_count = n;
        if (n)
            memcpy(last_words, a, n * 4);
        if (index == 6 && a[0]) {
            store(SYN68K_TO_US(a[0]), 0, 7);
            store(SYN68K_TO_US(a[0]), 4, 2);
        }
        if (result)
            *result = foreign_answer;
        return 1;
    }
    if (index == NELEM(entries)) {
        assert(!((uintptr_t)procedure & 15) && word(procedure, 4) == US_TO_SYN68K(toc));
        if (code == 0x398b0) {
            assert(n == 6 && !a[5]);
            ++localized_calls;
            *result = 0xabcdef01;
            return 1;
        }
        if (code == 0x49358) {
            assert(n == 1);
            ++folds;
            *result = a[0] == 0xe9 ? 0xc9 : a[0] == 0xdf ? 'S' : a[0];
            return 1;
        }
        assert(code == 0x4617c && n == 3 && !a[0]);
        ++decomposes;
        uint16* out = (uint16*)SYN68K_TO_US(a[2]);
        out[0] = CW(a[1] == 0xe9 ? 'e' : a[1] == 0xc9 ? 'E' : 'A');
        out[1] = CW(a[1] == 0xc4 ? 0x308 : 0x301);
        *result = oversize ? 11 : 2;
        return 1;
    }
    const char* name = entries[index].name;
#define IS(s) (!strcmp(name, s))
    if (IS("CFStringGetLength")) {
        assert(n == 1);
        *result = lookup(a[0])->length;
    }
    else if (IS("CFStringGetCharacters")) {
        assert(n == 4);
        test_string* s = lookup(a[0]);
        assert(a[1] <= s->length && a[2] <= s->length - a[1]);
        uint16* out = (uint16*)SYN68K_TO_US(a[3]);
        assert(!((uintptr_t)out & 63));
        for (unsigned i = 0; i < a[2]; ++i)
            out[i] = CW(s->text[a[1] + i]);
        ++range_calls;
    }
    else if (IS("CFUniCharIsMemberOf")) {
        assert(n == 2 && a[1] == 9);
        *result = a[0] == 0xe9 || a[0] == 0xc9 || a[0] == 0xc4;
    }
    else if (IS("CFStringFindWithOptions")) {
        assert(n == 6);
        test_string* s = lookup(a[0]);
        test_string* pattern = lookup(a[1]);
        assert(!a[2] && a[3] == s->length);
        int found = -1;
        if (pattern->length && pattern->length <= s->length)
            for (unsigned at = 0; at + pattern->length <= s->length; ++at) {
                if ((a[4] & 8) && at != ((a[4] & 4) ? s->length - pattern->length : 0))
                    continue;
                if (!memcmp(s->text + at, pattern->text, pattern->length * 2)) {
                    found = at;
                    if (!(a[4] & 4))
                        break;
                }
            }
        if (found >= 0 && a[5]) {
            store(SYN68K_TO_US(a[5]), 0, found);
            store(SYN68K_TO_US(a[5]), 4, pattern->length);
        }
        *result = found >= 0 ? 255 : 0;
    }
    else if (IS("CFAllocatorGetDefault")) {
        assert(!n);
        *result = default_allocator;
    }
    else if (IS("CFGetAllocator")) {
        assert(n == 1);
        *result = lookup(a[0])->owner;
    }
    else if (IS("CFRetain")) {
        assert(n == 1);
        ++retains;
        ++lookup(a[0])->references;
        if (result)
            *result = a[0];
    }
    else if (IS("CFRelease")) {
        assert(n == 1 && !result);
        ++releases;
        --lookup(a[0])->references;
    }
    else if (IS("__CFStringComputeEightBitStringEncoding")) {
        assert(!n);
        ++encoding_calls;
        store(toc, 0x3a70, 0);
        if (result)
            *result = 0;
    }
    else if (IS("__CFStringCreateImmutableFunnel2")) {
        assert(n == 10);
        memcpy(funnel, a, sizeof funnel);
        ++funnel_calls;
        *result = US_TO_SYN68K(copied);
    }
    else if (IS("CFDictionaryCreateMutable")) {
        assert(n == 4 && !a[0] && !a[1] && a[3] == US_TO_SYN68K(toc) + 0x33f0);
        const void* keys = SYN68K_TO_US(a[2]);
        assert(!((uintptr_t)keys & 31) && !memcmp(keys, toc + 0x3a98, 24));
        ++cache_creates;
        *result = dict_null ? 0 : US_TO_SYN68K(dictionary);
    }
    else if (IS("CFDictionaryGetValue")) {
        assert(n == 2 && a[0] == US_TO_SYN68K(dictionary));
        ++lookups;
        if (second_hit && lookups == 2) {
            cache[cache_count].key = (const char*)raced->object + 9;
            cache[cache_count++].value = raced;
        }
        const char* key = (const char*)SYN68K_TO_US(a[1]);
        *result = 0;
        for (unsigned i = 0; i < cache_count; ++i)
            if (!strcmp(cache[i].key, key)) {
                *result = US_TO_SYN68K(cache[i].value->object);
                break;
            }
    }
    else if (IS("CFDictionarySetValue")) {
        assert(n == 3 && a[0] == US_TO_SYN68K(dictionary) && !result && cache_count < NELEM(cache));
        cache[cache_count].key = (const char*)SYN68K_TO_US(a[1]);
        cache[cache_count].value = lookup(a[2]);
        ++cache[cache_count++].value->references;
    }
    else if (IS("CFStringCreateWithCString")) {
        assert(n == 3 && !a[0] && !a[2]);
        ++factories;
        if (create_null) {
            *result = 0;
            return 1;
        }
        test_string* s = text((const char*)SYN68K_TO_US(a[1]));
        if (unicode_factory) {
            store(s->object, 4, 0x10);
            store(s->object, 8, s->length);
        }
        *result = US_TO_SYN68K(s->object);
    }
    else if (IS("CFAllocatorAllocate")) {
        assert(n == 3 && !a[0] && !a[2] && a[1] <= 256 && allocations < NELEM(allocated_keys));
        *result = allocation_null ? 0 : US_TO_SYN68K(allocated_keys[allocations]);
        ++allocations;
    }
    else if (IS("CFStringCreateMutable")) {
        assert(n == 2 && !a[0] && !a[1]);
        *result = US_TO_SYN68K(allocate_string()->object);
    }
    else if (IS("CFStringAppendFormat")) {
        assert(n == 4 && !a[1] && !result);
        test_string* s = lookup(a[0]);
        test_string* format = lookup(a[2]);
        if (equal_text(format, "%1c"))
            s->text[s->length++] = a[3];
        else {
            assert(equal_text(format, "\\%3o"));
            s->text[s->length++] = '\\';
            s->text[s->length++] = '0' + ((a[3] >> 6) & 7);
            s->text[s->length++] = '0' + ((a[3] >> 3) & 7);
            s->text[s->length++] = '0' + (a[3] & 7);
        }
    }
    else if (IS("CFLog")) {
        assert(n == 3 && !a[0] && !result && equal_text(lookup(a[1]), "warn:%@"));
        assert(equal_text(lookup(a[2]), "a\\200b"));
        ++logs;
    }
    else
        assert(!"Unhandled string dependency");
#undef IS
    return 1;
}

static void reset(void)
{
    memset(strings, 0, sizeof strings);
    memset(toc, 0, sizeof toc);
    memset(cache, 0, sizeof cache);
    memset(funnel, 0, sizeof funnel);
    foreign = calls = fail_at = foreign_calls = range_calls = localized_calls = folds = decomposes = invalid_callbacks =
        oversize = 0;
    cache_count = cache_creates = dict_null = create_null = unicode_factory = allocation_null = factories = releases =
        retains = allocations = logs = lookups = second_hit = encoding_calls = funnel_calls = 0;
    reference_ok = 1;
    default_allocator = 0x11112222;
    foreign_answer = 0xaabbccdd;
    for (unsigned i = 0; i < 24; ++i)
        toc[0x3a98 + i] = i + 1;
    store(toc, 0x5a4, US_TO_SYN68K(messages));
    strcpy((char*)messages + 0x5d, "\\%3o");
    strcpy((char*)messages + 0x62, "%1c");
    strcpy((char*)messages + 0x66, "warn:%@");
}

void ROMlib_carbon_cfstrings_tests(void)
{
    active = 1;
    for (unsigned i = 0; i < NELEM(entries); ++i) {
        store(vectors[i], 0, 0x10000000 + entries[i].offset);
        store(vectors[i], 4, US_TO_SYN68K(toc));
    }
    reset();
    test_string* a = text("abc");
    test_string* b = text("abd");
    assert(ROMlib_carbon_cf_string_compare(a->object, b->object, 0) == UINT32_MAX);
    assert(ROMlib_carbon_cf_string_compare(b->object, a->object, 0) == 1);
    assert(!ROMlib_carbon_cf_string_compare_options(a->object, a->object, 0, 3, 0));
    test_string* upper = text("ABC");
    assert(!ROMlib_carbon_cf_string_compare(a->object, upper->object, 1));
    test_string* long_a =
        text("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyz");
    test_string* long_b =
        text("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxzz");
    unsigned before = range_calls;
    assert(ROMlib_carbon_cf_string_compare(long_a->object, long_b->object, 0) == UINT32_MAX &&
           range_calls - before == 6);
    assert(!ROMlib_carbon_cf_string_compare_options(long_a->object, a->object, 0, 3, 0));
    test_string* sentinel = text("xb");
    sentinel->text[0] = 0xffff;
    test_string* empty = text("");
    assert(!ROMlib_carbon_cf_string_compare(sentinel->object, empty->object, 0));
    assert(ROMlib_carbon_cf_string_compare(a->object, b->object, 32) == 0xabcdef01 && localized_calls == 1);
    test_string* accented = text("x");
    accented->text[0] = 0xe9;
    test_string* decomposed = text("ex");
    decomposed->text[1] = 0x301;
    assert(!ROMlib_carbon_cf_string_compare(accented->object, decomposed->object, 16));
    assert(!ROMlib_carbon_cf_string_compare(accented->object, text("e")->object, 16));
    test_string* accented_upper = text("x");
    accented_upper->text[0] = 0xc9;
    assert(!ROMlib_carbon_cf_string_compare(accented->object, accented_upper->object, 1) && folds >= 2);
    assert(!ROMlib_carbon_cf_string_compare(decomposed->object, accented->object, 16));
    oversize = 1;
    assert(!ROMlib_carbon_cf_string_compare(accented->object, decomposed->object, 16) && invalid_callbacks == 1);
    oversize = 0;
    test_string* haystack = text("abxxab");
    test_string* pattern = text("ab");
    assert(ROMlib_carbon_cf_string_prefix(haystack->object, pattern->object) == 255);
    assert(ROMlib_carbon_cf_string_suffix(haystack->object, pattern->object) == 255);
    Byte range[12];
    memset(range, 0xa5, sizeof range);
    ROMlib_carbon_cf_string_find(range + 1, haystack->object, pattern->object, 4);
    assert(word(range + 1, 0) == 4 && word(range + 1, 4) == 2 && range[0] == 0xa5 && range[9] == 0xa5);
    ROMlib_carbon_cf_string_find(range + 1, haystack->object, text("none")->object, 0);
    assert(word(range + 1, 0) == UINT32_MAX && !word(range + 1, 4));

    const struct {
        const char* text;
        uint32 expected;
    } numbers[] = {
        {"", 0},
        {"abc", 0},
        {"+  12end", 12},
        {"- 2147483648x", 0x80000000},
        {"2147483647", 0x7fffffff},
        {"2147483648", 0x7fffffff},
        {"-2147483649", 0x80000000},
        {"9999999999999999999999999999999999999999999", 0x7fffffff},
        {"--2", 0},
        {"0x123", 0},
        {"  42.9", 42},
    };

    for (unsigned i = 0; i < NELEM(numbers); ++i)
        assert(ROMlib_carbon_cf_string_int(text(numbers[i].text)->object) == numbers[i].expected);
    test_string* spaces = text("x+ 9");
    spaces->text[0] = 0x200b;
    spaces->text[2] = 0;
    assert(ROMlib_carbon_cf_string_int(spaces->object) == 9);
    reset();
    a = text("abc");
    assert(ROMlib_carbon_cf_string_copy(0, a->object) == a->object && retains == 1 && a->references == 2);
    assert(ROMlib_carbon_cf_string_copy(default_allocator, a->object) == a->object && a->references == 3);
    for (unsigned unicode = 0; unicode < 2; ++unicode)
        for (unsigned external = 0; external < 2; ++external) {
            reset();
            a = text("abc");
            Byte storage[16] = "abc";
            uint32 flags = 1 | (unicode ? 0x10 : 0) | (external ? 0x20 : 0);
            store(a->object, 4, flags);
            if (external) {
                store(a->object, 8, US_TO_SYN68K(storage));
                store(a->object, 12, 3);
            }
            else {
                store(a->object, 8, 3);
                memcpy(a->object + 12, storage, 4);
            }
            assert(ROMlib_carbon_cf_string_copy(default_allocator, a->object) == copied && funnel_calls == 1 &&
                   !retains);
            assert(funnel[1] == US_TO_SYN68K(external ? storage : a->object + 12) && funnel[2] == (unicode ? 6 : 3));
            assert(funnel[3] == (unicode ? 0x100 : 0) && funnel[5] == unicode && !funnel[4] && !funnel[6] &&
                   !funnel[7] && !funnel[8] && funnel[9] == UINT32_MAX);
        }
    reset();
    a = text("abc");
    store(a->object, 4, 4);
    store(toc, 0x3a70, UINT32_MAX);
    assert(ROMlib_carbon_cf_string_copy(0x87654321, a->object) == copied && encoding_calls == 1 &&
           funnel[1] == US_TO_SYN68K(a->object + 9) && funnel[2] == 3);
    Byte external[16] = "external";
    store(a->object, 4, 0x20);
    store(a->object, 8, US_TO_SYN68K(external));
    store(a->object, 12, 0xffffff);
    assert(ROMlib_carbon_cf_string_copy(0x87654321, a->object) == copied && word(a->object, 12) == 8);
    reset();
    Byte literal[] = "unique native constant";
    void* constant = ROMlib_carbon_cf_string_constant(literal);
    assert(constant && cache_creates == 1 && factories == 1 && cache_count == 1 &&
           lookup(US_TO_SYN68K(constant))->references == 1);
    Byte same[] = "unique native constant";
    assert(ROMlib_carbon_cf_string_constant(same) == constant && factories == 1 && !retains);
    reset();
    unicode_factory = 1;
    constant = ROMlib_carbon_cf_string_constant(literal);
    assert(constant && allocations == 1 && !strcmp(cache[0].key, (char*)literal) && cache[0].value->references == 1);
    reset();
    raced = text("race");
    second_hit = 1;
    assert(ROMlib_carbon_cf_string_constant((const Byte*)"race") == raced->object && releases == 1 && factories == 1 &&
           cache_count == 1);
    reset();
    dict_null = 1;
    assert(!ROMlib_carbon_cf_string_constant(literal) && !factories && !word(toc, 0x98c));
    dict_null = 0;
    assert(ROMlib_carbon_cf_string_constant(literal) && cache_creates == 2);
    reset();
    create_null = 1;
    assert(!ROMlib_carbon_cf_string_constant(literal) && !cache_count);
    reset();
    unicode_factory = allocation_null = 1;
    assert(!ROMlib_carbon_cf_string_constant(literal) && releases == 1 && !cache_count);
    reset();
    Byte high[] = {'a', 0x80, 'b', 0};
    constant = ROMlib_carbon_cf_string_constant(high);
    assert(constant && logs == 1 && cache_count == 4);
    assert(ROMlib_carbon_cf_string_constant(high) == constant && logs == 1);
    reset();
    a = text("abc");
    b = text("abc");
    ROMlib_carbon_cf_string_compare(a->object, b->object, 0);
    unsigned steps = calls;
    for (unsigned i = 1; i <= steps; ++i) {
        reset();
        a = text("abc");
        b = text("abc");
        fail_at = i;
        assert(!ROMlib_carbon_cf_string_compare(a->object, b->object, 0) && calls == i);
    }
    reset();
    ROMlib_carbon_cf_string_constant(literal);
    steps = calls;
    for (unsigned i = 1; i <= steps; ++i) {
        reset();
        fail_at = i;
        assert(!ROMlib_carbon_cf_string_constant(literal) && calls == i);
    }
    reset();
    foreign = 1;
    a = text("abc");
    b = text("x");
    foreign_answer = US_TO_SYN68K(copied);
    assert(ROMlib_carbon_cf_string_compare(a->object, b->object, 0x81234567) == foreign_answer && last_count == 3 &&
           last_words[2] == 0x81234567);
    assert(ROMlib_carbon_cf_string_compare_options(a->object, b->object, 0x87654321, 0xfedcba98, 0x81234567) ==
               foreign_answer &&
           last_count == 5 && last_words[2] == 0x87654321 && last_words[3] == 0xfedcba98);
    assert(ROMlib_carbon_cf_string_constant(literal) == copied && last_count == 1 &&
           last_words[0] == US_TO_SYN68K(literal));
    assert(ROMlib_carbon_cf_string_copy(0xffffffff, a->object) == copied && last_count == 2 &&
           last_words[0] == 0xffffffff);
    assert(ROMlib_carbon_cf_string_prefix(a->object, b->object) == (foreign_answer & 255));
    assert(ROMlib_carbon_cf_string_suffix(a->object, b->object) == (foreign_answer & 255));
    assert(ROMlib_carbon_cf_string_int(a->object) == foreign_answer);
    ROMlib_carbon_cf_string_find(range + 1, a->object, b->object, 0x81234567);
    assert(last_count == 4 && last_words[0] == US_TO_SYN68K(range + 1) && last_words[3] == 0x81234567 &&
           word(range + 1, 0) == 7 && word(range + 1, 4) == 2);
    active = 0;
}
