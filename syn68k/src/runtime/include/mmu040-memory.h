/*+===================================================================
  File:      mmu040-memory.h

  Summary:   Declares mmu040 memory support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in syn68k/src/runtime/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SYN68K_MMU040_MEMORY_H
#define SYN68K_MMU040_MEMORY_H

#include "mmu040.h"

/* Operand boundary with optional aligned uncached transfers, not a cache or
   bus-timing implementation. Callbacks
   transfer big-endian bytes and receive translation attributes and the request's
   function code, including bypass spaces. They must
   either complete a whole chunk or fail without publishing any of that chunk.
   Descriptor accesses remain separate and always use physical addresses. */
typedef struct {
    Syn68kMMU040* mmu;
    Syn68kMMU040Bus tables;
    void* opaque;
    int (*read)(void* opaque, uint32_t physical, unsigned char* bytes, unsigned size, uint32_t attributes, unsigned fc);
    int (*write)(void* opaque, uint32_t physical, const unsigned char* bytes, unsigned size, uint32_t attributes,
                 unsigned fc);
    const struct Syn68kMMU040Native* native;
    /* Opt-in uncached bus boundary: naturally aligned byte/word/longword
       transfers, including multiple transfers within a page. Zero retains
       the operand callback boundary for cache/device implementations. */
    unsigned aligned_transfers;
} Syn68kMMU040Memory;

typedef struct {
    Syn68kMMU040Result translation;
    /* Address of the current chunk, including a failed translation or transfer.
     completed counts only successful operand bytes, not descriptor traffic. */
    uint32_t logical;
    unsigned completed;
    int bus_error;
    /* Original scalar request. value is the width-limited write operand or the
     successfully read byte prefix (in the low bits, in big-endian order). */
    uint32_t request, value;
    unsigned size, fc, writing, rmw;
} Syn68kMMU040Access;

/* Optional permanent, nonwrapping ordinary physical RAM. The owner retains
   both this storage and the native dispatch table throughout a bus invocation. */
typedef struct {
    unsigned char* bytes;
    uint32_t address;
    uint64_t size;
} Syn68kMMU040RAM;

typedef struct Syn68kMMU040Native {
    Syn68kMMU040Access (*read)(Syn68kMMU040Memory*, uint32_t, unsigned, unsigned, int, uint32_t*);
    Syn68kMMU040Access (*write)(Syn68kMMU040Memory*, uint32_t, unsigned, unsigned, uint32_t);
    Syn68kMMU040Access (*write_rmw)(Syn68kMMU040Memory*, uint32_t, unsigned, unsigned, uint32_t);
    Syn68kMMU040RAM ram;
    /* Instruction-local tracking, including RAM writes that bypass callbacks. */
    unsigned* write_attempted;
    /* Internal native flat execution bypasses translation through this live
       binding. Ordinary mapped buses leave it NULL. */
    const struct Syn68kNative040FlatState* flat;
    /* Optional translated-page slow path after a successful flat store.
       pages has one counter per 2^flat_page_shift guest bytes. */
    const uint32_t* flat_code_pages;
    unsigned flat_page_shift;
    void (*flat_write)(uint32_t address, unsigned size);
} Syn68kMMU040Native;

/* Byte, word and longword operands; addresses wrap modulo 2^32. Chunks stop at
   page boundaries, including when adjacent pages map to disjoint host ranges.
   aligned_transfers == 1 additionally splits at naturally aligned physical
   bus widths; only zero and one are valid. This does not model cache fills.
   A later failure preserves previous writes and descriptor U/M updates.
   Read output is published only on complete success. rmw != 0 checks write
   permission and updates M during the read portion; atomicity across the read
   and write portions must be provided by the embedding CPU/bus implementation.
   These functions do not deliver CPU exceptions or construct restart frames. */
Syn68kMMU040Access syn68k_mmu040_read(Syn68kMMU040Memory* memory, uint32_t logical, unsigned fc, unsigned size, int rmw,
                                      uint32_t* value);
Syn68kMMU040Access syn68k_mmu040_write(Syn68kMMU040Memory* memory, uint32_t logical, unsigned fc, unsigned size,
                                       uint32_t value);
Syn68kMMU040Access syn68k_mmu040_write_rmw(Syn68kMMU040Memory* memory, uint32_t logical, unsigned fc, unsigned size,
                                           uint32_t value);
/* Basic format-7 SSW transfer fields from MC68040UM 8.4.6.2. Supplies MA, ATC,
   LK, RW, SIZE and TT/TM, including alternate spaces; leaves continuation/trace/cache-push
   state to the CPU. Alternate-space ATC records are inconsistent and rejected.
   Returns zero (preserving *ssw) for success/bad requests.
   This is not a complete exception frame or restart/writeback protocol. */
int syn68k_mmu040_access_ssw(const Syn68kMMU040Access* access, uint16_t* ssw);

#endif
