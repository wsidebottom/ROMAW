/*+===================================================================
  File:      hfs_medium.h

  Summary:   Declares hfs medium support for disk images package support.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in packages/disk-images/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ROMAW_HFS_MEDIUM_H
#define ROMAW_HFS_MEDIUM_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef int (*romaw_hfs_io)(void*, uint64_t, void*, size_t, int);
void romaw_hfs_ram(uint64_t, romaw_hfs_io, void*);
#ifdef __cplusplus
}
#endif
#endif
