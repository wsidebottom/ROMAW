/*+===================================================================
  File:      drag_data.h

  Summary:   Stores native Drag Manager objects, items, and flavor data.

  Classes:   None.

  Functions: Private DragLib frame handlers.

  Origin:    CarbonLib 1.4 DragLib re-exports and Universal Interfaces 3.4.2 Drag.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMLIB_CARBON_DRAG_DATA_H
#define ROMLIB_CARBON_DRAG_DATA_H

#include "cf_batch.h"

enum {
    drag_bad_reference = -1850,
    drag_bad_item = -1851,
    drag_bad_flavor = -1852,
    drag_duplicate_flavor = -1853,
    drag_cannot_get_flavor = -1854,
    drag_data_promised = 1 << 9
};

typedef struct carbon_drag_flavor {
    struct carbon_drag_flavor* next;
    uint32_t type, flags, size;
    Byte* bytes;
    bool requesting;
} carbon_drag_flavor;

typedef struct carbon_drag_item {
    struct carbon_drag_item* next;
    uint32_t reference;
    uint16_t count;
    Rect bounds;
    carbon_drag_flavor* flavors;
} carbon_drag_item;

typedef struct carbon_drag {
    struct carbon_drag* next;
    uint32_t reference, send_proc, send_context;
    uint32_t drop_type;
    Size drop_size;
    Byte* drop_bytes;
    uint32_t tracking_region, attributes, input_proc, input_context;
    uint32_t drawing_proc, drawing_context, behaviors;
    struct drag_feedback* feedback;
    uint32_t image, image_mask, image_flags;
    Point image_offset;
    bool image_has_alpha;
    uint32_t hilite_region, hilite_port;
    bool hilite_suspended;
    Point origin, mouse, pinned;
    INTEGER modifiers, down_modifiers, up_modifiers;
    bool tracking;
    uint16_t count;
    carbon_drag_item* items;
} carbon_drag;

static carbon_drag* native_drags;
static uint32_t last_drag_reference;
static bool tracking_drag;

static void drag_release_hilite(carbon_drag* drag);

static void free_drag(carbon_drag* drag)
{
    drag_release_hilite(drag);
    free(drag->drop_bytes);
    while (drag->items) {
        carbon_drag_item* item = drag->items;
        drag->items = item->next;
        while (item->flavors) {
            carbon_drag_flavor* flavor = item->flavors;
            item->flavors = flavor->next;
            free(flavor->bytes);
            free(flavor);
        }
        free(item);
    }
    free(drag);
}

static void reset_drag_data(void)
{
    while (native_drags) {
        carbon_drag* drag = native_drags;
        native_drags = drag->next;
        free_drag(drag);
    }
}

static int drag_result(ROMlib_carbon_ppc_frame* call, int status)
{
    call->words[0] = (uint32_t)status;
    return 1;
}

static carbon_drag** find_drag(uint32_t reference)
{
    carbon_drag** link = &native_drags;
    while (*link && (*link)->reference != reference)
        link = &(*link)->next;
    return link;
}

static carbon_drag_item** find_drag_item(carbon_drag* drag, uint32_t reference)
{
    carbon_drag_item** link = &drag->items;
    while (*link && (*link)->reference != reference)
        link = &(*link)->next;
    return link;
}

static carbon_drag_flavor** find_drag_flavor(carbon_drag_item* item, uint32_t type)
{
    carbon_drag_flavor** link = &item->flavors;
    while (*link && (*link)->type != type)
        link = &(*link)->next;
    return link;
}

static int new_drag(ROMlib_carbon_ppc_frame* call)
{
    void* output = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], 4, 1) : NULL;
    if (!output)
        return drag_result(call, paramErr);
    if (last_drag_reference == UINT32_MAX)
        return drag_result(call, memFullErr);
    carbon_drag* drag = calloc(1, sizeof *drag);
    if (!drag)
        return drag_result(call, memFullErr);
    drag->reference = ++last_drag_reference;
    drag->drop_type = typeNull;
    drag->behaviors = 1;
    drag->next = native_drags;
    native_drags = drag;
    const uint32_t value = CL(drag->reference);
    memcpy(output, &value, sizeof value);
    return drag_result(call, noErr);
}

static int add_drag_flavor(carbon_drag* drag, carbon_drag_item** item, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t type = call->words[2], address = call->words[3], size = address ? call->words[4] : 0;
    if (size > INT32_MAX)
        return drag_result(call, paramErr);
    const void* data = size ? ROMlib_carbon_try_guest_memory(address, size, 0) : NULL;
    if (size && !data)
        return drag_result(call, paramErr);
    carbon_drag_flavor** link = *item ? find_drag_flavor(*item, type) : NULL;
    if (link && *link)
        return drag_result(call, drag_duplicate_flavor);
    if (*item ? (*item)->count == UINT16_MAX : drag->count == UINT16_MAX)
        return drag_result(call, memFullErr);

    carbon_drag_flavor* flavor = calloc(1, sizeof *flavor);
    if (!flavor)
        return drag_result(call, memFullErr);
    if (size) {
        flavor->bytes = malloc(size);
        if (!flavor->bytes) {
            free(flavor);
            return drag_result(call, memFullErr);
        }
        memcpy(flavor->bytes, data, size);
    }
    if (!*item) {
        carbon_drag_item* created = calloc(1, sizeof *created);
        if (!created) {
            free(flavor->bytes);
            free(flavor);
            return drag_result(call, memFullErr);
        }
        created->reference = call->words[1];
        *item = created;
        ++drag->count;
        link = &created->flavors;
    }
    flavor->type = type;
    flavor->flags = (call->words[5] & ~drag_data_promised) | (address ? 0 : drag_data_promised);
    flavor->size = size;
    *link = flavor;
    ++(*item)->count;
    return drag_result(call, noErr);
}

static int set_drag_flavor(carbon_drag_flavor* flavor, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t size = call->words[4], offset = call->words[5], address = call->words[3];
    if (offset > INT32_MAX || size > INT32_MAX - offset)
        return drag_result(call, paramErr);
    const void* data = size && address ? ROMlib_carbon_try_guest_memory(address, size, 0) : NULL;
    if (size && !data)
        return drag_result(call, paramErr);
    const uint32_t end = offset + size;
    if (size && end > flavor->size) {
        Byte* bytes = realloc(flavor->bytes, end);
        if (!bytes)
            return drag_result(call, memFullErr);
        if (offset > flavor->size)
            memset(bytes + flavor->size, 0, offset - flavor->size);
        flavor->bytes = bytes;
        flavor->size = end;
    }
    if (size)
        memcpy(flavor->bytes + offset, data, size);
    flavor->flags &= ~drag_data_promised;
    return drag_result(call, noErr);
}

static carbon_drag_flavor* request_drag_flavor(const ROMlib_carbon_ppc_frame* call, const carbon_drag* drag,
                                               carbon_drag_flavor* flavor)
{
    if (!(flavor->flags & drag_data_promised))
        return flavor;
    if (!drag->send_proc || flavor->requesting)
        return NULL;
    ROMlib_carbon_ppc_frame request = *call;
    request.words[0] = call->words[2];
    request.words[1] = drag->send_context;
    request.words[2] = call->words[1];
    request.words[3] = call->words[0];
    uint32_t status;
    flavor->requesting = true;
    const int invoked = ROMlib_carbon_invoke_pascal(drag->send_proc, 0x3fe0, &request, &status);
    /* The sender may dispose of the drag while fulfilling the promise. */
    carbon_drag* live = *find_drag(call->words[0]);
    carbon_drag_item* item = live ? *find_drag_item(live, call->words[1]) : NULL;
    flavor = item ? *find_drag_flavor(item, call->words[2]) : NULL;
    if (!flavor)
        return NULL;
    flavor->requesting = false;
    return invoked && !(int16_t)status && !(flavor->flags & drag_data_promised) ? flavor : NULL;
}

