/*+===================================================================
  File:      gworld.c

  Summary:   Native Carbon QDOffscreen readers over ROMAW's live GWorld state. CarbonLib 1.4 forwards these calls to
             QDOffscreen selectors 5, 13, 15, 18 and 23. Creation, locking, relocation and disposal retain the existing
             manager implementation and its canonical gw_info records.

  Classes:   None.

  Functions: ROMlib_carbon_get_gworld, ROMlib_carbon_gworld_device, ROMlib_carbon_gworld_pixmap,
             ROMlib_carbon_pix_base_addr, ROMlib_carbon_pixels_state, ROMlib_carbon_new_gworld_from_ptr,
             ROMlib_carbon_nqd_get_gworld, ROMlib_carbon_nqd_gworld_device, ROMlib_carbon_nqd_gworld_pixmap,
             ROMlib_carbon_nqd_pix_base_addr, ROMlib_carbon_nqd_pix_rowbytes, ROMlib_carbon_nqd_pixmap_32bit,
             ROMlib_carbon_nqd_pixels_state, ROMlib_carbon_nqd_ctab_changed, ROMlib_carbon_nqd_pixpat_changed,
             ROMlib_carbon_nqd_clear_error, ROMlib_carbon_nqd_is_done, ROMlib_carbon_nqd_offscreen_version

  Origin:    Existing repository source in packages/carbonlib/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Native Carbon QDOffscreen readers over ROMAW's live GWorld state.
   CarbonLib 1.4 forwards these calls to QDOffscreen selectors 5, 13, 15,
   18 and 23. Creation, locking, relocation and disposal retain the existing
   manager implementation and its canonical gw_info records. */
#include "internal.h"
#include "rsys/cquick.h"
#include "rsys/gworld.h"

int ROMlib_carbon_new_gworld_from_ptr(ROMlib_carbon_ppc_frame* call)
{
    const int32_t row_bytes = call->words[7];
    const uint32_t address = call->words[6];
    const GWorldFlags flags = call->words[5];
    const uint32_t depth = call->words[1];
    HIDDEN_GWorldPtr* output =
        call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], sizeof *output, 1) : NULL;
    call->words[0] = paramErr;
    if (!output || !address || row_bytes <= 0 || row_bytes >= 0x3fff)
        return 1;
    HIDDEN_RAW_SET(*output, 0);

    INTEGER bits_per_pixel;
    switch (depth) {
    case 1:
    case 2:
    case 4:
    case 8:
    case 16:
    case 32:
        bits_per_pixel = depth;
        break;
    case 33:
    case 34:
    case 36:
    case 40:
        bits_per_pixel = depth - 32;
        break;
    default:
        call->words[0] = cDepthErr;
        return 1;
    }
    const Rect* input = call->words[2] ? ROMlib_carbon_try_guest_memory(call->words[2], sizeof *input, 0) : NULL;
    if (!input)
        return 1;
    Rect bounds = *input;
    const int width = RECT_WIDTH(&bounds), height = RECT_HEIGHT(&bounds);
    GDHandle device = NULL;
    if (flags & noNewDevice) {
        const uint32_t master = call->words[4];
        device = master ? ROMlib_carbon_try_guest_memory(master, sizeof *device, 0) : NULL;
        const uint32_t device_body = device ? CL((uint32_t)(uintptr_t)HIDDEN_RAW(*device)) : 0;
        const GDevice* gd = device_body ? ROMlib_carbon_try_guest_memory(device_body, sizeof *gd, 0) : NULL;
        const uint32_t pixmap = gd ? CL((uint32_t)(uintptr_t)PP_RAW(gd->gdPMap)) : 0;
        const PixMapHandle pixels = pixmap ? ROMlib_carbon_try_guest_memory(pixmap, sizeof *pixels, 0) : NULL;
        const uint32_t pixmap_body = pixels ? CL((uint32_t)(uintptr_t)HIDDEN_RAW(*pixels)) : 0;
        const PixMap* map = pixmap_body ? ROMlib_carbon_try_guest_memory(pixmap_body, sizeof *map, 0) : NULL;
        if (!map || CW(map->pixelSize) != bits_per_pixel)
            return 1;
    }
    if (width <= 0 || height <= 0 || row_bytes < (width * bits_per_pixel + 7) / 8)
        return 1;
    Ptr base = ROMlib_carbon_try_guest_memory(address, (size_t)row_bytes * height, 1);
    CTabHandle colors = call->words[3] ? ROMlib_carbon_try_guest_memory(call->words[3], sizeof *colors, 0) : NULL;
    if (!base || (call->words[3] && !colors))
        return 1;
    if (colors && bits_per_pixel <= 8) {
        const uint32_t body = CL((uint32_t)(uintptr_t)HIDDEN_RAW(*colors));
        const ColorTable* table = body ? ROMlib_carbon_try_guest_memory(body, sizeof *table, 0) : NULL;
        if (!table || CW(table->ctSize) < 0 || CW(table->ctSize) > 255 ||
            !ROMlib_carbon_try_guest_memory(body, CTAB_STORAGE_FOR_SIZE(CW(table->ctSize)), 0))
            return 1;
    }
    call->words[0] =
        ROMlib_new_gworld_from_ptr(output, bits_per_pixel, &bounds, colors, device, flags, base, row_bytes);
    return 1;
}

