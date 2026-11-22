/*+===================================================================
  File:      bundle_state_test.c

  Summary:   Bundle ownership, metadata precedence and executable-load state tests.

  Classes:   None.

  Functions: ROMlib_carbon_bundle_state_tests, ROMlib_carbon_bundle_test_callback, ROMlib_carbon_bundle_test_reference

  Origin:    Existing repository source in packages/carbonlib/tests; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Bundle ownership, metadata precedence and executable-load state tests. */
#include "../src/internal.h"
#include "bundle_state_test.h"
#undef NDEBUG
#include <assert.h>

static const char* names[] = {"CFBundleGetTypeID",
                              "CFBundleCopyBundleURL",
                              "CFBundleGetInfoDictionary",
                              "CFBundleGetValueForInfoDictionaryKey",
                              "CFBundleGetIdentifier",
                              "CFBundleIsExecutableLoaded",
                              "CFBundleLoadExecutable",
                              "CFBundleGetPlugIn",
                              "CFRetain",
                              "CFRelease",
                              "CFGetAllocator",
                              "CFBundleGetLocalInfoDictionary",
                              "CFDictionaryGetValue",
                              "CFBundleCopyExecutableURL",
                              "CFSetRemoveValue",
                              "__CFStringMakeConstantString",
                              "CFLog"};
static const uint32 offsets[] = {0xd7e8,  0xdb20,  0xdd2c, 0xdedc,  0xdf74, 0xf184,  0xf18c,  0xf8d0, 0x533e4,
                                 0x53488, 0x53844, 0xdda8, 0x21e74, 0xf0f8, 0x1fab8, 0x38110, 0x403fc};
static Byte toc[0x72a8], bundle[80], descriptors[NELEM(names)][8], tokens[32];
static ROMlib_carbon_reference references[NELEM(names)];
static int active, reference_ok, fail_call, retain_changes_url;
static uint32 foreign_result, info_result, local_dictionary, local_value, global_value;
static uint32 executable[2], fragments[2], last_key, last_message;
static unsigned calls, executable_calls, fragment_calls, info_calls, retains, releases, removes, locks, unlocks,
    cleanups;
static uint32 trace[128];

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

static uint32 token(unsigned index)
{
    return US_TO_SYN68K(tokens + index);
}

int ROMlib_carbon_bundle_test_reference(const char* library, const char* name, ROMlib_carbon_reference* reference)
{
    if (!active)
        return -1;
    assert(!strcmp(library, "CoreFoundationLib"));
    for (unsigned i = 0; i < NELEM(names); ++i)
        if (!strcmp(name, names[i])) {
            *reference = references[i];
            return reference_ok;
        }
    assert(!"Unexpected bundle reference dependency");
    return 0;
}

