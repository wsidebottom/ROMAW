/*+===================================================================
  File:      urls_test.c

  Summary:   Original URL record, ownership and dependency protocol tests.

  Classes:   None.

  Functions: ROMlib_carbon_url_test_callback, ROMlib_carbon_url_test_reference, ROMlib_carbon_url_tests

  Origin:    Existing repository source in packages/carbonlib/tests; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Original URL record, ownership and dependency protocol tests. */
#include "../src/internal.h"
#include "urls_test.h"
#undef NDEBUG
#include <assert.h>

typedef struct {
    uint16 text[1024];
    unsigned length;
    int references;
} test_string;

static const struct {
    const char* name;
    uint32 offset;
} entries[] = {
    {"CFURLGetTypeID", 0x61d8},
    {"CFURLCanBeDecomposed", 0x83e0},
    {"CFURLGetString", 0x8448},
    {"CFURLGetBaseURL", 0x84a8},
    {"CFURLHasDirectoryPath", 0x89d4},
    {"CFURLCopyScheme", 0x85c0},
    {"CFURLCopyNetLocation", 0x863c},
    {"CFURLCopyPath", 0x8848},
    {"CFURLCopyStrictPath", 0x88b4},
    {"CFURLCopyResourceSpecifier", 0x8a6c},
    {"CFURLCopyLastPathComponent", 0xaba8},
    {"CFURLCreateCopyAppendingPathComponent", 0xae24},
    {"CFURLCopyHostName", 0x8be0},
    {"CFURLCopyUserName", 0x8de8},
    {"CFURLCopyPassword", 0x8ebc},
    {"CFURLCopyParameterString", 0x901c},
    {"CFURLCopyQueryString", 0x9124},
    {"CFURLCopyFragment", 0x91dc},
    {"CFURLGetPortNumber", 0x8cc0},
    {"__CFURLReservedPtr", 0x9258},
    {"__CFURLSetReservedPtr", 0x9260},
    {"CFURLCopyPathExtension", 0xad14},
    {"CFURLCreateFromFileSystemRepresentation", 0xa880},
    {"CFURLCreateFromFileSystemRepresentationRelativeToBase", 0xa928},
    {"CFURLCreateFromFSRef", 0xc58c},
    {"CFURLGetFSRef", 0xc614},
    {"_CFURLAlloc", 0x6ac8},
    {"_CFURLInitWithString", 0x7084},
    {"CFURLCreateWithString", 0x7ac0},
    {"CFURLCreateWithBytes", 0x78bc},
    {"CFURLCreateData", 0x79e4},
    {"CFURLCreateWithFileSystemPath", 0x9e10},
    {"CFURLCreateWithFileSystemPathRelativeToBase", 0x9f90},
    {"CFURLCreateCopyDeletingLastPathComponent", 0xb1e0},
    {"CFURLCreateCopyAppendingPathExtension", 0xb704},
    {"CFURLCreateCopyDeletingPathExtension", 0xb928},
    {"CFURLCopyAbsoluteURL", 0x7f6c},
    {"CFURLCopyFileSystemPath", 0xa4bc},
    {"CFURLGetFileSystemRepresentation", 0xa78c},
    {"CFGetAllocator", 0x53844},
    {"CFRetain", 0x533e4},
    {"CFRelease", 0x53488},
    {"CFStringCreateWithSubstring", 0x37ad0},
    {"CFStringGetLength", 0x38858},
    {"CFStringGetCharacterAtIndex", 0x3891c},
    {"__CFStringMakeConstantString", 0x38110},
    {"CFStringFindWithOptions", 0x3a8f8},
    {"CFStringCreateMutableCopy", 0x387ec},
    {"CFURLCreateStringByAddingPercentEscapes", 0x5cec},
    {"CFStringInsert", 0x3d148},
    {"CFURLCreateWithString", 0x7ac0},
    {"CFStringCreateWithFormat", 0x37a90},
    {"CFURLCreateWithFileSystemPathRelativeToBase", 0x9f90},
    {"CFURLCreateStringByReplacingPercentEscapes", 0x504c},
    {"_CFRuntimeCreateInstance", 0x53204},
    {"CFStringCreateCopy", 0x37d08},
    {"CFStringCreateExternalRepresentation", 0x3c698},
    {"CFStringDelete", 0x3d190},
    {"CFStringReplace", 0x3d1c8},
    {"CFStringCreateMutable", 0x38720},
    {"CFStringAppend", 0x3d2f8},
    {"CFStringAppendFormat", 0x3d8b4},
    {"CFStringGetBytes", 0x38ac8},
    {"CFStringGetSystemEncoding", 0x35600},
    {"CFStringCreateWithBytes", 0x37928},
    {"CFURLCreateWithFileSystemPath", 0x9e10},
    {"CFURLCopyAbsoluteURL", 0x7f6c},
    {"CFURLCopyFileSystemPath", 0xa4bc},
};

static test_string strings[128];
static Byte toc[0x72a8], vectors[NELEM(entries)][8], url[32], base[32], output_url[32], messages[0x300];
static uint32 ranges[18], base_ranges[18], converted_ranges[4];
static uint16 created_text[1024];
static unsigned created_length, created_style, created_directory;
static uint32 created_base;
static test_string *input, *component, *converted, *escaped, *decoded;
static int active, foreign, reference_ok, fail_at, allocation_fail, allocation_count;
static int escape_null, create_null, decode_null, retain_changes_global;
static int fs_supported, fs_info_error, fs_directory, fs_text_null, fs_make_result;
static Byte fs_reference[80];
static unsigned fs_support_calls, fs_info_calls, fs_make_calls;
static uint32 expected_leave, allocator_object;
static int runtime_fail, validate_result, convert_null;
static unsigned runtime_allocations, validations, reencodings, url_retains;
static uint32 reencode_arguments[7], representation_encoding;
static uint32 string_create_encoding;
static Byte external_data[32];
static test_string* merge_result;
static uint32 merge_arguments[5];
static unsigned merges, path_conversions;
static unsigned merge_null;
static unsigned encode_limit, encode_oversize;
static unsigned calls, parses, conversions, retains, releases, inserts, decodes;
static uint32 parse_flags, foreign_answer, last_arguments[8], last_count;

static uint32 word(const void* p, unsigned offset)
{
    uint32 value;
    memcpy(&value, (const Byte*)p + offset, 4);
    return CL(value);
}

static void store(void* p, unsigned offset, uint32 value)
{
    value = CL(value);
    memcpy((Byte*)p + offset, &value, 4);
}

static test_string* new_string(void)
{
    if (++allocation_count == allocation_fail)
        return NULL;
    for (unsigned i = 0; i < NELEM(strings); ++i)
        if (!strings[i].references) {
            strings[i].references = 1;
            strings[i].length = 0;
            return &strings[i];
        }
    assert(!"Test string pool exhausted");
    return NULL;
}

static test_string* text(const char* value)
{
    test_string* s = new_string();
    assert(s);
    while (*value)
        s->text[s->length++] = (Byte)*value++;
    return s;
}

static test_string* string(uint32 address)
{
    test_string* s = (test_string*)SYN68K_TO_US(address);
    assert(s >= strings && s < strings + NELEM(strings) && s->references);
    return s;
}

static void append(test_string* destination, const test_string* source)
{
    assert(destination->length + source->length <= NELEM(destination->text));
    memcpy(destination->text + destination->length, source->text, source->length * 2);
    destination->length += source->length;
}

static int equals(const test_string* s, const char* value)
{
    if (s->length != strlen(value))
        return 0;
    for (unsigned i = 0; i < s->length; ++i)
        if (s->text[i] != (Byte)value[i])
            return 0;
    return 1;
}

static void record(Byte* object, unsigned style, test_string* value, Byte* parent)
{
    memset(object, 0, 32);
    store(object, 8, (style << 16) | 0x2000 | 0x8000);
    store(object, 12, US_TO_SYN68K(value));
    store(object, 16, US_TO_SYN68K(parent));
    store(object, 20, US_TO_SYN68K(object == base ? base_ranges : ranges));
}

