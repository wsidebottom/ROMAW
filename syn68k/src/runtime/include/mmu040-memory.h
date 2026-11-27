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
#include "cache040.h"

/* Optional physical cache binding. The owner retains all pointers. NULL bus
   uses the ordinary operand callbacks for fills/pushes; descriptor operands
   always retain the separate table callbacks and their atomic U-bit update.
   CACR is sampled live for each cache operation. */
typedef struct {
    Syn68kCache040* state;
    const Syn68kCache040Bus* bus;
    const uint32_t* cacr;
    enum Syn68kCache040Traffic traffic;
    Syn68kCache040Interrupt interrupt;
} Syn68kMMU040Cache;

typedef struct Syn68kMMU040Access Syn68kMMU040Access;

/* Optional CPU-owned exception boundary. Component buses leave this NULL.
   The live callback checks the instruction-end page before posting a new
   forced exception. A nonzero return replaces it with the supplied access
   fault at pc. Neither this object nor its opaque pointer may escape the step. */
typedef struct {
    uint32_t pc;
    void* opaque;
    int (*page_fault)(void*, uint32_t next, Syn68kMMU040Access*);
} Syn68kMMU040Prefetch;

/* Operand boundary with an optional physical cache binding. Raw callbacks
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
       the operand callback boundary for cache/device implementations. The
       cache binding owns physical decomposition when present. */
    unsigned aligned_transfers;
    const Syn68kMMU040Cache* cache;
    const Syn68kMMU040Prefetch* prefetch;
} Syn68kMMU040Memory;

struct Syn68kMMU040Access {
    Syn68kMMU040Result translation;
    /* Address of the current chunk, including a failed translation or transfer.
       completed counts successful operand bytes of the current pass, excluding
       descriptor traffic and the discarded first-page pass of a crossing
       operand. Successful writes in that initial pass remain externally visible
       even when replay fails before completing the same bytes again. */
    uint32_t logical;
    unsigned completed;
    int bus_error;
    /* Original scalar request. value is the width-limited write operand or the
     successfully read byte prefix (in the low bits, in big-endian order). */
    uint32_t request, value;
    unsigned size, fc, writing, rmw;
    /* Physical cache failure, independent of the logical operand/translation.
       cache_table distinguishes table-search traffic. A copyback operand can
       already be complete when its old-victim push fails. */
    Syn68kCache040Access cache;
    unsigned cache_table;
    /* Unlike an ordinary failed table callback, this cannot be swallowed by
       diagnostic PTEST. Operand bus failures also set this flag. */
    unsigned cache_exception;
};

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
    Syn68kMMU040Access (*move16)(Syn68kMMU040Memory*, uint32_t, unsigned, int, unsigned char[16]);
    Syn68kCache040Snoop (*snoop)(Syn68kMMU040Memory*, uint32_t, unsigned, unsigned, unsigned, int, unsigned char[16]);
} Syn68kMMU040Native;

/* Byte, word and longword operands; addresses wrap modulo 2^32. Chunks stop at
   page boundaries, including when adjacent pages map to disjoint host ranges.
   aligned_transfers == 1 additionally splits at naturally aligned physical
   bus widths when cache is NULL; only zero and one are valid. A cache binding
   instead owns fills, physical beats and precise cache-fault metadata.
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
/* MOVE16 line half with one latched translation/FC/attribute set. bytes use
   memory-address order. Read output is unchanged until all four beats succeed;
   write input is captured before translation callbacks. Success reports a
   complete size/completed=16 line. On failure, the scalar access describes the
   failing longword (size=4, completed=0); cache.completed retains burst progress.
   cache.cycle has the last bus beat; burst=0 on a successful cache-hit read. */
Syn68kMMU040Access syn68k_mmu040_move16(Syn68kMMU040Memory*, uint32_t logical, unsigned fc, int writing,
                                        unsigned char bytes[16]);
/* Board-side physical snoop, independently of MMU enable, TTRs and CACR.
   This consumes an accepted external transaction; it never invokes descriptor
   or operand callbacks. Both physical tag banks obey their snoop transitions.
   The embedding supplies the memory response when intervention is clear. */
Syn68kCache040Snoop syn68k_mmu040_snoop(Syn68kMMU040Memory*, uint32_t physical, unsigned control,
                                        unsigned transfer_type, unsigned size, int writing, unsigned char bytes[16]);
/* Cache-aware translation/PTEST without an operand transfer. Ordinary PTEST
   diagnostic failures update MMUSR; physical cache failures preserve it and
   remain visible in the returned cache result. */
Syn68kMMU040Access syn68k_mmu040_memory_translate(Syn68kMMU040Memory*, uint32_t logical, unsigned fc, int writing,
                                                  int ptest);
/* Physical CINV/CPUSH selection through the optional cache binding. Addresses
   bypass translation. A push failure retains its physical cycle and line;
   it must not be interpreted as an ordinary logical operand writeback. */
Syn68kMMU040Access syn68k_mmu040_cache_maintain(Syn68kMMU040Memory*, unsigned caches, unsigned scope, uint32_t physical,
                                                int push);
/* Physical push status is separate from a scalar operand. FA/WB1A use
   cache.push_address, and all sixteen push_data bytes remain valid. */
int syn68k_mmu040_cache_push_ssw(const Syn68kMMU040Access*, uint16_t* ssw);
/* Basic format-7 transfer fields: MA, ATC, LK, RW, SIZE and TT/TM. A read-miss
   fill retains its original operand size and logical FA. Alternate-space ATC
   records, pushes and lockups require a distinct path. A copyback allocation
   fill preserves the original write status; its pending operand uses WB2,
   not WB1, independently of the internal read beat that failed.
   Returns zero without changing *ssw for success or unsupported metadata.
   CPU retirement and continuation/writeback certification remain separate. */
int syn68k_mmu040_access_ssw(const Syn68kMMU040Access* access, uint16_t* ssw);
/* Logical operand fields for retirement/WB2/WB3 validation, including the
   operand that triggered a physical push. A push's actual stack SSW/FA/PD
   must still come from its separate cache snapshot. */
int syn68k_mmu040_operand_status(const Syn68kMMU040Access*, uint16_t* ssw);

#endif
