/*+===================================================================
  File:      diskimage.h

  Summary:   Declares diskimage support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#ifndef RSYS_DISKIMAGE_H
#define RSYS_DISKIMAGE_H
#include <stdint.h>
#include <stdio.h>
#define ROMAW_IMAGE_VOLUMES 32

typedef struct {
    unsigned count;
    uint32_t offset[ROMAW_IMAGE_VOLUMES];
} romaw_image_plan;

/* Probe raw HFS, DiskCopy 4.2 and Apple partition maps. Never writes input.
   The legacy HFS engine uses signed 32-bit byte offsets. */
int romaw_image_probe(FILE* file, romaw_image_plan* plan);
#endif
