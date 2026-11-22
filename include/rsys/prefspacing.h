/*+===================================================================
  File:      prefspacing.h

  Summary:   Declares prefspacing support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#ifndef RSYS_PREFSPACING_H
#define RSYS_PREFSPACING_H
/* Insert missing blank lines before INI sections; preserve all existing bytes. */
int prefs_space_sections(const char* path);
#endif
