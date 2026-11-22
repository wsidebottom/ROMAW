/*+===================================================================
  File:      SDL_bmp.h

  Summary:   Declares sdl bmp support for host-platform configuration and integration.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/config/front-ends/sdl; original attribution and provenance
             comments are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_SDL_BMP_H_)
#define _SDL_BMP_H_

/*
 * Copyright 1998 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: SDL_bmp.h 63 2004-12-24 18:19:43Z ctm $
 */

extern int SDL_SaveCF_DIB(SDL_Surface* surfp, char** dib_bytesp, size_t* dib_lenp);

extern SDL_Surface* SDL_LoadCF_DIB(void* mem, size_t length);

#endif
