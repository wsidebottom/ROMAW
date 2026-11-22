/*+===================================================================
  File:      specialicons.c

  Summary:   Implements specialicons support for finder metadata package support.

  Classes:   None.

  Functions: finder_image_file_icon, finder_image_extension_icon, finder_location_icon, finder_special_folder_icon,
             finder_type_icon, finder_volume_icon

  Origin:    Existing repository source in packages/finder-metadata/src; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             finder-metadata/LICENSE.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include "finder_metadata/icons.h"
#include "finder_folder_icons.inc"

int finder_special_folder_icon(unsigned index, FinderIcon* icon)
{
    if (index >= 6)
        return 0;
    *icon = finder_special_icons[index];
    return 1;
}

int finder_image_file_icon(int dmg, FinderIcon* icon)
{
    *icon = finder_special_icons[13 + (dmg != 0)];
    return 1;
}

int finder_image_extension_icon(const char* extension, FinderIcon* icon)
{
    if (!extension || !icon)
        return 0;
    for (unsigned i = 0; i < sizeof finder_image_extensions / sizeof finder_image_extensions[0]; ++i) {
        const char* input = extension;
        const char* expected = finder_image_extensions[i].extension;
        while (*input && *expected) {
            unsigned char c = *input++;
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
            if (c != (unsigned char)*expected++)
                break;
            if (!*input && !*expected) {
                *icon = finder_special_icons[finder_image_extensions[i].icon];
                return 1;
            }
        }
    }
    return 0;
}

int finder_volume_icon(unsigned kind, FinderIcon* icon)
{
    if (kind < FINDER_VOLUME_LOCAL || kind > FINDER_VOLUME_DMG)
        return 0;
    *icon = finder_special_icons[5 + kind];
    return 1;
}

int finder_location_icon(int desktop, FinderIcon* icon)
{
    *icon = finder_special_icons[15 + (desktop != 0)];
    return 1;
}

int finder_type_icon(uint32_t type, FinderIcon* icon)
{
    if (!icon)
        return 0;
    for (unsigned i = 0; i < sizeof finder_generic_types / sizeof finder_generic_types[0]; ++i)
        if (finder_generic_types[i].type == type) {
            *icon = finder_special_icons[finder_generic_types[i].icon];
            return 1;
        }
    return 0;
}
