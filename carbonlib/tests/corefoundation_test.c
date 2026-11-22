/*+===================================================================
  File:      corefoundation_test.c

  Summary:   CoreFoundation guest-layout, pointer translation and bounded range tests.

  Classes:   None.

  Functions: DisposPtr, ROMlib_carbon_call_cfm, ROMlib_carbon_corefoundation_tests, ROMlib_carbon_find_symbol,
             ROMlib_carbon_invoke_callback, ROMlib_carbon_load_reference, ROMlib_carbon_probe_reference,
             ROMlib_carbon_reference_function, _NewPtr_flags

  Origin:    Existing repository source in packages/carbonlib/tests; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* CoreFoundation guest-layout, pointer translation and bounded range tests. */
#include "../src/internal.h"
#include "encodings_test.h"
#include "bundle_state_test.h"
#include "bundle_lifecycle_test.h"
#include "urls_test.h"
#include "cfstrings_test.h"
#include "cf_values_test.h"
#undef NDEBUG
#include <assert.h>
#include <float.h>
#include <math.h>

extern uint32 ROMlib_carbon_test_mac_time;

static void store_word(Byte* object, unsigned offset, uint32 value)
{
    uint32 encoded = CL(value);
    memcpy(object + offset, &encoded, sizeof encoded);
}

static void* function(const char* name)
{
    const ROMlib_carbon_export* entry = ROMlib_carbon_find_export(name, 2);
    assert(entry && entry->reference_library && !strcmp(entry->reference_library, "CoreFoundationLib"));
    assert(ROMlib_carbon_find_library_export("CoreFoundationLib", name, 2) == entry);
    assert(!ROMlib_carbon_find_library_export("StdCLib", name, 2));
    return entry->function;
}

static uint32 callback_values[8], callback_calls, callback_fail_at;
static const uint32* search_values;
static unsigned search_count, comparison_calls, comparison_fail_at;
static ROMlib_carbon_reference allocator_reference;
static uint32 allocator_fallback;
static unsigned allocator_fallback_calls;
static int allocator_reference_ok, allocator_fallback_ok;
static uint32 preferred_arguments[3], preferred_result;
static unsigned preferred_calls;
static int preferred_ok;
static Byte preferred_procedure;
static Byte allocation_procedures[3];
static uint32 allocation_arguments[4], allocation_result;
static unsigned allocation_calls, allocation_kind, allocation_count;
static int allocation_ok;
static ROMlib_carbon_reference creation_references[2];
static Byte creation_delegates[2], creation_retain_procedure;
static uint32 creation_delegate_arguments[2], creation_delegate_results[2], creation_info, creation_input;
static unsigned creation_delegate_calls[2], creation_retain_calls;
static int creation_reference_ok, creation_delegate_ok, creation_retain_ok;
static Byte* creation_mutated_context;
static const char* ownership_names[] = {"CFRetain",
                                        "CFRelease",
                                        "CFGetRetainCount",
                                        "CFGetAllocator",
                                        "CFDictionaryGetValue",
                                        "CFDictionarySetValue",
                                        "CFDictionaryRemoveValue"};
static ROMlib_carbon_reference ownership_references[7];
static Byte ownership_procedures[7], ownership_finalize, ownership_info_release;
static uint32 ownership_dictionary, ownership_key, ownership_delegate_results[4], ownership_argument;
static unsigned ownership_calls[7], ownership_events[8], ownership_event_count;
static int ownership_reference_ok, ownership_delegate_ok, ownership_mode, ownership_callback_ok;
static int ownership_dictionary_fail;
static Byte* ownership_poison;
static Byte* ownership_mutated_object;
static ROMlib_carbon_reference array_creation_references[2];
static Byte array_creation_delegates[2], array_retain_procedures[3], array_initialize_procedure;
static uint32 array_delegate_arguments[4], array_delegate_results[2], array_original_allocator;
static unsigned array_retain_calls, array_initialize_calls, array_fail_at, array_delegate_calls[2];
static int array_reference_ok, array_delegate_ok, array_initialize_ok, array_change_retain;
static Byte* array_object;
static Byte* array_changed_callbacks;
static const char* string_reference_names[] = {"CFStringGetCharacterAtIndex", "CFStringGetCharacters"};
static const uint32 string_reference_offsets[] = {0x3891c, 0x38a0c};
static ROMlib_carbon_reference string_references[2];
static Byte string_delegates[2], string_converters[2];
static uint32 string_arguments[4], string_result;
static unsigned string_last, string_count, string_conversions, string_fail_at;
static int string_reference_ok, string_callback_ok, string_reject;
static Byte *string_mutated_toc, *string_mutated_input;
static uint32 string_converter_replacement;
static void* string_alternate_converter;
static void* string_utf8_mutated_toc;
static uint32 string_utf8_replacement;
static ROMlib_carbon_reference unichar_reference;
static Byte unichar_delegate;
static uint32 unichar_arguments[2], unichar_result;
static unsigned unichar_calls;
static int unichar_reference_ok, unichar_callback_ok;
static const char* bundle_names[] = {
    "CFBundleGetFunctionPointerForName", "CFBundleGetDataPointerForName", "CFBundleLoadExecutable",
    "CFStringGetPascalString",           "__CFStringMakeConstantString",  "CFLog"};
static ROMlib_carbon_reference bundle_references[6];
static Byte bundle_procedures[6], bundle_record[48], bundle_name, bundle_target;
static Byte bundle_scratch[256], bundle_pascal[256], bundle_messages[0x600];
static unsigned bundle_calls[6], bundle_allocations, bundle_disposals, bundle_lookups, bundle_lookup_class;
static uint32 bundle_load_result, bundle_convert_result, bundle_lookup_address, bundle_log_arguments[4];
static int bundle_fail_stage, bundle_alloc_fail, bundle_lookup_error, bundle_reference_missing;
static const char* pascal_names[] = {"CFStringGetPascalString", "__CFStringComputeEightBitStringEncoding",
                                     "__CFStringEncodeByteStream"};
static const char* encoding_names[] = {"CFStringGetBytes", "CFStringGetFastestEncoding"};
static ROMlib_carbon_reference encoding_references[2];
static Byte encoding_procedures[3];
static int encoding_active, encoding_reference_ok, encoding_callback_ok;
static uint32 encoding_arguments[9], encoding_result;
static unsigned encoding_calls[3];
static ROMlib_carbon_reference pascal_references[3];
static Byte pascal_procedures[3], pascal_scratch[4];
static int pascal_active, pascal_alloc_fail, pascal_reference_ok, pascal_fail_index;
static unsigned pascal_allocations, pascal_disposals, pascal_calls[3];
static uint32 pascal_arguments[9], pascal_delegate_result, pascal_encoding, pascal_chars, pascal_used;
static void *pascal_input, *pascal_output;
static LONGINT pascal_capacity, pascal_length;
static uint32 pascal_initialized_encoding;
static unsigned pascal_clean_prefix;
static unsigned pascal_output_offset = 1;
static uint32 pascal_mutated_info = UINT32_MAX;
static Byte stream_scratch[64], stream_utf8;
static Byte stream_byte_scratch[1028];
static unsigned stream_byte_allocations, stream_byte_disposals;
static int stream_byte_alloc_fail;
static void* stream_alternate_callback;
static unsigned stream_allocations, stream_disposals, stream_calls;
static int stream_alloc_fail, stream_callback_ok;
static uint32 stream_arguments[6], stream_result, stream_used;
static const char* borrow_names[] = {"CFStringGetCStringPtr", "CFStringGetPascalStringPtr",
                                     "__CFStringComputeEightBitStringEncoding"};
static ROMlib_carbon_reference borrow_references[3];
static Byte borrow_procedures[3], borrow_target;
static int borrow_active, borrow_reference_ok, borrow_callback_ok;
static unsigned borrow_calls[3];
static uint32 borrow_arguments[2], borrow_initialized_encoding, borrow_mutated_info;
static Byte* borrow_mutated_string;

Ptr _NewPtr_flags(Size size, boolean_t system, boolean_t clear)
{
    assert(system && !clear);
    if (size == 1028) {
        ++stream_byte_allocations;
        MemErr = CW(memFullErr);
        return stream_byte_alloc_fail ? NULL : (Ptr)stream_byte_scratch;
    }
    if (size == 64) {
        ++stream_allocations;
        MemErr = CW(memFullErr);
        return stream_alloc_fail ? NULL : (Ptr)stream_scratch;
    }
    if (size == 4) {
        ++pascal_allocations;
        MemErr = CW(memFullErr);
        return pascal_alloc_fail ? NULL : (Ptr)pascal_scratch;
    }
    assert(size == 256);
    ++bundle_allocations;
    MemErr = CW(memFullErr);
    return bundle_alloc_fail ? NULL : (Ptr)bundle_scratch;
}

void DisposPtr(Ptr pointer)
{
    if (pointer == (Ptr)stream_byte_scratch)
        ++stream_byte_disposals;
    else if (pointer == (Ptr)stream_scratch)
        ++stream_disposals;
    else if (pointer == (Ptr)pascal_scratch)
        ++pascal_disposals;
    else {
        assert(pointer == (Ptr)bundle_scratch);
        ++bundle_disposals;
    }
    MemErr = CW(paramErr);
}

int32_t ROMlib_carbon_find_symbol(uint32_t connection, const unsigned char* name, uint32_t* address,
                                  unsigned* symbol_class)
{
    assert(connection == 0x12345678 && name == bundle_scratch && !memcmp(name, bundle_pascal, 256));
    ++bundle_lookups;
    *address = bundle_lookup_address;
    *symbol_class = bundle_lookup_class;
    return bundle_lookup_error;
}

int ROMlib_carbon_load_reference(const char* library, const char* name, ROMlib_carbon_reference* reference)
{
    return ROMlib_carbon_probe_reference(library, name, reference);
}

int ROMlib_carbon_call_cfm(const char* name, ROMlib_carbon_ppc_frame* call)
{
    (void)name;
    (void)call;
    return 0;
}

int ROMlib_carbon_probe_reference(const char* library, const char* name, ROMlib_carbon_reference* reference)
{
    if (strcmp(library, "CoreFoundationLib"))
        return 0;
    return ROMlib_carbon_reference_function(library, name, reference);
}

int ROMlib_carbon_reference_function(const char* library, const char* name, ROMlib_carbon_reference* reference)
{
    assert(!strcmp(library, "CoreFoundationLib"));
    memset(reference, 0, sizeof *reference);
    int values_status = ROMlib_carbon_values_test_reference(name, reference);
    if (values_status >= 0)
        return values_status;
    int encoding_status = ROMlib_carbon_encoding_test_reference(library, name, reference);
    if (encoding_status >= 0)
        return encoding_status;
    int string_status = ROMlib_carbon_cfstrings_test_reference(library, name, reference);
    if (string_status != -1)
        return string_status;
    int url_status = ROMlib_carbon_url_test_reference(library, name, reference);
    if (url_status >= 0)
        return url_status;
    int lifecycle_status = ROMlib_carbon_bundle_lifecycle_reference(library, name, reference);
    if (lifecycle_status >= 0)
        return lifecycle_status;
    int bundle_status = ROMlib_carbon_bundle_test_reference(library, name, reference);
    if (bundle_status >= 0)
        return bundle_status;
    if (encoding_active)
        for (unsigned i = 0; i < NELEM(encoding_names); ++i)
            if (!strcmp(name, encoding_names[i])) {
                if (!encoding_reference_ok)
                    return 0;
                *reference = encoding_references[i];
                return 1;
            }
    if (borrow_active)
        for (unsigned i = 0; i < NELEM(borrow_names); ++i)
            if (!strcmp(name, borrow_names[i])) {
                if (!borrow_reference_ok)
                    return 0;
                *reference = borrow_references[i];
                return 1;
            }
    if (pascal_active)
        for (unsigned i = 0; i < NELEM(pascal_names); ++i)
            if (!strcmp(name, pascal_names[i])) {
                if (!pascal_reference_ok)
                    return 0;
                *reference = pascal_references[i];
                return 1;
            }
    for (unsigned i = 0; i < NELEM(bundle_names); ++i)
        if (!strcmp(name, bundle_names[i])) {
            if (bundle_reference_missing)
                return 0;
            *reference = bundle_references[i];
            return 1;
        }
    if (!strcmp(name, "CFUniCharIsMemberOf")) {
        if (unichar_reference_ok)
            *reference = unichar_reference;
        return unichar_reference_ok;
    }
    for (unsigned i = 0; i < NELEM(string_reference_names); ++i)
        if (!strcmp(name, string_reference_names[i])) {
            if (!string_reference_ok)
                return 0;
            *reference = string_references[i];
            return 1;
        }
    if (!strcmp(name, "CFArrayGetTypeID") || !strcmp(name, "CFArrayCreate")) {
        if (array_reference_ok)
            *reference = array_creation_references[!strcmp(name, "CFArrayCreate")];
        return array_reference_ok;
    }
    for (unsigned i = 0; i < 7; ++i)
        if (!strcmp(name, ownership_names[i])) {
            if (ownership_reference_ok)
                *reference = ownership_references[i];
            return ownership_reference_ok;
        }
    if (strcmp(name, "CFAllocatorGetDefault")) {
        unsigned index = !strcmp(name, "CFAllocatorGetTypeID") ? 0 : 1;
        assert(index == 0 || !strcmp(name, "CFAllocatorCreate"));
        if (creation_reference_ok)
            *reference = creation_references[index];
        return creation_reference_ok;
    }
    if (allocator_reference_ok)
        *reference = allocator_reference;
    return allocator_reference_ok;
}

static int search_rank(uint32 value)
{
    for (unsigned i = 0; i < search_count; ++i)
        if (value == search_values[i])
            return i * 2;
    assert(value >= 0x40000000 && value <= 0x40000000 + search_count);
    return (int)(value - 0x40000000) * 2 - 1;
}

