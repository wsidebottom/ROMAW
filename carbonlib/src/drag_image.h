/*+===================================================================
  File:      drag_image.h

  Summary:   Composites local drag images and restores their screen backdrop.

  Classes:   None.

  Functions: Private Drag Manager image handlers.

  Origin:    CarbonLib 1.4 DragLib image workers and Apple TN1043.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMLIB_CARBON_DRAG_IMAGE_H
#define ROMLIB_CARBON_DRAG_IMAGE_H

enum { drag_image_and_outline = 0x10, drag_bad_image_region = -1860, drag_bad_image = -1861 };

typedef struct drag_image_frame {
    Ptr storage;
    Size capacity;
    PixMap background;
    RgnHandle clip, saved_clip;
    GrafPtr screen;
    bool visible;
} drag_image_frame;

static const PixMap* drag_image_pixmap(uint32_t address, unsigned expected_depth)
{
    uint32_t size;
    const PixMap* pixels = cfp_handle_data(address, &size, 0);
    if (!pixels || size < sizeof *pixels)
        return NULL;
    if ((CW(pixels->rowBytes) & ROWBYTES_FLAG_BITS) != PIXMAP_DEFAULT_ROW_BYTES)
        return NULL;
    const unsigned depth = CW(pixels->pixelSize);
    const unsigned stride = CW(pixels->rowBytes) & ROWBYTES_VALUE_BITS;
    const int width = RECT_WIDTH(&pixels->bounds), height = RECT_HEIGHT(&pixels->bounds);
    if ((depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16 && depth != 32) ||
        (expected_depth && depth != expected_depth) || width <= 0 || height <= 0 || stride < (width * depth + 7) / 8)
        return NULL;
    if (depth <= 8) {
        const uint32_t table_handle = CL((uint32_t)(uintptr_t)PP_RAW(pixels->pmTable));
        const ColorTable* table = cfp_handle_data(table_handle, &size, 0);
        if (!table || size < offsetof(ColorTable, ctTable) ||
            (size_t)(uint16_t)CW(table->ctSize) + 1 > (size - offsetof(ColorTable, ctTable)) / sizeof(ColorSpec))
            return NULL;
    }
    return pixels;
}

static int drag_set_image(ROMlib_carbon_ppc_frame* call, carbon_drag* drag, bool with_alpha)
{
    const uint32_t image = call->words[1], mask = call->words[2];
    const PixMap* source = drag_image_pixmap(image, 0);
    if (!source)
        return drag_result(call, with_alpha ? paramErr : drag_bad_image);
    if (with_alpha) {
        const PixMap* alpha = drag_image_pixmap(mask, 32);
        if (!alpha || RECT_WIDTH(&alpha->bounds) != RECT_WIDTH(&source->bounds) ||
            RECT_HEIGHT(&alpha->bounds) != RECT_HEIGHT(&source->bounds))
            return drag_result(call, paramErr);
    }
    else if (mask && !drag_region_record(mask))
        return drag_result(call, drag_bad_image_region);

    drag->image = image;
    drag->image_mask = mask;
    drag->image_offset = (Point){CW((int16_t)(call->words[3] >> 16)), CW((int16_t)call->words[3])};
    drag->image_flags = call->words[4];
    drag->image_has_alpha = with_alpha;
    return drag_result(call, noErr);
}

static Ptr drag_image_base(PixMapHandle pixels, const PixMap* map)
{
    Ptr base = C_GetPixBaseAddr(pixels);
    const Size size = (CW(map->rowBytes) & ROWBYTES_VALUE_BITS) * RECT_HEIGHT(&map->bounds);
    return base && ROMlib_carbon_try_guest_memory(US_TO_SYN68K(base), size, 0) ? base : NULL;
}

static void drag_image_hide(drag_image_frame* overlay)
{
    if (!overlay->visible)
        return;
    overlay->visible = false;
    if (US_TO_SYN68K(overlay->screen) == cfp_read(0xd2c)) {
        const GrafPtr previous = thePort;
        C_SetPort(overlay->screen);
        C_GetClip(overlay->saved_clip);
        C_SetClip(PORT_VIS_REGION(overlay->screen));
        Rect restore = overlay->background.bounds;
        C_CopyBits((BitMap*)&overlay->background, PORT_BITS_FOR_COPY(overlay->screen), &restore, &restore, srcCopy,
                   overlay->clip);
        C_SetClip(overlay->saved_clip);
        C_SetPort(previous);
    }
    pixmap_free_copy(&overlay->background);
    PP_RAW(overlay->background.baseAddr) = 0;
}

static void drag_image_finish(drag_image_frame* overlay)
{
    drag_image_hide(overlay);
    if (overlay->storage)
        DisposPtr(overlay->storage);
    if (overlay->clip)
        C_DisposeRgn(overlay->clip);
    if (overlay->saved_clip)
        C_DisposeRgn(overlay->saved_clip);
    *overlay = (drag_image_frame){0};
}

static void drag_mix_image(Byte* destination, const Byte* background, int width, int height, unsigned opacity,
                           const Byte* mask, unsigned mask_stride)
{
    for (int y = 0; y < height; ++y) {
        const Byte* alpha = mask ? mask + y * mask_stride : NULL;
        for (int x = 0; x < width; ++x, destination += 4, background += 4) {
            for (unsigned component = 1; component <= 3; ++component) {
                /* The saved worker uses CopyDeepMask. Black keeps the image and white keeps the backdrop. */
                const unsigned weight = alpha ? (opacity * (255 - alpha[4 * x + component]) + 127) / 255 : opacity;
                destination[component] =
                    (destination[component] * weight + background[component] * (65535 - weight) + 32767) / 65535;
            }
        }
    }
}

