/*+===================================================================
  File:      cf_string_queries.c

  Summary:   Native string searches, line bounds and collection operations. Comparison shares the native comparison
             entry and its retained Unicode workers.

  Classes:   None.

  Functions: ROMlib_cfstring_combine, ROMlib_cfstring_find_options, ROMlib_cfstring_find_results,
             ROMlib_cfstring_line_bounds, ROMlib_cfstring_separate

  Origin:    Existing repository source in packages/carbonlib/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Native string searches, line bounds and collection operations. Comparison
   shares the native comparison entry and its retained Unicode workers. */
#include "cf_batch.h"
#include <limits.h>

static int query_length(uint32 object, uint32* length)
{
    return object && cfp_call("CFStringGetLength", &object, 1, length) && (int32)*length >= 0;
}

static int query_character(uint32 object, uint32 index, uint32* character)
{
    uint32 arguments[] = {object, index};
    return cfp_call("CFStringGetCharacterAtIndex", arguments, 2, character);
}

uint32 ROMlib_cfstring_find_options(uint32 string, uint32 pattern, uint32 location, uint32 length, uint32 options,
                                    uint32 output)
{
    uint32 arguments[] = {string, pattern, location, length, options, output}, result = 0, total, pattern_length;
    ROMlib_carbon_reference reference;
    if (!cfp_reference("CFStringFindWithOptions", 0x3a8f8, arguments, 6, &reference, &result))
        return result;
    if (!query_length(string, &total) || !query_length(pattern, &pattern_length) || (uint64)location + length > total ||
        !pattern_length || !length)
        return 0;
    if (!(options & 16) && pattern_length > length)
        return 0;
    uint32 end = location + length;
    uint32 first = options & 4 ? end - 1 : location;
    for (uint32 offset = 0; offset < length; ++offset) {
        uint32 start = options & 4 ? first - offset : first + offset;
        uint32 minimum = options & 16 ? 1 : pattern_length;
        uint32 maximum = options & 16 ? end - start : pattern_length;
        if (maximum > end - start)
            maximum = end - start;
        if ((options & 8) && !(options & 4) && start != location)
            break;
        for (uint32 count = minimum; count <= maximum; ++count) {
            if ((options & 12) == 12 && start + count != end)
                continue;
            uint32 compared = ROMlib_carbon_cf_string_compare_options(
                SYN68K_TO_US_CHECK0(string), SYN68K_TO_US_CHECK0(pattern), start, count, options & (1 | 16));
            if (!compared) {
                if (output && (!cfp_write(output, start) || !cfp_write(output + 4, count)))
                    return 0;
                return 1;
            }
        }
    }
    return 0;
}

static int line_break(uint32 character)
{
    return character == 10 || character == 13 || character == 0x2028 || character == 0x2029;
}

void ROMlib_cfstring_line_bounds(uint32 object, uint32 location, uint32 length, uint32 start_output, uint32 end_output,
                                 uint32 contents_output)
{
    uint32 arguments[] = {object, location, length, start_output, end_output, contents_output}, ignored = 0, total;
    ROMlib_carbon_reference reference;
    if (!cfp_reference("CFStringGetLineBounds", 0x3b68c, arguments, 6, &reference, &ignored) ||
        !query_length(object, &total))
        return;
    if ((uint64)location + length > total) {
        cfp_failure("Native Carbon string line range is invalid");
        return;
    }
    uint32 start = location, probe = length ? location + length - 1 : location, character, previous;
    /* An index between CR and LF belongs to the line containing the pair. */
    if (start && start < total && query_character(object, start, &character) && character == 10 &&
        query_character(object, start - 1, &previous) && previous == 13)
        --start;
    while (start) {
        if (!query_character(object, start - 1, &character))
            return;
        if (line_break(character))
            break;
        --start;
    }
    uint32 contents = probe;
    while (contents < total) {
        if (!query_character(object, contents, &character))
            return;
        if (line_break(character))
            break;
        ++contents;
    }
    if (contents && contents < total && character == 10 && query_character(object, contents - 1, &previous) &&
        previous == 13)
        --contents;
    uint32 end = contents;
    if (end < total) {
        if (!query_character(object, end, &character))
            return;
        ++end;
        if (character == 13 && end < total) {
            if (!query_character(object, end, &character))
                return;
            if (character == 10)
                ++end;
        }
    }
    if (start_output && !cfp_write(start_output, start))
        return;
    if (end_output && !cfp_write(end_output, end))
        return;
    if (contents_output)
        cfp_write(contents_output, contents);
}