static void range(Byte* object, unsigned mask, unsigned location, unsigned length)
{
    uint32 flags = word(object, 8) | mask;
    store(object, 8, flags);
    unsigned index = 0;
    for (uint32 lower = flags & (mask - 1); lower; lower &= lower - 1)
        ++index;
    void* array = SYN68K_TO_US(word(object, 20));
    store(array, index * 8, location);
    store(array, index * 8 + 4, length);
}

int ROMlib_carbon_url_test_reference(const char* library, const char* name, ROMlib_carbon_reference* reference)
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
    assert(!"Unknown URL dependency");
    return 0;
}

int ROMlib_carbon_url_test_callback(void* procedure, const uint32* a, unsigned n, uint32* result)
{
    if (!active)
        return -1;
    ++calls;
    if (fail_at && calls >= (unsigned)fail_at)
        return 0;
    uint32 code = word(procedure, 0) - 0x10000000;
    unsigned i;
    for (i = 0; i < NELEM(entries) && entries[i].offset != code; ++i)
        ;
    if (i < 26 || (foreign && i < 39)) {
        assert(foreign);
        last_count = n;
        if (n)
            memcpy(last_arguments, a, n * 4);
        *result = foreign_answer;
        return 1;
    }
    if (i == NELEM(entries)) {
        assert(!((uintptr_t)procedure & 15) && word(procedure, 4) == US_TO_SYN68K(toc));
        if (code == 0x4af8) {
            assert(!n && result);
            ++fs_support_calls;
            *result = fs_supported;
            return 1;
        }
        if (code == 0x4d70) {
            assert(n == 6 && a[0] == US_TO_SYN68K(fs_reference) && a[1] == 2 && !a[3] && !a[4] && !a[5]);
            Byte* catalog = (Byte*)SYN68K_TO_US(a[2]);
            assert(!((uintptr_t)catalog & 255));
            uint16 flags = CW(fs_directory ? 16 : 0);
            memcpy(catalog, &flags, 2);
            ++fs_info_calls;
            *result = fs_info_error;
            return 1;
        }
        if (code == 0x1832c) {
            assert(n == 2 && a[0] == 0x12345678 && a[1] == US_TO_SYN68K(fs_reference));
            if (!fs_text_null)
                ++input->references;
            *result = fs_text_null ? 0 : US_TO_SYN68K(input);
            return 1;
        }
        if (code == 0xc178) {
            assert(n == 3 && a[0] == 0xaabbccdd && a[1] == US_TO_SYN68K(input) && a[2] == US_TO_SYN68K(fs_reference));
            ++fs_make_calls;
            if (fs_make_result)
                memset(fs_reference, 0x7a, sizeof fs_reference);
            *result = fs_make_result;
            return 1;
        }
        if (code == 0x9be8 || code == 0x7d68) {
            assert(n == (code == 0x9be8 ? 5 : 1) && result);
            if (n == 5)
                memcpy(merge_arguments, a, sizeof merge_arguments);
            ++merges;
            if (!merge_null)
                ++merge_result->references;
            *result = merge_null ? 0 : US_TO_SYN68K(merge_result);
            return 1;
        }
        if (code == 0xbf64 || code == 0x96ac) {
            assert(n == 2 && a[1] == 0xaabbccdd);
            ++path_conversions;
            ++decoded->references;
            *result = US_TO_SYN68K(decoded);
            return 1;
        }
        if (code == 0x6bac) {
            assert(n == 1 && result);
            ++validations;
            *result = validate_result;
            return 1;
        }
        if (code == 0x755c) {
            assert(n == 7 && result);
            memcpy(reencode_arguments, a, sizeof reencode_arguments);
            ++reencodings;
            if (!convert_null)
                ++converted->references;
            *result = convert_null ? 0 : US_TO_SYN68K(converted);
            return 1;
        }
        if (code == 0x9d84) {
            assert(n == 1 && a[0] == 0x12345678);
            ++converted->references;
            *result = US_TO_SYN68K(converted);
            return 1;
        }
        assert(n == 1 && !result);
        Byte* object = (Byte*)SYN68K_TO_US(a[0]);
        if (code == 0x61e0) {
            ++parses;
            store(object, 8, word(object, 8) | parse_flags);
        }
        else {
            assert(code == 0x98c8);
            ++conversions;
            store(object, 8, 0xf0000 | 0x2000 | 0x8000 | 1 | 32);
            store(object, 12, US_TO_SYN68K(converted));
            store(object, 20, US_TO_SYN68K(converted_ranges));
        }
        return 1;
    }
    const char* name = entries[i].name;
#define IS(s) (!strcmp(name, s))
    if (IS("CFGetAllocator")) {
        assert(n == 1);
        allocator_object = a[0];
        *result = 0xaabbccdd;
    }
    else if (IS("CFRetain")) {
        assert(n == 1);
        if (a[0] == US_TO_SYN68K(url) || a[0] == US_TO_SYN68K(base) || a[0] == US_TO_SYN68K(output_url)) {
            ++url_retains;
            if (result)
                *result = a[0];
            return 1;
        }
        test_string* s = string(a[0]);
        if (s->references > 0)
            ++s->references;
        ++retains;
        if (retain_changes_global)
            store(toc, 0x808, US_TO_SYN68K(decoded));
        if (result)
            *result = a[0];
    }
    else if (IS("CFRelease")) {
        assert(n == 1 && !result);
        test_string* s = string(a[0]);
        if (s->references > 0)
            --s->references;
        ++releases;
    }
    else if (IS("CFStringGetLength")) {
        assert(n == 1);
        *result = string(a[0])->length;
    }
    else if (IS("CFStringGetCharacterAtIndex")) {
        assert(n == 2);
        test_string* s = string(a[0]);
        assert(a[1] < s->length);
        *result = s->text[a[1]];
    }
    else if (IS("CFStringCreateWithSubstring")) {
        assert(n == 4 && (a[0] == 0xaabbccdd || a[0] == 0x12345678));
        test_string* s = string(a[1]);
        assert(a[2] <= s->length && a[3] <= s->length - a[2]);
        test_string* out = new_string();
        if (out) {
            out->length = a[3];
            memcpy(out->text, s->text + a[2], a[3] * 2);
        }
        *result = US_TO_SYN68K(out);
    }
    else if (IS("__CFStringMakeConstantString")) {
        assert(n == 1);
        unsigned offset = a[0] - US_TO_SYN68K(messages);
        const char* value = offset == 0x188   ? ":"
                            : offset == 0x18a ? "#"
                            : offset == 0x18c ? "%@:"
                            : offset == 0x190 ? "//"
                            : offset == 0x193 ? ";%@"
                            : offset == 0x197 ? "?%@"
                            : offset == 0x19b ? "#%@"
                            : offset == 0x1ed ? ".."
                            : offset == 0x1ca ? "%@%c"
                            : offset == 0x19f ? ""
                            : offset == 0x1cf ? "."
                            : offset == 0x1a0 ? "\\\\"
                            : offset == 0x1a6 ? ";?/"
                            : offset == 0x1aa ? "/"
                            : offset == 0x1d1 ? "%@%c%@%c"
                            : offset == 0x1da ? "%@%c%@"
                            : offset == 0x1e1 ? "%@%@%c"
                            : offset == 0x1e8 ? "%@%@"
                                              : NULL;
        assert(value);
        test_string* s = text(value);
        s->references = -1;
        *result = US_TO_SYN68K(s);
    }
    else if (IS("CFStringFindWithOptions")) {
        assert(n == 6 && (a[4] == 0 || a[4] == 4));
        test_string *s = string(a[0]), *pattern = string(a[1]);
        assert(a[2] <= s->length && a[3] <= s->length - a[2] && pattern->length);
        int found = -1;
        for (unsigned at = a[2]; pattern->length <= a[2] + a[3] - at; ++at) {
            if (!memcmp(s->text + at, pattern->text, pattern->length * 2)) {
                found = at;
                if (!a[4])
                    break;
            }
            if (at == a[2] + a[3])
                break;
        }
        if (found >= 0 && a[5]) {
            store(SYN68K_TO_US(a[5]), 0, found);
            store(SYN68K_TO_US(a[5]), 4, pattern->length);
        }
        *result = found >= 0 ? 255 : 0;
    }
    else if (IS("CFStringCreateMutableCopy")) {
        assert(n == 3 && a[0] == 0x12345678 && !a[1]);
        test_string* out = new_string();
        if (out)
            append(out, string(a[2]));
        *result = US_TO_SYN68K(out);
    }
    else if (IS("CFURLCreateStringByAddingPercentEscapes")) {
        assert(n == 5 && a[0] == 0x12345678 && a[1] == US_TO_SYN68K(component) && !a[2] && a[4] == 0x08000100);
        assert(equals(string(a[3]), ";?/"));
        if (!escape_null) {
            ++escaped->references;
            *result = US_TO_SYN68K(escaped);
        }
        else
            *result = 0;
    }
    else if (IS("CFStringInsert")) {
        assert(n == 3 && !result);
        test_string *s = string(a[0]), *addition = string(a[2]);
        assert(a[1] <= s->length && s->length + addition->length < NELEM(s->text));
        memmove(s->text + a[1] + addition->length, s->text + a[1], (s->length - a[1]) * 2);
        memcpy(s->text + a[1], addition->text, addition->length * 2);
        s->length += addition->length;
        ++inserts;
    }
    else if (IS("CFStringCreateWithFormat")) {
        assert(n >= 5 && n <= 7 && (a[0] == 0x12345678 || a[0] == 0xaabbccdd) && !a[1]);
        test_string *format = string(a[2]), *out = new_string();
        unsigned slot = 3;
        if (out)
            for (unsigned at = 0; at < format->length; ++at) {
                assert(format->text[at++] == '%' && slot < n);
                if (format->text[at] == '@')
                    append(out, string(a[slot++]));
                else {
                    assert(format->text[at] == 'c');
                    out->text[out->length++] = a[slot++];
                }
            }
        if (out)
            assert(slot == n);
        *result = US_TO_SYN68K(out);
    }
    else if (IS("CFURLCreateWithString") || IS("CFURLCreateWithFileSystemPathRelativeToBase") ||
             IS("CFURLCreateWithFileSystemPath")) {
        assert(a[0] == 0x12345678);
        test_string* s = string(a[1]);
        memcpy(created_text, s->text, s->length * 2);
        created_length = s->length;
        if (n == 3) {
            created_style = 15;
            created_base = a[2];
        }
        else {
            assert(n == 4 || n == 5);
            created_style = a[2];
            created_directory = a[3];
            created_base = n == 5 ? a[4] : 0;
        }
        *result = create_null ? 0 : US_TO_SYN68K(output_url);
    }
    else if (IS("CFURLCreateStringByReplacingPercentEscapes")) {
        assert(n == 3 && (a[0] == 0xaabbccdd || a[0] == 0x12345678) &&
               (a[2] == expected_leave || equals(string(a[2]), "")));
        ++decodes;
        if (decode_null)
            *result = 0;
        else {
            ++decoded->references;
            *result = US_TO_SYN68K(decoded);
        }
    }
    else if (IS("CFStringGetSystemEncoding")) {
        assert(!n);
        *result = 0;
    }
    else if (IS("CFStringCreateWithBytes")) {
        assert(n == 5 && a[0] == 0x12345678 && a[3] == string_create_encoding && !a[4]);
        test_string* out = new_string();
        if (out) {
            const Byte* bytes = (const Byte*)SYN68K_TO_US(a[1]);
            for (unsigned index = 0; index < a[2]; ++index)
                out->text[out->length++] = bytes[index];
        }
        *result = US_TO_SYN68K(out);
    }
    else if (IS("CFURLCopyAbsoluteURL")) {
        assert(n == 1 && (a[0] == US_TO_SYN68K(url) || a[0] == US_TO_SYN68K(base) || a[0] == US_TO_SYN68K(converted)));
        if (!create_null)
            ++converted->references;
        *result = create_null ? 0 : US_TO_SYN68K(converted);
    }
    else if (IS("CFURLCopyFileSystemPath")) {
        assert(n == 2 && a[0] == US_TO_SYN68K(url) && a[1] == 1);
        if (!fs_text_null)
            ++input->references;
        *result = fs_text_null ? 0 : US_TO_SYN68K(input);
    }
    else if (IS("_CFRuntimeCreateInstance")) {
        assert(n == 4 && (a[0] == 0x12345678 || a[0] == 0xaabbccdd) && a[1] == 42 && a[2] == 20 && !a[3]);
        ++runtime_allocations;
        memset(output_url, 0xa5, sizeof output_url);
        *result = runtime_fail ? 0 : US_TO_SYN68K(output_url);
    }
    else if (IS("CFStringCreateCopy")) {
        assert(n == 2 && a[0] == 0xaabbccdd);
        test_string* s = string(a[1]);
        test_string* out = new_string();
        if (out)
            append(out, s);
        *result = US_TO_SYN68K(out);
    }
    else if (IS("CFStringCreateExternalRepresentation")) {
        assert(n == 4 && a[0] == 0x12345678 && a[2] == representation_encoding && !a[3]);
        assert(a[1] == US_TO_SYN68K(representation_encoding == 0x08000100 ? input : converted));
        *result = US_TO_SYN68K(external_data);
    }
    else if (IS("CFStringDelete")) {
        assert(n == 3 && !result);
        test_string* s = string(a[0]);
        assert(a[1] <= s->length && a[2] <= s->length - a[1]);
        memmove(s->text + a[1], s->text + a[1] + a[2], (s->length - a[1] - a[2]) * 2);
        s->length -= a[2];
    }
    else if (IS("CFStringReplace")) {
        assert(n == 4 && !result);
        test_string* s = string(a[0]);
        test_string* replacement = string(a[3]);
        assert(a[1] <= s->length && a[2] <= s->length - a[1]);
        memmove(s->text + a[1] + replacement->length, s->text + a[1] + a[2], (s->length - a[1] - a[2]) * 2);
        memcpy(s->text + a[1], replacement->text, replacement->length * 2);
        s->length += replacement->length - a[2];
    }
    else if (IS("CFStringCreateMutable")) {
        assert(n == 2 && a[0] == 0xaabbccdd && !a[1]);
        *result = US_TO_SYN68K(new_string());
    }
    else if (IS("CFStringAppend")) {
        assert(n == 2 && !result);
        append(string(a[0]), string(a[1]));
    }
    else if (IS("CFStringAppendFormat")) {
        assert(n == 4 && !a[1] && !result);
        test_string* out = string(a[0]);
        test_string* format = string(a[2]);
        for (unsigned at = 0; at < format->length; ++at) {
            if (format->text[at] == '%') {
                assert(format->text[++at] == '@');
                if (a[3])
                    append(out, string(a[3]));
                else
                    append(out, decoded);
            }
            else
                out->text[out->length++] = format->text[at];
        }
    }
    else if (IS("CFStringGetBytes")) {
        assert(n == 9 && !a[1] && a[2] == string(a[0])->length && !a[3] && !a[4] && a[5] == 1);
        test_string* s = string(a[0]);
        unsigned count = s->length < a[7] ? s->length : a[7];
        if (count > encode_limit)
            count = encode_limit;
        Byte* out = (Byte*)SYN68K_TO_US(a[6]);
        for (unsigned at = 0; at < count; ++at)
            out[at] = s->text[at];
        store(SYN68K_TO_US(a[8]), 0, encode_oversize ? a[7] + 1 : count);
        *result = encode_oversize ? s->length : count;
    }
    else
        assert(!"Unhandled URL callback");
#undef IS
    return 1;
}

