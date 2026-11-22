/*+===================================================================
  File:      hostcatalog.h

  Summary:   Declares hostcatalog support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#ifndef FINDER_HOSTCATALOG_H
#define FINDER_HOSTCATALOG_H
/* Private exMD operations. All numeric fields are big endian; names are
   NUL-terminated MacRoman. Fixed sizes keep the host/guest ABI independent of
   compiler packing and native pointer sizes. */
#define FINDER_HOST_CATALOG_ACTION 6
#define FINDER_HOST_ICON_ACTION 7
#define FINDER_HOST_CATALOG_LIMIT 32
#define FINDER_HOST_CATALOG_RECORD 292
#define FINDER_HOST_CATALOG_BYTES (8 + FINDER_HOST_CATALOG_LIMIT * FINDER_HOST_CATALOG_RECORD)
#define FINDER_HOST_ICON_BYTES 2252
#endif