static bool drag_render_image(const carbon_drag* drag, drag_image_frame* overlay, const PixMap* pixels,
                              const PixMap* alpha, const Byte* alpha_bytes)
{
    const uint32_t address = cfp_read(0xd2c);
    GrafPtr screen = address ? (GrafPtr)SYN68K_TO_US(address) : NULL;
    if (!screen || !CGrafPort_p(screen) || !CPORT_PIXMAP((CGrafPtr)screen) ||
        PIXMAP_PIXEL_SIZE(CPORT_PIXMAP((CGrafPtr)screen)) < 8)
        return false;

    const int dx = CW(drag->image_offset.h) + CW(drag->pinned.h) - CW(drag->origin.h);
    const int dy = CW(drag->image_offset.v) + CW(drag->pinned.v) - CW(drag->origin.v);
    const Rect bounds = PORT_BOUNDS(screen);
    const int left = MAX(CW(bounds.left), CW(pixels->bounds.left) + dx);
    const int top = MAX(CW(bounds.top), CW(pixels->bounds.top) + dy);
    const int right = MIN(CW(bounds.right), CW(pixels->bounds.right) + dx);
    const int bottom = MIN(CW(bounds.bottom), CW(pixels->bounds.bottom) + dy);
    if (right <= left || bottom <= top)
        return true;

    const unsigned stride = (right - left) * 4;
    if (stride > ROWBYTES_VALUE_BITS)
        return false;
    const Size size = stride * (bottom - top);
    if (overlay->capacity < size) {
        Ptr storage = NewPtr(2 * size);
        if (!storage)
            return false;
        if (overlay->storage)
            DisposPtr(overlay->storage);
        overlay->storage = storage;
        overlay->capacity = size;
    }
    if (!overlay->clip)
        overlay->clip = C_NewRgn();
    if (!overlay->saved_clip)
        overlay->saved_clip = C_NewRgn();
    if (!overlay->clip || !overlay->saved_clip)
        return false;

    Rect target = {CW(top), CW(left), CW(bottom), CW(right)}, source = target;
    C_OffsetRect(&source, -dx, -dy);
    if (!drag->image_has_alpha && drag->image_mask) {
        if (drag_copy_region(drag->image_mask, US_TO_SYN68K(overlay->clip)) != noErr)
            return false;
        C_OffsetRgn(overlay->clip, dx, dy);
    }
    else
        C_RectRgn(overlay->clip, &target);
    if (PORT_VIS_REGION(screen))
        C_SectRgn(overlay->clip, PORT_VIS_REGION(screen), overlay->clip);

    if (C_EmptyRgn(overlay->clip))
        return true;

    /* Save the original screen pixels in their native format for restoration.
       Use a separate RGB copy for blending on indexed or 16-bit displays. */
    overlay->screen = screen;
    Rect saved_bounds;
    pixmap_copy(STARH(CPORT_PIXMAP((CGrafPtr)screen)), &target, &overlay->background, &saved_bounds);
    if (!PP_RAW(overlay->background.baseAddr))
        return false;

    PixMap composite = {.rowBytes = CW(PIXMAP_DEFAULT_ROW_BYTES | stride),
                        .bounds = target,
                        .pixelType = CWC(RGBDirect),
                        .pixelSize = CWC(32),
                        .cmpCount = CWC(3),
                        .cmpSize = CWC(8)};
    FIELD_ASSIGN_RAW(composite.baseAddr, RM(overlay->storage + size));
    const Byte* backdrop = (const Byte*)PPR(overlay->background.baseAddr);
    if (pixmap_rgb_spec(&overlay->background) != &mac_32bpp_rgb_spec) {
        PixMap rgb = composite;
        FIELD_ASSIGN_RAW(rgb.baseAddr, RM(overlay->storage));
        convert_pixmap(&overlay->background, &rgb, &saved_bounds, NULL);
        backdrop = (const Byte*)overlay->storage;
    }

    const GrafPtr previous = thePort;
    C_SetPort(screen);
    /* The shared Window Manager port may retain an empty application clip.
       Drag feedback uses screen visibility and restores that clip afterward. */
    C_GetClip(overlay->saved_clip);
    C_SetClip(PORT_VIS_REGION(screen));
    BitMap* screen_bits = PORT_BITS_FOR_COPY(screen);
    C_CopyBits((BitMap*)pixels, (BitMap*)&composite, &source, &target, srcCopy, NULL);
    const unsigned alpha_stride = alpha ? CW(alpha->rowBytes) & ROWBYTES_VALUE_BITS : 0;
    if (alpha_bytes)
        alpha_bytes +=
            (CW(source.top) - CW(pixels->bounds.top)) * alpha_stride + (CW(source.left) - CW(pixels->bounds.left)) * 4;
    static const unsigned opacity[] = {0x6000, 0x8000, 0xc000, 0xffff};
    drag_mix_image((Byte*)overlay->storage + size, backdrop, right - left, bottom - top, opacity[drag->image_flags & 3],
                   alpha_bytes, alpha_stride);
    C_CopyBits((BitMap*)&composite, screen_bits, &target, &target, srcCopy, overlay->clip);
    C_SetClip(overlay->saved_clip);
    C_SetPort(previous);
    overlay->visible = true;
    return true;
}

