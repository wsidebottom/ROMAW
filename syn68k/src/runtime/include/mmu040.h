/*+===================================================================
  File:      mmu040.h

  Summary:   Declares mmu040 support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in syn68k/src/runtime/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SYN68K_MMU040_H
#define SYN68K_MMU040_H

#include <stdint.h>

/* Internal translation engine; guest execution is not yet routed through it.
   MC68040UM sections 3.1-3.7 are the architectural reference. */
enum Syn68kMMU040Control {
    MMU040_TC,
    MMU040_URP,
    MMU040_SRP,
    MMU040_ITT0,
    MMU040_ITT1,
    MMU040_DTT0,
    MMU040_DTT1,
    MMU040_MMUSR
};

enum Syn68kMMU040Fault {
    MMU040_OK,
    MMU040_INVALID,
    MMU040_TABLE_BUS_ERROR,
    MMU040_WRITE_PROTECTED,
    MMU040_SUPERVISOR_ONLY,
    MMU040_BAD_REQUEST
};

typedef struct {
    void* opaque;
    /* Host-order longwords at physical bus addresses. Return zero on failure. */
    int (*read32)(void* opaque, uint32_t address, uint32_t* value);
    /* locked != 0 requests an indivisible read/OR/write of the U bit; value
     is the OR mask. Otherwise value is a complete descriptor write. The
     embedding bus owns synchronization and must preserve failed writes. */
    int (*update32)(void* opaque, uint32_t address, uint32_t value, int locked);
} Syn68kMMU040Bus;

typedef struct {
    uint32_t physical, status, descriptor_address, attributes;
    enum Syn68kMMU040Fault fault;
    unsigned transparent, cached;
} Syn68kMMU040Result;

typedef struct {
    uint32_t logical_page, physical_page, status, descriptor_address;
    enum Syn68kMMU040Fault fault;
    unsigned valid, supervisor, shift;
} Syn68kMMU040Entry;

typedef struct {
    uint32_t tc, urp, srp, itt[2], dtt[2], mmusr;
    Syn68kMMU040Entry atc[2][16][4]; /* Data, instruction. */
    unsigned replacement[2];
} Syn68kMMU040;

void syn68k_mmu040_init(Syn68kMMU040* mmu);
/* Hardware reset disables translation without flushing ATCs or clearing P. */
void syn68k_mmu040_reset(Syn68kMMU040* mmu);
int syn68k_mmu040_control(Syn68kMMU040* mmu, enum Syn68kMMU040Control control, int writing, uint32_t* value);
/* FC 1/2/5/6 are ordinary data/instruction accesses. Other three-bit FCs
   bypass translation. MOVES FC conversion is the caller's responsibility.
   A read-modify-write operand must pass writing != 0 from its read portion. */
Syn68kMMU040Result syn68k_mmu040_translate(Syn68kMMU040* mmu, const Syn68kMMU040Bus* bus, uint32_t logical, unsigned fc,
                                           int writing);
/* PTEST refreshes the selected ATC entry and writes MMUSR, including faults.
   Its returned fault is diagnostic, not a guest operand exception.
   With TC.E clear and no TTR hit, this engine selects zero for undefined MMUSR. */
Syn68kMMU040Result syn68k_mmu040_ptest(Syn68kMMU040* mmu, const Syn68kMMU040Bus* bus, uint32_t logical, unsigned fc,
                                       int writing);
/* Both ATCs; address selection also matches FC2. include_global == 0 models
   PFLUSHN/PFLUSHAN. Flushes do not change control registers or MMUSR. */
void syn68k_mmu040_flush(Syn68kMMU040* mmu, int all, uint32_t logical, int supervisor, int include_global);

#endif
