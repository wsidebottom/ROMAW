/*+===================================================================
  File:      drag_hilite.h

  Summary:   Maintains the QuickDraw highlight associated with a native drag.

  Classes:   None.

  Functions: Private Drag Manager highlight handlers.

  Origin:    CarbonLib 1.4 DragLib dispatch entries and Apple Drag Manager documentation.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMLIB_CARBON_DRAG_HILITE_H
#define ROMLIB_CARBON_DRAG_HILITE_H

static void drag_release_hilite(carbon_drag* drag)
{
    if (drag->hilite_region)
        DisposeRgn((RgnHandle)SYN68K_TO_US(drag->hilite_region));
    drag->hilite_region = drag->hilite_port = 0;
    drag->hilite_suspended = false;
}

static int drag_invert_hilite(uint32_t region, uint32_t port)
{
    _Alignas(4) uint32_t saved_port = 0;
    const ROMlib_carbon_ppc_frame get_port = {.words = {US_TO_SYN68K(&saved_port)}};
    const ROMlib_carbon_ppc_frame select = {.words = {port}};
    if (!drag_trap(0x0fd0, 0xc0, &get_port, NULL))
        return 0;
    if (port != CL(saved_port) && !drag_trap(0x0fcc, 0xc0, &select, NULL))
        return 0;

    const Byte saved_mode = HiliteMode;
    HiliteMode &= 0x7f;
    const ROMlib_carbon_ppc_frame invert_region = {.words = {region}};
    const int drawn = drag_trap(0x1154, 0xc0, &invert_region, NULL);
    HiliteMode = saved_mode;
    const ROMlib_carbon_ppc_frame restore = {.words = {CL(saved_port)}};
    return (port == CL(saved_port) || drag_trap(0x0fcc, 0xc0, &restore, NULL)) && drawn;
}

static int drag_clear_hilite(carbon_drag* drag)
{
    const int erased =
        !drag->hilite_region || drag->hilite_suspended || drag_invert_hilite(drag->hilite_region, drag->hilite_port);
    drag_release_hilite(drag);
    return erased;
}

static int drag_show_hilite(ROMlib_carbon_ppc_frame* call, carbon_drag* drag)
{
    const uint32_t source = call->words[1];
    if (!drag_region_record(source))
        return drag_result(call, paramErr);
    _Alignas(4) uint32_t port = 0;
    const ROMlib_carbon_ppc_frame get_port = {.words = {US_TO_SYN68K(&port)}};
    if (!drag_trap(0x0fd0, 0xc0, &get_port, NULL))
        return 0;
    if (!port)
        return drag_result(call, paramErr);

    RgnHandle outside = NewRgn(), inside = NewRgn();
    OSErr status = memFullErr;
    if (outside && inside) {
        const uint32_t outer = US_TO_SYN68K(outside), inner = US_TO_SYN68K(inside);
        status = drag_copy_region(source, outer);
        if (!status)
            status = drag_copy_region(source, inner);
        if (!status) {
            const bool expand = !(uint8_t)call->words[2];
            const int16_t outer_inset = expand ? -3 : 1, inner_inset = expand ? -1 : 3;
            InsetRgn(outside, outer_inset, outer_inset);
            InsetRgn(inside, inner_inset, inner_inset);
            DiffRgn(outside, inside, outside);
        }
    }
    if (inside)
        DisposeRgn(inside);
    if (status) {
        if (outside)
            DisposeRgn(outside);
        return drag_result(call, status);
    }
    const uint32_t region = US_TO_SYN68K(outside);
    if (!drag_clear_hilite(drag) || !drag_invert_hilite(region, CL(port))) {
        DisposeRgn(outside);
        return 0;
    }
    drag->hilite_region = region;
    drag->hilite_port = CL(port);
    return drag_result(call, noErr);
}

static int drag_update_hilite(ROMlib_carbon_ppc_frame* call, carbon_drag* drag)
{
    const uint32_t update = call->words[1];
    if (!drag_region_record(update))
        return drag_result(call, paramErr);
    if (!drag->hilite_region || drag->hilite_suspended)
        return drag_result(call, noErr);
    RgnHandle intersection = NewRgn();
    if (!intersection)
        return drag_result(call, memFullErr);
    SectRgn((RgnHandle)SYN68K_TO_US(update), (RgnHandle)SYN68K_TO_US(drag->hilite_region), intersection);
    const int drawn = drag_invert_hilite(US_TO_SYN68K(intersection), drag->hilite_port);
    DisposeRgn(intersection);
    return drawn && drag_result(call, noErr);
}

static int drag_scroll_hilite(ROMlib_carbon_ppc_frame* call, carbon_drag* drag, bool before)
{
    if (drag->hilite_region && drag->hilite_suspended != before &&
        (!before || (int16_t)call->words[1] || (int16_t)call->words[2])) {
        /* Erase before scrolling so pixels cannot carry the highlight along. */
        if (!drag_invert_hilite(drag->hilite_region, drag->hilite_port))
            return 0;
        drag->hilite_suspended = before;
    }
    return drag_result(call, noErr);
}

static int carbon_GetDragHiliteColor(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference saved;
    if (drag_saved_dispatch_available() && ROMlib_carbon_load_reference("DragLib", "GetDragHiliteColor", &saved))
        return ROMlib_carbon_invoke_frame(saved.function, call);

    WindowPtr window = call->words[0] ? (WindowPtr)SYN68K_TO_US(call->words[0]) : NULL;
    if (!window || !ROMlib_valid_window(window))
        return drag_result(call, paramErr);
    GrafPtr port = ROMlib_carbon_window_port(window);
    RGBColor color = HiliteRGB;
    if (CGrafPort_p(port))
        ROMlib_carbon_port_hilite_color(port, &color);
    return drag_output(call, call->words[1], &color, sizeof color);
}

#endif