void ROMlib_carbon_get_gworld(HIDDEN_CGrafPtr* port, HIDDEN_GDHandle* device)
{
    if (port)
        HIDDEN_RAW_SET(*port, theCPortX);
    if (device)
        HIDDEN_RAW_SET(*device, TheGDevice);
}

GDHandle ROMlib_carbon_gworld_device(GWorldPtr world)
{
    gw_info_t* info = world ? lookup_gw_info_by_gw(world) : NULL;
    return info ? info->gw_gd : NULL;
}

PixMapHandle ROMlib_carbon_gworld_pixmap(GWorldPtr world)
{
    if (!world)
        return NULL;
    gw_info_t* info = lookup_gw_info_by_gw(world);
    return info ? info->gw_pixmap : CGrafPort_p(world) ? CPORT_PIXMAP(world) : NULL;
}

Ptr ROMlib_carbon_pix_base_addr(PixMapHandle pixels)
{
    if (!pixels || !STARH(pixels))
        return NULL;
    if (PIXMAP_VERSION_X(pixels) == CWC(GWORLD_EXTERNAL_PIXMAP_VERSION))
        return PIXMAP_BASEADDR(pixels);
    gw_info_t* info = lookup_gw_info_by_gw_pixmap(pixels);
    BOOLEAN indirect;
    if (info)
        indirect = !(info->flags & pixelsLocked);
    else {
        info = lookup_gw_info_by_gw_gd_pixmap(pixels);
        indirect = info && info->gw_gd != MR(MainDevice);
    }
    Ptr base = PIXMAP_BASEADDR(pixels);
    return indirect && base ? STARH((Handle)base) : base;
}

GWorldFlags ROMlib_carbon_pixels_state(PixMapHandle pixels)
{
    gw_info_t* info = pixels ? lookup_gw_info_by_gw_pixmap(pixels) : NULL;
    return info ? info->flags & (pixelsPurgeable | pixelsLocked | keepLocal) : 0;
}

static PixMapPtr nqd_pixmap(uint32_t address, PixMapHandle* handle)
{
    *handle = address ? ROMlib_carbon_try_guest_memory(address, sizeof **handle, 0) : NULL;
    const uint32_t body = *handle ? CL((uint32_t)(uintptr_t)HIDDEN_RAW(**handle)) : 0;
    PixMapPtr pixmap = body ? ROMlib_carbon_try_guest_memory(body, sizeof *pixmap, 0) : NULL;
    ROMlib_carbon_set_qd_error(pixmap ? noErr : paramErr);
    return pixmap;
}

int ROMlib_carbon_nqd_get_gworld(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t port_address = call->words[0], device_address = call->words[1];
    HIDDEN_CGrafPtr* port = port_address ? ROMlib_carbon_try_guest_memory(port_address, sizeof *port, 1) : NULL;
    HIDDEN_GDHandle* device = device_address ? ROMlib_carbon_try_guest_memory(device_address, sizeof *device, 1) : NULL;
    ROMlib_carbon_set_qd_error(noErr);
    if ((port_address && !port) || (device_address && !device))
        return 0;
    ROMlib_carbon_get_gworld(port, device);
    return 1;
}

int ROMlib_carbon_nqd_gworld_device(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t address = call->words[0];
    GWorldPtr world = address ? ROMlib_carbon_try_guest_memory(address, sizeof *world, 0) : NULL;
    ROMlib_carbon_set_qd_error(world ? noErr : paramErr);
    GDHandle device = world ? ROMlib_carbon_gworld_device(world) : NULL;
    if (world && !device)
        device = MR(TheGDevice);
    call->words[0] = US_TO_SYN68K_CHECK0(device);
    return 1;
}

int ROMlib_carbon_nqd_gworld_pixmap(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t address = call->words[0];
    GWorldPtr world = address ? ROMlib_carbon_try_guest_memory(address, sizeof *world, 0) : NULL;
    ROMlib_carbon_set_qd_error(world ? noErr : paramErr);
    PixMapHandle handle = ROMlib_carbon_gworld_pixmap(world);
    call->words[0] = US_TO_SYN68K_CHECK0(handle);
    return 1;
}

