/*+===================================================================
  File:      CDEF-128.c

  Summary:   C translation of the Finder custom framed control resource.

  Classes:   C has no classes.

  Functions: finder_cdef128

  Origin:    Translation of the original CDEF-128.bin Finder resource.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/

#include "finder_code.h"

static int16_t signed_word(uint32_t value)
{
    value &= 0xffff;
    return (int16_t)(value < 0x8000 ? (int32_t)value : (int32_t)value - 0x10000);
}

static void inset(FinderCodeRect* rect)
{
    rect->top = signed_word((uint32_t)rect->top + 3);
    rect->left = signed_word((uint32_t)rect->left + 3);
    rect->bottom = signed_word((uint32_t)rect->bottom - 3);
    rect->right = signed_word((uint32_t)rect->right - 3);
}

int32_t finder_cdef128(const FinderCodeServices* services, void* context, int16_t variant, uint32_t control,
                       int16_t message, uint32_t parameter)
{
    static const uint8_t gray[8] = {0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55};
    int32_t result = 10;
    (void)variant;
    services->lock(context, control);
    uint32_t record = services->read32(context, control);
    uint32_t top_left = services->read32(context, record + 8);
    uint32_t bottom_right = services->read32(context, record + 12);
    FinderCodeRect rect = {signed_word(top_left >> 16), signed_word(top_left), signed_word(bottom_right >> 16),
                           signed_word(bottom_right)};

    switch (message) {
    case 0: /* drawCntl; the original ignores the part-code parameter. */
        if (!services->read8(context, record + 16))
            result = 0;
        else if (services->read8(context, record + 17)) {
            services->pen_size(context, 2, 2);
            services->frame_rect(context, &rect);
            services->pen_normal(context);
        }
        else {
            services->erase_rect(context, &rect);
            services->frame_rect(context, &rect);
            inset(&rect);
            if (!(services->rom85(context) & 0x4000)) {
                uint16_t color[3];
                services->get_fore_color(context, color);
                /* Re-dereference after Toolbox calls, as the original does. */
                record = services->read32(context, control);
                uint32_t value_address = record + 38;
                int16_t value = signed_word((uint32_t)services->read8(context, value_address) << 8 |
                                            services->read8(context, value_address + 1));
                services->pm_fore_color(context, value);
                services->paint_rect(context, &rect);
                services->rgb_fore_color(context, color);
            }
            else
                services->fill_rect(context, &rect, gray);
        }
        break;
    case 1: /* testCntl: a packed Point, vertical in the high word. */
        result = 0;
        if (services->read8(context, record + 16)) {
            inset(&rect);
            int16_t vertical = signed_word(parameter >> 16);
            int16_t horizontal = signed_word(parameter);
            if (horizontal >= rect.left && horizontal < rect.right && vertical >= rect.top && vertical < rect.bottom)
                result = 10;
        }
        break;
    case 2: /* calcCRgn: strip the high byte of the guest region address. */
        services->rect_region(context, parameter & 0x00ffffff, &rect);
        break;
    default:
        break;
    }
    services->unlock(context, control);
    return result;
}
