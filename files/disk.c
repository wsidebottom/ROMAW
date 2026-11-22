/*+===================================================================
  File:      disk.c

  Summary:   Implements disk support for Macintosh File Manager and host-file operations.

  Classes:   None.

  Functions: DiskEject, DriveStatus, SetTagBuffer

  Origin:    Existing repository source in executor/src/files; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986, 1988, 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in Disk.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "Disk.h"

A1(PUBLIC, OSErr, DiskEject, INTEGER, rn)
{
    return paramErr;
}

A1(PUBLIC, OSErr, SetTagBuffer, Ptr, bp)
{
    return paramErr;
}

A2(PUBLIC, OSErr, DriveStatus, INTEGER, dn, DrvSts*, statp)
{
    return paramErr;
}