enum drag_data_operation {
    drag_create,
    drag_dispose,
    drag_add_flavor,
    drag_set_flavor,
    drag_set_send_proc,
    drag_count_items,
    drag_item_reference,
    drag_count_flavors,
    drag_flavor_type,
    drag_flavor_flags,
    drag_flavor_size,
    drag_flavor_data,
    drag_get_bounds,
    drag_set_bounds,
    drag_get_drop_location,
    drag_set_drop_location,
    drag_track,
    drag_get_region,
    drag_set_region,
    drag_get_attributes,
    drag_get_mouse,
    drag_set_mouse,
    drag_get_origin,
    drag_get_modifiers,
    drag_set_input_proc,
    drag_set_drawing_proc,
    drag_show_highlight,
    drag_hide_highlight,
    drag_update_highlight,
    drag_pre_scroll,
    drag_post_scroll,
    drag_set_classic_image,
    drag_set_alpha_image,
    drag_change_behaviors
};

static int drag_output(ROMlib_carbon_ppc_frame* call, uint32_t address, const void* value, size_t size)
{
    void* output = address ? ROMlib_carbon_try_guest_memory(address, size, 1) : NULL;
    if (!output)
        return drag_result(call, paramErr);
    memcpy(output, value, size);
    return drag_result(call, noErr);
}

