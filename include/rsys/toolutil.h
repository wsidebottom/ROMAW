/*+===================================================================
  File:      toolutil.h

  Summary:   Declares toolutil support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__RSYS_TOOLUTIL__)
#define __RSYS_TOOLUTIL__

#include "SANE.h"

extern StringHandle ROMlib_phoney_name_string;

extern void unpack_int16_bits(HIDDEN_Ptr* sp, HIDDEN_Ptr* dp, INTEGER len);
extern trap void R_Fix2X(void* dummyretpc, Fixed x, extended80* ret);
extern trap void R_Frac2X(void* dummyretpc, Fract x, extended80* ret);

#endif /* !defined(__RSYS_TOOLUTIL__) */
