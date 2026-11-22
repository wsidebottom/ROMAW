/*+===================================================================
  File:      system750.h

  Summary:   Define persistent 750 CPU/cache/thermal/performance state and exception/clock interfaces.

  Classes:   ROMlib_ppc_system750; uses ROMlib_ppc_registers and ROMlib_ppc_mmu750.

  Functions: ROMlib_ppc_750_reset, ROMlib_ppc_750_exception, ROMlib_ppc_750_rfi, ROMlib_ppc_750_advance,
             ROMlib_ppc_750_set_decrementer, ROMlib_ppc_750_set_tben, ROMlib_ppc_750_set_inputs, ROMlib_ppc_750_interrupt,
             ROMlib_ppc_750_set_hid0,
             ROMlib_ppc_750_set_pll, ROMlib_ppc_750_set_temperature, ROMlib_ppc_750_set_thermal,
             ROMlib_ppc_750_advance_core, ROMlib_ppc_750_performance_write, ROMlib_ppc_750_performance_event,
             ROMlib_ppc_750_performance_complete, ROMlib_ppc_750_set_msr, ROMlib_ppc_750_set_time_base,
             ROMlib_ppc_750_write_spr, ROMlib_ppc_750_set_l2, ROMlib_ppc_750_set_l2cr, ROMlib_ppc_750_drive_parity, ROMlib_ppc_750_sample_parity,
             ROMlib_ppc_750_qreq, ROMlib_ppc_750_l2zz, ROMlib_ppc_750_set_qack, ROMlib_ppc_750_advance_bus

  Origin:    Original implementation from MPC750UM chapters 4/10/11, AN1800 calibration and MPCFPE exception/RFI definitions.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SYN_PPC_SYSTEM750_H
#define SYN_PPC_SYSTEM750_H
#include "synppc/ppc.h"
#include "synppc/mmu750.h"
#include "synppc/cache750.h"
#include "synppc/l2cache750.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct ROMlib_ppc_system750 {
    ROMlib_ppc_mmu750 mmu;
    uint32_t srr0, srr1, dar, dsisr;
    uint32_t checkstop;
    uint32_t sprg[4];
    uint64_t time_base;
    uint32_t tben_disabled;
    uint32_t decrementer, decrementer_pending;
    uint32_t inputs, machine_check_pending, soft_reset_pending, tlbsync_wait;
    uint32_t ear;
    /* IABR reads expose the written value. Matching keeps the previous context
       until ISYNC, RFI or exception entry establishes the new context. */
    uint32_t iabr, iabr_previous, iabr_pending;
    /* Effective doubleword address, translation context, write/read enables. */
    uint32_t dabr;
    uint32_t hid0, hid1, ictc;
    uint64_t forward_cycle;
    uint32_t forward_next, forward_valid;
    uint32_t power_mode, qack, nap_snoop_delay;
    uint32_t thrm[3];
    int32_t junction_temperature; /* Integer Celsius input, before THRM3 calibration. */
    uint64_t core_cycles;
    uint32_t thermal_channel, thermal_remaining, thermal_halted, thermal_pending;
    uint32_t mmcr0, mmcr1, pmc[4], sia;
    uint32_t performance_pending, performance_frozen, performance_triggered;
    uint32_t performance_last_pc, performance_current_pc;
    ROMlib_ppc_l2cache750* l2; /* Borrowed board SRAM/tag bank; attach after reset. */
    uint32_t l2cr, l2invalidate_remaining;
    uint32_t reservation_valid, reservation_address; /* Physical 32-byte granule. */
    uint32_t bus_check_pending, bus_check_cause;     /* Asynchronous bus machine checks. */
    ROMlib_ppc_cache750 cache[2];                    /* Instruction, data; persistent across CPU objects. */
} ROMlib_ppc_system750;

enum {
    ROMLIB_PPC_750_HID0_EMCP = 0x80000000u,
    ROMLIB_PPC_750_HID0_DBP = 0x40000000,
    ROMLIB_PPC_750_HID0_EBA = 0x20000000,
    ROMLIB_PPC_750_HID0_EBD = 0x10000000,
    ROMLIB_PPC_750_HID0_ICE = 0x8000,
    ROMLIB_PPC_750_HID0_DCE = 0x4000,
    ROMLIB_PPC_750_HID0_ILOCK = 0x2000,
    ROMLIB_PPC_750_HID0_DLOCK = 0x1000,
    ROMLIB_PPC_750_HID0_ICFI = 0x800,
    ROMLIB_PPC_750_HID0_DCFI = 0x400,
    ROMLIB_PPC_750_HID0_DCFA = 0x40,
    ROMLIB_PPC_750_HID0_ABE = 8,
    ROMLIB_PPC_750_HID0_NOOPTI = 1
};

