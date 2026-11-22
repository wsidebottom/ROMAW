/*+===================================================================
  File:      syswm_vars.h

  Summary:   Declares syswm vars support for host-platform configuration and integration.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/config/front-ends/sdl; original attribution and provenance
             comments are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* 
 * Copyright 1998 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#if !defined(__syswm_vars_h__)
#define __syswm_vars_h__

#if defined(_WIN32) && !defined(WIN32)
#define WIN32
#endif

#include "SDL/SDL.h"
#include "SDL/SDL_syswm.h"

/* System dependent variables */
/* * */
extern HWND SDL_Window;

#endif
