/*+===================================================================
  File:      syncint.h

  Summary:   Declares syncint support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_syncint_h_)
#define _syncint_h_

#if defined(SDL)
#define SDL_DEFINE_INIT_ONLY

#include "SDL/SDL.h"
#include "SDL/SDL_timer.h"

#undef SDL_DEFINE_INIT_ONLY
#endif /* SDL */

extern int syncint_init(void);
extern void syncint_post(unsigned long usecs);

#endif /* !_syncint_h_ */
