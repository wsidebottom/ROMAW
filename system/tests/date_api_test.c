/*+===================================================================
  File:      date_api_test.c

  Summary:   Exercise cache initialization and the guest-facing date/time record ABI.

  Classes:   None.

  Functions: CToPascalCall, C_IUGetIntl, GetDateTime, GetHandleSize, Secs2Date, main, syn68k_guest_to_host,
             syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Exercise cache initialization and the guest-facing date/time record ABI. */
#define NDEBUG 1
#include "../../text/script.c"
#undef main
#undef NDEBUG
#include <assert.h>

uint64 ROMlib_offset;
uint32 syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count = 1;
uint32 syn68k_host_alias_base, syn68k_host_alias_size = 0xffffffff;
static HIDDEN_Ptr handles[2];
static Intl0Rec numeric;
static Intl1Rec verbose;
static int missing;
static void* pointers[32];
static unsigned pointer_count;
void* tooltraptable[0x400];
toolstuff_t toolstuff[0x400];

intptr_t CToPascalCall(void* p, unsigned long magic, ...)
{
    abort();
}

void* syn68k_guest_to_host(uint32 address)
{
    assert(address && address <= pointer_count);
    return pointers[address - 1];
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    for (unsigned i = 0; i < pointer_count; ++i)
        if (pointers[i] == (void*)address)
            return i + 1;
    pointers[pointer_count++] = (void*)address;
    return pointer_count;
}

Handle C_IUGetIntl(INTEGER id)
{
    assert(id == 0 || id == 1);
    return missing ? NULL : handles + id;
}

Size GetHandleSize(Handle h)
{
    return h == handles ? sizeof numeric : sizeof verbose;
}

void GetDateTime(LONGINT* seconds)
{
    *seconds = 0;
}

void Secs2Date(LONGINT seconds, DateTimeRec* today)
{
    memset(today, 0, sizeof *today);
    today->year = CWC(2026);
}

int main(void)
{
    struct {
        unsigned before;
        DateCacheRec cache;
        unsigned after;
    } guarded;

    struct {
        unsigned before;
        LongDateRec date;
        unsigned after;
    } out;

    LONGINT used;
    HIDDEN_RAW_SET(handles[0], RM(&numeric));
    HIDDEN_RAW_SET(handles[1], RM(&verbose));
    numeric.dateOrder = dmy;
    numeric.dateSep = '-';
    numeric.timeSep = ':';
    numeric.mornStr = CLC(0x20414d00);
    numeric.eveStr = CLC(0x20504d00);
    verbose.abbrLen = 3;
    memcpy(verbose.months[1], "\10February", 9);
    memset(&guarded, 0xa5, sizeof guarded);
    assert(!C_InitDateCache(&guarded.cache));
    assert(guarded.before == 0xa5a5a5a5 && guarded.after == 0xa5a5a5a5);
    assert(!memcmp(&guarded.cache, "EXD1", 4));
    /* The cache is a snapshot: subsequent locale changes do not mutate it. */
    numeric.dateOrder = mdy;
    memset(&out, 0xa5, sizeof out);
    assert(!C_String2Date((Ptr) "29-2-2000", 9, &guarded.cache, &used, &out.date));
    assert(CL(used) == 9 && CW(out.date.year) == 2000 && CW(out.date.month) == 2 && CW(out.date.day) == 29 &&
           CW(out.date.dayOfWeek) == 3 && CW(out.date.dayOfYear) == 60);
    assert((C_String2Time((Ptr) "12:34 PM", 8, &guarded.cache, &used, &out.date) & 0x8000) == 0);
    assert(CW(out.date.hour) == 12 && CW(out.date.minute) == 34 && CW(out.date.second) == 0 &&
           CW(out.date.year) == 2000);
    assert(out.before == 0xa5a5a5a5 && out.after == 0xa5a5a5a5);
    LongDateRec saved = out.date;
    assert((C_String2Time((Ptr) "12:99", 5, &guarded.cache, &used, &out.date) & 0x8000) != 0);
    assert(!used && !memcmp(&saved, &out.date, sizeof saved));
    assert((C_String2Date((Ptr) "x", 1, &guarded.cache, &used, &out.date) & 0x8000) != 0);
    assert(!used && !memcmp(&saved, &out.date, sizeof saved));
    assert(C_InitDateCache(NULL) == paramErr);
    missing = 1;
    assert(C_InitDateCache(&guarded.cache) == resNotFound);
    assert(!memcmp(&guarded.cache, "\0\0\0\0", 4));
    puts("PASS: initialized movable cache, locale snapshot, LongDateRec ABI and failures");
    return 0;
}
