/*+===================================================================
  File:      win_print.h

  Summary:   Declares win print support for host-platform configuration and integration.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/config/os/cygwin32; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__WIN_PRINT_H__)
#define __WIN_PRINT_H__

typedef enum {
    WIN_PRINT_PORTRAIT,
    WIN_PRINT_LANDSCAPE,
} orientation_t;

typedef struct win_print_str* win_printp_t;

extern boolean_t get_info(win_printp_t* wpp, int physx, int physy, orientation_t orientation, int copies,
                          uint32* last_errorp);

#define INCHES(x) ((x) * 72)
#define MMETERS(x) ((x) * 2.835)

#endif
