/*+===================================================================
  File:      translate.h

  Summary:   ifndef _translate_h

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in syn68k/src/runtime/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#ifndef _translate_h
#define _translate_h_

#include "block.h"

extern void (*call_while_busy_func)(int);

typedef struct {
    BOOL valid;
    BOOL reversed;
    const uint16* m68koperand;
    int amode;
    int size;
} AmodeFetchInfo;

extern int generate_block(Block* parent, uint32 m68k_address, Block** new);
extern Block* make_artificial_block(Block* parent, syn68k_addr_t m68k_address, int extra_words, uint16** extra_start);

extern int emulation_depth;

#endif /* Not _translate_h_ */