int ROMlib_carbon_invoke_callback(void* procedure, const uint32_t* arguments, unsigned count, uint32_t* result)
{
    int values_status = ROMlib_carbon_values_test_callback(procedure, arguments, count, result);
    if (values_status >= 0)
        return values_status;
    int encoding_status = ROMlib_carbon_encoding_test_callback(procedure, arguments, count, result);
    if (encoding_status >= 0)
        return encoding_status;
    int string_status = ROMlib_carbon_cfstrings_test_callback(procedure, arguments, count, result);
    if (string_status != -1)
        return string_status;
    int url_status = ROMlib_carbon_url_test_callback(procedure, arguments, count, result);
    if (url_status >= 0)
        return url_status;
    int lifecycle_status = ROMlib_carbon_bundle_lifecycle_callback(procedure, arguments, count, result);
    if (lifecycle_status >= 0)
        return lifecycle_status;
    int bundle_status = ROMlib_carbon_bundle_test_callback(procedure, arguments, count, result);
    if (bundle_status >= 0)
        return bundle_status;
    for (unsigned i = 0; i < NELEM(borrow_names); ++i)
        if (procedure == &borrow_procedures[i]) {
            ++borrow_calls[i];
            if (i == 2) {
                assert(!count);
                store_word((Byte*)borrow_references[0].toc, 0x3a70, borrow_initialized_encoding);
                if (borrow_mutated_string)
                    store_word(borrow_mutated_string, 4, borrow_mutated_info);
                *result = borrow_initialized_encoding;
            }
            else {
                assert(count == 2);
                memcpy(borrow_arguments, arguments, sizeof borrow_arguments);
                *result = US_TO_SYN68K(&borrow_target);
            }
            return borrow_callback_ok;
        }
    if (procedure == &stream_utf8 || (stream_alternate_callback && procedure == stream_alternate_callback)) {
        assert(count == 6);
        ++stream_calls;
        memcpy(stream_arguments, arguments, sizeof stream_arguments);
        uint32 encoded = CL(stream_used);
        memcpy(SYN68K_TO_US_CHECK0(arguments[5]), &encoded, 4);
        *result = stream_result;
        return stream_callback_ok;
    }
    if (encoding_active)
        for (unsigned i = 0; i < NELEM(encoding_procedures); ++i)
            if (procedure == &encoding_procedures[i]) {
                assert(count == (i == 1 ? 1 : 9) && result);
                memcpy(encoding_arguments, arguments, count * sizeof *arguments);
                ++encoding_calls[i];
                *result = encoding_result;
                return encoding_callback_ok;
            }
    for (unsigned i = 0; i < NELEM(pascal_names); ++i)
        if (procedure == &pascal_procedures[i] ||
            (pascal_active && i == 2 && procedure == pascal_references[i].function)) {
            ++pascal_calls[i];
            if (i == 1) {
                assert(!count);
                uint32 encoded = CL(pascal_initialized_encoding);
                memcpy((Byte*)pascal_references[0].toc + 0x3a70, &encoded, 4);
                if (pascal_mutated_info != UINT32_MAX) {
                    encoded = CL(pascal_mutated_info);
                    memcpy((Byte*)pascal_input + 4, &encoded, 4);
                }
                *result = 0;
            }
            else {
                assert(count == (i ? 9 : 4));
                memcpy(pascal_arguments, arguments, count * sizeof *arguments);
                if (!i)
                    *result = pascal_delegate_result;
                else {
                    assert(arguments[0] == US_TO_SYN68K(pascal_input) && !arguments[1] &&
                           arguments[2] == (uint32)pascal_length && !arguments[3] && arguments[4] == pascal_encoding &&
                           !arguments[5] && arguments[6] == US_TO_SYN68K((Byte*)pascal_output + pascal_output_offset) &&
                           arguments[7] == (uint32)pascal_capacity - 1 && arguments[8] == US_TO_SYN68K(pascal_scratch));
                    for (unsigned j = 0; j < pascal_clean_prefix; ++j)
                        assert(((Byte*)pascal_output)[j + 1] == 0xa5);
                    pascal_clean_prefix = 0;
                    uint32 written = pascal_used < (uint32)pascal_capacity ? pascal_used : (uint32)pascal_capacity - 1;
                    for (uint32 j = 0; j < written; ++j)
                        ((Byte*)pascal_output)[j + pascal_output_offset] = (Byte)(j * 7);
                    uint32 encoded = CL(pascal_used);
                    memcpy(pascal_scratch, &encoded, 4);
                    MemErr = CW(-321);
                    *result = pascal_chars;
                }
            }
            return (int)i != pascal_fail_index;
        }
    for (unsigned i = 0; i < NELEM(bundle_names); ++i)
        if (procedure == &bundle_procedures[i]) {
            ++bundle_calls[i];
            if ((int)i == bundle_fail_stage)
                return 0;
            if (i < 2) {
                assert(count == 2 && arguments[0] == US_TO_SYN68K(bundle_record) &&
                       arguments[1] == US_TO_SYN68K(&bundle_name));
                *result = US_TO_SYN68K(&bundle_target);
            }
            else if (i == 2) {
                assert(count == 1 && arguments[0] == US_TO_SYN68K(bundle_record));
                bundle_record[36] = (Byte)bundle_load_result;
                *result = bundle_load_result;
            }
            else if (i == 3) {
                assert(count == 4 && arguments[0] == US_TO_SYN68K(&bundle_name) &&
                       arguments[1] == US_TO_SYN68K(bundle_scratch) && arguments[2] == 255 && arguments[3] == 0x600);
                memcpy(bundle_scratch, bundle_pascal, 256);
                *result = bundle_convert_result;
            }
            else if (i == 4) {
                assert(count == 1 &&
                       arguments[0] == US_TO_SYN68K(bundle_messages + (bundle_lookup_error ? 0x50f : 0x52c)));
                *result = 0xdeadbeef;
            }
            else {
                assert(count == 4);
                memcpy(bundle_log_arguments, arguments, sizeof bundle_log_arguments);
                *result = 0;
            }
            return 1;
        }
    if (procedure == &unichar_delegate) {
        assert(count == 2 && result);
        memcpy(unichar_arguments, arguments, sizeof unichar_arguments);
        *result = unichar_result;
        ++unichar_calls;
        return unichar_callback_ok;
    }
    for (unsigned i = 0; i < NELEM(string_delegates); ++i)
        if (procedure == &string_delegates[i]) {
            string_last = i;
            string_count = count;
            assert(count <= 4);
            memcpy(string_arguments, arguments, count * sizeof *arguments);
            if (result)
                *result = string_result;
            return string_callback_ok;
        }
    for (unsigned i = 0; i < 2; ++i)
        if (procedure == &string_converters[i] ||
            (!i && string_alternate_converter && procedure == string_alternate_converter)) {
            assert(count == 3 && result && !arguments[0] && arguments[1] >= 128 && arguments[1] <= 255);
            ++string_conversions;
            if (string_fail_at && string_fail_at == string_conversions)
                return 0;
            uint16 encoded = CW(arguments[1] + 0x100 + i * 0x100);
            memcpy(SYN68K_TO_US(arguments[2]), &encoded, sizeof encoded);
            *result = string_reject ? 0x100 : 1;
            if (string_utf8_mutated_toc) {
                store_word(string_utf8_mutated_toc, 0x9c4, string_utf8_replacement);
                string_utf8_mutated_toc = NULL;
            }
            if (string_mutated_toc) {
                store_word(string_mutated_toc, 0x6f5c,
                           string_converter_replacement ? string_converter_replacement
                                                        : US_TO_SYN68K(&string_converters[1]));
                string_mutated_input[1] = 0xfe;
            }
            return 1;
        }
    if (procedure == &array_initialize_procedure) {
        assert(count == 1 && !result && arguments[0] == US_TO_SYN68K(array_object));
        ++array_initialize_calls;
        if (array_changed_callbacks) {
            store_word(array_changed_callbacks, 0, 0xfedcba98);
            store_word(array_changed_callbacks, 4, US_TO_SYN68K(&array_retain_procedures[1]));
        }
        return array_initialize_ok;
    }
    for (unsigned i = 0; i < 3; ++i)
        if (procedure == &array_retain_procedures[i]) {
            assert(count == 2 && result && arguments[0] == array_original_allocator);
            ++array_retain_calls;
            *result = arguments[1] ^ (i == 2 ? 0 : i == 1 ? 0xaaaaaaaa : 0x55555555);
            if (array_change_retain && array_retain_calls == 1)
                store_word(array_object, 16, US_TO_SYN68K(&array_retain_procedures[1]));
            return !array_fail_at || array_retain_calls != array_fail_at;
        }
    for (unsigned i = 0; i < 2; ++i)
        if (procedure == &array_creation_delegates[i]) {
            assert(count == i * 4 && result);
            ++array_delegate_calls[i];
            if (i)
                memcpy(array_delegate_arguments, arguments, sizeof array_delegate_arguments);
            *result = array_delegate_results[i];
            return array_delegate_ok;
        }
    if (procedure == &ownership_finalize || procedure == &ownership_info_release) {
        assert(count == 1 && arguments && !result);
        assert(ownership_event_count < 8);
        ownership_events[ownership_event_count++] = procedure == &ownership_finalize ? 1 : 3;
        ownership_argument = arguments[0];
        if (procedure == &ownership_finalize && ownership_mutated_object)
            store_word(ownership_mutated_object, 4, 0x780);
        return ownership_callback_ok;
    }
    for (unsigned i = 0; i < 7; ++i)
        if (procedure == &ownership_procedures[i]) {
            ++ownership_calls[i];
            if (i < 4) {
                assert(count == 1 && arguments);
                ownership_argument = arguments[0];
                if (result)
                    *result = ownership_delegate_results[i];
                return ownership_delegate_ok;
            }
            assert(count == (i == 5 ? 3 : 2) && arguments[0] == 0xabc00001 && arguments[1] == ownership_key);
            if ((int)i == ownership_dictionary_fail)
                return 0;
            if (i == 4) {
                assert(result);
                *result = ownership_dictionary;
            }
            else {
                assert(!result);
                ownership_dictionary = i == 5 ? arguments[2] : 0;
            }
            return 1;
        }
    if (procedure == &creation_retain_procedure) {
        assert(count == 1 && arguments && result);
        ++creation_retain_calls;
        creation_input = arguments[0];
        if (creation_mutated_context) {
            store_word(creation_mutated_context, 0, 0x87654321);
            store_word(creation_mutated_context, 8, 0);
        }
        *result = creation_info;
        return creation_retain_ok;
    }
    for (unsigned i = 0; i < 2; ++i)
        if (procedure == &creation_delegates[i]) {
            assert(count == i * 2 && result);
            ++creation_delegate_calls[i];
            if (i)
                memcpy(creation_delegate_arguments, arguments, sizeof creation_delegate_arguments);
            *result = creation_delegate_results[i];
            return creation_delegate_ok;
        }
    for (unsigned i = 0; i < 4; ++i)
        if (procedure == (i == 3 ? NULL : &allocation_procedures[i])) {
            ++allocation_calls;
            allocation_kind = i;
            allocation_count = count;
            if (ownership_mode && i == 2) {
                assert(ownership_event_count < 8);
                ownership_events[ownership_event_count++] = 2;
                if (ownership_poison)
                    memset(ownership_poison, 0xdd, 48);
            }
            assert(count <= 4 && arguments);
            memcpy(allocation_arguments, arguments, count * sizeof *arguments);
            if (result)
                *result = allocation_result;
            return i != 3 && allocation_ok;
        }
    if (procedure == &preferred_procedure) {
        assert(count == 3 && result);
        memcpy(preferred_arguments, arguments, sizeof preferred_arguments);
        ++preferred_calls;
        *result = preferred_result;
        return preferred_ok;
    }
    if (!count) {
        assert(procedure == (void*)(uintptr_t)0x9abc && !arguments && result);
        ++allocator_fallback_calls;
        *result = allocator_fallback;
        return allocator_fallback_ok;
    }
    if (count == 3) {
        assert(procedure == (void*)(uintptr_t)0x5678 && result && arguments[2] == 0x80000002);
        ++comparison_calls;
        int first = search_rank(arguments[0]), second = search_rank(arguments[1]);
        *result = first < second ? (uint32)-7 : first > second ? 9 : 0;
        return !comparison_fail_at || comparison_calls != comparison_fail_at;
    }
    assert(procedure == (void*)(uintptr_t)0x1234 && count == 2 && !result);
    assert(arguments[1] == 0x80000001 && callback_calls < 8);
    callback_values[callback_calls++] = arguments[0];
    return !callback_fail_at || callback_calls != callback_fail_at;
}

static void check_search(const Byte* array, const uint32* values, unsigned count)
{
    LONGINT (*search)(const void*, LONGINT, LONGINT, uint32, void*, uint32) = function("CFArrayBSearchValues");
    assert(ROMlib_carbon_find_export("CFArrayBSearchValues", 2)->flags == ROMLIB_CARBON_PPC_ONLY);
    search_values = values;
    search_count = count;
    uint32 encoded_flags;
    memcpy(&encoded_flags, array + 4, sizeof encoded_flags);
    BOOLEAN storage = (CL(encoded_flags) & 3) == 3;
    /* Verified CFStorage minimum offsets when a nonzero range starts with
       a value equal to the key. Outside-range keys retain the normal bounds. */
    const unsigned minimum[] = {0, 1, 1, 2, 1, 2, 3, 4, 1};
    for (unsigned start = 0; start <= count; ++start)
        for (unsigned length = 0; length <= count - start; ++length)
            for (unsigned query = 0; query < count * 2 + 1; ++query) {
                uint32 needle = query < count ? values[query] : 0x40000000 + query - count;
                int rank = search_rank(needle);
                unsigned expected = start;
                while (expected < start + length && search_rank(values[expected]) < rank)
                    ++expected;
                if (storage && start && length && rank >= search_rank(values[start]) &&
                    rank <= search_rank(values[start + length - 1]) && expected < start + minimum[length])
                    expected = start + minimum[length];
                comparison_calls = comparison_fail_at = 0;
                assert(search(array, start, length, needle, (void*)(uintptr_t)0x5678, 0x80000002) == expected);
                assert(length || !comparison_calls);
            }
    comparison_calls = 0;
    comparison_fail_at = 2;
    assert(search(array, 0, count, values[0], (void*)(uintptr_t)0x5678, 0x80000002) == 0);
    assert(comparison_calls == 2);
    comparison_calls = comparison_fail_at = 0;
    assert(search(array, count, 0, 0, NULL, 0) == count);
    assert(search(NULL, 1, 1, 0, NULL, 0) == 1);
    assert(search(array, -1, 1, 0, NULL, 0) == -1);
    assert(search(array, 0, -1, 0, NULL, 0) == 0);
    assert(search(array, INT32_MAX, INT32_MAX, 0, NULL, 0) == INT32_MAX);
    assert(!comparison_calls);
}

static void check_array(const Byte* array, const uint32* values, unsigned count)
{
    check_search(array, values, count);
    uint32 (*at)(const void*, LONGINT) = function("CFArrayGetValueAtIndex");
    void (*copy)(const void*, LONGINT, LONGINT, void*) = function("CFArrayGetValues");
    void (*apply)(const void*, LONGINT, LONGINT, void*, uint32) = function("CFArrayApplyFunction");
    assert(ROMlib_carbon_find_export("CFArrayApplyFunction", 2)->flags == ROMLIB_CARBON_PPC_ONLY);
    Byte output[40];
    assert(count <= 8);
    for (unsigned i = 0; i < count; ++i)
        assert(at(array, i) == values[i]);
    assert(!at(array, -1) && !at(array, count) && !at(array, INT32_MAX));
    for (unsigned start = 0; start <= count; ++start)
        for (unsigned length = 0; length <= count - start; ++length) {
            memset(output, 0xa5, sizeof output);
            copy(array, start, length, output + 1);
            for (unsigned i = 0; i < length; ++i) {
                uint32 encoded;
                memcpy(&encoded, output + 1 + i * 4, 4);
                assert(CL(encoded) == values[start + i]);
            }
            assert(output[0] == 0xa5 && output[length * 4 + 1] == 0xa5);
            callback_calls = callback_fail_at = 0;
            apply(array, start, length, (void*)(uintptr_t)0x1234, 0x80000001);
            assert(callback_calls == length);
            for (unsigned i = 0; i < length; ++i)
                assert(callback_values[i] == values[start + i]);
        }
    callback_calls = 0;
    callback_fail_at = 2;
    apply(array, 0, count, (void*)(uintptr_t)0x1234, 0x80000001);
    assert(callback_calls == (count < 2 ? count : 2));
    callback_calls = callback_fail_at = 0;
    apply(array, -1, 1, NULL, 0);
    apply(array, 0, -1, NULL, 0);
    apply(array, count, 1, NULL, 0);
    apply(array, INT32_MAX, INT32_MAX, NULL, 0);
    apply(NULL, 0, 1, NULL, 0);
    assert(!callback_calls);
    memset(output, 0xa5, sizeof output);
    copy(array, -1, 1, output);
    copy(array, 0, -1, output);
    copy(array, count, 1, output);
    copy(array, INT32_MAX, INT32_MAX, output);
    for (unsigned i = 0; i < sizeof output; ++i)
        assert(output[i] == 0xa5);
}

static void array_readers_for(const uint32* values)
{
    Byte array[96], deque[4 + (255 + 8) * 4], storage[64], branch[20], leaves[4][20], encoded_values[32];
    for (unsigned i = 0; i < 8; ++i)
        store_word(encoded_values, i * 4, values[i]);
    for (unsigned kind = 0; kind < 2; ++kind)
        for (unsigned callbacks = 0; callbacks < 4; ++callbacks) {
            memset(array, 0xa5, sizeof array);
            store_word(array, 4, 0x12340000 | kind | (callbacks << 2));
            store_word(array, 8, 8);
            unsigned header = (kind ? 16 : 12) + (callbacks == 3 ? 20 : 0);
            memcpy(array + header, encoded_values, sizeof encoded_values);
            check_array(array, values, 8);
        }
    memset(array, 0, sizeof array);
    store_word(array, 4, 2);
    store_word(array, 8, 8);
    store_word(array, 12, US_TO_SYN68K(&deque[0]));
    for (unsigned left = 0; left < 256; ++left) {
        memset(deque, 0xa5, sizeof deque);
        deque[0] = left;
        memcpy(deque + 4 + left * 4, encoded_values, sizeof encoded_values);
        check_array(array, values, 8);
    }
    memset(storage, 0, sizeof storage);
    memset(branch, 0, sizeof branch);
    memset(leaves, 0, sizeof leaves);
    store_word(storage, 8, 4);
    store_word(storage, 12, 0);
    store_word(storage, 16, 2);
    store_word(storage, 20, US_TO_SYN68K(&encoded_values[0]));
    for (unsigned i = 0; i < 4; ++i) {
        store_word(leaves[i], 0, 8);
        leaves[i][4] = 1;
        store_word(leaves[i], 8, 8);
        store_word(leaves[i], 12, US_TO_SYN68K(&encoded_values[i * 8]));
    }
    store_word(branch, 0, 16);
    store_word(branch, 8, US_TO_SYN68K(&leaves[0][0]));
    store_word(branch, 12, US_TO_SYN68K(&leaves[1][0]));
    store_word(storage, 24, 32);
    store_word(storage, 32, US_TO_SYN68K(&branch[0]));
    store_word(storage, 36, US_TO_SYN68K(&leaves[2][0]));
    store_word(storage, 40, US_TO_SYN68K(&leaves[3][0]));
    store_word(array, 4, 3);
    store_word(array, 12, US_TO_SYN68K(&storage[0]));
    Byte saved[64];
    memcpy(saved, storage, sizeof saved);
    check_array(array, values, 8);
    assert(!memcmp(saved, storage, sizeof saved));
    uint32 (*at)(const void*, LONGINT) = function("CFArrayGetValueAtIndex");
    void (*copy)(const void*, LONGINT, LONGINT, void*) = function("CFArrayGetValues");
    assert(!at(NULL, 0));
    copy(NULL, 0, 1, array);
    copy(array, 0, 1, NULL);
}

static void array_readers(void)
{
    const uint32 opaque[] = {0, 1, 0xffffffff, 0xdeadbeef, 0x80000000, 0x7fffffff, 0x12345678, 0xaaaaaaaa};
    const uint32 duplicates[] = {10, 10, 10, 20, 20, 30, 30, 30};
    array_readers_for(opaque);
    array_readers_for(duplicates);
}

static void string_readers(void)
{
    LONGINT (*length)(void*) = function("CFStringGetLength");
    const void* (*characters)(const void*) = function("CFStringGetCharactersPtr");
    Byte record[32], before[32], external[256];
    memset(external, 'x', sizeof external);
    external[255] = 0;
    for (uint32 flags = 0; flags < 256; ++flags) {
        memset(record, 0xa5, sizeof record);
        store_word(record, 4, 0xface0000 | flags);
        if (flags & 0x60) {
            store_word(record, 8, US_TO_SYN68K(&external[0]));
            store_word(record, 12, 0x12345678);
        }
        else if ((flags & 5) == 4)
            record[8] = 231;
        else
            store_word(record, 8, 0x12345678);
        memcpy(before, record, sizeof record);
        const Byte* contents = flags & 0x60 ? external : record + ((flags & 5) == 4 ? 8 : 12);
        assert(length(record) == ((flags & 5) == 4 ? contents[0] : 0x12345678));
        assert(characters(record) == (flags & 0x10 ? contents : NULL));
        assert(!memcmp(before, record, sizeof record));
        if ((flags & 0x60) && (flags & 5) != 4) {
            store_word(record, 12, 0x00ffffff);
            assert(length(record) == 255);
            uint32 encoded;
            memcpy(&encoded, record + 12, sizeof encoded);
            assert(CL(encoded) == 255);
            external[0] = 0;
            assert(length(record) == 255); /* The reference caches this length. */
            store_word(record, 12, 0x00ffffff);
            assert(!length(record));
            external[0] = 'x';
        }
    }
    for (unsigned i = 0; i < 256; ++i) {
        store_word(record, 4, 4);
        record[8] = i;
        assert(length(record) == i);
        store_word(record, 4, 0x24);
        store_word(record, 8, US_TO_SYN68K(&external[0]));
        external[0] = i;
        assert(length(record) == i);
    }
    assert(!length(NULL) && !characters(NULL));
}

