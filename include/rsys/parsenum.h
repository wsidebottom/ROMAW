/*+===================================================================
  File:      parsenum.h

  Summary:   Declares parsenum support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_RSYS_PARSENUM_H_)
#define _RSYS_PARSENUM_H_

extern boolean_t parse_number(const char* orig_num, int32* val, unsigned round_up_to_multiple_of);

#endif /* !_RSYS_PARSENUM_H_ */
