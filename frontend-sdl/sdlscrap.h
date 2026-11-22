/*+===================================================================
  File:      sdlscrap.h

  Summary:   Derived from public domain source code written by Sam Lantinga

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

#if !defined(__sdlscrap_h__)
#define __sdlscrap_h__

#include "SDL/SDL.h"

extern SDL_Surface* surface_from_dib(void* lp, size_t length);

#endif
