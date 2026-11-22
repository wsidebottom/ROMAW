/*+===================================================================
  File:      keyboard_resource_test.c

  Summary:   Loading the host keyboard must bypass guest Resource Manager patches.

  Classes:   None.

  Functions: CToPascalCall, C_GetResource, C_LoadResource, HLock, _gui_fatal, main, syn68k_guest_to_host,
             syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Loading the host keyboard must bypass guest Resource Manager patches. */
#define NDEBUG 1
#include "rsys/common.h"
#include "../../events/osevent.c"
#undef main
#undef NDEBUG
#include <assert.h>

#include "guest_test_memory.h"

uint64 ROMlib_offset;
uint32 syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count = 1;
uint32 syn68k_host_alias_base = 0xe0000000, syn68k_host_alias_size = 0x10000000;
void* tooltraptable[0x400];
toolstuff_t toolstuff[0x400];
static Handle expected;
static unsigned resource_gets, loads, locks;

void* syn68k_guest_to_host(uint32 address)
{
    return fixture_guest_to_host(address);
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    return fixture_host_to_guest(address);
}

intptr_t CToPascalCall(void* proc, unsigned long magic, ...)
{
    abort();
}

Handle C_GetResource(ResType type, INTEGER id)
{
    assert(type == TICK("KCHR") && id == 0 && TheZone == SysZone);
    ++resource_gets;
    return expected;
}

void C_LoadResource(Handle handle)
{
    assert(handle == expected && TheZone == SysZone);
    ++loads;
}

void HLock(Handle handle)
{
    assert(handle == expected);
    ++locks;
}

void _gui_fatal(const char* file, int line, const char* fn, const char* format, ...)
{
    abort();
}

int main(void)
{
    unsigned char* memory = calloc(1, 65536);
    THz saved;
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    HIDDEN_RAW_SET(SysZone_H, RM((THz)(memory + 0x1000)));
    saved = RM((THz)(memory + 0x2000));
    HIDDEN_RAW_SET(TheZone_H, saved);
    expected = (Handle)(memory + 0x3000);
    HIDDEN_RAW_SET(*expected, RM((Ptr)(memory + 0x4000)));
    /* Any attempted guest patch dispatch aborts through CToPascalCall. */
    tooltraptable[0x1a0] = (void*)1;
    tooltraptable[0x1a2] = (void*)1;
    assert(ROMlib_kchr_ptr() == (Ptr)(memory + 0x4000));
    assert(TheZone == saved && resource_gets == 1 && loads == 1 && locks == 1);
    assert(ROMlib_kchr_ptr() == (Ptr)(memory + 0x4000) && resource_gets == 1);
    invalidate_kchr_ptr();
    assert(ROMlib_kchr_ptr() == (Ptr)(memory + 0x4000) && resource_gets == 2);
    free(memory);
    puts("Native keyboard resource loading and zone restoration passed");
    return 0;
}
