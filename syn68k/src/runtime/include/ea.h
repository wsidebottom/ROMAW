/*+===================================================================
  File:      ea.h

  Summary:   Declares ea support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in syn68k/src/runtime/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SYN68K_EA_H
#define SYN68K_EA_H
#include "syn68k_public.h"
#include "mmu040-memory.h"

typedef struct {
    uint32 pc, fault_address;
    /* 2: memory fault; 4: reserved index; 11: unsupported addressing mode. */
    unsigned error;
    /* NULL preserves the flat runtime path. Explicit contexts can use mapped
     accesses while global guest MMU enable remains gated. */
    Syn68kMMU040Memory* memory;
    Syn68kMMU040Access access;
    /* MOVEM CM restart: decode live extension lengths without repeating an
     indexed pointer read, and use the frame's already calculated address. */
    const uint32* resume_ea;
} Syn68kEAContext;

typedef struct {
    uint32 address;
    int reg;
    unsigned instruction; /* Immediate source, not PC-relative data. */
} Syn68kEA;

Syn68kEA syn68k_effective_address(Syn68kEAContext* context, unsigned code, unsigned size, int writing, int registers);
/* Memory helpers accept byte, word or longword transfers only. */
uint32 syn68k_ea_read_memory(Syn68kEAContext* context, uint32 address, unsigned size);
uint32 syn68k_ea_fetch_memory(Syn68kEAContext* context, uint32 address, unsigned size);
uint32 syn68k_ea_read_operand(Syn68kEAContext* context, Syn68kEA ea, unsigned size);
/* Explicit FC and RMW intent for alternate-function-code/locked operands.
   MOVES must normalize FC 2/6 to 1/5 before calling these functions. */
uint32 syn68k_ea_read_memory_fc(Syn68kEAContext* context, uint32 address, unsigned size, unsigned fc, int rmw);
void syn68k_ea_write_memory_fc(Syn68kEAContext* context, uint32 address, unsigned size, uint32 value, unsigned fc);
void syn68k_ea_write_memory(Syn68kEAContext* context, uint32 address, unsigned size, uint32 value);
#endif
