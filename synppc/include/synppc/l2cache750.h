/*+===================================================================
  File:      l2cache750.h

  Summary:   Define a caller-owned 750 sectored unified L2 bank and ordered physical transactions.

  Classes:   ROMlib_ppc_l2cache750_tag, ROMlib_ppc_l2cache750, ROMlib_ppc_l2cache750_result, ROMlib_ppc_l2cache750_observer;
             uses ROMlib_ppc_physical_bus and private ROMlib_ppc_l2cache750_transaction.

  Functions: ROMlib_ppc_l2cache750_reset, ROMlib_ppc_l2cache750_invalidate, ROMlib_ppc_l2cache750_busy,
             ROMlib_ppc_l2cache750_transfer, ROMlib_ppc_l2cache750_block, ROMlib_ppc_l2cache750_transfer_observed,
             ROMlib_ppc_l2cache750_block_observed, ROMlib_ppc_l2cache750_history

  Origin:    Original functional model from MPC750UM chapter 9, especially 9.1.1/9.1.2.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/
/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SYN_PPC_L2CACHE750_H
#define SYN_PPC_L2CACHE750_H
#include "synppc/cache750.h"
#ifdef __cplusplus
extern "C" {
#endif

struct ROMlib_ppc_physical_bus;
struct ROMlib_ppc_l2cache750_transaction;

typedef struct ROMlib_ppc_l2cache750_tag {
    uint32_t address;
    unsigned char state[4]; /* Per 32-byte sector: invalid/exclusive/modified. */
} ROMlib_ppc_l2cache750_tag;

typedef struct ROMlib_ppc_l2cache750 {
    ROMlib_ppc_l2cache750_tag tags[4096][2];
    unsigned char data[1048576];
    unsigned char parity[1048576 / 8]; /* One odd-parity bit per SRAM byte. */
    unsigned char lru[4096];           /* Next victim way; lowest invalid way wins. */
    struct ROMlib_ppc_l2cache750_transaction* transactions;
} ROMlib_ppc_l2cache750;

typedef struct ROMlib_ppc_l2cache750_result {
    uint32_t physical, cause;
    unsigned completed, fault, hits, misses, castouts, pushes;
} ROMlib_ppc_l2cache750_result;

enum {
    ROMLIB_PPC_L2CACHE750_ENABLE = 1,
    ROMLIB_PPC_L2CACHE750_DATA_ONLY = 2,
    ROMLIB_PPC_L2CACHE750_WRITE_THROUGH = 4,
    ROMLIB_PPC_L2CACHE750_TEST = 8,
    ROMLIB_PPC_L2CACHE750_PARITY = 16,
    /* Access qualifiers, independent of WIMG and control. An explicit push
       originates in DCBST/DCBF; L1_HIT supplies the accepted STWCX context. */
    ROMLIB_PPC_L2CACHE750_EXPLICIT_PUSH = 65536,
    ROMLIB_PPC_L2CACHE750_L1_HIT = 131072,
    ROMLIB_PPC_L2CACHE750_SNOOP_PUSH = 262144,
    ROMLIB_PPC_L2CACHE750_RETAIN = 524288
};

enum {
    ROMLIB_PPC_L2CACHE750_HIT = 1,
    ROMLIB_PPC_L2CACHE750_MISS = 2,
    ROMLIB_PPC_L2CACHE750_CASTOUT = 3,
    ROMLIB_PPC_L2CACHE750_PUSH = 4
};

typedef struct ROMlib_ppc_l2cache750_observer {
    void* opaque;
    void (*event)(void*, unsigned event, unsigned count);
} ROMlib_ppc_l2cache750_observer;

/* Synchronous observation preserves event order across clocks/snoops in bus
   callbacks. The observer is borrowed only for the call and must not change
   mapping/cache configuration. SNOOP_PUSH writes a whole L1 intervention to
   RAM without allocation or L2TS suppression; RETAIN updates an existing L2
   sector to exclusive instead of invalidating it for an uncached snoop read. */
