/*+===================================================================
  File:      fpu.h

  Summary:   Declares fpu support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: syn68k_fpu_step and syn68k_fpu_step_decoded declarations; syn68k_fpu_trace_result inline exception metadata.

  Origin:    Existing repository source in syn68k/src/runtime/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SYN68K_FPU_H
#define SYN68K_FPU_H

#include "mmu040-memory.h"

typedef struct {
    uint32_t next, exception_pc, fault_address;
    unsigned vector, format;
    Syn68kMMU040Access access;
    /* Final move-out, FMOVEM, control-list, FSAVE and FScc stores can
       use WB1/WB2/WB3. Physical and ATC writebacks retain the whole scalar;
       cleanup can repeat completed bus-subcycle or first-page prefixes.
       1 offers already completed FPU/EA state and the following PC; it does
       not retire the component step. A pending post-instruction exception is
       captured separately, with its original calculated EA.
       2 offers an unfinished control-list, FMOVEM or FSAVE
       store for checked abort/replay from its original PC; no result is retired.
       3 offers an early FMOVE.X store with computed FPSR/FPIAR retained;
       sources and all remaining state must still match the entry checkpoint. */
    uint32_t store_next;
    unsigned store_state;
    uint32_t post_ea;
    unsigned post_vector;
    /* One following longword at access.request + 4. Move-out/FMOVEM capture
       before the failed callback; controls/FSAVE sample the following live
       source afterward. Retired stores do not repeat earlier transfers. */
    uint32_t store_tail;
    unsigned store_tail_valid;
    /* FSAVE clears its saved-frame state only when the runner retires it. */
    unsigned store_frame_clear;
    /* Data function code for the following word after the failed callback. */
    unsigned store_tail_fc;
    /* MC68040 T0 eligibility: pipeline-serializing FPU forms or a taken FBcc.
       FDBcc always qualifies, including an exhausted or unchanged counter. */
    unsigned trace_flow;
    /* Replay-only operand size for an auto-update control-list, FMOVEM, FSAVE or FMOVE.X EA.
       The runner checks the whole register file against the expected update
       before restoring just that address register for instruction restart. */
    unsigned store_ea_size;
    /* Numeric output status captured before any destination store callback. */
    uint32_t replay_fpsr;
    /* Decoded speculative branch reference. Only odd references are retained;
       an untaken branch can still raise an address error without a bus cycle. */
    uint32_t prefetch_address;
} Syn68kFPUStep;

/* Dispatch bookkeeping: component stepping leaves prefetch errors and trace
   to its caller. Forced/FPU exceptions precede prefetch errors, which precede
   normal trace. Trace eligibility uses the instruction-entry SR. */
static inline void syn68k_fpu_trace_result(Syn68kFPUStep* result, uint16_t entry_sr, uint32_t pc)
{
    if (!result->vector && ((result->prefetch_address | result->next) & 1)) {
        uint32_t reference = result->prefetch_address & 1 ? result->prefetch_address : result->next;
        result->vector = 3;
        result->format = 2;
        result->exception_pc = pc;
        result->fault_address = reference & ~1u;
    }
    if (!result->vector && ((entry_sr & 0x8000) || ((entry_sr & 0x4000) && result->trace_flow))) {
        result->vector = 9;
        result->format = 2;
        result->exception_pc = result->next;
        result->fault_address = pc;
    }
}

/* Internal single-instruction boundary. Uses the supplied logical memory bus,
   or flat memory when NULL. Returns zero for a non-FPU opcode (and for an
   unavailable flat opcode, preserving the existing dispatcher contract).
   PC must be word aligned; the CPU dispatcher owns alignment and trace exceptions.
   A mapped opcode/operand fault returns one and reports vector 2. The caller
   owns exception delivery, restart/writeback handling and mapped instruction-
   cache coherence. Completed flat stores invalidate translated blocks. This
   function does not construct a guest exception stack. State
   is serialized like the existing FPU dispatcher, not reentrant or thread safe. */
int syn68k_fpu_step(uint32_t pc, Syn68kMMU040Memory* memory, Syn68kFPUStep* result);
/* Same serialized dispatch after the stream romaw fetched the opcode. */
int syn68k_fpu_step_decoded(uint32_t pc, unsigned opcode, Syn68kMMU040Memory* memory, Syn68kFPUStep* result);

#endif
