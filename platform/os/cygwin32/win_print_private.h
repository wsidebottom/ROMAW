/*+===================================================================
  File:      win_print_private.h

  Summary:   All the information in a win_print_info_t can be derived from the return of a PrintDlg call

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    All the information in a win_print_info_t can be derived from

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* All the information in a win_print_info_t can be derived from
   the return of a PrintDlg call */

typedef struct {
    HDC hDC;
    int hres;   /* dots per inch */
    int vres;   /* dots per inch */
    int printx; /* dots */
    int printy; /* dots */
    int physx;  /* points */
    int physy;  /* points */
    short orientation;
    float scale;
    short copies;
    int rowwidth;
    int nbytes;
} win_print_info_t;

typedef struct win_print_str {
    win_print_info_t info;
} win_print_t;
