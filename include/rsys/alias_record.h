/*+===================================================================
  File:      alias_record.h

  Summary:   Bounded access to the public version-2 Alias Manager record layout. ifndef RSYS_ALIAS_RECORD_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Bounded access to the public version-2 Alias Manager record layout. */
#ifndef RSYS_ALIAS_RECORD_H
#define RSYS_ALIAS_RECORD_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static unsigned alias_word(const unsigned char* p)
{
    return (unsigned)p[0] * 256 + p[1];
}

static int alias_record_valid(const unsigned char* p, size_t size)
{
    size_t end, pos;
    if (!p || size < 150 || alias_word(p + 6) != 2 || p[10] > 27 || p[50] > 63)
        return 0;
    end = alias_word(p + 4);
    if (end < 150 || end > size)
        return 0;
    for (pos = 150; pos < end;) {
        unsigned tag, n;
        if (end - pos < 4)
            return 0;
        tag = alias_word(p + pos);
        n = alias_word(p + pos + 2);
        pos += 4;
        if (tag == 0xffff)
            return n == 0 && pos == end;
        if (n > end - pos || (n + (n & 1)) > end - pos)
            return 0;
        pos += n + (n & 1);
    }
    return end == 150;
}

static const unsigned char* alias_record_tag(const unsigned char* p, unsigned wanted, size_t* length)
{
    size_t pos, end = alias_word(p + 4);
    *length = 0;
    for (pos = 150; pos + 4 <= end;) {
        unsigned tag = alias_word(p + pos), n = alias_word(p + pos + 2);
        pos += 4;
        if (tag == wanted) {
            *length = n;
            return p + pos;
        }
        if (tag == 0xffff)
            break;
        pos += n + (n & 1);
    }
    return NULL;
}

static int alias_relative_path(const unsigned char* absolute, size_t size, int levels, const unsigned char* leaf,
                               unsigned char* result)
{
    size_t start = size, leaf_start, prefix;
    if (!result)
        return 0;
    result[0] = 0;
    if (!absolute || !size || !leaf || !leaf[0] || leaf[0] > 63 || levels < 1 || levels > 128 || absolute[0] == ':' ||
        absolute[size - 1] == ':')
        return 0;
    for (size_t i = 1; i < size; ++i)
        if (absolute[i] == ':' && absolute[i - 1] == ':')
            return 0;
    leaf_start = size;
    while (leaf_start && absolute[leaf_start - 1] != ':')
        --leaf_start;
    for (int level = 0; level < levels; ++level) {
        if (!start)
            return 0;
        --start;
        while (start && absolute[start - 1] != ':')
            --start;
    }
    if (!start)
        return 0; /* The volume name cannot be part of a relative suffix. */
    prefix = leaf_start - start;
    if (prefix + leaf[0] + 1 > 255)
        return 0;
    result[0] = prefix + leaf[0] + 1;
    result[1] = ':';
    memcpy(result + 2, absolute + start, prefix);
    memcpy(result + 2 + prefix, leaf + 1, leaf[0]);
    return 1;
}

static int alias_record_info(const unsigned char* p, size_t size, int index, unsigned char* result)
{
    const unsigned char* text = NULL;
    size_t length = 0;
    if (!result)
        return -50;
    result[0] = 0;
    if (index < -3 || !alias_record_valid(p, size))
        return -50;
    if (index == 0 || index == -1) {
        text = p + (index == 0 ? 50 : 10);
        length = *text++;
    }
    else if (index > 0) {
        size_t end;
        text = alias_record_tag(p, 2, &length);
        if (text) {
            end = length;
            /* Skip the file name, then walk parents towards the root. */
            while (end && text[end - 1] != ':')
                --end;
            length = 0;
            for (int level = 1; level <= index && end; ++level) {
                size_t start = --end;
                while (start && text[start - 1] != ':')
                    --start;
                if (level == index) {
                    /* The volume is not a parent folder. */
                    length = start ? end - start : 0;
                    text += start;
                    break;
                }
                end = start;
            }
        }
        else if (index == 1)
            text = alias_record_tag(p, 0, &length);
    }
    else {
        /* AFP mount-info strings use offsets, not fixed-size string slots. */
        const unsigned char* tail = alias_record_tag(p, 9, &length);
        size_t field = index == -3 ? 12 : 14;
        size_t offset = tail && length >= field + 2 ? alias_word(tail + field) : 0;
        if ((p[137] & 16) && offset >= 24 && offset < length && tail[offset] <= length - offset - 1) {
            text = tail + offset + 1;
            length = tail[offset];
        }
        else
            length = 0;
    }
    if (!text)
        length = 0;
    if (length > 63)
        length = 63;
    result[0] = length;
    if (length)
        memcpy(result + 1, text, length);
    return 0;
}
#endif
