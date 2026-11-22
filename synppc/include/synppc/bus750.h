/*+===================================================================
  File:      bus750.h

  Summary:   Define 750 logical transfers, L1 controls and explicit physical bus transactions.

  Classes:   ROMlib_ppc_physical_bus, ROMlib_ppc_bus_result; uses ROMlib_ppc_system750.

  Functions: ROMlib_ppc_750_read, ROMlib_ppc_750_write, ROMlib_ppc_750_transfer_bytes, ROMlib_ppc_750_translate, ROMlib_ppc_750_external,
             ROMlib_ppc_750_load_reserve, ROMlib_ppc_750_store_conditional, ROMlib_ppc_750_cache_control,
             ROMlib_ppc_750_snoop, ROMlib_ppc_750_host_sync

  Origin:    Original MPC750 MMU/data path; endian mapping follows MPCFPE 3.1.4.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SYN_PPC_BUS750_H
#define SYN_PPC_BUS750_H
#include "synppc/system750.h"
#ifdef __cplusplus
extern "C" {
#endif

enum {
    ROMLIB_PPC_PHYSICAL_TABLE = 16,
    ROMLIB_PPC_PHYSICAL_EXTERNAL = 32,
    ROMLIB_PPC_PHYSICAL_CONTROL = 64,
    ROMLIB_PPC_PHYSICAL_ATOMIC = 128,
    ROMLIB_PPC_PHYSICAL_RESOURCE_SHIFT = 8,
    ROMLIB_PPC_PHYSICAL_RESOURCE_MASK = 0xF00
};

enum {
    ROMLIB_PPC_BUS_DCBZ = 1,
    ROMLIB_PPC_BUS_DCBI = 2,
    ROMLIB_PPC_BUS_DCBF = 3,
    ROMLIB_PPC_BUS_DCBST = 4,
    ROMLIB_PPC_BUS_SYNC = 5,
    ROMLIB_PPC_BUS_EIEIO = 6
};

typedef struct ROMlib_ppc_physical_bus {
    void* opaque;
    /* Bytes are in physical bus order. Access contains WRITE/INSTRUCTION or
       PHYSICAL_TABLE, or PHYSICAL_EXTERNAL with the four-bit resource ID at
       RESOURCE_SHIFT; wimg is the normalized four-bit storage attribute.
       PHYSICAL_ATOMIC identifies LWARX reads/fills and accepted STWCX writes;
       replacement copybacks do not inherit the atomic flag.
       PHYSICAL_CONTROL has size=0/buffer=NULL and its operation (BUS_*) at
       RESOURCE_SHIFT. It represents a serialized address-only broadcast.
       A zero return reports a bus error; cause defaults to SRR1[TEA] but the
       callback may supply the MPC750 machine-check cause bits instead.
       A transfer cannot wrap the 32-bit physical boundary. */
    int (*transfer)(void*, uint32_t, void*, unsigned, unsigned, unsigned, uint32_t* cause);
    /* Atomically OR the host-integer R/C mask into the physical PTE's second
       big-endian word. No endian munging applies to hardware page-table reads.
       The address identifies the eight-byte PTE, as in mmu750.h. */
    int (*history)(void*, uint32_t, uint32_t, uint32_t* cause);
    /* Optional ordinary physical RAM. The caller owns this permanent writable
       nonwrapping span and serializes its accesses. Transfers and aligned atomic R/C updates
       within the span bypass callbacks; external/control transactions never do.
       Keep overlays, devices and callback-observable memory outside this span. */
    unsigned char* linear_memory;
    uint32_t linear_address, linear_size;
    /* A checked callback boundary can mark an embedding exception here. Model
       cleanup then reports configuration failure without inventing a bus check;
       the boundary rethrows the original exception after native code returns. */
    const unsigned* callback_aborted;
#ifdef __cplusplus
    constexpr ROMlib_ppc_physical_bus(void* context = nullptr, decltype(transfer) transaction = nullptr,
                                      decltype(history) update_history = nullptr, unsigned char* memory = nullptr,
                                      uint32_t address = 0, uint32_t size = 0, const unsigned* aborted = nullptr)
        : opaque(context), transfer(transaction), history(update_history), linear_memory(memory),
          linear_address(address), linear_size(size), callback_aborted(aborted)
    {
    }
#endif
} ROMlib_ppc_physical_bus;

typedef struct ROMlib_ppc_bus_result {
    ROMlib_ppc_mmu_result mapping;
    uint32_t address, size, completed;
    unsigned table;
} ROMlib_ppc_bus_result;

/* Translate without transferring an operand. Used by cache and external
   controls and host-service write preflight; access uses the mmu750 flags.
   Physical PTE failures retain machine-check cause/address information. */
int ROMlib_ppc_750_translate(ROMlib_ppc_system750*, const ROMlib_ppc_physical_bus*, uint32_t, unsigned access,
                             ROMlib_ppc_bus_result*);

/* External-control word transaction. EAR[E] is checked before alignment and
   translation. The translated address and numeric word are device operands,
   independent of ordinary-memory LE lanes. Payload bytes use canonical BE word
   order; WIMG is forced to 0101 and EAR[RID] selects the external resource.
   DR=0 chooses identity addressing for the architecturally undefined case.
   A failed read leaves *value untouched. */
