/*+===================================================================
  File:      bundle_lifecycle_test.c

  Summary:   Original native bundle lifecycle dependency/ownership tests.

  Classes:   None.

  Functions: ROMlib_carbon_bundle_lifecycle_callback, ROMlib_carbon_bundle_lifecycle_reference,
             ROMlib_carbon_bundle_lifecycle_tests

  Origin:    Existing repository source in packages/carbonlib/tests; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Original native bundle lifecycle dependency/ownership tests. */
#include "../src/internal.h"
#include "bundle_lifecycle_test.h"
#undef NDEBUG
#include <assert.h>

static const struct {
    const char* name;
    uint32 offset;
} entries[] = {
    {"CFBundleCreate", 0xda30},
    {"CFBundleGetLocalInfoDictionary", 0xdda8},
    {"CFBundleUnloadExecutable", 0xf2c0},
    {"CFBundleGetVersionNumber", 0xe3cc},
    {"CFBundleGetDevelopmentRegion", 0xe4f8},
    {"CFBundleGetFunctionPointersForNames", 0xf6c4},
    {"CFBundleGetDataPointersForNames", 0xf84c},
    {"CFDictionaryGetValue", 0x21e74},
    {"CFRetain", 0x533e4},
    {"CFRelease", 0x53488},
    {"CFURLGetFileSystemRepresentation", 0xa78c},
    {"CFURLCreateFromFileSystemRepresentation", 0xa880},
    {"_CFGetFileProperties", 0x18ce4},
    {"_CFRuntimeCreateInstance", 0x53204},
    {"CFGetAllocator", 0x53844},
    {"__CFStringMakeConstantString", 0x38110},
    {"CFBundleCopyResourceURL", 0x11538},
    {"CFURLCreateDataAndPropertiesFromResource", 0x4590},
    {"CFPropertyListCreateFromXMLData", 0x2a2ec},
    {"CFStringGetTypeID", 0x36e98},
    {"CFGetTypeID", 0x53394},
    {"CFStringGetLength", 0x38858},
    {"CFStringGetCharacters", 0x38a0c},
    {"CFNumberCreate", 0x24d58},
    {"CFDictionarySetValue", 0x224c8},
    {"CFNumberGetTypeID", 0x24d50},
    {"CFNumberGetValue", 0x24fcc},
    {"CFDictionaryRemoveValue", 0x22708},
    {"CFSetRemoveValue", 0x1fab8},
    {"CFDictionaryApplyFunction", 0x21f78},
    {"CFArrayGetCount", 0x1bd6c},
    {"CFArrayGetValueAtIndex", 0x1c134},
    {"CFBundleGetFunctionPointerForName", 0xf5b0},
    {"CFBundleGetDataPointerForName", 0xf7bc},
};

static Byte toc[0x72a8], bundle[88], instance[88], tokens[256], vectors[NELEM(entries)][8];
static uint16 text[12];
static int active, reference_ok, foreign, fail_at;
static unsigned calls, releases, locks, unlocks, registrations, plugins, closes, reads, parses, array_gets;
static uint32 numeric, fallback, region, string_length, created_number, value_type, cached, canonical;
static uint32 file_error, detect, directory, mode, properties, create_ok, resource_url, read_ok, parsed, parse_error;
static uint32 main_region, count, seen[128], last_arguments[8], last_count;
static int plugin_clears, canonical_ok, apply_changes_cache;
static uint32 last_release;

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

static uint32 token(unsigned i)
{
    return US_TO_SYN68K(tokens + i * 4);
}

int ROMlib_carbon_bundle_lifecycle_reference(const char* library, const char* name, ROMlib_carbon_reference* reference)
{
    if (!active)
        return -1;
    assert(!strcmp(library, "CoreFoundationLib"));
    for (unsigned i = 0; i < NELEM(entries); ++i)
        if (!strcmp(name, entries[i].name)) {
            *reference = (ROMlib_carbon_reference){
                vectors[i], toc,       entries[i].offset, foreign || i >= NELEM(entries) - 2 ? 0x5dee4 : 0x5dee0,
                0,          sizeof toc};
            return reference_ok;
        }
    assert(!"Unknown lifecycle dependency");
    return 0;
}