static void reset(void)
{
    memset(strings, 0, sizeof strings);
    memset(toc, 0, sizeof toc);
    memset(ranges, 0, sizeof ranges);
    calls = parses = conversions = retains = releases = inserts = decodes = 0;
    foreign = fail_at = allocation_fail = allocation_count = 0;
    escape_null = create_null = decode_null = retain_changes_global = 0;
    reference_ok = 1;
    allocator_object = 0;
    runtime_fail = convert_null = 0;
    validate_result = 1;
    runtime_allocations = validations = reencodings = url_retains = 0;
    string_create_encoding = 0;
    representation_encoding = 0x08000100;
    merges = path_conversions = encode_oversize = merge_null = 0;
    encode_limit = UINT32_MAX;
    memset(merge_arguments, 0, sizeof merge_arguments);
    memset(reencode_arguments, 0, sizeof reencode_arguments);
    fs_supported = 1;
    fs_info_error = fs_directory = fs_text_null = 0;
    fs_make_result = 255;
    fs_support_calls = fs_info_calls = fs_make_calls = expected_leave = 0;
    memset(fs_reference, 0xa5, sizeof fs_reference);
    input = text("/root/file");
    component = text("child");
    converted = text("file:///root");
    escaped = text("ENC");
    decoded = text("decoded");
    merge_result = text("/base/child");
    record(url, 15, input, NULL);
    record(base, 15, input, NULL);
    parse_flags = 0x2000 | 0x8000;
    store(toc, 0x3f8, US_TO_SYN68K(messages));
    store(toc, 0x808, US_TO_SYN68K(text("file")));
    store(toc, 0x80c, US_TO_SYN68K(text("localhost")));
    store(toc, 0x810, 42);
    store(toc, 0x226c, UINT32_MAX);
    store(converted_ranges, 0, 0);
    store(converted_ranges, 4, 4);
    store(converted_ranges, 8, 7);
    store(converted_ranges, 12, 5);
    foreign_answer = US_TO_SYN68K(decoded);
}

