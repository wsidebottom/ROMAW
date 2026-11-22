/*+===================================================================
  File:      cf_string_edit.c

  Summary:   Native string editing. Unicode character mapping and classification retain the initialized 1.4 tables;
             resizing/replacement retain its private workers.

  Classes:   None.

  Functions: ROMlib_cfstring_capitalize, ROMlib_cfstring_lowercase, ROMlib_cfstring_pad, ROMlib_cfstring_trim,
             ROMlib_cfstring_trim_whitespace, ROMlib_cfstring_uppercase

  Origin:    Existing repository source in packages/carbonlib/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Native string editing. Unicode character mapping and classification retain
   the initialized 1.4 tables; resizing/replacement retain its private workers. */
#include "cf_batch.h"
#include <limits.h>

static int edit_length(uint32 object, uint32* length)
{
    return object && cfp_call("CFStringGetLength", &object, 1, length) && (int32)*length >= 0;
}

static int edit_character(uint32 object, uint32 index, uint32* character)
{
    uint32 arguments[] = {object, index};
    return cfp_call("CFStringGetCharacterAtIndex", arguments, 2, character);
}

void ROMlib_cfstring_pad(uint32 object, uint32 padding, uint32 target, uint32 pad_index)
{
    uint32 arguments[] = {object, padding, target, pad_index}, ignored = 0, length, padding_length;
    ROMlib_carbon_reference reference;
    if (!cfp_reference("CFStringPad", 0x3d944, arguments, 4, &reference, &ignored) || !edit_length(object, &length))
        return;
    if (target > INT32_MAX / 2) {
        cfp_failure("Native Carbon string padding length is invalid");
        return;
    }
    if (target <= length) {
        if (target < length)
            ROMlib_cfstring_delete(object, target, length - target);
        return;
    }
    if (!edit_length(padding, &padding_length) || !padding_length || pad_index >= padding_length) {
        cfp_failure("Native Carbon string padding source or index is invalid");
        return;
    }
    uint32 source = padding, copy = 0;
    if (object == padding) {
        uint32 duplicate[] = {0, padding};
        if (!cfp_call("CFStringCreateCopy", duplicate, 2, &copy) || !copy)
            return;
        source = copy;
    }
    while (length < target) {
        _Alignas(512) uint16 buffer[256];
        uint32 count = padding_length - pad_index;
        if (count > target - length)
            count = target - length;
        if (count > NELEM(buffer))
            count = NELEM(buffer);
        uint32 extract[] = {source, pad_index, count, US_TO_SYN68K(buffer)};
        if (!cfp_call("CFStringGetCharacters", extract, 4, &ignored))
            return;
        ROMlib_cfstring_append_characters(object, US_TO_SYN68K(buffer), count);
        length += count;
        pad_index += count;
        if (pad_index == padding_length)
            pad_index = 0;
    }
    if (copy)
        cfp_call("CFRelease", &copy, 1, &ignored);
}

void ROMlib_cfstring_trim(uint32 object, uint32 pattern)
{
    uint32 arguments[] = {object, pattern}, ignored = 0, length, pattern_length;
    ROMlib_carbon_reference reference;
    if (!cfp_reference("CFStringTrim", 0x3dd68, arguments, 2, &reference, &ignored) || !edit_length(object, &length) ||
        !edit_length(pattern, &pattern_length) || !pattern_length)
        return;
    uint32 start = 0, end = length;
    while (end - start >= pattern_length &&
           !ROMlib_carbon_cf_string_compare_options(SYN68K_TO_US_CHECK0(object), SYN68K_TO_US_CHECK0(pattern), start,
                                                    pattern_length, 0))
        start += pattern_length;
    while (end - start >= pattern_length &&
           !ROMlib_carbon_cf_string_compare_options(SYN68K_TO_US_CHECK0(object), SYN68K_TO_US_CHECK0(pattern),
                                                    end - pattern_length, pattern_length, 0))
        end -= pattern_length;
    if (end < length)
        ROMlib_cfstring_delete(object, end, length - end);
    if (start)
        ROMlib_cfstring_delete(object, 0, start);
}

static int edit_member(const ROMlib_carbon_reference* reference, uint32 character, uint32 kind, uint32* member)
{
    uint32 arguments[] = {character, kind};
    return cfp_private(reference, 0x461fc, arguments, 2, member);
}

