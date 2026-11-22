/*+===================================================================
  File:      mmu750.h

  Summary:   Define the functional PowerPC 750 BAT, hashed-page and TLB translation boundary.

  Classes:   C records for architectural registers, TLB entries, physical-table bus and translation results.

  Functions: ROMlib_ppc_mmu_reset, ROMlib_ppc_mmu_flush, ROMlib_ppc_mmu_tlbie, ROMlib_ppc_mmu_translate

  Origin:    Original implementation from MPC750UM chapter 5 and MPCFPE chapter 7.
             https://www.nxp.com/docs/en/reference-manual/MPC750UM.pdf
             https://www.nxp.com/docs/en/user-guide/MPCFPE_AD_R1.pdf

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SYN_PPC_MMU750_H
#define SYN_PPC_MMU750_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum {
    ROMLIB_PPC_MMU_WRITE = 1,
    ROMLIB_PPC_MMU_INSTRUCTION = 2,
    ROMLIB_PPC_MMU_DCBZ = 4,
    ROMLIB_PPC_MMU_PROBE = 8,
    ROMLIB_PPC_MMU_PREFLIGHT = 1 << 16
};

enum {
    ROMLIB_PPC_MMU_NONE,
    ROMLIB_PPC_MMU_MISSING,
    ROMLIB_PPC_MMU_PROTECTION,
    ROMLIB_PPC_MMU_NO_EXECUTE,
    ROMLIB_PPC_MMU_DIRECT_STORE,
    ROMLIB_PPC_MMU_BUS,
    ROMLIB_PPC_MMU_ALIGNMENT,
    ROMLIB_PPC_MMU_CONFIGURATION,
    ROMLIB_PPC_MMU_EXTERNAL_DISABLED,
    ROMLIB_PPC_MMU_RETRY /* Cache/bus arbitration, not an architectural exception. */
};

typedef struct {
    uint64_t tag;
    uint32_t pte, valid;
} ROMlib_ppc_tlb750;

typedef struct {
    uint32_t msr, sdr1, sr[16], ibat[4][2], dbat[4][2];
    ROMlib_ppc_tlb750 tlb[2][64][2];
    uint8_t replace[2][64];
} ROMlib_ppc_mmu750;

/* Table reads return both architectural big-endian PTE words as host integers.
   History updates atomically OR mask into the PTE's second word, preserving
   concurrent unrelated bits. Addresses identify the eight-byte PTE. All these
   transactions are physical table accesses, not recursively translated.
   Return nonzero on success. Callbacks and state are confined to one CPU thread. */
typedef struct {
    void* opaque;
    int (*read)(void*, uint32_t, uint32_t[2]);
    int (*history)(void*, uint32_t, uint32_t);
} ROMlib_ppc_mmu_bus;

typedef struct {
    uint32_t address, physical, wimg, fault, vector, cause;
    int bat;
    unsigned tlb_hit;
} ROMlib_ppc_mmu_result;

/* Reset deterministically initializes architectural registers and internal TLBs.
   Hardware BAT/TLB contents at reset are otherwise unspecified. Register/table
   changes require the normal software synchronization/invalidation protocol. */
void ROMlib_ppc_mmu_reset(ROMlib_ppc_mmu750*);
void ROMlib_ppc_mmu_flush(ROMlib_ppc_mmu750*);
/* 750 tlbie invalidates both ways of both TLBs at EA[14:19], ignoring VSID.
   tlbia is not a 750 instruction; flush is a host lifecycle operation. */
void ROMlib_ppc_mmu_tlbie(ROMlib_ppc_mmu750*, uint32_t);
/* One address only: no guest operand transfer and no CPU exception entry.
   Returns one on translation success, zero with bounded diagnostic metadata.
   PROBE implements the non-faulting/no-table-search dcbt/dcbtst lookup policy.
   PREFLIGHT with WRITE checks store translation and reports faults without
   changing PTE history or TLB contents. Physical table reads still occur.
   Cross-page operand splitting and exception priority belong to the CPU bus. */
int ROMlib_ppc_mmu_translate(ROMlib_ppc_mmu750*, const ROMlib_ppc_mmu_bus*, uint32_t, unsigned, ROMlib_ppc_mmu_result*);
#ifdef __cplusplus
}
#endif
#endif
