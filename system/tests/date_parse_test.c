/*+===================================================================
  File:      date_parse_test.c

  Summary:   Implements date parse test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "rsys/date_parse.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static void name(unsigned char* p, const char* s)
{
    p[0] = strlen(s);
    memcpy(p + 1, s, p[0]);
}

int main(void)
{
    romaw_date_cache cache = {0};
    const char* months[] = {"January", "February", "March",     "April",   "May",      "June",
                            "July",    "August",   "September", "October", "November", "December"};
    int year, month, day, hour, minute, second, status;
    size_t used;
    cache.date_sep = '/';
    cache.time_sep = ':';
    cache.abbreviation = 3;
    cache.year[0] = 2026 >> 8;
    cache.year[1] = 2026 & 255;
    for (int i = 0; i < 12; ++i)
        name(cache.months[i], months[i]);
    name(cache.days[0], "Sunday");
    name(cache.am, "AM");
    name(cache.pm, "PM");

    const struct {
        const char* text;
        unsigned order;
        int year, month, day, fatal;
    } dates[] = {
        {"2/29/2000", 0, 2000, 2, 29, 0},
        {"29/2/2000", 1, 2000, 2, 29, 0},
        {"2000/2/29", 2, 2000, 2, 29, 0},
        {"February 29, 2000", 0, 2000, 2, 29, 0},
        {"29 Feb. 2000", 0, 2000, 2, 29, 0},
        {"Sunday, December 31, 2023", 0, 2023, 12, 31, 0},
        {" 1-2-26 trailing", 0, 2026, 1, 2, 0},
        {"2/29/1900", 0, 0, 0, 0, 1},
        {"2/29/2100", 0, 0, 0, 0, 1},
        {"13/1/2000", 0, 0, 0, 0, 1},
        {"1/0/2000", 0, 0, 0, 0, 1},
        {"12/31/32767", 0, 32767, 12, 31, 0},
        {"1/1/32768", 0, 0, 0, 0, 1},
        {"1/1/999999999999999999999", 0, 0, 0, 0, 1},
        {"", 0, 0, 0, 0, 1},
        {"1", 0, 0, 0, 0, 1},
    };

    for (unsigned i = 0; i < sizeof dates / sizeof dates[0]; ++i) {
        cache.order = dates[i].order;
        romaw_date_input input = {(const unsigned char*)dates[i].text, strlen(dates[i].text), 0};
        used = 0;
        status = romaw_parse_date(input, &cache, &year, &month, &day, &used);
        assert(!!(status & 0x8000) == dates[i].fatal);
        if (!dates[i].fatal)
            assert(year == dates[i].year && month == dates[i].month && day == dates[i].day && used <= input.length);
    }

    const struct {
        const char* text;
        int hour, minute, second, fatal;
    } times[] = {
        {"12:00 AM", 0, 0, 0, 0},    {"12:00 pm", 12, 0, 0, 0},
        {"1:02:03 PM", 13, 2, 3, 0}, {"23:59:59", 23, 59, 59, 0},
        {"24:00", 0, 0, 0, 1},       {"13:00 PM", 0, 0, 0, 1},
        {"12:60", 0, 0, 0, 1},       {"00:00:60", 0, 0, 0, 1},
        {"1:", 0, 0, 0, 1},          {"99999999999999999", 0, 0, 0, 1},
    };

    for (unsigned i = 0; i < sizeof times / sizeof times[0]; ++i) {
        romaw_date_input input = {(const unsigned char*)times[i].text, strlen(times[i].text), 0};
        used = 0;
        status = romaw_parse_time(input, &cache, &hour, &minute, &second, &used);
        assert(!!(status & 0x8000) == times[i].fatal);
        if (!times[i].fatal)
            assert(hour == times[i].hour && minute == times[i].minute && second == times[i].second &&
                   used <= input.length);
    }
    /* Exact-size buffers have no implicit terminator; ASan checks every prefix. */
    for (size_t length = 0; length < 80; ++length) {
        unsigned char* text = malloc(length ? length : 1);
        for (int sample = 0; sample < 1000; ++sample) {
            for (size_t i = 0; i < length; ++i)
                text[i] = rand() & 255;
            romaw_date_input input = {text, length, 0};
            romaw_parse_date(input, &cache, &year, &month, &day, &used);
            romaw_parse_time(input, &cache, &hour, &minute, &second, &used);
        }
        free(text);
    }
    puts("PASS: locale date orders, names, leap years, AM/PM and 80000 bounded inputs");
    return 0;
}