static void string_character_readers(void)
{
    uint32 (*at)(const void*, LONGINT) = function("CFStringGetCharacterAtIndex");
    void (*copy)(const void*, LONGINT, LONGINT, void*) = function("CFStringGetCharacters");
    Byte toc[0x72a8] = {0}, record[560], before[560], external[520], output[520];
    string_reference_ok = string_callback_ok = 1;
    for (unsigned i = 0; i < NELEM(string_references); ++i)
        string_references[i] =
            (ROMlib_carbon_reference){&string_delegates[i], toc, string_reference_offsets[i], 0x5dee0, 0, sizeof toc};
    const unsigned forms[] = {0, 4, 0x20, 0x24, 0x10, 0x30, 0x60};
    for (unsigned alignment = 0; alignment < 4; ++alignment)
        for (unsigned form = 0; form < NELEM(forms); ++form)
            for (unsigned mode = 0; mode < 3; ++mode) {
                Byte* string = record + alignment;
                Byte* destination = output + alignment + 1;
                unsigned flags = forms[form], count = flags & 4 ? 255 : 256;
                memset(record, 0xa5, sizeof record);
                store_word(string, 4, 0xface0000 | flags);
                Byte* contents = flags & 0x60 ? external : string + (flags & 4 ? 8 : 12);
                if (flags & 0x60) {
                    store_word(string, 8, US_TO_SYN68K(contents));
                    store_word(string, 12, count);
                }
                else if (!(flags & 4))
                    store_word(string, 8, count);
                if (flags & 4)
                    *contents++ = count;
                for (unsigned i = 0; i < count; ++i)
                    if (flags & 0x10) {
                        uint16 encoded = CW(i == 255 ? 0xffff : i * 257);
                        memcpy(contents + i * 2, &encoded, 2);
                    }
                    else
                        contents[i] = i;
                store_word(toc, 0x6f5c, mode ? US_TO_SYN68K(&string_converters[0]) : 0);
                string_reject = mode == 2;
                memcpy(before, record, sizeof before);
                for (unsigned i = 0; i < count; ++i) {
                    uint32 expected = flags & 0x10       ? (i == 255 ? 0xffff : i * 257)
                                      : i < 128 || !mode ? i
                                      : mode == 1        ? i + 0x100
                                                         : 0xfffe;
                    assert(at(string, i) == expected);
                }
                memset(output, 0xa5, sizeof output);
                copy(string, 0, count, destination);
                for (unsigned i = 0; i < count; ++i) {
                    uint16 encoded;
                    memcpy(&encoded, destination + i * 2, 2);
                    uint32 expected = flags & 0x10       ? (i == 255 ? 0xffff : i * 257)
                                      : i < 128 || !mode ? i
                                      : mode == 1        ? i + 0x100
                                                         : 0xfffe;
                    assert(CW(encoded) == expected);
                }
                assert(output[alignment] == 0xa5 && destination[count * 2] == 0xa5);
                assert(!memcmp(before, record, sizeof before));
                assert(!at(string, -1) && !at(string, count));
                memset(output, 0xa5, sizeof output);
                copy(string, count, 1, output);
                copy(string, -1, 1, output);
                copy(string, 0, -1, output);
                copy(string, INT32_MAX, INT32_MAX, output);
                for (unsigned i = 0; i < sizeof output; ++i)
                    assert(output[i] == 0xa5);
            }
    /* Character readers leave the lazy length cache unchanged. The live
       converter can change between bytes. */
    store_word(record, 4, 0x20);
    store_word(record, 8, US_TO_SYN68K(external));
    store_word(record, 12, 0x00ffffff);
    memcpy(external, "abc", 4);
    assert(at(record, 1) == 'b');
    copy(record, 0, 3, output);
    uint32 encoded;
    memcpy(&encoded, record + 12, 4);
    assert(CL(encoded) == 0x00ffffff);
    store_word(record, 12, 2);
    external[0] = 0x80;
    external[1] = 0x81;
    store_word(toc, 0x6f5c, US_TO_SYN68K(&string_converters[0]));
    string_mutated_toc = toc;
    string_mutated_input = external;
    string_reject = 0;
    copy(record, 0, 2, output);
    assert(output[0] == 1 && output[1] == 0x80 && output[2] == 2 && output[3] == 0xfe);
    string_mutated_toc = string_mutated_input = NULL;
    string_conversions = 0;
    string_fail_at = 2;
    memset(output, 0xa5, sizeof output);
    copy(record, 0, 2, output);
    assert(string_conversions == 2 && output[0] == 2 && output[1] == 0x80 && output[2] == 0xa5);
    string_fail_at = 0;
    /* Foreign profiles delegate the whole API, preserving all raw arguments. */
    for (unsigned i = 0; i < NELEM(string_references); ++i)
        ++string_references[i].code_size;
    string_result = 0xfedcba98;
    assert(at(record, -1) == 0xba98 && string_last == 0 && string_count == 2 && string_arguments[1] == UINT32_MAX);
    copy(record, -1, -2, output);
    assert(string_last == 1 && string_count == 4 && string_arguments[1] == UINT32_MAX &&
           string_arguments[2] == UINT32_MAX - 1 && string_arguments[3] == US_TO_SYN68K(output));
    string_callback_ok = 0;
    assert(!at(record, 0));
    string_reference_ok = 0;
}

static void string_builtin_byte_readers(void)
{
    uint32 (*at)(const void*, LONGINT) = function("CFStringGetCharacterAtIndex");
    void (*copy)(const void*, LONGINT, LONGINT, void*) = function("CFStringGetCharacters");
    Byte toc[0x72a8] = {0}, descriptors[16], record[280], external[260], output[520];
    string_reference_ok = string_callback_ok = 1;
    string_reject = string_fail_at = 0;
    for (unsigned i = 0; i < NELEM(string_references); ++i) {
        string_references[i] =
            (ROMlib_carbon_reference){descriptors + i * 8, toc, string_reference_offsets[i], 0x5dee0, 0, sizeof toc};
        store_word(descriptors, i * 8, 0x10000000 + string_reference_offsets[i]);
    }
    store_word(toc, 0x1fb4, 0x1004ce50);
    store_word(toc, 0x1fb8, US_TO_SYN68K(toc));
    store_word(toc, 0x6f5c, US_TO_SYN68K(toc + 0x1fb4));
    for (unsigned i = 0; i < 128; ++i) {
        uint16 encoded = CW(0xff00 ^ (i * 257));
        memcpy(toc + 0x52f8 + i * 2, &encoded, 2);
    }
    unsigned calls = string_conversions;
    const unsigned forms[] = {0, 4, 0x20, 0x24};
    for (unsigned alignment = 0; alignment < 4; ++alignment)
        for (unsigned form = 0; form < NELEM(forms); ++form) {
            unsigned flags = forms[form], length = flags & 4 ? 255 : 256;
            Byte* string = record + alignment;
            Byte* contents = flags & 0x20 ? external + alignment : string + (flags & 4 ? 8 : 12);
            store_word(string, 4, flags);
            if (flags & 0x20)
                store_word(string, 8, US_TO_SYN68K(contents));
            if (flags & 4)
                *contents++ = (Byte)length;
            else
                store_word(string, flags & 0x20 ? 12 : 8, length);
            for (unsigned i = 0; i < length; ++i)
                contents[i] = (Byte)i;
            memset(output, 0xa5, sizeof output);
            copy(string, 0, length, output + 1);
            for (unsigned i = 0; i < length; ++i) {
                uint32 expected = i < 128 ? i : 0xff00 ^ ((i - 128) * 257);
                uint16 encoded;
                memcpy(&encoded, output + 1 + i * 2, 2);
                assert(at(string, i) == expected && CW(encoded) == expected);
            }
            assert(output[0] == 0xa5 && output[1 + length * 2] == 0xa5);
        }
    assert(string_conversions == calls);
    /* The other built-ins differ in both mapping and rejection behavior. */
    uint16 windows[32], next[128];
    for (unsigned i = 0; i < 128; ++i) {
        next[i] = i % 9 ? (uint16)(0x100 + i * 31) : 0xfffd;
        uint16 encoded = CW(next[i]);
        memcpy(toc + 0x56f4 + i * 2, &encoded, 2);
        if (i < 32) {
            windows[i] = i % 5 ? (uint16)(0x200 + i * 17) : 0xfffd;
            encoded = CW(windows[i]);
            memcpy(toc + 0x548c + i * 2, &encoded, 2);
        }
    }
    const uint32 decoder_offsets[] = {0x1fe4, 0x1fcc, 0x1f9c, 0x1f84};
    const uint32 decoder_code[] = {0x4ccb0, 0x4cd08, 0x4cfc8, 0x4d164};
    store_word(record, 4, 0x20);
    store_word(record, 8, US_TO_SYN68K(external));
    store_word(record, 12, 256);
    for (unsigned i = 0; i < 256; ++i)
        external[i] = (Byte)i;
    for (unsigned decoder = 0; decoder < NELEM(decoder_offsets); ++decoder) {
        uint32 offset = decoder_offsets[decoder], code = 0x10000000 + decoder_code[decoder];
        store_word(toc, offset, code);
        store_word(toc, offset + 4, US_TO_SYN68K(toc));
        store_word(toc, 0x6f5c, US_TO_SYN68K(toc + offset));
        memset(output, 0xa5, sizeof output);
        copy(record, 0, 256, output + 1);
        for (unsigned i = 0; i < 256; ++i) {
            uint32 expected = i;
            if (!decoder && i >= 128)
                expected = 0xfffe;
            else if (decoder == 2 && i >= 128 && i <= 159)
                expected = windows[i - 128];
            else if (decoder == 3 && i >= 128)
                expected = next[i - 128];
            if (expected == 0xfffd)
                expected = 0xfffe;
            uint16 encoded;
            memcpy(&encoded, output + 1 + i * 2, 2);
            assert(at(record, i) == expected && CW(encoded) == expected);
        }
        assert(output[0] == 0xa5 && output[513] == 0xa5 && string_conversions == calls);
        string_alternate_converter = toc + offset;
        for (unsigned field = 0; field < 2; ++field) {
            store_word(toc, offset + field * 4, 0);
            assert(at(record, 128) == 0x180 && string_conversions == ++calls);
            store_word(toc, offset, code);
            store_word(toc, offset + 4, US_TO_SYN68K(toc));
        }
        string_alternate_converter = NULL;
    }
    store_word(toc, 0x6f5c, US_TO_SYN68K(toc + 0x1fb4));
    /* Both the table and callback selection stay live between reads. */
    store_word(record, 4, 0x20);
    store_word(record, 8, US_TO_SYN68K(external));
    store_word(record, 12, 2);
    external[0] = 0x80;
    external[1] = 0x81;
    uint16 replacement = CW(0x1234);
    uint16 rejected = CW(0xfffd);
    memcpy(toc + 0x52f8, &rejected, 2);
    assert(at(record, 0) == 0xfffd); /* MacRoman accepts this table word. */
    memcpy(toc + 0x52f8, &replacement, 2);
    assert(at(record, 0) == 0x1234);
    copy(record, 0, 1, toc + 0x52f9);
    assert(toc[0x52f8] == 0x12 && toc[0x52f9] == 0x12 && toc[0x52fa] == 0x34);
    memcpy(toc + 0x52f8, &replacement, 2);
    replacement = CW(0xfe01);
    memcpy(toc + 0x52fa, &replacement, 2);
    string_alternate_converter = toc + 0x1fb4;
    for (unsigned field = 0; field < 2; ++field) {
        store_word(toc, 0x1fb4 + field * 4, 0);
        assert(at(record, 0) == 0x180 && string_conversions == ++calls);
        store_word(toc, 0x1fb4, 0x1004ce50);
        store_word(toc, 0x1fb8, US_TO_SYN68K(toc));
    }
    string_alternate_converter = NULL;
    store_word(toc, 0x6f5c, US_TO_SYN68K(&string_converters[0]));
    string_mutated_toc = toc;
    string_mutated_input = external;
    string_converter_replacement = US_TO_SYN68K(toc + 0x1fb4);
    copy(record, 0, 2, output + 1);
    uint16 first, second;
    memcpy(&first, output + 1, 2);
    memcpy(&second, output + 3, 2);
    assert(CW(first) == 0x180 && CW(second) == (0xff00 ^ (126 * 257)) && string_conversions == ++calls);
    string_mutated_toc = string_mutated_input = NULL;
    string_converter_replacement = 0;
    string_reference_ok = string_callback_ok = 0;
}

static uint32 pascal_convert(void* string, void* output, LONGINT capacity, uint32 encoding)
{
    pascal_input = string;
    pascal_output = output;
    pascal_capacity = capacity;
    pascal_encoding = encoding;
    uint32 (*convert)(void*, void*, LONGINT, uint32) = ROMlib_carbon_find_export(pascal_names[0], 2)->function;
    return convert(string, output, capacity, encoding);
}

static void string_encoding_exports(void)
{
    typedef LONGINT (*bytes_function)(void*, LONGINT, LONGINT, uint32, uint32, uint32, void*, LONGINT, void*);
    bytes_function bytes = function("CFStringGetBytes");
    uint32 (*fastest)(const void*) = function("CFStringGetFastestEncoding");
    Byte toc[0x72a8] = {0}, record[64], external[32], output[32], used[4];
    const Byte payload[] = {'A', 0, 0x80, 0xff, 'Z'};
    encoding_active = encoding_reference_ok = encoding_callback_ok = 1;
    encoding_references[0] = (ROMlib_carbon_reference){&encoding_procedures[0], toc, 0x38ac8, 0x5dee0, 0, sizeof toc};
    encoding_references[1] = (ROMlib_carbon_reference){&encoding_procedures[1], toc, 0x3cfa8, 0x5dee0, 0, sizeof toc};
    pascal_active = pascal_reference_ok = 1;
    pascal_fail_index = -1;
    pascal_references[0].toc = toc;
    pascal_references[2] = (ROMlib_carbon_reference){&encoding_procedures[2], toc, 0x419bc, 0x5dee0, 0, sizeof toc};
    for (unsigned alignment = 0; alignment < 4; ++alignment)
        for (unsigned form = 0; form < 4; ++form) {
            unsigned flags = (form & 1 ? 4 : 0) | (form & 2 ? 0x20 : 0);
            Byte* string = record + alignment;
            Byte* contents = form & 2 ? external + alignment : string + (flags & 4 ? 8 : 12);
            store_word(string, 4, flags);
            if (form & 2)
                store_word(string, 8, US_TO_SYN68K(contents));
            if (flags & 4)
                *contents++ = sizeof payload;
            else
                store_word(string, form & 2 ? 12 : 8, sizeof payload);
            memcpy(contents, payload, sizeof payload);
            for (uint32 start = 0; start <= sizeof payload; ++start)
                for (uint32 length = 0; length <= sizeof payload - start; ++length)
                    for (uint32 capacity = 0; capacity <= sizeof payload + 1; ++capacity)
                        for (unsigned measure = 0; measure < 2; ++measure)
                            for (unsigned publish = 0; publish < 2; ++publish) {
                                memset(output, 0xa5, sizeof output);
                                memset(used, 0xa5, sizeof used);
                                uint32 count = measure ? length : MIN(length, capacity), encoded;
                                assert(bytes(string, start, length, 0, 0xff, 1, measure ? NULL : output + 1, capacity,
                                             publish ? used : NULL) == (LONGINT)count);
                                memcpy(&encoded, used, 4);
                                assert(CL(encoded) == (publish ? count : 0xa5a5a5a5));
                                if (!measure)
                                    assert(!memcmp(output + 1, payload + start, count));
                                assert(output[0] == 0xa5 && output[1 + (measure ? 0 : count)] == 0xa5);
                            }
        }
    assert(!encoding_calls[0] && !encoding_calls[1] && !encoding_calls[2]);
    /* The general engine receives all nine words in its private order. */
    store_word(record, 4, 0x10);
    store_word(record, 8, 5);
    encoding_result = 2;
    assert(bytes(record, 1, 2, 0x8000100, 0xabcdef80, 0x123401, output + 1, 17, used) == 2);
    const uint32 expected[] = {
        US_TO_SYN68K(record), 1, 2, 0x123401, 0x8000100, 0xabcdef80, US_TO_SYN68K(output + 1), 17, US_TO_SYN68K(used)};
    assert(!memcmp(encoding_arguments, expected, sizeof expected) && encoding_calls[2] == 1);
    encoding_callback_ok = 0;
    assert(!bytes(record, 1, 2, 0x100, 0, 0, output, 16, used));
    encoding_callback_ok = 1;
    /* Fastest encoding uses the format bit and initializes the shared byte
       encoding once; wide strings do not invoke its initializer. */
    for (unsigned flags = 0; flags < 256; ++flags) {
        store_word(record, 4, 0xface0000 | flags);
        assert(fastest(record) == (flags & 0x10 ? 0x100 : 0));
    }
    pascal_input = record;
    pascal_mutated_info = UINT32_MAX;
    pascal_initialized_encoding = 0x201;
    unsigned initialized = pascal_calls[1];
    store_word(toc, 0x3a70, UINT32_MAX);
    store_word(record, 4, 0x10);
    assert(fastest(record) == 0x100 && pascal_calls[1] == initialized);
    store_word(record, 4, 0);
    assert(fastest(record) == 0x201 && pascal_calls[1] == initialized + 1);
    assert(fastest(record) == 0x201 && pascal_calls[1] == initialized + 1);
    store_word(toc, 0x3a70, UINT32_MAX);
    pascal_initialized_encoding = UINT32_MAX;
    assert(fastest(record) == UINT32_MAX && pascal_calls[1] == initialized + 2);
    pascal_fail_index = 1;
    assert(fastest(record) == UINT32_MAX && pascal_calls[1] == initialized + 3);
    pascal_fail_index = -1;
    /* Foreign profiles forward complete public ABI words without inspecting
       their object layout. No result is narrowed to a Boolean. */
    encoding_result = 0x89abcdef;
    for (unsigned which = 0; which < 2; ++which) {
        ROMlib_carbon_reference known = encoding_references[which];
        for (unsigned variant = 0; variant < 5; ++variant) {
            encoding_references[which] = known;
            if (!variant)
                ++encoding_references[which].code_offset;
            else if (variant == 1)
                ++encoding_references[which].code_size;
            else if (variant == 2)
                ++encoding_references[which].toc_offset;
            else if (variant == 3)
                --encoding_references[which].toc_size;
            else
                encoding_references[which].toc = NULL;
            if (which)
                assert(fastest(NULL) == encoding_result && !encoding_arguments[0]);
            else {
                assert((uint32)bytes(NULL, -1, -2, 0xdeadbeef, 0x123480, 0xabcdef01, output, -3, used) ==
                       encoding_result);
                const uint32 raw[] = {0,          UINT32_MAX,           UINT32_MAX - 1, 0xdeadbeef,        0x123480,
                                      0xabcdef01, US_TO_SYN68K(output), UINT32_MAX - 2, US_TO_SYN68K(used)};
                assert(!memcmp(encoding_arguments, raw, sizeof raw));
            }
        }
        encoding_references[which] = known;
    }
    encoding_reference_ok = 0;
    assert(!bytes(record, 0, 0, 0, 0, 0, output, 0, used) && fastest(record) == UINT32_MAX);
    encoding_active = pascal_active = 0;
}