int ROMlib_ppc_l2cache750_transfer_observed(ROMlib_ppc_l2cache750*, const struct ROMlib_ppc_physical_bus*,
                                            unsigned capacity, uint32_t physical, void*, unsigned size,
                                            unsigned critical, unsigned access, unsigned wimg, unsigned control,
                                            ROMlib_ppc_l2cache750_result*, const ROMlib_ppc_l2cache750_observer*);
int ROMlib_ppc_l2cache750_block_observed(ROMlib_ppc_l2cache750*, const struct ROMlib_ppc_physical_bus*,
                                         unsigned capacity, uint32_t physical, unsigned operation, unsigned control,
                                         ROMlib_ppc_l2cache750_result*, const ROMlib_ppc_l2cache750_observer*);
/* Canonical BE PTE R/C update: cache hits update L2, misses use the external
   atomic history callback without allocation. Global WT preserves hit state
   and commits to external memory before modifying the cached word. */
int ROMlib_ppc_l2cache750_history(ROMlib_ppc_l2cache750*, const struct ROMlib_ppc_physical_bus*, unsigned capacity,
                                  uint32_t pte, uint32_t mask, unsigned control, ROMlib_ppc_l2cache750_result*,
                                  const ROMlib_ppc_l2cache750_observer*);

/* All operations require caller serialization. Only qualified block snoops may
   re-enter from bus callbacks; active incoming/victim ranges return MMU_RETRY.
   Allocate this roughly 1.1-Mbyte bank outside the CPU/host stack. */
void ROMlib_ppc_l2cache750_reset(ROMlib_ppc_l2cache750*);
/* Clear tags/status/LRU without modifying external SRAM bytes. Reject while busy. */
int ROMlib_ppc_l2cache750_invalidate(ROMlib_ppc_l2cache750*);
int ROMlib_ppc_l2cache750_busy(const ROMlib_ppc_l2cache750*, uint32_t physical);
/* Capacity is 256/512/1024 KiB; zero is allowed only while disabled. The first
   two sizes use two sectors per tag, the largest four. Size is 1..8 for single
   beats or exactly 32 for an aligned burst. Critical offset 0/8/16/24 chooses
   burst-read beat order. Buffers use physical byte order. Single read/store
   misses never allocate; full bursts do. Inhibited/data-only bypasses leave
   tags and LRU unchanged. EXPLICIT_PUSH requires a burst write. Accepted
   conditional stores use PHYSICAL_ATOMIC and optional L1_HIT; no reservation
   is created by this interface. Read buffers commit only on complete success.
   Failed fills retain the previous tag and completed dirty-sector copybacks.
   TEST suppresses single-store misses and routes explicit pushes into L2.
   System750 supplies L2CR sequencing; SRAM pin timing/parity remain unmodeled. */
int ROMlib_ppc_l2cache750_transfer(ROMlib_ppc_l2cache750*, const struct ROMlib_ppc_physical_bus*, unsigned capacity,
                                   uint32_t physical, void*, unsigned size, unsigned critical, unsigned access,
                                   unsigned wimg, unsigned control, ROMlib_ppc_l2cache750_result*);
/* Reuse CACHE750 STORE/FLUSH/INVALIDATE/SNOOP/SNOOP_UNCACHED_READ operation
   codes. ZERO is a lookup-only global DCBZ probe. Both explicit STORE and FLUSH push then invalidate the L2 sector.
   Uncached snoop reads push dirty data and retain an exclusive copy. */
int ROMlib_ppc_l2cache750_block(ROMlib_ppc_l2cache750*, const struct ROMlib_ppc_physical_bus*, unsigned capacity,
                                uint32_t physical, unsigned operation, unsigned control, ROMlib_ppc_l2cache750_result*);
#ifdef __cplusplus
}
#endif
#endif