static bool drag_image_show(const carbon_drag* drag, drag_image_frame* overlay)
{
    if (!drag->image)
        return false;
    const PixMap* pixels = drag_image_pixmap(drag->image, 0);
    if (!pixels)
        return false;
    PixMapHandle handle = (PixMapHandle)SYN68K_TO_US(drag->image);
    if (!C_LockPixels(handle))
        return false;
    PixMap image = *STARH(handle);
    Ptr base = drag_image_base(handle, &image);

    PixMapHandle mask = NULL;
    PixMap alpha;
    const Byte* alpha_bytes = NULL;
    if (base && drag->image_has_alpha && drag_image_pixmap(drag->image_mask, 32)) {
        PixMapHandle candidate = (PixMapHandle)SYN68K_TO_US(drag->image_mask);
        if (C_LockPixels(candidate)) {
            mask = candidate;
            alpha = *STARH(mask);
            if (RECT_WIDTH(&alpha.bounds) == RECT_WIDTH(&image.bounds) &&
                RECT_HEIGHT(&alpha.bounds) == RECT_HEIGHT(&image.bounds))
                alpha_bytes = (const Byte*)drag_image_base(mask, &alpha);
        }
    }

    bool drawn = false;
    if (base && (!drag->image_has_alpha || alpha_bytes)) {
        FIELD_ASSIGN_RAW(image.baseAddr, RM(base));
        drawn = drag_render_image(drag, overlay, &image, mask ? &alpha : NULL, alpha_bytes);
    }
    if (mask)
        C_UnlockPixels(mask);
    C_UnlockPixels(handle);
    return drawn;
}

#endif
