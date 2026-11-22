/*+===================================================================
  File:      hfs_metadata.c

  Summary:   Implements hfs metadata support for disk images package support.

  Classes:   None.

  Functions: romaw_hfs_allocate, romaw_hfs_finder_info

  Origin:    Existing repository source in packages/disk-images/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Preserve all 32 FinderInfo bytes through libhfs's catalog serializer. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "../third_party/hfsutils/libhfs.h"
#include "../third_party/hfsutils/record.h"
#include "../third_party/hfsutils/volume.h"
#include "../third_party/hfsutils/file.h"
#include <stdint.h>
static _Thread_local uint64_t mapped_block;

static int capture_block(hfsvol* volume, unsigned int allocation, unsigned int number, block* buffer)
{
    (void)buffer;
    mapped_block = ((uint64_t)volume->vstart + volume->mdb.drAlBlSt +
                    (uint64_t)allocation * volume->mdb.drAlBlkSiz / 512 + number) *
                   512;
    return 0;
}

int romaw_hfs_allocate(hfsfile* file, unsigned long size, int (*map)(void*, uint64_t, uint64_t, size_t), void* opaque)
{
    unsigned long *logical, *physical;
    f_getptrs(file, 0, &logical, &physical);
    file->cat.u.fil.filClpSize = file->vol->mdb.drAlBlkSiz;
    while (*physical < size)
        if (f_alloc(file) <= 0)
            return -1;
    *logical = size;
    file->flags |= HFS_FILE_UPDATE_CATREC;
    for (uint64_t offset = 0; offset < size; offset += file->vol->mdb.drAlBlkSiz) {
        block buffer;
        size_t n = size - offset < file->vol->mdb.drAlBlkSiz ? (size_t)(size - offset) : file->vol->mdb.drAlBlkSiz;
        if (f_doblock(file, (unsigned long)(offset / 512), &buffer, capture_block) ||
            !map(opaque, mapped_block, offset, n))
            return -1;
    }
    return 0;
}

int romaw_hfs_finder_info(hfsvol* volume, const char* path, const unsigned char info[32])
{
    CatDataRec record;
    node location;
    unsigned char packed[512];
    unsigned int size;
    if (v_resolve(&volume, path, &record, 0, 0, &location) <= 0)
        return -1;
    if (volume->flags & HFS_VOL_READONLY)
        return -1;
    r_packcatdata(&record, packed, &size);
    if (record.cdrType == cdrDirRec)
        memcpy(packed + 22, info, 32);
    else if (record.cdrType == cdrFilRec) {
        memcpy(packed + 4, info, 16);
        memcpy(packed + 56, info + 16, 16);
    }
    else
        return -1;
    r_unpackcatdata(packed, &record);
    return v_putcatrec(&record, &location);
}
