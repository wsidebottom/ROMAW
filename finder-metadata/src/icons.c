/*+===================================================================
  File:      icons.c

  Summary:   Implements icons support for finder metadata package support.

  Classes:   None.

  Functions: finder_bundle_file_types, finder_bundle_icon_id, finder_icon_icns_encode, finder_icon_rgba, finder_icon_rgba_size,
             finder_icon_from_rgba, finder_parse_custom_icon, finder_parse_icns_icon, finder_parse_icon,
             finder_edit_custom_icon.

  Origin:    Existing repository source in packages/finder-metadata/src; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             finder-metadata/LICENSE.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include "finder_metadata/icons.h"
#include <stdlib.h>
#include <string.h>

const uint32_t finder_icon_types[6] = {0x49434e23, 0x69637323, 0x69636c34, 0x69636c38, 0x69637334, 0x69637338};
const size_t finder_icon_sizes[6] = {256, 64, 512, 1024, 128, 256};

static unsigned word(const unsigned char* p)
{
    return ((unsigned)p[0] << 8) | p[1];
}

static uint32_t longword(const unsigned char* p)
{
    return ((uint32_t)word(p) << 16) | word(p + 2);
}

static int extent(size_t size, size_t offset, size_t length)
{
    return offset <= size && length <= size - offset;
}

typedef struct Fork {
    const unsigned char* bytes;
    size_t size, data, data_size, map, map_size, types;
    unsigned count;
} Fork;

/* Resource offsets are relative to three different bases. Validate each base
 * before following offsets; never cast unaligned on-disk structs. */
static const unsigned char* resource(const Fork* f, uint32_t type, int indexed, int key, size_t* length)
{
    unsigned i;
    for (i = 0; i < f->count; ++i) {
        size_t t = f->types + 2 + i * 8, refs, n, j;
        const unsigned char* p = f->bytes + f->map;
        if (longword(p + t) != type)
            continue;
        n = (size_t)word(p + t + 4) + 1;
        refs = f->types + word(p + t + 6);
        if (!extent(f->map_size, refs, n * 12))
            return NULL;
        for (j = 0; j < n; ++j) {
            const unsigned char* r = p + refs + j * 12;
            size_t offset, size;
            if (indexed ? j != (size_t)key : (int16_t)word(r) != key)
                continue;
            if (r[4] & 1)
                return NULL; /* compressed resource needs a decompressor */
            offset = ((size_t)r[5] << 16) | ((size_t)r[6] << 8) | r[7];
            if (!extent(f->data_size, offset, 4))
                return NULL;
            size = longword(f->bytes + f->data + offset);
            if (!extent(f->data_size, offset + 4, size))
                return NULL;
            *length = size;
            return f->bytes + f->data + offset + 4;
        }
        return NULL;
    }
    return NULL;
}

int finder_bundle_icon_id(const unsigned char* p, size_t size, uint32_t creator, unsigned local, int* id)
{
    size_t offset = 8, i, count;
    int modern = 0, modern_id = 0;
    if (size < 8 || longword(p) != creator)
        return 0;
    count = (size_t)word(p + 6) + 1;
    for (i = 0; i < count; ++i) {
        size_t n, j;
        uint32_t type;
        if (!extent(size, offset, 6))
            return 0;
        type = longword(p + offset);
        n = (size_t)word(p + offset + 4) + 1;
        offset += 6;
        if (n > (size - offset) / 4)
            return 0;
        for (j = 0; j < n; ++j, offset += 4)
            if (word(p + offset) == local) {
                if (type == finder_icon_types[0]) {
                    *id = (int16_t)word(p + offset + 2);
                    return 1;
                }
                if (type == 0x69636e73) {
                    modern = 1;
                    modern_id = (int16_t)word(p + offset + 2);
                }
            }
    }
    if (modern)
        *id = modern_id;
    return modern;
}

