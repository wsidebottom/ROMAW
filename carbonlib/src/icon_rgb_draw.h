/*+===================================================================
  File:      icon_rgb_draw.h

  Summary:   Draw classic RGB icons and map their alpha masks in QuickDraw.

  Classes:   None.

  Functions: Private helpers for the IconRef drawing and hit-test adapters.

  Origin:    Universal Interfaces 3.4.2 Icons.h and Executor QuickDraw.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_ICON_RGB_DRAW_H
#define ROMAW_CARBON_ICON_RGB_DRAW_H

#include "CQuickDraw.h"
#include "rsys/region.h"

typedef struct {
    unsigned width;
    const Byte* rgb;
    uint32 length;
    const Byte* mask;
    boolean_t packed_mask;
} IconRGBImage;

static OSErr icon_rgb_find_image(IconFamilyView view, const Rect* bounds, IconRGBImage* image)
{
    static const struct {
        uint32 type, mask, monochrome;
        unsigned width;
    } formats[] = {
        {TICK("is32"), TICK("s8mk"), small1BitMask, 16},
        {TICK("il32"), TICK("l8mk"), large1BitMask, 32},
        {TICK("ih32"), TICK("h8mk"), TICK("ich#"), 48},
        {TICK("it32"), TICK("t8mk"), 0, 128},
    };

    const int target = MAX(RECT_WIDTH(bounds), RECT_HEIGHT(bounds));
    int selected = -1;
    uint32 offset = 0;
    for (unsigned i = 0; i < NELEM(formats); ++i) {
        uint32 element = icon_family_element(view.bytes, view.size, formats[i].type);
        if (element == view.size || icon_read_word(view.bytes + element + 4) == icon_family_header_size)
            continue;
        selected = i;
        offset = element;
        if ((int)formats[i].width >= target)
            break;
    }
    if (selected < 0)
        return no_such_icon;

    const unsigned width = formats[selected].width;
    const unsigned count = width * width;
    *image = (IconRGBImage){width, view.bytes + offset + icon_family_header_size,
                            icon_read_word(view.bytes + offset + 4) - icon_family_header_size, NULL, FALSE};
    uint32 mask = icon_family_element(view.bytes, view.size, formats[selected].mask);
    if (mask < view.size) {
        if (icon_read_word(view.bytes + mask + 4) != count + icon_family_header_size)
            return paramErr;
        image->mask = view.bytes + mask + icon_family_header_size;
    }
    else if (formats[selected].monochrome) {
        mask = icon_family_element(view.bytes, view.size, formats[selected].monochrome);
        if (mask < view.size) {
            if (icon_read_word(view.bytes + mask + 4) != count / 4 + icon_family_header_size)
                return paramErr;
            image->mask = view.bytes + mask + icon_family_header_size + count / 8;
            image->packed_mask = TRUE;
        }
    }
    return noErr;
}

static unsigned icon_rgb_alpha(const IconRGBImage* image, unsigned pixel)
{
    if (!image->mask)
        return 255;
    if (image->packed_mask)
        return image->mask[pixel / 8] & (0x80 >> (pixel & 7)) ? 255 : 0;
    return image->mask[pixel];
}

static OSErr icon_rgb_map_mask(const IconRGBImage* image, int16 alignment, Rect* destination, RgnHandle region)
{
    const int width = image->width;
    Rect source = {0, 0, CW(width), CW(width)};
    if (!image->mask)
        C_RectRgn(region, &source);
    else {
        unsigned count = width * width;
        Ptr bits = NewPtrClear(count / 8);
        if (!bits)
            return memFullErr;
        for (unsigned i = 0; i < count; ++i)
            if (icon_rgb_alpha(image, i))
                ((Byte*)bits)[i / 8] |= 0x80 >> (i & 7);
        BitMap mask = {0};
        FIELD_ASSIGN_RAW(mask.baseAddr, RM(bits));
        mask.rowBytes = CW(width / 8);
        mask.bounds = source;
        OSErr error = C_BitMapToRegion(region, &mask);
        DisposPtr(bits);
        if (error)
            return error;
    }
    if (!C_EmptyRgn(region) && alignment) {
        Rect visible = RGN_BBOX(region);
        C_MapRect(&visible, &source, destination);
        int dx = 0, dy = 0;
        switch (alignment & 12) {
        case atHorizontalCenter:
            dx = (CW(destination->left) + CW(destination->right) - CW(visible.left) - CW(visible.right)) / 2;
            break;
        case atLeft:
            dx = CW(destination->left) - CW(visible.left);
            break;
        case atRight:
            dx = CW(destination->right) - CW(visible.right);
            break;
        }
        switch (alignment & 3) {
        case atVerticalCenter:
            dy = (CW(destination->top) + CW(destination->bottom) - CW(visible.top) - CW(visible.bottom)) / 2;
            break;
        case atTop:
            dy = CW(destination->top) - CW(visible.top);
            break;
        case atBottom:
            dy = CW(destination->bottom) - CW(visible.bottom);
            break;
        }
        C_OffsetRect(destination, dx, dy);
    }
    C_MapRgn(region, &source, destination);
    return noErr;
}

static Byte icon_rgb_blend(Byte foreground, uint16 background, unsigned alpha)
{
    return (foreground * 257u * alpha + background * (255u - alpha) + 32767) / 65535u;
}

static OSErr icon_rgb_draw_image(const IconRGBImage* image, const Byte* rgb, const Rect* rectangle, RgnHandle region)
{
    GrafPtr port = thePort;
    if (!port)
        return paramErr;
    BitMap* target = PORT_BITS_FOR_COPY(port);
    Rect clipped;
    if (!C_SectRect(rectangle, &PORT_BOUNDS(port), &clipped) || !C_SectRect(&clipped, &RGN_BBOX(region), &clipped))
        return noErr;
    if (PORT_CLIP_REGION(port) && !C_SectRect(&clipped, &RGN_BBOX(PORT_CLIP_REGION(port)), &clipped))
        return noErr;
    if (PORT_VIS_REGION(port) && !C_SectRect(&clipped, &RGN_BBOX(PORT_VIS_REGION(port)), &clipped))
        return noErr;

    enum { tile_width = 512, tile_height = 32 };

    const unsigned width = image->width;
    Ptr tile = NewPtr(MIN(RECT_WIDTH(&clipped), tile_width) * MIN(RECT_HEIGHT(&clipped), tile_height) * 4);
    if (!tile)
        return memFullErr;
    PixMap pixels = {0};
    FIELD_ASSIGN_RAW(pixels.baseAddr, RM(tile));
    pixels.pixelType = CWC(RGBDirect);
    pixels.pixelSize = CWC(32);
    pixels.cmpCount = CWC(3);
    pixels.cmpSize = CWC(8);
    const int top = CW(clipped.top), left = CW(clipped.left), bottom = CW(clipped.bottom), right = CW(clipped.right);
    const int origin_x = CW(rectangle->left), origin_y = CW(rectangle->top);
    const int destination_width = RECT_WIDTH(rectangle), destination_height = RECT_HEIGHT(rectangle);
    for (int y = top; y < bottom; y += tile_height) {
        int yend = MIN(y + tile_height, bottom);
        for (int x = left; x < right; x += tile_width) {
            int xend = MIN(x + tile_width, right);
            Byte* output = (Byte*)tile;
            for (int row = y; row < yend; ++row) {
                unsigned source_row = (row - origin_y) * width / destination_height;
                for (int column = x; column < xend; ++column, output += 4) {
                    unsigned source_column = (column - origin_x) * width / destination_width;
                    unsigned index = source_row * width + source_column;
                    unsigned alpha = icon_rgb_alpha(image, index);
                    const Byte* color = rgb + 4 * index;
                    if (alpha == 255)
                        memcpy(output, color, 4);
                    else {
                        RGBColor background = {0};
                        C_GetCPixel(column, row, &background);
                        output[0] = 0;
                        output[1] = icon_rgb_blend(color[1], CW(background.red), alpha);
                        output[2] = icon_rgb_blend(color[2], CW(background.green), alpha);
                        output[3] = icon_rgb_blend(color[3], CW(background.blue), alpha);
                    }
                }
            }
            pixels.rowBytes = CW(PIXMAP_DEFAULT_ROW_BYTES | (xend - x) * 4);
            Rect tile_bounds = {CW(y), CW(x), CW(yend), CW(xend)};
            pixels.bounds = tile_bounds;
            C_CopyBits((BitMap*)&pixels, target, &tile_bounds, &tile_bounds, srcCopy, region);
        }
    }
    DisposPtr(tile);
    return noErr;
}

static int32 icon_rgb_operation(IconOperation operation, const IconRGBImage* image, Rect bounds, int16 alignment,
                                void* argument)
{
    const boolean_t hit = operation == icon_point || operation == icon_rect;
    IconRGBImage snapshot = *image;
    const unsigned pixels = image->width * image->width;
    const unsigned mask_size = image->mask ? (image->packed_mask ? pixels / 8 : pixels) : 0;
    const unsigned rgb_size = operation == icon_draw ? pixels * 4 : 0;
    Byte* buffers = mask_size + rgb_size ? malloc(mask_size + rgb_size) : NULL;
    if (mask_size + rgb_size && !buffers)
        return hit ? FALSE : memFullErr;
    if (rgb_size && !icon_rgb_decode(image->rgb, image->length, image->width, buffers)) {
        free(buffers);
        return paramErr;
    }
    if (mask_size) {
        memcpy(buffers + rgb_size, image->mask, mask_size);
        snapshot.mask = buffers + rgb_size;
    }
    RgnHandle region = operation == icon_region ? argument : C_NewRgn();
    if (!region) {
        free(buffers);
        return hit ? FALSE : memFullErr;
    }
    int32 result = noErr;
    if (RECT_WIDTH(&bounds) <= 0 || RECT_HEIGHT(&bounds) <= 0)
        C_SetEmptyRgn(region);
    else
        result = icon_rgb_map_mask(&snapshot, alignment, &bounds, region);
    if (!result && !C_EmptyRgn(region)) {
        if (operation == icon_draw)
            result = icon_rgb_draw_image(&snapshot, buffers, &bounds, region);
        else if (operation == icon_rect)
            result = C_RectInRgn(argument, region);
        else if (operation == icon_point) {
            const Point* guest = argument;
            Point point = {CW(guest->v), CW(guest->h)};
            result = C_PtInRgn(point, region);
        }
    }
    else if (hit)
        result = FALSE;
    if (operation != icon_region)
        C_DisposeRgn(region);
    free(buffers);
    return result;
}

#endif
