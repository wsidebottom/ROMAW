/*+===================================================================
  File:      intl_resource.c

  Summary:   Implement the bundled INTL 1 localization procedure in C.

  Classes:   None.

  Functions: ROMlib_intl1_localization, ROMlib_intl1_definition

  Origin:    C translation of the bundled Intl1Rec.localRtn RTS instruction.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/

#include "rsys/common.h"
#include "rsys/intl_resource.h"

void ROMlib_intl1_localization(void)
{
    /* The original localRtn returned immediately, without modifying state. */
}

static syn68k_addr_t intl1_callback(syn68k_addr_t pc, void* unused)
{
    (void)pc;
    (void)unused;
    ROMlib_intl1_localization();
    /* The MOVEM veneer saves A0 above the caller's original return address.
     * Neither that veneer nor this callback modifies guest condition codes.
     */
    EM_A0 = POPUL();
    return POPADDR();
}

ProcPtr ROMlib_intl1_definition(void)
{
    static syn68k_addr_t entry;
    if (!entry)
        entry = callback_install(intl1_callback, NULL);
    return (ProcPtr)SYN68K_TO_US(entry);
}
