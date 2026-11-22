/*+===================================================================
  File:      icons.h

  Summary:   Declares icons support for finder metadata package support.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in packages/finder-metadata/include/finder_metadata; original attribution and
             provenance comments are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             finder-metadata/LICENSE.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#ifndef FINDER_ICONS_H
#define FINDER_ICONS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum FinderVolumeKind {
    FINDER_VOLUME_NONE,
    FINDER_VOLUME_LOCAL,
    FINDER_VOLUME_SHARED,
    FINDER_VOLUME_FLOPPY,
    FINDER_VOLUME_OPTICAL,
    FINDER_VOLUME_ROMAW,
    FINDER_VOLUME_IMAGE,
    FINDER_VOLUME_DMG
};

typedef struct FinderIcon {
    unsigned char mono[256], small[64], color4[512], color8[1024];
    unsigned char small4[128], small8[256];
    unsigned present;
} FinderIcon;

/* Bits follow the order of the six resource types below. */
extern const uint32_t finder_icon_types[6];
extern const size_t finder_icon_sizes[6];
int finder_parse_icon(const unsigned char* fork, size_t size, uint32_t creator, uint32_t file_type, FinderIcon* icon);
int finder_bundle_icon_id(const unsigned char* bundle, size_t size, uint32_t creator, unsigned local, int* id);
size_t finder_bundle_file_types(const unsigned char* fork, size_t size, uint32_t creator, uint32_t* types,
                                size_t capacity);
/* System, Apple menu, Control Panels, Preferences, Fonts, Extensions. */
int finder_location_icon(int desktop, FinderIcon* icon);
int finder_volume_icon(unsigned kind, FinderIcon* icon);
int finder_image_file_icon(int dmg, FinderIcon* icon);
/* Extension includes its dot; ASCII matching ignores case. Unknown returns zero. */
int finder_image_extension_icon(const char* extension, FinderIcon* icon);
/* Type-specific generic art; call only after custom/application artwork fails.
   Unknown types return zero without changing the output. */
int finder_type_icon(uint32_t type, FinderIcon* icon);
int finder_special_folder_icon(unsigned index, FinderIcon* icon);
int finder_parse_custom_icon(const unsigned char* fork, size_t size, FinderIcon* icon);
/* Replace the custom family, or remove it when icon is NULL. The caller frees
   the rebuilt fork. A malformed source is rejected before any write. */
int finder_edit_custom_icon(const unsigned char* fork, size_t size, const FinderIcon* icon, unsigned char** result,
                            size_t* result_size);
int finder_parse_icns_icon(const unsigned char* bytes, size_t size, FinderIcon* icon);
#define FINDER_ICON_FILE_LIMIT (32u * 1024u * 1024u)
/* ICO or ICNS; requested size is 16 or 32. Output has size*size*4 RGBA bytes.
   Native previews retain full colour/alpha. Failure leaves output unchanged. */
int finder_decode_icon_file(const unsigned char* bytes, size_t length, unsigned size, unsigned char* rgba);
/* Preserve classic families, or quantize decoded artwork for Toolbox consumers. */
int finder_parse_icon_file(const unsigned char* bytes, size_t length, FinderIcon* icon);
int finder_icon_from_rgba(const unsigned char large[4096], const unsigned char small[1024], FinderIcon* icon);
#define FINDER_ICON_ICNS_LIMIT 2304
size_t finder_icon_icns_encode(const FinderIcon* icon, unsigned char* bytes, size_t capacity);
void finder_icon_rgba(const FinderIcon* icon, unsigned char rgba[4096]);
void finder_icon_rgba_size(const FinderIcon* icon, unsigned size, unsigned char* rgba);
#ifdef __cplusplus
}
#endif
#endif