static int parse_icon(const unsigned char* bytes, size_t size, uint32_t creator, uint32_t file_type, int custom,
                      FinderIcon* icon, int* family_id)
{
    Fork f;
    size_t n, i, j;
    int id = custom ? -16455 : 0, found = custom;
    const unsigned char* p;
    unsigned char* dest[6] = {icon->mono, icon->small, icon->color4, icon->color8, icon->small4, icon->small8};
    memset(icon, 0, sizeof *icon);
    if (!bytes || size < 16)
        return 0;
    f.bytes = bytes;
    f.size = size;
    f.data = longword(bytes);
    f.map = longword(bytes + 4);
    f.data_size = longword(bytes + 8);
    f.map_size = longword(bytes + 12);
    if (!extent(size, f.data, f.data_size) || !extent(size, f.map, f.map_size) || f.map_size < 28)
        return 0;
    f.types = word(bytes + f.map + 24);
    if (!extent(f.map_size, f.types, 2))
        return 0;
    n = word(bytes + f.map + f.types);
    f.count = n == 65535 ? 0 : n + 1;
    if (!extent(f.map_size, f.types + 2, (size_t)f.count * 8))
        return 0;
    /* FREF local IDs are not necessarily zero, and resource IDs need not be 128. */
    for (i = 0; i < 65536 && !found; ++i) {
        unsigned local;
        p = resource(&f, 0x46524546, 1, i, &n);
        if (!p)
            break;
        if (n < 6 || longword(p) != file_type)
            continue;
        local = word(p + 4);
        for (j = 0; j < 65536; ++j) {
            p = resource(&f, 0x424e444c, 1, j, &n);
            if (!p)
                break;
            if (finder_bundle_icon_id(p, n, creator, local, &id)) {
                found = 1;
                break;
            }
        }
    }
    if (!found) {
        /* Some extensions have a single icon family and no bundle at all.
         Do not guess from an application's internal dialog artwork, or
         substitute another file type from an existing bundle. */
        if (resource(&f, 0x424e444c, 1, 0, &n))
            return 0;
        p = resource(&f, 0x49434e23, 1, 0, &n);
        if (!p || n < 256 || resource(&f, 0x49434e23, 1, 1, &n))
            return 0;
        /* Recover the resource ID from the already bounded type/ref lists. */
        for (i = 0; i < f.count; ++i) {
            const unsigned char* t = bytes + f.map + f.types + 2 + i * 8;
            if (longword(t) == 0x49434e23) {
                size_t ref = f.types + word(t + 6);
                if (!extent(f.map_size, ref, 12))
                    return 0;
                id = (int16_t)word(bytes + f.map + ref);
                break;
            }
        }
    }
    if (family_id) {
        *family_id = id;
        return 1;
    }
    for (i = 0; i < 6; ++i) {
        p = resource(&f, finder_icon_types[i], 0, id, &n);
        if (p && n >= finder_icon_sizes[i]) {
            memcpy(dest[i], p, finder_icon_sizes[i]);
            icon->present |= 1u << i;
        }
    }
    return (icon->present & 1) != 0;
}

size_t finder_bundle_file_types(const unsigned char* bytes, size_t size, uint32_t creator, uint32_t* types,
                                size_t capacity)
{
    if (!bytes || size < 16 || !types)
        return 0;
    Fork f = {0};
    f.bytes = bytes;
    f.size = size;
    f.data = longword(bytes);
    f.map = longword(bytes + 4);
    f.data_size = longword(bytes + 8);
    f.map_size = longword(bytes + 12);
    if (!extent(size, f.data, f.data_size) || !extent(size, f.map, f.map_size) || f.map_size < 28)
        return 0;
    f.types = word(bytes + f.map + 24);
    if (!extent(f.map_size, f.types, 2))
        return 0;
    unsigned n = word(bytes + f.map + f.types);
    f.count = n == 65535 ? 0 : n + 1;
    if (!extent(f.map_size, f.types + 2, (size_t)f.count * 8))
        return 0;
    size_t count = 0;
    for (unsigned i = 0; i < 65536 && count < capacity; ++i) {
        size_t length;
        const unsigned char* ref = resource(&f, 0x46524546, 1, i, &length);
        if (!ref)
            break;
        if (length < 6)
            continue;
        uint32_t type = longword(ref);
        unsigned local = word(ref + 4);
        int id, found = 0;
        for (unsigned j = 0; j < 65536; ++j) {
            const unsigned char* bundle = resource(&f, 0x424e444c, 1, j, &length);
            if (!bundle)
                break;
            if (finder_bundle_icon_id(bundle, length, creator, local, &id)) {
                found = 1;
                break;
            }
        }
        if (!found)
            continue;
        for (size_t j = 0; j < count; ++j)
            if (types[j] == type)
                found = 0;
        if (found)
            types[count++] = type;
    }
    return count;
}

