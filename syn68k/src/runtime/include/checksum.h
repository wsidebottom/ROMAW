/*+===================================================================
  File:      checksum.h

  Summary:   Declare and optionally inline translated-block checksums.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in syn68k/src/runtime/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#ifndef _checksum_h_
#define _checksum_h_

#include "syn68k_private.h"

#include "block.h"

extern uint32 compute_block_checksum(const Block* b);

#ifdef INLINE_CHECKSUM
#include "callback.h"

static __inline__ uint32 inline_compute_block_checksum(const Block* b)
{
    const uint16* code;
    uint32 sum;
    long n;
    syn68k_addr_t start;

    /* Don't actually checksum the bytes at a callback! */
    start = b->m68k_start_address;
    if (IS_CALLBACK(start))
        return (start + (uintptr_t)callback_argument(start) + (uintptr_t)callback_function(start));
    else if (start >= MAGIC_ADDRESS_BASE && start < CALLBACK_STUB_BASE)
        return 0;

    code = SYN68K_TO_US(start);
    for (sum = 0, n = b->m68k_code_length / sizeof(uint16); --n >= 0;) {
        /* Bizarre but fast checksum.  A good checksum like CRC is
       * probably too slow.
       */
        sum = (sum >> 14) ^ (sum * 33) ^ code[n];
    }

    return sum;
}
#endif /* INLINE_CHECKSUM */

#endif /* Not _checksum_h_ */
