/*+===================================================================
  File:      cfstrings.c

  Summary:   CarbonLib 1.4 string comparison, interning, copying and query control flow.

  Classes:   None.

  Functions: ROMlib_carbon_cf_string_compare, ROMlib_carbon_cf_string_compare_options, ROMlib_carbon_cf_string_constant,
             ROMlib_carbon_cf_string_copy, ROMlib_carbon_cf_string_find, ROMlib_carbon_cf_string_int,
             ROMlib_carbon_cf_string_prefix, ROMlib_carbon_cf_string_suffix

  Origin:    Existing repository source in packages/carbonlib/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* CarbonLib 1.4 string comparison, interning, copying and query control flow. */
#include "internal.h"

static uint32 string_word(const void* p, unsigned offset)
{
    uint32 value;
    memcpy(&value, (const Byte*)p + offset, 4);
    return CL(value);
}

static void string_store(void* p, unsigned offset, uint32 value)
{
    value = CL(value);
    memcpy((Byte*)p + offset, &value, 4);
}

static int string_reference(const char* name, uint32 offset, const uint32* words, unsigned count,
                            ROMlib_carbon_reference* reference, uint32* result)
{
    if (!ROMlib_carbon_reference_function("CoreFoundationLib", name, reference))
        return 0;
    if (reference->code_offset == offset && reference->code_size == 0x5dee0 && !reference->toc_offset &&
        reference->toc && reference->toc_size == 0x72a8)
        return 1;
    uint32 answer;
    if (ROMlib_carbon_invoke_callback(reference->function, words, count, &answer) && result)
        *result = answer;
    return 0;
}

static int string_call(const char* name, const uint32* words, unsigned count, uint32* result)
{
    ROMlib_carbon_reference reference;
    return ROMlib_carbon_reference_function("CoreFoundationLib", name, &reference) &&
           ROMlib_carbon_invoke_callback(reference.function, words, count, result);
}

static int string_private(const ROMlib_carbon_reference* reference, uint32 offset, const uint32* words, unsigned count,
                          uint32* result)
{
    _Alignas(16) uint32 vector[2] = {CL(string_word(reference->function, 0) - reference->code_offset + offset),
                                     CL(US_TO_SYN68K(reference->toc))};
    return ROMlib_carbon_invoke_callback(vector, words, count, result);
}

typedef struct {
    uint32 object, offset, end, count, index;
    _Alignas(64) uint16 data[32];
    uint16 current;
} string_stream;

static int string_stream_fill(string_stream* stream)
{
    if ((int32)stream->offset >= (int32)stream->end) {
        stream->count = 0;
        stream->index = 1;
        stream->current = 0xffff;
        return 1;
    }
    stream->count = stream->end - stream->offset;
    if (stream->count > NELEM(stream->data))
        stream->count = NELEM(stream->data);
    uint32 words[] = {stream->object, stream->offset, stream->count, US_TO_SYN68K(stream->data)};
    if (!string_call("CFStringGetCharacters", words, 4, NULL))
        return 0;
    stream->index = 1;
    stream->current = CW(stream->data[0]);
    return 1;
}

static int string_stream_init(string_stream* stream, uint32 object, uint32 offset, uint32 length)
{
    stream->object = object;
    stream->offset = offset;
    stream->end = offset + length;
    return string_stream_fill(stream);
}

static int string_stream_next(string_stream* stream)
{
    if (stream->index < stream->count) {
        stream->current = CW(stream->data[stream->index++]);
        return 1;
    }
    stream->offset += stream->count;
    return string_stream_fill(stream);
}

static int string_fold(const ROMlib_carbon_reference* reference, uint16 character, uint32 options, uint32* folded)
{
    *folded = character;
    if (!(options & 1))
        return 1;
    if (character < 128) {
        if (character >= 'a' && character <= 'z')
            *folded -= 32;
        return 1;
    }
    uint32 value = character;
    return string_private(reference, 0x49358, &value, 1, folded);
}

static int string_decomposable(uint32 character, uint32* result)
{
    *result = 0;
    if ((character & 65535) <= 127)
        return 1;
    uint32 words[] = {character, 9};
    if (!string_call("CFUniCharIsMemberOf", words, 2, result))
        return 0;
    *result = (*result & 255) != 0;
    return 1;
}

