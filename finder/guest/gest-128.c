/*+===================================================================
  File:      gest-128.c

  Summary:   C translation of the Finder private Gestalt selector lookup.

  Classes:   C has no classes.

  Functions: finder_gest128

  Origin:    Translation of the original gest-128.bin Finder resource.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/

#include "finder_code.h"

int16_t finder_gest128(const FinderCodeServices* services, void* context, FinderGest128* state, uint32_t selector,
                       uint32_t response_address)
{
    if (!state->handle) {
        uint32_t handle;
        int16_t error = services->new_handle(context, 0, &handle);
        if (error < 0)
            return error;
        state->handle = handle;
    }
    int32_t size = services->handle_size(context, state->handle);
    if (size < 0) {
        uint32_t low = (uint32_t)size & 0xffff;
        return (int16_t)(low < 0x8000 ? (int32_t)low : (int32_t)low - 0x10000);
    }
    if (selector == UINT32_C(0x666e648c)) {
        services->write32(context, response_address, state->entry_address);
        return 0;
    }
    if (selector == UINT32_C(0x666e64f6)) {
        services->write32(context, response_address, state->handle);
        return 0;
    }

    uint32_t entry = services->read32(context, state->handle);
    /* Preserve ASL.L #8,D2 and SUBQ.W #1,D2 literally. This is NOT a
     * size / 8 bounded array search. The original relies on its private table
     * layout; the supplied guest-memory services must validate accesses.
     */
    uint32_t remaining = (uint32_t)size << 8;
    while (remaining && !(remaining & UINT32_C(0x80000000))) {
        remaining = (remaining & UINT32_C(0xffff0000)) | ((remaining - 1) & 0xffff);
        uint32_t candidate = services->read32(context, entry);
        if (services->failed && services->failed(context))
            return -5551;
        if (candidate == selector) {
            uint32_t response = services->read32(context, entry + 4);
            if (services->failed && services->failed(context))
                return -5551;
            services->write32(context, response_address, response);
            return 0;
        }
        entry += 8;
    }
    return -5551; /* gestaltUndefSelectorErr; response is untouched. */
}
