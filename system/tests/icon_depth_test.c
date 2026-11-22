/*+===================================================================
  File:      icon_depth_test.c

  Summary:   Old-style screen ports must retain colour without colouring offscreen bits.

  Classes:   None.

  Functions: main, syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Old-style screen ports must retain colour without colouring offscreen bits. */
#define NDEBUG 1
#include "../../ui/icon.c"
#undef main
#undef NDEBUG
#include <assert.h>

#if __SIZEOF_POINTER__ == 8
uint64 ROMlib_offset;
#else
uint32 ROMlib_offset;
#endif
uint32 syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count;
uint32 syn68k_host_alias_base, syn68k_host_alias_size;

void* syn68k_guest_to_host(uint32 address)
{
    return (void*)(uintptr_t)(ROMlib_offset + address);
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    return address - ROMlib_offset;
}

int main(void)
{
    unsigned char* memory = calloc(1, 65536);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    GDHandle device = (GDHandle)(memory + 0x4000);
    PP_ASSIGN(*device, (GDPtr)(memory + 0x4100));
    PixMapHandle screen = (PixMapHandle)(memory + 0x5000);
    PP_ASSIGN(*screen, (PixMapPtr)(memory + 0x5100));
    PP_ASSIGN(MainDevice_H, device);
    PP_ASSIGN(GD_PMAP_X(device), screen);
    PP_ASSIGN(PIXMAP_BASEADDR_X(screen), (Ptr)(memory + 0x6000));
    GrafPort mono = {0};
    PP_ASSIGN(PORT_BASEADDR_X(&mono), (Ptr)(memory + 0x6000));
    const int depths[] = {1, 2, 4, 8, 16, 32};
    for (unsigned i = 0; i < sizeof depths / sizeof depths[0]; ++i) {
        PIXMAP_PIXEL_SIZE_X(screen) = CW(depths[i]);
        assert(icon_port_depth(&mono) == depths[i]);
    }
    PP_ASSIGN(PORT_BASEADDR_X(&mono), (Ptr)(memory + 0x7000));
    assert(icon_port_depth(&mono) == 1);
    CGrafPort color = {0};
    color.portVersion = CWC(0xc000);
    PP_ASSIGN(color.portPixMap, screen);
    assert(icon_port_depth((GrafPtr)&color) == 32);
    HIDDEN_RAW_SET(MainDevice_H, 0);
    assert(icon_port_depth(&mono) == 1);
    free(memory);
    puts("Icon depth: colour screen ports, colour pixmaps and monochrome offscreen bits passed");
    return 0;
}
