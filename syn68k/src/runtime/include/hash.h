/*+===================================================================
  File:      hash.h

  Summary:   Declares hash support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in syn68k/src/runtime/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#ifndef _hash_h_
#define _hash_h_

#include "block.h"

#define LOG_NUM_BUCKETS 12
#define NUM_HASH_BUCKETS (1UL << LOG_NUM_BUCKETS)

/* This hash function assumes the low bit conveys no information (== 0). */
#define BLOCK_HASH(x) ((unsigned)((((x) ^ ((x) >> LOG_NUM_BUCKETS)) >> 1) % NUM_HASH_BUCKETS))

extern Block* block_hash_table[NUM_HASH_BUCKETS];

extern void hash_init(void);
extern void hash_destroy(void);
extern Block* hash_lookup(uint32 addr);
/* defined in `syn68k_public.h'
   extern const uint16 *hash_lookup_code_and_create_if_needed (uint32 addr); */
extern const uint16* hash_lookup_code_and_create_if_needed(uint32 addr);
extern void hash_insert(Block* b);
extern void hash_remove(Block* b);
#ifdef DEBUG
extern BOOL hash_verify(void);
extern void hash_stats(void);
#endif

#endif /* Not _hash_h_ */