int finder_parse_icon(const unsigned char* bytes, size_t size, uint32_t creator, uint32_t file_type, FinderIcon* icon)
{
    return parse_icon(bytes, size, creator, file_type, 0, icon, NULL);
}

int finder_icon_resource_id(const unsigned char* bytes, size_t size, uint32_t creator, uint32_t file_type, int* id)
{
    FinderIcon unused;
    return id && parse_icon(bytes, size, creator, file_type, 0, &unused, id);
}

int finder_parse_custom_icon(const unsigned char* bytes, size_t size, FinderIcon* icon)
{
    return parse_icon(bytes, size, 0, 0, 1, icon, NULL);
}

enum { custom_icon_id = -16455, fork_header_size = 256, resource_data_limit = 0x1000000 };

typedef struct IconResource {
    uint32_t type;
    uint16_t id, name;
    unsigned char flags;
    const unsigned char* bytes;
    size_t size;
} IconResource;

static void store_word(unsigned char* p, uint16_t value)
{
    p[0] = value >> 8;
    p[1] = value;
}

static void store_long(unsigned char* p, uint32_t value)
{
    store_word(p, value >> 16);
    store_word(p + 2, value);
}

static int custom_icon_type(uint32_t type)
{
    if (type == 0x69636e73 || type == 0x49434f4e || type == 0x69636c32 || type == 0x69637332 || type == 0x696c3332 ||
        type == 0x69733332 || type == 0x6c386d6b || type == 0x73386d6b)
        return 1;
    for (unsigned index = 0; index < 6; ++index)
        if (finder_icon_types[index] == type)
            return 1;
    return 0;
}

