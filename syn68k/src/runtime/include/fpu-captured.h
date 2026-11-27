/*+===================================================================
  File:      fpu-captured.h

  Summary:   Resume checked FPU exceptions and describe retired or restartable store faults.

  Classes:   None.

  Functions: syn68k_fpu_finish_frame, syn68k_fpu_finish_captured, syn68k_fpu_finish_destination, syn68k_fpu_finish_command and
             syn68k_fpu_finish_command_at declarations; syn68k_fpu_final_store_fault and syn68k_fpu_replay_store_fault
             inline metadata definitions.

  Origin:    Original ARDI Executor direct 68040 FPU integration.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SYN68K_FPU_CAPTURED_H
#define SYN68K_FPU_CAPTURED_H
#include "fpu.h"
#include "native040.h"

/* Operation selectors implemented by FPSP. Selectors 64 through 127 are absent. */
#define SYN68K_FPU_FPSP_OPERATIONS UINT64_C(0x00ff0062f277f74e)

/* FSAVE permits control-alterable and predecrement forms. FRESTORE permits
   control and postincrement forms. Neither instruction takes an immediate. */
static inline int syn68k_fpu_frame_ea_valid(unsigned opcode)
{
    unsigned mode = (opcode >> 3) & 7;
    int writing = !(opcode & 0x40);
    if (mode < 2 || mode == (writing ? 3 : 4))
        return 0;
    return mode != 7 || (opcode & 7) <= (writing ? 1u : 3u);
}
/* Fault metadata only: the caller has finished all FPU/EA work and offers
   its last scalar transfer. WB1 retains the full operand after physical bus
   subcycles; WB2 does so after a first-page ATC prefix. A physical cache push
   instead retains PD0-3 and may have already committed this operand in cache.
   The stream runner independently validates progress and pending WB2/WB3. */
static inline void syn68k_fpu_final_store_fault(Syn68kFPUStep* result, uint32_t next)
{
    const Syn68kMMU040Access* access = &result->access;
    uint16_t ssw;
    uint16_t push_ssw;
    int push = syn68k_mmu040_cache_push_ssw(access, &push_ssw);
    if (result->vector == 2 && result->format == 7 && !(next & 1) && access->writing && !access->rmw &&
        (access->completed < access->size || push) &&
        access->logical == (uint32_t)(access->request + access->completed) &&
        (access->size == 1 || access->size == 2 || access->size == 4) && (access->fc == 1 || access->fc == 5) &&
        syn68k_mmu040_operand_status(access, &ssw) && !(access->bus_error && (ssw & 0x400)) &&
        (push || !access->completed || access->bus_error || ((ssw & 0x400) && !(access->logical & 0xfff)))) {
        result->store_next = next;
        result->store_state = 1;
    }
}

/* An unfinished pure FPU store can offer abort/replay, never retirement.
   The runner must certify every architectural register before accepting it.
   WB1 retains a physical fault operand; WB2 retains an ATC fault operand.
   A cache push retains PD0-3 and any incomplete logical operand in WB2.
   No abort case introduces a continuation bit or changes the saved PC. */
static inline void syn68k_fpu_replay_store_fault(Syn68kFPUStep* result)
{
    const Syn68kMMU040Access* access = &result->access;
    uint16_t ssw;
    uint16_t push_ssw;
    int push = syn68k_mmu040_cache_push_ssw(access, &push_ssw);
    if (!result->store_state && result->vector == 2 && result->format == 7 && !(result->exception_pc & 1) &&
        access->writing && !access->rmw && (access->completed < access->size || push) &&
        (access->size == 2 || access->size == 4) &&
        access->logical == (uint32_t)(access->request + access->completed) && (access->fc == 1 || access->fc == 5) &&
        syn68k_mmu040_operand_status(access, &ssw) && !(access->bus_error && (ssw & 0x400)) &&
        (push || access->bus_error || ((ssw & 0x400) && (!access->completed || !(access->logical & 0xfff))))) {
        result->store_next = result->exception_pc;
        result->store_state = 2;
    }
}

typedef struct {
    uint32_t fpcr;
    unsigned original_null;
    /* The originating instruction passed its pending-frame guard. */
    unsigned frame_checked;
} Syn68kFPUEntry;

typedef struct {
    uint32_t pc, next, ea;
    unsigned opcode, command, original_null;
    Syn68kNative040Float source;
    unsigned unsupported, signaling, source_tag;
    Syn68kMMU040Access access;
    /* Optional instruction-entry controls; callbacks may change live FPCR. */
    unsigned has_entry;
    Syn68kFPUEntry entry;
} Syn68kFPUCaptured;

typedef struct {
    uint32_t pc, next, ea, header;
    unsigned opcode;
    Syn68kFPUEntry entry;
    const uint8_t* image;
    unsigned image_size;
} Syn68kFPUFrame;

/* Exceptional FSAVE/FRESTORE continuation after EA evaluation. A restore
   header has already been read once; busy-frame recovery consumes only the
   remaining architectural transfers. No EA or initial header is replayed. */
int syn68k_fpu_finish_frame(const Syn68kFPUFrame* captured, Syn68kMMU040Memory* memory, Syn68kFPUStep* result);

/* Checked exceptional exit only, outside the originating native frame. The
   caller has validated a supported, non-packed memory-source arithmetic
   command, cleared null state before EA evaluation, fetched its complete
   operand and committed its EA updates once. Later callback state is retained.
   No command/EA/device reads are repeated. Invalid packets return zero before
   changing CPU state or *result. Serialized like the ordinary FPU model. */
int syn68k_fpu_finish_captured(const Syn68kFPUCaptured* captured, Syn68kFPUStep* result);
/* Checked memory move-out exit after capturing the source register and
   evaluating its destination once. Conversion exceptions may perform their
   architecturally required stores through memory, without repeating the EA. */
int syn68k_fpu_finish_destination(const Syn68kFPUCaptured* captured, Syn68kMMU040Memory* memory, Syn68kFPUStep* result);
/* Checked exit before EA/operand consumption. A mapped native body has already
   fetched the class-0 opcode and command exactly once. Do not reread a device
   instruction stream while reporting an unsupported or exceptional command.
   Pending-frame exits precede command fetch and use the ordinary step API. */
int syn68k_fpu_finish_command(uint32_t pc, unsigned opcode, unsigned command, Syn68kMMU040Memory* memory,
                              Syn68kFPUStep* result);
/* A device callback may change architectural control state during command
   fetch. Retain entry rounding/null state without undoing those changes.
   This entry also accepts an already consumed F240 condition word. */
int syn68k_fpu_finish_command_at(uint32_t pc, unsigned opcode, unsigned command, Syn68kMMU040Memory* memory,
                                 const Syn68kFPUEntry* entry, Syn68kFPUStep* result);
#endif