static int string_compare(const ROMlib_carbon_reference* reference, uint32 first, uint32 second, uint32 location,
                          uint32 length, uint32 options, uint32* result)
{
    if (options & 32) {
        uint32 words[] = {first, second, location, length, options, 0};
        return string_private(reference, 0x398b0, words, 6, result);
    }
    string_stream a, b;
    uint32 second_length;
    if (!string_stream_init(&a, first, location, length) ||
        !string_call("CFStringGetLength", &second, 1, &second_length) ||
        !string_stream_init(&b, second, 0, second_length))
        return 0;
    while (a.current != 0xffff && b.current != 0xffff) {
        uint32 x, y;
        if (!string_fold(reference, a.current, options, &x) || !string_fold(reference, b.current, options, &y))
            return 0;
        x &= 65535;
        y &= 65535;
        if ((options & 16) && x != y) {
            uint32 xd, yd;
            if (!string_decomposable(x, &xd) || !string_decomposable(y, &yd))
                return 0;
            if (xd != yd) {
                _Alignas(32) uint16 decomposed[10];
                uint32 words[] = {0, xd ? x : y, US_TO_SYN68K(decomposed)}, count;
                if (!string_private(reference, 0x4617c, words, 3, &count))
                    return 0;
                if (count > NELEM(decomposed))
                    return ROMlib_carbon_invoke_callback(NULL, NULL, 0, NULL);
                string_stream* other = xd ? &b : &a;
                uint32 current = xd ? y : x;
                for (unsigned i = 0; i < count && other->current != 0xffff; ++i) {
                    uint32 unit = CW(decomposed[i]);
                    if (unit != current) {
                        *result = (xd ? unit < current : current < unit) ? UINT32_MAX : 1;
                        return 1;
                    }
                    if (!string_stream_next(other))
                        return 0;
                    current = other->current;
                }
                if (!string_stream_next(xd ? &a : &b))
                    return 0;
                continue;
            }
        }
        if (x != y) {
            *result = x < y ? UINT32_MAX : 1;
            return 1;
        }
        if (!string_stream_next(&a) || !string_stream_next(&b))
            return 0;
    }
    *result = a.current != 0xffff ? 1 : b.current != 0xffff ? UINT32_MAX : 0;
    return 1;
}

uint32 ROMlib_carbon_cf_string_compare_options(const void* first, const void* second, uint32 location, uint32 length,
                                               uint32 options)
{
    ROMlib_carbon_reference reference;
    uint32 words[] = {US_TO_SYN68K(first), US_TO_SYN68K(second), location, length, options}, result = 0;
    if (string_reference("CFStringCompareWithOptions", 0x39fa8, words, 5, &reference, &result))
        string_compare(&reference, words[0], words[1], location, length, options, &result);
    return result;
}

uint32 ROMlib_carbon_cf_string_compare(const void* first, const void* second, uint32 options)
{
    ROMlib_carbon_reference reference;
    uint32 words[] = {US_TO_SYN68K(first), US_TO_SYN68K(second), options}, result = 0, length;
    if (string_reference("CFStringCompare", 0x3a880, words, 3, &reference, &result) &&
        string_call("CFStringGetLength", words, 1, &length))
        string_compare(&reference, words[0], words[1], 0, length, options, &result);
    return result;
}

static uint32 string_has(const char* name, uint32 offset, const void* string, const void* pattern, uint32 options)
{
    ROMlib_carbon_reference reference;
    uint32 words[] = {US_TO_SYN68K(string), US_TO_SYN68K(pattern)}, result = 0, length;
    if (string_reference(name, offset, words, 2, &reference, &result)) {
        if (!string_call("CFStringGetLength", words, 1, &length))
            return 0;
        uint32 find[] = {words[0], words[1], 0, length, options, 0};
        if (!string_call("CFStringFindWithOptions", find, 6, &result))
            return 0;
    }
    return result & 255;
}

uint32 ROMlib_carbon_cf_string_prefix(const void* string, const void* pattern)
{
    return string_has("CFStringHasPrefix", 0x3b5ac, string, pattern, 8);
}

uint32 ROMlib_carbon_cf_string_suffix(const void* string, const void* pattern)
{
    return string_has("CFStringHasSuffix", 0x3b61c, string, pattern, 12);
}

