/*+===================================================================
  File:      drag_tracking.h

  Summary:   Pumps local Drag Manager tracking and dispatches guest handlers.

  Classes:   None.

  Functions: Private drag tracking helpers.

  Origin:    CarbonLib 1.4 DragLib disassembly and Universal Interfaces 3.4.2 Drag.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMLIB_CARBON_DRAG_TRACKING_H
#define ROMLIB_CARBON_DRAG_TRACKING_H

#include "OSUtil.h"
#include "rsys/layers.h"

enum {
    drag_enter_handler = 1,
    drag_enter_window = 2,
    drag_in_window = 3,
    drag_leave_window = 4,
    drag_leave_handler = 5,
    drag_left_sender = 1,
    drag_inside_application = 2,
    drag_inside_sender = 4,
    drag_not_accepted = -1857
};

static int drag_trap(uint32_t vector, uint32_t info, const ROMlib_carbon_ppc_frame* args, uint32_t* result)
{
    uint32_t ignored;
    return ROMlib_carbon_call_toolbox_vector(vector, info, 0, args, result ? result : &ignored);
}

static uint32_t drag_pack_point(Point point)
{
    return (uint32_t)(uint16_t)CW(point.v) << 16 | (uint16_t)CW(point.h);
}

static int carbon_WaitMouseMoved(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference saved;
    if (drag_saved_dispatch_available() && ROMlib_carbon_load_reference("DragLib", "WaitMouseMoved", &saved))
        return ROMlib_carbon_invoke_frame(saved.function, call);

    _Alignas(32) EventRecord event;
    const uint32_t initial = call->words[0];
    const ROMlib_carbon_ppc_frame args = {.words = {mDownMask | mUpMask, US_TO_SYN68K(&event)}};
    for (;;) {
        uint32_t queued;
        if (!drag_trap(0x13c4, 0x390, &args, &queued))
            return 0;
        if (queued || (CW(event.modifiers) & btnState))
            return drag_result(call, false);
        if (drag_pack_point(event.where) != initial)
            return drag_result(call, true);
        Delay(1, NULL);
    }
}

static int drag_window_at(Point point, uint32_t* window, bool* present)
{
    _Alignas(4) uint32_t found = 0;
    ROMlib_carbon_ppc_frame args = {.words = {drag_pack_point(point), US_TO_SYN68K(&found)}};
    /* DragLib's original dependency searches the initialized saved layer tree.
       Use its actual entry point; native-only hosts retain the live Toolbox trap. */
    ROMlib_carbon_reference saved;
    uint32_t part;
    if (ROMlib_carbon_probe_reference("WindowsLib", "GlobalFindWindow", &saved)) {
        if (!ROMlib_carbon_invoke_frame(saved.function, &args))
            return 0;
        part = args.words[0];
    }
    else if (!drag_trap(0x12b0, 0x3e0, &args, &part))
        return 0;
    *window = CL(found);
    if (!*window && (int16_t)part == inDesk) {
        const Point host_point = {CW(point.v), CW(point.h)};
        WindowPtr desktop = ROMlib_desktop_window_at(host_point);
        if (desktop)
            *window = US_TO_SYN68K(desktop);
    }
    /* FindWindow reports the desktop as inDesk with a null WindowPtr.
       Application-wide handlers must still receive its tracking and drop. */
    *present = *window || (int16_t)part == inDesk;
    return 1;
}

static int drag_notify(const carbon_drag* drag, uint32_t window, unsigned message, int* accepted)
{
    const bool tracking = message != 0;
    const uint64_t cutoff = last_drag_handler_order;
    uint64_t last = 0;
    *accepted = false;
    for (;;) {
        const carbon_drag_handler* handler = drag_handlers;
        while (handler && (handler->order <= last || handler->order > cutoff || handler->tracking != tracking ||
                           (handler->window && handler->window != window)))
            handler = handler->next;
        if (!handler)
            return 1;
        last = handler->order;
        const ROMlib_carbon_ppc_frame args = {
            .words = {tracking ? message : window, tracking ? window : handler->context,
                      tracking ? handler->context : drag->reference, drag->reference}};
        uint32_t status;
        if (!ROMlib_carbon_invoke_pascal(handler->procedure, tracking ? 0x3fa0 : 0xfe0, &args, &status))
            return 0;
        if (!(int16_t)status) {
            *accepted = true;
            if (!tracking)
                return 1;
        }
    }
}

