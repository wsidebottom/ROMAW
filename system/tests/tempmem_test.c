/*+===================================================================
  File:      tempmem_test.c

  Summary:   Temporary handles must remain outside the application partition.

  Classes:   None.

  Functions: _FreeMem_flags, _MaxMem_flags, _NewHandle_flags, main, syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Temporary handles must remain outside the application partition. */
#define NDEBUG 1
#include "rsys/common.h"
#include "../../memory/tempmem.c"
#undef main
#undef NDEBUG
#include <assert.h>
#include "guest_test_memory.h"

uint64 ROMlib_offset;
uint32 syn68k_address_mask = 0xFFFFFFFFUL;
unsigned syn68k_host_alias_count = 1;
uint32 syn68k_host_alias_base = 0xe0000000, syn68k_host_alias_size = 0x10000000;

void* syn68k_guest_to_host(uint32 address)
{
    return fixture_guest_to_host(address);
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    return fixture_host_to_guest(address);
}

static boolean_t fail;
static Handle expected;

int32 _FreeMem_flags(boolean_t sys)
{
    assert(sys);
    return 123;
}

Size _MaxMem_flags(Size* grow, boolean_t sys)
{
    assert(TheZone == SysZone);
    *grow = CLC(17);
    return 99;
}

Handle _NewHandle_flags(Size size, boolean_t clear, boolean_t sys)
{
    assert(TheZone == SysZone && size == 20);
    MemErr = CW(fail ? memFullErr : noErr);
    return fail ? NULL : expected;
}

int main(void)
{
    unsigned char* memory = calloc(1, 65536);
    OSErr result;
    Size grow;
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    HIDDEN_RAW_SET(SysZone_H, (THz)RM(memory + 0x1000));
    HIDDEN_RAW_SET(ApplZone_H, (THz)RM(memory + 0x2000));
    HIDDEN_RAW_SET(TheZone_H, (THz)RM(memory + 0x3000));
    expected = (Handle)(memory + 0x1100);
    assert(C_TempFreeMem() == 123);
    assert(C_TempMaxMem(&grow) == 99 && grow == CLC(17));
    assert(TheZone == (THz)RM(memory + 0x3000));
    assert(C_TempNewHandle(20, &result) == expected && result == CWC(noErr));
    assert(TheZone == (THz)RM(memory + 0x3000));
    fail = TRUE;
    assert(!C_TempNewHandle(20, &result) && result == CWC(memFullErr));
    assert(!C_TempNewHandle(20, NULL));
    assert(TheZone == (THz)RM(memory + 0x3000));
    free(memory);
    puts("Temporary memory uses system heap and preserves caller zone on failure");
    return 0;
}
