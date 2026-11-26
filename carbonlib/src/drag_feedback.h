/*+===================================================================
  File:      drag_feedback.h

  Summary:   Draws and erases local Drag Manager feedback and zoom animations.

  Classes:   None.

  Functions: Private Drag Manager feedback and zoom handlers.

  Origin:    CarbonLib 1.4 DragLib drawing worker and Universal Interfaces 3.4.2 Drag.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMLIB_CARBON_DRAG_FEEDBACK_H
#define ROMLIB_CARBON_DRAG_FEEDBACK_H

enum { drag_region_begin = 1, drag_region_draw, drag_region_hide, drag_region_idle, drag_region_end };

typedef struct drag_feedback {
    uint32_t region, next_region, procedure, context;
    Point origin;
    OSErr error;
    drag_image_frame overlay;
    bool begun, visible, was_visible, running_callback;
} drag_feedback;

static int drag_draw_outline(uint32_t region)
{
    if (!region)
        return 1;
    uint32_t screen = cfp_read(0xd2c);
    if (!screen)
        screen = cfp_read(0x9de);
    if (!screen)
        return 0;

    RgnHandle saved_clip = C_NewRgn();
    if (!saved_clip)
        return 0;
    _Alignas(4) uint32_t saved_port = 0;
    _Alignas(32) PenState saved_pen;
    const ROMlib_carbon_ppc_frame get_port = {.words = {US_TO_SYN68K(&saved_port)}};
    const ROMlib_carbon_ppc_frame set_screen = {.words = {screen}};
    if (!drag_trap(0x0fd0, 0xc0, &get_port, NULL) || !drag_trap(0x0fcc, 0xc0, &set_screen, NULL)) {
        C_DisposeRgn(saved_clip);
        return 0;
    }
    C_GetClip(saved_clip);
    C_SetClip(PORT_VIS_REGION((GrafPtr)SYN68K_TO_US(screen)));
    const ROMlib_carbon_ppc_frame pen = {.words = {US_TO_SYN68K(&saved_pen)}};
    const ROMlib_carbon_ppc_frame normal = {0}, xor_mode = {.words = {patXor}}, outline = {.words = {region}};
    const int saved = drag_trap(0x1060, 0xc0, &pen, NULL);
    const int drawn = saved && drag_trap(0x1078, 0, &normal, NULL) && drag_trap(0x1070, 0x80, &xor_mode, NULL) &&
                      drag_trap(0x1148, 0xc0, &outline, NULL);
    const int restored_pen = !saved || drag_trap(0x1064, 0xc0, &pen, NULL);
    C_SetClip(saved_clip);
    C_DisposeRgn(saved_clip);
    const ROMlib_carbon_ppc_frame restore_port = {.words = {CL(saved_port)}};
    return drag_trap(0x0fcc, 0xc0, &restore_port, NULL) && drawn && restored_pen;
}

static int drag_drawing_callback(const carbon_drag* drag, drag_feedback* feedback, unsigned message, uint32_t show,
                                 uint32_t hide, uint32_t* status)
{
    const ROMlib_carbon_ppc_frame args = {.words = {message, show, show ? drag_pack_point(drag->pinned) : 0, hide,
                                                    hide ? drag_pack_point(feedback->origin) : 0, feedback->context,
                                                    drag->reference}};
    feedback->running_callback = true;
    const int invoked = ROMlib_carbon_invoke_pascal(feedback->procedure, 0xfffa0, &args, status);
    feedback->running_callback = false;
    return invoked;
}

static int drag_feedback_start(const carbon_drag* drag, drag_feedback* feedback)
{
    feedback->procedure = drag->drawing_proc;
    feedback->context = drag->drawing_context;
    if (feedback->procedure) {
        uint32_t status;
        if (!drag_drawing_callback(drag, feedback, drag_region_begin, drag->tracking_region, 0, &status))
            return 0;
        if ((int16_t)status)
            feedback->procedure = 0;
    }
    feedback->begun = true;
    return 1;
}

static int drag_feedback_begin(carbon_drag* drag, drag_feedback* feedback)
{
    const ROMlib_carbon_ppc_frame args = {0};
    if (!drag_trap(0x1160, 0x30, &args, &feedback->region) || !drag_trap(0x1160, 0x30, &args, &feedback->next_region))
        return 0;
    if (!feedback->region || !feedback->next_region) {
        feedback->error = memFullErr;
        return 1;
    }
    drag->feedback = feedback;
    return drag_feedback_start(drag, feedback);
}

static int drag_feedback_update(const carbon_drag* drag, drag_feedback* feedback)
{
    if (!feedback->begun && !drag_feedback_start(drag, feedback))
        return 0;
    if (!feedback->procedure && drag->image && !(drag->image_flags & drag_image_and_outline)) {
        if (feedback->visible && !drag_draw_outline(feedback->region))
            return 0;
        feedback->visible = false;
        if (drag_image_show(drag, &feedback->overlay)) {
            feedback->was_visible |= feedback->overlay.visible;
            return 1;
        }
    }
    const uint32_t show = drag->tracking_region, hide = feedback->visible ? feedback->region : 0;
    const ROMlib_carbon_ppc_frame check_empty = {.words = {show}};
    uint32_t empty, same = 0;
    if (!drag_trap(0x1188, 0xd0, &check_empty, &empty))
        return 0;
    if (hide && !empty) {
        const ROMlib_carbon_ppc_frame equal = {.words = {show, hide}};
        if (!drag_trap(0x118c, 0x3d0, &equal, &same))
            return 0;
    }
    if (!empty && !same) {
        feedback->error = drag_copy_region(show, feedback->next_region);
        if (feedback->error)
            return 1;
    }
    const Point origin = drag->pinned;
    if (feedback->procedure) {
        uint32_t status;
        if (!drag_drawing_callback(drag, feedback, same || (!hide && empty) ? drag_region_idle : drag_region_draw,
                                   empty || same ? 0 : feedback->next_region, same ? 0 : hide, &status))
            return 0;
        feedback->error = (int16_t)status;
        if (feedback->error)
            return 1;
    }
    else if (!same && (!drag_draw_outline(hide) || (!empty && !drag_draw_outline(feedback->next_region))))
        return 0;
    if (!empty && !same) {
        const uint32_t old = feedback->region;
        feedback->region = feedback->next_region;
        feedback->next_region = old;
    }
    feedback->visible = !empty;
    if (!feedback->procedure && (drag->image_flags & drag_image_and_outline))
        drag_image_show(drag, &feedback->overlay);
    feedback->was_visible |= feedback->visible || feedback->overlay.visible;
    feedback->origin = origin;
    return 1;
}

static int drag_feedback_stop(const carbon_drag* drag, drag_feedback* feedback)
{
    int ok = 1;
    drag_image_hide(&feedback->overlay);
    const uint32_t hide = feedback->visible ? feedback->region : 0;
    if (feedback->begun && feedback->procedure) {
        uint32_t status;
        ok = !hide || drag_drawing_callback(drag, feedback, drag_region_hide, 0, hide, &status);
        ok = drag_drawing_callback(drag, feedback, drag_region_end, 0, 0, &status) && ok;
    }
    else if (hide)
        ok = drag_draw_outline(hide);
    feedback->begun = feedback->visible = false;
    return ok;
}

static int drag_feedback_finish(carbon_drag* drag, drag_feedback* feedback)
{
    const int ok = drag_feedback_stop(drag, feedback);
    drag->feedback = NULL;
    drag_image_finish(&feedback->overlay);
    if (feedback->region)
        DisposHandle((Handle)SYN68K_TO_US(feedback->region));
    if (feedback->next_region)
        DisposHandle((Handle)SYN68K_TO_US(feedback->next_region));
    return ok;
}

static int drag_set_drawing_callback(ROMlib_carbon_ppc_frame* call, carbon_drag* drag)
{
    drag_feedback* feedback = drag->feedback;
    const uint32_t procedure = call->words[1], context = call->words[2];
    if (drag->drawing_proc == procedure && drag->drawing_context == context)
        return drag_result(call, noErr);
    if (feedback && feedback->running_callback)
        return drag_result(call, paramErr);
    if (feedback && !drag_feedback_stop(drag, feedback))
        return 0;
    drag->drawing_proc = procedure;
    drag->drawing_context = context;
    return drag_result(call, noErr);
}

static double drag_zoom_fraction(unsigned step, unsigned steps, unsigned acceleration)
{
    const double fraction = (double)step / steps;
    switch (acceleration) {
    case 1:
        return fraction * fraction;
    case 2:
        return fraction * (2 - fraction);
    default:
        return fraction;
    }
}

static int drag_zoom_frame(uint32_t region)
{
    if (!drag_draw_outline(region))
        return 0;
    _Alignas(16) EventRecord event;
    const ROMlib_carbon_ppc_frame wait = {.words = {0, US_TO_SYN68K(&event), 1, 0}};
    const int paused = drag_trap(0x0f80, 0x3f90, &wait, NULL);
    return drag_draw_outline(region) && paused;
}

static OSErr drag_zoom_region(uint32_t source, int16_t horizontal, int16_t vertical, int steps, unsigned acceleration)
{
    if (!drag_region_record(source) || steps <= 0 || acceleration > 2)
        return paramErr;
    const ROMlib_carbon_ppc_frame args = {0};
    uint32_t copy;
    if (!drag_trap(0x1160, 0x30, &args, &copy) || !copy)
        return memFullErr;
    OSErr status = drag_copy_region(source, copy);
    int16_t last_h = 0, last_v = 0;
    for (int i = 1; !status && i <= steps; ++i) {
        const double progress = drag_zoom_fraction(i, steps, acceleration);
        const int16_t h = (int16_t)(horizontal * progress), v = (int16_t)(vertical * progress);
        if (!drag_offset_region(copy, h - last_h, v - last_v) || !drag_zoom_frame(copy))
            status = paramErr;
        last_h = h;
        last_v = v;
    }
    DisposHandle((Handle)SYN68K_TO_US(copy));
    return status;
}

static int16_t drag_zoom_coordinate(int16_t start, int16_t end, double progress)
{
    const int difference = CW(end) - CW(start);
    return CW((int16_t)(CW(start) + (int)(difference * progress)));
}

static OSErr drag_zoom_rects(const Rect* start, const Rect* end, int steps, unsigned acceleration)
{
    if (!start || !end || steps <= 0 || acceleration > 2)
        return paramErr;
    const Rect first = *start, last = *end;
    _Alignas(8) Rect outline;
    const ROMlib_carbon_ppc_frame new_region = {0};
    uint32_t region;
    if (!drag_trap(0x1160, 0x30, &new_region, &region) || !region)
        return memFullErr;
    const ROMlib_carbon_ppc_frame rect_region = {.words = {region, US_TO_SYN68K(&outline)}};
    OSErr status = noErr;
    for (int i = 1; !status && i <= steps; ++i) {
        const double progress = drag_zoom_fraction(i, steps, acceleration);
        outline = (Rect){drag_zoom_coordinate(first.top, last.top, progress),
                         drag_zoom_coordinate(first.left, last.left, progress),
                         drag_zoom_coordinate(first.bottom, last.bottom, progress),
                         drag_zoom_coordinate(first.right, last.right, progress)};
        if (!drag_trap(0x117c, 0x3c0, &rect_region, NULL) || !drag_zoom_frame(region))
            status = paramErr;
    }
    DisposHandle((Handle)SYN68K_TO_US(region));
    return status;
}

static int carbon_ZoomRegion(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference saved;
    if (drag_saved_dispatch_available() && ROMlib_carbon_load_reference("DragLib", "ZoomRegion", &saved))
        return ROMlib_carbon_invoke_frame(saved.function, call);
    const uint32_t distance = call->words[1];
    return drag_result(call, drag_zoom_region(call->words[0], (int16_t)distance, (int16_t)(distance >> 16),
                                              (int16_t)call->words[2], (uint16_t)call->words[3]));
}

static int carbon_ZoomRects(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference saved;
    if (drag_saved_dispatch_available() && ROMlib_carbon_load_reference("DragLib", "ZoomRects", &saved))
        return ROMlib_carbon_invoke_frame(saved.function, call);
    const Rect* from = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], sizeof(Rect), 0) : NULL;
    const Rect* to = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], sizeof(Rect), 0) : NULL;
    return drag_result(call, drag_zoom_rects(from, to, (int16_t)call->words[2], (uint16_t)call->words[3]));
}

#endif
