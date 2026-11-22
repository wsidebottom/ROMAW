/*+===================================================================
  File:      cache750.h

  Summary:   Define a caller-owned 32-Kbyte eight-way 750 L1 cache bank and physical operations.

  Classes:   ROMlib_ppc_cache750_line, ROMlib_ppc_cache750, ROMlib_ppc_cache750_result, ROMlib_ppc_cache750_observer,
             ROMlib_ppc_cache750_layer;
             uses private ROMlib_ppc_cache750_transaction forward declaration.

  Functions: ROMlib_ppc_cache750_reset, ROMlib_ppc_cache750_transfer, ROMlib_ppc_cache750_block,
             ROMlib_ppc_cache750_invalidate_set, ROMlib_ppc_cache750_history, ROMlib_ppc_cache750_busy,
             ROMlib_ppc_cache750_transfer_observed, ROMlib_ppc_cache750_block_observed, ROMlib_ppc_cache750_transfer_layered,
             ROMlib_ppc_cache750_block_layered

  Origin:    Original functional implementation from MPC750UM chapter 3 and table 8-2.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/
/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SYN_PPC_CACHE750_H
#define SYN_PPC_CACHE750_H
#include "synppc/mmu750.h"
#ifdef __cplusplus
extern "C" {
#endif

struct ROMlib_ppc_physical_bus;
struct ROMlib_ppc_cache750_transaction;

typedef struct ROMlib_ppc_cache750_line {
    uint32_t address;
    unsigned char data[32];
    unsigned char state; /* 0 invalid, 1 exclusive, 2 modified. */
} ROMlib_ppc_cache750_line;

typedef struct ROMlib_ppc_cache750 {
    ROMlib_ppc_cache750_line lines[128][8];
    unsigned char plru[128]; /* B0..B6 occupy numeric bits 0..6. */
    /* Private stack of active bus transactions; NULL whenever the bank is idle.
       CPU accesses, reset, configuration and snapshots require serialization.
       Only snoops may re-enter a bank from its physical bus callbacks. */
    struct ROMlib_ppc_cache750_transaction* transactions;
} ROMlib_ppc_cache750;

typedef struct ROMlib_ppc_cache750_result {
    uint32_t physical, cause;
    unsigned completed, fault;
    /* Cacheable tag misses actually attempted and completed dirty replacement
       copybacks. Disabled/inhibited bypasses and explicit pushes are excluded.
       Counts survive partial fills/faults; ZERO allocation is a tag miss. */
    unsigned misses, castouts, pushes;
} ROMlib_ppc_cache750_result;

/* Optional synchronous event sink for ordering with re-entrant snoops/clocks.
   A miss is reported at tag lookup, a castout after successful dirty push and
   before the replacement fill. The sink must preserve bank/CPU configuration;
   like the physical bus callback, only qualified snoops may re-enter a bank. */
typedef struct ROMlib_ppc_cache750_observer {
    void* opaque;
    void (*event)(void*, unsigned misses, unsigned castouts);
} ROMlib_ppc_cache750_observer;

/* Optional whole-line lower level. Scalar transfers/history still use the
   supplied physical bus. A burst receives 32 bytes in address order and a
   critical-doubleword offset for reads; return fault/address/cause on failure.
   No callback pointer is retained in the bank. */
typedef struct ROMlib_ppc_cache750_layer {
    void* opaque;
    int (*burst)(void*, uint32_t physical, void*, unsigned critical, unsigned access, unsigned wimg,
                 ROMlib_ppc_cache750_result*);
} ROMlib_ppc_cache750_layer;

int ROMlib_ppc_cache750_transfer_layered(ROMlib_ppc_cache750*, const struct ROMlib_ppc_physical_bus*, uint32_t, void*,
                                         unsigned size, unsigned access, unsigned wimg, unsigned control,
                                         ROMlib_ppc_cache750_result*, const ROMlib_ppc_cache750_observer*,
                                         const ROMlib_ppc_cache750_layer*);
int ROMlib_ppc_cache750_block_layered(ROMlib_ppc_cache750*, const struct ROMlib_ppc_physical_bus*, uint32_t,
                                      unsigned operation, unsigned access, unsigned wimg, unsigned control,
                                      ROMlib_ppc_cache750_result*, const ROMlib_ppc_cache750_observer*,
                                      const ROMlib_ppc_cache750_layer*);

