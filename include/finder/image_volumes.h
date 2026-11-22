/*+===================================================================
  File:      image_volumes.h

  Summary:   Expose mounted Macintosh volumes and direct catalog browsing to Finder.

  Classes:   None.

  Functions: Interface declarations; see below.

  Origin:    Executor native Finder and File Manager integration.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#ifndef FINDER_IMAGE_VOLUMES_H
#define FINDER_IMAGE_VOLUMES_H
#include <stdint.h>
#include <stddef.h>
typedef struct FinderIcon FinderIcon;
#ifdef __cplusplus
extern "C" {
#endif
typedef struct FinderImageVolume {
    char source[32768];
    char name[32]; /* NUL-terminated MacRoman. */
    int16_t reference, partition;
    uint16_t read_only;
    uint64_t total_bytes, free_bytes;
} FinderImageVolume;

typedef struct FinderImageEntry {
    char name[32]; /* NUL-terminated MacRoman. */
    uint32_t id, parent, type, creator, data_size, resource_size, modified;
    uint16_t flags, directory;
    uint32_t created;
    uint16_t children, locked;
    unsigned char finder_info[32]; /* Raw DInfo/DXInfo or FInfo/FXInfo, big-endian. */
    uint32_t data_allocated, resource_allocated;
} FinderImageEntry;

typedef struct FinderApplicationMemory {
    uint32_t suggested, original_minimum;
    uint32_t minimum, preferred;
    uint16_t editable;
} FinderApplicationMemory;

/* Read the SIZE resources or save a SIZE 0 override, comparing the displayed
 * values before a write. The host pathname is mutable File Manager text. */
int16_t ROMlib_finder_host_memory(char* path, FinderApplicationMemory* memory, const FinderApplicationMemory* update);
int16_t ROMlib_finder_image_memory(int16_t reference, uint32_t parent, const char* name, uint32_t id,
                                   FinderApplicationMemory* memory, const FinderApplicationMemory* update);

/* Indexed volume snapshot: one on success, zero at the end. UI thread only. */
int ROMlib_finder_image_volume(uint32_t index, FinderImageVolume* result);
/* Borrowed source path and requested access mode for each live selection. */
const char* ROMlib_finder_image_mount(uint32_t index, int* mode);
/* One-based catalog index; -1 queries the directory itself. Macintosh OSErr. */
int16_t ROMlib_finder_image_entry(int16_t reference, uint32_t directory, int16_t index, FinderImageEntry* result);
int16_t ROMlib_finder_image_unmount(int16_t reference);
/* Fresh item details and bounded vers 1 reads for Get Info; UI thread only. */
int16_t ROMlib_finder_image_info(int16_t reference, uint32_t parent, const char* name, FinderImageEntry* result);
int ROMlib_finder_image_version(int16_t reference, uint32_t parent, const char* name, void* bytes, size_t capacity,
                                size_t* size);
/* Return the creator's MacRoman name and optional custom Kind text. The host
   directory selects its volume's Desktop DB. An image caller supplies the resolved app. */
void ROMlib_finder_document_kind(char* host_directory, int16_t reference, uint32_t parent, const char* application,
                                 uint32_t type, uint32_t creator, char owner[256], char kind[256]);
int16_t ROMlib_finder_image_lock(int16_t reference, uint32_t parent, const char* name, uint32_t id, int locked);

/* Caller frees the allocated Macintosh pathname. No file data is extracted. */
int16_t ROMlib_finder_image_path(int16_t reference, uint32_t parent, const char* name, char** result);
/* Read existing volume Desktop DB/DF records through bounded File Manager reads. */
int ROMlib_finder_image_desktop(int16_t reference, int operation, unsigned char key[38], unsigned index, void* data,
                                size_t* size, uint32_t* tag);
int16_t ROMlib_finder_image_set_comment(int16_t reference, uint32_t parent, const char* name, uint32_t id,
                                        const void* macroman, size_t size);
int ROMlib_finder_image_application(int16_t reference, uint32_t creator, FinderImageEntry* result);
int16_t ROMlib_finder_image_set_custom_icon(int16_t reference, uint32_t parent, const char* name, uint32_t id,
                                            const FinderIcon* icon);
int ROMlib_finder_image_custom_icon(int16_t reference, uint32_t parent, const char* name, int directory,
                                    FinderIcon* result);
/* Opening may fall back to a bounded catalog-only application search. */
int ROMlib_finder_image_find_application(int16_t reference, uint32_t creator, FinderImageEntry* result);
/* UI-thread writes through the mounted File Manager; never reopen the image. */
int16_t ROMlib_finder_image_writable(int16_t reference);
/* Change only layout fields in a fresh catalog record; preserve other metadata. */
int16_t ROMlib_finder_image_position(int16_t reference, uint32_t parent, const char* name, int32_t x, int32_t y);
int16_t ROMlib_finder_image_view(int16_t reference, uint32_t directory, uint16_t view);
int16_t ROMlib_finder_image_window(int16_t reference, uint32_t directory, int32_t left, int32_t top, int32_t right,
                                   int32_t bottom);
int16_t ROMlib_finder_image_lookup(int16_t reference, uint32_t parent, const char* name, uint32_t* id);
int16_t ROMlib_finder_image_create(int16_t reference, uint32_t parent, const char* name, int directory,
                                   const unsigned char info[32], uint32_t* id);
int16_t ROMlib_finder_image_set_info(int16_t reference, uint32_t parent, const char* name,
                                     const unsigned char info[32]);
int16_t ROMlib_finder_image_open_fork(int16_t reference, uint32_t parent, const char* name, int resource,
                                      int16_t* file);
int16_t ROMlib_finder_image_open_source_fork(int16_t reference, uint32_t parent, const char* name, int resource,
                                             int16_t* file);
int16_t ROMlib_finder_image_read(int16_t file, void* bytes, uint32_t count);
int16_t ROMlib_finder_image_write(int16_t file, const void* bytes, uint32_t count);
int16_t ROMlib_finder_image_close_fork(int16_t file);
int16_t ROMlib_finder_image_delete(int16_t reference, uint32_t parent, const char* name);
int16_t ROMlib_finder_image_rename(int16_t reference, uint32_t parent, const char* name, const char* target);
int16_t ROMlib_finder_image_check_mutable(int16_t reference, uint32_t parent, const char* name, uint32_t id);
int16_t ROMlib_finder_image_move(int16_t reference, uint32_t parent, const char* name, uint32_t id,
                                 uint32_t target_parent);
int16_t ROMlib_finder_image_flush(int16_t reference);
/* Root Icon<CR>, .VolumeIcon.icns or an older Desktop custom family. */
int ROMlib_finder_image_volume_icon(int16_t reference, FinderIcon* result);
/* Decode ICO/ICNS data-fork artwork without opening a second image connection. */
int ROMlib_finder_image_preview(int16_t reference, uint32_t parent, const char* name, unsigned char* rgba);
/* Read only icon resources; directory selects the folder's Icon<CR> file. */
int ROMlib_finder_image_icon(int16_t reference, uint32_t parent, const char* name, int directory, uint16_t flags,
                             uint32_t type, uint32_t creator, FinderIcon* result);
#ifdef __cplusplus
}
#endif
#endif
