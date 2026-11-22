/*+===================================================================
  File:      profile.h

  Summary:   ifndef _profile_h_

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in syn68k/src/runtime/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#ifndef _profile_h_
#define _profile_h_

#ifdef PROFILE

#include "block.h"

extern void profile_block(Block* b);
extern void dump_profile(const char* file);

#endif

#endif /* Not _profile_h_ */
