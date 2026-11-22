/*+===================================================================
  File:      date_parse.h

  Summary:   Bounded Roman date/time parsing. The opaque cache contains bytes, not host pointers, and can be moved or
             copied by guest applications. ifndef RSYS_DATE_PARSE_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Bounded Roman date/time parsing. The opaque cache contains bytes, not host
   pointers, and can be moved or copied by guest applications. */
#ifndef RSYS_DATE_PARSE_H
#define RSYS_DATE_PARSE_H
#include <stddef.h>
#include <string.h>

typedef struct {
    unsigned char magic[4], order, date_sep, time_sep, abbreviation;
    unsigned char days[7][16], months[12][16], am[16], pm[16], year[2];
} romaw_date_cache;

typedef struct {
    const unsigned char* text;
    size_t length, pos;
} romaw_date_input;

static int date_space(unsigned char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == 0xca;
}

static void date_skip_space(romaw_date_input* in)
{
    while (in->pos < in->length && date_space(in->text[in->pos]))
        ++in->pos;
}

static unsigned char date_fold(unsigned char ch)
{
    return ch >= 'A' && ch <= 'Z' ? ch + 'a' - 'A' : ch;
}

static int date_number(romaw_date_input* in, int* value, int* digits)
{
    size_t start = in->pos;
    *value = 0;
    while (in->pos < in->length && in->text[in->pos] >= '0' && in->text[in->pos] <= '9') {
        unsigned digit = in->text[in->pos++] - '0';
        if (*value > (32767 - (int)digit) / 10)
            return -1;
        *value = *value * 10 + digit;
    }
    *digits = in->pos - start;
    return *digits != 0;
}

static int date_name(romaw_date_input* in, const unsigned char* name, unsigned abbreviation)
{
    size_t available = in->length - in->pos, n = name[0];
    if (!n || n > 15)
        return 0;
    for (int shortened = 0; shortened < 2; ++shortened) {
        size_t i, count = shortened ? abbreviation : n;
        if (!count || count > n || count > available)
            continue;
        for (i = 0; i < count; ++i)
            if (date_fold(in->text[in->pos + i]) != date_fold(name[i + 1]))
                break;
        if (i == count) {
            unsigned char next = count < available ? in->text[in->pos + count] : 0;
            if ((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z') || next >= 0x80)
                continue;
            in->pos += count;
            if (in->pos < in->length && in->text[in->pos] == '.')
                ++in->pos;
            return 1;
        }
    }
    return 0;
}

static int date_month(romaw_date_input* in, const romaw_date_cache* cache)
{
    for (int i = 0; i < 12; ++i)
        if (date_name(in, cache->months[i], cache->abbreviation))
            return i + 1;
    return 0;
}

static int date_separator(romaw_date_input* in, unsigned char preferred, int* status)
{
    size_t start = in->pos;
    date_skip_space(in);
    if (in->pos < in->length) {
        unsigned char ch = in->text[in->pos];
        if (ch == preferred || ch == '/' || ch == '-' || ch == '.' || ch == ',') {
            ++in->pos;
            if (ch != preferred)
                *status |= 4; /* sepNotIntlSep */
            date_skip_space(in);
            return 1;
        }
    }
    return in->pos != start;
}

static int date_finish(romaw_date_input* in, size_t* used, int status)
{
    date_skip_space(in);
    *used = in->pos;
    return status | (in->pos < in->length ? 2 : 0); /* leftOverChars */
}

static int romaw_parse_date(romaw_date_input in, const romaw_date_cache* cache, int* year, int* month, int* day,
                            size_t* used)
{
    int a, b, c, digits = 0, status = 0, named, third_digits;
    date_skip_space(&in);
    for (int i = 0; i < 7; ++i)
        if (date_name(&in, cache->days[i], cache->abbreviation)) {
            date_separator(&in, ',', &status);
            break;
        }
    named = date_month(&in, cache);
    if (named)
        a = named;
    else if (date_number(&in, &a, &digits) != 1)
        return 0x8400;
    if (!date_separator(&in, cache->date_sep, &status))
        return 0x8400;
    b = named ? 0 : date_month(&in, cache);
    if (b)
        named = -1;
    else if (date_number(&in, &b, &third_digits) != 1)
        return 0x8400;
    if (!date_separator(&in, cache->date_sep, &status) || date_number(&in, &c, &third_digits) != 1)
        return 0x8400;
    if (named > 0)
        *month = a, *day = b, *year = c;
    else if (named < 0)
        *day = a, *month = b, *year = c;
    else if (cache->order == 2)
        *year = a, *month = b, *day = c, third_digits = digits;
    else if (cache->order == 1)
        *day = a, *month = b, *year = c;
    else
        *month = a, *day = b, *year = c;
    {
        unsigned current = (unsigned)cache->year[0] * 256 + cache->year[1];
        if (*year < 100) {
            int short_year = current % 100;
            if (short_year > 90 && *year <= 10)
                *year += 100;
            else if (short_year <= 10 && *year > 90)
                *year -= 100;
            *year += current / 100 * 100;
        }
        else if (*year < 1000)
            *year += current / 1000 * 1000;
    }
    {
        static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        int leap = *year % 4 == 0 && (*year % 100 != 0 || *year % 400 == 0);
        if (*year < 1 || *year > 32767 || *month < 1 || *month > 12 || *day < 1 ||
            *day > days[*month - 1] + (*month == 2 && leap))
            return 0x8800;
    }
    if (named)
        status |= 1; /* longDateFound */
    return date_finish(&in, used, status);
}

static int romaw_parse_time(romaw_date_input in, const romaw_date_cache* cache, int* hour, int* minute, int* second,
                            size_t* used)
{
    int digits, status = 0, suffix = -1;
    date_skip_space(&in);
    if (date_number(&in, hour, &digits) != 1)
        return 0x8400;
    *minute = *second = 0;
    if (in.pos < in.length && (in.text[in.pos] == cache->time_sep || in.text[in.pos] == ':')) {
        unsigned char separator = in.text[in.pos++];
        if (separator != cache->time_sep)
            status |= 4;
        if (date_number(&in, minute, &digits) != 1)
            return 0x8800;
        if (in.pos < in.length && in.text[in.pos] == separator) {
            ++in.pos;
            if (date_number(&in, second, &digits) != 1)
                return 0x8800;
        }
    }
    date_skip_space(&in);
    if (date_name(&in, cache->am, 0))
        suffix = 0;
    else if (date_name(&in, cache->pm, 0))
        suffix = 1;
    if (*minute > 59 || *second > 59 || (suffix < 0 ? *hour > 23 : *hour < 1 || *hour > 12))
        return 0x8800;
    if (suffix >= 0)
        *hour = *hour % 12 + suffix * 12;
    return date_finish(&in, used, status);
}
#endif