int finder_edit_custom_icon(const unsigned char* fork, size_t size, const FinderIcon* icon, unsigned char** result,
                            size_t* result_size)
{
    if (!result || !result_size || (size && (!fork || size < 16)) || (icon && !(icon->present & 1)))
        return 0;
    *result = NULL;
    *result_size = 0;
    /* The map has 16-bit offsets. No valid type/reference directory can use
       more than this many records. Keep payloads and names as opaque bytes. */
    IconResource* records = calloc(UINT16_MAX / 12 + 6, sizeof *records);
    if (!records)
        return 0;
    size_t count = 0, data_size = 0, name_size = 0;
    const unsigned char* names = NULL;
    uint16_t map_flags = 0;
    if (size) {
        size_t data = longword(fork), map = longword(fork + 4);
        size_t data_length = longword(fork + 8), map_length = longword(fork + 12);
        if (data < 16 || data_length > resource_data_limit || !extent(size, data, data_length) ||
            !extent(size, map, map_length) || map_length < 30 || map_length > 2 * UINT16_MAX)
            goto invalid;
        const unsigned char* m = fork + map;
        size_t types = word(m + 24), name_offset = word(m + 26);
        if (types < 28 || !extent(map_length, types, 2) || name_offset > map_length || name_offset < types + 2)
            goto invalid;
        map_flags = word(m + 22);
        names = m + name_offset;
        name_size = map_length - name_offset;
        size_t type_count = word(m + types) == UINT16_MAX ? 0 : (size_t)word(m + types) + 1;
        size_t refs_start = types + 2 + type_count * 8;
        if (refs_start > name_offset)
            goto invalid;
        for (size_t type_index = 0; type_index < type_count; ++type_index) {
            const unsigned char* t = m + types + 2 + type_index * 8;
            uint32_t type = longword(t);
            size_t refs = types + word(t + 6), number = (size_t)word(t + 4) + 1;
            if (refs < refs_start || !extent(name_offset, refs, number * 12))
                goto invalid;
            for (size_t i = 0; i < number; ++i) {
                const unsigned char* ref = m + refs + i * 12;
                uint16_t id = word(ref), name = word(ref + 2);
                size_t offset = ((size_t)ref[5] << 16) | word(ref + 6);
                if (!extent(data_length, offset, 4) ||
                    (name != UINT16_MAX &&
                     (!extent(name_size, name, 1) || !extent(name_size, (size_t)name + 1, names[name]))))
                    goto invalid;
                size_t length = longword(fork + data + offset);
                if (!extent(data_length, offset + 4, length))
                    goto invalid;
                if ((int16_t)id == custom_icon_id && custom_icon_type(type))
                    continue;
                if (count >= UINT16_MAX / 12)
                    goto invalid;
                records[count++] = (IconResource){type, id, name, ref[4], fork + data + offset + 4, length};
            }
        }
    }
    if (icon) {
        const unsigned char* planes[] = {icon->mono,   icon->small,  icon->color4,
                                         icon->color8, icon->small4, icon->small8};
        for (unsigned i = 0; i < 6; ++i)
            if (icon->present & (1u << i)) {
                size_t position = 0;
                while (position < count && records[position].type != finder_icon_types[i])
                    ++position;
                while (position < count && records[position].type == finder_icon_types[i])
                    ++position;
                memmove(records + position + 1, records + position, (count - position) * sizeof *records);
                records[position] = (IconResource){
                    finder_icon_types[i], (uint16_t)custom_icon_id, UINT16_MAX, 0, planes[i], finder_icon_sizes[i]};
                ++count;
            }
    }
    size_t types = 0;
    for (size_t i = 0; i < count; ++i) {
        if (records[i].size + 4 > resource_data_limit - data_size)
            goto invalid;
        data_size += records[i].size + 4;
        if (!i || records[i - 1].type != records[i].type)
            ++types;
        else if (records[i - 1].id == records[i].id)
            goto invalid;
    }
    size_t refs_start = 30 + types * 8, name_offset = refs_start + count * 12;
    if (name_offset > UINT16_MAX)
        goto invalid;
    size_t map = fork_header_size + data_size, map_size = name_offset + name_size;
    unsigned char* out = calloc(1, map + map_size);
    if (!out)
        goto invalid;
    store_long(out, fork_header_size);
    store_long(out + 4, map);
    store_long(out + 8, data_size);
    store_long(out + 12, map_size);
    memcpy(out + map, out, 16);
    store_word(out + map + 22, map_flags);
    store_word(out + map + 24, 28);
    store_word(out + map + 26, name_offset);
    store_word(out + map + 28, (uint16_t)(types - 1));
    if (name_size)
        memcpy(out + map + name_offset, names, name_size);
    size_t type_index = 0, offset = 0;
    for (size_t i = 0; i < count;) {
        size_t end = i + 1;
        while (end < count && records[end].type == records[i].type)
            ++end;
        unsigned char* type = out + map + 30 + type_index++ * 8;
        store_long(type, records[i].type);
        store_word(type + 4, end - i - 1);
        store_word(type + 6, refs_start + i * 12 - 28);
        for (; i < end; ++i) {
            const IconResource* record = &records[i];
            unsigned char* ref = out + map + refs_start + i * 12;
            store_word(ref, record->id);
            store_word(ref + 2, record->name);
            store_long(ref + 4, (uint32_t)record->flags << 24 | (uint32_t)offset);
            unsigned char* data = out + fork_header_size + offset;
            store_long(data, record->size);
            memcpy(data + 4, record->bytes, record->size);
            offset += record->size + 4;
        }
    }
    *result = out;
    *result_size = map + map_size;
    free(records);
    return 1;
invalid:
    free(records);
    return 0;
}

