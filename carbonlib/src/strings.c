/*+===================================================================
  File:      strings.c

  Summary:   Bounded classic C/Pascal string conversion, including in-place calls.

  Classes:   None.

  Functions: ROMlib_carbon_c2pstrcpy, ROMlib_carbon_copy_c_to_pascal, ROMlib_carbon_copy_pascal_to_c,
             ROMlib_carbon_p2cstrcpy

  Origin:    Existing repository source in packages/carbonlib/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Bounded classic C/Pascal string conversion, including in-place calls. */
#include "internal.h"

void ROMlib_carbon_copy_pascal_to_c(const Byte* source, char* destination)
{
    if (!destination)
        return;
    unsigned length = source ? source[0] : 0;
    if (length)
        memmove(destination, source + 1, length);
    destination[length] = 0;
}

void ROMlib_carbon_copy_c_to_pascal(const char* source, Byte* destination)
{
    if (!destination)
        return;
    unsigned length = 0;
    if (source)
        while (length < 255 && source[length])
            ++length;
    if (length)
        memmove(destination + 1, source, length);
    destination[0] = length;
}

void ROMlib_carbon_c2pstrcpy(Byte* destination, const char* source)
{
    ROMlib_carbon_copy_c_to_pascal(source, destination);
}

void ROMlib_carbon_p2cstrcpy(char* destination, const Byte* source)
{
    ROMlib_carbon_copy_pascal_to_c(source, destination);
}
