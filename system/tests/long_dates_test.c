/*+===================================================================
  File:      long_dates_test.c

  Summary:   Long-date output must initialize strings and retain all 64 date bits.

  Classes:   None.

  Functions: BlockMove, CToPascalCall, C_CurResFile, C_UseResFile, NumToString, ROMlib_getrestid, _NewHandle_flags,
             _warning, main, syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Long-date output must initialize strings and retain all 64 date bits. */
#define NDEBUG 1
#include "../../text/iu.c"
#undef main
#undef NDEBUG
#include <assert.h>
const char NULL_STRING[] = "";

void _warning(int err, const char* kind, const char* file, int line, const char* fn, const char* fmt, ...)
{
    abort();
}

#include "guest_test_memory.h"

uint64 ROMlib_offset;
uint32 syn68k_address_mask = 0xFFFFFFFFUL;
unsigned syn68k_host_alias_count = 1;
uint32 syn68k_host_alias_base = 0xe0000000, syn68k_host_alias_size = 0x10000000;
void* tooltraptable[0x400];
toolstuff_t toolstuff[0x400];

intptr_t CToPascalCall(void* proc, unsigned long magic, ...)
{
    abort();
}

void* syn68k_guest_to_host(uint32 address)
{
    return fixture_guest_to_host(address);
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    return fixture_host_to_guest(address);
}

static Handle locale[2];

INTEGER
C_CurResFile(void)
{
    return 0;
}

void C_UseResFile(INTEGER ref)
{
}

Handle ROMlib_getrestid(ResType type, INTEGER id)
{
    return id < 2 ? locale[id] : NULL;
}

Handle _NewHandle_flags(Size size, boolean_t sys, boolean_t clear)
{
    abort();
}

void NumToString(LONGINT n, StringPtr p)
{
    char text[32];
    int length = snprintf(text, sizeof text, "%ld", (long)n);
    p[0] = length;
    memcpy(p + 1, text, length);
}

void BlockMove(Ptr source, Ptr destination, Size size)
{
    memmove(destination, source, size);
}

static LongDateTime date_value(long long seconds)
{
    LongDateTime value;
    uint64 bits;
    memcpy(&bits, &seconds, sizeof bits);
    value.hilo.hi = CL((uint32)(bits >> 32));
    value.hilo.lo = CL((uint32)bits);
    return value;
}

static void expect_string(const unsigned char* p, const char* expected)
{
    assert(p[0] == strlen(expected) && !memcmp(p + 1, expected, p[0]));
}

static void pascal_string(unsigned char* p, const char* text)
{
    p[0] = strlen(text);
    memcpy(p + 1, text, p[0]);
}

