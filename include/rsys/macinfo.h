/*+===================================================================
  File:      macinfo.h

  Summary:   Declares macinfo support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#ifndef RSYS_MACINFO_H
#define RSYS_MACINFO_H
#include <stddef.h>
#include <stdint.h>
/* Retained for guest helpers querying standalone Get Info readiness. */
int ROMlib_mac_info_control(unsigned action);
int ROMlib_mac_info_request(const char* path);
int ROMlib_mac_info_take(char* out, size_t capacity);
#endif
