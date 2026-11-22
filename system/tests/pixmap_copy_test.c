/*+===================================================================
  File:      pixmap_copy_test.c

  Summary:   Implements pixmap copy test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main, syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include "rsys/common.h"
#include "QuickDraw.h"
#include "CQuickDraw.h"
#include "MemoryMgr.h"
#include "rsys/cquick.h"
#include <assert.h>
#undef black
#undef white
#undef main
uint64 ROMlib_offset;
uint32 syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count;
uint32 syn68k_host_alias_base, syn68k_host_alias_size;

void* syn68k_guest_to_host(uint32 address)
{
    (void)address;
    abort();
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    (void)address;
    abort();
}

static unsigned char* memory;
static int fail_pixels, fail_region, allocated, disposed, regions, blits;

static Ptr test_pixels(LONGINT size)
{
    ++allocated;
    if (fail_pixels) {
        MemErr = memFullErr;
        return NULL;
    }
    assert(size > 0 && size < 60000);
    MemErr = noErr;
    return (Ptr)(memory + 1024);
}

static void test_dispose(Ptr p)
{
    assert(p == (Ptr)(memory + 1024));
    ++disposed;
    MemErr = noErr;
}

static RgnHandle test_region(void)
{
    ++regions;
    return fail_region ? NULL : (RgnHandle)(memory + 128);
}

static BOOLEAN test_sect(const Rect* a, const Rect* b, Rect* out)
{
    (void)b;
    *out = *a;
    return TRUE;
}

static void test_rect(RgnHandle h, const Rect* r)
{
    (void)h;
    (void)r;
}

static void test_dispose_region(RgnHandle h)
{
    assert(h);
}

static void test_colors(const PixMap* pm, uint32* black, uint32* white)
{
    (void)pm;
    *black = 0;
    *white = 0xffffff;
}

static void test_blit(RgnHandle h, int mode, int tile, int bpp, const PixMap* source, PixMap* dest, const Rect* a,
                      const Rect* b, uint32 black, uint32 white)
{
    (void)h;
    (void)mode;
    (void)tile;
    (void)bpp;
    (void)source;
    (void)a;
    (void)b;
    (void)black;
    (void)white;
    assert(PPR(dest->baseAddr) == (Ptr)(memory + 1024));
    ++blits;
}

#undef NewPtr
#undef DisposPtr
#undef NewRgn
#undef SectRect
#undef RectRgn
#undef DisposeRgn
#define NewPtr test_pixels
#define DisposPtr test_dispose
#define NewRgn test_region
#define SectRect test_sect
#define RectRgn test_rect
#define DisposeRgn test_dispose_region
#define pixmap_black_white test_colors
#define ROMlib_blt_rgn_update_dirty_rect test_blit
#include "pixmap-copy-function.inc"

int main(void)
{
    memory = calloc(1, 65536);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    PixMap source = {0}, dest;
    Rect bounds = {CWC(0), CWC(0), CWC(20), CWC(40)}, out;
    source.pixelSize = CWC(32);
    fail_pixels = 1;
    pixmap_copy(&source, &bounds, &dest, &out);
    assert(!PP_RAW(dest.baseAddr) && allocated == 1 && regions == 0 && blits == 0);
    fail_pixels = 0;
    fail_region = 1;
    pixmap_copy(&source, &bounds, &dest, &out);
    assert(!PP_RAW(dest.baseAddr) && allocated == 2 && regions == 1 && disposed == 1 && blits == 0);
    assert(MemErr == memFullErr);
    fail_region = 0;
    pixmap_copy(&source, &bounds, &dest, &out);
    assert(PP_RAW(dest.baseAddr) && blits == 1 && (uint16)CW(dest.rowBytes) == (160 | PIXMAP_DEFAULT_ROW_BYTES));
    bounds.right = CWC(32767);
    pixmap_copy(&source, &bounds, &dest, &out);
    assert(!PP_RAW(dest.baseAddr) && allocated == 3 && blits == 1);
    bounds.right = CWC(0);
    pixmap_copy(&source, &bounds, &dest, &out);
    assert(!PP_RAW(dest.baseAddr) && allocated == 3);
    free(memory);
    puts("Pixmap scratch copies: pixel/region allocation failures, invalid sizes and success passed");
}