static const Region* drag_region_record(uint32_t handle)
{
    uint32_t extent;
    const Region* region = cfp_handle_data(handle, &extent, 0);
    if (!region || extent < sizeof *region)
        return NULL;
    const unsigned size = CW(region->rgnSize) & 0x7fff;
    return size >= sizeof *region && size <= extent && !(size & 1) ? region : NULL;
}

static OSErr drag_copy_region(uint32_t source, uint32_t dest)
{
    const Region* input = drag_region_record(source);
    if (!input || !drag_region_record(dest))
        return paramErr;
    if (source == dest)
        return noErr;
    const uint32_t extent = CW(input->rgnSize) & 0x7fff;
    SetHandleSize((Handle)SYN68K_TO_US(dest), extent);
    if (MemErr)
        return CW(MemErr);
    uint32_t capacity;
    void* output = cfp_handle_data(dest, &capacity, 1);
    input = drag_region_record(source);
    if (!input || !output || capacity < extent)
        return paramErr;
    memcpy(output, input, extent);
    return noErr;
}

static int drag_offset_region(uint32_t region, int16_t horizontal, int16_t vertical)
{
    const ROMlib_carbon_ppc_frame args = {.words = {region, (uint16_t)horizontal, (uint16_t)vertical}};
    return drag_trap(0x1180, 0xac0, &args, NULL);
}

static int carbon_TEGetHiliteRgn(ROMlib_carbon_ppc_frame* call)
{
    uint32_t size;
    if (!drag_region_record(call->words[0]) || !cfp_handle_data(call->words[1], &size, 0) || size < sizeof(TERec))
        return drag_result(call, paramErr);

    uint32_t result;
    if (!ROMlib_carbon_call_toolbox_vector(0x0ef4, 0x0fae, 0xf, call, &result))
        return 0;
    return drag_result(call, (int16_t)result);
}

#include "drag_image.h"
#include "drag_feedback.h"
#include "drag_hilite.h"

static int drag_move_pinned(carbon_drag* drag, Point point)
{
    const int16_t horizontal = (int16_t)(CW(point.h) - CW(drag->pinned.h));
    const int16_t vertical = (int16_t)(CW(point.v) - CW(drag->pinned.v));
    if (drag->tracking && (horizontal || vertical) && !drag_offset_region(drag->tracking_region, horizontal, vertical))
        return 0;
    drag->pinned = point;
    return 1;
}

static int drag_region_access(ROMlib_carbon_ppc_frame* call, carbon_drag* drag, bool set)
{
    const uint32_t handle = call->words[1], offset = call->words[2];
    const Region* input = drag_region_record(handle);
    const Region* tracked = drag_region_record(drag->tracking_region);
    Point* point = offset ? ROMlib_carbon_try_guest_memory(offset, sizeof(Point), !set) : NULL;
    if (!drag->tracking || !input || !tracked || (offset && !point))
        return drag_result(call, paramErr);
    if (!set) {
        const Point delta = {CW((int16_t)(CW(tracked->rgnBBox.top) - CW(drag->pinned.v))),
                             CW((int16_t)(CW(tracked->rgnBBox.left) - CW(drag->pinned.h)))};
        const OSErr status = drag_copy_region(drag->tracking_region, handle);
        if (status)
            return drag_result(call, status);
        if (point)
            memcpy(point, &delta, sizeof delta);
    }
    else {
        const Rect bounds = input->rgnBBox;
        Point delta = {0};
        if (point)
            memcpy(&delta, point, sizeof delta);
        const OSErr status = drag_copy_region(handle, drag->tracking_region);
        if (status)
            return drag_result(call, status);
        if (point && !drag_offset_region(drag->tracking_region, CW(drag->pinned.h) + CW(delta.h) - CW(bounds.left),
                                         CW(drag->pinned.v) + CW(delta.v) - CW(bounds.top)))
            return 0;
    }
    return drag_result(call, noErr);
}