static int drag_flavor_payload(ROMlib_carbon_ppc_frame* call, const carbon_drag* drag, carbon_drag_flavor* flavor,
                               bool copy)
{
    const uint32_t size_address = call->words[copy ? 4 : 3];
    void* size_output = size_address ? ROMlib_carbon_try_guest_memory(size_address, 4, 1) : NULL;
    if (!size_output)
        return drag_result(call, paramErr);
    uint32_t capacity = 0;
    if (copy) {
        memcpy(&capacity, size_output, 4);
        capacity = CL(capacity);
        if (capacity > INT32_MAX || (capacity && !call->words[3]))
            return drag_result(call, paramErr);
    }
    flavor = request_drag_flavor(call, drag, flavor);
    if (!flavor)
        return drag_result(call, drag_cannot_get_flavor);
    uint32_t size = flavor->size;
    if (copy) {
        const uint32_t offset = call->words[5];
        size = offset < size ? MIN(capacity, size - offset) : 0;
        if (size) {
            void* output = ROMlib_carbon_try_guest_memory(call->words[3], size, 1);
            if (!output)
                return drag_result(call, paramErr);
            memmove(output, flavor->bytes + offset, size);
        }
    }
    const uint32_t value = CL(size);
    return drag_output(call, size_address, &value, sizeof value);
}

static int drag_drop_location(ROMlib_carbon_ppc_frame* call, carbon_drag* drag, bool set)
{
    const uint32_t address = call->words[1];
    AEDesc* descriptor = address ? ROMlib_carbon_try_guest_memory(address, sizeof *descriptor, !set) : NULL;
    if (!descriptor)
        return drag_result(call, paramErr);
    if (!set) {
        AEDesc copy = {0};
        DESC_TYPE_X(&copy) = CL(typeNull);
        const OSErr error = drag->drop_type == typeNull
                                ? noErr
                                : C_AECreateDesc(drag->drop_type, (Ptr)drag->drop_bytes, drag->drop_size, &copy);
        if (!error)
            memcpy(descriptor, &copy, sizeof copy);
        return drag_result(call, error);
    }

    const uint32_t type = DESC_TYPE(descriptor);
    const uint32_t handle = type == typeNull ? 0 : CL((uint32_t)(uintptr_t)PP_RAW(DESC_DATA_X(descriptor)));
    uint32_t size = 0;
    const void* source = handle ? cfp_handle_data(handle, &size, 0) : NULL;
    if (handle && !source)
        return drag_result(call, paramErr);
    Byte* bytes = size ? malloc(size) : NULL;
    if (size && !bytes)
        return drag_result(call, memFullErr);
    if (size)
        memcpy(bytes, source, size);
    free(drag->drop_bytes);
    drag->drop_type = type;
    drag->drop_size = size;
    drag->drop_bytes = bytes;
    return drag_result(call, noErr);
}

#include "drag_tracking.h"

static int carbon_drag_data(ROMlib_carbon_ppc_frame* call, const char* name, enum drag_data_operation operation)
{
    carbon_drag** link = operation == drag_create ? NULL : find_drag(call->words[0]);
    carbon_drag* drag = link ? *link : NULL;
    ROMlib_carbon_reference saved;
    if (!drag && drag_saved_dispatch_available() && ROMlib_carbon_load_reference("DragLib", name, &saved))
        return ROMlib_carbon_invoke_frame(saved.function, call);
    if (operation == drag_create)
        return new_drag(call);
    if (!drag)
        return drag_result(call, drag_bad_reference);
    switch (operation) {
    case drag_dispose:
        if (drag->tracking)
            return drag_result(call, paramErr);
        if (!drag_clear_hilite(drag))
            return 0;
        *link = drag->next;
        free_drag(drag);
        return drag_result(call, noErr);
    case drag_set_send_proc:
        drag->send_proc = call->words[1];
        drag->send_context = call->words[2];
        return drag_result(call, noErr);
    case drag_get_drop_location:
    case drag_set_drop_location:
        return drag_drop_location(call, drag, operation == drag_set_drop_location);
    case drag_track:
    case drag_get_region:
    case drag_set_region:
    case drag_get_attributes:
    case drag_get_mouse:
    case drag_set_mouse:
    case drag_get_origin:
    case drag_get_modifiers:
    case drag_set_input_proc:
    case drag_set_drawing_proc:
    case drag_change_behaviors:
    case drag_set_classic_image:
    case drag_set_alpha_image:
    case drag_show_highlight:
    case drag_hide_highlight:
    case drag_update_highlight:
    case drag_pre_scroll:
    case drag_post_scroll:
        return drag_tracking_operation(call, drag, operation);
    case drag_count_items: {
        const uint16_t count = CW(drag->count);
        return drag_output(call, call->words[1], &count, sizeof count);
    }
    case drag_item_reference: {
        unsigned index = (uint16_t)call->words[1];
        if (!index || index > drag->count)
            return drag_result(call, drag_bad_item);
        carbon_drag_item* item = drag->items;
        for (; index > 1; --index)
            item = item->next;
        const uint32_t reference = CL(item->reference);
        return drag_output(call, call->words[2], &reference, sizeof reference);
    }
    default:
        break;
    }
    carbon_drag_item** item_link = find_drag_item(drag, call->words[1]);
    if (operation == drag_add_flavor)
        return add_drag_flavor(drag, item_link, call);
    carbon_drag_item* item = *item_link;
    if (!item)
        return drag_result(call, drag_bad_item);
    switch (operation) {
    case drag_get_bounds:
        return drag_output(call, call->words[2], &item->bounds, sizeof item->bounds);
    case drag_set_bounds: {
        const uint32_t address = call->words[2];
        const void* rect = address ? ROMlib_carbon_try_guest_memory(address, sizeof item->bounds, 0) : NULL;
        if (!rect)
            return drag_result(call, paramErr);
        memcpy(&item->bounds, rect, sizeof item->bounds);
        return drag_result(call, noErr);
    }
    case drag_count_flavors: {
        const uint16_t count = CW(item->count);
        return drag_output(call, call->words[2], &count, sizeof count);
    }
    case drag_flavor_type: {
        unsigned index = (uint16_t)call->words[2];
        if (!index || index > item->count)
            return drag_result(call, drag_bad_flavor);
        carbon_drag_flavor* flavor = item->flavors;
        for (; index > 1; --index)
            flavor = flavor->next;
        const uint32_t type = CL(flavor->type);
        return drag_output(call, call->words[3], &type, sizeof type);
    }
    default:
        break;
    }
    carbon_drag_flavor* flavor = *find_drag_flavor(item, call->words[2]);
    if (!flavor)
        return drag_result(call, drag_bad_flavor);
    if (operation == drag_set_flavor)
        return set_drag_flavor(flavor, call);
    if (operation == drag_flavor_flags) {
        const uint32_t flags = CL(flavor->flags);
        return drag_output(call, call->words[3], &flags, sizeof flags);
    }
    return drag_flavor_payload(call, drag, flavor, operation == drag_flavor_data);
}

