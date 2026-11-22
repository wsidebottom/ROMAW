/*+===================================================================
  File:      checksum.c

  Summary:   Implements checksum support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: compute_block_checksum

  Origin:    Existing repository source in syn68k/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#define INLINE_CHECKSUM
#include "checksum.h"

uint32 compute_block_checksum(const Block* b)
{
    return inline_compute_block_checksum(b);
}