enum {
    ROMLIB_PPC_750_RUN,
    ROMLIB_PPC_750_DOZE,
    ROMLIB_PPC_750_NAP_REQUEST,
    ROMLIB_PPC_750_SLEEP_REQUEST,
    ROMLIB_PPC_750_NAP,
    ROMLIB_PPC_750_SLEEP
};

/* Quiescent requests stop fetch. QACK commits nap/sleep. During acknowledged
   nap, release QACK for eight bus clocks before presenting a snoop. */
int ROMlib_ppc_750_qreq(const ROMlib_ppc_system750*);
/* L2CTL drives external SRAM low-power while nap or sleep remains active. */
int ROMlib_ppc_750_l2zz(const ROMlib_ppc_system750*);
void ROMlib_ppc_750_set_qack(ROMlib_ppc_system750*, int asserted);
void ROMlib_ppc_750_advance_bus(ROMlib_ppc_system750*, uint64_t clocks);

enum { ROMLIB_PPC_750_ADDRESS_LANES = 4, ROMLIB_PPC_750_DATA_LANES = 8 };

/* Board interface for qualified 60x address and data phases. Bit zero of
   parity represents the low byte of value. Supply all eight physical data
   lanes even for subword reads. Sample incoming snoop addresses and read
   data; drive outgoing master addresses and write data. A qualified input
   error latches a machine check for the next interrupt boundary. */
unsigned ROMlib_ppc_750_drive_parity(const ROMlib_ppc_system750*, unsigned lanes, uint64_t value);
void ROMlib_ppc_750_sample_parity(ROMlib_ppc_system750*, unsigned lanes, uint64_t value, unsigned parity);

/* Attach caller-owned L2 storage while disabled and idle; clear its tags, not
   SRAM. Reset detaches storage, so the board reattaches after reset. L2CR keeps
   implemented/reserved writable fields and treats IP as read-only. A disabled
   I request completes after 32768 supplied core cycles, leaving I set for the
   guest to clear. Writes cannot reconfigure an active bus transaction. */
int ROMlib_ppc_750_set_l2(ROMlib_ppc_system750*, ROMlib_ppc_l2cache750*);
int ROMlib_ppc_750_set_l2cr(ROMlib_ppc_system750*, uint32_t);
/* Reserved HID0 bits read zero. Enabled flash-invalidate requests complete
   synchronously and clear themselves; disabled requests remain set. Cache
   disable/lock does not flush data. EMCP masks raw MCP edge recognition. */
void ROMlib_ppc_750_set_hid0(ROMlib_ppc_system750*, uint32_t);

/* Board PLL_CFG[0..3] input, reflected in read-only HID1[0..3]. Supply a
   numeric four-bit encoding at reset/allowed board reconfiguration boundaries.
   Invalid encodings and checkstop leave state untouched and return zero. */
int ROMlib_ppc_750_set_pll(ROMlib_ppc_system750*, unsigned);
/* Independent, caller-serialized core-clock/sensor inputs. Clock counts are
   processor cycles, distinct from TB/DEC ticks. Sensor input is deterministic
   integer Celsius; reset selects zero until the embedding supplies a value.
   THRM3 calibration uses AN1800's 20/8/4/2-degree adjustment bits. */
void ROMlib_ppc_750_set_temperature(ROMlib_ppc_system750*, int32_t);
void ROMlib_ppc_750_advance_core(ROMlib_ppc_system750*, uint64_t cycles);
/* Write THRM1/2/3 by zero-based index. Ignore read-only TIN/TIV writes; THRM
   writes clear the relevant TIV and restart comparisons. THRM3 also rearms
   both channels. A threshold interrupt holds its result until reprogramming;
   the other channel can continue. Invalid indices return zero without mutation. */
int ROMlib_ppc_750_set_thermal(ROMlib_ppc_system750*, unsigned, uint32_t);

/* Logical event inputs mapped to the four counter-specific select tables in
   MPC750UM 11.2. Inputs represent counts, except LOAD_LATENCY, which reports
   one miss's latency for comparison against MMCR0[THRESHOLD]. Pipeline/stall
   timing inputs belong to the embedding; this is not a superscalar timing model. */
