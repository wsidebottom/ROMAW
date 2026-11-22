/*+===================================================================
  File:      uniquestring.h

  Summary:   ifndef __UNIQUESTRING_H__

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in syn68k/src/syngen/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#ifndef __UNIQUESTRING_H__
#define __UNIQUESTRING_H__

extern void init_unique_string(void);
extern const char* unique_string(const char* s);

#endif /* Not __UNIQUESTRING_H__ */