static void string_pascal_conversion(void)
{
    Byte low_memory[4096] = {0}, toc[0x72a8] = {0}, record[600], external[300], output[1040], before[1040];
    uint64 previous_offset = ROMlib_offset;
    ROMlib_offset = (uintptr_t)low_memory;
    pascal_active = pascal_reference_ok = 1;
    pascal_fail_index = -1;
    for (unsigned i = 0; i < NELEM(pascal_names); ++i)
        pascal_references[i] = (ROMlib_carbon_reference){&pascal_procedures[i], toc, 0x39274, 0x5dee0, 0, sizeof toc};
    for (unsigned alignment = 0; alignment < 4; ++alignment)
        for (unsigned form = 0; form < 4; ++form) {
            Byte* string = record + alignment;
            Byte* result = output + 4 + alignment;
            memset(record, 0xcc, sizeof record);
            memset(external, 0xdd, sizeof external);
            unsigned info = (form & 1 ? 4 : 0) | (form & 2 ? 0x20 : 0);
            store_word(string, 4, info);
            Byte* contents = form & 2 ? external + alignment : string + (info & 4 ? 8 : 12);
            if (form & 2)
                store_word(string, 8, US_TO_SYN68K(contents));
            for (unsigned length = 0; length <= 255; length += length ? 254 : 1) {
                if (info & 4)
                    contents[0] = (Byte)length;
                else
                    store_word(string, form & 2 ? 12 : 8, length);
                Byte* payload = contents + !!(info & 4);
                for (unsigned i = 0; i < length; ++i)
                    payload[i] = (Byte)(i * 7);
                memset(output, 0xa5, sizeof output);
                memcpy(before, output, sizeof output);
                assert(pascal_convert(string, result, (LONGINT)length, 0) == 0);
                assert(!memcmp(output, before, sizeof output));
                assert(pascal_convert(string, result, (LONGINT)length + 1, 0) == 1);
                assert(result[0] == length && !memcmp(result + 1, payload, length));
                memset(before + 4 + alignment, 0, length + 1);
                memcpy(before + 4 + alignment, result, length + 1);
                assert(!memcmp(output, before, sizeof output));
            }
        }
    assert(!pascal_calls[0] && !pascal_calls[1] && !pascal_calls[2] && !pascal_allocations);

    /* The ASCII-compatible target families retain complete encoding-word
       exceptions, including UTF-8's high bits and group boundaries. */
    static const struct {
        uint32 encoding;
        int copy;
    } targets[] = {{0, 1},     {1, 0},         {4, 0},     {5, 0},     {6, 1},     {0x20, 1},
                   {0x21, 0},  {0x22, 0},      {0x23, 1},  {0x98, 0},  {0x99, 1},  {0x100, 0},
                   {0x101, 1}, {0x8000100, 1}, {0x205, 1}, {0x206, 0}, {0x207, 1}, {0x300, 1},
                   {0x500, 1}, {0x600, 1},     {0x601, 0}, {0x700, 1}, {0x800, 0}, {0x8ff, 0},
                   {0x900, 1}, {0xa00, 1},     {0xa01, 0}, {0xafe, 0}, {0xaff, 1}, {0xb00, 1},
                   {0xbfe, 1}, {0xbff, 0},     {0xc00, 0}, {0xcff, 0}, {0xd00, 0}, {UINT32_MAX, 0}};

    memset(record, 0, sizeof record);
    store_word(record, 8, 3);
    memcpy(record + 12, "abc", 3);
    store_word(toc, 0x3a70, 0x600);
    pascal_length = pascal_chars = pascal_used = 3;
    for (unsigned i = 0; i < NELEM(targets); ++i) {
        unsigned calls = pascal_calls[2];
        memset(output, 0xa5, sizeof output);
        assert(pascal_convert(record, output + 4, 16, targets[i].encoding) == 1);
        assert(pascal_calls[2] == calls + !targets[i].copy);
        assert(output[4] == 3 && output[3] == 0xa5 && output[8] == 0xa5);
        assert(!memcmp(output + 5, targets[i].copy ? (const void*)"abc" : (const void*)"\0\7\16", 3));
    }
    assert(pascal_allocations == pascal_disposals);
    store_word(toc, 0x3a70, UINT32_MAX);
    pascal_initialized_encoding = 0;
    pascal_mutated_info = 4;
    memcpy(record + 12, "xabc", 4);
    assert(pascal_convert(record, output, 16, 0) == 1 && !memcmp(output, "\3abc", 4));
    assert(pascal_calls[1] == 1);
    pascal_mutated_info = UINT32_MAX;
    store_word(record, 4, 0);
    store_word(toc, 0x3a70, UINT32_MAX);
    pascal_initialized_encoding = UINT32_MAX;
    assert(pascal_convert(record, output, 16, 0x600) == 1 && pascal_calls[1] == 3);
    /* Lazy external lengths cache before the source-count rejection; the
       general path preserves partial payloads but clears only the prefix. */
    store_word(record, 4, 0x20);
    store_word(record, 8, US_TO_SYN68K(external));
    store_word(record, 12, 0x00ffffff);
    memset(external, 'a', 256);
    external[256] = 0;
    memset(output, 0xa5, sizeof output);
    assert(!pascal_convert(record, output, sizeof output, 0));
    uint32 cached;
    memcpy(&cached, record + 12, sizeof cached);
    assert(output[0] == 0xa5 && CL(cached) == 256);
    store_word(record, 4, 0x10);
    store_word(record, 8, 3);
    pascal_chars = 2;
    pascal_used = 2;
    assert(!pascal_convert(record, output + 4, 16, 0x100));
    assert(output[4] == 0 && output[5] == 0 && output[6] == 7 && output[7] == 0xa5);
    pascal_chars = 3;
    pascal_used = 300;
    assert(pascal_convert(record, output + 4, 1024, 0x8000100) == 1 && output[4] == 44);
    assert((uint16)MemErr == CW(-321));
    unsigned calls = pascal_calls[2];
    pascal_alloc_fail = 1;
    memset(output, 0xa5, sizeof output);
    assert(!pascal_convert(record, output, 16, 0x100) && pascal_calls[2] == calls && output[0] == 0xa5);
    assert((uint16)MemErr == CW(-321));
    pascal_alloc_fail = 0;
    pascal_fail_index = 2;
    pascal_used = 3;
    assert(!pascal_convert(record, output, 16, 0x100) && output[0] == 0xa5 && output[1] == 0 && output[2] == 7);
    assert(pascal_allocations == pascal_disposals + 1);
    pascal_fail_index = 1;
    store_word(record, 4, 0);
    store_word(toc, 0x3a70, UINT32_MAX);
    calls = pascal_calls[2];
    assert(!pascal_convert(record, output, 16, 0) && pascal_calls[2] == calls);
    pascal_fail_index = -1;
    ROMlib_carbon_reference known = pascal_references[0];
    for (unsigned variant = 0; variant < 5; ++variant) {
        pascal_references[0] = known;
        if (!variant)
            ++pascal_references[0].code_offset;
        else if (variant == 1)
            ++pascal_references[0].code_size;
        else if (variant == 2)
            ++pascal_references[0].toc_offset;
        else if (variant == 3)
            --pascal_references[0].toc_size;
        else
            pascal_references[0].toc = NULL;
        pascal_delegate_result = 0x1ff;
        assert(pascal_convert(record, output, -1, 0xdeadbeef) == 255);
        assert(pascal_arguments[0] == US_TO_SYN68K(record) && pascal_arguments[1] == US_TO_SYN68K(output) &&
               pascal_arguments[2] == UINT32_MAX && pascal_arguments[3] == 0xdeadbeef);
    }
    pascal_fail_index = 0;
    assert(!pascal_convert(record, output, 16, 0));
    pascal_references[0] = known;
    pascal_fail_index = -1;
    pascal_reference_ok = 0;
    assert(!pascal_convert(record, output, 16, 0));
    pascal_reference_ok = 1;
    assert(!pascal_convert(NULL, output, 0, 0) && !pascal_convert(record, NULL, 16, 0));
    pascal_active = 0;
    ROMlib_offset = previous_offset;
}

static void string_borrowed_pointers(void)
{
    const void* (*c_pointer)(void*, uint32) = ROMlib_carbon_find_export(borrow_names[0], 2)->function;
    const void* (*p_pointer)(void*, uint32) = ROMlib_carbon_find_export(borrow_names[1], 2)->function;
    Byte toc[0x72a8] = {0}, records[80], external[32], before_records[sizeof records], before_external[sizeof external];
    borrow_active = borrow_reference_ok = borrow_callback_ok = 1;
    borrow_references[0] = (ROMlib_carbon_reference){&borrow_procedures[0], toc, 0x38ff4, 0x5dee0, 0, sizeof toc};
    borrow_references[1] = (ROMlib_carbon_reference){&borrow_procedures[1], toc, 0x38d70, 0x5dee0, 0, sizeof toc};
    borrow_references[2] = (ROMlib_carbon_reference){&borrow_procedures[2], toc, 0x357c4, 0x5dee0, 0, sizeof toc};
    for (unsigned alignment = 0; alignment < 4; ++alignment)
        for (uint32 info = 0; info < 256; ++info) {
            Byte* string = records + alignment;
            memset(records, 0xa5, sizeof records);
            memset(external, 0xcc, sizeof external);
            store_word(string, 4, 0x12340000 | info);
            Byte* contents;
            if (info & 0x60) {
                contents = external + alignment;
                store_word(string, 8, US_TO_SYN68K(contents));
                store_word(string, 12, 3);
            }
            else {
                contents = string + ((info & 5) == 4 ? 8 : 12);
                if ((info & 5) != 4)
                    store_word(string, 8, 3);
            }
            memcpy(contents, "\3ABC\0", 5);
            memcpy(before_records, records, sizeof records);
            memcpy(before_external, external, sizeof external);
            assert(c_pointer(string, 0) == (info & 8 ? contents + !!(info & 4) : NULL));
            assert(p_pointer(string, 0) == ((info & 4) && !(info & 0x10) ? contents : NULL));
            assert(!memcmp(records, before_records, sizeof records) &&
                   !memcmp(external, before_external, sizeof external));
        }
    assert(!borrow_calls[0] && !borrow_calls[1] && !borrow_calls[2]);
    memset(records, 0, sizeof records);
    store_word(records, 4, 0x2d); /* External, mutable, Pascal and CString flags. */
    store_word(records, 8, US_TO_SYN68K(external));
    store_word(records, 12, 4);
    memcpy(external, "\3ABC\0", 5);
    assert(!p_pointer(records, 0) && c_pointer(records, 0) == external + 1);
    store_word(records, 12, 0xffffff);
    assert(!p_pointer(records, 0));
    uint32 cached;
    memcpy(&cached, records + 12, 4);
    assert(CL(cached) == 4);
    external[0] = 4;
    assert(p_pointer(records, 0) == external);
    external[2] = 'Z';
    assert(((const Byte*)c_pointer(records, 0))[1] == 'Z');
    store_word(records, 12, 0xffffff);
    assert(!p_pointer(records, 0x100) && !c_pointer(records, 0x100));
    memcpy(&cached, records + 12, 4);
    assert(CL(cached) == 0xffffff);

    static const struct {
        uint32 encoding;
        int compatible;
    } encodings[] = {{0, 1},     {1, 0},     {4, 0},     {5, 0},     {6, 1},         {0x21, 0},
                     {0x22, 0},  {0x98, 0},  {0x100, 0}, {0x101, 1}, {0x8000100, 1}, {0x206, 0},
                     {0x207, 1}, {0x600, 1}, {0x601, 0}, {0x800, 0}, {0xa01, 0},     {0xafe, 0},
                     {0xb00, 1}, {0xbff, 0}, {0xc00, 0}, {0xd00, 0}, {UINT32_MAX, 0}};

    store_word(records, 4, 0xc);
    memcpy(records + 8, "\3ABC\0", 5);
    store_word(toc, 0x3a70, 0x600);
    for (unsigned i = 0; i < NELEM(encodings); ++i) {
        assert(c_pointer(records, encodings[i].encoding) == (encodings[i].compatible ? records + 9 : NULL));
        assert(p_pointer(records, encodings[i].encoding) == (encodings[i].compatible ? records + 8 : NULL));
    }
    /* CString initializes even when its format flag is absent; Pascal rejects
       missing/Unicode format before invoking the initializer. */
    borrow_initialized_encoding = 0;
    store_word(records, 4, 0);
    store_word(toc, 0x3a70, UINT32_MAX);
    assert(!p_pointer(records, 0) && !borrow_calls[2]);
    store_word(records, 4, 0x14);
    assert(!p_pointer(records, 0) && !borrow_calls[2]);
    store_word(records, 4, 0);
    assert(!c_pointer(records, 0) && borrow_calls[2] == 1);
    store_word(toc, 0x3a70, UINT32_MAX);
    borrow_mutated_string = records;
    borrow_mutated_info = 8;
    assert(c_pointer(records, 0) == records + 12 && borrow_calls[2] == 2);
    store_word(toc, 0x3a70, UINT32_MAX);
    store_word(records, 4, 4);
    store_word(records, 8, 3);
    memcpy(records + 12, "\3ABC\0", 5);
    borrow_mutated_info = 5;
    assert(p_pointer(records, 0) == records + 12);
    borrow_mutated_string = NULL;
    store_word(toc, 0x3a70, UINT32_MAX);
    borrow_initialized_encoding = UINT32_MAX;
    unsigned initialized = borrow_calls[2];
    assert(!c_pointer(records, 0) && borrow_calls[2] == initialized + 2);
    borrow_callback_ok = 0;
    initialized = borrow_calls[2];
    assert(!p_pointer(records, 0) && borrow_calls[2] == initialized + 1);
    borrow_callback_ok = 1;
    for (unsigned which = 0; which < 2; ++which) {
        ROMlib_carbon_reference known = borrow_references[which];
        for (unsigned variant = 0; variant < 5; ++variant) {
            borrow_references[which] = known;
            if (!variant)
                ++borrow_references[which].code_offset;
            else if (variant == 1)
                ++borrow_references[which].code_size;
            else if (variant == 2)
                ++borrow_references[which].toc_offset;
            else if (variant == 3)
                --borrow_references[which].toc_size;
            else
                borrow_references[which].toc = NULL;
            assert((which ? p_pointer : c_pointer)(records, 0xdeadbeef) == &borrow_target);
            assert(borrow_arguments[0] == US_TO_SYN68K(records) && borrow_arguments[1] == 0xdeadbeef);
        }
        borrow_callback_ok = 0;
        assert(!(which ? p_pointer : c_pointer)(records, 0));
        borrow_references[which] = known;
        borrow_callback_ok = 1;
    }
    borrow_reference_ok = 0;
    assert(!c_pointer(records, 0) && !p_pointer(records, 0));
    borrow_reference_ok = 1;
    assert(!c_pointer(NULL, 0) && !p_pointer(NULL, 0));
    borrow_active = 0;
}

static uint32 stream_used_value(void)
{
    uint32 value;
    memcpy(&value, pascal_scratch, sizeof value);
    return CL(value);
}