/* CFRange uses the classic PPC caller-provided result in r3. */
void ROMlib_carbon_cf_string_find(void* range, const void* string, const void* pattern, uint32 options)
{
    ROMlib_carbon_reference reference;
    uint32 words[] = {US_TO_SYN68K(range), US_TO_SYN68K(string), US_TO_SYN68K(pattern), options}, result = 0, length;
    if (!string_reference("CFStringFind", 0x3b4e0, words, 4, &reference, &result) || !range)
        return;
    if (!string_call("CFStringGetLength", words + 1, 1, &length))
        return;
    _Alignas(8) uint32 found[2];
    uint32 find[] = {words[1], words[2], 0, length, options, US_TO_SYN68K(found)};
    if (!string_call("CFStringFindWithOptions", find, 6, &result))
        return;
    if (result & 255)
        memcpy(range, found, 8);
    else {
        string_store(range, 0, UINT32_MAX);
        string_store(range, 4, 0);
    }
}

static int string_number_space(uint16 c)
{
    return c <= 32 || (c >= 0x7f && c <= 0xa0) || (c >= 0x2000 && c <= 0x200b) || c == 0x3000;
}

uint32 ROMlib_carbon_cf_string_int(const void* string)
{
    ROMlib_carbon_reference reference;
    uint32 object = US_TO_SYN68K(string), result = 0, length;
    if (!string_reference("CFStringGetIntValue", 0x3cfe0, &object, 1, &reference, &result))
        return result;
    if (!string_call("CFStringGetLength", &object, 1, &length))
        return 0;
    string_stream stream;
    if (!string_stream_init(&stream, object, 0, length))
        return 0;
    while (string_number_space(stream.current))
        if (!string_stream_next(&stream))
            return 0;
    unsigned negative = stream.current == '-';
    if (negative || stream.current == '+') {
        if (!string_stream_next(&stream))
            return 0;
        while (string_number_space(stream.current))
            if (!string_stream_next(&stream))
                return 0;
    }
    uint32 limit = negative ? 0x80000000 : 0x7fffffff;
    while (stream.current >= '0' && stream.current <= '9') {
        uint32 digit = stream.current - '0';
        result = result > (limit - digit) / 10 ? limit : result * 10 + digit;
        if (!string_stream_next(&stream))
            return 0;
    }
    return negative ? 0u - result : result;
}

static uint32 string_data(uint32 object, uint32 flags)
{
    if (flags & 0x60)
        return string_word(SYN68K_TO_US(object), 8);
    return object + 8 + ((flags & 5) == 4 ? 0 : 4);
}

static uint32 string_storage_length(uint32 object, uint32 flags, uint32 data)
{
    void* string = SYN68K_TO_US(object);
    if ((flags & 5) == 4)
        return *(Byte*)SYN68K_TO_US(data);
    if (!(flags & 0x60))
        return string_word(string, 8);
    uint32 length = string_word(string, 12);
    if (length == 0xffffff) {
        length = strlen((const char*)SYN68K_TO_US(data));
        string_store(string, 12, length);
    }
    return length;
}

void* ROMlib_carbon_cf_string_copy(uint32 allocator, const void* string)
{
    ROMlib_carbon_reference reference;
    uint32 words[] = {allocator, US_TO_SYN68K(string)}, result = 0;
    if (!string_reference("CFStringCreateCopy", 0x37d08, words, 2, &reference, &result))
        return SYN68K_TO_US_CHECK0(result);
    uint32 object = words[1], flags = string_word(string, 4);
    if (!(flags & 1)) {
        uint32 effective = allocator, owner;
        if ((!effective && !string_call("CFAllocatorGetDefault", NULL, 0, &effective)) ||
            !string_call("CFGetAllocator", &object, 1, &owner))
            return NULL;
        if (owner == effective) {
            if (!string_call("CFRetain", &object, 1, NULL))
                return NULL;
            return (void*)string;
        }
    }
    uint32 data = string_data(object, flags), length = string_storage_length(object, flags, data), encoding;
    unsigned unicode = (flags & 0x10) != 0;
    if (unicode)
        encoding = 0x100;
    else {
        encoding = string_word(reference.toc, 0x3a70);
        if (encoding == UINT32_MAX && !string_call("__CFStringComputeEightBitStringEncoding", NULL, 0, NULL))
            return NULL;
        encoding = string_word(reference.toc, 0x3a70);
        if (flags & 4)
            ++data;
    }
    uint32 create[] = {allocator, data, unicode ? length * 2 : length, encoding, 0, unicode, 0, 0, 0, UINT32_MAX};
    if (!string_call("__CFStringCreateImmutableFunnel2", create, 10, &result))
        return NULL;
    return SYN68K_TO_US_CHECK0(result);
}