void ROMlib_cfstring_trim_whitespace(uint32 object)
{
    uint32 ignored = 0, length;
    ROMlib_carbon_reference reference;
    if (!cfp_reference("CFStringTrimWhitespace", 0x3e108, &object, 1, &reference, &ignored) ||
        !edit_length(object, &length))
        return;
    uint32 start = 0, end = length, character, member;
    while (start < end) {
        if (!edit_character(object, start, &character) || !edit_member(&reference, character, 3, &member))
            return;
        if (!(member & 255))
            break;
        ++start;
    }
    while (end > start) {
        if (!edit_character(object, end - 1, &character) || !edit_member(&reference, character, 3, &member))
            return;
        if (!(member & 255))
            break;
        --end;
    }
    if (end < length)
        ROMlib_cfstring_delete(object, end, length - end);
    if (start)
        ROMlib_cfstring_delete(object, 0, start);
}

static void string_case(const ROMlib_carbon_reference* reference, uint32 object, unsigned mode)
{
    uint32 length, flags, ignored;
    if (!edit_length(object, &length) || !cfp_get(object + 4, &flags))
        return;
    if (!(flags & 1)) {
        cfp_failure("Native Carbon case conversion requires a mutable string");
        return;
    }
    if (!length)
        return;
    uint32 storage = (flags & 0x60) ? cfp_read(object + 8) : object + 8 + ((flags & 5) == 4 ? 0 : 4);
    if (!(flags & 0x10)) {
        uint32 first = (flags & 4) ? 1 : 0;
        uint8* bytes = ROMlib_carbon_guest_memory(storage + first, length, 1);
        if (!bytes)
            return;
        uint32 index;
        int in_word = 0;
        for (index = 0; index < length; ++index) {
            unsigned character = bytes[index];
            if (character > 127)
                break;
            if (mode == 0 || (mode == 2 && in_word)) {
                if (character >= 'A' && character <= 'Z')
                    bytes[index] += 'a' - 'A';
            }
            else if (character >= 'a' && character <= 'z')
                bytes[index] -= 'a' - 'A';
            in_word =
                (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') || character == '\'';
        }
        if (index == length)
            return;
    }
    /* Conversion of an 8-bit string to Unicode occurs before taking the byte
       pointer again: resizing can move storage or invoke an allocator callback. */
    uint32 resize[] = {object, 0, 0, 0, 1};
    if (!cfp_private(reference, 0x35918, resize, 5, &ignored) || !cfp_get(object + 4, &flags))
        return;
    storage = (flags & 0x60) ? cfp_read(object + 8) : object + 8 + ((flags & 5) == 4 ? 0 : 4);
    int in_word = 0;
    for (uint32 index = 0; index < length; ++index) {
        const void* source = ROMlib_carbon_guest_memory(storage + index * 2, 2, 0);
        if (!source)
            return;
        uint16 encoded;
        memcpy(&encoded, source, 2);
        uint32 character = CW(encoded);
        _Alignas(16) uint16 mapped[4] = {0, 0, 0, 0};
        uint32 mapping = mode == 2 ? (in_word ? 0 : 2) : mode;
        uint32 arguments[] = {character, US_TO_SYN68K(mapped), NELEM(mapped), mapping, 0}, count;
        if (!cfp_private(reference, 0x492e0, arguments, 5, &count))
            return;
        if (!count || count > NELEM(mapped)) {
            cfp_failure("Native Carbon Unicode case mapping returned an invalid extent");
            return;
        }
        /* The 1.4 public loop advances one character irrespective of the mapping
           width. Preserve writes within the remaining string, and reject its
           historical out-of-range expansion rather than overwriting allocation
           metadata beyond the last character. */
        if (count > length - index) {
            cfp_failure("Native Carbon Unicode case expansion exceeds the original string extent");
            return;
        }
        if (!cfp_move(storage + index * 2, US_TO_SYN68K(mapped), count * 2))
            return;
        if (mode == 2) {
            uint32 member;
            if (!edit_member(reference, CW(mapped[0]), 5, &member))
                return;
            in_word = (member & 255) || CW(mapped[0]) == '\'' || CW(mapped[0]) == 0x2bc;
        }
    }
}

void ROMlib_cfstring_lowercase(uint32 object, uint32 locale)
{
    uint32 arguments[] = {object, locale}, ignored = 0;
    ROMlib_carbon_reference reference;
    if (cfp_reference("CFStringLowercase", 0x3e6e4, arguments, 2, &reference, &ignored))
        string_case(&reference, object, 0);
}

void ROMlib_cfstring_uppercase(uint32 object, uint32 locale)
{
    uint32 arguments[] = {object, locale}, ignored = 0;
    ROMlib_carbon_reference reference;
    if (cfp_reference("CFStringUppercase", 0x3e908, arguments, 2, &reference, &ignored))
        string_case(&reference, object, 1);
}

void ROMlib_cfstring_capitalize(uint32 object, uint32 locale)
{
    uint32 arguments[] = {object, locale}, ignored = 0;
    ROMlib_carbon_reference reference;
    if (cfp_reference("CFStringCapitalize", 0x3eb2c, arguments, 2, &reference, &ignored))
        string_case(&reference, object, 2);
}