int ROMlib_carbon_bundle_lifecycle_callback(void* procedure, const uint32* a, unsigned n, uint32* result)
{
    if (!active)
        return -1;
    uint32 code = word(procedure, 0) - 0x10000000;
    assert(calls < NELEM(seen));
    seen[calls++] = code;
    if (fail_at && calls >= (unsigned)fail_at)
        return 0;
    unsigned i;
    for (i = 0; i < NELEM(entries) && entries[i].offset != code; ++i)
        ;
    if (i < 7) {
        assert(foreign);
        last_count = n;
        memcpy(last_arguments, a, n * 4);
        *result = token(31);
        return 1;
    }
    if (i == NELEM(entries)) {
        assert(word(procedure, 4) == US_TO_SYN68K(toc) && !((uintptr_t)procedure & 15));
        switch (code) {
        case 0x1a98c:
        case 0x1a990:
            assert(n == 1 && a[0] == US_TO_SYN68K(toc + 0x830) && !result);
            if (code == 0x1a98c)
                ++locks;
            else
                ++unlocks;
            return 1;
        case 0x13738:
            assert(n == 2 && a[0] == canonical);
            *(Byte*)SYN68K_TO_US(a[1]) = 2;
            *result = detect;
            return 1;
        case 0x13adc:
            assert(n == 3 && a[0] == token(20));
            *result = token(0);
            return 1;
        case 0xc674:
            assert(n == 2 && a[0] == US_TO_SYN68K(instance) && !a[1]);
            ++registrations;
            cached = a[0];
            return 1;
        case 0x165ec:
            assert(n == 1 && a[0] == US_TO_SYN68K(instance));
            ++plugins;
            return 1;
        case 0x15098:
            assert(n == 1 && a[0] == US_TO_SYN68K(bundle));
            *result = main_region;
            return 1;
        case 0xf4d0:
            assert(!n);
            return 1;
        case 0x16b20:
            assert(n == 1 && a[0] == US_TO_SYN68K(bundle));
            if (plugin_clears)
                bundle[36] = 0;
            return 1;
        case 0x5a46c:
            assert(n == 1 && a[0] == token(24));
            ++closes;
            return 1;
        default:
            assert(!"Unexpected private lifecycle callback");
            return 0;
        }
    }
    const char* name = entries[i].name;
#define IS(s) (!strcmp(name, s))
    if (IS("CFDictionaryGetValue")) {
        assert(n == 2);
        if (a[0] == token(23))
            *result = cached;
        else {
            assert(a[0] == token(0));
            *result = a[1] == token(10) ? numeric : a[1] == token(11) ? fallback : region;
        }
    }
    else if (IS("CFRetain")) {
        assert(n == 1);
        *result = a[0];
    }
    else if (IS("CFRelease")) {
        assert(n == 1 && !result);
        ++releases;
        last_release = a[0];
    }
    else if (IS("CFURLGetFileSystemRepresentation")) {
        assert(n == 4 && a[0] == token(1) && a[1] == 1 && a[3] == 256);
        assert((a[2] & 0x1ffff) + a[3] <= 0x20000);
        strcpy((char*)SYN68K_TO_US(a[2]), "Bundle");
        *result = 1;
    }
    else if (IS("CFURLCreateFromFileSystemRepresentation")) {
        assert(n == 4 && a[0] == 0xfedcba98 && a[2] == 6 && a[3] == 1);
        assert(!strcmp((const char*)SYN68K_TO_US(a[1]), "Bundle"));
        *result = canonical_ok ? canonical : 0;
    }
    else if (IS("_CFGetFileProperties")) {
        assert(n == 8 && a[0] == 0xfedcba98 && a[1] == canonical && !a[4] && !a[6] && !a[7]);
        *(Byte*)SYN68K_TO_US(a[2]) = directory;
        store(SYN68K_TO_US(a[3]), 0, mode);
        store(SYN68K_TO_US(a[5]), 0, properties);
        *result = file_error;
    }
    else if (IS("_CFRuntimeCreateInstance")) {
        assert(n == 4 && a[0] == 0xfedcba98 && a[1] == 42 && a[2] == 80 && !a[3]);
        *result = create_ok ? US_TO_SYN68K(instance) : 0;
    }
    else if (IS("CFGetAllocator")) {
        assert(n == 1);
        *result = token(20);
    }
    else if (IS("__CFStringMakeConstantString")) {
        assert(n == 1);
        *result = token(21);
    }
    else if (IS("CFBundleCopyResourceURL")) {
        assert(n == 4 && a[0] == US_TO_SYN68K(bundle) && a[1] == token(21) && a[2] == token(21) && !a[3]);
        *result = resource_url;
    }
    else if (IS("CFURLCreateDataAndPropertiesFromResource")) {
        assert(n == 6 && a[0] == token(20) && a[1] == resource_url && !a[3] && !a[4]);
        ++reads;
        store(SYN68K_TO_US(a[2]), 0, token(22));
        *result = read_ok;
    }
    else if (IS("CFPropertyListCreateFromXMLData")) {
        assert(n == 4 && a[0] == token(20) && a[1] == token(22) && !a[2]);
        ++parses;
        store(SYN68K_TO_US(a[3]), 0, parse_error);
        *result = parsed;
    }
    else if (IS("CFStringGetTypeID")) {
        assert(!n);
        *result = 7;
    }
    else if (IS("CFNumberGetTypeID")) {
        assert(!n);
        *result = 8;
    }
    else if (IS("CFGetTypeID")) {
        assert(n == 1);
        *result = value_type;
    }
    else if (IS("CFStringGetLength")) {
        assert(n == 1);
        *result = string_length;
    }
    else if (IS("CFStringGetCharacters")) {
        assert(n == 4 && !a[1] && a[2] == string_length && a[2] <= 10);
        assert((a[3] & 0x1ffff) + a[2] * 2 <= 0x20000);
        memcpy(SYN68K_TO_US(a[3]), text, string_length * 2);
    }
    else if (IS("CFNumberCreate")) {
        assert(n == 3 && a[0] == token(20) && a[1] == 3);
        created_number = word(SYN68K_TO_US(a[2]), 0);
        *result = token(4);
    }
    else if (IS("CFNumberGetValue")) {
        assert(n == 3 && a[1] == 3);
        store(SYN68K_TO_US(a[2]), 0, created_number);
        *result = 1;
    }
    else if (IS("CFDictionarySetValue")) {
        assert(n == 3 && a[0] == token(0));
        if (a[1] == token(10))
            numeric = a[2];
        else {
            assert(a[1] == token(12));
            region = a[2];
        }
    }
    else if (IS("CFDictionaryRemoveValue")) {
        assert(n == 2 && a[0] == token(0));
        if (a[1] == token(10))
            numeric = 0;
        else {
            assert(a[1] == token(12));
            region = 0;
        }
    }
    else if (IS("CFSetRemoveValue")) {
        assert(n == 2 && a[1] == US_TO_SYN68K(bundle));
    }
    else if (IS("CFDictionaryApplyFunction")) {
        assert(n == 3 && a[0] == token(25) && a[1] == token(26) && a[2] == token(20));
        if (apply_changes_cache)
            store(bundle, 64, token(28));
    }
    else if (IS("CFArrayGetCount")) {
        assert(n == 1);
        *result = count;
    }
    else if (IS("CFArrayGetValueAtIndex")) {
        assert(n == 2 && a[1] < count);
        ++array_gets;
        *result = token(a[1]);
    }
    else if (IS("CFBundleGetFunctionPointerForName") || IS("CFBundleGetDataPointerForName")) {
        assert(n == 2);
        *result = a[1] == token(1) ? 0 : a[1];
    }
    else
        assert(!"Unhandled lifecycle dependency");
#undef IS
    return 1;
}

