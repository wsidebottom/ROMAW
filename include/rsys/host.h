/*+===================================================================
  File:      host.h

  Summary:   Declares host support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__HOST__)
#define __HOST__

#include "CQuickDraw.h"

extern int host_cursor_depth;

extern void host_set_cursor(char* cursor_data, unsigned short cursor_mask[16], int hotspot_x, int hotspot_y);
extern int host_set_cursor_visible(int show_p);
extern boolean_t host_hide_cursor_if_intersects(int top, int left, int bottom, int right);
extern void host_beep_at_user(void);
extern void host_flush_shadow_screen(void);

#endif /* !__HOST__ */