int ROMlib_carbon_bundle_test_callback(void* procedure, const uint32_t* arguments, unsigned count, uint32_t* result)
{
    if (!active)
        return -1;
    uint32 code = word(procedure, 0) - 0x10000000;
    assert(calls < NELEM(trace));
    trace[calls++] = code;
    if ((int)calls == fail_call)
        return 0;
    unsigned i;
    for (i = 0; i < NELEM(offsets) && offsets[i] != code; ++i)
        ;
    if (i < 8) {
        assert(count == (!i ? 0 : i == 3 ? 2 : 1) && result);
        *result = foreign_result;
        return 1;
    }
    if (i == 8) {
        assert(count == 1 && result && arguments[0] == token(0));
        ++retains;
        if (retain_changes_url)
            store(bundle, 8, token(1));
        *result = arguments[0];
    }
    else if (i == 9) {
        assert(count == 1 && !result && (arguments[0] == token(0) || arguments[0] == token(1)));
        ++releases;
    }
    else if (i == 10) {
        assert(count == 1 && result && arguments[0] == US_TO_SYN68K(bundle));
        *result = token(2);
    }
    else if (i == 11) {
        assert(count == 1 && result && arguments[0] == US_TO_SYN68K(bundle));
        *result = local_dictionary;
    }
    else if (i == 12) {
        assert(count == 2 && result && (arguments[0] == token(3) || arguments[0] == token(4)));
        last_key = arguments[1];
        *result = arguments[0] == token(3) ? local_value : global_value;
    }
    else if (i == 13) {
        assert(count == 1 && result && arguments[0] == US_TO_SYN68K(bundle) && executable_calls < 2);
        *result = executable[executable_calls++];
    }
    else if (i == 14) {
        assert(count == 2 && !result && arguments[0] == token(5) && arguments[1] == US_TO_SYN68K(bundle));
        ++removes;
    }
    else if (i == 15) {
        assert(count == 1 && result);
        last_message = arguments[0];
        *result = token(6);
    }
    else if (i == 16) {
        assert(count == 3 && !result && arguments[0] == 21 && arguments[1] == token(6) &&
               arguments[2] == US_TO_SYN68K(bundle));
    }
    else {
        assert(word(procedure, 4) == US_TO_SYN68K(toc) && !((uintptr_t)procedure & 15));
        if (code == 0x13adc) {
            assert(count == 3 && result && arguments[0] == token(2) && arguments[1] == word(bundle, 8) &&
                   arguments[2] == bundle[16]);
            ++info_calls;
            *result = info_result;
        }
        else if (code == 0xfd2c) {
            assert(count == 4 && result && arguments[0] == token(2) && !arguments[2] && fragment_calls < 2);
            assert(arguments[3] == (fragment_calls ? 1 : word(bundle, 8) ? 0 : 1));
            assert(arguments[1] == (arguments[3] ? token(1) : token(0)));
            *result = fragments[fragment_calls++];
        }
        else if (code == 0xf4d0) {
            assert(!count && !result);
            ++cleanups;
        }
        else {
            assert((code == 0x1a98c || code == 0x1a990) && count == 1 && !result &&
                   arguments[0] == US_TO_SYN68K(toc + 0x830));
            if (code == 0x1a98c)
                ++locks;
            else
                ++unlocks;
        }
    }
    return 1;
}

static void reset(void)
{
    memset(bundle, 0, sizeof bundle);
    calls = executable_calls = fragment_calls = info_calls = retains = releases = removes = locks = unlocks = cleanups =
        0;
    fail_call = -1;
    retain_changes_url = 0;
    info_result = token(4);
    local_dictionary = local_value = global_value = last_key = last_message = 0;
    executable[0] = executable[1] = token(1);
    fragments[0] = fragments[1] = 0;
    store(bundle, 8, token(0));
    store(toc, 0x824, 0);
    toc[0x820] = 0;
}

