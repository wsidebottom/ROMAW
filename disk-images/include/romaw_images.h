/*+===================================================================
  File:      romaw_images.h

  Summary:   Declares romaw images support for disk images package support.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in packages/disk-images/include; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#ifndef ROMAW_IMAGES_H
#define ROMAW_IMAGES_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct romaw_image_set romaw_image_set;
int romaw_images_dc42_sync(FILE* file);
/* NDIF permits in-place writes only for an identity raw map without CRC. */
int romaw_images_ndif_writable(const char* source);

typedef struct {
    int directory;
    uint64_t data_size, resource_size;
    unsigned char finder_info[32];
} romaw_image_item;

/* Direct catalog/fork access. No directories, placeholders or sidecars are
     created. Returned paths identify virtual entries inside this open set. */
romaw_image_set* romaw_images_open_direct_utf8(const char* source, char* error, size_t capacity);
romaw_image_set* romaw_images_open_direct(const char* source, void (*pump)(void), char* error, size_t capacity);
int romaw_images_stat_utf8(romaw_image_set* set, const char* path, romaw_image_item* item);
/* Source image and escaped image:volume/item path for Classic Get Info. */
int romaw_images_target_utf8(romaw_image_set* set, const char* path, char* source, size_t source_capacity, char* guest,
                             size_t guest_capacity);
int romaw_images_list_utf8(romaw_image_set* set, const char* path,
                           int (*visit)(const char*, const romaw_image_item*, void*), void* opaque);
int romaw_images_fork_utf8(romaw_image_set* set, const char* path, int resource, unsigned char** bytes, size_t* size,
                           char* error, size_t capacity);
/* Bounded source reads and host fork layout; no file content is materialized. */
int romaw_images_read_fork_utf8(romaw_image_set* set, const char* path, int resource, uint64_t offset, void* bytes,
                                size_t size, char* error, size_t capacity);
int romaw_images_host_info_utf8(const char* path, romaw_image_item* item, char* resource_path, size_t resource_capacity,
                                uint64_t* resource_offset, char* error, size_t capacity);
int romaw_images_guest_name_utf8(romaw_image_set* set, const char* path, char name[32]);
/* Copy an item (including forks and descendants) to a host destination. */
int romaw_images_export_utf8(romaw_image_set* set, const char* path, const char* destination, char* error,
                             size_t capacity);
typedef int (*romaw_image_copy_progress)(void* opaque, uint64_t done, uint64_t total);
int romaw_images_export_progress_utf8(romaw_image_set* set, const char* path, const char* destination,
                                      romaw_image_copy_progress progress, void* opaque, char* error, size_t capacity);
/* Resolve a colon-separated MacRoman path below a projected volume root. */
int romaw_images_guest_path_utf8(romaw_image_set* set, unsigned volume, const char* guest, char* path, size_t capacity,
                                 char* error, size_t error_capacity);
/* 0: create directory, 1: rename (value is a virtual path), 2: move to image Trash,
     3: import host file/directory (value is a host path). */
int romaw_images_mutate_utf8(romaw_image_set* set, const char* path, unsigned operation, const char* value, char* error,
                             size_t capacity);
/* Read-only HFS facade for the classic File Manager. Metadata lives in RAM;
     file blocks are read from the original filesystem/container on demand. */
int romaw_images_device(romaw_image_set* set, unsigned volume, char* error, size_t capacity);
int romaw_images_device_read(romaw_image_set* set, unsigned volume, uint64_t offset, void* bytes, size_t size);
/* Metadata-only large volumes resolve forks by projected catalog ID. Returns
   1 on success, 0 for a normal extent-backed fork, -1 on invalid input/read. */
int romaw_images_device_fork_read(romaw_image_set* set, unsigned volume, uint32_t id, int resource, uint64_t offset,
                                  void* bytes, size_t size);
/* Native host path encoding. Creates a private snapshot below cache_directory.
   kind 1 is a decompressed raw image; kind 2 an HFS+ directory with AppleDouble.
   A null result means unsupported/damaged input; error contains a diagnostic.
   Source images are never modified. max 32 partitions, 1000000 HFS+ records, 16 GiB. */
romaw_image_set* romaw_images_prepare(const char* source, const char* cache_directory, void (*pump)(void), char* error,
                                      size_t capacity);
/* UTF-8 paths on every host; returned snapshot paths are UTF-8 too. */
romaw_image_set* romaw_images_prepare_utf8(const char* source, const char* cache_directory, void (*pump)(void),
                                           char* error, size_t capacity);
/* Open the source read-only and project its catalog without copying file
     forks. The private directory contains catalog placeholders/metadata only.
     Requested forks are read from the still-open image on demand. */
romaw_image_set* romaw_images_open_readonly_utf8(const char* source, const char* cache_directory, char* error,
                                                 size_t capacity);
/* Materialize a projected item for an icon read (resources only), or a copy
     (both forks; recursive for directories). Never writes the source image. */
int romaw_images_read_item_utf8(romaw_image_set* set, const char* path, int resources_only, char* error,
                                size_t capacity);
/* Catalog data-fork length, available before any fork is read. */
int romaw_images_item_size_utf8(romaw_image_set* set, const char* path, uint64_t* size);
unsigned romaw_images_count(const romaw_image_set* set);
int romaw_images_kind(const romaw_image_set* set, unsigned index);
const char* romaw_images_path(const romaw_image_set* set, unsigned index);
const char* romaw_images_name(const romaw_image_set* set, unsigned index);
/* Nonempty for incomplete media whose catalog can still be read. The caller
   must obtain an explicit choice before exposing a partial volume to a user. */
const char* romaw_images_warning(const romaw_image_set* set);
void romaw_images_close(romaw_image_set* set, int remove_files);
#ifdef __cplusplus
}
#endif
#endif
