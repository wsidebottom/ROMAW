/*+===================================================================
  File:      pascal.c

  Summary:   Pascal String Utilities, using the CarbonLib -> StdCLib re-export contract. Reference: local System 9
             StdCLib disassembly, 0x10ea8-0x11330, and Apple's Pascal String Utilities Reference (PLStringFuncs.h
             signatures).

  Classes:   None.

  Functions: ROMlib_carbon_pl_append, ROMlib_carbon_pl_append_n, ROMlib_carbon_pl_compare, ROMlib_carbon_pl_compare_n,
             ROMlib_carbon_pl_copy, ROMlib_carbon_pl_copy_n, ROMlib_carbon_pl_find_any, ROMlib_carbon_pl_find_char,
             ROMlib_carbon_pl_find_last_char, ROMlib_carbon_pl_find_string, ROMlib_carbon_pl_length,
             ROMlib_carbon_pl_position, ROMlib_carbon_pl_span

  Origin:    Existing repository source in packages/carbonlib/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Pascal String Utilities, using the CarbonLib -> StdCLib re-export contract.
   Reference: local System 9 StdCLib disassembly, 0x10ea8-0x11330, and Apple's
   Pascal String Utilities Reference (PLStringFuncs.h signatures). */
#include "internal.h"

INTEGER ROMlib_carbon_pl_length(const Byte* string)
{
    return string ? string[0] : 0;
}

INTEGER ROMlib_carbon_pl_compare_n(const Byte* first, const Byte* second, INTEGER maximum)
{
    if (maximum <= 0)
        return 0;
    unsigned first_length = ROMlib_carbon_pl_length(first), second_length = ROMlib_carbon_pl_length(second);
    unsigned length = first_length < second_length ? first_length : second_length;
    if (length > maximum)
        length = maximum;
    for (unsigned i = 1; i <= length; ++i)
        if (first[i] != second[i])
            return (INTEGER)first[i] - second[i];
    return length == maximum ? 0 : (INTEGER)first_length - (INTEGER)second_length;
}

INTEGER ROMlib_carbon_pl_compare(const Byte* first, const Byte* second)
{
    return ROMlib_carbon_pl_compare_n(first, second, 256);
}

Byte* ROMlib_carbon_pl_copy(Byte* destination, const Byte* source)
{
    if (destination) {
        unsigned length = ROMlib_carbon_pl_length(source);
        if (length)
            memmove(destination + 1, source + 1, length);
        destination[0] = length;
    }
    return destination;
}

Byte* ROMlib_carbon_pl_copy_n(Byte* destination, const Byte* source, INTEGER maximum)
{
    if (!destination || maximum <= 0)
        return destination;
    unsigned limit = maximum > 255 ? 255 : maximum;
    unsigned source_length = ROMlib_carbon_pl_length(source);
    unsigned copied = source_length < limit ? source_length : limit;
    /* StdCLib preserves a longer destination's length when the requested
       prefix is fully supplied. A short source instead shortens the string
       and zero-fills through the requested cap, outside its new length. */
    unsigned result_length = destination[0] < limit || source_length < limit ? copied : destination[0];
    if (copied)
        memmove(destination + 1, source + 1, copied);
    if (copied < limit)
        memset(destination + 1 + copied, 0, limit - copied);
    destination[0] = result_length;
    return destination;
}

Byte* ROMlib_carbon_pl_append_n(Byte* destination, const Byte* source, INTEGER maximum)
{
    if (!destination || maximum <= 0)
        return destination;
    unsigned length = destination[0], copied = ROMlib_carbon_pl_length(source);
    if (copied > maximum)
        copied = maximum;
    if (copied > 255 - length)
        copied = 255 - length;
    if (copied)
        memmove(destination + length + 1, source + 1, copied);
    destination[0] = length + copied;
    return destination;
}

Byte* ROMlib_carbon_pl_append(Byte* destination, const Byte* source)
{
    return ROMlib_carbon_pl_append_n(destination, source, 255);
}

const Byte* ROMlib_carbon_pl_find_char(const Byte* string, INTEGER character)
{
    unsigned length = ROMlib_carbon_pl_length(string);
    for (unsigned i = 1; i <= length; ++i)
        if (string[i] == (Byte)character)
            return string + i;
    return NULL;
}

const Byte* ROMlib_carbon_pl_find_last_char(const Byte* string, INTEGER character)
{
    for (unsigned i = ROMlib_carbon_pl_length(string); i; --i)
        if (string[i] == (Byte)character)
            return string + i;
    return NULL;
}

const Byte* ROMlib_carbon_pl_find_any(const Byte* string, const Byte* characters)
{
    unsigned length = ROMlib_carbon_pl_length(string);
    for (unsigned i = 1; i <= length; ++i)
        if (ROMlib_carbon_pl_find_char(characters, string[i]))
            return string + i;
    return NULL;
}

INTEGER ROMlib_carbon_pl_span(const Byte* string, const Byte* characters)
{
    unsigned length = ROMlib_carbon_pl_length(string), i = 0;
    while (i < length && ROMlib_carbon_pl_find_char(characters, string[i + 1]))
        ++i;
    return i;
}

const Byte* ROMlib_carbon_pl_find_string(const Byte* string, const Byte* search)
{
    unsigned length = ROMlib_carbon_pl_length(string), wanted = ROMlib_carbon_pl_length(search);
    if (wanted)
        for (unsigned i = 1; i + wanted - 1 <= length; ++i)
            if (!memcmp(string + i, search + 1, wanted))
                return string + i;
    return NULL;
}

INTEGER ROMlib_carbon_pl_position(const Byte* search, const Byte* string)
{
    const Byte* found = ROMlib_carbon_pl_find_string(string, search);
    return found ? found - string : 0;
}
