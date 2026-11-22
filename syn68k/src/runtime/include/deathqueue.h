/*+===================================================================
  File:      deathqueue.h

  Summary:   ifndef _deathqueue_h_

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in syn68k/src/runtime/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#ifndef _deathqueue_h_
#define _deathqueue_h_

#include "block.h"

extern Block *death_queue_head, *death_queue_tail;
extern void death_queue_enqueue(Block* b);
extern void death_queue_dequeue(Block* b);

#endif /* Not _deathqueue_h_ */
