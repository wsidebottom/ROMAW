/*+===================================================================
  File:      alloc.h

  Summary:   ifndef _alloc_h_

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in syn68k/src/runtime/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#ifndef _alloc_h_
#define _alloc_h_

#include <stddef.h> /* typedef size_t */

extern void* xmalloc(size_t size);
extern void* xrealloc(void* old, size_t new_size);
extern void* xcalloc(size_t num_elems, size_t byte_size);
extern void xmake_executable(const void* p, size_t size);

#endif /* Not _alloc_h_ */