static int drag_update_mouse(carbon_drag* drag, const EventRecord* event, int* cancelled)
{
    drag->mouse = event->where;
    drag->modifiers = event->modifiers;
    if (!drag_move_pinned(drag, drag->mouse))
        return 0;
    if (!drag->input_proc)
        return 1;
    _Alignas(4) Point point = drag->pinned;
    _Alignas(4) INTEGER modifiers = drag->modifiers;
    const ROMlib_carbon_ppc_frame args = {
        .words = {US_TO_SYN68K(&point), US_TO_SYN68K(&modifiers), drag->input_context, drag->reference}};
    uint32_t status;
    if (!ROMlib_carbon_invoke_pascal(drag->input_proc, 0x3fe0, &args, &status))
        return 0;
    *cancelled = (int16_t)status != noErr;
    drag->modifiers = modifiers;
    return drag_move_pinned(drag, point);
}

static int track_drag(ROMlib_carbon_ppc_frame* call, carbon_drag* drag)
{
    const uint32_t event_address = call->words[1], initial_region = call->words[2];
    const EventRecord* initial =
        event_address ? ROMlib_carbon_try_guest_memory(event_address, sizeof *initial, 0) : NULL;
    if (!initial || CW(initial->what) != mouseDown || tracking_drag ||
        (initial_region && !drag_region_record(initial_region)))
        return drag_result(call, paramErr);

    _Alignas(16) EventRecord event = *initial;
    const ROMlib_carbon_ppc_frame allocate = {0};
    uint32_t region;
    if (!drag_trap(0x1160, 0x30, &allocate, &region))
        return 0;
    if (!region)
        return drag_result(call, memFullErr);
    const OSErr copy_error = initial_region ? drag_copy_region(initial_region, region) : noErr;
    if (copy_error) {
        DisposHandle((Handle)SYN68K_TO_US(region));
        return drag_result(call, copy_error);
    }

    tracking_drag = drag->tracking = true;
    drag->tracking_region = region;
    drag->origin = event.where;
    drag->pinned = event.where;
    drag->down_modifiers = event.modifiers;
    drag->up_modifiers = 0;
    drag->attributes = 0;
    uint32_t sender = 0, window = 0;
    int cancelled = false, ignored, accepted = false;
    bool entered_handler = false, sender_present = false, window_present = false;
    drag_feedback feedback = {0};
    int ok = drag_window_at(drag->origin, &sender, &sender_present) && drag_update_mouse(drag, &event, &cancelled);
    /* Enter-handler callbacks already query whether this is their own drag.
       Publish the origin before dispatching any tracking message. */
    if (ok && sender_present)
        drag->attributes = drag_inside_application | drag_inside_sender;
    if (ok && !cancelled)
        ok = drag_feedback_begin(drag, &feedback);
    if (ok && !cancelled && !feedback.error) {
        ok = drag_notify(drag, sender, drag_enter_handler, &ignored);
        entered_handler = ok;
    }
    const ROMlib_carbon_ppc_frame wait = {.words = {mUpMask, US_TO_SYN68K(&event), 1, 0}};
    while (ok && !cancelled && !feedback.error) {
        uint32_t target;
        bool target_present;
        ok = drag_window_at(drag->pinned, &target, &target_present);
        if (!ok)
            break;
        drag->attributes &= drag_left_sender;
        if (target_present)
            drag->attributes |= drag_inside_application;
        if (target_present && sender_present && target == sender)
            drag->attributes |= drag_inside_sender;
        else
            drag->attributes |= drag_left_sender;
        if (target != window || target_present != window_present) {
            if (window_present)
                ok = drag_notify(drag, window, drag_leave_window, &ignored);
            window = target;
            window_present = target_present;
            if (ok && window_present)
                ok = drag_notify(drag, window, drag_enter_window, &ignored);
        }
        if (ok && window_present)
            ok = drag_notify(drag, window, drag_in_window, &ignored);
        if (ok)
            ok = drag_feedback_update(drag, &feedback);
        if (!ok || feedback.error || CW(event.what) == mouseUp)
            break;

        ok = drag_trap(0x0f80, 0x3f90, &wait, NULL);
        drag_image_hide(&feedback.overlay);
        if (ok)
            ok = drag_update_mouse(drag, &event, &cancelled);
        if (ok && (CW(event.modifiers) & btnState) && CW(event.what) != mouseUp)
            cancelled = true;
    }
    drag_image_hide(&feedback.overlay);
    if (ok && !cancelled && !feedback.error && CW(event.what) == mouseUp) {
        drag->up_modifiers = drag->modifiers;
        if (window_present)
            ok = drag_notify(drag, window, 0, &accepted);
    }
    if (ok && window_present)
        ok = drag_notify(drag, window, drag_leave_window, &ignored);
    if (ok && entered_handler)
        ok = drag_notify(drag, window ? window : sender, drag_leave_handler, &ignored);
    const int erased = drag_clear_hilite(drag);
    const int finished = drag_feedback_finish(drag, &feedback) && erased;
    if (ok && finished && !feedback.error && !accepted && feedback.was_visible && (drag->behaviors & 1)) {
        const int16_t horizontal = (int16_t)(CW(drag->origin.h) - CW(drag->pinned.h));
        const int16_t vertical = (int16_t)(CW(drag->origin.v) - CW(drag->pinned.v));
        if (horizontal || vertical)
            ok = drag_zoom_region(region, horizontal, vertical, 6, 2) == noErr;
    }
    drag->tracking_region = 0;
    tracking_drag = drag->tracking = false;
    DisposHandle((Handle)SYN68K_TO_US(region));
    return ok && finished && drag_result(call, feedback.error ? feedback.error : accepted ? noErr : drag_not_accepted);
}