enum {
    ROMLIB_PPC_750_PME_CYCLES = 1,
    ROMLIB_PPC_750_PME_COMPLETED,
    ROMLIB_PPC_750_PME_TIME_BASE,
    ROMLIB_PPC_750_PME_DISPATCHED,
    ROMLIB_PPC_750_PME_EIEIO,
    ROMLIB_PPC_750_PME_ITLB_SEARCH,
    ROMLIB_PPC_750_PME_L2_HIT,
    ROMLIB_PPC_750_PME_FETCH,
    ROMLIB_PPC_750_PME_IABR_MATCH,
    ROMLIB_PPC_750_PME_LOAD_LATENCY,
    ROMLIB_PPC_750_PME_UNRESOLVED_BRANCH,
    ROMLIB_PPC_750_PME_DISPATCH_STALL,
    ROMLIB_PPC_750_PME_L1I_MISS,
    ROMLIB_PPC_750_PME_ITLB_MISS,
    ROMLIB_PPC_750_PME_L2I_MISS,
    ROMLIB_PPC_750_PME_BRANCH_NOT_TAKEN,
    ROMLIB_PPC_750_PME_PR_CHANGE,
    ROMLIB_PPC_750_PME_LWARX,
    ROMLIB_PPC_750_PME_LOAD_STORE,
    ROMLIB_PPC_750_PME_SNOOP,
    ROMLIB_PPC_750_PME_L1_CASTOUT,
    ROMLIB_PPC_750_PME_SYSTEM,
    ROMLIB_PPC_750_PME_FETCH_L1_MISS,
    ROMLIB_PPC_750_PME_BRANCH_OOO_CORRECT,
    ROMLIB_PPC_750_PME_L1D_MISS,
    ROMLIB_PPC_750_PME_DTLB_MISS,
    ROMLIB_PPC_750_PME_L2D_MISS,
    ROMLIB_PPC_750_PME_BRANCH_TAKEN,
    ROMLIB_PPC_750_PME_USER_PM_CHANGE,
    ROMLIB_PPC_750_PME_STWCX,
    ROMLIB_PPC_750_PME_FLOATING,
    ROMLIB_PPC_750_PME_L2_SNOOP_CASTOUT,
    ROMLIB_PPC_750_PME_CACHE_L2_HIT,
    ROMLIB_PPC_750_PME_LOAD_MISS_CYCLES,
    ROMLIB_PPC_750_PME_BRANCH_SECOND_CORRECT,
    ROMLIB_PPC_750_PME_BRANCH_DEPENDENCY,
    ROMLIB_PPC_750_PME_L2_CASTOUT,
    ROMLIB_PPC_750_PME_DTLB_SEARCH,
    ROMLIB_PPC_750_PME_BRANCH_MISPREDICT,
    ROMLIB_PPC_750_PME_SUPERVISOR_PM_CHANGE,
    ROMLIB_PPC_750_PME_STWCX_SUCCESS,
    ROMLIB_PPC_750_PME_SYNC,
    ROMLIB_PPC_750_PME_SNOOP_RETRY,
    ROMLIB_PPC_750_PME_INTEGER,
    ROMLIB_PPC_750_PME_BRANCH_STALL
};

/* Program the supervisor selectors 952..958. MMCR1 reserved bits read zero.
   MMCR0 writes rearm internal freeze/trigger state; a signaled request remains
   pending until exception entry. Writes to an already-negative enabled PMC can
   signal immediately, as permitted by MPC750UM 11.2. Instruction privilege and
   user-alias write rejection belong to the CPU front end. */
int ROMlib_ppc_750_performance_write(ROMlib_ppc_system750*, unsigned spr, uint32_t value, uint32_t pc);
int ROMlib_ppc_750_performance_event(ROMlib_ppc_system750*, unsigned event, uint64_t count, uint32_t pc);

/* Completed-instruction event mask. Synthetic host stubs are not instructions
   of the physical ISA and do not enter this interface. */
