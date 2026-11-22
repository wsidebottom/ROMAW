/*+===================================================================
  File:      hosttrash.h

  Summary:   Declares hosttrash support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#ifndef RSYS_HOSTTRASH_H
#define RSYS_HOSTTRASH_H
/* 0: supported, 1: open, 2: recycle paths together, 3: empty with host UI.
   A recycle failure must never fall back to permanent deletion. */
int ROMlib_host_trash(unsigned action, unsigned count, const char* const* paths);
#endif
