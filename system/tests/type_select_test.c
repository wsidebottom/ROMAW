/*+===================================================================
  File:      type_select_test.c

  Summary:   Exercise Finder's Roman type-selection state and callback ABI.

  Classes:   None.

  Functions: CToPascalCall, RelString, main, syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Exercise Finder's Roman type-selection state and callback ABI. */
#define NDEBUG 1
#include "../../text/iu.c"
#undef NDEBUG
#undef main
#include <assert.h>
#include <stdarg.h>
#include "guest_test_memory.h"

uint64 ROMlib_offset;
uint32 syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count = 1;
uint32 syn68k_host_alias_base = 0xe0000000, syn68k_host_alias_size = 0x10000000;
static const unsigned char* items[] = {(void*)"\005Alpha", (void*)"\004Book", (void*)"\006Boston", (void*)"\004Zulu"};

void* syn68k_guest_to_host(uint32 p)
{
    return fixture_guest_to_host(p);
}

uint32 syn68k_host_to_guest(ptr_sized_uint p)
{
    return fixture_host_to_guest(p);
}

INTEGERRET
RelString(StringPtr a, StringPtr b, BOOLEAN c, BOOLEAN d)
{
    int order = strncasecmp((char*)a + 1, (char*)b + 1, MIN(a[0], b[0]));
    return order < 0 ? -1 : order > 0 ? 1 : a[0] < b[0] ? -1 : a[0] > b[0];
}

intptr_t CToPascalCall(void* callback, unsigned long convention, ...)
{
    assert(callback == (void*)0x1234);
    assert(convention == (1 | (2 << 3) | (5 << 6) | (5 << 9) | (5 << 12)));
    va_list ap;
    va_start(ap, convention);
    int index = va_arg(ap, int);
    INTEGER* script = va_arg(ap, INTEGER*);
    HIDDEN_Ptr* string = va_arg(ap, HIDDEN_Ptr*);
    assert(va_arg(ap, void*) == (void*)0x5678);
    assert(index >= 1 && index <= 4);
    *script = 0;
    HIDDEN_RAW_SET(*string, RM((Ptr)items[index - 1]));
    va_end(ap);
    return TRUE;
}

int main(void)
{
    _Static_assert(sizeof(ROMlib_type_select_record) == 70, "TypeSelect ABI");

    struct {
        unsigned before;
        ROMlib_type_select_record r;
        unsigned after;
    } g;

    memset(&g, 0xa5, sizeof g);
    ROMlib_type_select_clear(&g.r);
    assert(g.before == 0xa5a5a5a5 && g.after == 0xa5a5a5a5 && !g.r.keys[0]);
    EventRecord event = {0};
    event.what = CWC(keyDown);
    event.message = CLC('b');
    event.when = CLC(1);
    assert(ROMlib_type_select_key(&event, &g.r) && g.r.keys[0] == 1);
    assert(!ROMlib_type_select_compare(&g.r, 0, (StringPtr)items[1]));
    assert(ROMlib_type_select_find(&g.r, 4, 0, (ProcPtr)0x1234, (void*)0x5678) == 2);
    event.message = CLC('o');
    event.when = CLC(2);
    assert(ROMlib_type_select_key(&event, &g.r) && g.r.keys[0] == 2);
    event.message = CLC('s');
    ROMlib_type_select_key(&event, &g.r);
    assert(ROMlib_type_select_find(&g.r, 4, 0, (ProcPtr)0x1234, (void*)0x5678) == 3);
    assert(ROMlib_type_select_find(&g.r, 4, -1, (ProcPtr)0x1234, (void*)0x5678) == 2);
    assert(ROMlib_type_select_find(&g.r, 4, 1, (ProcPtr)0x1234, (void*)0x5678) == 4);
    event.message = CLC(8);
    ROMlib_type_select_key(&event, &g.r);
    assert(g.r.keys[0] == 2);
    event.message = CLC('z');
    event.when = CLC(64);
    assert(ROMlib_type_select_key(&event, &g.r) && g.r.keys[0] == 1);
    event.modifiers = CWC(cmdKey);
    event.message = CLC('q');
    assert(!ROMlib_type_select_key(&event, &g.r) && g.r.keys[1] == 'z');
    event.modifiers = 0;
    for (unsigned i = 0; i < 100; ++i)
        ROMlib_type_select_key(&event, &g.r);
    assert(g.r.keys[0] == 63 && g.after == 0xa5a5a5a5);
    g.r.keys[0] = 255;
    event.message = CLC('a');
    assert(ROMlib_type_select_key(&event, &g.r) && g.r.keys[0] == 1);
    assert(!ROMlib_type_select_find(&g.r, -1, 0, (ProcPtr)0x1234, (void*)0x5678));
    ROMlib_type_select_clear(NULL);
    assert(!ROMlib_type_select_key(NULL, &g.r));
    puts("Type select: timeout, bounded keys, prefix collation, neighbors and callback ABI passed");
}
