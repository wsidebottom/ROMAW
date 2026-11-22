/*+===================================================================
  File:      diagnostics.h

  Summary:   ifndef _diagnostics_h_

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in syn68k/src/runtime/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#ifndef _diagnostics_h_
#define _diagnostics_h_

#include <stdio.h>
#include "syn68k_private.h"
#include "block.h"

extern void print_cc_bits(FILE* stream, int bits);
extern void hexdump(const uint16* addr, int num_words);
extern void dump_cpu_state(void);
extern Block* which_block(uint16* code);

#endif /* Not _diagnostics_h_ */