static void stream_encoding(void)
{
    Byte low_memory[4096] = {0}, toc[0x72a8] = {0}, string[160] = {0}, output[256], literals[64] = {0};
    uint64 previous_offset = ROMlib_offset;
    ROMlib_offset = (uintptr_t)low_memory;
    pascal_active = pascal_reference_ok = 1;
    pascal_fail_index = -1;
    pascal_references[2] = (ROMlib_carbon_reference){&pascal_procedures[2], toc, 0x419b8, 0x5dee0, 0, sizeof toc};
    store_word(string, 4, 0x10);
    store_word(string, 8, 6);
    for (unsigned i = 0; i < 6; ++i) {
        uint16 encoded = CW(0x100 + i);
        memcpy(string + 12 + i * 2, &encoded, 2);
    }
    uint32 arguments[] = {US_TO_SYN68K(string),        2, 3, 0, 0x100, 0xff, US_TO_SYN68K(output + 4), 8,
                          US_TO_SYN68K(pascal_scratch)},
           result;
    unsigned delegated = pascal_calls[2];
    for (unsigned mark = 0; mark < 2; ++mark)
        for (unsigned capacity = mark * 2; capacity <= 10; ++capacity) {
            memset(output, 0xa5, sizeof output);
            arguments[3] = mark;
            arguments[7] = capacity;
            uint32 count = MIN(UINT32_C(3), (capacity - mark * 2) / 2);
            assert(ROMlib_carbon_cf_encode_stream(arguments, &result) && result == count);
            assert(stream_used_value() == count * 2 + mark * 2);
            if (mark)
                assert(output[4] == 0xfe && output[5] == 0xff);
            /* The reference's Unicode path reads from the start, not location. */
            assert(!memcmp(output + 4 + mark * 2, string + 12, count * 2));
            assert(output[3] == 0xa5 && output[4 + count * 2 + mark * 2] == 0xa5);
        }
    arguments[3] = 0x100; /* Boolean arguments use their low byte. */
    arguments[6] = 0;
    arguments[7] = 0;
    assert(ROMlib_carbon_cf_encode_stream(arguments, &result) && result == 3 && stream_used_value() == 6);
    assert(pascal_calls[2] == delegated);
    static const uint16 units[] = {'A', 0, 9, 10, 13, '\\', 0x1f, 0x7f, 0xff, 0x100, 0xd800, 0xffff, 'Z'};
    static const Byte expected[] = "A"
                                   "\\000"
                                   "\t\n\r"
                                   "\\\\"
                                   "\\037"
                                   "\\177"
                                   "\\377"
                                   "\\u0100"
                                   "\\ud800";
    static const unsigned boundaries[] = {0, 1, 5, 6, 7, 8, 10, 14, 18, 22, 28, 34};
    store_word(string, 8, NELEM(units));
    for (unsigned i = 0; i < NELEM(units); ++i) {
        uint16 encoded = CW(units[i]);
        memcpy(string + 12 + i * 2, &encoded, 2);
    }
    memcpy(literals + 0x1b, "0123456789abcdef", 16);
    store_word(toc, 0x5a8, US_TO_SYN68K(literals));
    arguments[1] = 0;
    arguments[2] = NELEM(units);
    arguments[3] = 1;
    arguments[4] = 0xbff;
    arguments[6] = US_TO_SYN68K(output + 4);
    MemErr = CW(-123);
    for (unsigned capacity = 0; capacity <= 35; ++capacity) {
        memset(output, 0xa5, sizeof output);
        arguments[7] = capacity;
        unsigned count = 0;
        while (count + 1 < NELEM(boundaries) && boundaries[count + 1] <= capacity)
            ++count;
        assert(ROMlib_carbon_cf_encode_stream(arguments, &result) && result == count);
        assert(stream_used_value() == boundaries[count]);
        assert(!memcmp(output + 4, expected, boundaries[count]));
        assert(output[3] == 0xa5 && output[4 + boundaries[count]] == 0xa5);
        assert((uint16)MemErr == CW(-123));
    }
    assert(stream_allocations == stream_disposals && pascal_calls[2] == delegated);
    arguments[6] = 0;
    arguments[7] = 0;
    assert(ROMlib_carbon_cf_encode_stream(arguments, &result) && result == 11 && stream_used_value() == 34);
    /* NonLossyASCII treats length as its inline-buffer end bound in 1.4. */
    arguments[1] = 2;
    arguments[2] = 3;
    assert(ROMlib_carbon_cf_encode_stream(arguments, &result) && result == 1 && stream_used_value() == 1);
    stream_alloc_fail = 1;
    memset(pascal_scratch, 0xa5, sizeof pascal_scratch);
    assert(!ROMlib_carbon_cf_encode_stream(arguments, &result) && stream_used_value() == 0xa5a5a5a5);
    stream_alloc_fail = 0;
    assert(stream_allocations == stream_disposals + 1);
    arguments[1] = 1;
    arguments[2] = 2;
    arguments[3] = 1;
    arguments[4] = 0x8000100;
    arguments[6] = US_TO_SYN68K(output + 4);
    arguments[7] = 17;
    store_word(toc, 0x3e88, 0x8000100);
    store_word(toc, 0x593c, US_TO_SYN68K(&stream_utf8));
    stream_callback_ok = 1;
    stream_result = 2;
    stream_used = 7;
    assert(ROMlib_carbon_cf_encode_stream(arguments, &result) && result == 2 && stream_used_value() == 7);
    assert(stream_arguments[0] == 0x100 && stream_arguments[1] == US_TO_SYN68K(string + 14) &&
           stream_arguments[2] == 2 && stream_arguments[3] == arguments[6] && stream_arguments[4] == 17 &&
           stream_arguments[5] == arguments[8]);
    store_word(toc, 0x593c, 0);
    arguments[3] = 0x100;
    arguments[6] = 0;
    assert(ROMlib_carbon_cf_encode_stream(arguments, &result));
    assert(stream_calls == 2 && !stream_arguments[0] && !stream_arguments[3] && !stream_arguments[4]);
    stream_callback_ok = 0;
    assert(!ROMlib_carbon_cf_encode_stream(arguments, &result));
    assert(pascal_calls[2] == delegated);
    /* The built-in codec is selected only by the canonical descriptor and
       verified code/TOC. These vectors include legacy surrogate/count rules. */
    Byte descriptor[8];
    void* saved_function = pascal_references[2].function;
    pascal_references[2].function = descriptor;
    store_word(descriptor, 0, 0x100419b8);
    store_word(toc, 0x1f5c, 0x1004d248);
    store_word(toc, 0x1f60, US_TO_SYN68K(toc));
    store_word(toc, 0x9ac, US_TO_SYN68K(toc + 0x1f5c));
    memcpy(toc + 0x5934, "\0\0\300\340\360\370\374\0", 8);

    static const struct {
        uint16 units[4];
        uint32 length, capacity, count, bytes;
        Byte output[10];
    } utf8_cases[] = {
        {{0, 0x41, 0x7f}, 3, 3, 3, 3, {0, 0x41, 0x7f}},
        {{0, 0x41, 0x7f}, 3, 2, 2, 2, {0, 0x41}},
        {{0x80, 0x7ff, 0x800, 0xffff}, 4, 10, 4, 10, {0xc2, 0x80, 0xdf, 0xbf, 0xe0, 0xa0, 0x80, 0xef, 0xbf, 0xbf}},
        {{0x80, 0x7ff, 0x800, 0xffff}, 4, 5, 2, 4, {0xc2, 0x80, 0xdf, 0xbf}},
        {{0xd800, 0xdc00}, 2, 1, 1, 0, {0}},
        {{0xd800, 0xdc00}, 2, 3, 1, 0, {0}},
        {{0xd800, 0xdc00}, 2, 4, 2, 4, {0xf0, 0x90, 0x80, 0x80}},
        {{0xdbff, 0xdfff}, 2, 4, 2, 4, {0xf4, 0x8f, 0xbf, 0xbf}},
        {{0xdc00}, 1, 3, 1, 3, {0xed, 0xb0, 0x80}},
        {{0xd800, 0x41}, 2, 9, 0, 0, {0}},
        {{0x41, 0xd800}, 2, 9, 1, 1, {0x41}},
    };

    arguments[1] = 1;
    arguments[3] = 1;
    arguments[6] = US_TO_SYN68K(output + 4);
    for (unsigned i = 0; i < NELEM(utf8_cases); ++i) {
        store_word(string, 8, utf8_cases[i].length + 1);
        for (uint32 j = 0; j < utf8_cases[i].length; ++j) {
            uint16 encoded = CW(utf8_cases[i].units[j]);
            memcpy(string + 14 + j * 2, &encoded, 2);
        }
        arguments[2] = utf8_cases[i].length;
        arguments[7] = utf8_cases[i].capacity;
        memset(output, 0xa5, sizeof output);
        assert(ROMlib_carbon_cf_encode_stream(arguments, &result) && result == utf8_cases[i].count);
        assert(stream_used_value() == utf8_cases[i].bytes);
        assert(!memcmp(output + 4, utf8_cases[i].output, utf8_cases[i].bytes));
        assert(output[3] == 0xa5 && output[4 + utf8_cases[i].bytes] == 0xa5);
    }
    /* A zero capacity requests measurement, despite a non-null output. */
    arguments[7] = 0;
    assert(ROMlib_carbon_cf_encode_stream(arguments, &result) && result == 1 && stream_used_value() == 1);
    memset(output, 0xa5, sizeof output);
    assert(ROMlib_carbon_cf_encode_stream(arguments, &result));
    assert(output[4] == 0xa5);
    /* Keep the prefix-byte table live, like the original codec. */
    store_word(string, 8, 2);
    arguments[2] = 1;
    arguments[7] = 1;
    toc[0x5935] = 0x80;
    assert(ROMlib_carbon_cf_encode_stream(arguments, &result) && result == 1 && output[4] == 0xc1);
    toc[0x5935] = 0;
    stream_callback_ok = 1;
    unsigned callback_calls = stream_calls;
    stream_alternate_callback = toc + 0x1f5c;
    for (unsigned field = 0; field < 2; ++field) {
        store_word(toc, 0x1f5c + field * 4, 0);
        assert(ROMlib_carbon_cf_encode_stream(arguments, &result) && stream_calls == ++callback_calls);
        store_word(toc, 0x1f5c, 0x1004d248);
        store_word(toc, 0x1f60, US_TO_SYN68K(toc));
    }
    pascal_references[2].function = saved_function;
    stream_alternate_callback = NULL;
    store_word(toc, 0x9ac, US_TO_SYN68K(&stream_utf8));
    /* Remaining converters retain their real function and raw nine-word ABI. */
    arguments[1] = 0;
    arguments[2] = 3;
    arguments[3] = 0;
    arguments[4] = 0x600;
    arguments[5] = 0;
    arguments[6] = US_TO_SYN68K(output + 1);
    arguments[7] = 16;
    pascal_input = string;
    pascal_output = output;
    pascal_length = pascal_chars = 3;
    pascal_used = 2;
    pascal_capacity = 17;
    pascal_encoding = 0x600;
    assert(ROMlib_carbon_cf_encode_stream(arguments, &result) && result == 3 && pascal_calls[2] == delegated + 1);
    assert(!memcmp(pascal_arguments, arguments, sizeof arguments));
    /* Warm byte storage can finish in C, including raw NUL/high bytes when
       encoding matches. Mixed tails must reach fallback before any writes. */
    borrow_active = borrow_reference_ok = borrow_callback_ok = 1;
    borrow_references[0] = (ROMlib_carbon_reference){&borrow_procedures[0], toc, 0x38ff4, 0x5dee0, 0, sizeof toc};
    borrow_references[1] = (ROMlib_carbon_reference){&borrow_procedures[1], toc, 0x38d70, 0x5dee0, 0, sizeof toc};
    borrow_references[2] = (ROMlib_carbon_reference){&borrow_procedures[2], toc, 0x357c4, 0x5dee0, 0, sizeof toc};
    store_word(toc, 0x3a70, 0);
    store_word(string, 4, 8);
    store_word(string, 8, 4);
    memcpy(string + 12, "A\0\200C", 5);
    arguments[1] = 1;
    arguments[2] = 2;
    arguments[3] = 1;
    arguments[4] = 0;
    arguments[7] = 2;
    memset(output, 0xa5, sizeof output);
    assert(ROMlib_carbon_cf_encode_stream(arguments, &result) && result == 2 && stream_used_value() == 2);
    assert(output[0] == 0xa5 && output[1] == 0 && output[2] == 0x80 && output[3] == 0xa5);
    arguments[6] = 0;
    arguments[7] = 0;
    assert(ROMlib_carbon_cf_encode_stream(arguments, &result) && result == 2 && stream_used_value() == 2);
    store_word(string, 4, 4);
    memcpy(string + 8, "\4AB\200C", 6);
    arguments[1] = 0;
    arguments[2] = 4;
    arguments[3] = 0;
    arguments[4] = 0x600;
    arguments[6] = US_TO_SYN68K(output + 1);
    for (uint32 capacity = 0; capacity <= 2; ++capacity) {
        memset(output, 0xa5, sizeof output);
        arguments[7] = capacity;
        assert(ROMlib_carbon_cf_encode_stream(arguments, &result) && result == capacity);
        assert(stream_used_value() == capacity && !memcmp(output + 1, "AB", capacity));
        assert(output[0] == 0xa5 && output[1 + capacity] == 0xa5);
    }
    assert(pascal_calls[2] == delegated + 1);
    arguments[7] = 3;
    pascal_length = 4;
    pascal_chars = pascal_used = 1;
    pascal_capacity = 4;
    memset(output, 0xa5, sizeof output);
    pascal_clean_prefix = 2;
    assert(ROMlib_carbon_cf_encode_stream(arguments, &result) && result == 1 && pascal_calls[2] == delegated + 2);
    assert(!pascal_clean_prefix && !memcmp(pascal_arguments, arguments, sizeof arguments));
    store_word(toc, 0x3a70, UINT32_MAX);
    memset(output, 0xa5, sizeof output);
    pascal_clean_prefix = 2;
    assert(ROMlib_carbon_cf_encode_stream(arguments, &result) && pascal_calls[2] == delegated + 3);
    assert(!pascal_clean_prefix && !borrow_calls[2]);
    borrow_active = 0;
    pascal_active = 0;
    ROMlib_offset = previous_offset;
}

static void string_c_conversion(void)
{
    Byte low_memory[4096] = {0}, toc[0x72a8] = {0}, record[700], external[600], output[1040];
    uint64 previous_offset = ROMlib_offset;
    ROMlib_offset = (uintptr_t)low_memory;
    pascal_names[0] = "CFStringGetCString";
    pascal_output_offset = 0;
    pascal_active = pascal_reference_ok = 1;
    pascal_fail_index = -1;
    for (unsigned i = 0; i < NELEM(pascal_names); ++i)
        pascal_references[i] = (ROMlib_carbon_reference){&pascal_procedures[i], toc, 0x395a0, 0x5dee0, 0, sizeof toc};
    for (unsigned alignment = 0; alignment < 4; ++alignment)
        for (unsigned form = 0; form < 4; ++form)
            for (unsigned n = 0; n < 5; ++n) {
                const unsigned lengths[] = {0, 1, 255, 256, 511};
                unsigned length = lengths[n], info = (form & 1 ? 4 : 0) | (form & 2 ? 0x20 : 0);
                if ((info & 4) && length > 255)
                    continue;
                Byte* string = record + alignment;
                Byte* contents = form & 2 ? external + alignment : string + (info & 4 ? 8 : 12);
                memset(record, 0xcc, sizeof record);
                store_word(string, 4, info);
                if (form & 2)
                    store_word(string, 8, US_TO_SYN68K(contents));
                if (info & 4)
                    contents[0] = (Byte)length;
                else
                    store_word(string, form & 2 ? 12 : 8, length);
                Byte* bytes = contents + !!(info & 4);
                for (unsigned i = 0; i < length; ++i)
                    bytes[i] = (Byte)(i * 7);
                memset(output, 0xa5, sizeof output);
                assert(!pascal_convert(string, output + 4, length, 0));
                for (unsigned i = 0; i < sizeof output; ++i)
                    assert(output[i] == 0xa5);
                assert(pascal_convert(string, output + 4, length + 1, 0));
                assert(!memcmp(output + 4, bytes, length) && !output[4 + length]);
                assert(output[3] == 0xa5 && output[5 + length] == 0xa5);
            }
    memset(record, 0, sizeof record);
    store_word(record, 4, 0x10);
    store_word(record, 8, 3);
    pascal_length = pascal_chars = 3;
    pascal_used = 300;
    memset(output, 0xa5, sizeof output);
    assert(pascal_convert(record, output + 4, 1024, 0x8000100));
    assert(output[303] == (Byte)(299 * 7) && !output[304] && output[305] == 0xa5);
    pascal_chars = pascal_used = 2;
    memset(output, 0xa5, sizeof output);
    assert(!pascal_convert(record, output + 4, 16, 0x100));
    assert(!output[4] && output[5] == 7 && output[6] == 0xa5);
    pascal_fail_index = 2;
    memset(output, 0xa5, sizeof output);
    assert(!pascal_convert(record, output + 4, 16, 0x100));
    assert(output[5] == 7 && output[6] == 0xa5);
    pascal_fail_index = -1;
    pascal_alloc_fail = 1;
    memset(output, 0xa5, sizeof output);
    assert(!pascal_convert(record, output + 4, 16, 0x100) && output[4] == 0xa5);
    pascal_alloc_fail = 0;
    assert((uint16)MemErr == CW(-321));
    ROMlib_carbon_reference known = pascal_references[0];
    for (unsigned variant = 0; variant < 5; ++variant) {
        pascal_references[0] = known;
        if (!variant)
            ++pascal_references[0].code_offset;
        else if (variant == 1)
            ++pascal_references[0].code_size;
        else if (variant == 2)
            ++pascal_references[0].toc_offset;
        else if (variant == 3)
            --pascal_references[0].toc_size;
        else
            pascal_references[0].toc = NULL;
        pascal_delegate_result = 0x1ff;
        assert(pascal_convert(record, output, -1, 0xdeadbeef) == 255);
        assert(pascal_arguments[0] == US_TO_SYN68K(record) && pascal_arguments[1] == US_TO_SYN68K(output) &&
               pascal_arguments[2] == UINT32_MAX && pascal_arguments[3] == 0xdeadbeef);
    }
    pascal_references[0] = known;
    pascal_reference_ok = 0;
    assert(!pascal_convert(record, output, 16, 0));
    pascal_names[0] = "CFStringGetPascalString";
    pascal_output_offset = 1;
    pascal_active = 0;
    ROMlib_offset = previous_offset;
}