#define CARBON_DRAG_DATA(name, op)                          \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call) \
    {                                                       \
        return carbon_drag_data(call, #name, drag_##op);    \
    }

CARBON_DRAG_DATA(NewDrag, create)
CARBON_DRAG_DATA(DisposeDrag, dispose)
CARBON_DRAG_DATA(AddDragItemFlavor, add_flavor)
CARBON_DRAG_DATA(SetDragItemFlavorData, set_flavor)
CARBON_DRAG_DATA(SetDragSendProc, set_send_proc)
CARBON_DRAG_DATA(CountDragItems, count_items)
CARBON_DRAG_DATA(GetDragItemReferenceNumber, item_reference)
CARBON_DRAG_DATA(CountDragItemFlavors, count_flavors)
CARBON_DRAG_DATA(GetFlavorType, flavor_type)
CARBON_DRAG_DATA(GetFlavorFlags, flavor_flags)
CARBON_DRAG_DATA(GetFlavorDataSize, flavor_size)
CARBON_DRAG_DATA(GetFlavorData, flavor_data)
CARBON_DRAG_DATA(GetDragItemBounds, get_bounds)
CARBON_DRAG_DATA(SetDragItemBounds, set_bounds)
CARBON_DRAG_DATA(GetDropLocation, get_drop_location)
CARBON_DRAG_DATA(SetDropLocation, set_drop_location)
CARBON_DRAG_DATA(TrackDrag, track)
CARBON_DRAG_DATA(GetDragRegion, get_region)
CARBON_DRAG_DATA(SetDragRegion, set_region)
CARBON_DRAG_DATA(GetDragAttributes, get_attributes)
CARBON_DRAG_DATA(GetDragMouse, get_mouse)
CARBON_DRAG_DATA(SetDragMouse, set_mouse)
CARBON_DRAG_DATA(GetDragOrigin, get_origin)
CARBON_DRAG_DATA(GetDragModifiers, get_modifiers)
CARBON_DRAG_DATA(SetDragInputProc, set_input_proc)
CARBON_DRAG_DATA(SetDragDrawingProc, set_drawing_proc)
CARBON_DRAG_DATA(ChangeDragBehaviors, change_behaviors)
CARBON_DRAG_DATA(ShowDragHilite, show_highlight)
CARBON_DRAG_DATA(HideDragHilite, hide_highlight)
CARBON_DRAG_DATA(UpdateDragHilite, update_highlight)
CARBON_DRAG_DATA(DragPreScroll, pre_scroll)
CARBON_DRAG_DATA(DragPostScroll, post_scroll)
CARBON_DRAG_DATA(SetDragImage, set_classic_image)
CARBON_DRAG_DATA(SetDragImageWithAlpha, set_alpha_image)

#undef CARBON_DRAG_DATA
#endif
