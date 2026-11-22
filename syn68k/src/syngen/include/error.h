/*+===================================================================
  File:      error.h

  Summary:   ifndef _error_h_

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in syn68k/src/syngen/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#ifndef _error_h_
#define _error_h_

#include "parse.h"

extern void error(const char* fmt, ...);
#if defined(__GNUC__) && !defined(__STRICT_ANSI__)
extern volatile void fatal_error(const char* fmt, ...);
extern volatile void fatal_parse_error(const List* ls, const char* fmt, ...);
extern volatile void fatal_input_error(const char* fmt, ...);
#else
extern void fatal_error(const char* fmt, ...);
extern void fatal_parse_error(const List* ls, const char* fmt, ...);
extern void fatal_input_error(const char* fmt, ...);
#endif
extern void parse_error(const List* ls, const char* fmt, ...);
extern void input_error(const char* fmt, ...);

#endif /* Not _error_h_ */