static void stream_byte_encoding(void)
{
    Byte low_memory[4096] = {0}, toc[0x72a8] = {0}, string[20] = {0}, input[800], output[2000], descriptor[8];
    uint64 previous_offset = ROMlib_offset;
    ROMlib_offset = (uintptr_t)low_memory;
    pascal_active = pascal_reference_ok = 1;
    pascal_fail_index = -1;
    pascal_references[2] = (ROMlib_carbon_reference){descriptor, toc, 0x419b8, 0x5dee0, 0, sizeof toc};
    borrow_active = borrow_reference_ok = borrow_callback_ok = 1;
    borrow_references[0] = (ROMlib_carbon_reference){&borrow_procedures[0], toc, 0x38ff4, 0x5dee0, 0, sizeof toc};
    borrow_references[1] = (ROMlib_carbon_reference){&borrow_procedures[1], toc, 0x38d70, 0x5dee0, 0, sizeof toc};
    store_word(descriptor, 0, 0x100419b8);
    store_word(toc, 0x1f5c, 0x1004d248);
    store_word(toc, 0x1f60, US_TO_SYN68K(toc));
    store_word(toc, 0x593c, US_TO_SYN68K(toc + 0x1f5c));
    store_word(toc, 0x3e88, 0x8000100);
    store_word(toc, 0x6f5c, US_TO_SYN68K(&string_converters[0]));
    memcpy(toc + 0x5934, "\0\0\300\340\360\370\374\0", 8);
    store_word(string, 4, 0x28);
    store_word(string, 8, US_TO_SYN68K(input));
    memset(input, 0x80, sizeof input);
    memcpy(input, "ABC", 3);
    string_reject = string_fail_at = 0;
    string_mutated_toc = string_mutated_input = NULL;
    unsigned delegated = pascal_calls[2];
    uint32 arguments[] = {US_TO_SYN68K(string),        0, 0, 0, 0x8000100, 0, US_TO_SYN68K(output + 1), 0,
                          US_TO_SYN68K(pascal_scratch)},
           result;
    const uint32 lengths[] = {0, 1, 3, 4, 514, 515, 516, 775};
    const uint32 capacities[] = {0, 1, 3, 4, 5, 6, 1024, 1028, 1100, 1998};
    for (unsigned measure = 0; measure < 2; ++measure)
        for (unsigned n = 0; n < NELEM(lengths); ++n)
            for (unsigned c = 0; c < NELEM(capacities); ++c) {
                uint32 length = lengths[n], capacity = capacities[c], prefix = MIN(length, UINT32_C(3));
                uint32 count = measure ? length : MIN(prefix, capacity);
                if (!measure && capacity > prefix)
                    count += MIN(length - prefix, (capacity - prefix) / 2);
                uint32 bytes = count <= prefix ? count : prefix + (count - prefix) * 2;
                store_word(string, 12, length);
                arguments[2] = length;
                arguments[6] = measure ? 0 : US_TO_SYN68K(output + 1);
                arguments[7] = capacity;
                memset(output, 0xa5, sizeof output);
                MemErr = CW(-123);
                assert(ROMlib_carbon_cf_encode_stream(arguments, &result) && result == count);
                assert(stream_used_value() == bytes && (uint16)MemErr == CW(-123));
                if (!measure) {
                    assert(!memcmp(output + 1, "ABC", MIN(count, prefix)));
                    for (uint32 i = prefix; i < count; ++i)
                        assert(output[1 + prefix + (i - prefix) * 2] == 0xc6 &&
                               output[2 + prefix + (i - prefix) * 2] == 0x80);
                }
                assert(output[0] == 0xa5 && output[1 + (measure ? 0 : bytes)] == 0xa5);
            }
    assert(pascal_calls[2] == delegated && stream_byte_allocations == stream_byte_disposals);
    /* Zero-capacity measurement without borrowed storage differs from the
       borrowed-prefix early return above. A null used output is also valid. */
    store_word(string, 4, 0x20);
    store_word(string, 12, 775);
    arguments[2] = 775;
    arguments[6] = US_TO_SYN68K(output + 1);
    arguments[7] = 0;
    assert(ROMlib_carbon_cf_encode_stream(arguments, &result) && result == 775 && stream_used_value() == 1547);
    arguments[7] = 1998;
    arguments[8] = 0;
    assert(ROMlib_carbon_cf_encode_stream(arguments, &result) && result == 775);
    /* Prefixes stay unpublished if scratch allocation fails. Callback errors
       after prefix publication preserve output but not final used counts. */
    store_word(string, 4, 0x28);
    arguments[8] = US_TO_SYN68K(pascal_scratch);
    stream_byte_alloc_fail = 1;
    memset(output, 0xa5, sizeof output);
    memset(pascal_scratch, 0xa5, sizeof pascal_scratch);
    assert(!ROMlib_carbon_cf_encode_stream(arguments, &result) && output[1] == 0xa5);
    assert(stream_used_value() == 0xa5a5a5a5 && (uint16)MemErr == CW(-123));
    stream_byte_alloc_fail = 0;
    for (unsigned fail = 1; fail <= 513; fail += 512) {
        string_conversions = 0;
        string_fail_at = fail;
        memset(output, 0xa5, sizeof output);
        assert(!ROMlib_carbon_cf_encode_stream(arguments, &result) && !memcmp(output + 1, "ABC", 3));
        assert(stream_used_value() == 0xa5a5a5a5);
    }
    string_fail_at = 0;
    assert(stream_byte_allocations == stream_byte_disposals + 1);
    /* Byte decoding may replace the UTF-8 callback reentrantly. It must be
       observed before encoding, without overwriting the new cache. */
    store_word(string, 12, 4);
    arguments[2] = 4;
    arguments[7] = 10;
    string_utf8_replacement = US_TO_SYN68K(&stream_utf8);
    stream_result = 1;
    stream_used = 2;
    for (unsigned fail = 0; fail < 2; ++fail) {
        store_word(toc, 0x9c4, US_TO_SYN68K(toc + 0x1f5c));
        string_utf8_mutated_toc = toc;
        stream_callback_ok = !fail;
        unsigned calls = stream_calls;
        memset(pascal_scratch, 0xa5, sizeof pascal_scratch);
        assert(ROMlib_carbon_cf_encode_stream(arguments, &result) == !fail && stream_calls == calls + 1);
        assert(!stream_arguments[0] && stream_arguments[1] == US_TO_SYN68K(stream_byte_scratch) &&
               stream_arguments[2] == 1 && stream_arguments[3] == US_TO_SYN68K(output + 4) &&
               stream_arguments[4] == 7 && stream_arguments[5] == US_TO_SYN68K(stream_byte_scratch + 1024));
        assert(stream_used_value() == (fail ? 0xa5a5a5a5 : 5));
        uint32 cached;
        memcpy(&cached, toc + 0x9c4, 4);
        assert(CL(cached) == string_utf8_replacement);
    }
    assert(stream_byte_allocations == stream_byte_disposals + 1);
    /* Cold caches and changed callbacks delegate the original whole call. */
    store_word(string, 12, 775);
    arguments[2] = 775;
    arguments[7] = 1998;
    pascal_input = string;
    pascal_output = output;
    pascal_length = 775;
    pascal_encoding = 0x8000100;
    pascal_capacity = 1999;
    pascal_chars = pascal_used = 1;
    for (unsigned cold = 0; cold < 3; ++cold) {
        store_word(toc, 0x3a70, cold == 0 ? UINT32_MAX : 0);
        store_word(toc, 0x9a8, cold == 1 ? UINT32_MAX : 0);
        store_word(toc, 0x9c4, cold == 2 ? US_TO_SYN68K(&stream_utf8) : 0);
        memset(output, 0xa5, sizeof output);
        pascal_clean_prefix = 3;
        assert(ROMlib_carbon_cf_encode_stream(arguments, &result) && pascal_calls[2] == ++delegated);
        assert(!pascal_clean_prefix && !memcmp(pascal_arguments, arguments, sizeof arguments));
    }
    borrow_active = pascal_active = 0;
    ROMlib_offset = previous_offset;
}

static void bundle_symbols(void)
{
    Byte low_memory[4096] = {0};
    uint64 previous_offset = ROMlib_offset;
    ROMlib_offset = (uintptr_t)low_memory;
    void* (*function)(void*, const void*) = ROMlib_carbon_find_export(bundle_names[0], 2)->function;
    void* (*data)(void*, const void*) = ROMlib_carbon_find_export(bundle_names[1], 2)->function;
    Byte toc[0x72a8] = {0};
    for (unsigned i = 0; i < NELEM(bundle_names); ++i)
        bundle_references[i] =
            (ROMlib_carbon_reference){&bundle_procedures[i], toc, i ? 0xf7bc : 0xf5b0, 0x5dee0, 0, sizeof toc};
    store_word(toc, 0x408, US_TO_SYN68K(bundle_messages));
    store_word(bundle_record, 32, 1);
    store_word(bundle_record, 40, 0x12345678);
    bundle_pascal[0] = 254;
    for (unsigned i = 1; i < sizeof bundle_pascal; ++i)
        bundle_pascal[i] = (Byte)i;
    bundle_load_result = bundle_convert_result = 0x101;
    bundle_lookup_address = US_TO_SYN68K(&bundle_target);
    bundle_lookup_class = 2;
    bundle_fail_stage = -1;
    MemErr = CW(-123);
    assert(function(bundle_record, &bundle_name) == &bundle_target);
    assert((uint16)MemErr == CW(-123));
    assert(bundle_calls[2] == 1 && bundle_calls[3] == 1 && bundle_lookups == 1);
    assert(function(bundle_record, &bundle_name) == &bundle_target);
    assert(bundle_calls[2] == 1 && bundle_calls[3] == 2);
    assert(bundle_allocations == 2 && bundle_disposals == 2 && !bundle_calls[0] && !bundle_calls[1]);
    unsigned converted = bundle_calls[3], allocations = bundle_allocations;
    for (unsigned kind = 0; kind < 4; ++kind) {
        if (kind == 1)
            continue;
        store_word(bundle_record, 32, kind);
        assert(!function(bundle_record, &bundle_name));
    }
    assert(bundle_calls[3] == converted && bundle_allocations == allocations);
    store_word(bundle_record, 32, 1);
    bundle_record[36] = 0;
    bundle_load_result = 0x100;
    assert(!function(bundle_record, &bundle_name) && bundle_allocations == allocations);
    bundle_load_result = 1;
    bundle_convert_result = 0x100;
    unsigned looked_up = bundle_lookups;
    assert(!function(bundle_record, &bundle_name) && bundle_lookups == looked_up);
    bundle_convert_result = 1;
    for (unsigned symbol_class = 0; symbol_class < 5; ++symbol_class) {
        bundle_lookup_class = symbol_class;
        assert(function(bundle_record, &bundle_name) == (symbol_class == 2 ? &bundle_target : NULL));
        assert(data(bundle_record, &bundle_name) == (symbol_class == 1 ? &bundle_target : NULL));
        assert(bundle_log_arguments[0] == 21 && bundle_log_arguments[1] == 0xdeadbeef &&
               bundle_log_arguments[2] == symbol_class && bundle_log_arguments[3] == US_TO_SYN68K(bundle_scratch));
    }
    bundle_lookup_error = -2802;
    assert(!function(bundle_record, &bundle_name));
    assert(bundle_log_arguments[2] == (uint32)(int32)-2802);
    bundle_lookup_error = 0;
    bundle_lookup_class = 2;
    bundle_lookup_address = 0;
    unsigned logged = bundle_calls[5];
    assert(!function(bundle_record, &bundle_name) && bundle_calls[5] == logged);
    bundle_lookup_address = US_TO_SYN68K(&bundle_target);
    bundle_alloc_fail = 1;
    unsigned disposed = bundle_disposals;
    converted = bundle_calls[3];
    assert(!function(bundle_record, &bundle_name) && bundle_calls[3] == converted && bundle_disposals == disposed);
    assert((uint16)MemErr == CW(-123));
    bundle_alloc_fail = 0;
    assert(bundle_allocations == bundle_disposals + 1);
    for (unsigned stage = 2; stage <= 5; ++stage) {
        bundle_record[36] = stage == 2 ? 0 : 1;
        bundle_fail_stage = stage;
        bundle_lookup_error = stage >= 4 ? -2802 : 0;
        unsigned next_calls = stage == 5 ? 0 : bundle_calls[stage + 1];
        assert(!function(bundle_record, &bundle_name));
        if (stage != 5)
            assert(bundle_calls[stage + 1] == next_calls);
        assert(bundle_allocations == bundle_disposals + 1);
    }
    bundle_fail_stage = -1;
    bundle_lookup_error = 0;
    for (unsigned which = 0; which < 2; ++which) {
        ROMlib_carbon_reference known = bundle_references[which];
        for (unsigned variant = 0; variant < 5; ++variant) {
            bundle_references[which] = known;
            if (!variant)
                ++bundle_references[which].code_offset;
            else if (variant == 1)
                ++bundle_references[which].code_size;
            else if (variant == 2)
                ++bundle_references[which].toc_offset;
            else if (variant == 3)
                --bundle_references[which].toc_size;
            else
                bundle_references[which].toc = NULL;
            assert((which ? data : function)(bundle_record, &bundle_name) == &bundle_target);
        }
        bundle_fail_stage = which;
        assert(!(which ? data : function)(bundle_record, &bundle_name));
        bundle_references[which] = known;
        bundle_fail_stage = -1;
    }
    bundle_reference_missing = 1;
    assert(!function(bundle_record, &bundle_name));
    bundle_reference_missing = 0;
    assert(!function(NULL, &bundle_name));
    ROMlib_offset = previous_offset;
}

static void unichar_membership(void)
{
    const ROMlib_carbon_export* entry =
        ROMlib_carbon_find_library_export("CoreFoundationLib", "CFUniCharIsMemberOf", 2);
    assert(entry && (entry->flags & ROMLIB_CARBON_COMPANION_ONLY));
    assert(!ROMlib_carbon_find_export("CFUniCharIsMemberOf", 2));
    assert(!ROMlib_carbon_find_library_export("CarbonLib", "CFUniCharIsMemberOf", 2));
    assert(ROMlib_carbon_find_library_export("CoreFoundationLib", "CFUniCharIsMemberOf", 0) == entry);
    assert(!ROMlib_carbon_find_library_export("CoreFoundationLib", "CFUniCharIsMemberOf", 1));
    uint32 (*member)(uint32, uint32) = entry->function;
    Byte toc[0x72a8] = {0}, before_toc[sizeof toc], records[12][192], before_records[sizeof records];
    Byte* expected = calloc(12, 65536);
    assert(expected);
    unichar_reference = (ROMlib_carbon_reference){&unichar_delegate, toc, 0x461fc, 0x5dee0, 0, sizeof toc};
    unichar_reference_ok = unichar_callback_ok = 1;
    unichar_calls = 0;
    const unsigned offsets[] = {0x348, 0x344, 0x340, 0x33c, 0x338, 0x330, 0x334, 0x32c, 0x328, 0x324, 0x320, 0x31c};
    const unsigned counts[] = {0, 1, 9, 10, 11};
    for (unsigned alignment = 0; alignment < 4; ++alignment) {
        memset(records, 0xa5, sizeof records);
        memset(expected, 0, 12 * 65536);
        for (unsigned which = 0; which < 12; ++which) {
            Byte* set = records[which] + alignment;
            unsigned singles = counts[(which + alignment) % NELEM(counts)];
            unsigned ranges = counts[(which + alignment + 1) % NELEM(counts)];
            uint32 summary[2] = {0};
            store_word(set, 0, 1);
            store_word(set, 20, singles);
            store_word(set, 24, ranges);
            store_word(set, 28, singles == 1 ? 65535 : which * 3);
            store_word(set, 32, singles ? 65535 : 0);
            for (unsigned i = 0; i < singles; ++i) {
                uint32 value = i + 1 == singles ? 65535 : i * 6000 + which * 3;
                store_word(set, 36 + i * 4, value);
                expected[which * 65536 + value] = 1;
            }
            for (unsigned i = 0; i < ranges; ++i) {
                uint32 first = i ? i * 5900 + which * 7 : 1020, last = first + 12;
                store_word(set, 36 + singles * 4 + i * 8, first);
                store_word(set, 40 + singles * 4 + i * 8, last);
                for (uint32 value = first; value <= last; ++value)
                    expected[which * 65536 + value] = 1;
            }
            for (uint32 value = 0; value < 65536; ++value)
                if (expected[which * 65536 + value])
                    summary[value >> 15] |= UINT32_C(1) << ((value >> 10) & 31);
            store_word(set, 4, summary[0]);
            store_word(set, 8, summary[1]);
            store_word(toc, offsets[which], US_TO_SYN68K(set));
        }
        memcpy(before_toc, toc, sizeof toc);
        memcpy(before_records, records, sizeof records);
        for (uint32 charset = 0; charset <= 15; ++charset)
            for (uint32 value = 0; value < 65536; ++value) {
                int result =
                    charset >= 4   ? expected[(charset - 4) * 65536 + value]
                    : charset == 0 ? 0
                    : charset == 1
                        ? value <= 31 || (value >= 127 && value <= 159)
                        : value == 32 || value == 160 || (value >= 0x2000 && value <= 0x200b) || value == 0x3000 ||
                              (charset == 3 && ((value >= 9 && value <= 13) || value == 0x2028 || value == 0x2029));
                assert(member(value, charset) == (uint32)result);
            }
        assert(!memcmp(toc, before_toc, sizeof toc) && !memcmp(records, before_records, sizeof records));
    }
    assert(!unichar_calls);
    assert(member(0xabcd0041, 1) == member(0x41, 1));
    assert(!member(0, 16) && !member(0, 0xffff) && !member(0, UINT32_MAX));
    /* The live summary and version gates are authoritative even if the payload
       has matching members. Oversized private counts fail before any indexing. */
    Byte* set = records[0] + 3;
    store_word(set, 0, 2);
    assert(!member(1024, 4));
    store_word(set, 0, 1);
    store_word(set, 4, 0);
    assert(!member(1024, 4));
    store_word(set, 4, UINT32_MAX);
    store_word(set, 20, 65537);
    assert(!member(1024, 4));
    store_word(set, 20, 0);
    store_word(set, 24, UINT32_MAX);
    assert(!member(1024, 4));
    store_word(toc, offsets[0], 0);
    assert(!member(1024, 4));
    ROMlib_carbon_reference known = unichar_reference;
    for (unsigned variant = 0; variant < 5; ++variant) {
        unichar_reference = known;
        if (!variant)
            ++unichar_reference.code_offset;
        else if (variant == 1)
            ++unichar_reference.code_size;
        else if (variant == 2)
            ++unichar_reference.toc_offset;
        else if (variant == 3)
            --unichar_reference.toc_size;
        else
            unichar_reference.toc = NULL;
        unichar_result = 0xfedcba98;
        assert(member(0xabcd0041, 0x12345678) == 0x98);
        assert(unichar_calls == variant + 1 && unichar_arguments[0] == 0xabcd0041 &&
               unichar_arguments[1] == 0x12345678);
    }
    unichar_callback_ok = 0;
    assert(!member(0, 0));
    unichar_reference_ok = 0;
    assert(!member(0, 0));
    free(expected);
}

static void date_readers(void)
{
    double (*current)(void) = function("CFAbsoluteTimeGetCurrent");
    double (*absolute)(const void*) = ROMlib_carbon_cf_date_time;
    double (*interval)(const void*, const void*) = function("CFDateGetTimeIntervalSinceDate");
    LONGINT (*compare)(const void*, const void*, uint32) = function("CFDateCompare");
    const uint32 seconds[] = {0, 1, 0x7fffffff, 0x80000000, 3061151999u, 3061152000u, 3061152001u, 0xffffffff};
    for (unsigned i = 0; i < sizeof seconds / sizeof *seconds; ++i) {
        ROMlib_carbon_test_mac_time = seconds[i];
        assert(current() == (double)((int64)seconds[i] - INT64_C(3061152000)));
    }
    const double values[] = {-INFINITY, -DBL_MAX, -1.25, -0.0, 0.0, 0.5, DBL_MAX, INFINITY, NAN};
    Byte dates[sizeof values / sizeof *values][19];
    for (unsigned i = 0; i < sizeof values / sizeof *values; ++i) {
        uint64 bits;
        memcpy(&bits, &values[i], sizeof bits);
        memset(dates[i], 0xa5, sizeof dates[i]);
        store_word(dates[i] + 1, 8, bits >> 32);
        store_word(dates[i] + 1, 12, bits);
        double actual = absolute(dates[i] + 1);
        assert(!memcmp(&actual, &values[i], sizeof actual));
        assert(dates[i][0] == 0xa5 && dates[i][17] == 0xa5);
    }
    for (unsigned i = 0; i < sizeof values / sizeof *values; ++i)
        for (unsigned j = 0; j < sizeof values / sizeof *values; ++j) {
            double expected = values[i] - values[j];
            double actual = interval(dates[i] + 1, dates[j] + 1);
            assert(isnan(expected) ? isnan(actual) : actual == expected);
            LONGINT order = values[i] < values[j] ? -1 : values[i] > values[j] ? 1 : 0;
            assert(compare(dates[i] + 1, dates[j] + 1, 0xffffffff) == order);
        }
    assert(!absolute(NULL));
}

