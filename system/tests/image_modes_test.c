/*+===================================================================
  File:      image_modes_test.c

  Summary:   Exercise real source selection and dynamic palettes using guest handles.

  Classes:   None.

  Functions: CToPascalCall, C_GetCTSeed, main, syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Exercise real source selection and dynamic palettes using guest handles. */
#define NDEBUG 1
#include "rsys/common.h"
#include "QuickDraw.h"
#include "CQuickDraw.h"
static GDHandle test_device;
#undef TheGDevice
#define TheGDevice test_device
#include "../../quickdraw/image.c"
#undef main
#undef NDEBUG
#include <assert.h>

typeof(ROMlib_offset) ROMlib_offset;
uint32 syn68k_address_mask = 0xFFFFFFFFUL;
unsigned syn68k_host_alias_count;
uint32 syn68k_host_alias_base, syn68k_host_alias_size;
void* tooltraptable[0x400];
toolstuff_t toolstuff[0x400];

intptr_t CToPascalCall(void* proc, unsigned long magic, ...)
{
    abort();
}

void* syn68k_guest_to_host(uint32 address)
{
    abort();
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    abort();
}

static int seed;

LONGINT
C_GetCTSeed(void)
{
    return ++seed;
}

int main(void)
{
    unsigned char* memory = calloc(1, 65536);
    pixel_image_t image = {0};
    CTabHandle palettes[3];
    PixMapHandle screen;
    RGBColor colors[2] = {{CWC(0x1234), CWC(0x5678), CWC(0x9ABC)}, {CWC(0xFFFF), 0, CWC(0xFFFF)}};
    int i, bpp, before;

    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    test_device = (GDHandle)RM(memory + 0x1000);
    *(uint32*)(memory + 0x1000) = CL(0x1100);
    screen = (PixMapHandle)(memory + 0x2000);
    *(uint32*)screen = CL(0x2100);
    PP_ASSIGN(GD_PMAP_X(MR(test_device)), screen);
    for (bpp = 1; bpp <= 32; bpp *= 2) {
        PIXMAP_PIXEL_SIZE_X(screen) = CW(bpp);
        assert(image_visual(0) == 0);
        assert(image_visual(1) == (bpp <= 4 ? 1 : 2));
    }
    for (i = 0; i < 3; ++i) {
        unsigned base = 0x3000 + i * 0x1000;
        image.bits[i] = (PixMapHandle)(memory + base);
        *(uint32*)image.bits[i] = CL(base + 0x100);
        PIXMAP_PIXEL_SIZE_X(image.bits[i]) = CW(i == 0 ? 1 : i == 1 ? 4 : 8);
        palettes[i] = (CTabHandle)(memory + base + 0x200);
        *(uint32*)palettes[i] = CL(base + 0x300);
        PP_ASSIGN(PIXMAP_TABLE_X(image.bits[i]), palettes[i]);
        CTAB_SIZE_X(palettes[i]) = CW(i == 0 ? 1 : i == 1 ? 15 : 255);
    }
    image_update_ctab(&image, colors, 1);
    assert(seed == 2);
    assert(!memcmp(&CTAB_TABLE(palettes[1])[0].rgb, colors, sizeof(RGBColor)));
    assert(!memcmp(&CTAB_TABLE(palettes[2])[1].rgb, &colors[1], sizeof(RGBColor)));
    assert(CTAB_TABLE(palettes[0])[0].rgb.red == 0);
    assert(CTAB_TABLE(palettes[1])[2].rgb.red == 0);
    before = seed;
    image_update_ctab(&image, colors, 1);
    assert(seed == before); /* unchanged colors preserve conversion caches */
    colors[0].red = CWC(0x4321);
    image_update_ctab(&image, colors, 0);
    assert(seed == before + 2);
    assert(CTAB_TABLE(palettes[1])[0].rgb.red == CWC(0x4321));
    assert(CTAB_TABLE(palettes[2])[0].rgb.red == CWC(0x4321));
    image_update_palette(&image, 0, colors, 0);
    assert(CTAB_TABLE(palettes[0])[0].rgb.red == CWC(0x4321));
    /* A missing 4-bit representation can fall back to monochrome without
     replacing black/white with the control's shadow/highlight palette. */
    PIXMAP_PIXEL_SIZE_X(image.bits[1]) = CWC(1);
    before = CTAB_SEED_X(palettes[1]);
    colors[0].red = CWC(0x1111);
    image_update_ctab(&image, colors, 0);
    assert(CTAB_SEED_X(palettes[1]) == before);
    assert(CTAB_TABLE(palettes[1])[0].rgb.red == CWC(0x4321));
    assert(CTAB_TABLE(palettes[2])[0].rgb.red == CWC(0x1111));
    free(memory);
    puts("Icon source selection and synchronized color palettes passed");
    return 0;
}
