/*+===================================================================
  File:      trapname.h

  Summary:   Declares trapname support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_RSYS_TRAPNAME_H_)
#define _RSYS_TRAPNAME_H_

#if !defined(NDEBUG)
extern const char* trap_name(int trapno);
extern const char* trap_name_array[0x1000];
#endif

#endif /* !_RSYS_TRAPNAME_H_ */
