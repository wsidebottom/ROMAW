/*+===================================================================
  File:      munger_growth_test.c

  Summary:   Reproduce a failed Munger growth without letting the old tail be overwritten.

  Classes:   None.

  Functions: BlockMove, GetHandleSize, MemError, SetHandleSize, main, syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Reproduce a failed Munger growth without letting the old tail be overwritten. */
#define NDEBUG 1
#include "rsys/common.h"
#include "rsys/mman_private.h"
#undef MM_SLAM
#define MM_SLAM(where) ((void)0)
#include "../../services/toolutil.c"
#undef NDEBUG
#undef main
#include <assert.h>
CPUState cpu_state;
#include "guest_test_memory.h"

uint64 ROMlib_offset;
uint32 syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count = 1;
uint32 syn68k_host_alias_base = 0xe0000000, syn68k_host_alias_size = 0x10000000;
static uint8 data[64];
static Size size;
static OSErr error;
static int fail_growth, moves;

void* syn68k_guest_to_host(uint32 p)
{
    return fixture_guest_to_host(p);
}

uint32 syn68k_host_to_guest(ptr_sized_uint p)
{
    return fixture_host_to_guest(p);
}

Size GetHandleSize(Handle h)
{
    error = noErr;
    return size;
}

OSErr MemError(void)
{
    return error;
}

void SetHandleSize(Handle h, Size requested)
{
    if (fail_growth && requested > size) {
        error = memFullErr;
        return;
    }
    assert(requested >= 0 && requested <= sizeof data);
    size = requested;
    error = noErr;
}

void BlockMove(Ptr from, Ptr to, Size count)
{
    ++moves;
    assert((uint8*)to >= data && (uint8*)to + count <= data + size);
    memmove(to, from, count);
}

int main(void)
{
    HIDDEN_Ptr master;
    HIDDEN_RAW_SET(master, RM((Ptr)data));
    Handle h = (Handle)&master;
    memset(data, 0xa5, sizeof data);
    memcpy(data, "abcdef", 6);
    size = 6;
    uint8 before[64];
    memcpy(before, data, sizeof data);
    fail_growth = 1;
    assert(C_Munger(h, 2, (Ptr) "", 0, (Ptr) "XYZ", 3) == -1);
    assert(MemError() == memFullErr && size == 6 && !moves);
    assert(!memcmp(before, data, sizeof data));
    fail_growth = 0;
    assert(C_Munger(h, 2, (Ptr) "", 0, (Ptr) "XYZ", 3) == 5);
    assert(!MemError() && size == 9 && moves == 1);
    assert(!memcmp(data, "abXYZcdef", 9) && data[9] == 0xa5);
    puts("Munger: failed growth preserves data, extent and heap guard; successful insertion passed");
}