enum {
    ROMLIB_PPC_CACHE750_ENABLE = 1,
    ROMLIB_PPC_CACHE750_LOCK = 2,
    ROMLIB_PPC_CACHE750_FLUSH_ASSIST = 4,
    ROMLIB_PPC_CACHE750_FORCE_THROUGH = 8
};

enum {
    ROMLIB_PPC_CACHE750_STORE = 1,
    ROMLIB_PPC_CACHE750_FLUSH = 2,
    ROMLIB_PPC_CACHE750_INVALIDATE = 3,
    ROMLIB_PPC_CACHE750_ZERO = 4,
    ROMLIB_PPC_CACHE750_SNOOP = 5,
    ROMLIB_PPC_CACHE750_SNOOP_UNCACHED_READ = 6
};

void ROMlib_ppc_cache750_reset(ROMlib_ppc_cache750*);
/* Address phase collision with a pending fill, copyback or bypass transaction.
   The bus master must abort/retry after the active callback returns. */
int ROMlib_ppc_cache750_busy(const ROMlib_ppc_cache750*, uint32_t physical);
/* One bank, selected by the caller. Physical bytes have no endian munging.
   Access accepts MMU WRITE/INSTRUCTION and PHYSICAL_TABLE/ATOMIC. Size is 1..8;
   physical wrap is rejected. Read buffers commit only on complete success.
   ENABLE/LOCK/DCFA follow HID0; FORCE_THROUGH implements successful STWCX.
   WB misses allocate; WT/forced stores do not allocate. Inhibited accesses
   bypass tags without changing them (MPC750UM 3.3.3, unlike table 3-7).
   Bus failures retain their cause/address and set fault=MMU_BUS; API misuse
   sets MMU_CONFIGURATION. Success clears fault. Castouts use WIMG=0000;
   explicit clean/flush and snoop pushes use 1000 (MPC750UM table 3-6). */
int ROMlib_ppc_cache750_transfer(ROMlib_ppc_cache750*, const struct ROMlib_ppc_physical_bus*, uint32_t, void*,
                                 unsigned size, unsigned access, unsigned wimg, unsigned control,
                                 ROMlib_ppc_cache750_result*);
/* Observed variants preserve the ordinary API/result and additionally report
   events at their occurrence, before later bus callbacks can freeze counters. */
int ROMlib_ppc_cache750_transfer_observed(ROMlib_ppc_cache750*, const struct ROMlib_ppc_physical_bus*, uint32_t, void*,
                                          unsigned size, unsigned access, unsigned wimg, unsigned control,
                                          ROMlib_ppc_cache750_result*, const ROMlib_ppc_cache750_observer*);
int ROMlib_ppc_cache750_block_observed(ROMlib_ppc_cache750*, const struct ROMlib_ppc_physical_bus*, uint32_t,
                                       unsigned operation, unsigned access, unsigned wimg, unsigned control,
                                       ROMlib_ppc_cache750_result*, const ROMlib_ppc_cache750_observer*);
/* STORE/FLUSH/INVALIDATE operate even while disabled/locked. ZERO requires
   ENABLE, ignores LOCK and allocates without a read. The caller handles MMU,
   privilege, DCBZ alignment and address-only coherency broadcasts. Snoop calls
   represent qualified global transactions on a data bank; disabled banks
   ignore snoops. Cached reads/writes invalidate after pushing dirty data;
   an uncached read pushes dirty data and retains an exclusive copy. */
int ROMlib_ppc_cache750_block(ROMlib_ppc_cache750*, const struct ROMlib_ppc_physical_bus*, uint32_t, unsigned operation,
                              unsigned access, unsigned wimg, unsigned control, ROMlib_ppc_cache750_result*);
/* ICBI uses only EA[20:26] and invalidates all ways, even while disabled. */
void ROMlib_ppc_cache750_invalidate_set(ROMlib_ppc_cache750*, uint32_t effective);
/* Hardware PTE R/C update, with canonical BE bytes and WIMG=0010. A resident
   enabled data line is updated in exclusive ownership and becomes modified.
   Disabled/locked-miss updates use the bus's atomic physical history callback.
   CPU accesses must be serialized; re-entrant snoops follow the busy/retry rule. */
int ROMlib_ppc_cache750_history(ROMlib_ppc_cache750*, const struct ROMlib_ppc_physical_bus*, uint32_t pte,
                                uint32_t mask, unsigned control, ROMlib_ppc_cache750_result*);
#ifdef __cplusplus
}
#endif
#endif
