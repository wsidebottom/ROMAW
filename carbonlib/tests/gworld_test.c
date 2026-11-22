/*+===================================================================
  File:      gworld_test.c

  Summary:   Exercise native GWorld readers against mutable canonical manager records.

  Classes:   None.

  Functions: ROMlib_carbon_gworld_tests, lookup_gw_info_by_gw, lookup_gw_info_by_gw_gd_pixmap,
             lookup_gw_info_by_gw_pixmap, ROMlib_new_gworld_from_ptr

  Origin:    Existing repository source in packages/carbonlib/tests; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Exercise native GWorld readers against mutable canonical manager records. */
#include "../src/internal.h"
#include "rsys/cquick.h"
#include "rsys/gworld.h"
#undef NDEBUG
#include <assert.h>

static gw_info_t info;
static BOOLEAN registered;

QDErr ROMlib_new_gworld_from_ptr(HIDDEN_GWorldPtr* output, INTEGER depth, Rect* bounds, CTabHandle colors,
                                 GDHandle device, GWorldFlags flags, Ptr base, INTEGER row_bytes)
{
    abort();
}

gw_info_t* lookup_gw_info_by_gw(GWorldPtr world)
{
    return registered && info.gw == world ? &info : NULL;
}

gw_info_t* lookup_gw_info_by_gw_pixmap(PixMapHandle pixels)
{
    return registered && info.gw_pixmap == pixels ? &info : NULL;
}

gw_info_t* lookup_gw_info_by_gw_gd_pixmap(PixMapHandle pixels)
{
    return registered && info.gw_gd_pixmap == pixels ? &info : NULL;
}

void ROMlib_carbon_gworld_tests(void)
{
    static Byte lowmem[0x10000];
    uint64 saved_offset = ROMlib_offset;
    CGrafPtr saved_port = theCPortX;
    ROMlib_offset = (uintptr_t)lowmem;
    CGrafPort world = {0}, plain = {0};
    PixMap pixels = {0}, device_pixels = {0};
    HIDDEN_PixMapPtr master, device_master;
    HIDDEN_Ptr buffer_master;
    HIDDEN_GDevicePtr gd_master;
    Byte first[16], second[16];
    HIDDEN_RAW_SET(master, RM(&pixels));
    HIDDEN_RAW_SET(device_master, RM(&device_pixels));
    HIDDEN_RAW_SET(buffer_master, RM(&first[0]));
    HIDDEN_RAW_SET(gd_master, RM(&world));
    world.portVersion = plain.portVersion = CWC(0xc000);
    HIDDEN_RAW_SET(plain.portPixMap, RM(&device_master));
    PixMapHandle handle = (PixMapHandle)&master;
    info = (gw_info_t){
        .gw = &world, .gw_pixmap = handle, .gw_gd = (GDHandle)&gd_master, .gw_gd_pixmap = (PixMapHandle)&device_master};
    registered = TRUE;
    assert(ROMlib_carbon_gworld_device(&world) == info.gw_gd);
    assert(ROMlib_carbon_gworld_pixmap(&world) == handle);
    assert(ROMlib_carbon_gworld_pixmap(&plain) == (PixMapHandle)&device_master);
    assert(!ROMlib_carbon_gworld_device(&plain));
    SET_THEPORT_RAW(RM(&world));
    HIDDEN_RAW_SET(TheGDevice_H, RM(info.gw_gd));
    HIDDEN_CGrafPtr out_port;
    HIDDEN_GDHandle out_device;
    ROMlib_carbon_get_gworld(&out_port, &out_device);
    assert(HIDDEN_NATIVE(out_port) == &world && HIDDEN_NATIVE(out_device) == info.gw_gd);
    SET_THEPORT_RAW(RM(&plain));
    ROMlib_carbon_get_gworld(&out_port, NULL);
    assert(HIDDEN_NATIVE(out_port) == &plain);
    ROMlib_carbon_get_gworld(NULL, &out_device);
    ROMlib_carbon_get_gworld(NULL, NULL);
    FIELD_ASSIGN_RAW(pixels.baseAddr, RM(&buffer_master));
    assert(ROMlib_carbon_pix_base_addr(handle) == (Ptr)first);
    HIDDEN_RAW_SET(buffer_master, RM(&second[0]));
    assert(ROMlib_carbon_pix_base_addr(handle) == (Ptr)second);
    HIDDEN_RAW_SET(buffer_master, 0);
    assert(!ROMlib_carbon_pix_base_addr(handle));
    info.flags = pixelsLocked;
    FIELD_ASSIGN_RAW(pixels.baseAddr, RM(&first[0]));
    assert(ROMlib_carbon_pix_base_addr(handle) == (Ptr)first);
    FIELD_ASSIGN_RAW(device_pixels.baseAddr, RM(&buffer_master));
    HIDDEN_RAW_SET(buffer_master, RM(&second[0]));
    assert(ROMlib_carbon_pix_base_addr((PixMapHandle)&device_master) == (Ptr)second);
    HIDDEN_RAW_SET(MainDevice_H, RM(info.gw_gd));
    /* Screen storage is already a pointer; never dereference it as a handle. */
    assert(ROMlib_carbon_pix_base_addr((PixMapHandle)&device_master) == (Ptr)&buffer_master);
    for (unsigned flags = 0; flags < 256; ++flags) {
        info.flags = (flags << 24) | flags;
        assert(ROMlib_carbon_pixels_state(handle) == (info.flags & (pixelsPurgeable | pixelsLocked | keepLocal)));
    }
    registered = FALSE;
    assert(!ROMlib_carbon_pixels_state(handle));
    assert(ROMlib_carbon_pix_base_addr(handle) == (Ptr)first);
    assert(!ROMlib_carbon_gworld_device(&world));
    HIDDEN_RAW_SET(master, 0);
    assert(!ROMlib_carbon_pix_base_addr(handle));
    assert(!ROMlib_carbon_gworld_pixmap(NULL) && !ROMlib_carbon_gworld_device(NULL));
    assert(!ROMlib_carbon_pix_base_addr(NULL) && !ROMlib_carbon_pixels_state(NULL));
    SET_THEPORT_RAW(saved_port);
    ROMlib_offset = saved_offset;
}
