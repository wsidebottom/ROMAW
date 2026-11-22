/*+===================================================================
  File:      backpatch.h

  Summary:   ifndef _backpatch_h_

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in syn68k/src/runtime/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#ifndef _backpatch_h_
#define _backpatch_h_

typedef struct _backpatch_t {
    struct _backpatch_t* next; /* Next in linked list.                  */
    int32 offset_location;     /* Bit index from block to index start.  */
    int32 const_offset;        /* Add this to src addr.                 */
    int8 num_bits;             /* Number of bits to be patched.         */
    int8 relative_p;           /* Boolean: relative offset?             */
    struct _Block* target;     /* Target block; we jmp to its code ptr. */
} backpatch_t;

extern void backpatch_apply_and_free(struct _Block* b, backpatch_t* p);
extern void backpatch_add(struct _Block* b, int offset_location, int num_bits, BOOL relative_p, int const_offset,
                          struct _Block* target);

#include "block.h"

#endif /* !_backpatch_h_ */