static int string_constant(const ROMlib_carbon_reference* reference, const Byte* literal, uint32* result);

static int string_constant_format(const ROMlib_carbon_reference* reference, unsigned offset, uint32* result)
{
    const Byte* message = (const Byte*)SYN68K_TO_US(string_word(reference->toc, 0x5a4) + offset);
    return string_constant(reference, message, result);
}

static int string_constant(const ROMlib_carbon_reference* reference, const Byte* literal, uint32* result)
{
    *result = 0;
    if (!literal)
        return 1;
    void* toc = (void*)reference->toc;
    uint32 dictionary = string_word(toc, 0x98c);
    if (!dictionary) {
        _Alignas(32) Byte keys[24];
        memcpy(keys, (const Byte*)toc + 0x3a98, sizeof keys);
        uint32 create[] = {0, 0, US_TO_SYN68K(keys), US_TO_SYN68K(toc) + 0x33f0};
        if (!string_call("CFDictionaryCreateMutable", create, 4, &dictionary))
            return 0;
        string_store(toc, 0x98c, dictionary);
        if (!dictionary)
            return 1;
    }
    /* The original lock/unlock entries at 0x1a98c/0x1a990 are both blr. */
    uint32 lookup[] = {dictionary, US_TO_SYN68K(literal)};
    if (!string_call("CFDictionaryGetValue", lookup, 2, result))
        return 0;
    if (*result)
        return 1;
    size_t length = strlen((const char*)literal);
    unsigned ascii = 1;
    for (size_t i = 0; i < length; ++i)
        if (literal[i] > 127) {
            ascii = 0;
            break;
        }
    if (!ascii) {
        uint32 create[] = {0, 0}, escaped;
        if (!string_call("CFStringCreateMutable", create, 2, &escaped))
            return 0;
        if (!escaped)
            return 1;
        for (size_t i = 0; i < length; ++i) {
            uint32 format;
            if (!string_constant_format(reference, literal[i] > 127 ? 0x5d : 0x62, &format))
                return 0;
            uint32 append[] = {escaped, 0, format, literal[i]};
            if (!string_call("CFStringAppendFormat", append, 4, NULL))
                return 0;
        }
        uint32 format;
        if (!string_constant_format(reference, 0x66, &format))
            return 0;
        uint32 log[] = {0, format, escaped};
        if (!string_call("CFLog", log, 3, NULL) || !string_call("CFRelease", &escaped, 1, NULL))
            return 0;
    }
    uint32 create[] = {0, US_TO_SYN68K(literal), 0}, object;
    if (!string_call("CFStringCreateWithCString", create, 3, &object))
        return 0;
    if (!object)
        return 1;
    uint32 flags = string_word(SYN68K_TO_US(object), 4), key;
    if (flags & 0x10) {
        if (length >= UINT32_MAX)
            return string_call("CFRelease", &object, 1, NULL);
        uint32 allocate[] = {0, length + 1, 0};
        if (!string_call("CFAllocatorAllocate", allocate, 3, &key))
            return 0;
        if (!key)
            return string_call("CFRelease", &object, 1, NULL);
        memcpy(SYN68K_TO_US(key), literal, length + 1);
    }
    else
        key = string_data(object, flags) + ((flags & 4) != 0);
    lookup[0] = string_word(toc, 0x98c);
    lookup[1] = key;
    uint32 existing;
    if (!string_call("CFDictionaryGetValue", lookup, 2, &existing))
        return 0;
    if (!existing) {
        uint32 insert[] = {lookup[0], key, object};
        if (!string_call("CFDictionarySetValue", insert, 3, NULL))
            return 0;
        existing = object;
    }
    if (!string_call("CFRelease", &object, 1, NULL))
        return 0;
    *result = existing;
    return 1;
}

void* ROMlib_carbon_cf_string_constant(const Byte* literal)
{
    ROMlib_carbon_reference reference;
    uint32 argument = US_TO_SYN68K(literal), result = 0;
    if (string_reference("__CFStringMakeConstantString", 0x38110, &argument, 1, &reference, &result) &&
        !string_constant(&reference, literal, &result))
        return NULL;
    return SYN68K_TO_US_CHECK0(result);
}
