/*+===================================================================
  File:      getcpixel_test.c

  Summary:   Pixel reads must ignore drawing colors and respect packed pixel positions.

  Classes:   None.

  Functions: C_GetPixBaseAddr, _warning, main, pixmap_rgb_spec, syn68k_guest_to_host, syn68k_host_to_guest,
             validate_relative_bw_ctab

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Pixel reads must ignore drawing colors and respect packed pixel positions. */
#define NDEBUG 1
#include "rsys/common.h"
#include "QuickDraw.h"
#include "CQuickDraw.h"
#include "rsys/cquick.h"
static GrafPtr test_port;
#undef thePort
#undef theCPort
#define thePort test_port
#define theCPort ((CGrafPtr)test_port)
#include "../../quickdraw/qIMV.c"
#undef main
#undef NDEBUG
#include <assert.h>

#include "guest_test_memory.h"

uint64 ROMlib_offset;
uint32 syn68k_address_mask = 0xFFFFFFFFUL;
unsigned syn68k_host_alias_count = 1;
uint32 syn68k_host_alias_base = 0xe0000000, syn68k_host_alias_size = 0x10000000;
static CTabHandle test_table;
static unsigned char* test_pixels;
static uint32 decoded_pixel;
static int decoded_depth;
static unsigned warnings;

void _warning(int error, const char* type, const char* file, int line, const char* function, const char* format, ...)
{
    ++warnings;
}

void* syn68k_guest_to_host(uint32 address)
{
    return fixture_guest_to_host(address);
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    return fixture_host_to_guest(address);
}

Ptr C_GetPixBaseAddr(PixMapHandle map)
{
    return (Ptr)test_pixels;
}

CTabHandle validate_relative_bw_ctab(void)
{
    return test_table;
}

static void decode_pixel(const rgb_spec_t* spec, uint32 pixel, RGBColor* out)
{
    decoded_pixel = pixel;
    decoded_depth = spec->bpp;
    *out = (RGBColor){CWC(1), CWC(2), CWC(3)};
}

const rgb_spec_t* pixmap_rgb_spec(const PixMap* map)
{
    static rgb_spec_t spec;
    spec.bpp = CW(map->pixelSize);
    spec.pixel_to_rgbcolor = decode_pixel;
    return &spec;
}

int main(void)
{
    unsigned char* memory = calloc(1, 8192);
    static const unsigned char expected[4][8] = {{1, 0, 1, 0, 0, 1, 0, 1}, {2, 2, 1, 1}, {10, 5}, {165}};
    static const int depths[] = {1, 2, 4, 8};
    PixMapHandle map;
    unsigned color, depth, x, y, i;
    RGBColor result, sentinel = {CWC(123), CWC(456), CWC(789)};
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    test_port = (GrafPtr)(memory + 128);
    map = (PixMapHandle)(memory + 512);
    *(uint32*)map = CL(544);
    test_table = (CTabHandle)(memory + 640);
    *(uint32*)test_table = CL(672);
    CTAB_SIZE_X(test_table) = CWC(255);
    CTAB_FLAGS_X(test_table) = CTAB_GDEVICE_BIT_X;
    for (i = 0; i < 256; ++i)
        CTAB_TABLE(test_table)
    [i].rgb = (RGBColor){CW(i), CW(i + 256), CW(i + 512)};
    test_pixels = memory + 4096;
    test_pixels[0] = 0xA5;
    test_pixels[8] = 0x5A;
    for (color = 0; color < 2; ++color) {
        memset(test_port, 0, sizeof(CGrafPort));
        if (color) {
            ((CGrafPtr)test_port)->portVersion = CWC(0xC000);
            PP_ASSIGN(CPORT_PIXMAP_X(test_port), map);
            PP_ASSIGN(PIXMAP_TABLE_X(map), test_table);
            PIXMAP_SET_ROWBYTES_X(map, CWC(8));
            CPORT_RGB_FG_COLOR(test_port) = sentinel;
            CPORT_RGB_BK_COLOR(test_port) = sentinel;
        }
        else {
            PP_ASSIGN(PORT_BITS(test_port).baseAddr, (Ptr)test_pixels);
            PORT_BITS(test_port).rowBytes = CWC(8);
        }
        for (depth = 0; depth < (color ? NELEM(depths) : 1); ++depth) {
            int bpp = depths[depth], width = 8 / bpp;
            Rect bounds = {CWC(7), CWC(-3), CWC(9), CW(-3 + width)};
            if (color) {
                PIXMAP_PIXEL_SIZE_X(map) = CW(bpp);
                PIXMAP_BOUNDS(map) = bounds;
            }
            else
                PORT_BITS(test_port).bounds = bounds;
            for (y = 0; y < 2; ++y)
                for (x = 0; x < width; ++x) {
                    unsigned index = expected[depth][x];
                    if (y)
                        index ^= (1U << bpp) - 1;
                    result = sentinel;
                    C_GetCPixel(-3 + x, 7 + y, &result);
                    assert(!memcmp(&result, &CTAB_TABLE(test_table)[index].rgb, sizeof result));
                }
            result = sentinel;
            C_GetCPixel(-4, 7, &result);
            C_GetCPixel(-3 + width, 7, &result);
            C_GetCPixel(-3, 6, &result);
            C_GetCPixel(-3, 9, &result);
            assert(!memcmp(&result, &sentinel, sizeof result));
        }
    }
    /* Non-device palettes use explicit values, rather than entry positions. */
    CTAB_FLAGS_X(test_table) = 0;
    CTAB_SIZE_X(test_table) = CWC(3);
    CTAB_TABLE(test_table)[0].value = CWC(4);
    CTAB_TABLE(test_table)[1].value = CWC(1);
    CTAB_TABLE(test_table)[2].value = CWC(0);
    CTAB_TABLE(test_table)[3].value = CWC(2);
    test_pixels[0] = 2;
    C_GetCPixel(-3, 7, &result);
    assert(!memcmp(&result, &CTAB_TABLE(test_table)[3].rgb, sizeof result));
    test_pixels[0] = 7;
    C_GetCPixel(-3, 7, &result);
    assert(warnings == 1 && !result.red && !result.green && !result.blue);
    /* Direct pixels are passed to their decoder without alignment assumptions. */
    PIXMAP_BOUNDS(map) = (Rect){0, 0, CWC(1), CWC(2)};
    test_pixels = memory + 4097;
    memcpy(test_pixels, "\x12\x34\x56\x78\x9A\xBC\xDE\xF0", 8);
    PIXMAP_PIXEL_SIZE_X(map) = CWC(16);
    C_GetCPixel(1, 0, &result);
    assert(decoded_depth == 16 && decoded_pixel == 0x7856);
    PIXMAP_PIXEL_SIZE_X(map) = CWC(32);
    C_GetCPixel(1, 0, &result);
    assert(decoded_depth == 32 && decoded_pixel == 0xF0DEBC9A);
    assert(result.red == CWC(1) && result.green == CWC(2) && result.blue == CWC(3));
    C_GetCPixel(0, 0, NULL);
    test_port = NULL;
    C_GetCPixel(0, 0, &result);
    free(memory);
    puts("Pixel readback: packed depths, direct bytes, bounds and colors passed");
    return 0;
}