int ROMlib_ppc_750_external(ROMlib_ppc_system750*, const ROMlib_ppc_physical_bus*, uint32_t, int write, uint32_t* value,
                            ROMlib_ppc_bus_result*);

/* Transfer a numeric scalar (1/2/4/8 bytes). READ permits INSTRUCTION for a
   word-aligned four-byte fetch; WRITE takes no instruction flag. Reads commit
   *value only after complete success. A later-page failure preserves acknowledged
   earlier bus effects and reports completed bytes and the faulting subaddress.
   Integer misalignment and 32-bit wrapping are supported. Opcode-specific FP,
   multiple/string and reservation alignment belongs to the instruction layer.
   LE uses the 750's doubleword byte-lane mapping, not a flat modern LE layout.
   HID0 selects persistent L1 banks; this boundary reports faults without entering CPU exceptions. */
int ROMlib_ppc_750_read(ROMlib_ppc_system750*, const ROMlib_ppc_physical_bus*, uint32_t, unsigned, unsigned access,
                        uint64_t*, ROMlib_ppc_bus_result*);
int ROMlib_ppc_750_write(ROMlib_ppc_system750*, const ROMlib_ppc_physical_bus*, uint32_t, unsigned, uint64_t,
                         ROMlib_ppc_bus_result*);
/* Transfer raw bytes in ascending effective-address order. Access is zero
   for reads or WRITE for stores. A zero-length transfer leaves memory alone.
   On failure, completed counts the transferred prefix and mapping describes
   the faulting subaddress. A failed read leaves the untransferred suffix alone. */
int ROMlib_ppc_750_transfer_bytes(ROMlib_ppc_system750*, const ROMlib_ppc_physical_bus*, uint32_t, void* bytes,
                                  uint32_t size, unsigned access, ROMlib_ppc_bus_result*);
/* Aligned LWARX word transaction. Establish a physical reservation before
   the operand read so intervening snoops can cancel it. Faults cancel it;
   successful reads commit *value without reasserting a canceled reservation. */
int ROMlib_ppc_750_load_reserve(ROMlib_ppc_system750*, const ROMlib_ppc_physical_bus*, uint32_t, uint32_t* value,
                                ROMlib_ppc_bus_result*);
/* The CPU has already accepted the reservation. This always writes through,
   updating a data-cache hit only after bus success and never allocating. */
int ROMlib_ppc_750_store_conditional(ROMlib_ppc_system750*, const ROMlib_ppc_physical_bus*, uint32_t, uint32_t,
                                     ROMlib_ppc_bus_result*);
/* Cache instruction secondary opcode: 54/86/470/278/246/982/1014 and SYNC 598
   or EIEIO 854. Privilege and reserved instruction bits belong to the CPU.
   DCBZ preserves cache-off and W/I alignment priority; hints do not walk or
   cause MMU exceptions. Physical bus errors still cause machine check.
   Instruction cache invalidation is virtual index only. */
int ROMlib_ppc_750_cache_control(ROMlib_ppc_system750*, const ROMlib_ppc_physical_bus*, uint32_t, unsigned opcode,
                                 ROMlib_ppc_bus_result*);
/* Host-memory boundary for HLE callbacks: push and invalidate every enabled
   L1 data-cache line and L2 sector, including under DLOCK, and cancel
   reservations. Disabled banks retain their tags, as for external masters. I-cache remains untouched;
   publishing host-generated instructions requires explicit code maintenance.
   Call before and after host code that accesses guest RAM. CPU activity must
   be serialized; an active transaction returns RETRY before changing tags.
   Failed pushes retain their line and latch an asynchronous machine check. */
int ROMlib_ppc_750_host_sync(ROMlib_ppc_system750*, const ROMlib_ppc_physical_bus*, ROMlib_ppc_bus_result*);
/* Observe another bus master's address phase before it transfers data. One
   physical 32-byte granule per call; TT is the numeric five-bit 60x transfer
   type (MPC750UM table 3-5), global/inhibited are normalized booleans. Global
   requests snoop an enabled data bank, never I-cache. Non-global writes still
   monitor reservations, even with DCE off; TT=18 cancels any reservation.
   Nonglobal traffic does not invalidate data tags (table 3-6). The caller
   serializes CPU accesses; snoops alone may re-enter physical callbacks. The
   external master retries its transfer after a failed push or collision.
   No guest exception is entered here. Push failures retain MCE data and latch
   an unmaskable bus-check request for the next CPU boundary, even if the other
   master aborts/retries. Exception entry acknowledges that request. A collision
   with an active fill/copyback returns zero with fault=MMU_RETRY, vector/cause=0,
   without issuing another transfer or latching machine check. The external
   master retries after the original physical callback/transaction completes. */
int ROMlib_ppc_750_snoop(ROMlib_ppc_system750*, const ROMlib_ppc_physical_bus*, uint32_t physical,
                         unsigned transfer_type, int global, int inhibited, ROMlib_ppc_bus_result*);
#ifdef __cplusplus
}
#endif
#endif
