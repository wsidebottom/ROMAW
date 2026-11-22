/*+===================================================================
  File:      drag_handlers.h

  Summary:   Maintains native Drag Manager tracking and receive registrations.

  Classes:   None.

  Functions: ROMlib_carbon_drag_forget_window, ROMlib_carbon_drag_reset;
             private DragLib frame handlers.

  Origin:    DragLib 1.4 dispatch vectors and Universal Interfaces 3.4.2.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMLIB_CARBON_DRAG_HANDLERS_H
#define ROMLIB_CARBON_DRAG_HANDLERS_H

#include "cf_batch.h"

typedef struct carbon_drag_handler {
    struct carbon_drag_handler* next;
    uint64_t order;
    uint32_t procedure, window, context;
    bool tracking;
} carbon_drag_handler;

static carbon_drag_handler* drag_handlers;
static uint64_t last_drag_handler_order;

static bool drag_saved_dispatch_available(void)
{
    const uint32_t vector = cfp_read(0x1db4);
    /* ROMAW initializes unused Toolbox slots to a shared error handler. */
    const uint32_t original = (uint32_t)(uintptr_t)toolstuff[0x3ed].orig;
    return vector && vector != original;
}

#include "drag_data.h"

void ROMlib_carbon_drag_forget_window(uint32_t window)
{
    for (carbon_drag* drag = native_drags; drag; drag = drag->next)
        if (drag->hilite_port == window)
            drag_release_hilite(drag);

    carbon_drag_handler** link = &drag_handlers;
    while (*link) {
        carbon_drag_handler* entry = *link;
        if (entry->window == window) {
            *link = entry->next;
            free(entry);
        }
        else
            link = &entry->next;
    }
}

void ROMlib_carbon_drag_reset(void)
{
    reset_drag_data();
    while (drag_handlers) {
        carbon_drag_handler* entry = drag_handlers;
        drag_handlers = entry->next;
        free(entry);
    }
}

static int carbon_drag_registration(ROMlib_carbon_ppc_frame* call, const char* name, bool tracking, bool install)
{
    enum { duplicate_handler = -1855, handler_not_found = -1856 };

    const uint32_t procedure = call->words[0], window = call->words[1];
    ROMlib_carbon_reference saved;
    /* The original PEF calls the boot-installed Drag Manager vector. */
    if (drag_saved_dispatch_available() && ROMlib_carbon_load_reference("DragLib", name, &saved))
        return ROMlib_carbon_invoke_frame(saved.function, call);

    if (!procedure) {
        call->words[0] = (uint32_t)paramErr;
        return 1;
    }
    carbon_drag_handler** link = &drag_handlers;
    while (*link && ((*link)->procedure != procedure || (*link)->window != window || (*link)->tracking != tracking))
        link = &(*link)->next;

    OSErr status = noErr;
    if (install) {
        if (*link)
            status = duplicate_handler;
        else {
            carbon_drag_handler* entry = malloc(sizeof *entry);
            if (entry) {
                *entry =
                    (carbon_drag_handler){NULL, ++last_drag_handler_order, procedure, window, call->words[2], tracking};
                *link = entry;
            }
            else
                status = memFullErr;
        }
    }
    else if (*link) {
        carbon_drag_handler* entry = *link;
        *link = entry->next;
        free(entry);
    }
    else
        status = handler_not_found;
    call->words[0] = (uint32_t)status;
    return 1;
}

#define CARBON_DRAG_HANDLER(name, tracking, install)                     \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call)              \
    {                                                                    \
        return carbon_drag_registration(call, #name, tracking, install); \
    }

CARBON_DRAG_HANDLER(InstallTrackingHandler, true, true)
CARBON_DRAG_HANDLER(InstallReceiveHandler, false, true)
CARBON_DRAG_HANDLER(RemoveTrackingHandler, true, false)
CARBON_DRAG_HANDLER(RemoveReceiveHandler, false, false)

#undef CARBON_DRAG_HANDLER
#endif