uint32 ROMlib_cfstring_find_results(uint32 allocator, uint32 string, uint32 pattern, uint32 location, uint32 length,
                                    uint32 options)
{
    uint32 arguments[] = {allocator, string, pattern, location, length, options}, result = 0, ignored;
    ROMlib_carbon_reference reference;
    if (!cfp_reference("CFStringCreateArrayWithFindResults", 0x3b2f4, arguments, 6, &reference, &result))
        return result;
    uint32 total;
    if (!query_length(string, &total) || (uint64)location + length > total)
        return 0;
    uint32 data = 0, count = 0, end = location + length;
    while (length) {
        _Alignas(16) uint32 match[3] = {0, 0, 0};
        if (!ROMlib_cfstring_find_options(string, pattern, location, length, options, US_TO_SYN68K(match)))
            break;
        uint32 found = CL(match[0]), matched = CL(match[1]);
        if (!matched || found < location || (uint64)found + matched > end)
            return cfp_failure("Native Carbon string search did not advance");
        if (!data)
            data = ROMlib_cfdata_create_mutable(allocator, 0);
        if (!data)
            return 0;
        match[2] = CL(data);
        ROMlib_cfdata_append(data, US_TO_SYN68K(match), sizeof match);
        ++count;
        /* CarbonLib 1.4 advances to the end even for a backwards search. */
        location = found + matched;
        length = end - location;
    }
    if (!count)
        return 0;
    uint32 bytes;
    if (!cfp_call("CFDataGetBytePtr", &data, 1, &bytes))
        return 0;
    result = ROMlib_cfarray_create_mutable(allocator, count * 4, US_TO_SYN68K(reference.toc) + 0x3ab0);
    if (!result)
        return 0;
    for (uint32 index = 0; index < count; ++index)
        ROMlib_cfarray_append(result, bytes + index * 12);
    cfp_call("CFRelease", &data, 1, &ignored);
    return result;
}

uint32 ROMlib_cfstring_separate(uint32 allocator, uint32 string, uint32 separator)
{
    uint32 arguments[] = {allocator, string, separator}, result = 0, total, separator_length, ignored;
    ROMlib_carbon_reference reference;
    if (!cfp_reference("CFStringCreateArrayBySeparatingStrings", 0x3c46c, arguments, 3, &reference, &result))
        return result;
    if (!query_length(string, &total) || !query_length(separator, &separator_length))
        return 0;
    /* Empty separators produce one copy of the original string. */
    result = ROMlib_cfarray_create_mutable(allocator, 0, US_TO_SYN68K(reference.toc) + 0x3234);
    if (!result)
        return 0;
    uint32 start = 0;
    for (;;) {
        _Alignas(16) uint32 match[2];
        uint32 found = total, matched = 0;
        if (separator_length &&
            ROMlib_cfstring_find_options(string, separator, start, total - start, 0, US_TO_SYN68K(match))) {
            found = CL(match[0]);
            matched = CL(match[1]);
        }
        uint32 part = ROMlib_cfstring_substring(allocator, string, start, found - start);
        if (!part)
            return 0;
        ROMlib_cfarray_append(result, part);
        if (!cfp_call("CFRelease", &part, 1, &ignored))
            return 0;
        if (!matched)
            break;
        start = found + matched;
    }
    return result;
}

uint32 ROMlib_cfstring_combine(uint32 allocator, uint32 strings, uint32 separator)
{
    uint32 arguments[] = {allocator, strings, separator}, result = 0, count, ignored;
    ROMlib_carbon_reference reference;
    if (!cfp_reference("CFStringCreateByCombiningStrings", 0x3bf68, arguments, 3, &reference, &result))
        return result;
    if (!cfp_call("CFArrayGetCount", &strings, 1, &count))
        return 0;
    uint32 mutable = ROMlib_cfstring_create_mutable(allocator, 0);
    if (!mutable)
        return 0;
    for (uint32 index = 0; index < count; ++index) {
        uint32 get[] = {strings, index}, value;
        if (!cfp_call("CFArrayGetValueAtIndex", get, 2, &value))
            return 0;
        if (index)
            ROMlib_cfstring_append(mutable, separator);
        ROMlib_cfstring_append(mutable, value);
    }
    uint32 copy[] = {allocator, mutable};
    cfp_call("CFStringCreateCopy", copy, 2, &result);
    cfp_call("CFRelease", &mutable, 1, &ignored);
    return result;
}
