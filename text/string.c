/*+===================================================================
  File:      string.c

  Summary:   Implements string support for Macintosh fonts, scripts and international text.

  Classes:   None.

  Functions: pstr_from_c_string, pstr_index_after, str255_from_c_string, str31assign, str63assign,
             stringhandle_from_c_string

  Origin:    Existing repository source in executor/src/text; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"
#include "rsys/string.h"

#include "MemoryMgr.h"

/* Capacity includes the Pascal length byte. */
void pstr_from_c_string(StringPtr dest, size_t capacity, const char* source)
{
    size_t len = 0;

    if (!capacity)
        return;
    if (capacity > 256)
        capacity = 256;
    while (len < capacity - 1 && source[len])
        ++len;
    dest[0] = len;
    memcpy(dest + 1, source, len);
}

void str255_from_c_string(Str255 str255, const char* c_stringp)
{
    pstr_from_c_string(str255, sizeof(Str255), c_stringp);
}

char* pstr_index_after(StringPtr p, char c, int i)
{
    StringPtr ep;

    if (p) {
        for (ep = p + (unsigned char)p[0] + 1, p += 1 + i; p < ep && *p != c; p++)
            ;
        return (p == ep) ? NULL : (char*)p;
    }
    else
        return NULL;
}

PRIVATE void strNassign(Str63 new, const StringPtr old, int n)
{
    int old_length, new_length;

    old_length = U(old[0]);
    if (old_length <= n)
        new_length = old_length;
    else {
        warning_unexpected("Truncating string that's too long for a Str%d:  "
                           "\"%.*s\"",
                           n, old_length, old + 1);
        new_length = n;
    }

    new[0] = new_length;
    memcpy(new + 1, old + 1, new_length);
}

void str63assign(Str63 new, const StringPtr old)
{
    strNassign(new, old, 63);
}

void str31assign(Str63 new, const StringPtr old)
{
    strNassign(new, old, 31);
}

PUBLIC StringHandle stringhandle_from_c_string(const char* c_stringp)
{
    int len;
    StringHandle retval;

    /* A Pascal string contains at most 255 data bytes. */
    for (len = 0; len < 255 && c_stringp[len]; ++len)
        ;
    retval = (StringHandle)NewHandle(len + 1);
    if (retval)
        pstr_from_c_string(STARH(retval), len + 1, c_stringp);
    return retval;
}