static void owned(void* p, const char* value)
{
    assert(p && equals((test_string*)p, value));
    test_string* s = p;
    if (s->references > 0)
        --s->references;
}

void ROMlib_carbon_url_tests(void)
{
    active = 1;
    for (unsigned i = 0; i < NELEM(entries); ++i) {
        store(vectors[i], 0, 0x10000000 + entries[i].offset);
        store(vectors[i], 4, US_TO_SYN68K(toc));
    }
    reset();
    assert(ROMlib_carbon_cf_url_type_id() == 42 && !calls);
    assert(ROMlib_carbon_cf_url_string(url) == input && !calls);
    assert(!ROMlib_carbon_cf_url_base(url) && ROMlib_carbon_cf_url_decomposable(url));
    store(url, 8, 0xf0000);
    assert(ROMlib_carbon_cf_url_decomposable(url) && parses == 1);
    assert(ROMlib_carbon_cf_url_decomposable(url) && parses == 1);
    for (unsigned style = 0; style < 3; ++style)
        for (unsigned cached = 0; cached < 2; ++cached)
            for (unsigned parent = 0; parent < 2; ++parent) {
                reset();
                record(url, style, input, parent ? base : NULL);
                if (cached)
                    store(url, 8, word(url, 8) | 0x100000);
                assert(ROMlib_carbon_cf_url_string(url) == (cached && parent ? input : converted));
                assert(conversions == !(cached && parent));
            }
    reset();
    record(url, 0, input, NULL);
    retain_changes_global = 1;
    assert(ROMlib_carbon_cf_url_scheme(url) == decoded && retains == 1);
    owned(ROMlib_carbon_cf_url_net_location(url), "localhost");
    reset();
    store(url, 16, US_TO_SYN68K(base));
    range(base, 1, 0, 5);
    owned(ROMlib_carbon_cf_url_scheme(url), "/root");
    assert(ROMlib_carbon_cf_url_base(url) == base);
    store(base, 8, word(base, 8) | 0x1000);
    assert(ROMlib_carbon_cf_url_directory(url));
    range(url, 32, 0, input->length);
    assert(!ROMlib_carbon_cf_url_directory(url));
    owned(ROMlib_carbon_cf_url_path(url), "/root/file");
    Byte absolute[3] = {0xa5, 0xa5, 0xa5};
    owned(ROMlib_carbon_cf_url_strict_path(url, absolute + 1), "root/file");
    assert(absolute[0] == 0xa5 && absolute[1] == 1 && absolute[2] == 0xa5);
    reset();
    input = text("relative");
    record(url, 15, input, NULL);
    range(url, 32, 0, input->length);
    owned(ROMlib_carbon_cf_url_strict_path(url, absolute + 1), "relative");
    assert(!absolute[1]);
    reset();
    range(url, 32, 0, 0);
    assert(!ROMlib_carbon_cf_url_strict_path(url, absolute + 1) && !absolute[1]);
    reset();
    input = text("/");
    record(url, 15, input, NULL);
    range(url, 32, 0, 1);
    owned(ROMlib_carbon_cf_url_strict_path(url, NULL), "");
    reset();
    input = text("u:p@host:23");
    record(url, 15, input, NULL);
    range(url, 2, 0, 1);
    range(url, 4, 2, 1);
    range(url, 8, 4, 4);
    range(url, 16, 9, 2);
    owned(ROMlib_carbon_cf_url_net_location(url), "u:p@host:23");
    reset();
    input = text("/p;params?query#frag");
    record(url, 15, input, NULL);
    range(url, 32, 0, 2);
    range(url, 64, 3, 6);
    range(url, 128, 10, 5);
    range(url, 256, 16, 4);
    owned(ROMlib_carbon_cf_url_resource(url), ";params?query#frag");
    reset();
    input = text("mailto:a@b");
    record(url, 15, input, NULL);
    store(url, 8, word(url, 8) & ~0x8000);
    range(url, 1, 0, 6);
    owned(ROMlib_carbon_cf_url_resource(url), "a@b");
    for (unsigned style = 0; style < 3; ++style) {
        reset();
        const char* path = style == 0 ? "/a/b/" : style == 1 ? "Disk:a:b:" : "C:\\a\\b\\";
        input = text(path);
        record(url, style, input, NULL);
        owned(ROMlib_carbon_cf_url_last_component(url), style == 2 ? "C:\\a\\b" : "b");
        reset();
        input = text(style == 0 ? "/" : style == 1 ? ":" : "\\");
        record(url, style, input, NULL);
        owned(ROMlib_carbon_cf_url_last_component(url), "/");
    }
    reset();
    range(url, 32, 0, input->length);
    assert(ROMlib_carbon_cf_url_last_component(url) == decoded && decodes == 1);
    reset();
    range(url, 32, 0, input->length);
    store(url, 8, word(url, 8) | 0x100000);
    owned(ROMlib_carbon_cf_url_last_component(url), "file");
    assert(!decodes);
    reset();
    owned(ROMlib_carbon_cf_url_last_component(url), "");
    for (unsigned style = 0; style < 3; ++style)
        for (unsigned trailing = 0; trailing < 2; ++trailing)
            for (unsigned directory = 0; directory < 2; ++directory) {
                reset();
                input = text(trailing ? "base/" : "base");
                unsigned separator = style == 0 ? '/' : style == 1 ? ':' : 0x5c;
                if (trailing)
                    input->text[4] = separator;
                record(url, style, input, base);
                assert(ROMlib_carbon_cf_url_append(0x12345678, url, component, directory | 0x80000000) == output_url);
                assert(created_style == style && created_directory == (directory | 0x80000000) &&
                       created_base == US_TO_SYN68K(base));
                test_string expected = {0};
                append(&expected, input);
                if (!trailing)
                    expected.text[expected.length++] = separator;
                append(&expected, component);
                if (directory)
                    expected.text[expected.length++] = separator;
                assert(created_length == expected.length && !memcmp(created_text, expected.text, created_length * 2));
                assert(releases == 1);
            }
    for (unsigned directory = 0; directory < 2; ++directory) {
        reset();
        range(url, 32, 0, input->length);
        store(url, 16, US_TO_SYN68K(base));
        assert(ROMlib_carbon_cf_url_append(0x12345678, url, component, directory) == output_url);
        test_string expected = {0};
        append(&expected, input);
        expected.text[expected.length++] = '/';
        append(&expected, escaped);
        if (directory)
            expected.text[expected.length++] = '/';
        assert(created_style == 15 && created_base == US_TO_SYN68K(base) && created_length == expected.length);
        assert(!memcmp(created_text, expected.text, created_length * 2) && releases == 2 && escaped->references == 1);
    }
    reset();
    record(url, 1, input, NULL);
    component = text("x:y");
    assert(ROMlib_carbon_cf_url_append(0x12345678, url, component, 1) == output_url && conversions == 1);
    assert(created_length == strlen("file:///root/ENC/") && created_style == 15);
    reset();
    assert(!ROMlib_carbon_cf_url_append(0x12345678, url, component, 1) && !releases);
    reset();
    range(url, 32, 0, input->length);
    escape_null = 1;
    assert(!ROMlib_carbon_cf_url_append(0x12345678, url, component, 1) && releases == 1);
    reset();
    range(url, 32, 0, input->length);
    create_null = 1;
    assert(!ROMlib_carbon_cf_url_append(0x12345678, url, component, 1) && releases == 2);
    reset();
    record(url, 0, input, NULL);
    create_null = 1;
    assert(!ROMlib_carbon_cf_url_append(0x12345678, url, component, 0) && releases == 1);
    reset();
    range(url, 32, 0, input->length);
    allocation_fail = allocation_count + 1;
    assert(!ROMlib_carbon_cf_url_append(0x12345678, url, component, 1) && !releases);
    /* Callback failures retain the backend error rather than publish success. */
    reset();
    range(url, 32, 0, input->length);
    ROMlib_carbon_cf_url_append(0x12345678, url, component, 1);
    unsigned steps = calls;
    for (unsigned at = 1; at <= steps; ++at) {
        reset();
        range(url, 32, 0, input->length);
        fail_at = at;
        assert(!ROMlib_carbon_cf_url_append(0x12345678, url, component, 1) && calls == at);
    }
    reset();
    foreign = 1;
    assert(ROMlib_carbon_cf_url_type_id() == foreign_answer && !last_count);
    assert(ROMlib_carbon_cf_url_decomposable(url) == (foreign_answer & 255));
    assert(ROMlib_carbon_cf_url_directory(url) == (foreign_answer & 255));
    void* (*getters[])(void*) = {ROMlib_carbon_cf_url_string,        ROMlib_carbon_cf_url_base,
                                 ROMlib_carbon_cf_url_scheme,        ROMlib_carbon_cf_url_net_location,
                                 ROMlib_carbon_cf_url_path,          ROMlib_carbon_cf_url_resource,
                                 ROMlib_carbon_cf_url_last_component};
    for (unsigned i = 0; i < NELEM(getters); ++i)
        assert(getters[i](url) == decoded);
    assert(ROMlib_carbon_cf_url_strict_path(url, NULL) == decoded && last_count == 2 && !last_arguments[1]);
    assert(ROMlib_carbon_cf_url_append(0xffffffff, url, component, 0x81234567) == decoded);
    assert(last_count == 4 && last_arguments[0] == 0xffffffff && last_arguments[1] == US_TO_SYN68K(url) &&
           last_arguments[2] == US_TO_SYN68K(component) && last_arguments[3] == 0x81234567);

    void* (*authorities[])(void*) = {ROMlib_carbon_cf_url_host, ROMlib_carbon_cf_url_user,
                                     ROMlib_carbon_cf_url_password};
    const unsigned masks[] = {8, 2, 4};
    for (unsigned index = 0; index < NELEM(authorities); ++index) {
        reset();
        store(url, 16, US_TO_SYN68K(base));
        range(base, masks[index], 0, input->length);
        assert(authorities[index](url) == decoded && decodes == 1 && releases == 1);
        assert(allocator_object == US_TO_SYN68K(base));
        for (unsigned blocker = 1; blocker <= 16; blocker <<= 1) {
            reset();
            store(url, 16, US_TO_SYN68K(base));
            range(base, masks[index], 0, input->length);
            store(url, 8, word(url, 8) | blocker);
            /* A present selected range is not a missing component. */
            if (blocker != masks[index])
                assert(!authorities[index](url) && !decodes);
        }
        reset();
        record(url, 0, input, NULL);
        if (!index)
            owned(authorities[index](url), "localhost");
        else
            assert(!authorities[index](url));
    }
    void* (*tails[])(void*, const void*) = {ROMlib_carbon_cf_url_parameters, ROMlib_carbon_cf_url_query,
                                            ROMlib_carbon_cf_url_fragment};
    for (unsigned index = 0; index < NELEM(tails); ++index) {
        unsigned mask = 64u << index;
        reset();
        range(url, mask, 0, input->length);
        expected_leave = US_TO_SYN68K(component);
        assert(tails[index](url, component) == decoded && releases == 1 && decodes == 1);
        reset();
        store(url, 16, US_TO_SYN68K(base));
        range(base, mask, 0, input->length);
        if (mask == 256)
            assert(!tails[index](url, NULL));
        else {
            assert(tails[index](url, NULL) == decoded && releases == 1);
            assert(allocator_object == US_TO_SYN68K(url));
            for (unsigned blocker = 1; blocker < mask; blocker <<= 1) {
                reset();
                store(url, 16, US_TO_SYN68K(base));
                range(base, mask, 0, input->length);
                store(url, 8, word(url, 8) | blocker);
                assert(!tails[index](url, NULL));
            }
        }
        reset();
        record(url, 0, input, NULL);
        assert(!tails[index](url, NULL));
        reset();
        range(url, mask, 0, input->length);
        decode_null = 1;
        assert(!tails[index](url, NULL) && releases == 1);
    }

    const struct {
        const char* text;
        uint32 value;
    } ports[] = {
        {"", UINT32_MAX},
        {"0", 0},
        {"65536", 65536},
        {"2147483647", 0x7fffffff},
        {"2147483648", 0x7fffffff},
        {"999999999999999999999999999999", 0x7fffffff},
        {"-2147483648", 0x80000000},
        {"-2147483649", 0x80000000},
        {"-99999999999999999999999999", 0x80000000},
        {"+  23", 23},
        {"  - 23", (uint32)-23},
        {"23 ", UINT32_MAX},
        {"23x", UINT32_MAX},
        {"+", UINT32_MAX},
        {"--2", UINT32_MAX},
    };

    for (unsigned index = 0; index < NELEM(ports); ++index) {
        reset();
        input = text(ports[index].text);
        record(url, 15, input, NULL);
        range(url, 16, 0, input->length);
        assert(ROMlib_carbon_cf_url_port(url) == ports[index].value && releases == 1);
    }
    const uint16 spaces[] = {0, 0x20, 0x7f, 0xa0, 0x2000, 0x200b, 0x3000};
    for (unsigned index = 0; index < NELEM(spaces); ++index) {
        reset();
        input = text("x+ 7");
        input->text[0] = input->text[2] = spaces[index];
        record(url, 15, input, NULL);
        range(url, 16, 0, input->length);
        assert(ROMlib_carbon_cf_url_port(url) == 7);
    }
    reset();
    store(url, 16, US_TO_SYN68K(base));
    input = text("80");
    record(base, 15, input, NULL);
    range(base, 16, 0, 2);
    assert(ROMlib_carbon_cf_url_port(url) == 80);
    range(url, 1, 0, 1);
    assert(ROMlib_carbon_cf_url_port(url) == UINT32_MAX);
    reset();
    ROMlib_carbon_cf_url_set_reserved(url, fs_reference);
    assert(ROMlib_carbon_cf_url_reserved(url) == fs_reference);
    assert(word(url, 24) == US_TO_SYN68K(fs_reference));
    ROMlib_carbon_cf_url_set_reserved(url, NULL);
    assert(!ROMlib_carbon_cf_url_reserved(url));
    const char* filenames[] = {"/a/file.txt", "/a/file.", "/a/file", "/a/.hidden", "/a/a.b.c", "/"};
    const char* extensions[] = {"txt", "", NULL, "hidden", "c", NULL};
    for (unsigned index = 0; index < NELEM(filenames); ++index) {
        reset();
        input = text(filenames[index]);
        record(url, 0, input, NULL);
        void* extension = ROMlib_carbon_cf_url_extension(url);
        if (extensions[index])
            owned(extension, extensions[index]);
        else
            assert(!extension);
    }
    const Byte path_bytes[] = {'D', ':', 'a', 0, 0x8e};
    for (unsigned relative = 0; relative < 2; ++relative) {
        reset();
        void* answer =
            relative ? ROMlib_carbon_cf_url_from_bytes_base(0x12345678, path_bytes, sizeof path_bytes, 0x80000001, base)
                     : ROMlib_carbon_cf_url_from_bytes(0x12345678, path_bytes, sizeof path_bytes, 0x80000001);
        assert(answer == output_url && created_style == 1 && created_directory == 0x80000001);
        assert(created_length == sizeof path_bytes && created_text[3] == 0 && created_text[4] == 0x8e);
        assert(created_base == (relative ? US_TO_SYN68K(base) : 0) && releases == 1);
        reset();
        allocation_fail = allocation_count + 1;
        assert(!ROMlib_carbon_cf_url_from_bytes(0x12345678, path_bytes, sizeof path_bytes, 0) && !releases);
    }
    for (unsigned folder = 0; folder < 2; ++folder) {
        reset();
        fs_directory = folder;
        assert(ROMlib_carbon_cf_url_from_fsref(0x12345678, fs_reference) == output_url);
        assert(fs_info_calls == 1 && fs_support_calls == 1 && created_directory == folder * 16);
        assert(created_style == 1 && releases == 1 && input->references == 1);
    }
    reset();
    fs_supported = 0;
    assert(!ROMlib_carbon_cf_url_from_fsref(0x12345678, fs_reference) && !fs_info_calls);
    assert(!ROMlib_carbon_cf_url_get_fsref(url, fs_reference) && !fs_make_calls && !releases);
    reset();
    create_null = 1;
    assert(!ROMlib_carbon_cf_url_from_fsref(0x12345678, fs_reference) && releases == 1 && input->references == 1);
    reset();
    create_null = 1;
    assert(!ROMlib_carbon_cf_url_from_bytes(0x12345678, path_bytes, sizeof path_bytes, 1) && releases == 1);
    reset();
    fs_info_error = (uint32)-43;
    assert(!ROMlib_carbon_cf_url_from_fsref(0x12345678, fs_reference) && !releases);
    reset();
    fs_text_null = 1;
    assert(!ROMlib_carbon_cf_url_from_fsref(0x12345678, fs_reference) && !releases);
    reset();
    assert(ROMlib_carbon_cf_url_get_fsref(url, fs_reference) == 255);
    assert(fs_support_calls == 2 && fs_make_calls == 1 && releases == 2 && fs_reference[0] == 0x7a);
    assert(input->references == 1 && converted->references == 1);
    reset();
    create_null = 1;
    assert(!ROMlib_carbon_cf_url_get_fsref(url, fs_reference) && !releases && !fs_make_calls);
    reset();
    fs_text_null = 1;
    assert(!ROMlib_carbon_cf_url_get_fsref(url, fs_reference) && releases == 1 && !fs_make_calls);
    reset();
    fs_make_result = 0;
    assert(!ROMlib_carbon_cf_url_get_fsref(url, fs_reference) && releases == 2 && fs_make_calls == 1);
    reset();
    foreign = 1;
    for (unsigned i = 0; i < NELEM(authorities); ++i)
        assert(authorities[i](url) == decoded && last_count == 1);
    for (unsigned i = 0; i < NELEM(tails); ++i)
        assert(tails[i](url, component) == decoded && last_count == 2 && last_arguments[1] == US_TO_SYN68K(component));
    assert(ROMlib_carbon_cf_url_port(url) == foreign_answer);
    assert(ROMlib_carbon_cf_url_extension(url) == decoded);
    assert(ROMlib_carbon_cf_url_reserved(url) == decoded);
    ROMlib_carbon_cf_url_set_reserved(url, fs_reference);
    assert(last_count == 2 && last_arguments[1] == US_TO_SYN68K(fs_reference));
    assert(ROMlib_carbon_cf_url_from_bytes_base(0x12345678, path_bytes, sizeof path_bytes, 0x81234567, base) ==
           decoded);
    assert(last_count == 5 && last_arguments[0] == 0x12345678 && last_arguments[1] == US_TO_SYN68K(path_bytes) &&
           last_arguments[2] == sizeof path_bytes && last_arguments[3] == 0x81234567 &&
           last_arguments[4] == US_TO_SYN68K(base));
    assert(ROMlib_carbon_cf_url_from_bytes(0x12345678, path_bytes, sizeof path_bytes, 0) == decoded && last_count == 4);
    assert(ROMlib_carbon_cf_url_from_fsref(0xffffffff, fs_reference) == decoded && last_count == 2 &&
           last_arguments[0] == 0xffffffff);
    assert(ROMlib_carbon_cf_url_get_fsref(url, fs_reference) == (foreign_answer & 255) && last_count == 2);

    reset();
    assert(ROMlib_carbon_cf_url_alloc(0x12345678) == output_url && runtime_allocations == 1);
    for (unsigned i = 0; i < sizeof output_url; ++i)
        assert(output_url[i] == (i >= 8 && i < 28 ? 0 : 0xa5));
    reset();
    runtime_fail = 1;
    assert(!ROMlib_carbon_cf_url_alloc(0x12345678));
    const char* initial_strings[] = {"http://host/a#f", "path#fragment:colon", "a:b#c#d", "relative", "a#b:c#d"};
    const unsigned absolute_strings[] = {1, 0, 1, 0, 1};
    for (unsigned i = 0; i < NELEM(initial_strings); ++i) {
        reset();
        input = text(initial_strings[i]);
        memset(output_url, 0, sizeof output_url);
        store(output_url, 8, 0x123);
        ROMlib_carbon_cf_url_initialize(output_url, input, base);
        assert(word(output_url, 8) == (0xf0123 | (absolute_strings[i] ? 0x4000 : 0)));
        assert(equals(string(word(output_url, 12)), initial_strings[i]) && input->references == 1);
        assert(word(output_url, 16) == (absolute_strings[i] ? 0 : US_TO_SYN68K(converted)));
    }
    reset();
    assert(ROMlib_carbon_cf_url_create_string(0x12345678, input, base) == output_url && !validations);
    assert(word(output_url, 8) == 0xf0000 && word(output_url, 16) == US_TO_SYN68K(converted));
    assert(!ROMlib_carbon_cf_url_create_string(0x12345678, NULL, base));
    reset();
    input = text("");
    assert(!ROMlib_carbon_cf_url_create_string(0x12345678, input, base) && !validations);
    reset();
    input = text("%ZZ");
    assert(!ROMlib_carbon_cf_url_create_string(0x12345678, input, base) && !runtime_allocations);
    reset();
    runtime_fail = 1;
    assert(!ROMlib_carbon_cf_url_create_string(0x12345678, input, base));
    for (unsigned utf8 = 0; utf8 < 2; ++utf8) {
        reset();
        uint32 encoding = utf8 ? 0x08000100 : 0;
        string_create_encoding = encoding;
        assert(ROMlib_carbon_cf_url_create_bytes(0x12345678, (const Byte*)"abc", 3, encoding, base) == output_url);
        assert(reencodings == !utf8 && releases == (utf8 ? 1 : 2) && !validations);
        if (!utf8)
            assert(reencode_arguments[1] == encoding && reencode_arguments[2] == 0x08000100 && !reencode_arguments[3] &&
                   !reencode_arguments[4] && !reencode_arguments[5] && !reencode_arguments[6]);
    }
    reset();
    assert(!ROMlib_carbon_cf_url_create_bytes(0x12345678, path_bytes, 0, 0, base) && releases == 1 && !validations);
    reset();
    convert_null = 1;
    assert(!ROMlib_carbon_cf_url_create_bytes(0x12345678, path_bytes, sizeof path_bytes, 0, base) && releases == 1);
    for (unsigned utf8 = 0; utf8 < 2; ++utf8)
        for (unsigned spaces = 0; spaces < 2; ++spaces) {
            reset();
            representation_encoding = utf8 ? 0x08000100 : 0;
            assert(ROMlib_carbon_cf_url_create_data(0x12345678, url, representation_encoding, spaces) == external_data);
            assert(releases == 1 && reencodings == !utf8);
            if (!utf8)
                assert(reencode_arguments[1] == 0x08000100 && !reencode_arguments[2] && reencode_arguments[3] == 1 &&
                       reencode_arguments[5] == (spaces ? US_TO_SYN68K(toc) + 0x2264 : 0) &&
                       reencode_arguments[6] == (spaces ? 4 : 0));
        }

    const struct {
        unsigned style, directory;
        const char* path;
        const char* expected;
        unsigned folder, absolute, safe;
    } paths[] = {
        {0, 0, "/a/", "/a", 0, 1, 1},
        {0, 1, "/a", "/a/", 1, 1, 1},
        {0, 0, "/", "/", 1, 1, 1},
        {0, 0, "/a//", "/a//", 1, 1, 1},
        {0, 0, "a b", "a b", 0, 0, 0},
        {0, 0, "a%20", "a%20", 0, 0, 0},
        {1, 0, "Disk:a:", "Disk:a", 0, 1, 0},
        {1, 1, ":a", ":a:", 1, 0, 0},
        {2, 0, "D:apostrophe", "D:apostrophe", 0, 0, 0},
        {2, 0, "D:'path", "D:'path", 0, 1, 0},
        {3, 0, "other", "other", 0, 1, 0},
    };

    for (unsigned i = 0; i < NELEM(paths); ++i) {
        reset();
        input = text(paths[i].path);
        assert(ROMlib_carbon_cf_url_create_file_base(0x12345678, input, paths[i].style, paths[i].directory, base) ==
               output_url);
        assert(equals(string(word(output_url, 12)), paths[i].expected));
        assert(word(output_url, 8) ==
               (paths[i].style << 16 | (paths[i].folder ? 0x1000 : 0) | (paths[i].safe ? 0x100000 : 0)));
        assert(word(output_url, 16) == (paths[i].absolute ? 0 : US_TO_SYN68K(converted)));
    }
    reset();
    input = text("");
    assert(!ROMlib_carbon_cf_url_create_file_base(0x12345678, input, 0, 0, base));
    reset();
    runtime_fail = 1;
    assert(!ROMlib_carbon_cf_url_create_file_base(0x12345678, input, 0, 1, base) && releases == 1);
    reset();
    assert(ROMlib_carbon_cf_url_create_file(0x12345678, input, 0, 0) == output_url && !word(output_url, 16));
    reset();
    input = text("relative");
    assert(ROMlib_carbon_cf_url_create_file(0x12345678, input, 0, 0) == output_url &&
           word(output_url, 16) == US_TO_SYN68K(converted));
    assert(converted->references == 2 && releases == 1);

    const struct {
        const char* path;
        const char* expected;
    } parents[] = {
        {"/a/b", "/a/"}, {"file", "./"},     {".", "../"},        {"..", "../../"},
        {"/", "/../"},   {"a/.", "a/./../"}, {"a/./", "a/./../"}, {"a/../", "a/../../"},
    };

    for (unsigned i = 0; i < NELEM(parents); ++i) {
        reset();
        input = text(parents[i].path);
        record(url, 0, input, NULL);
        assert(ROMlib_carbon_cf_url_delete_last(0x12345678, url) == output_url);
        assert(equals(string(word(output_url, 12)), parents[i].expected) && (word(output_url, 8) & 0x1000));
    }
    reset();
    input = text("/a/file.tar.gz/");
    record(url, 0, input, NULL);
    store(url, 8, word(url, 8) | 0x1000);
    assert(ROMlib_carbon_cf_url_delete_extension(0x12345678, url) == output_url);
    assert(equals(string(word(output_url, 12)), "/a/file.tar/") && (word(output_url, 8) & 0x1000));
    reset();
    record(url, 0, input, NULL);
    assert(ROMlib_carbon_cf_url_delete_extension(0x12345678, url) == url && url_retains == 1);
    reset();
    record(url, 0, input, NULL);
    component = text("txt");
    assert(ROMlib_carbon_cf_url_append_extension(0x12345678, url, component) == output_url);
    assert(equals(string(word(output_url, 12)), "/root/file.txt"));
    reset();
    range(url, 32, 0, input->length);
    assert(ROMlib_carbon_cf_url_append_extension(0x12345678, url, component) == output_url);
    assert(equals(string(word(output_url, 12)), "/root/file.ENC") && reencodings == 0);
    reset();
    foreign = 1;
    assert(ROMlib_carbon_cf_url_alloc(0xffffffff) == decoded && last_count == 1 && last_arguments[0] == 0xffffffff);
    ROMlib_carbon_cf_url_initialize(url, input, base);
    assert(last_count == 3 && last_arguments[0] == US_TO_SYN68K(url) && last_arguments[1] == US_TO_SYN68K(input) &&
           last_arguments[2] == US_TO_SYN68K(base));
    assert(ROMlib_carbon_cf_url_create_string(0x12345678, input, base) == decoded && last_count == 3);
    assert(ROMlib_carbon_cf_url_create_bytes(0x12345678, path_bytes, sizeof path_bytes, 0x08000100, base) == decoded &&
           last_count == 5 && last_arguments[3] == 0x08000100);
    assert(ROMlib_carbon_cf_url_create_data(0x12345678, url, 0x08000100, 0x81234567) == decoded && last_count == 4 &&
           last_arguments[3] == 0x81234567);
    assert(ROMlib_carbon_cf_url_create_file(0x12345678, input, 2, 0x81234567) == decoded && last_count == 4 &&
           last_arguments[2] == 2);
    assert(ROMlib_carbon_cf_url_create_file_base(0x12345678, input, 2, 1, base) == decoded && last_count == 5 &&
           last_arguments[4] == US_TO_SYN68K(base));
    assert(ROMlib_carbon_cf_url_delete_last(0x12345678, url) == decoded && last_count == 2);
    assert(ROMlib_carbon_cf_url_delete_extension(0x12345678, url) == decoded && last_count == 2);
    assert(ROMlib_carbon_cf_url_append_extension(0x12345678, url, component) == decoded && last_count == 3 &&
           last_arguments[2] == US_TO_SYN68K(component));

    /* Exhaust the BMP at the interior/final boundary without allocating objects.
       The original allowed ASCII set plus '#' is independently specified here. */
    reset();
    runtime_fail = 1;
    for (unsigned final = 0; final < 2; ++final)
        for (uint32 character = 0; character <= 0xffff; ++character) {
            input->length = final ? 2 : 3;
            input->text[0] = 'a';
            input->text[1] = character;
            input->text[2] = 'b';
            unsigned ascii = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
                             (character >= '0' && character <= '9') ||
                             (character && character < 128 && strchr("!#$&'()*+,-./:;=?@_~", character));
            unsigned expected = character != '%' && (final || ascii);
            unsigned before = runtime_allocations;
            assert(!ROMlib_carbon_cf_url_create_string(0x12345678, input, NULL));
            assert(runtime_allocations - before == expected && !validations);
        }

    const struct {
        const char* text;
        unsigned valid;
    } validation_cases[] = {
        {"a#b#", 1}, {"a#b#c", 0}, {"%", 0},  {"%0", 0},  {"%00", 1}, {"%aF", 1}, {"%ag", 0}, {"%G0", 0},
        {"a ", 1},   {"a b", 0},   {"a[", 1}, {"a[b", 0}, {"#", 1},   {"##", 1},  {"###", 0},
    };

    for (unsigned i = 0; i < NELEM(validation_cases); ++i) {
        reset();
        runtime_fail = 1;
        input = text(validation_cases[i].text);
        assert(!ROMlib_carbon_cf_url_create_string(0x12345678, input, NULL));
        assert(runtime_allocations == validation_cases[i].valid);
    }
    reset();
    assert(ROMlib_carbon_cf_url_absolute(url) == url && url_retains == 1);
    reset();
    record(url, 0, input, base);
    record(base, 0, text("/base/"), NULL);
    store(base, 8, word(base, 8) | 0x1000);
    assert(ROMlib_carbon_cf_url_absolute(url) == output_url && merges == 1);
    assert(equals(string(word(output_url, 12)), "/base/child") && merge_arguments[2] == 1 && !merge_arguments[3]);
    assert(merge_result->references == 1 && !word(output_url, 16));
    reset();
    record(base, 0, text("/base/"), NULL);
    store(url, 16, US_TO_SYN68K(base));
    store(url, 8, word(url, 8) | 0x100000);
    range(url, 32, 0, input->length);
    assert(ROMlib_carbon_cf_url_absolute(url) == output_url && merges == 1);
    assert(word(output_url, 8) == 0x100000 && equals(string(word(output_url, 12)), "/base/child"));
    reset();
    input = text("child");
    record(url, 15, input, base);
    range(url, 32, 0, 5);
    record(base, 15, text("https://host/base/"), NULL);
    range(base, 1, 0, 5);
    range(base, 8, 8, 4);
    range(base, 32, 12, 6);
    assert(ROMlib_carbon_cf_url_absolute(url) == output_url && merges == 1);
    assert(equals(string(word(output_url, 12)), "https://host/base/child") && word(output_url, 8) == 0xf4000);
    reset();
    input = text("child");
    record(url, 15, input, base);
    range(url, 32, 0, 5);
    record(base, 15, text("https://host/base/"), NULL);
    range(base, 1, 0, 5);
    range(base, 8, 8, 4);
    merge_null = 1;
    assert(!ROMlib_carbon_cf_url_absolute(url) && merges == 1);
    assert(!ROMlib_carbon_cf_url_absolute(NULL));
    assert(!ROMlib_carbon_cf_url_filesystem_path(NULL, 0));
    reset();
    input = text("//other/a");
    record(url, 15, input, base);
    range(url, 8, 2, 5);
    range(url, 32, 7, 2);
    record(base, 15, text("https://host/base/"), NULL);
    range(base, 1, 0, 5);
    assert(ROMlib_carbon_cf_url_absolute(url) == output_url && !merges);
    assert(equals(string(word(output_url, 12)), "https://other/a"));
    for (unsigned style = 0; style < 3; ++style) {
        reset();
        record(url, style, input, NULL);
        owned(ROMlib_carbon_cf_url_filesystem_path(url, style), "/root/file");
        assert(!path_conversions && !decodes);
    }
    reset();
    input = text("/a/");
    record(url, 0, input, NULL);
    store(url, 8, word(url, 8) | 0x1000);
    owned(ROMlib_carbon_cf_url_filesystem_path(url, 0), "/a");
    assert(releases == 1);
    reset();
    input = text("/");
    record(url, 0, input, NULL);
    store(url, 8, word(url, 8) | 0x1000);
    owned(ROMlib_carbon_cf_url_filesystem_path(url, 0), "/");
    reset();
    range(url, 32, 0, input->length);
    decoded = text("/decoded/");
    owned(ROMlib_carbon_cf_url_filesystem_path(url, 0), "/decoded");
    assert(decodes == 1 && releases == 2 && decoded->references == 1);
    for (unsigned style = 1; style < 3; ++style) {
        reset();
        range(url, 32, 0, input->length);
        owned(ROMlib_carbon_cf_url_filesystem_path(url, style), "decoded");
        assert(path_conversions == 1 && releases == 1 && !decodes);
    }
    reset();
    range(url, 32, 0, input->length);
    store(url, 8, word(url, 8) | 0x100000);
    owned(ROMlib_carbon_cf_url_filesystem_path(url, 0), "/root/file");
    assert(!decodes);
    reset();
    range(url, 32, 0, input->length);
    assert(!ROMlib_carbon_cf_url_filesystem_path(url, 3) && releases == 1 && !path_conversions);
    for (unsigned capacity = 1; capacity <= 12; ++capacity) {
        reset();
        record(url, 1, input, NULL);
        Byte buffer[16];
        memset(buffer, 0xa5, sizeof buffer);
        uint32 answer = ROMlib_carbon_cf_url_filesystem_bytes(url, 0, buffer + 1, capacity);
        assert(answer == (capacity > input->length));
        assert(buffer[0] == 0xa5 && buffer[capacity + 1] == 0xa5);
        unsigned copied = capacity - 1 < input->length ? capacity - 1 : input->length;
        assert(!memcmp(buffer + 1, "/root/file", copied));
        assert(buffer[copied + 1] == (answer ? 0 : 0xa5));
        assert(input->references == 1 && releases == 1);
    }
    reset();
    record(url, 1, input, base);
    record(base, 1, text("Disk:base:"), NULL);
    store(base, 8, word(base, 8) | 0x1000);
    Byte buffer[32];
    memset(buffer, 0xa5, sizeof buffer);
    assert(ROMlib_carbon_cf_url_filesystem_bytes(url, 1, buffer, sizeof buffer) && merges == 1);
    assert(!strcmp((char*)buffer, "/base/child") && merge_arguments[2] == 1 && merge_arguments[3] == 1);
    reset();
    record(url, 1, input, NULL);
    encode_limit = 3;
    memset(buffer, 0xa5, sizeof buffer);
    assert(!ROMlib_carbon_cf_url_filesystem_bytes(url, 0, buffer, sizeof buffer) && buffer[3] == 0xa5 && releases == 1);
    reset();
    record(url, 1, input, NULL);
    encode_oversize = 1;
    memset(buffer, 0xa5, sizeof buffer);
    assert(!ROMlib_carbon_cf_url_filesystem_bytes(url, 0, buffer, 10) && buffer[9] == 0xa5);
    assert(!ROMlib_carbon_cf_url_filesystem_bytes(url, 0, buffer, 0));
    assert(!ROMlib_carbon_cf_url_filesystem_bytes(url, 0, buffer, 0x80000000));
    assert(!ROMlib_carbon_cf_url_filesystem_bytes(url, 0, NULL, sizeof buffer));
    reset();
    foreign = 1;
    assert(ROMlib_carbon_cf_url_absolute(url) == decoded && last_count == 1);
    assert(ROMlib_carbon_cf_url_filesystem_path(url, 0x81234567) == decoded && last_count == 2 &&
           last_arguments[1] == 0x81234567);
    assert(ROMlib_carbon_cf_url_filesystem_bytes(url, 0x81234567, buffer, 0xfedcba98) == (foreign_answer & 255));
    assert(last_count == 4 && last_arguments[1] == 0x81234567 && last_arguments[2] == US_TO_SYN68K(buffer) &&
           last_arguments[3] == 0xfedcba98);
    active = 0;
}
