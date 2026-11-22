/*+===================================================================
  File:      sdl_mem.h

  Summary:   Derived from public domain source code written by Sam Lantinga Separate the memory management routines
             because they don't compile with USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES enabled

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

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

extern char* sdl_ReallocHandle(char** mem, int len);
