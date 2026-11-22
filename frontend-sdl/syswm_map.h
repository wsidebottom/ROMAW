/*+===================================================================
  File:      syswm_map.h

  Summary:   Declares syswm map support for host-platform configuration and integration.

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

#if !defined(__syswm_map_h__)
#define __syswm_map_h__

extern int sdl_syswm_init(void);
extern int sdl_syswm_event(const SDL_Event* event);
extern int ROMlib_sdl_desk_shape(const SDL_Rect* rects, int count);

#endif