/* Classic indexed ICNS families retain their original masks and palettes. */
int finder_parse_icns_icon(const unsigned char* bytes, size_t size, FinderIcon* icon)
{
    FinderIcon result = {0};
    unsigned char* planes[] = {result.mono, result.small, result.color4, result.color8, result.small4, result.small8};
    if (!bytes || size < 8 || longword(bytes) != 0x69636e73 || longword(bytes + 4) != size)
        return 0;
    for (size_t offset = 8; offset < size;) {
        if (!extent(size, offset, 8))
            return 0;
        size_t length = longword(bytes + offset + 4);
        if (length < 8 || !extent(size, offset, length))
            return 0;
        for (unsigned i = 0; i < 6; ++i)
            if (longword(bytes + offset) == finder_icon_types[i]) {
                if (length - 8 != finder_icon_sizes[i])
                    return 0;
                memcpy(planes[i], bytes + offset + 8, length - 8);
                result.present |= 1u << i;
            }
        offset += length;
    }
    if (!(result.present & 1))
        return 0;
    *icon = result;
    return 1;
}

/* Standard Macintosh icon palettes. These are colour values, not artwork. */
static const unsigned char palette4[16][3] = {{255, 255, 255}, {252, 243, 5},   {255, 100, 2}, {221, 8, 6},
                                              {242, 8, 132},   {70, 0, 164},    {0, 0, 211},   {2, 170, 234},
                                              {31, 182, 20},   {0, 100, 17},    {85, 44, 5},   {144, 112, 57},
                                              {192, 192, 192}, {128, 128, 128}, {64, 64, 64},  {0, 0, 0}};

static void color8(unsigned index, unsigned char* rgb)
{
    static const unsigned char ramp[10] = {238, 221, 187, 170, 136, 119, 85, 68, 34, 17};
    if (index < 215) {
        rgb[0] = 255 - (index / 36) * 51;
        rgb[1] = 255 - ((index / 6) % 6) * 51;
        rgb[2] = 255 - (index % 6) * 51;
    }
    else if (index < 255) {
        unsigned channel = (index - 215) / 10;
        rgb[0] = rgb[1] = rgb[2] = 0;
        if (channel < 3)
            rgb[channel] = ramp[(index - 215) % 10];
        else
            rgb[0] = rgb[1] = rgb[2] = ramp[(index - 215) % 10];
    }
    else
        rgb[0] = rgb[1] = rgb[2] = 0;
}

void finder_icon_palette_rgb(unsigned depth, unsigned index, unsigned char rgb[3])
{
    if (depth == 8)
        color8(index & 255, rgb);
    else if (depth == 4)
        memcpy(rgb, palette4[index & 15], 3);
    else
        memset(rgb, index & 1 ? 0 : 255, 3);
}

void finder_icon_rgba(const FinderIcon* icon, unsigned char rgba[4096])
{
    unsigned i;
    for (i = 0; i < 1024; ++i) {
        unsigned char* p = rgba + i * 4;
        if (icon->present & 8)
            color8(icon->color8[i], p);
        else if (icon->present & 4)
            memcpy(p, palette4[(icon->color4[i / 2] >> ((i & 1) ? 0 : 4)) & 15], 3);
        else
            p[0] = p[1] = p[2] = (icon->mono[i / 8] & (128 >> (i % 8))) ? 0 : 255;
        p[3] = (icon->mono[128 + i / 8] & (128 >> (i % 8))) ? 255 : 0;
    }
}

