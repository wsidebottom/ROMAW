/*+===================================================================
  File:      sdl_mem.c

  Summary:   Derived from public domain source code written by Sam Lantinga Separate the memory management routines
             because they don't compile with USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES enabled

  Classes:   None.

  Functions: sdl_ReallocHandle

  Origin:    Derived from public domain source code written by Sam Lantinga

  Copyright and Legal notices: Original public-domain and warranty notices are retained below.
===================================================================+*/

/*
 * Copyright 1998 by Abacus Research and
 * Development, Inc.  All rights reserved.
 *
 * Derived from public domain source code written by Sam Lantinga
 */

/* Separate the memory management routines because they don't compile with
   USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES enabled
*/

#include "rsys/common.h"
#include "MemoryMgr.h"

char* sdl_ReallocHandle(char** mem, int len)
{
    ReallocHandle((Handle)mem, len);
    if (MemErr != noErr)
        return NULL;
    else
        return (char*)STARH((Handle)mem);
}