int ROMlib_carbon_nqd_pix_base_addr(ROMlib_carbon_ppc_frame* call)
{
    PixMapHandle handle;
    const PixMapPtr pixmap = nqd_pixmap(call->words[0], &handle);
    call->words[0] = pixmap ? US_TO_SYN68K_CHECK0(ROMlib_carbon_pix_base_addr(handle)) : 0;
    return 1;
}

int ROMlib_carbon_nqd_pix_rowbytes(ROMlib_carbon_ppc_frame* call)
{
    PixMapHandle handle;
    const PixMapPtr pixmap = nqd_pixmap(call->words[0], &handle);
    uint32_t rowbytes = pixmap ? (uint16_t)CW(pixmap->rowBytes) & 0x7fff : 0;
    /* NQD uses the reserved slot as a handle to its extension when rowBytes
       contains the 0x3f3f sentinel. Check the extension length and signature. */
    if (rowbytes == 0x3f3f && pixmap->pmReserved && !(CL(pixmap->pmReserved) & 3)) {
        const uint32_t* master = ROMlib_carbon_try_guest_memory(CL(pixmap->pmReserved), sizeof *master, 0);
        uint32_t extension[10];
        const uint32_t body = master ? CL(*master) : 0;
        const void* record = body && !(body & 1) ? ROMlib_carbon_try_guest_memory(body, sizeof extension, 0) : NULL;
        if (record) {
            memcpy(extension, record, sizeof extension);
            if ((int32_t)CL(extension[0]) >= (int32_t)sizeof extension && CL(extension[8]) == TICK("pmEx"))
                rowbytes = CL(extension[7]);
        }
    }
    call->words[0] = rowbytes;
    return 1;
}

int ROMlib_carbon_nqd_pixmap_32bit(ROMlib_carbon_ppc_frame* call)
{
    PixMapHandle handle;
    const PixMapPtr pixmap = nqd_pixmap(call->words[0], &handle);
    const uint16_t version = pixmap ? CW(pixmap->pmVersion) : UINT16_MAX;
    call->words[0] = version == GWORLD_EXTERNAL_PIXMAP_VERSION;
    if (version == 0) {
        for (GDHandle device = MR(DeviceList); device; device = GD_NEXT_GD(device)) {
            const PixMapHandle device_pixmap = GD_PMAP(device);
            if (device_pixmap && STARH(device_pixmap) &&
                PP_RAW(PIXMAP_BASEADDR_X(device_pixmap)) == PP_RAW(pixmap->baseAddr)) {
                call->words[0] = (GD_FLAGS(device) & 0x0100) != 0;
                break;
            }
        }
    }
    return 1;
}

int ROMlib_carbon_nqd_pixels_state(ROMlib_carbon_ppc_frame* call)
{
    PixMapHandle handle;
    const PixMapPtr pixmap = nqd_pixmap(call->words[0], &handle);
    call->words[0] = pixmap ? ROMlib_carbon_pixels_state(handle) : 0;
    return 1;
}

static void* nqd_handle_record(uint32_t address, size_t size)
{
    Handle handle = address ? ROMlib_carbon_try_guest_memory(address, sizeof *handle, 0) : NULL;
    const uint32_t body = handle ? CL((uint32_t)(uintptr_t)HIDDEN_RAW(*handle)) : 0;
    void* result = body ? ROMlib_carbon_try_guest_memory(body, size, 1) : NULL;
    ROMlib_carbon_set_qd_error(result ? noErr : paramErr);
    return result;
}

int ROMlib_carbon_nqd_ctab_changed(ROMlib_carbon_ppc_frame* call)
{
    ColorTable* table = nqd_handle_record(call->words[0], offsetof(ColorTable, ctTable));
    if (table)
        table->ctSeed = CL(GetCTSeed());
    return 1;
}

int ROMlib_carbon_nqd_pixpat_changed(ROMlib_carbon_ppc_frame* call)
{
    PixPat* pattern = nqd_handle_record(call->words[0], sizeof *pattern);
    if (pattern)
        pattern->patXValid = CWC(-1);
    return 1;
}

int ROMlib_carbon_nqd_clear_error(ROMlib_carbon_ppc_frame* call)
{
    // NQD's GDeviceChanged and PortChanged only reset the shared error.
    (void)call;
    ROMlib_carbon_set_qd_error(noErr);
    return 1;
}

int ROMlib_carbon_nqd_is_done(ROMlib_carbon_ppc_frame* call)
{
    // ROMAW's QuickDraw operations complete synchronously.
    ROMlib_carbon_set_qd_error(noErr);
    call->words[0] = TRUE;
    return 1;
}

int ROMlib_carbon_nqd_offscreen_version(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_set_qd_error(noErr);
    call->words[0] = 0x150;
    return 1;
}
