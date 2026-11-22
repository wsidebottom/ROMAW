/*+===================================================================
  File:      winfs.h

  Summary:   Declares winfs support for host-platform configuration and integration.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/config/os/cygwin32; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_winfs_h_)
#define _winfs_h_

/*
 * Copyright 1997 by Abacus Research and Development, Inc.
 * All rights reserved.
 */

extern int ROMlib_win_volume_bytes(const char* path, uint64* total, uint64* free_bytes);
extern int sync(void);
extern int fsync(int fd);

#endif /* !defined (_winfs_h_) */