static void allocator_context(void)
{
    void (*context)(const void*, void*) = function("CFAllocatorGetContext");
    LONGINT (*preferred)(const void*, LONGINT, uint32) = function("CFAllocatorGetPreferredSizeForSize");
    Byte allocator[51], saved[51], output[39];
    const LONGINT sizes[] = {INT32_MIN, -1, 0, 1, 16, INT32_MAX};
    preferred_ok = 1;
    for (unsigned alignment = 0; alignment < 4; ++alignment) {
        Byte* object = allocator + alignment;
        memset(allocator, 0xa5, sizeof allocator);
        for (unsigned i = 0; i < 8; ++i)
            store_word(object, 16 + 4 * i, 0x80000000 + i);
        memcpy(saved, allocator, sizeof saved);
        memset(output, 0xa5, sizeof output);
        context(object, output + 1);
        uint32 version;
        memcpy(&version, output + 1, 4);
        assert(!version && !memcmp(output + 5, object + 16, 32));
        assert(output[0] == 0xa5 && output[37] == 0xa5 && output[38] == 0xa5);
        assert(!memcmp(allocator, saved, sizeof saved));
        context(object, NULL);
        for (unsigned callback = 0; callback < 2; ++callback) {
            store_word(object, 44, callback ? US_TO_SYN68K(&preferred_procedure) : 0);
            for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; ++i)
                for (unsigned j = 0; j < sizeof sizes / sizeof *sizes; ++j) {
                    preferred_calls = 0;
                    preferred_result = (uint32)sizes[j];
                    LONGINT expected = 0;
                    if (callback && sizes[i] > 0)
                        expected = sizes[j];
                    if (expected < sizes[i])
                        expected = sizes[i];
                    assert(preferred(object, sizes[i], 0xfedcba98) == expected);
                    assert(preferred_calls == (callback && sizes[i] > 0));
                    if (preferred_calls)
                        assert(preferred_arguments[0] == (uint32)sizes[i] && preferred_arguments[1] == 0xfedcba98 &&
                               preferred_arguments[2] == 0x80000000);
                }
        }
        preferred_ok = 0;
        preferred_calls = 0;
        assert(!preferred(object, 1, 0) && preferred_calls == 1);
        preferred_ok = 1;
    }
    allocator_reference_ok = allocator_fallback_ok = 1;
    allocator_reference = (ROMlib_carbon_reference){.function = (void*)(uintptr_t)0x9abc};
    allocator_fallback = US_TO_SYN68K(&allocator[3]);
    memset(output, 0xa5, sizeof output);
    context(NULL, output + 1);
    assert(!memcmp(output + 5, allocator + 3 + 16, 32));
    preferred_result = 48;
    assert(preferred(NULL, 16, 0xffffffff) == 48);
    allocator_reference_ok = 0;
    memset(output, 0xa5, sizeof output);
    context(NULL, output + 1);
    for (unsigned i = 0; i < sizeof output; ++i)
        assert(output[i] == 0xa5);
    assert(!preferred(NULL, 16, 0));
}

static void allocator_allocation(void)
{
    void* (*allocate)(const void*, LONGINT, uint32) = function("CFAllocatorAllocate");
    void* (*reallocate)(const void*, void*, LONGINT, uint32) = function("CFAllocatorReallocate");
    void (*deallocate)(const void*, void*) = function("CFAllocatorDeallocate");
    Byte record[51], saved[51], block[32], replacement[32];
    const LONGINT sizes[] = {INT32_MIN, -1, 0, 1, 16, INT32_MAX};
    const uint32 hints = 0xfedcba98, info = 0x80000001;
    allocation_result = US_TO_SYN68K(&replacement[0]);
    allocation_ok = 1;
    for (unsigned alignment = 0; alignment < 4; ++alignment) {
        Byte* allocator = record + alignment;
        memset(record, 0xa5, sizeof record);
        store_word(allocator, 16, info);
        for (unsigned mask = 0; mask < 8; ++mask) {
            for (unsigned i = 0; i < 3; ++i)
                store_word(allocator, 32 + 4 * i, mask & (1u << i) ? US_TO_SYN68K(&allocation_procedures[i]) : 0);
            memcpy(saved, record, sizeof saved);
            for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; ++i) {
                LONGINT size = sizes[i];
                allocation_calls = 0;
                assert(allocate(allocator, size, hints) == (size && (mask & 1) ? replacement : NULL));
                assert(allocation_calls == (size != 0));
                if (size) {
                    assert(allocation_kind == (mask & 1 ? 0 : 3) && allocation_count == 3);
                    assert(allocation_arguments[0] == (uint32)size && allocation_arguments[1] == hints &&
                           allocation_arguments[2] == info);
                }
                for (unsigned present = 0; present < 2; ++present) {
                    void* pointer = present ? block : NULL;
                    int callback = -1;
                    if (!pointer && size > 0)
                        callback = mask & 1 ? 0 : 3;
                    else if (pointer && !size)
                        callback = mask & 4 ? 2 : -1;
                    else if ((pointer || size) && (mask & 2))
                        callback = 1;
                    allocation_calls = 0;
                    assert(reallocate(allocator, pointer, size, hints) ==
                           (callback == 0 || callback == 1 ? replacement : NULL));
                    assert(allocation_calls == (callback >= 0));
                    if (callback >= 0) {
                        assert(allocation_kind == (unsigned)callback);
                        uint32 raw_pointer = pointer ? US_TO_SYN68K(pointer) : 0;
                        if (callback == 0 || callback == 3) {
                            assert(allocation_count == 3 && allocation_arguments[0] == (uint32)size &&
                                   allocation_arguments[1] == hints && allocation_arguments[2] == info);
                        }
                        else if (callback == 1) {
                            assert(allocation_count == 4 && allocation_arguments[0] == raw_pointer &&
                                   allocation_arguments[1] == (uint32)size && allocation_arguments[2] == hints &&
                                   allocation_arguments[3] == info);
                        }
                        else
                            assert(allocation_count == 2 && allocation_arguments[0] == raw_pointer &&
                                   allocation_arguments[1] == info);
                    }
                    allocation_calls = 0;
                    deallocate(allocator, pointer);
                    assert(allocation_calls == (present && (mask & 4) != 0));
                    if (allocation_calls)
                        assert(allocation_kind == 2 && allocation_count == 2 &&
                               allocation_arguments[0] == US_TO_SYN68K(pointer) && allocation_arguments[1] == info);
                }
            }
            assert(!memcmp(record, saved, sizeof record));
        }
        allocation_ok = 0;
        allocation_calls = 0;
        assert(!allocate(allocator, 16, hints) && allocation_calls == 1);
        assert(!reallocate(allocator, block, 16, hints) && allocation_calls == 2);
        deallocate(allocator, block);
        assert(allocation_calls == 3);
        allocation_ok = 1;
        allocation_result = 0;
        assert(!allocate(allocator, 16, hints) && !reallocate(allocator, block, 16, hints));
        allocation_result = US_TO_SYN68K(&replacement[0]);
    }
    allocator_reference_ok = allocator_fallback_ok = 1;
    allocator_reference = (ROMlib_carbon_reference){.function = (void*)(uintptr_t)0x9abc};
    allocator_fallback = US_TO_SYN68K(&record[3]);
    assert(allocate(NULL, 16, hints) == replacement);
    assert(reallocate(NULL, block, 16, hints) == replacement);
    allocation_calls = 0;
    deallocate(NULL, block);
    assert(allocation_calls == 1 && allocation_kind == 2);
    allocator_reference_ok = 0;
    allocation_calls = 0;
    assert(!allocate(NULL, 16, hints) && !reallocate(NULL, block, 16, hints));
    deallocate(NULL, block);
    assert(!allocation_calls);
}

static void allocator_creation(void)
{
    uint32 (*type_id)(void) = function("CFAllocatorGetTypeID");
    void* (*create)(uint32, const void*) = function("CFAllocatorCreate");
    static Byte toc[0x72a8];
    Byte parent[48], context[39], output[55];
    memset(parent, 0, sizeof parent);
    store_word(parent, 16, 0xfedcba98);
    store_word(parent, 32, US_TO_SYN68K(&allocation_procedures[0]));
    uint32 parent_address = US_TO_SYN68K(&parent[0]);
    allocator_reference = (ROMlib_carbon_reference){.function = (void*)(uintptr_t)0x9abc,
                                                    .toc = toc,
                                                    .code_offset = 0x17344,
                                                    .code_size = 0x5dee0,
                                                    .toc_size = sizeof toc};
    allocator_reference_ok = 1;
    for (unsigned i = 0; i < 2; ++i) {
        creation_references[i] = allocator_reference;
        creation_references[i].function = &creation_delegates[i];
        creation_references[i].code_offset = i ? 0x173f4 : 0x1733c;
    }
    creation_reference_ok = creation_delegate_ok = creation_retain_ok = allocation_ok = 1;
    creation_info = 0x80000002;
    store_word(toc, 0x3164, parent_address);
    store_word(toc, 0x6f28, 0);
    store_word(toc, 0xa68, 512);
    store_word(toc, 0xa6c, 0);
    const uint32 types[] = {0, 2, 255, 256, UINT32_MAX};
    for (unsigned alignment = 0; alignment < 4; ++alignment)
        for (unsigned retained = 0; retained < 2; ++retained)
            for (unsigned owner = 0; owner < 3; ++owner)
                for (unsigned i = 0; i < sizeof types / sizeof *types; ++i) {
                    Byte* input = context + alignment;
                    Byte* object = output + alignment + 1;
                    memset(context, 0xa5, sizeof context);
                    memset(output, 0xa5, sizeof output);
                    store_word(input, 0, 0x12345678);
                    store_word(input, 4, 0x80000001);
                    store_word(input, 8, retained ? US_TO_SYN68K(&creation_retain_procedure) : 0);
                    for (unsigned word = 3; word < 9; ++word)
                        store_word(input, word * 4, 0x80000100 + word);
                    store_word(input, 20, US_TO_SYN68K(&allocation_procedures[0]));
                    store_word(toc, 0x89c, types[i]);
                    assert(type_id() == types[i]);
                    allocation_result = US_TO_SYN68K(object);
                    allocation_calls = creation_retain_calls = 0;
                    uint32 allocator = owner == 0 ? 0 : owner == 1 ? parent_address : 0x221;
                    assert(create(allocator, input) == object);
                    assert(allocation_calls == 1 && allocation_kind == 0 && allocation_count == 3);
                    assert(allocation_arguments[0] == 48 && allocation_arguments[1] == 0);
                    assert(creation_retain_calls == retained);
                    if (retained)
                        assert(creation_input == 0x80000001);
                    uint32 info = retained ? creation_info : 0x80000001;
                    assert(allocation_arguments[2] == (owner == 2 ? info : 0xfedcba98));
                    Byte expected[48];
                    memset(expected, 0, sizeof expected);
                    store_word(expected, 4, types[i] > 0 && types[i] < 512 ? (types[i] & 255) << 8 : 0);
                    store_word(expected, 8, owner == 2 ? 0x221 : parent_address);
                    memcpy(expected + 12, input, 4);
                    store_word(expected, 16, info);
                    memcpy(expected + 20, input + 8, 28);
                    assert(!memcmp(object, expected, sizeof expected));
                    assert(object[-1] == 0xa5 && object[48] == 0xa5);
                }
    // Callbacks may change the live context before construction copies it.
    Byte* input = context + 3;
    store_word(input, 8, US_TO_SYN68K(&creation_retain_procedure));
    creation_mutated_context = input;
    allocation_result = US_TO_SYN68K(&output[1]);
    assert(create(parent_address, input) == output + 1);
    Byte expected_version[4];
    store_word(expected_version, 0, 0x87654321);
    assert(!memcmp(output + 13, expected_version, 4));
    uint32 retained_callback;
    memcpy(&retained_callback, output + 21, 4);
    assert(!retained_callback);
    creation_mutated_context = NULL;
    store_word(input, 8, US_TO_SYN68K(&creation_retain_procedure));
    // Allocation failure retains info; retain failure stops before allocation.
    allocation_result = 0;
    allocation_calls = creation_retain_calls = 0;
    assert(!create(0x221, input) && allocation_calls == 1 && creation_retain_calls == 1);
    creation_retain_ok = 0;
    allocation_calls = 0;
    assert(!create(parent_address, input) && !allocation_calls);
    creation_retain_ok = 1;
    ROMlib_carbon_reference good[2];
    memcpy(good, creation_references, sizeof good);
    creation_delegate_results[0] = 0xfedcba98;
    creation_delegate_results[1] = US_TO_SYN68K(&output[1]);
    creation_delegate_calls[0] = creation_delegate_calls[1] = 0;
    for (unsigned variant = 0; variant < 5; ++variant) {
        memcpy(creation_references, good, sizeof good);
        for (unsigned i = 0; i < 2; ++i) {
            if (variant == 0)
                ++creation_references[i].code_offset;
            else if (variant == 1)
                ++creation_references[i].code_size;
            else if (variant == 2)
                ++creation_references[i].toc_offset;
            else if (variant == 3)
                --creation_references[i].toc_size;
            else
                creation_references[i].toc = NULL;
        }
        assert(type_id() == 0xfedcba98 && create(0x221, input) == output + 1);
        assert(creation_delegate_arguments[0] == 0x221 && creation_delegate_arguments[1] == US_TO_SYN68K(input));
    }
    assert(creation_delegate_calls[0] == 5 && creation_delegate_calls[1] == 5);
    creation_delegate_ok = 0;
    assert(!type_id() && !create(0x221, input));
    creation_reference_ok = 0;
    assert(!type_id() && !create(0x221, input));
}

static uint32 load_word(const Byte* object, unsigned offset)
{
    uint32 encoded;
    memcpy(&encoded, object + offset, 4);
    return CL(encoded);
}

static void ownership(void)
{
    void* (*retain)(void*) = function("CFRetain");
    void (*release)(void*) = function("CFRelease");
    LONGINT (*count)(const void*) = function("CFGetRetainCount");
    void* (*allocator)(const void*) = function("CFGetAllocator");
    static Byte toc[0x72a8];
    Byte storage[55], parent[48], klass[36], other[48];
    Byte* object = storage + 5;
    const uint32 offsets[] = {0x533e4, 0x53488, 0x535f0, 0x53844, 0x21e74, 0x224c8, 0x22708};
    for (unsigned i = 0; i < 7; ++i)
        ownership_references[i] = (ROMlib_carbon_reference){.function = &ownership_procedures[i],
                                                            .toc = toc,
                                                            .code_offset = offsets[i],
                                                            .code_size = 0x5dee0,
                                                            .toc_size = sizeof toc};
    ownership_reference_ok = ownership_delegate_ok = ownership_callback_ok = 1;
    ownership_dictionary_fail = -1;
    ownership_key = US_TO_SYN68K(object);
    store_word(toc, 0xa64, 0xabc00001);
    for (uint32 prefix = 0; prefix <= 0xffff; ++prefix) {
        memset(ownership_calls, 0, sizeof ownership_calls);
        ownership_dictionary = 3;
        store_word(object, 4, (prefix << 16) | 0xa57d);
        uint32 total = (prefix & 0x8000 ? 3u << 15 : 0) + (prefix & 0x7fff) + 1;
        assert(count(object) == (LONGINT)total);
        assert(ownership_calls[4] == (prefix >> 15));
        memset(ownership_calls, 0, sizeof ownership_calls);
        assert(retain(object) == object);
        uint32 next = prefix + 1;
        int overflow = !(next & 0x7fff);
        assert(load_word(object, 4) == (((overflow ? 0x8000 : next) << 16) | 0xa57d));
        assert(ownership_dictionary == (overflow ? 4 : 3));
        assert(ownership_calls[4] == (unsigned)overflow && ownership_calls[5] == (unsigned)overflow);
        if (prefix) {
            store_word(object, 4, (prefix << 16) | 0xa57d);
            ownership_dictionary = 2;
            memset(ownership_calls, 0, sizeof ownership_calls);
            release(object);
            assert(load_word(object, 4) == (((prefix == 0x8000 ? 0xffff : prefix - 1) << 16) | 0xa57d));
            assert(ownership_dictionary == (prefix == 0x8000 ? 1 : 2));
        }
    }
    const uint32 highs[] = {0, 1, 2, 0xffff, 0x10000, UINT32_MAX};
    for (unsigned i = 0; i < sizeof highs / sizeof *highs; ++i) {
        ownership_dictionary = highs[i];
        store_word(object, 4, 0xffff0755);
        uint32 total = (highs[i] << 15) + 32768;
        assert(count(object) == (total & 0x80000000 ? INT32_MAX : (LONGINT)total));
        store_word(object, 4, 0x80000755);
        release(object);
        assert(load_word(object, 4) == (highs[i] == 1 ? 0x7fff0755 : 0xffff0755));
        assert(ownership_dictionary == (highs[i] == 1 ? 0 : highs[i] - 1));
    }
    for (int failure = 4; failure <= 6; ++failure) {
        ownership_dictionary_fail = failure;
        ownership_dictionary = 1;
        store_word(object, 4, 0x7fff0780);
        if (failure < 6) {
            assert(!retain(object) && load_word(object, 4) == 0x7fff0780);
            store_word(object, 4, 0x80000780);
        }
        else
            store_word(object, 4, 0x80000780);
        ownership_dictionary = failure == 5 ? 2 : 1;
        release(object);
        assert(load_word(object, 4) == 0x80000780);
    }
    ownership_dictionary_fail = -1;
    memset(parent, 0, sizeof parent);
    memset(klass, 0, sizeof klass);
    memset(other, 0, sizeof other);
    store_word(parent, 4, 0x10200);
    store_word(parent, 16, 0xfedcba98);
    store_word(parent, 40, US_TO_SYN68K(&allocation_procedures[2]));
    store_word(toc, 0x3164, US_TO_SYN68K(&parent[0]));
    store_word(toc, 0x6f28, US_TO_SYN68K(&other[0]));
    store_word(toc, 0x65c4 + 7 * 4, US_TO_SYN68K(&klass[0]));
    store_word(klass, 16, US_TO_SYN68K(&ownership_finalize));
    ownership_mode = allocation_ok = 1;
    for (unsigned alignment = 0; alignment < 4; ++alignment) {
        object = storage + 4 + alignment;
        for (unsigned system = 0; system < 2; ++system) {
            store_word(storage + alignment, 0, US_TO_SYN68K(&parent[0]));
            store_word(object, 4, 0x700 | (system ? 0x80 : 0));
            store_word(parent, 4, 0x10200);
            assert(allocator(object) == parent);
            ownership_event_count = allocation_calls = 0;
            ownership_poison = object;
            release(object);
            assert(ownership_event_count == 2 && ownership_events[0] == 1 && ownership_events[1] == 2);
            assert(allocation_calls == 1 && allocation_arguments[0] == US_TO_SYN68K(system ? object : object - 4) &&
                   allocation_arguments[1] == 0xfedcba98);
            assert(load_word(parent, 4) == (system ? 0x10200 : 0x200));
        }
        for (unsigned own = 0; own < 2; ++own) {
            memset(object, 0, 48);
            store_word(object, 4, 0x200);
            store_word(object, 8, own ? 0x221 : US_TO_SYN68K(&parent[0]));
            store_word(object, 16, 0x80000001);
            store_word(object, 24, US_TO_SYN68K(&ownership_info_release));
            store_word(object, 40, US_TO_SYN68K(&allocation_procedures[2]));
            assert(allocator(object) == (own ? object : parent));
            ownership_event_count = allocation_calls = 0;
            release(object);
            assert(ownership_event_count == 2 && ownership_events[0] == (own ? 2 : 3) &&
                   ownership_events[1] == (own ? 3 : 2));
            assert(allocation_arguments[0] == US_TO_SYN68K(object) &&
                   allocation_arguments[1] == (own ? 0x80000001 : 0xfedcba98));
            assert(ownership_argument == 0x80000001);
        }
    }
    ownership_poison = NULL;
    object = storage + 5;
    store_word(object, 4, 0x700);
    ownership_mutated_object = object;
    ownership_event_count = 0;
    release(object);
    assert(allocation_arguments[0] == US_TO_SYN68K(object));
    ownership_mutated_object = NULL;
    ownership_callback_ok = 0;
    ownership_event_count = allocation_calls = 0;
    store_word(object, 4, 0x780);
    release(object);
    assert(ownership_event_count == 1 && !allocation_calls);
    ownership_callback_ok = 1;
    store_word(storage + 1, 0, US_TO_SYN68K(&parent[0]));
    store_word(parent, 4, 0x10200);
    store_word(object, 4, 0x700);
    allocation_ok = 0;
    ownership_event_count = allocation_calls = 0;
    release(object);
    assert(ownership_event_count == 2 && allocation_calls == 1 && load_word(parent, 4) == 0x10200);
    allocation_ok = 1;
    memset(object, 0, 48);
    store_word(object, 4, 0x200);
    store_word(object, 8, US_TO_SYN68K(&parent[0]));
    store_word(object, 16, 0x80000001);
    store_word(object, 24, US_TO_SYN68K(&ownership_info_release));
    ownership_callback_ok = 0;
    ownership_event_count = allocation_calls = 0;
    release(object);
    assert(ownership_event_count == 1 && ownership_events[0] == 3 && !allocation_calls);
    ownership_callback_ok = 1;
    store_word(object, 8, 0x221);
    store_word(object, 40, US_TO_SYN68K(&allocation_procedures[2]));
    allocation_ok = 0;
    ownership_event_count = allocation_calls = 0;
    release(object);
    assert(ownership_event_count == 1 && ownership_events[0] == 2 && allocation_calls == 1);
    allocation_ok = 1;
    ROMlib_carbon_reference good[4];
    memcpy(good, ownership_references, sizeof good);
    ownership_delegate_results[0] = US_TO_SYN68K(&other[0]);
    ownership_delegate_results[2] = 0x12345678;
    ownership_delegate_results[3] = US_TO_SYN68K(&parent[0]);
    for (unsigned variant = 0; variant < 5; ++variant) {
        memcpy(ownership_references, good, sizeof good);
        for (unsigned i = 0; i < 4; ++i) {
            if (variant == 0)
                ++ownership_references[i].code_offset;
            else if (variant == 1)
                ++ownership_references[i].code_size;
            else if (variant == 2)
                ++ownership_references[i].toc_offset;
            else if (variant == 3)
                --ownership_references[i].toc_size;
            else
                ownership_references[i].toc = NULL;
        }
        assert(retain(object) == other && count(object) == 0x12345678 && allocator(object) == parent);
        release(object);
        assert(ownership_argument == US_TO_SYN68K(object));
    }
    ownership_delegate_ok = 0;
    assert(!retain(object) && !count(object) && !allocator(object));
    release(object);
    ownership_reference_ok = 0;
    assert(!retain(object) && !count(object) && !allocator(object));
    release(object);
    ownership_mode = 0;
    assert(!retain(NULL) && !count(NULL) && !allocator(NULL));
    release(NULL);
}

