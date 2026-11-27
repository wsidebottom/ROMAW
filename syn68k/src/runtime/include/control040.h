/*+===================================================================
  File:      control040.h

  Summary:   Share 68040 control-register transfers between checked execution and closed native roots.

  Classes:   None.

  Functions: Private inline control-register and stack-bank transfer definitions.

  Origin:    Extracted from maintained MOVEC execution in syn68k/src/runtime/trap.c.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#ifndef SYN68K_CONTROL040_H
#define SYN68K_CONTROL040_H
#include "trap.h"
#include "mmu040-memory.h"

static inline int mmu_translation_opcode(unsigned opcode)
{
    return (opcode & 0xfff0) == 0xf500 || opcode == 0xf510 || opcode == 0xf518 || (opcode & 0xffd8) == 0xf548;
}

static inline uint32 syn68k_control040_bank(CPUState* cpu, unsigned selector, int writing, uint32 value)
{
    uint32* bank;
    int active;
    if (selector == 0x800) {
        bank = &cpu->usp;
        active = USE_USP(cpu->sr);
    }
    else if (selector == 0x803) {
        bank = &cpu->msp;
        active = USE_MSP(cpu->sr);
    }
    else {
        bank = &cpu->isp;
        active = USE_ISP(cpu->sr);
    }
    if (active)
        bank = &cpu->regs[15].ul.n;
    if (writing)
        *bank = value;
    return *bank;
}

/* The instruction's privilege and command-fetch guards precede this transfer.
   Failure leaves both CPU state and the value unchanged. */
static inline int syn68k_control040_transfer(CPUState* cpu, Syn68kMMU040Memory* memory, unsigned selector, int writing,
                                             uint32* output)
{
    uint32 value = *output;
    switch (selector) {
    case 0:
        if (writing)
            cpu->sfc = value & 7;
        value = cpu->sfc;
        break;
    case 1:
        if (writing)
            cpu->dfc = value & 7;
        value = cpu->dfc;
        break;
    case 2:
        if (writing)
            cpu->cacr = value & 0x80008000u;
        value = cpu->cacr & 0x80008000u;
        break;
    case 0x800:
    case 0x803:
    case 0x804:
        value = syn68k_control040_bank(cpu, selector, writing, value);
        break;
    case 0x801:
        if (writing)
            cpu->vbr = value;
        value = cpu->vbr;
        break;
    default: {
        enum Syn68kMMU040Control control;
        switch (selector) {
        case 3:
            control = MMU040_TC;
            break;
        case 4:
            control = MMU040_ITT0;
            break;
        case 5:
            control = MMU040_ITT1;
            break;
        case 6:
            control = MMU040_DTT0;
            break;
        case 7:
            control = MMU040_DTT1;
            break;
        case 0x805:
            control = MMU040_MMUSR;
            break;
        case 0x806:
            control = MMU040_URP;
            break;
        case 0x807:
            control = MMU040_SRP;
            break;
        default:
            return 0;
        }
        /* Only the explicit mapped backend owns a connected MMU. Never
           accept ineffective enable writes in the flat ROMAW path. */
        if (!memory || !memory->mmu || !syn68k_mmu040_control(memory->mmu, control, writing, &value))
            return 0;
        break;
    }
    }
    *output = value;
    return 1;
}
#endif