static void reset(void)
{
    memset(bundle, 0, sizeof bundle);
    memset(instance, 0xa5, sizeof instance);
    memset(toc, 0, sizeof toc);
    calls = releases = locks = unlocks = registrations = plugins = closes = reads = parses = array_gets = 0;
    foreign = fail_at = plugin_clears = apply_changes_cache = 0;
    last_release = 0;
    reference_ok = canonical_ok = 1;
    created_number = 0;
    numeric = cached = region = main_region = file_error = parse_error = 0;
    canonical = token(2);
    fallback = token(5);
    value_type = 7;
    string_length = 3;
    text[0] = CW('1');
    text[1] = CW('.');
    text[2] = CW('2');
    detect = directory = create_ok = read_ok = 1;
    mode = 0x4000;
    properties = token(3);
    resource_url = token(1);
    parsed = token(0);
    count = 3;
    store(bundle, 20, token(0));
    store(bundle, 32, 1);
    store(bundle, 40, token(24));
    store(toc, 0x2304, token(10));
    store(toc, 0x2280, token(11));
    store(toc, 0x2284, token(12));
    store(toc, 0x834, 42);
    store(toc, 0x404, token(26));
    store(toc, 0x82c, token(23));
}

void ROMlib_carbon_bundle_lifecycle_tests(void)
{
    active = 1;
    for (unsigned i = 0; i < NELEM(entries); ++i) {
        store(vectors[i], 0, 0x10000000 + entries[i].offset);
        store(vectors[i], 4, US_TO_SYN68K(toc));
    }
    reset();
    assert(ROMlib_carbon_cf_bundle_create(0xfedcba98, SYN68K_TO_US(token(1))) == instance);
    assert(word(instance, 0) == 0xa5a5a5a5 && word(instance, 4) == 0xa5a5a5a5);
    assert(word(instance, 8) == canonical && instance[16] == 2 && word(instance, 32) == 1);
    assert(word(instance, 20) == token(0) && !word(instance, 84) && registrations == 1 && plugins == 1);
    unsigned before = calls;
    assert(ROMlib_carbon_cf_bundle_create(0xfedcba98, SYN68K_TO_US(token(1))) == instance);
    assert(calls == before + 4 && registrations == 1 && locks == 2 && unlocks == 2);
    for (unsigned valid = 0; valid < 4; ++valid) {
        reset();
        detect = 0;
        directory = valid != 1;
        mode = valid == 2 ? 0x8000 : 0x4000;
        file_error = valid == 3 ? 1 : 0;
        assert(ROMlib_carbon_cf_bundle_create(0xfedcba98, SYN68K_TO_US(token(1))) == (valid ? NULL : instance));
        if (!valid)
            assert(instance[16] == 3 && word(instance, 12) == properties);
        else
            assert(releases == (valid == 3 ? 1 : 2));
    }
    reset();
    canonical = token(1);
    canonical_ok = 0;
    assert(ROMlib_carbon_cf_bundle_create(0xfedcba98, SYN68K_TO_US(token(1))) == instance);
    assert(word(instance, 8) == token(1));
    /* Allocation failure disposes the retained canonical URL. */
    reset();
    create_ok = 0;
    assert(!ROMlib_carbon_cf_bundle_create(0xfedcba98, SYN68K_TO_US(token(1))) && releases == 1);
    reset();
    assert(ROMlib_carbon_cf_bundle_local_info(bundle) == SYN68K_TO_US(parsed));
    assert(reads == 1 && parses == 1 && releases == 2 && word(bundle, 24) == parsed);
    assert(ROMlib_carbon_cf_bundle_local_info(bundle) == SYN68K_TO_US(parsed) && reads == 1);
    reset();
    parsed = 0;
    parse_error = token(27);
    assert(!ROMlib_carbon_cf_bundle_local_info(bundle) && releases == 3);
    assert(!ROMlib_carbon_cf_bundle_local_info(bundle) && reads == 2);
    reset();
    resource_url = 0;
    assert(!ROMlib_carbon_cf_bundle_local_info(bundle) && !reads && !releases);
    reset();
    read_ok = 0;
    assert(!ROMlib_carbon_cf_bundle_local_info(bundle) && reads == 1 && !parses && releases == 1);

    static const struct {
        const char* text;
        uint32 expected;
    } versions[] = {{"", 0},
                    {"1", 0x10008000},
                    {"1.", 0x01008000},
                    {"12", 0x12008000},
                    {"1.2.3", 0x01238000},
                    {"12.3.4b255", 0x123460ff},
                    {"1.2b256", 0},
                    {"1.2b", 0x01206000},
                    {".", 0x8000},
                    {"a", 0x4000},
                    {"1.2f1", 0},
                    {"1.2b12x", 0},
                    {"1.2.3.4", 0},
                    {"123", 0},
                    {"1.2b999", 0},
                    {"1.2a1", 0x01204001},
                    {"1.2d0", 0x01202000},
                    {"1.2.3b255x", 0},
                    {"12345678901", 0}};

    for (unsigned i = 0; i < NELEM(versions); ++i) {
        reset();
        string_length = strlen(versions[i].text);
        for (unsigned j = 0; j < string_length; ++j)
            text[j] = CW((uint16)versions[i].text[j]);
        assert(ROMlib_carbon_cf_bundle_version(bundle) == versions[i].expected);
        assert(created_number == versions[i].expected && numeric == token(4));
    }
    reset();
    numeric = token(4);
    value_type = 8;
    created_number = 0xfedcba98;
    assert(ROMlib_carbon_cf_bundle_version(bundle) == 0xfedcba98);
    reset();
    numeric = token(6);
    value_type = 9;
    assert(!ROMlib_carbon_cf_bundle_version(bundle) && !numeric);
    reset();
    numeric = fallback = 0;
    assert(!ROMlib_carbon_cf_bundle_version(bundle));
    reset();
    region = token(5);
    assert(ROMlib_carbon_cf_bundle_development(bundle) == SYN68K_TO_US(region));
    reset();
    region = token(5);
    value_type = 9;
    assert(!ROMlib_carbon_cf_bundle_development(bundle) && !region);
    reset();
    region = token(5);
    string_length = 0;
    assert(!ROMlib_carbon_cf_bundle_development(bundle) && !region);
    for (unsigned found = 0; found < 2; ++found) {
        reset();
        store(toc, 0x814, US_TO_SYN68K(bundle));
        main_region = found ? token(5) : 0;
        assert(ROMlib_carbon_cf_bundle_development(bundle) == SYN68K_TO_US(found ? token(5) : token(21)));
        assert(releases == found);
    }
    for (unsigned busy = 0; busy < 2; ++busy)
        for (unsigned kind = 0; kind < 3; ++kind) {
            reset();
            toc[0x820] = busy;
            bundle[36] = 1;
            store(bundle, 32, kind);
            store(bundle, 64, token(25));
            ROMlib_carbon_cf_bundle_unload(bundle);
            assert(closes == (kind == 1) && bundle[36] == (kind != 1));
            assert(locks == !busy && unlocks == !busy && releases == (kind == 1));
            assert(word(bundle, 64) == (kind == 1 ? 0 : token(25)));
        }
    reset();
    bundle[36] = 1;
    store(bundle, 64, token(25));
    apply_changes_cache = 1;
    ROMlib_carbon_cf_bundle_unload(bundle);
    assert(last_release == token(28) && !word(bundle, 64));
    reset();
    plugin_clears = 1;
    bundle[36] = 1;
    ROMlib_carbon_cf_bundle_unload(bundle);
    assert(!closes && !bundle[36]);
    reset();
    ROMlib_carbon_cf_bundle_unload(bundle);
    assert(calls == 1 && !closes);
    for (unsigned data = 0; data < 2; ++data) {
        reset();
        uint32 output[5];
        memset(output, 0xa5, sizeof output);
        void (*bulk)(void*, const void*, void*) =
            data ? ROMlib_carbon_cf_bundle_data_pointers : ROMlib_carbon_cf_bundle_functions;
        bulk(bundle, tokens, NULL);
        assert(!calls);
        bulk(bundle, tokens, output + 1);
        assert(array_gets == 3 && CL(output[1]) == token(0) && !output[2] && CL(output[3]) == token(2));
        assert(output[0] == 0xa5a5a5a5 && output[4] == 0xa5a5a5a5);
        count = 0;
        before = calls;
        bulk(bundle, tokens, output);
        assert(calls == before + 1);
    }
    /* Exercise every constructor dependency failure without publishing a new object. */
    reset();
    ROMlib_carbon_cf_bundle_create(0xfedcba98, SYN68K_TO_US(token(1)));
    unsigned steps = calls;
    for (unsigned at = 1; at <= steps; ++at) {
        reset();
        fail_at = at;
        assert(!ROMlib_carbon_cf_bundle_create(0xfedcba98, SYN68K_TO_US(token(1))) && calls == at);
    }
    reset();
    foreign = 1;
    assert(ROMlib_carbon_cf_bundle_create(0xffffffff, tokens) == SYN68K_TO_US(token(31)));
    assert(last_count == 2 && last_arguments[0] == 0xffffffff && last_arguments[1] == US_TO_SYN68K(tokens));
    assert(ROMlib_carbon_cf_bundle_local_info(bundle) == SYN68K_TO_US(token(31)));
    assert(ROMlib_carbon_cf_bundle_version(bundle) == token(31));
    assert(ROMlib_carbon_cf_bundle_development(bundle) == SYN68K_TO_US(token(31)));
    ROMlib_carbon_cf_bundle_unload(bundle);
    ROMlib_carbon_cf_bundle_functions(bundle, tokens, instance);
    assert(last_count == 3 && last_arguments[2] == US_TO_SYN68K(instance));
    ROMlib_carbon_cf_bundle_data_pointers(bundle, tokens, NULL);
    assert(last_count == 3 && !last_arguments[2]);
    reset();
    reference_ok = 0;
    assert(!ROMlib_carbon_cf_bundle_create(0, tokens) && !ROMlib_carbon_cf_bundle_version(bundle));
    ROMlib_carbon_cf_bundle_unload(bundle);
    assert(!calls);
    active = 0;
}