void finder_icon_rgba_size(const FinderIcon* icon, unsigned size, unsigned char* rgba)
{
    if (!icon || !rgba || (size != 16 && size != 32))
        return;
    if (size == 32) {
        finder_icon_rgba(icon, rgba);
        return;
    }
    if (!(icon->present & 2)) {
        unsigned char large[4096];
        finder_icon_rgba(icon, large);
        for (unsigned y = 0; y < 16; ++y)
            for (unsigned x = 0; x < 16; ++x)
                memcpy(rgba + (y * 16 + x) * 4, large + (y * 64 + x * 2) * 4, 4);
        return;
    }
    for (unsigned i = 0; i < 256; ++i) {
        unsigned char* pixel = rgba + i * 4;
        if (icon->present & 32)
            color8(icon->small8[i], pixel);
        else if (icon->present & 16)
            memcpy(pixel, palette4[(icon->small4[i / 2] >> ((i & 1) ? 0 : 4)) & 15], 3);
        else
            pixel[0] = pixel[1] = pixel[2] = icon->small[i / 8] & (128 >> (i % 8)) ? 0 : 255;
        pixel[3] = icon->small[32 + i / 8] & (128 >> (i % 8)) ? 255 : 0;
    }
}

static unsigned nearest_color(const unsigned char* rgba, unsigned count)
{
    unsigned best = 0, distance = UINT32_MAX;
    for (unsigned index = 0; index < count; ++index) {
        unsigned char color[3];
        if (count == 16)
            memcpy(color, palette4[index], 3);
        else
            color8(index, color);
        int r = (int)rgba[0] - color[0], g = (int)rgba[1] - color[1], b = (int)rgba[2] - color[2];
        unsigned current = r * r + g * g + b * b;
        if (current < distance) {
            distance = current;
            best = index;
            if (!current)
                break;
        }
    }
    return best;
}

int finder_icon_from_rgba(const unsigned char large[4096], const unsigned char small[1024], FinderIcon* icon)
{
    if (!large || !small || !icon)
        return 0;
    FinderIcon result = {0};
    for (unsigned pass = 0; pass < 2; ++pass) {
        unsigned pixels = pass ? 256 : 1024;
        const unsigned char* source = pass ? small : large;
        unsigned char* mono = pass ? result.small : result.mono;
        unsigned char* four = pass ? result.small4 : result.color4;
        unsigned char* eight = pass ? result.small8 : result.color8;
        for (unsigned i = 0; i < pixels; ++i) {
            const unsigned char* color = source + i * 4;
            if (color[3] < 128)
                continue;
            unsigned bit = 128 >> (i % 8);
            mono[pixels / 8 + i / 8] |= bit;
            if (299u * color[0] + 587u * color[1] + 114u * color[2] < 128000)
                mono[i / 8] |= bit;
            four[i / 2] |= nearest_color(color, 16) << ((i & 1) ? 0 : 4);
            eight[i] = nearest_color(color, 256);
        }
    }
    result.present = 63;
    *icon = result;
    return 1;
}

static void put_longword(unsigned char* p, uint32_t value)
{
    p[0] = value >> 24;
    p[1] = value >> 16;
    p[2] = value >> 8;
    p[3] = value;
}

size_t finder_icon_icns_encode(const FinderIcon* icon, unsigned char* bytes, size_t capacity)
{
    const unsigned char* planes[] = {icon->mono, icon->small, icon->color4, icon->color8, icon->small4, icon->small8};
    size_t length = 8;
    if (!(icon->present & 1) || (icon->present & ~63u))
        return 0;
    for (unsigned i = 0; i < 6; ++i)
        if (icon->present & (1u << i))
            length += 8 + finder_icon_sizes[i];
    if (!bytes || capacity < length)
        return 0;
    memcpy(bytes, "icns", 4);
    put_longword(bytes + 4, length);
    size_t offset = 8;
    for (unsigned i = 0; i < 6; ++i)
        if (icon->present & (1u << i)) {
            size_t size = finder_icon_sizes[i];
            put_longword(bytes + offset, finder_icon_types[i]);
            put_longword(bytes + offset + 4, size + 8);
            memcpy(bytes + offset + 8, planes[i], size);
            offset += 8 + size;
        }
    return length;
}
