/*+===================================================================
  File:      rangetree.h

  Summary:   ifndef _rangetree_h

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in syn68k/src/runtime/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#ifndef _rangetree_h
#define _rangetree_h

#include "syn68k_private.h"
#include "block.h"

extern void range_tree_init(void);
extern void range_tree_destroy(void);
extern void range_tree_insert(Block* b);
extern void range_tree_remove(Block* b);
extern Block* range_tree_lookup(syn68k_addr_t h);
extern Block* range_tree_find_first_at_or_after(syn68k_addr_t addr);
extern Block* range_tree_first_to_intersect(syn68k_addr_t low, syn68k_addr_t high);
#ifdef DEBUG
extern BOOL range_tree_verify(void);
extern void range_tree_dump(void);
#endif

#endif /* Not _rangetree_h_ */
