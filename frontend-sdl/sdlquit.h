/*+===================================================================
  File:      sdlquit.h

  Summary:   Declares sdlquit support for host-platform configuration and integration.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/config/front-ends/sdl; original attribution and provenance
             comments are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_SDLQUIT_H_)
#define _SDLQUIT_H_

/*
 * Copyright 1998 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: sdlquit.h 63 2004-12-24 18:19:43Z ctm $
 */

extern int sdl_really_quit(void);
extern int sdl_close_is_quit(void);

#endif
