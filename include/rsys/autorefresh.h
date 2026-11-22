/*+===================================================================
  File:      autorefresh.h

  Summary:   Declares autorefresh support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_RSYS_SCREENCHECKSUM_H_)
#define _RSYS_SCREENCHECKSUM_H_

/* Milliseconds between checks for refresh mode. */
#define AUTOREFRESH_CHECK_MSECS 1000

#define NUM_AUTOREFRESH_STRIPS 8U

extern boolean_t do_autorefresh_p;
extern void note_romaw_changed_screen(int top, int bottom);
extern boolean_t autodetect_refresh(void);

#endif /* !_RSYS_SCREENCHECKSUM_H_ */
