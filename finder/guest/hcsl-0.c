/*+===================================================================
  File:      hcsl-0.c

  Summary:   C translation of the Finder hcsl Gestalt installer and handler.

  Classes:   C has no classes.

  Functions: finder_hcsl0_install, finder_hcsl0_query

  Origin:    Translation of the original hcsl-0.bin Finder resource.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/

#include "finder_code.h"

void finder_hcsl0_install(FinderHcsl0* state, uint32_t response, FinderHcslRegister register_handler, void* context)
{
    state->response = response;
    /* The installer calls _NewGestalt but does not test or return its error. */
    (void)register_handler(context, UINT32_C(0x6863736c), state);
}

int16_t finder_hcsl0_query(const FinderCodeServices* services, void* context, const FinderHcsl0* state,
                           uint32_t response_address)
{
    services->write32(context, response_address, state->response);
    return 0;
}
