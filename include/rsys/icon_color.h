/*+===================================================================
  File:      icon_color.h

  Summary:   System 7 icon-label palette rules, following SuperMario IconUtils.c Brighten/MakeClut and IconUtils.r's
             protected color-index lists. ifndef RSYS_ICON_COLOR_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* System 7 icon-label palette rules, following SuperMario IconUtils.c
   Brighten/MakeClut and IconUtils.r's protected color-index lists. */
#ifndef RSYS_ICON_COLOR_H
#define RSYS_ICON_COLOR_H

#include <stdint.h>

static inline int ROMlib_icon_label_index(unsigned depth, unsigned index)
{
    static const uint8_t indices8[] = {0,   1,   5,   8,   19,  22,  42,  43,  51,  72,  84,  92,
                                       105, 127, 146, 159, 165, 171, 176, 192, 216, 227, 236, 245,
                                       246, 247, 248, 249, 250, 251, 252, 253, 254, 255};
    static const uint8_t indices4[] = {0, 1, 7, 8, 11, 12, 13, 14, 15};
    const uint8_t* indices = depth == 8 ? indices8 : indices4;
    unsigned count = depth == 8 ? sizeof indices8 : sizeof indices4;
    for (unsigned i = 0; i < count; ++i)
        if (indices[i] == index)
            return 1;
    return 0;
}

static inline void ROMlib_icon_label_rgb(uint16_t color[3], const uint16_t label[3], unsigned depth, unsigned index)
{
    if (depth == 4) {
        if (index == 15)
            for (unsigned i = 0; i < 3; ++i)
                color[i] = label[i];
        return;
    }
    if (depth != 8 || !ROMlib_icon_label_index(depth, index))
        return;
    uint32_t maximum = label[0];
    for (unsigned i = 1; i < 3; ++i)
        if (label[i] > maximum)
            maximum = label[i];
    if (!maximum)
        return; /* Black labels change the text only. */
    uint32_t luminance = (5u * label[0] + 9u * label[1] + 2u * label[2]) >> 4;
    luminance >>= 1;
    luminance += luminance >> 2;
    luminance += 0x6000;
    for (unsigned i = 0; i < 3; ++i) {
        uint32_t tint = (uint32_t)label[i] * 65535u / maximum;
        tint = 65535u - luminance + tint * luminance / 65535u;
        color[i] = index ? ((uint32_t)color[i] * tint) >> 16 : ((uint32_t)color[i] + tint) >> 1;
    }
}

/* SuperMario IconUtils Darken: halve brightness and emphasize the strongest
   component, retaining near-gray colors (two 8-bit component levels). Inputs
   and outputs are host-order RGB values; callers retain their alpha/mask. */
static inline void ROMlib_icon_selected_rgb(uint16_t color[3])
{
    for (unsigned i = 0; i < 3; ++i)
        color[i] >>= 1;
    int rg = (int)color[0] - color[1], rb = (int)color[0] - color[2];
    if (rg > -512 && rg < 512 && rb > -512 && rb < 512)
        return;
    unsigned smallest = 0, other = 1, last = 2;
    if ((int)color[smallest] - color[other] >= 512) {
        unsigned swap = smallest;
        smallest = other;
        other = swap;
    }
    if ((int)color[smallest] - color[last] >= 512) {
        unsigned swap = smallest;
        smallest = last;
        last = swap;
    }
    color[smallest] >>= 1;
    int difference = (int)color[other] - color[last];
    if (difference <= -512)
        color[other] >>= 1;
    else if (difference >= 512)
        color[last] >>= 1;
}

/* Classic icon families reserve certain palette indices for detail which
   should stay unchanged under labels and selection. CIcons have no such list. */
static inline void ROMlib_icon_selected_index_rgb(uint16_t color[3], unsigned depth, unsigned index)
{
    if (ROMlib_icon_label_index(depth, index))
        ROMlib_icon_selected_rgb(color);
}

#endif