static void array_creation(void)
{
    void* (*create)(const void*, const void*, LONGINT, const void*) = function("CFArrayCreate");
    uint32 (*type_id)(void) = function("CFArrayGetTypeID");
    uint32 (*value)(const void*, LONGINT) = function("CFArrayGetValueAtIndex");
    static Byte toc[0x72a8];
    Byte system[48], custom[48], klass[36], callbacks[23], values[35], pool[80];
    memset(system, 0, sizeof system);
    memset(custom, 0, sizeof custom);
    memset(klass, 0, sizeof klass);
    for (unsigned i = 0; i < 2; ++i)
        array_creation_references[i] = (ROMlib_carbon_reference){.function = &array_creation_delegates[i],
                                                                 .toc = toc,
                                                                 .code_offset = i ? 0x1b8b4 : 0x1b5c4,
                                                                 .code_size = 0x5dee0,
                                                                 .toc_size = sizeof toc};
    allocator_reference = array_creation_references[0];
    allocator_reference.code_offset = 0x17344;
    allocator_reference_ok = array_reference_ok = array_delegate_ok = allocation_ok = array_initialize_ok = 1;
    ownership_references[0] = array_creation_references[0];
    ownership_references[0].code_offset = 0x533e4;
    ownership_reference_ok = 1;
    store_word(system, 16, 0xfedcba98);
    store_word(custom, 16, 0xdeadbeef);
    store_word(system, 32, US_TO_SYN68K(&allocation_procedures[0]));
    store_word(custom, 32, US_TO_SYN68K(&allocation_procedures[0]));
    store_word(toc, 0x3164, US_TO_SYN68K(&system[0]));
    store_word(toc, 0x8c0, 7);
    store_word(toc, 0xa68, 256);
    store_word(toc, 0xa6c, 0);
    store_word(toc, 0x65c4 + 7 * 4, US_TO_SYN68K(&klass[0]));
    store_word(toc, 0x3238, US_TO_SYN68K(&array_retain_procedures[2]));
    store_word(toc, 0x323c, 0x11111111);
    store_word(toc, 0x3240, 0x22222222);
    store_word(toc, 0x3244, 0x33333333);
    const uint32 words[] = {0, 1, 0xdeadbeef, UINT32_MAX, 0x80000000, 0x7fffffff, 0x12345678, 0xaaaaaaaa};
    for (unsigned alignment = 0; alignment < 4; ++alignment)
        for (unsigned owner = 0; owner < 4; ++owner)
            for (unsigned form = 0; form < 7; ++form)
                for (unsigned length = 0; length <= 8; ++length) {
                    Byte* selected = owner < 2 ? system : custom;
                    int prefixed = owner >= 2;
                    const void* input_allocator = owner & 1 ? selected : NULL;
                    store_word(toc, 0x6f28, prefixed ? US_TO_SYN68K(&custom[0]) : 0);
                    store_word(custom, 4, 0x200);
                    Byte* input = callbacks + alignment;
                    Byte* elements = values + alignment;
                    memset(callbacks, 0, sizeof callbacks);
                    if (form == 3 || form == 4)
                        memcpy(input, toc + 0x3234, 20);
                    else if (form >= 5) {
                        store_word(input, 4, form == 5 ? US_TO_SYN68K(&array_retain_procedures[0]) : 0);
                        store_word(input, 8, 0x76543210);
                    }
                    if (form == 2 || form == 4 || form == 5)
                        store_word(input, 0, 0x12345678);
                    for (unsigned i = 0; i < 8; ++i)
                        store_word(elements, i * 4, words[i]);
                    memset(pool, 0xa5, sizeof pool);
                    Byte* allocation = pool + 1 + alignment;
                    array_object = allocation + (prefixed ? 4 : 0);
                    allocation_result = US_TO_SYN68K(allocation);
                    array_original_allocator = input_allocator ? US_TO_SYN68K(input_allocator) : 0;
                    allocation_calls = array_retain_calls = array_initialize_calls = 0;
                    unsigned kind = form < 3 ? 0 : form < 5 ? 1 : 3;
                    unsigned header = kind == 3 ? 32 : 12;
                    assert(type_id() == 7);
                    assert(create(input_allocator, elements, length, form ? input : NULL) == array_object);
                    assert(allocation_calls == 1 &&
                           allocation_arguments[0] == header + length * 4 + (prefixed ? 4 : 0));
                    assert(allocation_arguments[1] == 0 &&
                           allocation_arguments[2] == (prefixed ? 0xdeadbeef : 0xfedcba98));
                    assert(load_word(array_object, 4) == (0x700 | (prefixed ? 0 : 0x80) | (kind << 2)));
                    assert(load_word(array_object, 8) == length);
                    assert(array_retain_calls == (form >= 3 && form <= 5 ? length : 0));
                    if (prefixed) {
                        assert(load_word(allocation, 0) == US_TO_SYN68K(&custom[0]));
                        assert(load_word(custom, 4) == 0x10200);
                    }
                    if (kind == 3)
                        assert(!memcmp(array_object + 12, input, 20));
                    for (unsigned i = 0; i < length; ++i)
                        assert(value(array_object, i) == (words[i] ^ (form == 5 ? 0x55555555 : 0)));
                    assert(allocation[-1] == 0xa5 && array_object[header + length * 4] == 0xa5);
                }
    Byte* input = callbacks + 3;
    Byte* elements = values + 3;
    memset(input, 0, 20);
    store_word(input, 4, US_TO_SYN68K(&array_retain_procedures[0]));
    store_word(input, 8, 0x76543210);
    store_word(toc, 0x6f28, 0);
    array_original_allocator = 0;
    allocation_result = US_TO_SYN68K(&pool[1]);
    array_object = pool + 1;
    array_changed_callbacks = input;
    store_word(klass, 8, US_TO_SYN68K(&array_initialize_procedure));
    array_initialize_calls = array_retain_calls = 0;
    assert(create(NULL, elements, 3, input) == array_object && array_initialize_calls == 1);
    assert(load_word(array_object, 12) == 0xfedcba98 && value(array_object, 0) == (words[0] ^ 0xaaaaaaaa));
    array_changed_callbacks = NULL;
    store_word(klass, 8, 0);
    store_word(input, 4, US_TO_SYN68K(&array_retain_procedures[0]));
    array_change_retain = 1;
    array_retain_calls = 0;
    assert(create(NULL, elements, 3, input) == array_object && array_retain_calls == 3);
    assert(value(array_object, 0) == (words[0] ^ 0x55555555) && value(array_object, 1) == (words[1] ^ 0xaaaaaaaa));
    array_change_retain = 0;
    array_fail_at = 2;
    array_retain_calls = 0;
    assert(!create(NULL, elements, 3, input) && array_retain_calls == 2 && !load_word(array_object, 8));
    array_fail_at = 0;
    allocation_result = 0;
    array_retain_calls = 0;
    assert(!create(NULL, elements, 3, input) && !array_retain_calls);
    allocation_result = US_TO_SYN68K(&pool[1]);
    allocation_ok = 0;
    assert(!create(NULL, elements, 3, input) && !array_retain_calls);
    allocation_ok = 1;
    store_word(klass, 8, US_TO_SYN68K(&array_initialize_procedure));
    array_initialize_ok = 0;
    assert(!create(NULL, elements, 3, input) && !array_retain_calls);
    array_initialize_ok = 1;
    store_word(klass, 8, 0);
    allocation_calls = 0;
    assert(!create(NULL, elements, -1, NULL) && !create(NULL, elements, INT32_MAX, NULL) &&
           !create(NULL, NULL, 1, NULL) && !allocation_calls);
    store_word(toc, 0x8c0, 0);
    assert(!create(NULL, elements, 0, NULL) && !allocation_calls);
    store_word(toc, 0x8c0, 256);
    assert(type_id() == 256 && !create(NULL, elements, 0, NULL) && !allocation_calls);
    ROMlib_carbon_reference good[2];
    memcpy(good, array_creation_references, sizeof good);
    array_delegate_results[0] = 0xfedcba98;
    array_delegate_results[1] = US_TO_SYN68K(&pool[1]);
    for (unsigned variant = 0; variant < 5; ++variant) {
        memcpy(array_creation_references, good, sizeof good);
        for (unsigned i = 0; i < 2; ++i) {
            if (variant == 0)
                ++array_creation_references[i].code_offset;
            else if (variant == 1)
                ++array_creation_references[i].code_size;
            else if (variant == 2)
                ++array_creation_references[i].toc_offset;
            else if (variant == 3)
                --array_creation_references[i].toc_size;
            else
                array_creation_references[i].toc = NULL;
        }
        assert(type_id() == 0xfedcba98 && create(custom, elements, -1, input) == pool + 1);
        assert(array_delegate_arguments[0] == US_TO_SYN68K(&custom[0]) &&
               array_delegate_arguments[1] == US_TO_SYN68K(elements) && array_delegate_arguments[2] == UINT32_MAX &&
               array_delegate_arguments[3] == US_TO_SYN68K(input));
    }
    array_delegate_ok = 0;
    assert(!type_id() && !create(NULL, NULL, 0, NULL));
    array_reference_ok = 0;
    assert(!type_id() && !create(NULL, NULL, 0, NULL));
}

static void allocator_default(void)
{
    void* (*get)(void) = function("CFAllocatorGetDefault");
    assert(ROMlib_carbon_find_export("CFAllocatorGetDefault", 2)->flags == ROMLIB_CARBON_PPC_ONLY);
    static Byte toc[0x72a8], before[0x72a8], system[24], replacement[24], custom[24];
    allocator_reference = (ROMlib_carbon_reference){.function = (void*)(uintptr_t)0x9abc,
                                                    .toc = toc,
                                                    .code_offset = 0x17344,
                                                    .code_size = 0x5dee0,
                                                    .toc_offset = 0,
                                                    .toc_size = sizeof toc};
    allocator_reference_ok = allocator_fallback_ok = 1;
    allocator_fallback_calls = 0;
    store_word(toc, 0x3164, US_TO_SYN68K(&system[0]));
    store_word(toc, 0x6f28, 0);
    assert(get() == system);
    store_word(toc, 0x6f28, US_TO_SYN68K(&custom[0]));
    assert(get() == custom);
    store_word(toc, 0x6f28, 0);
    store_word(toc, 0x3164, US_TO_SYN68K(&replacement[0]));
    memcpy(before, toc, sizeof toc);
    assert(get() == replacement && !memcmp(before, toc, sizeof toc));
    assert(!allocator_fallback_calls);
    ROMlib_carbon_reference good = allocator_reference;
    allocator_fallback = US_TO_SYN68K(&custom[0]);
    for (unsigned variant = 0; variant < 5; ++variant) {
        allocator_reference = good;
        switch (variant) {
        case 0:
            allocator_reference.code_offset += 4;
            break;
        case 1:
            allocator_reference.code_size += 4;
            break;
        case 2:
            allocator_reference.toc_offset += 4;
            break;
        case 3:
            allocator_reference.toc_size -= 4;
            break;
        case 4:
            allocator_reference.toc = NULL;
            break;
        }
        assert(get() == custom);
    }
    assert(allocator_fallback_calls == 5);
    allocator_reference_ok = 0;
    assert(!get() && allocator_fallback_calls == 5);
    allocator_reference_ok = 1;
    allocator_fallback_ok = 0;
    assert(!get() && allocator_fallback_calls == 6);
}

void ROMlib_carbon_corefoundation_tests(void)
{
    array_readers();
    string_readers();
    string_character_readers();
    string_builtin_byte_readers();
    bundle_symbols();
    string_pascal_conversion();
    stream_encoding();
    string_c_conversion();
    string_borrowed_pointers();
    stream_byte_encoding();
    string_encoding_exports();
    unichar_membership();
    date_readers();
    allocator_default();
    allocator_context();
    allocator_allocation();
    allocator_creation();
    ownership();
    array_creation();
    LONGINT (*array_count)(const void*) = function("CFArrayGetCount");
    uint32 (*type_id)(const void*) = function("CFGetTypeID");
    LONGINT (*data_length)(const void*) = function("CFDataGetLength");
    Byte* (*data_bytes)(const void*) = function("CFDataGetBytePtr");
    Byte* (*mutable_bytes)(const void*) = function("CFDataGetMutableBytePtr");
    void (*copy)(const void*, LONGINT, LONGINT, void*) = function("CFDataGetBytes");
    Byte record[32], original[32], payload[256], output[258];
    const uint32 counts[] = {0, 1, 255, 65536, 0x12345678, 0x7fffffff, 0x80000000, 0xffffffff};
    for (uint32 flags = 0; flags < 65536; ++flags) {
        memset(record, 0xa5, sizeof record);
        store_word(record, 4, 0xdead0000 | flags);
        assert(type_id(record) == flags >> 8);
    }
    assert(!type_id(NULL));
    for (unsigned alignment = 0; alignment < 4; ++alignment)
        for (unsigned i = 0; i < sizeof counts / sizeof *counts; ++i) {
            memset(record, 0xa5, sizeof record);
            store_word(record + alignment, 8, counts[i]);
            store_word(record + alignment, 20, US_TO_SYN68K(&payload[0]));
            memcpy(original, record, sizeof record);
            assert(array_count(record + alignment) == (LONGINT)counts[i]);
            assert(data_length(record + alignment) == (LONGINT)counts[i]);
            assert(data_bytes(record + alignment) == payload && mutable_bytes(record + alignment) == payload);
            assert(!memcmp(record, original, sizeof record));
        }
    memset(record, 0, sizeof record);
    store_word(record, 8, sizeof payload);
    store_word(record, 20, US_TO_SYN68K(&payload[0]));
    for (unsigned i = 0; i < sizeof payload; ++i)
        payload[i] = i;
    for (unsigned start = 0; start <= sizeof payload; ++start)
        for (unsigned length = 0; length <= sizeof payload - start; ++length) {
            memset(output, 0xa5, sizeof output);
            copy(record, start, length, output + 1);
            assert(!memcmp(output + 1, payload + start, length));
            assert(output[0] == 0xa5 && output[length + 1] == 0xa5);
        }
    memset(output, 0xa5, sizeof output);
    const LONGINT bad[][2] = {{-1, 1}, {0, -1}, {257, 0}, {256, 1}, {255, 2}, {INT32_MAX, INT32_MAX}, {1, INT32_MAX}};
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; ++i) {
        copy(record, bad[i][0], bad[i][1], output + 1);
        for (unsigned j = 0; j < sizeof output; ++j)
            assert(output[j] == 0xa5);
    }
    copy(record, 0, 0, NULL);
    copy(record, 0, 1, NULL);
    copy(NULL, 0, 1, output);
    assert(!array_count(NULL) && !data_length(NULL) && !data_bytes(NULL) && !mutable_bytes(NULL));
    copy(record, 0, 255, payload + 1);
    for (unsigned i = 1; i < sizeof payload; ++i)
        assert(payload[i] == i - 1);
    store_word(record, 20, 0);
    assert(!data_bytes(record) && !mutable_bytes(record));
    copy(record, 0, 1, output);
    assert(output[0] == 0xa5);
}
