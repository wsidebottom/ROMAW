/*+===================================================================
  File:      desktop_database.h

  Summary:   Classic Desktop Manager disk records, independent of guest memory. ifndef ROMAW_DESKTOP_DATABASE_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Classic Desktop Manager disk records, independent of guest memory. */
#ifndef ROMAW_DESKTOP_DATABASE_H
#define ROMAW_DESKTOP_DATABASE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum desktop_operation {
    desktop_open,
    desktop_get,
    desktop_put,
    desktop_remove,
    desktop_index,
    desktop_reset,
    desktop_delete,
    desktop_info
};

/* Keys include their classic length byte. Values are classic big-endian
   Desktop records. Icon bitmaps live in Desktop DF at IconDRec.IconPos.
   Open returns whether BOTH files existed in *size. Index is one-based.
   Put/get of an icon passes its bitmap, rather than the ten-byte IconDRec;
   *tag contains the IconDRec tag. No pointers are retained. */
int romaw_desktop_database(const char* root, enum desktop_operation op, unsigned char key[38], unsigned index,
                           void* data, size_t* size, uint32_t* tag);
/* Source-backed read access. The callback reads exact bounded ranges from
   Desktop DB (file 0) or Desktop DF (file 1); no source bytes are retained. */
typedef int (*romaw_desktop_read)(void* opaque, unsigned file, uint64_t offset, void* data, size_t size);
int romaw_desktop_database_read(romaw_desktop_read read, void* opaque, uint64_t db_size, uint64_t df_size,
                                enum desktop_operation op, unsigned char key[38], unsigned index, void* data,
                                size_t* size, uint32_t* tag);
/* Publish a verified comment update with the old index available for recovery.
   The publisher owns the file handles and does not write Desktop DF. */
typedef int (*romaw_desktop_publish)(void* opaque, const void* tree, size_t size, const void* previous,
                                     size_t previous_size);
int romaw_desktop_database_comment(romaw_desktop_read read, romaw_desktop_publish publish, void* opaque, size_t db_size,
                                   uint32_t id, const void* text, size_t size);
#ifdef _WIN32
/* Native Finder paths are Unicode; legacy callers retain their existing API. */
int romaw_desktop_database_wide(const wchar_t* root, enum desktop_operation op, unsigned char key[38], unsigned index,
                                void* data, size_t* size, uint32_t* tag);
#endif
#ifdef __cplusplus
}
#endif
#endif
