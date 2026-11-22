/*+===================================================================
  File:      destroyblock.h

  Summary:   ifndef _destroyblock_h_

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in syn68k/src/runtime/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#ifndef _destroyblock_h_
#define _destroyblock_h_

#include "block.h"

extern unsigned long destroy_block(Block* b);
extern unsigned long destroy_blocks(syn68k_addr_t low_m68k_address, uint32 num_bytes);
extern unsigned long destroy_any_block(void);

#endif /* Not _destroyblock_h_ */
