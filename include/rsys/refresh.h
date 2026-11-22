/*+===================================================================
  File:      refresh.h

  Summary:   Declares refresh support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_refresh_h_)
#define _refresh_h_

extern void set_refresh_rate(int new);
extern void dequeue_refresh_task(void);
extern void C_handle_refresh(void);
extern BOOLEAN find_changed_rect_and_update_shadow(const uint32* screen, uint32* shadow, long row_longs, long num_rows,
                                                   int* top_long, int* left_long, int* bottom_long, int* right_long);

#endif /* !_refresh_h_ */
