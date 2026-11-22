/*+===================================================================
  File:      native040-control.c

  Summary:   Instantiate fixed native MOVEC read/write roots after privilege and command-fetch guards.

  Classes:   None.

  Functions: Build-time native040_movec_read and native040_movec_write roots.

  Origin:    Original ARDI Executor direct 68040 lowering, sharing maintained control-register semantics.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "native040-control.h"
#include "control040.h"

static inline void transfer(Syn68kNative040Control* frame, int writing)
{
    frame->ok = 0;
    if (!frame->cpu || frame->command > 65535)
        return;
    unsigned reg = frame->command >> 12;
    uint32 value = frame->cpu->regs[reg].ul.n;
    if (!syn68k_control040_transfer(frame->cpu, frame->memory, frame->command & 0xfff, writing, &value))
        return;
    if (!writing)
        frame->cpu->regs[reg].ul.n = value;
    frame->ok = 1;
}

__attribute__((used, noinline)) void native040_movec_read(Syn68kNative040Control* frame)
{
    transfer(frame, 0);
}

__attribute__((used, noinline)) void native040_movec_write(Syn68kNative040Control* frame)
{
    transfer(frame, 1);
}
