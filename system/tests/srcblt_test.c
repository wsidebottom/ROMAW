/*+===================================================================
  File:      srcblt_test.c

  Summary:   Windows indexed blits must preserve the portable fgbk truth tables.

  Classes:   None.

  Functions: main, nonspecial_rgn_to_special_rgn, syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Windows indexed blits must preserve the portable fgbk truth tables. */
#define NDEBUG 1
#include "rsys/common.h"
#include "../../quickdraw/srcblt.c"
#undef main
#undef NDEBUG
#include <assert.h>

#if SIZEOF_CHAR_P == 8
uint64 ROMlib_offset;
#else
uint32 ROMlib_offset;
#endif
uint32 syn68k_address_mask = 0xFFFFFFFFUL;
unsigned syn68k_host_alias_count;
uint32 syn68k_host_alias_base, syn68k_host_alias_size;
INTEGER phony_special_region[7] = {0, 0, 0, RGNSTOP, 0, RGNSTOP, RGNSTOPX};

void* syn68k_guest_to_host(uint32 address)
{
    abort();
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    abort();
}

void nonspecial_rgn_to_special_rgn(const INTEGER* source, INTEGER* destination)
{
    abort();
}

static unsigned char expected_pixel(unsigned source, unsigned destination, unsigned foreground, unsigned background,
                                    unsigned mode)
{
    unsigned bit, result = 0;
    for (bit = 0; bit < 8; ++bit) {
        unsigned mask = 1U << bit;
        int ink = !!(source & mask) ^ !!(mode & 4);
        int value;
        switch (mode & 3) {
        case 0:
            value = !!((ink ? foreground : background) & mask);
            break;
        case 1:
            value = !!((ink ? foreground : destination) & mask);
            break;
        case 2:
            value = !!(destination & mask) ^ ink;
            break;
        default:
            value = !!((ink ? background : destination) & mask);
            break;
        }
        if (value)
            result |= mask;
    }
    return result;
}

int main(void)
{
    static const unsigned colors[] = {0, 5, 0x5A, 0xFF};
    unsigned char* memory = calloc(1, 4096);
    RgnHandle region;
    blt_bitmap_t source = {0}, destination = {0};
    Point src_origin = {CWC(4), CWC(8)};
    Point dst_origin = {CWC(2), CWC(4)};
    unsigned mode, fg, bk, value, x, y;
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    region = (RgnHandle)(memory + 64);
    *(uint32*)region = CL(128);
    STARH(region)->rgnSize = CWC(sizeof(Region));
    STARH(region)->rgnBBox = (Rect){CWC(2), CWC(4), CWC(4), CWC(10)};
    FIELD_ASSIGN_RAW(source.baseAddr, CL(512));
    source.rowBytes = CWC(12);
    source.bounds = (Rect){CWC(4), CWC(7), CWC(6), CWC(15)};
    FIELD_ASSIGN_RAW(destination.baseAddr, CL(1024));
    destination.rowBytes = CWC(16);
    destination.bounds = (Rect){CWC(2), CWC(3), CWC(4), CWC(11)};
    srcblt_log2_bpp = 3;
    for (mode = 0; mode < 8; ++mode)
        for (fg = 0; fg < NELEM(colors); ++fg)
            for (bk = 0; bk < NELEM(colors); ++bk)
                for (value = 0; value < 256; ++value) {
                    unsigned char before[48], original[24];
                    memset(memory + 1016, 0xCC, sizeof before);
                    for (y = 0; y < 2; ++y)
                        for (x = 0; x < 8; ++x) {
                            memory[512 + y * 12 + x] = value + x * 37 + y * 13;
                            memory[1024 + y * 16 + x] = value ^ (x * 19 + y * 71);
                        }
                    memcpy(before, memory + 1016, sizeof before);
                    memcpy(original, memory + 512, sizeof original);
                    assert(win32_srcblt_8bpp(region, mode, &source, &destination, &src_origin, &dst_origin, colors[fg],
                                             colors[bk]));
                    for (y = 0; y < 2; ++y)
                        for (x = 1; x < 7; ++x)
                            before[8 + y * 16 + x] = expected_pixel(original[y * 12 + x], before[8 + y * 16 + x],
                                                                    colors[fg], colors[bk], mode);
                    assert(!memcmp(before, memory + 1016, sizeof before));
                    assert(!memcmp(original, memory + 512, sizeof original));
                }
    /* Palette dragging moves overlapping pixels in both axes. */
    FIELD_ASSIGN_RAW(source.baseAddr, CL(512));
    FIELD_ASSIGN_RAW(destination.baseAddr, CL(512));
    source.rowBytes = destination.rowBytes = CWC(16);
    source.bounds = destination.bounds = (Rect){CWC(0), CWC(0), CWC(8), CWC(16)};
    src_origin = (Point){CWC(2), CWC(4)};
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -3; dx <= 3; ++dx)
            for (mode = 0; mode < 8; ++mode) {
                unsigned char original[128], expected[128];
                dst_origin = (Point){CW(2 + dy), CW(4 + dx)};
                STARH(region)->rgnBBox = (Rect){CW(2 + dy), CW(4 + dx), CW(6 + dy), CW(12 + dx)};
                for (x = 0; x < sizeof original; ++x)
                    original[x] = memory[512 + x] = x * 37 + 13;
                memcpy(expected, original, sizeof expected);
                for (y = 0; y < 4; ++y)
                    for (x = 0; x < 8; ++x) {
                        int destination_offset = (2 + dy + y) * 16 + 4 + dx + x;
                        expected[destination_offset] = expected_pixel(original[(2 + y) * 16 + 4 + x],
                                                                      original[destination_offset], 0xA5, 0x37, mode);
                    }
                assert(win32_srcblt_8bpp(region, mode, &source, &destination, &src_origin, &dst_origin, 0xA5, 0x37));
                assert(!memcmp(expected, memory + 512, sizeof expected));
            }
    free(memory);
    puts("Windows indexed blits: eight modes, colors, origins, overlapping "
         "moves and guards passed");
    return 0;
}
