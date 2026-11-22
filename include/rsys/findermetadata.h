/*+===================================================================
  File:      findermetadata.h

  Summary:   Declares findermetadata support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#ifndef ROMAW_FINDERMETADATA_H
#define ROMAW_FINDERMETADATA_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Guest wire format: five big-endian longs, or name[256] and three longs.
   Actions: read/write window, read/write layout, flush pending writes. */
int romaw_finder_metadata(unsigned action, const char* path, void* data, unsigned count);
int romaw_finder_bundle_icon_id(const unsigned char* bytes, size_t size, unsigned creator, unsigned local, int* id);
#ifdef __cplusplus
}
#endif
#endif