void ROMlib_carbon_bundle_state_tests(void)
{
    active = reference_ok = 1;
    for (unsigned i = 0; i < NELEM(names); ++i) {
        store(descriptors[i], 0, 0x10000000 + offsets[i]);
        store(descriptors[i], 4, US_TO_SYN68K(toc));
        references[i] = (ROMlib_carbon_reference){descriptors[i], toc, offsets[i], 0x5dee0, 0, sizeof toc};
    }
    uint32 (*type_id)(void) = ROMlib_carbon_find_export(names[0], 2)->function;
    void* (*url)(void*) = ROMlib_carbon_find_export(names[1], 2)->function;
    void* (*info)(void*) = ROMlib_carbon_find_export(names[2], 2)->function;
    void* (*value)(void*, const void*) = ROMlib_carbon_find_export(names[3], 2)->function;
    void* (*identifier)(void*) = ROMlib_carbon_find_export(names[4], 2)->function;
    uint32 (*loaded)(void*) = ROMlib_carbon_find_export(names[5], 2)->function;
    uint32 (*load)(void*) = ROMlib_carbon_find_export(names[6], 2)->function;
    void* (*plugin)(void*) = ROMlib_carbon_find_export(names[7], 2)->function;
    reset();
    store(toc, 0x834, 0x12345678);
    store(toc, 0x227c, token(7));
    store(toc, 0x408, token(8));
    assert(type_id() == 0x12345678);
    for (unsigned i = 0; i < 256; ++i) {
        bundle[36] = bundle[76] = i;
        assert(loaded(bundle) == i && plugin(bundle) == (i ? bundle : NULL));
    }
    assert(!calls);
    assert(url(bundle) == tokens && retains == 1);
    retain_changes_url = 1;
    assert(url(bundle) == tokens + 1 && retains == 2);
    store(bundle, 8, 0);
    assert(!url(bundle) && retains == 2);
    reset();
    bundle[16] = 0xfe;
    assert(info(bundle) == tokens + 4 && info_calls == 1 && word(bundle, 20) == token(4));
    assert(info(bundle) == tokens + 4 && info_calls == 1);
    info_result = 0;
    store(bundle, 20, 0);
    assert(!info(bundle) && !info(bundle) && info_calls == 3);
    reset();
    assert(!value(NULL, tokens) && !value(bundle, NULL) && !calls);
    local_dictionary = token(3);
    local_value = token(9);
    assert(value(bundle, tokens + 7) == tokens + 9 && !info_calls && last_key == token(7));
    local_value = 0;
    global_value = token(10);
    assert(value(bundle, tokens + 7) == tokens + 10 && info_calls == 1);
    assert(identifier(bundle) == tokens + 10 && last_key == token(7));
    for (unsigned flag = 0; flag < 2; ++flag) {
        reset();
        bundle[36] = 0xff;
        store(toc, 0x824, flag ? token(5) : 0);
        assert(load(bundle) == 1 && releases == 1 && executable_calls == 1);
        assert(locks == 1 && unlocks == 1 && removes == flag && !cleanups && !fragment_calls);
    }
    reset();
    executable[0] = 0;
    bundle[36] = 1;
    assert(load(bundle) == 1 && word(bundle, 32) == 7 && !releases);
    const uint32 kinds[] = {0, 1, 6, 7, UINT32_MAX};
    for (unsigned i = 0; i < NELEM(kinds); ++i)
        for (unsigned busy = 0; busy < 2; ++busy) {
            reset();
            store(bundle, 32, kinds[i]);
            toc[0x820] = busy;
            fragments[0] = token(11);
            assert(load(bundle) == (kinds[i] == 1));
            assert(cleanups == !busy);
            if (kinds[i] == 1) {
                assert(word(bundle, 40) == token(11) && bundle[36] == 1 && fragment_calls == 1 && retains == 1 &&
                       releases == 2);
            }
            else
                assert(!fragment_calls && last_message == token(8) + (kinds[i] == 7 ? 0x3fe : 0x41c));
        }
    reset();
    store(bundle, 32, 1);
    fragments[1] = token(12);
    assert(load(bundle) == 1 && fragment_calls == 2 && executable_calls == 2 && releases == 3 &&
           word(bundle, 40) == token(12));
    reset();
    store(bundle, 32, 1);
    assert(!load(bundle) && fragment_calls == 2 && word(bundle, 32) == 6 && !bundle[36]);
    reset();
    store(bundle, 8, 0);
    store(bundle, 32, 1);
    fragments[0] = token(12);
    assert(load(bundle) == 1 && fragment_calls == 1 && !retains && releases == 2);
    /* Fail each callback in the two-attempt path, without continuing after failure. */
    for (unsigned failure = 1; failure <= 11; ++failure) {
        reset();
        store(bundle, 32, 1);
        fail_call = failure;
        assert(!load(bundle));
        assert(calls == failure);
    }
    foreign_result = token(17);
    typedef void* (*pointer_query)(void*);
    pointer_query pointers[] = {url, info, identifier, plugin};
    const unsigned pointer_indices[] = {1, 2, 4, 7};
    for (unsigned variant = 0; variant < 5; ++variant) {
        for (unsigned i = 0; i < 8; ++i) {
            ROMlib_carbon_reference original = references[i];
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
            reset();
            if (!i)
                assert(type_id() == foreign_result);
            else if (i == 3)
                assert(value(bundle, tokens) == tokens + 17);
            else if (i == 5)
                assert(loaded(bundle) == (foreign_result & 255));
            else if (i == 6)
                assert(load(bundle) == (foreign_result & 255));
            else
                for (unsigned j = 0; j < NELEM(pointers); ++j)
                    if (i == pointer_indices[j])
                        assert(pointers[j](bundle) == tokens + 17);
            references[i] = original;
        }
    }
    reference_ok = 0;
    assert(!type_id() && !url(bundle) && !info(bundle) && !identifier(bundle) && !plugin(bundle) && !loaded(bundle) &&
           !load(bundle) && !value(bundle, tokens));
    active = 0;
}
