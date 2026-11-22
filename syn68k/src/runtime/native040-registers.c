/*+===================================================================
  File:      native040-registers.c

  Summary:   Instantiate closed raw floating-register load/store primitives with ordered mapped and flat transfers.

  Classes:   None.

  Functions: Build-time native040_load_fprs and native040_store_fprs roots.

  Origin:    Original ARDI Executor direct 68040 lowering using the maintained native scalar memory roots.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "native040-registers.h"
#include "native040-memory.h"

static int scalar(Syn68kNative040Registers* frame, unsigned offset, unsigned size, int writing, uint32_t* value)
{
    Syn68kNative040MMU request = {.memory = frame->memory,
                                  .ram = frame->ram,
                                  .address = frame->address + offset,
                                  .size = size,
                                  .fc = (frame->cpu->sr >> 11 & 4) | 1,
                                  .value = writing ? *value : 0};
    if (writing)
        native040_write(&request);
    else
        native040_read(&request);
    frame->access = request.access;
    if (request.access.bus_error || request.access.translation.fault != MMU040_OK)
        return 0;
    if (!writing)
        *value = request.value;
    return 1;
}

/* The frontend owns instruction validation, list selection and EA updates.
   This primitive consumes no instruction bytes and classifies no FP values. */
static void transfer(Syn68kNative040Registers* frame, int writing)
{
    if (!frame)
        return;
    frame->ok = 0;
    frame->store_tail_valid = 0;
    frame->access = (Syn68kMMU040Access){0};
    if (!frame->cpu || !frame->memory || !frame->memory->mmu || frame->mask > 255 || frame->reverse > 1)
        return;
    for (unsigned reg = 0; reg < 8; ++reg) {
        unsigned bit = frame->reverse ? reg : 7 - reg;
        if (!(frame->mask & (1u << bit)))
            continue;
        if (writing) {
            // Each register is captured after callbacks from preceding registers.
            Syn68kFP80 source = frame->cpu->fpu.regs[reg];
            Syn68kNative040MMU span = {
                .memory = frame->memory, .address = frame->address, .size = 12, .fc = (frame->cpu->sr >> 11 & 4) | 1};
            native040_span(&span);
            if (!span.ok) {
                frame->access = span.access;
                return;
            }
            uint32_t high = source.exponent, padding = 0, middle = source.significand >> 32, low = source.significand;
            if (!scalar(frame, 0, 2, 1, &high) || !scalar(frame, 2, 2, 1, &padding))
                return;
            frame->store_tail = low;
            frame->store_tail_valid = 1;
            if (!scalar(frame, 4, 4, 1, &middle))
                return;
            frame->store_tail_valid = 0;
            if (!scalar(frame, 8, 4, 1, &low))
                return;
        }
        else {
            uint32_t high, middle, low;
            if (!scalar(frame, 0, 4, 0, &high) || !scalar(frame, 4, 4, 0, &middle) || !scalar(frame, 8, 4, 0, &low))
                return;
            // A partial value must not replace even the exponent of its FPR.
            frame->cpu->fpu.regs[reg].exponent = high >> 16;
            frame->cpu->fpu.regs[reg].significand = (uint64_t)middle << 32 | low;
        }
        frame->address += 12;
    }
    frame->ok = 1;
}

__attribute__((used, noinline)) void native040_load_fprs(Syn68kNative040Registers* frame)
{
    transfer(frame, 0);
}

__attribute__((used, noinline)) void native040_store_fprs(Syn68kNative040Registers* frame)
{
    transfer(frame, 1);
}
