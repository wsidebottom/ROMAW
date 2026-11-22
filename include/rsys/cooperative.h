/*+===================================================================
  File:      cooperative.h

  Summary:   ifndef ROMAW_COOPERATIVE_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef ROMAW_COOPERATIVE_H
#define ROMAW_COOPERATIVE_H

/* Private ROMAW ABI, not the classic Thread Manager. The state occupies
   512 guest bytes and may only be restored in the process that saved it.
   Registers, SR, continuation and FPU state are saved; interpreter internals
   and pending hardware interrupts remain owned by the emulator. */
#define ROMAW_CONTEXT_TRAP 0xABFE
#define ROMAW_CONTEXT_SIZE 512
#define ROMAW_CONTEXT_MAGIC 0x46435458u
#define ROMAW_CONTEXT_VERSION 1
#define ROMAW_CONTEXT_TEMPLATE 0xffffffffu
/* Protect the Finder switcher's guest-global updates from VBL callbacks.
   A successful exchange consumes the guard; a deferred exchange requires END. */
#define ROMAW_CONTEXT_BEGIN_SWITCH 0xfffffffeu
#define ROMAW_CONTEXT_END_SWITCH 0xfffffffdu

extern uint32 ROMlib_cooperative_stack_low, ROMlib_cooperative_stack_high;
extern void* ROMlib_cooperative_interpreter;
extern int ROMlib_cooperative_depth;
extern unsigned ROMlib_cooperative_critical;
/* syn68k exports this counter in every interpreter dispatch configuration. */
extern int emulation_depth;

extern syn68k_addr_t ROMlib_exchange_guest_context(syn68k_addr_t pc, uint16 status);
#endif
