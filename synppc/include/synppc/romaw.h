/*+===================================================================
  File:      romaw.h

  Summary:   ROMAW-specific launch interface; requires its Macintosh Toolbox types. ifndef SYN_PPC_ROMAW_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in synppc/include/synppc; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* ROMAW-specific launch interface; requires its Macintosh Toolbox types. */
#ifndef SYN_PPC_ROMAW_H
#define SYN_PPC_ROMAW_H
#include <stdint.h>
#include "FileMgr.h"
#ifdef __cplusplus
extern "C" {
#endif
int ROMlib_launch_ppc_fragment(FSSpecPtr, uint32_t, uint32_t);
int ROMlib_launch_cfm68k_fragment(FSSpecPtr, uint32_t, uint32_t);
int ROMlib_launch_cfm68k_resource(FSSpecPtr);
void ROMlib_cfm68k_shutdown(void);
void ROMlib_ppc_begin_launch(void);
int ROMlib_ppc_request_system(uint32_t version);
#ifdef __cplusplus
}
#endif
#endif
