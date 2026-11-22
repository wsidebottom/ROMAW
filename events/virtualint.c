/*+===================================================================
  File:      virtualint.c

  Summary:   Implements virtualint support for Macintosh event handling and virtual interrupts.

  Classes:   None.

  Functions: do_virtual_interrupt

  Origin:    Existing repository source in executor/src/events; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"

#if defined(SYN68K)

#include "rsys/blockinterrupts.h"

void do_virtual_interrupt(void)
{
    syn68k_addr_t pc;

    pc = interrupt_process_any_pending(MAGIC_EXIT_EMULATOR_ADDRESS);
    if (pc != MAGIC_EXIT_EMULATOR_ADDRESS) {
        interpret_code(hash_lookup_code_and_create_if_needed(pc));
    }
}

#endif /* SYN68K */
