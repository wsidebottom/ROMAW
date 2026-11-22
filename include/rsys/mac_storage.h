/*+===================================================================
  File:      mac_storage.h

  Summary:   Shared host-order AppleSingle/AppleDouble and Basilisk metadata layout. No guest pointers, packed records
             or platform I/O belong in this boundary. ifndef ROMAW_MAC_STORAGE_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Shared host-order AppleSingle/AppleDouble and Basilisk metadata layout.
   No guest pointers, packed records or platform I/O belong in this boundary. */
#ifndef ROMAW_MAC_STORAGE_H
#define ROMAW_MAC_STORAGE_H
#include <stdint.h>
#include <stddef.h>

/* Equivalent host spellings of the classic folder Icon<CR> file. */
#define MAC_STORAGE_CUSTOM_ICON_NAME "Icon\r"
#define MAC_STORAGE_ESCAPED_ICON_NAME "Icon%0D"
#define MAC_STORAGE_SFM_ICON_CHARACTER 0xf00d

#define MAC_STORAGE_INFO_DIRECTORY ".finf"
#define MAC_STORAGE_RESOURCE_DIRECTORY ".rsrc"
#define MAC_STORAGE_NETATALK_DIRECTORY ".AppleDouble"

typedef struct ROMAWMacStorageEntry {
    uint32_t id, offset, length, capacity;
} ROMAWMacStorageEntry;

static inline uint32_t romaw_mac_storage_u32(const unsigned char* p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* Callers may supply a bounded header buffer rather than the entire file.
   Both historical magic byte orders are recognized, as in the File Manager. */
static inline int romaw_mac_storage_magic(const unsigned char* bytes, size_t available)
{
    if (!bytes || available < 4)
        return 0;
    uint32_t magic = romaw_mac_storage_u32(bytes);
    return magic == 0x00051600 || magic == 0x00051607 || magic == 0x00160500 || magic == 0x07160500;
}

static inline int romaw_mac_storage_table(const unsigned char* bytes, size_t available, unsigned* count)
{
    if (available < 26 || !count || !romaw_mac_storage_magic(bytes, available))
        return 0;
    *count = (unsigned)bytes[24] << 8 | bytes[25];
    return *count <= (available - 26) / 12;
}

static inline int romaw_mac_storage_entry(const unsigned char* bytes, size_t available, uint64_t file_size,
                                          unsigned index, ROMAWMacStorageEntry* entry)
{
    unsigned count;
    if (!entry || !romaw_mac_storage_table(bytes, available, &count) || index >= count)
        return 0;
    const unsigned char* p = bytes + 26 + index * 12;
    entry->id = romaw_mac_storage_u32(p);
    entry->offset = romaw_mac_storage_u32(p + 4);
    entry->length = romaw_mac_storage_u32(p + 8);
    if (entry->offset > INT32_MAX || entry->length > INT32_MAX - entry->offset || entry->offset < 26 + count * 12 ||
        (entry->length && (entry->offset > file_size || entry->length > file_size - entry->offset)) ||
        (entry->id == 9 && entry->length < 16))
        return 0;
    entry->capacity = INT32_MAX;
    return 1;
}

/* Encode one descriptor while constructing or updating a container. Callers
   publish their buffer only after every entry has been written successfully.
   Payloads must already exist within file_size; no guest structs are copied. */
static inline int romaw_mac_storage_store(unsigned char* bytes, size_t available, uint64_t file_size, unsigned index,
                                          const ROMAWMacStorageEntry* entry)
{
    unsigned count;
    if (!entry || !romaw_mac_storage_table(bytes, available, &count) || index >= count ||
        entry->offset < 26 + count * 12 || entry->offset > INT32_MAX || entry->length > INT32_MAX - entry->offset ||
        (entry->length && (entry->offset > file_size || entry->length > file_size - entry->offset)) ||
        (entry->id == 9 && entry->length < 16))
        return 0;
    unsigned char* destination = bytes + 26 + index * 12;
    uint32_t values[] = {entry->id, entry->offset, entry->length};
    for (unsigned field = 0; field < 3; ++field)
        for (unsigned byte = 0; byte < 4; ++byte)
            destination[field * 4 + byte] = values[field] >> (24 - byte * 8);
    return 1;
}

/* capacity is the growth allowed before the next entry, not allocation slack.
   Entry fields are host-order offsets; the located FInfo/fork bytes stay in
   their original Macintosh encoding. Compact records locate the first FInfo. */
static inline int romaw_mac_storage_find(const unsigned char* bytes, size_t available, uint64_t file_size, uint32_t id,
                                         int compact_info, ROMAWMacStorageEntry* entry)
{
    unsigned count;
    if (romaw_mac_storage_table(bytes, available, &count)) {
        for (unsigned i = 0; i < count; ++i)
            if (romaw_mac_storage_u32(bytes + 26 + i * 12) == id) {
                if (!romaw_mac_storage_entry(bytes, available, file_size, i, entry))
                    return 0;
                for (unsigned j = 0; j < count; ++j) {
                    uint32_t other = romaw_mac_storage_u32(bytes + 26 + j * 12 + 4);
                    if (other > entry->offset && other - entry->offset < entry->capacity)
                        entry->capacity = other - entry->offset;
                }
                return 1;
            }
        return 0;
    }
    if (entry && bytes && compact_info && !romaw_mac_storage_magic(bytes, available) && id == 9 &&
        available == file_size && available >= 16 && available <= 256) {
        entry->id = 9;
        entry->offset = 0;
        /* Legacy short records may contain a reserved trailer. */
        entry->length = available >= 32 ? 32 : 16;
        entry->capacity = available;
        return 1;
    }
    return 0;
}
#endif