static int drag_tracking_operation(ROMlib_carbon_ppc_frame* call, carbon_drag* drag, enum drag_data_operation operation)
{
    const uint32_t output = call->words[1];
    switch (operation) {
    case drag_track:
        return track_drag(call, drag);
    case drag_get_region:
    case drag_set_region:
        return drag_region_access(call, drag, operation == drag_set_region);
    case drag_get_attributes: {
        const uint32_t attributes = CL(drag->attributes);
        return drag_output(call, output, &attributes, sizeof attributes);
    }
    case drag_get_mouse: {
        const uint32_t pinned = call->words[2];
        if ((output && !ROMlib_carbon_try_guest_memory(output, sizeof(Point), 1)) ||
            (pinned && !ROMlib_carbon_try_guest_memory(pinned, sizeof(Point), 1)))
            return drag_result(call, paramErr);
        if (output)
            drag_output(call, output, &drag->mouse, sizeof(Point));
        return pinned ? drag_output(call, pinned, &drag->pinned, sizeof(Point)) : drag_result(call, noErr);
    }
    case drag_set_mouse: {
        const Point point = {CW((int16_t)(output >> 16)), CW((int16_t)output)};
        return drag_move_pinned(drag, point) && drag_result(call, noErr);
    }
    case drag_get_origin:
        return drag_output(call, output, &drag->origin, sizeof(Point));
    case drag_get_modifiers: {
        const INTEGER values[] = {drag->modifiers, drag->down_modifiers, drag->up_modifiers};
        void* locations[NELEM(values)] = {0};
        for (unsigned i = 0; i < NELEM(values); ++i) {
            if (call->words[i + 1] &&
                !(locations[i] = ROMlib_carbon_try_guest_memory(call->words[i + 1], sizeof(INTEGER), 1)))
                return drag_result(call, paramErr);
        }
        for (unsigned i = 0; i < NELEM(values); ++i)
            if (locations[i])
                memcpy(locations[i], &values[i], sizeof(INTEGER));
        return drag_result(call, noErr);
    }
    case drag_set_input_proc:
        drag->input_proc = output;
        drag->input_context = call->words[2];
        return drag_result(call, noErr);
    case drag_set_drawing_proc:
        return drag_set_drawing_callback(call, drag);
    case drag_show_highlight:
        return drag_show_hilite(call, drag);
    case drag_hide_highlight:
        return drag_clear_hilite(drag) && drag_result(call, noErr);
    case drag_update_highlight:
        return drag_update_hilite(call, drag);
    case drag_pre_scroll:
    case drag_post_scroll:
        return drag_scroll_hilite(call, drag, operation == drag_pre_scroll);
    case drag_set_classic_image:
    case drag_set_alpha_image:
        return drag_set_image(call, drag, operation == drag_set_alpha_image);
    case drag_change_behaviors:
        drag->behaviors = (drag->behaviors | output) & ~call->words[2];
        return drag_result(call, noErr);
    default:
        return drag_result(call, paramErr);
    }
}

#endif
