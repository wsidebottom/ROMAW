/*+===================================================================
  File:      bitmap_region_test.c

  Summary:   Bitmap-to-region allocation failure must leave the heap untouched.

  Classes:   None.

  Functions: MemError, SetHandleSize, main, syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Bitmap-to-region allocation failure must leave the heap untouched. */
#define NDEBUG 1
#include "../../quickdraw/qIMVI.c"
#undef main
#undef NDEBUG
#include <assert.h>

__typeof__(ROMlib_offset) ROMlib_offset;
uint32 syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count;
uint32 syn68k_host_alias_base, syn68k_host_alias_size;
static unsigned char memory[0x20000];
static Size extent;
static OSErr memory_error;
static int fail;

void* syn68k_guest_to_host(uint32 address)
{
    return (void*)(uintptr_t)(ROMlib_offset + address);
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    return address - ROMlib_offset;
}

void SetHandleSize(Handle handle, Size size)
{
    assert((void*)handle == memory + 0x1000);
    if (fail) {
        memory_error = memFullErr;
        return;
    }
    assert(size <= 32766 && !(size & 1));
    extent = size;
    memory_error = noErr;
}

OSErr MemError(void)
{
    return memory_error;
}

int main(void)
{
    ROMlib_offset = (uintptr_t)memory;
    RgnHandle region = (RgnHandle)(memory + 0x1000);
    HIDDEN_RAW_SET(*region, RM((Ptr)(memory + 0x2000)));
    BitMap bitmap = {0};
    bitmap.rowBytes = CWC(4);
    bitmap.bounds.bottom = bitmap.bounds.right = CWC(32);
    HIDDEN_RAW_SET(bitmap.baseAddr, RM((Ptr)(memory + 0x10000)));
    memset(memory + 0x10000, 0xaa, 128);
    memset(memory + 0x2000, 0x5a, 32768);
    unsigned char before[32768];
    memcpy(before, memory + 0x2000, sizeof before);
    fail = 1;
    assert(C_BitMapToRegion(region, &bitmap) == memFullErr);
    assert(!memcmp(before, memory + 0x2000, sizeof before));
    fail = 0;
    assert(C_BitMapToRegion(region, &bitmap) == noErr);
    assert(extent > SMALLRGN && extent < 4096);
    assert(RGN_SIZE(region) == extent);
    for (unsigned i = 2322; i < 32768; ++i)
        assert(memory[0x2000 + i] == 0x5a);
    assert(C_BitMapToRegion(NULL, &bitmap) == paramErr);
    bitmap.rowBytes = CWC(2);
    assert(C_BitMapToRegion(region, &bitmap) == paramErr);
    bitmap.rowBytes = CWC(64);
    bitmap.bounds.bottom = bitmap.bounds.right = CWC(512);
    for (unsigned row = 0; row < 512; ++row)
        memset(memory + 0x10000 + row * 64, row & 1 ? 0xaa : 0, 64);
    memory[0x2000 + 32766] = memory[0x2000 + 32767] = 0x5a;
    assert(C_BitMapToRegion(region, &bitmap) == rgnTooBigErr);
    assert(memory[0x2000 + 32766] == 0x5a && memory[0x2000 + 32767] == 0x5a);
    puts("bitmap regions preserve allocation failures and bound icon storage");
    return 0;
}