int main(void)
{
    unsigned char* memory = calloc(1, 65536);
    Intl0Ptr numeric;
    Intl1Ptr verbose;
    LongDateTime value;
    DateTimeRec record;
    unsigned char guarded[258];
    unsigned i;

    static const struct {
        long long seconds;
        int year, month, day, hour, minute, second, weekday;
    } cases[] = {
        {-60052665601LL, 1, 1, 1, 23, 59, 59, 2},      {-9588067201LL, 1600, 2, 29, 23, 59, 59, 3},
        {-126144001LL, 1899, 12, 31, 23, 59, 59, 1},   {-121046401LL, 1900, 2, 28, 23, 59, 59, 4},
        {-120960001LL, 1900, 3, 1, 23, 59, 59, 5},     {-1LL, 1903, 12, 31, 23, 59, 59, 5},
        {86399LL, 1904, 1, 1, 23, 59, 59, 6},          {5183999LL, 1904, 2, 29, 23, 59, 59, 2},
        {2082931199LL, 1970, 1, 1, 23, 59, 59, 5},     {3029529599LL, 1999, 12, 31, 23, 59, 59, 6},
        {3034713599LL, 2000, 2, 29, 23, 59, 59, 3},    {3875212799LL, 2026, 10, 18, 23, 59, 59, 1},
        {4230403199LL, 2038, 1, 19, 23, 59, 59, 3},    {4295030399LL, 2040, 2, 6, 23, 59, 59, 2},
        {4295116799LL, 2040, 2, 7, 23, 59, 59, 3},     {6190387199LL, 2100, 2, 28, 23, 59, 59, 1},
        {6190473599LL, 2100, 3, 1, 23, 59, 59, 2},     {15657494399LL, 2400, 2, 29, 23, 59, 59, 3},
        {255485145599LL, 9999, 12, 31, 23, 59, 59, 6},
    };

    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    locale[0] = (Handle)(memory + 0x1000);
    locale[1] = (Handle)(memory + 0x1004);
    HIDDEN_RAW_SET(*locale[0], RM((Ptr)(memory + 0x2000)));
    HIDDEN_RAW_SET(*locale[1], RM((Ptr)(memory + 0x3000)));
    numeric = (Intl0Ptr)STARH(locale[0]);
    verbose = (Intl1Ptr)STARH(locale[1]);
    numeric->dateOrder = mdy;
    numeric->dateSep = '/';
    numeric->shrtDateFmt = century;
    numeric->timeSep = ':';
    numeric->timeFmt = hrLeadingZ | minLeadingZ | secLeadingZ;
    verbose->lngDateFmt = 1;
    verbose->abbrLen = 3;
    verbose->st1 = CLC(0x2c200000);
    verbose->st2 = CLC(0x20000000);
    verbose->st3 = CLC(0x2c200000);
    {
        const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
        const char* months[] = {"January", "February", "March",     "April",   "May",      "June",
                                "July",    "August",   "September", "October", "November", "December"};
        for (i = 0; i < 7; ++i)
            pascal_string(verbose->days[i], days[i]);
        for (i = 0; i < 12; ++i)
            pascal_string(verbose->months[i], months[i]);
    }
    for (i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        char expected[64];
        value = date_value(cases[i].seconds);
        assert(long_date_record(&value, &record));
        assert(CW(record.year) == cases[i].year && CW(record.month) == cases[i].month &&
               CW(record.day) == cases[i].day);
        assert(CW(record.hour) == cases[i].hour && CW(record.minute) == cases[i].minute &&
               CW(record.second) == cases[i].second && CW(record.dayOfWeek) == cases[i].weekday);
        memset(guarded, 0xCD, sizeof guarded);
        C_IULDateString(&value, shortDate, guarded + 1, NULL);
        snprintf(expected, sizeof expected, "%d/%d/%d", cases[i].month, cases[i].day, cases[i].year);
        expect_string(guarded + 1, expected);
        assert(guarded[0] == 0xCD && guarded[257] == 0xCD);
        C_IULTimeString(&value, TRUE, guarded + 1, NULL);
        expect_string(guarded + 1, "23:59:59");
    }
    value = date_value(0);
    C_IULDateString(&value, longDate, guarded + 1, locale[1]);
    expect_string(guarded + 1, "Friday, January 1, 1904");
    C_IULDateString(&value, abbrevDate, guarded + 1, locale[1]);
    expect_string(guarded + 1, "Fri, Jan 1, 1904");
    numeric->dateOrder = dmy;
    numeric->dateSep = '-';
    C_IULDateString(&value, shortDate, guarded + 1, locale[0]);
    expect_string(guarded + 1, "1-1-1904");
    C_IULTimeString(&value, FALSE, guarded + 1, locale[0]);
    expect_string(guarded + 1, "00:00");
    numeric->timeCycle = 1;
    numeric->mornStr = CLC(0x20414d00);
    numeric->eveStr = CLC(0x20504d00);
    C_IULTimeString(&value, FALSE, guarded + 1, locale[0]);
    expect_string(guarded + 1, "12:00 AM");
    value = date_value(43200);
    C_IULTimeString(&value, TRUE, guarded + 1, locale[0]);
    expect_string(guarded + 1, "12:00:00 PM");
    value = date_value(-1);
    C_IULTimeString(&value, TRUE, guarded + 1, locale[0]);
    expect_string(guarded + 1, "11:59:59 PM");
    value = date_value(0x7FFFFFFFFFFFFFFFLL);
    C_IULDateString(&value, longDate, guarded + 1, NULL);
    assert(!guarded[1]);
    value = date_value((-0x7FFFFFFFFFFFFFFFLL - 1));
    C_IULTimeString(&value, TRUE, guarded + 1, NULL);
    assert(!guarded[1]);
    C_IULDateString(NULL, longDate, guarded + 1, NULL);
    assert(!guarded[1]);
    free(memory);
    puts(
        "PASS: 64-bit locale date/time output, Gregorian boundaries, pre-epoch dates, rollover and initialized failures");
    return 0;
}
