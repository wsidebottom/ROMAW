/*+===================================================================
  File:      mkvol.h

  Summary:   Declares mkvol support for ROMAW build and development support.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/mkvol; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_MKVOL_H_)
#define _MKVOL_H_

/*
 * Copyright 1995 by Abacus Research and Development, Inc.
 * All rights reserved.
 */

typedef size_t (*write_funcp_t)(int user_arg, void* bufp, size_t buf_len);

extern int format_disk(unsigned long timevar, const char* volumename, int nsecs, write_funcp_t writefuncp,
                       int user_arg);
#endif