enum {
    ROMLIB_PPC_750_COMPLETE_INTEGER = 1,
    ROMLIB_PPC_750_COMPLETE_FLOATING = 2,
    ROMLIB_PPC_750_COMPLETE_MEMORY = 4,
    ROMLIB_PPC_750_COMPLETE_LWARX = 8,
    ROMLIB_PPC_750_COMPLETE_STWCX = 16,
    ROMLIB_PPC_750_COMPLETE_STWCX_SUCCESS = 32,
    ROMLIB_PPC_750_COMPLETE_SYNC = 64,
    ROMLIB_PPC_750_COMPLETE_EIEIO = 128,
    ROMLIB_PPC_750_COMPLETE_SYSTEM = 256,
    ROMLIB_PPC_750_COMPLETE_BRANCH = 512,
    ROMLIB_PPC_750_COMPLETE_TAKEN = 1024
};

void ROMlib_ppc_750_performance_complete(ROMlib_ppc_system750*, uint32_t pc, uint32_t msr, unsigned events);
void ROMlib_ppc_750_set_msr(ROMlib_ppc_system750*, uint32_t value, uint32_t pc);
void ROMlib_ppc_750_set_time_base(ROMlib_ppc_system750*, uint64_t value);
/* Side-effect SPR writes after instruction decoding/privilege checks. Return
   one for an accepted selector, zero for an invalid selector/configuration,
   or minus one when L2CR cannot acquire its required idle cache storage. */
int ROMlib_ppc_750_write_spr(ROMlib_ppc_system750*, unsigned selector, uint32_t value, uint32_t pc);

enum {
    ROMLIB_PPC_750_MCP = 1,
    ROMLIB_PPC_750_SMI = 2,
    ROMLIB_PPC_750_INT = 4,
    ROMLIB_PPC_750_PERFORMANCE = 8,
    ROMLIB_PPC_750_THERMAL = 16,
    ROMLIB_PPC_750_SRESET = 32,
    ROMLIB_PPC_750_CKSTP_IN = 64,
    ROMLIB_PPC_750_TLBISYNC = 128
};

/* Caller-serialized clock/input interface. One timer tick spans four bus
   clocks on the 750. No host wall-clock dependency.
   SRESET latches on assertion. MCP latches on assertion with HID0[EMCP]. CKSTP_IN freezes the core until hard reset. Other inputs are levels.
   Performance condition generation belongs to its SPR model; TAU adds an internal thermal request. */
void ROMlib_ppc_750_advance(ROMlib_ppc_system750*, uint64_t ticks);
/* TBEN gates time-base counting. Zero-initialized and reset state starts enabled. */
void ROMlib_ppc_750_set_tben(ROMlib_ppc_system750*, int asserted);
void ROMlib_ppc_750_set_decrementer(ROMlib_ppc_system750*, uint32_t);
void ROMlib_ppc_750_set_inputs(ROMlib_ppc_system750*, uint32_t);
/* Select at an instruction boundary: MCP, SMI, INT, performance, DEC, thermal.
   EE masks all but MCP. A DEC request clears only when accepted. Returns 1
   after entry, 0 with no eligible request, -1 in checkstop. */
int ROMlib_ppc_750_interrupt(ROMlib_ppc_system750*, ROMlib_ppc_registers*);

/* Functional reset with deterministic values for otherwise undefined state.
   MSR[IP]=1 and the first instruction address is FFF00100 (MPC750UM 2.1.6).
   Both L1 banks, HID0, reservations and pending bus checks are cleared. The environment binds execution and bus callbacks. */
void ROMlib_ppc_750_reset(ROMlib_ppc_system750*, ROMlib_ppc_registers*);
/* MPC750UM 4.3: MSR fields saved to and restored from SRR1. */
#define ROMLIB_PPC_750_SRR1_MSR_MASK UINT32_C(0x87C0FFFF)
/* Enter an already selected architectural exception. The caller owns pending
   input priority/masking. Cause supplies DSISR for DSI/alignment or exception
   bits for ISI/program/machine-check; address supplies DAR when applicable.
   Returns zero for checkstop or an invalid API argument. No memory is accessed.
   Syscall saves PC+4; all other inputs use the supplied current/next PC. */
int ROMlib_ppc_750_exception(ROMlib_ppc_system750*, ROMlib_ppc_registers*, unsigned vector, uint32_t cause,
                             uint32_t address);
/* Privilege is checked before modifying state. A user RFI enters program/privilege
   at its own PC. Supervisor RFI restores the 750's saved MSR fields and masks IA. */
int ROMlib_ppc_750_rfi(ROMlib_ppc_system750*, ROMlib_ppc_registers*);
#ifdef __cplusplus
}
#endif
#endif
