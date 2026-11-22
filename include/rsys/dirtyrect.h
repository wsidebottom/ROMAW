/*+===================================================================
  File:      dirtyrect.h

  Summary:   Declares dirtyrect support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_DIRTYRECT_H_)
#define _DIRTYRECT_H_

#include "host_bltmacros.h"

extern void dirty_rect_accrue(int top, int left, int bottom, int right);
extern void dirty_rect_update_screen(void);
extern boolean_t dirty_rect_subsumed_p(int top, int left, int bottom, int right);

extern int num_dirty_rects;

#endif /* !_DIRTYRECT_H_ */
