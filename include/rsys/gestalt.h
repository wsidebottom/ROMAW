/*+===================================================================
  File:      gestalt.h

  Summary:   Declares gestalt support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_RSYS_GESTALT_H_)
#define _RSYS_GESTALT_H_

#if defined(USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES)
typedef uint32 OSType;
typedef uint32 OSErr;
#endif

enum {
    DONGLE_GESTALT = 0xb7d20e84,
};

extern void replace_physgestalt_selector(OSType selector, uint32 new_value);
extern void ROMlib_clear_gestalt_list(void);
extern OSErr ROMlib_gestalt_value(OSType, uint32, unsigned);
extern void ROMlib_add_to_gestalt_list(OSType selector, OSErr retval, uint32 new_value);

/* GhostScript DLL version number */
enum { gestaltGhostScriptVersion = T('g', 'o', 's', 't') };

#if !defined(USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES)
/*
 * Copyright 1996-1999 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: gestalt.h 63 2004-12-24 18:19:43Z ctm $
 */

/* ROMAW Version as string */
enum { gestaltROMAWVersionString = T('x', 'q', 't', 'r') };

/* screen size */
enum { gestaltScreenSize = T('s', 'c', 'r', 'n') };

enum { gestaltPHYSICAL = T('M', 'U', 'L', '8') };

enum { physicalUndefSelectorErr = -15551 };

extern OSErrRET C_PhysicalGestalt(OSType selector, LONGINT* responsep);

extern void gestalt_set_system_version(uint32 version);
extern void gestalt_set_memory_size(uint32 size);
extern int ROMlib_powerpc_cache_read(uint32 address, unsigned size, uint32* value);

#endif

#endif /* !_RSYS_GESTALT_H_ */
