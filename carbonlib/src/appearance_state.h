/*+===================================================================
  File:      appearance_state.h

  Summary:   Implements Appearance Manager client registration, preferences, and drawing services.

  Classes:   None.

  Functions: Appearance Manager export entries and shared drawing-state and panel services.

  Origin:    Mac OS 9 AppearanceLib client table, process-reference workers and preference collection.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_APPEARANCE_STATE_H
#define ROMAW_CARBON_APPEARANCE_STATE_H

#include "MemoryMgr.h"
#include "ResourceMgr.h"
#include "cf_batch.h"
#include "rsys/cquick.h"
#include "rsys/pattern_origin.h"
#include "rsys/stubify.h"
#include "rsys/wind.h"
#include <stdlib.h>
#include <string.h>

enum appearance_client_operation { appearance_client_register, appearance_client_unregister, appearance_client_query };

enum { appearance_unimplemented = -4 };

enum {
    appearance_vector = 0x17d0,
    appearance_slot = 0x274,
    appearance_primary_group = 0x0c,
    appearance_secondary_group = 0x0d,
    appearance_separator = 0x0e,
};

static int appearance_client_result(ROMlib_carbon_ppc_frame* call, int32_t result)
{
    call->words[0] = result;
    return 1;
}

/* Appearance drawing states are opaque guest tokens with per-launch ownership.
   They save color/pen state, not a port, clip region or font selection. */
struct ROMlib_carbon_theme_state {
    ROMlib_carbon_theme_state* next;
    uint32 token;
    PenState pen;
    RGBColor foreground, background;
    LONGINT foreground_index, background_index;
    Pattern background_pattern;
    PixPatHandle background_pixpat;
    Point origin;
    INTEGER text_mode;
    boolean_t color, background_valid;
};

static void appearance_free_theme_state(ROMlib_carbon_theme_state* state)
{
    DisposPtr((Ptr)SYN68K_TO_US(state->token));
    free(state);
}

void ROMlib_carbon_free_theme_states(ROMlib_carbon_theme_state** states)
{
    while (*states) {
        ROMlib_carbon_theme_state* state = *states;
        *states = state->next;
        appearance_free_theme_state(state);
    }
}

static int32_t appearance_get_theme_drawing_state(uint32 output_address)
{
    ROMlib_carbon_theme_state** states = ROMlib_carbon_theme_state_slot();
    void* output = output_address ? ROMlib_carbon_try_guest_memory(output_address, sizeof(uint32_t), 1) : NULL;
    if (!states || !output)
        return paramErr;
    memset(output, 0, sizeof(uint32_t));
    if (!thePort || !ROMlib_valid_qd_port(thePort))
        return paramErr;
    unsigned count = 0;
    for (ROMlib_carbon_theme_state* saved = *states; saved; saved = saved->next)
        ++count;
    if (count >= 512)
        return memFullErr;
    ROMlib_carbon_theme_state* state = calloc(1, sizeof *state);
    if (!state)
        return memFullErr;
    Ptr token = NewPtrClear(sizeof(uint32_t));
    if (!token) {
        free(state);
        return memFullErr;
    }
    state->token = US_TO_SYN68K(token);
    GetPenState(&state->pen);
    state->color = CGrafPort_p(thePort);
    state->foreground_index = PORT_FG_COLOR(thePort);
    state->background_index = PORT_BK_COLOR(thePort);
    state->text_mode = PORT_TX_MODE(thePort);
    state->origin = ROMlib_pattern_origin();
    if (state->color) {
        GetForeColor(&state->foreground);
        GetBackColor(&state->background);
        PixPatHandle background = CPORT_BK_PIXPAT(theCPort);
        if (background) {
            state->background_valid = TRUE;
            PATASSIGN(state->background_pattern, PIXPAT_1DATA(background));
            if (PIXPAT_TYPE_X(background) != CWC(pixpat_type_orig))
                state->background_pixpat = background;
        }
    }
    else {
        state->background_valid = TRUE;
        PATASSIGN(state->background_pattern, PORT_BK_PAT(thePort));
    }
    state->next = *states;
    *states = state;
    const uint32_t encoded = CL(state->token);
    memcpy(output, &encoded, sizeof encoded);
    return noErr;
}

static int32_t appearance_dispose_theme_drawing_state(uint32 token)
{
    ROMlib_carbon_theme_state** link = ROMlib_carbon_theme_state_slot();
    if (!link)
        return paramErr;
    while (*link && (*link)->token != token)
        link = &(*link)->next;
    if (!*link)
        return paramErr;
    ROMlib_carbon_theme_state* state = *link;
    *link = state->next;
    appearance_free_theme_state(state);
    return noErr;
}

static int32_t appearance_set_theme_drawing_state(uint32 token, BOOLEAN dispose)
{
    ROMlib_carbon_theme_state** states = ROMlib_carbon_theme_state_slot();
    ROMlib_carbon_theme_state* state = states ? *states : NULL;
    while (state && state->token != token)
        state = state->next;
    if (!state || !thePort || !ROMlib_valid_qd_port(thePort))
        return paramErr;
    THz zone = GetZone();
    if (thePort == MR(wmgr_port))
        SetZone(MR(SysZone));
    SetPenState(&state->pen);
    if (state->color && CGrafPort_p(thePort)) {
        RGBForeColor(&state->foreground);
        RGBBackColor(&state->background);
        if (state->background_valid) {
            if (state->background_pixpat)
                BackPixPat(state->background_pixpat);
            else
                BackPat(state->background_pattern);
        }
    }
    else {
        ForeColor(state->foreground_index);
        BackColor(state->background_index);
        if (state->background_valid)
            BackPat(state->background_pattern);
    }
    TextMode(state->text_mode);
    ROMlib_QDSetPatternOrigin(state->origin);
    SetZone(zone);
    return dispose ? appearance_dispose_theme_drawing_state(token) : noErr;
}

static int32_t appearance_normalize_theme_drawing_state(void)
{
    if (!thePort || !ROMlib_valid_qd_port(thePort))
        return paramErr;
    THz zone = GetZone();
    if (thePort == MR(wmgr_port))
        SetZone(MR(SysZone));
    ForeColor(blackColor);
    BackColor(whiteColor);
    PenNormal();
    BackPat(white);
    TextMode(srcOr);
    ROMlib_QDSetPatternOrigin((Point){0, 0});
    SetZone(zone);
    return noErr;
}

static int carbon_GetThemeDrawingState(ROMlib_carbon_ppc_frame* call)
{
    return appearance_client_result(call, appearance_get_theme_drawing_state(call->words[0]));
}

static int carbon_SetThemeDrawingState(ROMlib_carbon_ppc_frame* call)
{
    return appearance_client_result(call, appearance_set_theme_drawing_state(call->words[0], (uint8_t)call->words[1]));
}

static int carbon_DisposeThemeDrawingState(ROMlib_carbon_ppc_frame* call)
{
    return appearance_client_result(call, appearance_dispose_theme_drawing_state(call->words[0]));
}

static int carbon_NormalizeThemeDrawingState(ROMlib_carbon_ppc_frame* call)
{
    return appearance_client_result(call, appearance_normalize_theme_drawing_state());
}

static int carbon_GetThemeMetric(ROMlib_carbon_ppc_frame* call)
{
    /* CarbonLib 1.4 uses these Platinum measurements in pixels. A negative
       entry identifies a metric that this version rejects. */
    static const int8_t metrics[] = {16, 11, 12, 12, 2, 2, 2, 3, 2, 1, 21, 12, 3, 5, 0, 16, 12, -1, -1, 20};
    const uint32_t metric = call->words[0];
    if (!call->words[1] || !ROMlib_carbon_try_guest_memory(call->words[1], sizeof(uint32_t), 1))
        return appearance_client_result(call, paramErr);
    const int value = metric < NELEM(metrics) ? metrics[metric] : -1;
    return cfp_write(call->words[1], value < 0 ? 0 : value) &&
           appearance_client_result(call, value < 0 ? paramErr : noErr);
}

static int appearance_dispatch_patch(bool* patched)
{
    uint32_t vector;
    if (!cfp_get(appearance_vector, &vector))
        return 0;
    *patched = vector && vector != (uint32_t)(uintptr_t)toolstuff[appearance_slot].orig;
    return 1;
}

static int appearance_dispatch(unsigned proc_info, unsigned selector, ROMlib_carbon_ppc_frame* call,
                               const ROMlib_carbon_ppc_frame* arguments)
{
    uint32_t result;
    return ROMlib_carbon_call_toolbox_vector(appearance_vector, proc_info, selector, arguments, &result) &&
           appearance_client_result(call, result);
}

/* Appearance's classic Window Manager selectors use the same WDEF metadata
   and collapse operations as the corresponding WindowsLib entries. */
#define APPEARANCE_WINDOW(name, proc_info, selector, native)                   \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call)                   \
    {                                                                        \
        bool patched;                                                        \
        if (!appearance_dispatch_patch(&patched))                            \
            return 0;                                                        \
        return patched ? appearance_dispatch(proc_info, selector, call, call) \
                       : carbon_window_##native(call);                       \
    }

APPEARANCE_WINDOW(IsWindowCollapsable, 0x398, 0x0f, WMIsWindowCollapsable)
APPEARANCE_WINDOW(IsWindowCollapsed, 0x398, 0x10, WMIsWindowCollapsed)
APPEARANCE_WINDOW(CollapseWindow, 0x7b8, 0x11, WMCollapseWindow)
APPEARANCE_WINDOW(CollapseAllWindows, 0x1b8, 0x12, WMCollapseAllWindows)
APPEARANCE_WINDOW(GetWindowFeatures, 0xfb8, 0x13, WMGetWindowFeatures)

#undef APPEARANCE_WINDOW

int32_t ROMlib_carbon_draw_theme_panel(const void* bounds, uint32_t state, enum ROMlib_carbon_theme_panel panel)
{
    if (!bounds || state > 2 || !thePort || !ROMlib_valid_qd_port(thePort))
        return paramErr;
    Rect rect = *(const Rect*)bounds;
    if (EmptyRect(&rect))
        return noErr;

    draw_state_t saved;
    draw_state_save(&saved);
    PenNormal();
    RGBColor ink = ROMlib_theme_background_rgb;
    RGBForeColor(&ink);
    PaintRect(&rect);
    if (panel == appearance_window_header) {
        ink = ROMlib_white_rgb_color;
        RGBForeColor(&ink);
        MoveTo(CW(rect.left), CW(rect.top));
        LineTo(CW(rect.right) - 1, CW(rect.top));
        ink = state ? ROMlib_black_rgb_color : ROMlib_gray_rgb_color;
        RGBForeColor(&ink);
        MoveTo(CW(rect.left), CW(rect.bottom) - 1);
        LineTo(CW(rect.right) - 1, CW(rect.bottom) - 1);
    }
    else {
        static const RGBColor shadow = {CWC(0x8888), CWC(0x8888), CWC(0x8888)};
        ink = state ? ROMlib_black_rgb_color : shadow;
        RGBForeColor(&ink);
        FrameRect(&rect);
        Rect bevel = rect;
        InsetRect(&bevel, 1, 1);
        if (RECT_WIDTH(&bevel) > 1 && RECT_HEIGHT(&bevel) > 1) {
            ink = ROMlib_white_rgb_color;
            RGBForeColor(&ink);
            MoveTo(CW(bevel.left), CW(bevel.bottom) - 1);
            LineTo(CW(bevel.left), CW(bevel.top));
            LineTo(CW(bevel.right) - 1, CW(bevel.top));
            ink = shadow;
            RGBForeColor(&ink);
            MoveTo(CW(bevel.right) - 1, CW(bevel.top) + 1);
            LineTo(CW(bevel.right) - 1, CW(bevel.bottom) - 1);
            LineTo(CW(bevel.left) + 1, CW(bevel.bottom) - 1);
        }
    }
    draw_state_restore(&saved);
    return noErr;
}

static int appearance_draw_panel(ROMlib_carbon_ppc_frame* call, enum ROMlib_carbon_theme_panel panel)
{
    if (call->words[1] == 7 || call->words[1] == 8)
        call->words[1] = 0;
    bool patched;
    if (!appearance_dispatch_patch(&patched))
        return 0;
    if (patched)
        return appearance_dispatch(0xfb8, panel, call, call);
    const Rect* rect = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], sizeof *rect, 0) : NULL;
    return appearance_client_result(call, ROMlib_carbon_draw_theme_panel(rect, call->words[1], panel));
}

static int carbon_DrawThemeWindowHeader(ROMlib_carbon_ppc_frame* call)
{
    return appearance_draw_panel(call, appearance_window_header);
}

static int carbon_DrawThemeWindowListViewHeader(ROMlib_carbon_ppc_frame* call)
{
    return appearance_draw_panel(call, appearance_list_view_header);
}

static int carbon_DrawThemePlacard(ROMlib_carbon_ppc_frame* call)
{
    return appearance_draw_panel(call, appearance_placard);
}

int32_t ROMlib_carbon_set_theme_window_background(void* port, int16_t brush, uint8_t update)
{
    WindowPtr window = port;
    if (!window || !ROMlib_valid_window(window))
        return paramErr;
    RGBColor color;
    switch (brush) {
    case -1:
        color = ROMlib_black_rgb_color;
        break;
    case -2:
    case 10:
        color = ROMlib_white_rgb_color;
        break;
    case 1:
    case 2:
        color = ROMlib_theme_background_rgb;
        break;
    default:
        return appearance_unimplemented;
    }
    GrafPtr previous = thePort;
    SetPort(window);
    AuxWinHandle aux = HIDDEN_NATIVE(*lookup_aux_win(window));
    if (aux) {
        uint32_t flags = Hx(aux, awFlags) & ~ROMlib_theme_window_background;
        if (brush == 1 || brush == 2)
            flags |= ROMlib_theme_window_background;
        HxX(aux, awFlags) = CL(flags);
    }
    RGBBackColor(&color);
    if (update)
        InvalRect(&PORT_RECT(window));
    SetPort(previous);
    return noErr;
}

static int carbon_SetThemeWindowBackground(ROMlib_carbon_ppc_frame* call)
{
    WindowPtr window = ROMlib_carbon_try_guest_memory(call->words[0], sizeof(WindowRecord), 0);
    if (!window || !ROMlib_valid_window(window))
        return appearance_client_result(call, paramErr);
    int16_t brush = call->words[1];
    if (brush >= 49 && brush <= 51)
        brush = 1;
    call->words[1] = (uint32_t)(int32_t)brush;
    bool patched;
    if (!appearance_dispatch_patch(&patched))
        return 0;
    if (patched)
        return appearance_dispatch(0x1bb8, 4, call, call);
    return appearance_client_result(call, ROMlib_carbon_set_theme_window_background(window, brush, (uint8_t)call->words[2]));
}

static int appearance_classic_service(ROMlib_carbon_ppc_frame* call, unsigned proc_info, unsigned selector,
                                      const char* name)
{
    bool patched;
    if (!appearance_dispatch_patch(&patched))
        return 0;
    if (patched)
        return appearance_dispatch(proc_info, selector, call, call);
    return ROMlib_carbon_call_romaw_service(name, call);
}

static uint32_t appearance_classic_draw_state(uint32_t state)
{
    return state == 7 || state == 8 ? 0 : state;
}

static bool appearance_has_rect(uint32_t address)
{
    return address && ROMlib_carbon_try_guest_memory(address, sizeof(Rect), 0);
}

static int carbon_DrawThemeMenuItem(ROMlib_carbon_ppc_frame* call)
{
    if (!appearance_has_rect(call->words[0]) || !appearance_has_rect(call->words[1]))
        return appearance_client_result(call, paramErr);
    call->words[4] = appearance_classic_draw_state((uint16_t)call->words[4]);
    return appearance_classic_service(call, 0xfaafb8, 0x1d, "DrawThemeMenuItem");
}

static int appearance_draw_frame(ROMlib_carbon_ppc_frame* call, unsigned selector, const char* name)
{
    if (!appearance_has_rect(call->words[0]))
        return appearance_client_result(call, paramErr);
    call->words[1] = appearance_classic_draw_state(call->words[1]);
    return appearance_classic_service(call, 0xfb8, selector, name);
}

static int carbon_DrawThemeEditTextFrame(ROMlib_carbon_ppc_frame* call)
{
    return appearance_draw_frame(call, 0x09, "DrawThemeEditTextFrame");
}

static int carbon_DrawThemeListBoxFrame(ROMlib_carbon_ppc_frame* call)
{
    return appearance_draw_frame(call, 0x0a, "DrawThemeListBoxFrame");
}

static int carbon_DrawThemeModelessDialogFrame(ROMlib_carbon_ppc_frame* call)
{
    return appearance_draw_frame(call, 0x08, "DrawThemeModelessDialogFrame");
}

static int carbon_DrawThemeGenericWell(ROMlib_carbon_ppc_frame* call)
{
    if (!appearance_has_rect(call->words[0]))
        return appearance_client_result(call, paramErr);
    call->words[1] = appearance_classic_draw_state(call->words[1]);
    return appearance_classic_service(call, 0x1fb8, 0x22, "DrawThemeGenericWell");
}

static int appearance_classic_brush(ROMlib_carbon_ppc_frame* call, unsigned selector, const char* name)
{
    const int16_t brush = call->words[0];
    if (brush >= 49 && brush <= 51)
        call->words[0] = 1;
    return appearance_classic_service(call, 0x1ab8, selector, name);
}

static int carbon_SetThemePen(ROMlib_carbon_ppc_frame* call)
{
    return appearance_classic_brush(call, 0x01, "SetThemePen");
}

static int carbon_SetThemeBackground(ROMlib_carbon_ppc_frame* call)
{
    return appearance_classic_brush(call, 0x02, "SetThemeBackground");
}

static int draw_theme_decoration(ROMlib_carbon_ppc_frame* call, unsigned kind)
{
    const Rect* source = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], sizeof *source, 0) : NULL;
    if (!source)
        return appearance_client_result(call, paramErr);
    ROMlib_carbon_ppc_frame arguments = *call;
    arguments.words[1] = appearance_classic_draw_state(arguments.words[1]);

    bool patched;
    if (!appearance_dispatch_patch(&patched))
        return 0;
    if (patched)
        return appearance_dispatch(0xfb8, kind, call, &arguments);

    Rect bounds = *source;
    if (C_EmptyRect(&bounds))
        return appearance_client_result(call, noErr);

    /* Classic fallback draws an inset highlight and an outer frame. */
    const bool inactive = arguments.words[1] == 0;
    const bool primary = kind == appearance_primary_group;
    RGBColor shadow = {CWC(0x9999), CWC(0x9999), CWC(0x9999)};
    RGBColor highlight = {CWC(0xeeee), CWC(0xeeee), CWC(0xeeee)};
    if (primary && !inactive)
        shadow = (RGBColor){CWC(0x7777), CWC(0x7777), CWC(0x7777)};

    draw_state_t saved;
    draw_state_save(&saved);
    C_PenNormal();
    C_RGBForeColor(&shadow);
    if (kind == appearance_separator) {
        const INTEGER middle = CW(bounds.top) + RECT_HEIGHT(&bounds) / 2;
        C_MoveTo(CW(bounds.left), middle);
        C_LineTo(CW(bounds.right) - 1, middle);
    }
    else {
        C_FrameRect(&bounds);
        if (primary && RECT_WIDTH(&bounds) > 2 && RECT_HEIGHT(&bounds) > 2) {
            C_InsetRect(&bounds, 1, 1);
            C_RGBForeColor(&highlight);
            C_FrameRect(&bounds);
        }
    }
    draw_state_restore(&saved);
    return appearance_client_result(call, noErr);
}

static int carbon_DrawThemePrimaryGroup(ROMlib_carbon_ppc_frame* call)
{
    return draw_theme_decoration(call, appearance_primary_group);
}

static int carbon_DrawThemeSecondaryGroup(ROMlib_carbon_ppc_frame* call)
{
    return draw_theme_decoration(call, appearance_secondary_group);
}

static int carbon_DrawThemeSeparator(ROMlib_carbon_ppc_frame* call)
{
    return draw_theme_decoration(call, appearance_separator);
}

static int carbon_GetThemeMenuBarHeight(ROMlib_carbon_ppc_frame* call)
{
    INTEGER* height = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], sizeof *height, 1) : NULL;
    if (!height)
        return appearance_client_result(call, paramErr);
    bool patched;
    if (!appearance_dispatch_patch(&patched))
        return 0;
    if (patched)
        return appearance_dispatch(0x3b8, 0x1a, call, call);
    const INTEGER encoded = MBarHeight;
    memcpy(height, &encoded, sizeof encoded);
    return appearance_client_result(call, noErr);
}

static int carbon_IsThemeInColor(ROMlib_carbon_ppc_frame* call)
{
    bool patched;
    if (!appearance_dispatch_patch(&patched))
        return 0;
    if (patched) {
        if (!appearance_dispatch(0x6b8, 0x24, call, call))
            return 0;
        /* The saved 68K trap may place its Boolean in the high byte. */
        call->words[0] = call->words[0] != 0;
        return 1;
    }
    return appearance_client_result(call, (int16_t)call->words[0] > 1 && (uint8_t)call->words[1]);
}

static int appearance_reference_layout(const ROMlib_carbon_reference* reference, uint32_t offset)
{
    return reference->code_offset == offset && reference->code_size == 0x405a4 && reference->toc &&
           reference->toc_offset == 0x8000 && reference->toc_size == 0x4958;
}

static bool appearance_pascal_string(uint32_t address)
{
    const Byte* length = address ? ROMlib_carbon_try_guest_memory(address, 1, 0) : NULL;
    return length && ROMlib_carbon_try_guest_memory(address, *length + 1, 0);
}

static int appearance_engine_call(const ROMlib_carbon_reference* ref, uint32_t slot, uint32_t* args, unsigned count,
                                  uint32_t* result)
{
    uint32_t methods, method;
    if (!cfp_private(ref, 0x18eac, NULL, 0, &args[0]) || !args[0] || !cfp_get(args[0], &methods) || !methods ||
        !cfp_get(methods + slot, &method) || !method)
        return 0;
    return cfp_callback(method, args, count, result);
}

/* The native geometry below is from the built-in Platinum engine. An
   installed engine may implement different dimensions and supported kinds. */
static bool appearance_custom_engine(const char* name, uint32_t offset, ROMlib_carbon_reference* ref)
{
    if (!ROMlib_carbon_load_reference("AppearanceLib", name, ref))
        return false;
    if (!appearance_reference_layout(ref, offset))
        return true;
    uint32_t current, builtin;
    if (!cfp_private(ref, 0x18eac, NULL, 0, &current) || !cfp_private(ref, 0x19024, NULL, 0, &builtin))
        return true;
    return current && current != builtin;
}

typedef struct PACKED {
    uint32_t state;
    uint16_t value;
    uint16_t adornment;
} appearance_button_info;

enum appearance_button_geometry { appearance_button_background, appearance_button_content, appearance_button_region };

/* Platinum's per-kind button methods at AppearanceLib 0x1c9c8–0x20a70. */
static Rect appearance_button_bounds(Rect bounds, uint16_t kind, uint16_t adornment,
                                     enum appearance_button_geometry geometry)
{
    const bool rtl = (adornment & 0x10) != 0;
    const int left = CW(bounds.left), right = CW(bounds.right);
    const int top = CW(bounds.top), bottom = CW(bounds.bottom);
    if (geometry == appearance_button_content) {
        switch (kind) {
        case 0:
            InsetRect(&bounds, 2, 2);
            break;
        case 1:
        case 2:
        case 6:
        case 7: {
            const int spacing = kind == 7 ? 15 : 18;
            if (rtl)
                bounds.right = CW((int16_t)(right - spacing));
            else
                bounds.left = CW((int16_t)(left + spacing));
            break;
        }
        case 4:
        case 5: {
            const int arrow = (uint16_t)(bottom - top) < 19 ? 19 : 22;
            InsetRect(&bounds, 6, 2);
            if (rtl)
                bounds.left = CW((int16_t)(left + 6 + arrow));
            else
                bounds.right = CW((int16_t)(right - 6 - arrow));
            break;
        }
        default: {
            const int inset = kind == 8 ? 2 : kind == 9 ? 4 : 3;
            InsetRect(&bounds, inset, inset);
            break;
        }
        }
        return bounds;
    }

    if (kind == 0) {
        int margin = 0;
        if (adornment & 1)
            margin = 3;
        else if (adornment & 4)
            margin = geometry == appearance_button_region ? 3 : 2;
        InsetRect(&bounds, -margin, -margin);
    }
    else if (geometry == appearance_button_region && kind == 4) {
        if (rtl)
            bounds.left = CW((int16_t)(right - 22));
        else
            bounds.right = CW((int16_t)(left + 22));
    }
    else if (geometry == appearance_button_background && kind == 7) {
        bounds.bottom = CW((int16_t)(top + 23));
        if (rtl)
            bounds.left = CW((int16_t)(right - 13));
        else
            bounds.right = CW((int16_t)(left + 13));
    }
    else if ((kind == 1 || kind == 2) && (adornment & 0x20)) {
        const int height_difference = bottom - top - 12;
        const int half = (height_difference - (height_difference < 0 && height_difference % 2)) / 2;
        bounds.top = CW((int16_t)(bottom - half - 12));
        bounds.bottom = CW((int16_t)(bottom - half));
        if (rtl)
            bounds.left = CW((int16_t)(right - 16));
        else
            bounds.right = CW((int16_t)(left + 16));
    }
    return bounds;
}

static int appearance_button_geometry_query(ROMlib_carbon_ppc_frame* call, const char* name, uint32_t offset,
                                            enum appearance_button_geometry geometry)
{
    const Rect* input = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], sizeof(Rect), 0) : NULL;
    const appearance_button_info* info =
        call->words[2] ? ROMlib_carbon_try_guest_memory(call->words[2], sizeof(*info), 0) : NULL;
    const uint16_t kind = call->words[1];
    const uint32_t output = call->words[3];
    if (!input || !info || !output)
        return appearance_client_result(call, paramErr);

    ROMlib_carbon_reference reference;
    if (appearance_custom_engine(name, offset, &reference))
        return ROMlib_carbon_invoke_frame(reference.function, call);
    if (kind > 9)
        return appearance_client_result(call, paramErr);
    Rect bounds = appearance_button_bounds(*input, kind, CW(info->adornment), geometry);
    if (geometry != appearance_button_region) {
        void* destination = ROMlib_carbon_try_guest_memory(output, sizeof bounds, 1);
        if (!destination)
            return appearance_client_result(call, paramErr);
        memcpy(destination, &bounds, sizeof bounds);
    }
    else {
        uint32_t extent;
        if (!cfp_handle_data(output, &extent, 1) || extent < sizeof(Region))
            return appearance_client_result(call, paramErr);
        RgnHandle region = (RgnHandle)SYN68K_TO_US(output);
        if (kind == 0 || kind == 4 || kind == 5) {
            if (!thePort || !ROMlib_valid_qd_port(thePort))
                return appearance_client_result(call, paramErr);
            OpenRgn();
            FrameRoundRect(&bounds, 8, 8);
            CloseRgn(region);
        }
        else
            RectRgn(region, &bounds);
    }
    return appearance_client_result(call, noErr);
}

static int carbon_GetThemeButtonBackgroundBounds(ROMlib_carbon_ppc_frame* call)
{
    return appearance_button_geometry_query(call, "GetThemeButtonBackgroundBounds", 0x31a4,
                                            appearance_button_background);
}

static int carbon_GetThemeButtonContentBounds(ROMlib_carbon_ppc_frame* call)
{
    return appearance_button_geometry_query(call, "GetThemeButtonContentBounds", 0x30c4, appearance_button_content);
}

static int carbon_GetThemeButtonRegion(ROMlib_carbon_ppc_frame* call)
{
    return appearance_button_geometry_query(call, "GetThemeButtonRegion", 0x2fe4, appearance_button_region);
}

/* The Platinum engine keeps the active accent table in the Appearance
   state reached through the System globals pointer. */
static const ColorTable* appearance_accent_table(uint32_t* address, uint32_t* extent)
{
    uint32_t globals, state;
    if (!cfp_get(0x2b6, &globals) || !globals || !cfp_get(globals + 0x2a8, &state) || !state ||
        !cfp_get(state, address) || !*address)
        return NULL;
    const ColorTable* table = cfp_handle_data(*address, extent, 0);
    if (!table || *extent < offsetof(ColorTable, ctTable))
        return NULL;
    const int16_t last = CW(table->ctSize);
    if (last < -1 || (unsigned)(last + 1) > (*extent - offsetof(ColorTable, ctTable)) / sizeof(ColorSpec))
        return NULL;
    return table;
}

static int carbon_GetThemeAccentColors(ROMlib_carbon_ppc_frame* call)
{
    bool patched;
    if (!appearance_dispatch_patch(&patched))
        return 0;
    if (patched)
        return appearance_dispatch(0x3b8, 0x25, call, call);

    const uint32_t output = call->words[0];
    if (!output || !ROMlib_carbon_try_guest_memory(output, sizeof(uint32_t), 1))
        return appearance_client_result(call, paramErr);
    ROMlib_carbon_reference ref = {0};
    if (appearance_custom_engine("GetThemeAccentColors", 0x393d4, &ref))
        return ROMlib_carbon_invoke_frame(ref.function, call);

    uint32_t source, extent;
    if (!ref.function || !appearance_accent_table(&source, &extent))
        return cfp_write(output, 0) && appearance_client_result(call, -30564); /* themeHasNoAccentsErr */
    Handle original = (Handle)SYN68K_TO_US(source);
    const SignedByte saved = HGetState(original);
    HLock(original);
    Handle copy = NewHandle(extent);
    const OSErr status = MemError();
    if (copy)
        memcpy(STARH(copy), STARH(original), extent);
    HSetState(original, saved);
    if (!cfp_write(output, copy ? US_TO_SYN68K(copy) : 0)) {
        if (copy)
            DisposHandle(copy);
        return 0;
    }
    return appearance_client_result(call, status);
}

static int carbon_DrawThemeFocusRegion(ROMlib_carbon_ppc_frame* call)
{
    bool patched;
    if (!appearance_dispatch_patch(&patched))
        return 0;
    if (patched)
        return appearance_dispatch(0x7b8, 0x23, call, call);
    uint32_t extent;
    if (!cfp_handle_data(call->words[0], &extent, 0) || extent < sizeof(Region) ||
        !thePort || !ROMlib_valid_qd_port(thePort))
        return appearance_client_result(call, paramErr);
    ROMlib_carbon_reference ref = {0};
    if (appearance_custom_engine("DrawThemeFocusRegion", 0x39350, &ref))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    const bool focused = (uint8_t)call->words[1] != 0;
    RgnHandle outer = NewRgn();
    if (!outer)
        return appearance_client_result(call, memFullErr);
    RgnHandle inner = focused ? NULL : NewRgn();
    if (!focused && !inner) {
        DisposeRgn(outer);
        return appearance_client_result(call, memFullErr);
    }
    CopyRgn((RgnHandle)SYN68K_TO_US(call->words[0]), outer);
    InsetRgn(outer, -3, -3);

    draw_state_t saved;
    draw_state_save(&saved);
    PenNormal();
    if (focused) {
        RGBColor color = {0};
        uint32_t table;
        const ColorTable* accents = ref.function ? appearance_accent_table(&table, &extent) : NULL;
        if (accents && (int16_t)CW(accents->ctSize) >= 3)
            memcpy(&color, &accents->ctTable[3].rgb, sizeof color);
        RGBForeColor(&color);
        PenSize(2, 2);
        FrameRgn(outer);
    }
    else {
        CopyRgn(outer, inner);
        InsetRgn(inner, 2, 2);
        DiffRgn(outer, inner, outer);
        EraseRgn(outer);
        DisposeRgn(inner);
    }
    draw_state_restore(&saved);
    DisposeRgn(outer);
    return appearance_client_result(call, noErr);
}

static bool appearance_platinum_uses_gray(int16_t depth, bool color_device)
{
    return depth >= (color_device ? 8 : 4);
}

static void appearance_foreground_shade(uint16_t shade)
{
    const uint16_t channel = CW(shade);
    RGBColor color = {channel, channel, channel};
    RGBForeColor(&color);
}

static OSErr appearance_draw_clipped(RgnHandle area, ROMlib_device_draw_proc draw, void* data)
{
    RgnHandle original_clip = NewRgn();
    if (!original_clip)
        return memFullErr;
    GetClip(original_clip);
    SectRgn(area, original_clip, area);
    SetClip(area);
    const OSErr error = ROMlib_device_loop(area, draw, data, 0);
    SetClip(original_clip);
    DisposeRgn(original_clip);
    return error;
}

typedef struct {
    Rect bounds;
    bool horizontal, active, monochrome;
} appearance_tick_drawing;

static void appearance_draw_tick(INTEGER depth, INTEGER flags, GDHandle device, void* context)
{
    (void)device;
    const appearance_tick_drawing* tick = context;
    const bool uses_gray = !tick->monochrome && appearance_platinum_uses_gray(depth, (flags & 1) != 0);
    const bool horizontal = tick->horizontal;
    draw_state_t saved;
    draw_state_save(&saved);
    PenNormal();
    MoveTo(CW(tick->bounds.left) + 1, CW(tick->bounds.top) + 1);
    if (uses_gray)
        appearance_foreground_shade(tick->active ? 0 : 0x7777);
    else {
        Pattern stipple = {0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55};
        Pattern empty = {0};
        ForeColor(blackColor);
        PenPat(tick->active ? empty : stipple);
    }
    Line(horizontal ? 5 : 0, horizontal ? 0 : 5);
    if (uses_gray && tick->active) {
        appearance_foreground_shade(0x8888);
        Move(horizontal ? 1 : 0, horizontal ? 0 : 1);
        Line(horizontal ? 0 : 1, horizontal ? 1 : 0);
        Line(horizontal ? -6 : 0, horizontal ? 0 : -6);
        ForeColor(whiteColor);
        Move(-1, -1);
        Line(horizontal ? 0 : -1, horizontal ? -1 : 0);
        Line(horizontal ? 6 : 0, horizontal ? 0 : 6);
    }
    draw_state_restore(&saved);
}

static int carbon_DrawThemeTickMark(ROMlib_carbon_ppc_frame* call)
{
    const void* input = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], sizeof(Rect), 0) : NULL;
    if (!input || !thePort || !ROMlib_valid_qd_port(thePort))
        return appearance_client_result(call, paramErr);
    ROMlib_carbon_reference ref = {0};
    if (appearance_custom_engine("DrawThemeTickMark", 0x3e78, &ref))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    appearance_tick_drawing tick;
    memcpy(&tick.bounds, input, sizeof tick.bounds);
    if (EmptyRect(&tick.bounds))
        return appearance_client_result(call, noErr);
    tick.horizontal = RECT_WIDTH(&tick.bounds) > RECT_HEIGHT(&tick.bounds);
    tick.active = call->words[1] == 1;
    uint32_t address, extent;
    const ColorTable* accents = ref.function ? appearance_accent_table(&address, &extent) : NULL;
    tick.monochrome = accents && (int16_t)CW(accents->ctSize) == -1;
    RgnHandle area = NewRgn();
    if (!area)
        return appearance_client_result(call, memFullErr);
    RectRgn(area, &tick.bounds);
    const OSErr error = appearance_draw_clipped(area, appearance_draw_tick, &tick);
    DisposeRgn(area);
    return appearance_client_result(call, error);
}

static int appearance_DrawThemeScrollBarDelimiters(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t state = call->words[2];
    const void* input = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], sizeof(Rect), 0) : NULL;
    if (!(uint16_t)call->words[0] || !input || state > 1 || (call->words[3] & ~0x3fdU) ||
        !thePort || !ROMlib_valid_qd_port(thePort))
        return appearance_client_result(call, paramErr);
    ROMlib_carbon_reference ref;
    if (appearance_custom_engine("DrawThemeScrollBarDelimiters", 0xa538, &ref))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    bool patched;
    if (!appearance_dispatch_patch(&patched))
        return 0;
    Rect bounds;
    memcpy(&bounds, input, sizeof bounds);
    draw_state_t saved;
    draw_state_save(&saved);
    PenNormal();
    const uint32_t brush = state ? 17 : 18;
    ROMlib_carbon_ppc_frame arguments = *call;
    arguments.words[0] = brush;
    arguments.words[1] = 32;
    arguments.words[2] = 1;
    const bool brush_set = !patched || appearance_dispatch(0x1ab8, 1, &arguments, &arguments);
    if (brush_set) {
        if (!patched)
            appearance_foreground_shade(state ? 0 : 0x5555);
        const int baseline = CW(bounds.bottom) - 15;
        const int divider = CW(bounds.right) - 15;
        MoveTo(CW(bounds.left), baseline);
        LineTo(CW(bounds.right), baseline);
        MoveTo(divider, CW(bounds.top));
        LineTo(divider, baseline);
    }
    draw_state_restore(&saved);
    return brush_set && appearance_client_result(call, noErr);
}

static int carbon_DrawThemeScrollBarDelimiters(ROMlib_carbon_ppc_frame* call)
{
    call->words[2] = appearance_classic_draw_state(call->words[2]);
    return appearance_DrawThemeScrollBarDelimiters(call);
}

static int appearance_apply_background(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t kind = call->words[0];
    const uint32_t state = call->words[2];
    if (kind < 1 || kind > 4 || state > (kind == 2 ? 2U : 1U) || !appearance_has_rect(call->words[1]))
        return appearance_client_result(call, paramErr);

    ROMlib_carbon_reference reference;
    if (appearance_custom_engine("ApplyThemeBackground", 0x2d40, &reference))
        return ROMlib_carbon_invoke_frame(reference.function, call);
    if (!thePort || !ROMlib_valid_qd_port(thePort))
        return appearance_client_result(call, paramErr);

    /* The built-in Platinum engine uses a solid background pattern and changes
       the port's background color without painting the supplied rectangle. */
    const int16_t depth = call->words[3];
    const bool color_device = (uint8_t)call->words[4] != 0;
    const bool use_gray = appearance_platinum_uses_gray(depth, color_device);
    const uint16_t shade = kind == 1 || !state ? 0xeeee : 0xdddd;
    const uint16_t channel = CW(use_gray ? shade : 0xffff);
    RGBColor color = {channel, channel, channel};
    Pattern solid = {0};
    BackPat(solid);
    RGBBackColor(&color);
    return appearance_client_result(call, noErr);
}

static int carbon_ApplyThemeBackground(ROMlib_carbon_ppc_frame* call)
{
    call->words[2] = appearance_classic_draw_state(call->words[2]);
    return appearance_apply_background(call);
}

static int appearance_ApplyThemeBackground(ROMlib_carbon_ppc_frame* call)
{
    return appearance_apply_background(call);
}

static void appearance_tab_pane_bevel(Rect rect, uint16_t leading, uint16_t trailing)
{
    const int left = CW(rect.left), top = CW(rect.top);
    const int right = CW(rect.right) - 1, bottom = CW(rect.bottom) - 1;
    if (right <= left || bottom <= top)
        return;
    appearance_foreground_shade(leading);
    MoveTo(left, bottom - 1);
    LineTo(left, top);
    LineTo(right - 1, top);
    appearance_foreground_shade(trailing);
    MoveTo(left + 1, bottom);
    LineTo(right, bottom);
    LineTo(right, top + 1);
}

static void appearance_tab_pane_corners(Rect rect)
{
    const int left = CW(rect.left), top = CW(rect.top);
    const int right = CW(rect.right) - 1, bottom = CW(rect.bottom) - 1;
    if (right <= left || bottom <= top)
        return;
    appearance_foreground_shade(0xdddd);
    MoveTo(left, bottom);
    LineTo(left, bottom);
    MoveTo(right, top);
    LineTo(right, top);
}

static int appearance_DrawThemeTabPane(ROMlib_carbon_ppc_frame* call)
{
    Rect* bounds = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], sizeof(Rect), 0) : NULL;
    const uint32_t state = call->words[1];
    if (!bounds || EmptyRect(bounds) || state > 1)
        return appearance_client_result(call, paramErr);

    ROMlib_carbon_reference reference;
    if (appearance_custom_engine("DrawThemeTabPane", 0x3320, &reference))
        return ROMlib_carbon_invoke_frame(reference.function, call);
    if (!thePort || !ROMlib_valid_qd_port(thePort))
        return appearance_client_result(call, paramErr);

    Rect rect = *bounds;
    if (!RectInRgn(&rect, PORT_VIS_REGION(thePort)))
        return appearance_client_result(call, noErr);

    GDHandle device = GetGDevice();
    const int16_t depth = device ? PIXMAP_PIXEL_SIZE(GD_PMAP(device)) : 1;
    const bool color_device = device && (GD_FLAGS(device) & (1 << gdDevType));
    draw_state_t saved;
    draw_state_save(&saved);
    PenNormal();
    if (!appearance_platinum_uses_gray(depth, color_device)) {
        appearance_foreground_shade(0xffff);
        PaintRect(&rect);
        appearance_foreground_shade(0);
        FrameRect(&rect);
    }
    else {
        appearance_foreground_shade(0xeeee);
        PaintRect(&rect);
        appearance_foreground_shade(state ? 0 : 0x5555);
        FrameRect(&rect);
        if (state) {
            InsetRect(&rect, 1, 1);
            appearance_tab_pane_bevel(rect, 0xcccc, 0x9999);
            appearance_tab_pane_corners(rect);
            InsetRect(&rect, 1, 1);
            appearance_tab_pane_bevel(rect, 0xffff, 0xbbbb);
        }
    }
    draw_state_restore(&saved);
    return appearance_client_result(call, noErr);
}

static int carbon_DrawThemeTabPane(ROMlib_carbon_ppc_frame* call)
{
    call->words[1] = appearance_classic_draw_state(call->words[1]);
    return appearance_DrawThemeTabPane(call);
}

enum appearance_tab_direction { appearance_tab_north, appearance_tab_south, appearance_tab_east, appearance_tab_west };

/* AppearanceLib 0x2211c combines a rectangular center with two independent
   end masks. The masks include the pane overlap even when the center does not. */
static OSErr appearance_tab_end_region(RgnHandle region, Rect bounds, enum appearance_tab_direction direction,
                                       bool small, int depth, bool trailing)
{
    static const uint8_t horizontal_inset[2][24] = {
        {10, 8, 7, 6, 6, 5, 5, 5, 4, 4, 4, 3, 3, 3, 2, 2, 2, 1, 1, 1, 0, 0, 0, 0},
        {10, 8, 7, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2, 1, 1, 0, 0, 0, 0},
    };
    static const uint8_t vertical_width[2][12] = {
        {4, 7, 10, 13, 16, 19, 21, 22, 23, 23, 24, 24},
        {4, 6, 8, 10, 12, 14, 16, 17, 18, 18, 19, 19},
    };
    const bool horizontal = direction <= appearance_tab_south;
    const int mask_depth = small ? 19 : 24, end_extent = 12;
    const int width = horizontal ? end_extent : mask_depth;
    const int height = horizontal ? mask_depth : end_extent;
    _Alignas(128) Byte pixels[24][4] = {0};
    for (int y = 0; y < height; ++y) {
        int left = 0, right = width;
        if (horizontal) {
            const int row = direction == appearance_tab_south ? height - y - 1 : y;
            if (trailing)
                right -= horizontal_inset[small][row];
            else
                left = horizontal_inset[small][row];
        }
        else {
            const int filled = vertical_width[small][trailing ? height - y - 1 : y];
            if (direction == appearance_tab_west)
                left = width - filled;
            else
                right = filled;
        }
        for (int x = left; x < right; ++x)
            pixels[y][x / 8] |= (Byte)(0x80 >> (x % 8));
    }

    BitMap bitmap = {.rowBytes = CWC(4), .bounds = {.bottom = CW((int16_t)height), .right = CW((int16_t)width)}};
    PP_ASSIGN(bitmap.baseAddr, (Ptr)pixels);
    const OSErr error = BitMapToRegion(region, &bitmap);
    if (error)
        return error;
    int x = CW(bounds.left), y = CW(bounds.top);
    if (horizontal) {
        if (trailing)
            x = CW(bounds.right) - end_extent;
        if (direction == appearance_tab_south)
            y = CW(bounds.bottom) - depth;
    }
    else {
        if (trailing)
            y = CW(bounds.bottom) - end_extent;
        if (direction == appearance_tab_east)
            x = CW(bounds.right) - depth;
    }
    OffsetRgn(region, x, y);
    return noErr;
}

static Rect appearance_tab_center(Rect bounds, enum appearance_tab_direction direction, int depth)
{
    if (direction <= appearance_tab_south) {
        InsetRect(&bounds, 12, 0);
        if (direction == appearance_tab_north)
            bounds.bottom = CW((int16_t)(CW(bounds.top) + depth));
        else
            bounds.top = CW((int16_t)(CW(bounds.bottom) - depth));
    }
    else {
        InsetRect(&bounds, 0, 12);
        if (direction == appearance_tab_west)
            bounds.right = CW((int16_t)(CW(bounds.left) + depth));
        else
            bounds.left = CW((int16_t)(CW(bounds.right) - depth));
    }
    return bounds;
}

static int carbon_GetThemeTabRegion(ROMlib_carbon_ppc_frame* call)
{
    Rect* input = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], sizeof(Rect), 0) : NULL;
    const uint16_t style = call->words[1], direction = call->words[2];
    uint32_t extent;
    if (!input || EmptyRect(input) || !cfp_handle_data(call->words[3], &extent, 1) || extent < sizeof(Region))
        return appearance_client_result(call, paramErr);
    ROMlib_carbon_reference reference;
    if (appearance_custom_engine("GetThemeTabRegion", 0x35a4, &reference))
        return ROMlib_carbon_invoke_frame(reference.function, call);
    if (style > 4 || direction > appearance_tab_west)
        return appearance_client_result(call, paramErr);

    const Rect bounds = *input;
    const uint16_t cross =
        direction <= appearance_tab_south ? CW(bounds.bottom) - CW(bounds.top) : CW(bounds.right) - CW(bounds.left);
    const bool small = cross < 21;
    const int depth = (small ? 16 : 21) + (style >= 3 ? 3 : 0);
    RgnHandle output = (RgnHandle)SYN68K_TO_US(call->words[3]);
    RgnHandle temporary = NewRgn();
    if (!temporary)
        return appearance_client_result(call, memFullErr);
    OSErr error = appearance_tab_end_region(output, bounds, direction, small, depth, false);
    if (!error) {
        Rect center = appearance_tab_center(bounds, direction, depth);
        RectRgn(temporary, &center);
        UnionRgn(output, temporary, output);
        error = appearance_tab_end_region(temporary, bounds, direction, small, depth, true);
        if (!error)
            UnionRgn(output, temporary, output);
    }
    DisposeRgn(temporary);
    return appearance_client_result(call, error);
}

static int carbon_CopyThemeIdentifier(ROMlib_carbon_ppc_frame* call)
{
    void* output = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], sizeof(uint32_t), 1) : NULL;
    if (!output)
        return appearance_client_result(call, paramErr);

    /* CarbonLib 1.4 returns this identifier even when a custom theme is active. */
    _Alignas(64) char identifier[64] = "com.apple.theme.appearance.platinum";
    const uint32_t string = ROMlib_cfstring_cstring(0, US_TO_SYN68K(identifier), 0x0600);
    if (!string) {
        ROMlib_carbon_reference reference;
        if (ROMlib_carbon_reference_function("CarbonLib", "CopyThemeIdentifier", &reference))
            return ROMlib_carbon_invoke_frame(reference.function, call);
    }
    const uint32_t encoded = CL(string);
    memcpy(output, &encoded, sizeof encoded);
    return appearance_client_result(call, string ? noErr : memFullErr);
}

enum {
    appearance_window_title = 0,
    appearance_window_title_text = 1,
    appearance_window_close = 2,
    appearance_window_zoom = 3,
    appearance_window_drag = 5,
    appearance_window_grow = 6,
    appearance_window_collapse = 7,
    appearance_window_structure = 32,
    appearance_window_content = 33,
    appearance_window_has_grow = 1,
    appearance_window_has_zoom = 0x18,
    appearance_window_has_close = 0x20,
    appearance_window_has_collapse = 0x40,
    appearance_window_has_title = 0x80,
    appearance_window_collapsed = 0x100,
    appearance_window_dirty = 0x200,
};

typedef struct PACKED {
    uint16_t size;
    int16_t title_height;
    int16_t title_width;
    int16_t popup_offset;
    int16_t popup_width;
    uint16_t popup_position;
} appearance_window_metrics;

typedef struct {
    Rect content, title, title_text, close, zoom, collapse, grow, grow_frame;
    uint32_t attributes;
    uint16_t flavor;
    bool active, zoom_visible, collapse_visible;
} appearance_window_geometry;

/* Platinum's document window metrics and rectangular widget bounds. */
static void appearance_document_geometry(appearance_window_geometry* layout, const appearance_window_metrics* metrics)
{
    const uint32_t attributes = layout->attributes;
    const int left = CW(layout->content.left), right = CW(layout->content.right);
    const int top = CW(layout->content.top), bottom = CW(layout->content.bottom);
    const int requested_height = (attributes & appearance_window_has_title) ? CW(metrics->title_height) + 6 : 0;
    const int height = requested_height > 22 ? requested_height : 22;
    C_SetRect(&layout->title, left - 6, top - height, right + 6, top);

    const int widget_top = top - height - 1 + (height - 12) / 2;
    C_SetRect(&layout->close, left - 2, widget_top, left + 11, widget_top + 13);
    C_SetRect(&layout->collapse, right - 11, widget_top, right + 2, widget_top + 13);
    layout->zoom = layout->collapse;
    if (attributes & appearance_window_has_collapse)
        OffsetRect(&layout->zoom, -16, 0);
    const int label_min = (attributes & appearance_window_has_close) ? left + 15 : left - 1;
    const int widget_min = label_min + ((attributes & appearance_window_dirty) ? 0 : 12);
    layout->collapse_visible = (attributes & appearance_window_has_collapse) != 0;
    layout->zoom_visible = (attributes & appearance_window_has_zoom) != 0;
    if (layout->zoom_visible && CW(layout->zoom.left) <= widget_min) {
        layout->collapse_visible = false;
        layout->zoom = layout->collapse;
        layout->zoom_visible = CW(layout->zoom.left) > widget_min;
    }
    else if (!layout->zoom_visible && CW(layout->collapse.left) <= widget_min)
        layout->collapse_visible = false;

    int label_max = right + 1;
    if (layout->zoom_visible)
        label_max = CW(layout->zoom.left) - 4;
    else if (layout->collapse_visible)
        label_max = CW(layout->collapse.left) - 4;
    int label_left = left - 6 + ((uint16_t)(right - left + 12) - CW(metrics->title_width)) / 2;
    if (label_left < label_min)
        label_left = label_min;
    int label_right = label_left + CW(metrics->title_width);
    if (label_right > label_max)
        label_right = label_max;
    C_SetRect(&layout->title_text, label_left, top - height + 2, label_right, top - 2);

    C_SetRect(&layout->grow, right - 15, bottom - 15, right + 2, bottom + 2);
    layout->grow_frame = layout->grow;
    layout->grow_frame.right = CW((int16_t)(right + 7));
    layout->grow_frame.bottom = CW((int16_t)(bottom + 7));
}

static OSErr appearance_window_shape(const appearance_window_geometry* layout, RgnHandle output, RgnHandle scratch)
{
    Rect bounds = layout->content;
    switch (layout->flavor) {
    case 0:
        if (layout->attributes & appearance_window_collapsed)
            bounds = layout->title;
        else {
            InsetRect(&bounds, -6, -6);
            bounds.top = layout->title.top;
        }
        break;
    case 1:
    case 3:
        InsetRect(&bounds, -3, -3);
        break;
    case 5:
    case 6:
        InsetRect(&bounds, -1, -1);
        break;
    }
    RectRgn(output, &bounds);
    if (layout->flavor != 5) {
        if (!scratch)
            return memFullErr;
        if (layout->flavor != 6) {
            bounds.right = CW((int16_t)(CW(bounds.right) - 1));
            bounds.bottom = CW((int16_t)(CW(bounds.bottom) - 1));
        }
        OffsetRect(&bounds, 2, 2);
        RectRgn(scratch, &bounds);
        UnionRgn(scratch, output, output);
    }
    return noErr;
}

static Rect appearance_window_part(const appearance_window_geometry* layout, uint16_t code)
{
    const uint32_t attributes = layout->attributes;
    const bool document = layout->flavor == 0;
    Rect rect = {0};
    switch (code) {
    case appearance_window_title:
        if (document)
            rect = layout->title;
        break;
    case appearance_window_title_text:
        if (document && (attributes & appearance_window_has_title))
            rect = layout->title_text;
        break;
    case appearance_window_close:
        if (document && layout->active && (attributes & appearance_window_has_close))
            rect = layout->close;
        break;
    case appearance_window_zoom:
        if (document && layout->active && layout->zoom_visible)
            rect = layout->zoom;
        break;
    case appearance_window_collapse:
        if (document && layout->active && layout->collapse_visible)
            rect = layout->collapse;
        break;
    case appearance_window_grow:
        if (document && layout->active && (attributes & appearance_window_has_grow))
            rect = layout->grow_frame;
        break;
    }
    if (code == appearance_window_close || code == appearance_window_zoom || code == appearance_window_collapse)
        InsetRect(&rect, 1, 1);
    return rect;
}

static OSErr appearance_window_region_query(const appearance_window_geometry* layout, uint16_t code, RgnHandle region)
{
    const bool document = layout->flavor == 0;
    const bool collapsed = (layout->attributes & appearance_window_collapsed) != 0;
    const bool grows = document && (layout->attributes & appearance_window_has_grow);
    const bool drag = document && code == appearance_window_drag && !collapsed;
    const bool needs_scratch =
        code == appearance_window_structure || drag || (code == appearance_window_content && grows && !collapsed);
    RgnHandle scratch = needs_scratch ? NewRgn() : NULL;
    if (needs_scratch && !scratch)
        return memFullErr;

    OSErr error = noErr;
    if (code == appearance_window_structure || drag) {
        error = appearance_window_shape(layout, region, scratch);
        if (!error && drag) {
            RectRgn(scratch, &layout->content);
            DiffRgn(region, scratch, region);
            const uint16_t controls[] = {appearance_window_close, appearance_window_zoom, appearance_window_collapse,
                                         appearance_window_grow};
            for (size_t i = 0; i < NELEM(controls); ++i) {
                Rect box = appearance_window_part(layout, controls[i]);
                RectRgn(scratch, &box);
                DiffRgn(region, scratch, region);
            }
        }
    }
    else if (code == appearance_window_content) {
        if (document && collapsed)
            SetEmptyRgn(region);
        else {
            RectRgn(region, &layout->content);
            if (grows) {
                RectRgn(scratch, &layout->grow);
                DiffRgn(region, scratch, region);
            }
        }
    }
    else {
        Rect box = appearance_window_part(layout, code);
        RectRgn(region, &box);
    }
    if (scratch)
        DisposeRgn(scratch);
    if (!error && EmptyRgn(region) && !(collapsed && code == appearance_window_content))
        error = -30593; /* errWindowRegionCodeInvalid */
    return error;
}

static uint16_t appearance_window_hit(const appearance_window_geometry* layout, Point point)
{
    RgnHandle structure = NewRgn(), scratch = NewRgn();
    const bool inside =
        structure && scratch && !appearance_window_shape(layout, structure, scratch) && PtInRgn(point, structure);
    if (structure)
        DisposeRgn(structure);
    if (scratch)
        DisposeRgn(scratch);
    if (!inside)
        return 0;

    static const uint16_t parts[] = {appearance_window_grow, appearance_window_title, appearance_window_close,
                                     appearance_window_zoom, appearance_window_collapse};
    static const uint16_t hits[] = {6, 1, 2, 3, 7};
    const bool collapsed = (layout->attributes & appearance_window_collapsed) != 0;
    if (layout->active) {
        for (size_t i = collapsed ? 1 : 0; i < NELEM(parts); ++i) {
            Rect box = appearance_window_part(layout, parts[i]);
            if (C_PtInRect(point, &box))
                return hits[i];
        }
    }
    Rect content = layout->content, grow = layout->grow;
    const bool in_content = C_PtInRect(point, &content);
    if (!collapsed && in_content &&
        !(layout->flavor == 0 && (layout->attributes & appearance_window_has_grow) && C_PtInRect(point, &grow)))
        return appearance_window_content;
    if (layout->flavor == 0 && !in_content)
        return appearance_window_drag;
    return appearance_window_structure;
}

static int appearance_window_query(ROMlib_carbon_ppc_frame* call, bool hit)
{
    const uint16_t flavor = call->words[0];
    const uint32_t state = call->words[2], attributes = call->words[4];
    const Rect* rect = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], sizeof(Rect), 0) : NULL;
    const appearance_window_metrics* metrics =
        call->words[3] ? ROMlib_carbon_try_guest_memory(call->words[3], sizeof(*metrics), 0) : NULL;
    uint32_t extent;
    uint16_t* code = hit && call->words[6] ? ROMlib_carbon_try_guest_memory(call->words[6], sizeof(*code), 1) : NULL;
    RgnHandle region = !hit && cfp_handle_data(call->words[6], &extent, 1) && extent >= sizeof(Region)
                           ? (RgnHandle)SYN68K_TO_US(call->words[6])
                           : NULL;
    if (hit && code) {
        const uint16_t zero = 0;
        memcpy(code, &zero, sizeof zero);
    }
    if ((!code && hit) || (!region && !hit))
        return appearance_client_result(call, hit ? 0 : paramErr);

    const char* name = hit ? "GetThemeWindowRegionHit" : "GetThemeWindowRegion";
    ROMlib_carbon_reference reference = {0};
    if (appearance_custom_engine(name, hit ? 0xa29c : 0x9d9c, &reference))
        return ROMlib_carbon_invoke_frame(reference.function, call);
    if (!rect || !metrics || CW(rect->right) < CW(rect->left) || CW(rect->bottom) < CW(rect->top) || state > 1 ||
        (attributes & ~0x3fdu)) {
        if (region)
            SetEmptyRgn(region);
        return appearance_client_result(call, hit ? 0 : paramErr);
    }
    switch (flavor) {
    case 0:
    case 1:
    case 3:
    case 5:
    case 6:
        break;
    default:
        if (reference.function)
            return ROMlib_carbon_invoke_frame(reference.function, call);
        if (region)
            SetEmptyRgn(region);
        return appearance_client_result(call, hit ? 0 : paramErr);
    }
    appearance_window_geometry layout = {
        .content = *rect,
        .attributes = attributes,
        .flavor = flavor,
        .active = state == 1,
    };
    if (!flavor)
        appearance_document_geometry(&layout, metrics);
    if (hit) {
        const Point point = {.v = CW((int16_t)(call->words[5] >> 16)), .h = CW((int16_t)call->words[5])};
        const uint16_t region_code = appearance_window_hit(&layout, point);
        const uint16_t encoded = CW(region_code);
        memcpy(code, &encoded, sizeof encoded);
        return appearance_client_result(call, region_code != 0);
    }
    const OSErr error = appearance_window_region_query(&layout, (uint16_t)call->words[5], region);
    if (error)
        SetEmptyRgn(region);
    return appearance_client_result(call, error);
}

static int carbon_GetThemeWindowRegion(ROMlib_carbon_ppc_frame* call)
{
    call->words[2] = appearance_classic_draw_state(call->words[2]);
    return appearance_window_query(call, false);
}

static int carbon_GetThemeWindowRegionHit(ROMlib_carbon_ppc_frame* call)
{
    call->words[2] = appearance_classic_draw_state(call->words[2]);
    return appearance_window_query(call, true);
}

static void appearance_popup_arrow_bits(uint16_t* rows, unsigned orientation, int size, bool inactive)
{
    const int length = 3 + size * 2;
    const int radius = length / 2;
    const bool horizontal = (orientation & 1) == 0;
    const int height = horizontal ? length : radius + 1;
    const int width = horizontal ? radius + 1 : length;
    const int parity = orientation == 0 || orientation == 3 ? radius & 1 : 0;
    const bool reversed = orientation == 1 || orientation == 2;
    for (int y = 0; y < height; ++y) {
        uint16_t bits = 0;
        for (int x = 0; x < width; ++x) {
            const int rise = horizontal ? abs(y - radius) : abs(x - radius);
            const int distance = horizontal ? x : y;
            const int run = reversed ? radius - distance : distance;
            if (rise <= run && (!inactive || ((x + y) & 1) == parity))
                bits |= 0x8000U >> x;
        }
        rows[y] = CW(bits);
    }
}

static int appearance_DrawThemePopupArrow(ROMlib_carbon_ppc_frame* call)
{
    const Rect* source = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], sizeof(Rect), 0) : NULL;
    if (!source)
        return appearance_client_result(call, paramErr);
    ROMlib_carbon_reference reference;
    if (appearance_custom_engine("DrawThemePopupArrow", 0x4358, &reference))
        return ROMlib_carbon_invoke_frame(reference.function, call);

    GrafPtr port = thePort;
    Rect bounds = *source;
    if (EmptyRect(&bounds))
        return appearance_client_result(call, noErr);
    if (!port || !ROMlib_valid_qd_port(port))
        return appearance_client_result(call, paramErr);
    if (!RectInRgn(&bounds, PORT_VIS_REGION(port)))
        return appearance_client_result(call, noErr);

    const uint16_t orientation = call->words[1], size = call->words[2];
    const uint32_t state = call->words[3];
    if (orientation > 3 || size > 3 || state > 3)
        return appearance_client_result(call, paramErr);

    RgnHandle original_clip = NewRgn();
    if (!original_clip)
        return appearance_client_result(call, memFullErr);
    GetClip(original_clip);
    ClipRect(&bounds);
    SectRgn(PORT_CLIP_REGION(port), original_clip, PORT_CLIP_REGION(port));

    GDHandle device = GetGDevice();
    const int16_t depth = device ? PIXMAP_PIXEL_SIZE(GD_PMAP(device)) : 1;
    const bool color_device = device && (GD_FLAGS(device) & (1 << gdDevType));
    const bool solid_inactive = !state && appearance_platinum_uses_gray(depth, color_device);
    const int length = 3 + size * 2, narrow = length / 2 + 1;
    const int width = (orientation & 1) ? length : narrow;
    const int height = (orientation & 1) ? narrow : length;
    Rect destination = {bounds.top, bounds.left, CW(CW(bounds.top) + height), CW(CW(bounds.left) + width)};
    draw_state_t saved;
    draw_state_save(&saved);
    int completed = 1;
    if (call->words[4]) {
        _Alignas(8) Rect erase_bounds = destination;
        const ROMlib_carbon_ppc_frame arguments = {
            .words = {US_TO_SYN68K(&erase_bounds), call->words[5], depth, color_device},
        };
        uint32_t ignored;
        completed = ROMlib_carbon_invoke_pascal(call->words[4], 0x1bc0, &arguments, &ignored);
    }
    if (!ROMlib_valid_qd_port(port)) {
        DisposeRgn(original_clip);
        return appearance_client_result(call, paramErr);
    }
    SetPort(port);
    if (completed && !EmptyRect(&destination)) {
        _Alignas(32) uint16_t rows[9];
        appearance_popup_arrow_bits(rows, orientation, size, !state && !solid_inactive);
        BitMap mask = {.rowBytes = CWC(2), .bounds = {0, 0, CW(height), CW(width)}};
        FIELD_ASSIGN_RAW(mask.baseAddr, RM((Ptr)rows));
        PenNormal();
        appearance_foreground_shade(solid_inactive ? 0x7777 : 0);
        RGBColor paper = ROMlib_white_rgb_color;
        RGBBackColor(&paper);
        CopyBits(&mask, PORT_BITS_FOR_COPY(port), &mask.bounds, &destination, srcOr, NULL);
    }
    draw_state_restore(&saved);
    SetClip(original_clip);
    DisposeRgn(original_clip);
    return completed && appearance_client_result(call, noErr);
}

static int carbon_DrawThemePopupArrow(ROMlib_carbon_ppc_frame* call)
{
    call->words[3] = appearance_classic_draw_state(call->words[3]);
    return appearance_DrawThemePopupArrow(call);
}

static int carbon_GetThemeStandaloneGrowBoxBounds(ROMlib_carbon_ppc_frame* call)
{
    Rect* bounds = call->words[3] ? ROMlib_carbon_try_guest_memory(call->words[3], sizeof *bounds, 1) : NULL;
    if (!bounds)
        return appearance_client_result(call, paramErr);
    ROMlib_carbon_reference ref;
    if (appearance_custom_engine("GetThemeStandaloneGrowBoxBounds", 0x42a4, &ref))
        return ROMlib_carbon_invoke_frame(ref.function, call);

    const int16_t top = call->words[0] >> 16;
    const int16_t anchor = call->words[0];
    const int16_t size = (uint8_t)call->words[2] ? 11 : 16;
    const int16_t left = call->words[1] & 1 ? anchor - size : anchor;
    const Rect result = {CW(top), CW(left), CW(top + size), CW(left + size)};
    memcpy(bounds, &result, sizeof result);
    return appearance_client_result(call, noErr);
}

/* The guest record is 68K-aligned and its 32-bit values can be unaligned. */
typedef struct PACKED {
    uint16 kind;
    Rect bounds;
    uint32 min, max, value, reserved;
    uint16 attributes;
    uint8 enable_state, filler;
} appearance_track_info;

_Static_assert(sizeof(appearance_track_info) == 30, "ThemeTrackDrawInfo prefix");

typedef struct {
    Rect groove, thumb, drag;
    bool horizontal;
} appearance_slider_geometry;

static Rect appearance_transpose_rect(Rect rect)
{
    return (Rect){rect.left, rect.top, rect.right, rect.bottom};
}

static int32_t appearance_pixel_round(double position)
{
    if (position >= INT32_MAX)
        return INT32_MAX;
    if (position <= INT32_MIN)
        return INT32_MIN;
    return (int32_t)position;
}

static appearance_slider_geometry appearance_slider(const appearance_track_info* track, uint8_t direction)
{
    const bool horizontal = RECT_WIDTH(&track->bounds) > RECT_HEIGHT(&track->bounds);
    const int16_t start = horizontal ? CW(track->bounds.left) : CW(track->bounds.top);
    const int16_t end = horizontal ? CW(track->bounds.right) : CW(track->bounds.bottom);
    const int16_t cross_start = horizontal ? CW(track->bounds.top) : CW(track->bounds.left);
    const int16_t cross_end = horizontal ? CW(track->bounds.bottom) : CW(track->bounds.right);
    const int16_t thumb_cross_start = direction == 2 ? cross_start : cross_end - 16;
    const int16_t thumb_length = direction == 1 || direction == 2 ? 15 : 13;
    const int16_t groove_cross_start = thumb_cross_start + (direction == 2 ? 3 : direction == 1 ? 6 : 4);
    const int16_t travel = end - start - 10 - thumb_length;
    const int32_t min = CL(track->min), max = CL(track->max), value = CL(track->value);
    const int32_t range = (uint32_t)max - (uint32_t)min;
    int offset = 0;
    if (range > 0 && value != min) {
        if (value >= max)
            offset = travel;
        else {
            const int32_t delta = (uint32_t)value - (uint32_t)min;
            offset = appearance_pixel_round((float)delta * travel / (float)range + 0.5);
        }
    }
    const int16_t thumb_start = CW(track->attributes) & 2 ? end - 5 - (int64_t)offset - thumb_length
                                                         : start + 5 + (int64_t)offset;
    return (appearance_slider_geometry){
        .groove = {CW(groove_cross_start), CW(start + 2), CW(groove_cross_start + 7), CW(end - 2)},
        .thumb = {CW(thumb_cross_start), CW(thumb_start), CW(thumb_cross_start + 16), CW(thumb_start + thumb_length)},
        .drag = {CW(thumb_cross_start), CW(start + 5), CW(thumb_cross_start + 16), CW(end - 5)},
        .horizontal = horizontal,
    };
}

static int carbon_GetThemeTrackBounds(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t address = call->words[0];
    const void* input = address ? ROMlib_carbon_try_guest_memory(address, sizeof(appearance_track_info), 0) : NULL;
    Rect* output = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], sizeof *output, 1) : NULL;
    if (!input || !output)
        return appearance_client_result(call, paramErr);
    appearance_track_info track;
    memcpy(&track, input, sizeof track);

    ROMlib_carbon_reference ref = {0};
    if (appearance_custom_engine("GetThemeTrackBounds", 0x3ac4, &ref))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    const uint16_t kind = CW(track.kind);
    Rect result = track.bounds;
    int32_t status = noErr;
    switch (kind) {
    case 0: /* Medium scrollbar. */
    case 1: /* Small scrollbar. */
        if (CW(result.left) > CW(result.right) || CW(result.top) > CW(result.bottom))
            status = paramErr;
        break;
    case 2: { /* Medium slider. */
        const uint8_t* direction = ROMlib_carbon_try_guest_memory(address + sizeof track, 1, 0);
        if (!direction)
            return appearance_client_result(call, paramErr);
        appearance_slider_geometry slider = appearance_slider(&track, *direction);
        C_UnionRect(&slider.groove, &slider.thumb, &result);
        if (!slider.horizontal)
            result = appearance_transpose_rect(result);
        break;
    }
    case 3: /* Medium progress bar. */
    case 4: /* Medium indeterminate bar. */
        result.bottom = CW(CW(result.top) + 14);
        break;
    default:
        return ref.function ? ROMlib_carbon_invoke_frame(ref.function, call) : appearance_client_result(call, paramErr);
    }
    memcpy(output, &result, sizeof result);
    return appearance_client_result(call, status);
}

typedef struct {
    Rect drag, thumb;
    bool horizontal, show_thumb;
} appearance_scroll_thumb;

enum { appearance_arrow_style_field = -0x75b0 };
static uint16_t appearance_arrow_style;

static uint16_t appearance_platinum_preference(const ROMlib_carbon_reference* ref, int32_t offset)
{
    uint16_t value;
    const void* stored =
        ref->toc ? ROMlib_carbon_try_guest_memory(US_TO_SYN68K(ref->toc) + offset, sizeof value, 0) : NULL;
    if (!stored)
        return offset == appearance_arrow_style_field ? appearance_arrow_style : 0;
    memcpy(&value, stored, sizeof value);
    return CW(value);
}

static appearance_scroll_thumb appearance_scroll_track(const appearance_track_info* track, int32_t view_size,
                                                       bool proportional)
{
    const bool horizontal = CW(track->attributes) & 1;
    Rect drag = horizontal ? track->bounds : appearance_transpose_rect(track->bounds);
    const int16_t start = CW(drag.left);
    const int16_t end = CW(drag.right);
    const int16_t length = end - start;
    appearance_scroll_thumb result = {.drag = drag, .thumb = drag, .horizontal = horizontal};
    if (end < start)
        result.drag.right = result.thumb.right = drag.left;
    if (end < start || track->enable_state != 0)
        return result;

    const int16_t thickness = RECT_HEIGHT(&drag) + 1;
    const int32_t min = CL(track->min), max = CL(track->max), value = CL(track->value);
    const int32_t range = (uint32_t)max - (uint32_t)min;
    int16_t thumb_length = thickness;
    if (proportional && view_size > 0) {
        const int32_t extent = (uint32_t)view_size + (uint32_t)range;
        if (extent) {
            const int16_t scaled = appearance_pixel_round((float)length * view_size / (float)extent);
            thumb_length = MIN(length, MAX(thickness, scaled));
        }
    }

    const int16_t travel = length - thumb_length;
    int32_t offset = 0;
    if (range > 0 && value != min) {
        if (value >= max)
            offset = travel;
        else {
            const int32_t delta = (uint32_t)value - (uint32_t)min;
            offset = appearance_pixel_round((float)delta * travel / (float)range + 0.5);
        }
    }
    const int16_t thumb_start = start + (int64_t)offset;
    result.thumb.left = CW(thumb_start);
    result.thumb.right = CW(thumb_start + thumb_length);
    result.show_thumb = (CW(track->attributes) & 4) && length >= thickness;
    return result;
}

static bool appearance_scroll_from_guest(const appearance_track_info* track, uint32_t address,
                                         const ROMlib_carbon_reference* ref, appearance_scroll_thumb* result)
{
    int32_t view_size = 0;
    const bool proportional = appearance_platinum_preference(ref, -0x75ae) == 1;
    if (proportional && track->enable_state == 0) {
        uint32_t encoded;
        const void* view = ROMlib_carbon_try_guest_memory(address + sizeof *track, sizeof encoded, 0);
        if (!view)
            return false;
        memcpy(&encoded, view, sizeof encoded);
        view_size = CL(encoded);
    }
    *result = appearance_scroll_track(track, view_size, proportional);
    return true;
}

static OSErr appearance_slider_thumb_region(RgnHandle region, const appearance_slider_geometry* slider,
                                            unsigned direction)
{
    /* Build the 1-bit silhouette in a horizontal coordinate system. The
       vertical thumb is the transpose of the same shape. */
    _Alignas(32) uint16_t pixels[16] = {0};
    const bool pointed = direction == 1 || direction == 2;
    const unsigned width = pointed ? 15 : 13;
    for (unsigned y = 0; y < NELEM(pixels); ++y) {
        const unsigned row = direction == 1 ? 15 - y : y;
        const unsigned inset = pointed ? (row >= 10 ? row - 9 : row == 0) : (y == 0 || y == 15);
        for (unsigned x = inset; x < width - inset; ++x) {
            const unsigned line = slider->horizontal ? y : x;
            const unsigned bit = slider->horizontal ? x : y;
            pixels[line] |= 0x8000U >> bit;
        }
    }
    for (unsigned row = 0; row < NELEM(pixels); ++row)
        pixels[row] = CW(pixels[row]);
    const int bottom = slider->horizontal ? 16 : width;
    const int right = slider->horizontal ? width : 16;
    BitMap mask = {.rowBytes = CWC(2), .bounds = {0, 0, CW(bottom), CW(right)}};
    FIELD_ASSIGN_RAW(mask.baseAddr, RM((Ptr)pixels));
    const OSErr error = BitMapToRegion(region, &mask);
    if (!error) {
        const Rect thumb = slider->horizontal ? slider->thumb : appearance_transpose_rect(slider->thumb);
        OffsetRgn(region, CW(thumb.left), CW(thumb.top));
    }
    return error;
}

static int carbon_GetThemeTrackThumbRgn(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t address = call->words[0];
    const void* input = address ? ROMlib_carbon_try_guest_memory(address, sizeof(appearance_track_info), 0) : NULL;
    uint32_t extent;
    if (!input || !cfp_handle_data(call->words[1], &extent, 1) || extent < sizeof(Region))
        return appearance_client_result(call, paramErr);
    ROMlib_carbon_reference ref = {0};
    if (appearance_custom_engine("GetThemeTrackThumbRgn", 0x3b5c, &ref))
        return ROMlib_carbon_invoke_frame(ref.function, call);

    appearance_track_info track;
    memcpy(&track, input, sizeof track);
    const uint16_t kind = CW(track.kind);
    RgnHandle region = (RgnHandle)SYN68K_TO_US(call->words[1]);
    if (kind <= 1) {
        appearance_scroll_thumb scroll;
        if (!appearance_scroll_from_guest(&track, address, &ref, &scroll))
            return appearance_client_result(call, paramErr);
        if (scroll.show_thumb) {
            const Rect bounds = scroll.horizontal ? scroll.thumb : appearance_transpose_rect(scroll.thumb);
            RectRgn(region, &bounds);
        }
        return appearance_client_result(call, noErr);
    }
    if (kind == 2) {
        const uint8_t* direction = ROMlib_carbon_try_guest_memory(address + sizeof track, 1, 0);
        if (!direction)
            return appearance_client_result(call, paramErr);
        const appearance_slider_geometry slider = appearance_slider(&track, *direction);
        return appearance_client_result(call, appearance_slider_thumb_region(region, &slider, *direction));
    }
    return kind > 4 && ref.function ? ROMlib_carbon_invoke_frame(ref.function, call)
                                    : appearance_client_result(call, paramErr);
}

static int carbon_HitTestThemeTrack(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t address = call->words[0];
    const void* input = address ? ROMlib_carbon_try_guest_memory(address, sizeof(appearance_track_info), 0) : NULL;
    void* output = call->words[2] ? ROMlib_carbon_try_guest_memory(call->words[2], sizeof(INTEGER), 1) : NULL;
    if (!input || !output)
        return appearance_client_result(call, 0);
    ROMlib_carbon_reference ref = {0};
    if (appearance_custom_engine("HitTestThemeTrack", 0x39bc, &ref))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    appearance_track_info track;
    memcpy(&track, input, sizeof track);
    const uint16_t kind = CW(track.kind);
    if (kind > 4 && ref.function)
        return ROMlib_carbon_invoke_frame(ref.function, call);

    INTEGER part = 0;
    memcpy(output, &part, sizeof part);
    Point point = {(int16_t)(call->words[1] >> 16), (int16_t)call->words[1]};
    if (kind > 2 || track.enable_state || !C_PtInRect(point, &track.bounds))
        return appearance_client_result(call, 0);

    Rect thumb, pages;
    bool horizontal;
    if (kind <= 1) {
        appearance_scroll_thumb scroll;
        if (!appearance_scroll_from_guest(&track, address, &ref, &scroll) || !scroll.show_thumb)
            return appearance_client_result(call, 0);
        thumb = scroll.thumb;
        pages = scroll.drag;
        horizontal = scroll.horizontal;
    }
    else {
        const uint8_t* direction = ROMlib_carbon_try_guest_memory(address + sizeof track, 1, 0);
        if (!direction)
            return appearance_client_result(call, 0);
        const appearance_slider_geometry slider = appearance_slider(&track, *direction);
        thumb = slider.thumb;
        pages = slider.drag;
        pages.left = CW(CW(pages.left) - 5);
        pages.right = CW(CW(pages.right) + 5);
        horizontal = slider.horizontal;
    }
    if (!horizontal)
        point = (Point){point.h, point.v};
    Rect previous = pages;
    previous.right = thumb.left;
    pages.left = thumb.right;
    if (C_PtInRect(point, &thumb))
        part = CWC(inThumb);
    else if (C_PtInRect(point, &previous))
        part = CWC(inPageUp);
    else if (C_PtInRect(point, &pages))
        part = CWC(inPageDown);
    memcpy(output, &part, sizeof part);
    return appearance_client_result(call, part != 0);
}

enum appearance_track_query { appearance_drag_rect, appearance_offset_position, appearance_region_position, appearance_live_value };

static bool appearance_region_bounds(uint32_t address, Rect* bounds)
{
    uint32_t data;
    if (!address || !cfp_get(address, &data) || !data)
        return false;
    const Region* region = ROMlib_carbon_try_guest_memory(data, offsetof(Region, rgnBBox) + sizeof *bounds, 0);
    if (!region || CW(region->rgnSize) < 10)
        return false;
    memcpy(bounds, &region->rgnBBox, sizeof *bounds);
    return true;
}

static int appearance_query_track(ROMlib_carbon_ppc_frame* call, enum appearance_track_query query, const char* name,
                                  uint32_t reference_offset)
{
    const unsigned output_word = query == appearance_drag_rect ? 1 : 2;
    const uint32_t address = call->words[0], output_address = call->words[output_word];
    const size_t output_size = query == appearance_drag_rect ? sizeof(Rect) : sizeof(uint32_t);
    const void* input = address ? ROMlib_carbon_try_guest_memory(address, sizeof(appearance_track_info), 0) : NULL;
    void* output = output_address ? ROMlib_carbon_try_guest_memory(output_address, output_size, 1) : NULL;
    if (!input || !output)
        return appearance_client_result(call, paramErr);

    appearance_track_info track;
    memcpy(&track, input, sizeof track);
    ROMlib_carbon_reference ref = {0};
    if (appearance_custom_engine(name, reference_offset, &ref))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    const uint16_t kind = CW(track.kind);
    Rect drag, thumb;
    bool horizontal, show_thumb = true;
    if (kind <= 1) {
        if (track.enable_state != 0 && query != appearance_drag_rect && ref.function)
            return ROMlib_carbon_invoke_frame(ref.function, call);
        appearance_scroll_thumb scroll;
        if (!appearance_scroll_from_guest(&track, address, &ref, &scroll))
            return appearance_client_result(call, paramErr);
        drag = scroll.drag;
        thumb = scroll.thumb;
        horizontal = scroll.horizontal;
        show_thumb = scroll.show_thumb;
    }
    else if (kind == 2) {
        const uint8_t* direction = ROMlib_carbon_try_guest_memory(address + sizeof track, 1, 0);
        if (!direction)
            return appearance_client_result(call, paramErr);
        const appearance_slider_geometry slider = appearance_slider(&track, *direction);
        drag = slider.drag;
        thumb = slider.thumb;
        horizontal = slider.horizontal;
    }
    else
        return ref.function ? ROMlib_carbon_invoke_frame(ref.function, call) : appearance_client_result(call, paramErr);

    if (query == appearance_drag_rect) {
        const Rect result = horizontal ? drag : appearance_transpose_rect(drag);
        memcpy(output, &result, sizeof result);
        return appearance_client_result(call, noErr);
    }

    const int travel = RECT_WIDTH(&drag) - RECT_WIDTH(&thumb);
    const int16_t relative = call->words[1];
    int32_t result;
    if (query == appearance_live_value) {
        const int32_t min = CL(track.min), max = CL(track.max);
        const int16_t extent = travel;
        const int32_t range = (uint32_t)max - (uint32_t)min;
        if (kind == 2) {
            int32_t delta = 0;
            if (relative > 0 && extent) {
                delta = relative >= extent ? range
                                           : appearance_pixel_round((float)relative / extent * range + 0.5);
            }
            result = CW(track.attributes) & 2 ? (uint32_t)max - (uint32_t)delta : (uint32_t)min + (uint32_t)delta;
        }
        else if (extent <= 0)
            result = CL(track.value);
        else if (relative <= 0)
            result = min;
        else if (relative >= extent)
            result = max;
        else
            result = (uint32_t)min + (uint32_t)appearance_pixel_round((float)relative / extent * range + 0.5);
    }
    else {
        Rect region = {0};
        if (query == appearance_region_position && !appearance_region_bounds(call->words[1], &region))
            return appearance_client_result(call, paramErr);
        int position = horizontal ? (int16_t)call->words[1] : (int16_t)(call->words[1] >> 16);
        if (query == appearance_region_position)
            position = horizontal ? CW(region.left) : CW(region.top);
        else
            position += CW(thumb.left);
        position -= CW(drag.left);
        if (kind <= 1 && !show_thumb)
            position = 0;
        result = position < 0 ? 0 : MIN(position, travel);
    }
    const uint32_t encoded = CL(result);
    memcpy(output, &encoded, sizeof encoded);
    return appearance_client_result(call, noErr);
}

static int carbon_GetThemeTrackDragRect(ROMlib_carbon_ppc_frame* call)
{
    return appearance_query_track(call, appearance_drag_rect, "GetThemeTrackDragRect", 0x3bf4);
}

static int carbon_GetThemeTrackThumbPositionFromOffset(ROMlib_carbon_ppc_frame* call)
{
    return appearance_query_track(call, appearance_offset_position, "GetThemeTrackThumbPositionFromOffset", 0x3c8c);
}

static int carbon_GetThemeTrackThumbPositionFromRegion(ROMlib_carbon_ppc_frame* call)
{
    return appearance_query_track(call, appearance_region_position, "GetThemeTrackThumbPositionFromRegion", 0x3d30);
}

static int carbon_GetThemeTrackLiveValue(ROMlib_carbon_ppc_frame* call)
{
    return appearance_query_track(call, appearance_live_value, "GetThemeTrackLiveValue", 0x3dd4);
}

enum appearance_scroll_arrow { appearance_start_outer, appearance_start_inner, appearance_end_inner, appearance_end_outer };

typedef struct {
    Rect track;
    Rect arrows[4];
    uint8_t present;
} appearance_scroll_geometry;

static appearance_scroll_geometry appearance_scroll_arrows(Rect bounds, bool horizontal, uint8_t enabled,
                                                           uint16_t style)
{
    appearance_scroll_geometry result = {.track = bounds};
    if (enabled == 1)
        return result;
    const Rect axis = horizontal ? bounds : appearance_transpose_rect(bounds);
    const int16_t thickness = RECT_HEIGHT(&axis), length = RECT_WIDTH(&axis);
    if (style == 2 && length < 160)
        style = 0;
    const int start = CW(axis.left), end = CW(axis.right);
    const int start_split = start + thickness - 1, end_split = end - thickness + 1;
    const int first[] = {start, start_split, end_split - thickness, end - thickness};
    const int last[] = {start + thickness, start_split + thickness, end_split, end};
    static const uint8_t buttons[] = {0x9, 0xc, 0xf};
    if (style >= NELEM(buttons))
        return result;
    result.present = buttons[style];
    Rect track = axis;
    if (style != 1)
        track.left = CW(style == 2 ? start_split + thickness - 1 : start_split);
    track.right = CW(style ? end_split - thickness + 1 : end_split);
    result.track = horizontal ? track : appearance_transpose_rect(track);
    for (unsigned i = 0; i < NELEM(result.arrows); ++i) {
        const Rect arrow = {axis.top, CW(first[i]), axis.bottom, CW(last[i])};
        result.arrows[i] = horizontal ? arrow : appearance_transpose_rect(arrow);
    }
    return result;
}

static int appearance_scroll_arrows_entry(ROMlib_carbon_ppc_frame* call, bool hit_test)
{
    const uint32_t source_address = call->words[0], track_address = call->words[hit_test ? 5 : 4];
    const void* source = source_address ? ROMlib_carbon_try_guest_memory(source_address, sizeof(Rect), 0) : NULL;
    void* output = track_address ? ROMlib_carbon_try_guest_memory(track_address, sizeof(Rect), 1) : NULL;
    const uint32_t part_address = call->words[6];
    void* part_output =
        hit_test && part_address ? ROMlib_carbon_try_guest_memory(part_address, sizeof(INTEGER), 1) : NULL;
    if (!source || !output || (hit_test && !part_output))
        return appearance_client_result(call, hit_test ? 0 : paramErr);
    const char* name = hit_test ? "HitTestThemeScrollBarArrows" : "GetThemeScrollBarTrackRect";
    ROMlib_carbon_reference ref = {0};
    if (appearance_custom_engine(name, hit_test ? 0x46b0 : 0x45f4, &ref))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    Rect bounds;
    memcpy(&bounds, source, sizeof bounds);
    appearance_scroll_geometry layout = appearance_scroll_arrows(bounds, (uint8_t)call->words[3], (uint8_t)call->words[1],
                                                                 appearance_platinum_preference(&ref, appearance_arrow_style_field));
    memcpy(output, &layout.track, sizeof layout.track);
    if (!hit_test)
        return appearance_client_result(call, noErr);

    const Point point = {(int16_t)(call->words[4] >> 16), (int16_t)call->words[4]};
    static const unsigned hit_order[] = {appearance_start_outer, appearance_end_inner, appearance_end_outer,
                                        appearance_start_inner};
    INTEGER code = 0;
    for (unsigned i = 0; i < NELEM(hit_order); ++i) {
        const unsigned button = hit_order[i];
        if ((layout.present & (1 << button)) && C_PtInRect(point, &layout.arrows[button])) {
            code = CW(i < 2 ? inUpButton : inDownButton);
            break;
        }
    }
    memcpy(part_output, &code, sizeof code);
    return appearance_client_result(call, code != 0);
}

static int carbon_GetThemeScrollBarTrackRect(ROMlib_carbon_ppc_frame* call)
{
    return appearance_scroll_arrows_entry(call, false);
}

static int carbon_HitTestThemeScrollBarArrows(ROMlib_carbon_ppc_frame* call)
{
    return appearance_scroll_arrows_entry(call, true);
}

static int carbon_GetThemeMenuTitleOffset(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (appearance_custom_engine("GetThemeMenuTitleOffset", 0x2738, &ref))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    return appearance_client_result(call, 0);
}

static int carbon_GetThemeMenuTitleExtra(ROMlib_carbon_ppc_frame* call)
{
    bool patched;
    if (!appearance_dispatch_patch(&patched))
        return 0;
    if (patched)
        return appearance_dispatch(0x7b8, 0x21, call, call);
    void* output = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], sizeof(INTEGER), 1) : NULL;
    if (!output)
        return appearance_client_result(call, paramErr);
    ROMlib_carbon_reference ref;
    if (appearance_custom_engine("GetThemeMenuTitleExtra", 0x392cc, &ref))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    const INTEGER width = CW((uint8_t)call->words[1] ? 9 : 13);
    memcpy(output, &width, sizeof width);
    return appearance_client_result(call, noErr);
}

static int carbon_GetThemeMenuBarShape(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (appearance_custom_engine("GetThemeMenuBarShape", 0x53b8, &ref))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    return appearance_client_result(call, 0); /* The Platinum menu bar uses the rounded shape. */
}

static OSErr appearance_menu_background_region(Rect* rect, RgnHandle output)
{
    RgnHandle shadow = NewRgn();
    if (!shadow)
        return memFullErr;
    RectRgn(output, rect);
    InsetRgn(output, -1, -1);
    CopyRgn(output, shadow);
    OffsetRgn(shadow, 1, 1);
    UnionRgn(output, shadow, output);
    DisposeRgn(shadow);
    return noErr;
}

static int carbon_GetThemeMenuBackgroundRegion(ROMlib_carbon_ppc_frame* call)
{
    bool patched;
    if (!appearance_dispatch_patch(&patched))
        return 0;
    if (patched)
        return appearance_dispatch(0x3bb8, 0x1c, call, call);
    const void* bounds = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], sizeof(Rect), 0) : NULL;
    uint32_t extent;
    if (!bounds || !cfp_handle_data(call->words[2], &extent, 1) || extent < sizeof(Region))
        return appearance_client_result(call, paramErr);
    ROMlib_carbon_reference ref;
    if (appearance_custom_engine("GetThemeMenuBackgroundRegion", 0x39150, &ref))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    Rect rect;
    memcpy(&rect, bounds, sizeof rect);
    RgnHandle output = (RgnHandle)SYN68K_TO_US(call->words[2]);
    return appearance_client_result(call, appearance_menu_background_region(&rect, output));
}

static void appearance_draw_menu_background(INTEGER depth, INTEGER flags, GDHandle device, void* context)
{
    (void)device;
    const Rect* rect = context;
    const int top = CW(rect->top), left = CW(rect->left), bottom = CW(rect->bottom), right = CW(rect->right);
    const bool uses_gray = appearance_platinum_uses_gray(depth, (flags & 1) != 0);
    draw_state_t saved;
    draw_state_save(&saved);
    PenNormal();
    const uint16_t shade = uses_gray ? CWC(0xdddd) : CWC(0xffff);
    const RGBColor paper = {shade, shade, shade};
    RGBBackColor(&paper);
    EraseRect(rect);
    if (uses_gray) {
        appearance_foreground_shade(0xffff);
        MoveTo(left, bottom - 2);
        LineTo(left, top);
        LineTo(right - 2, top);
        appearance_foreground_shade(0x9999);
        Move(1, 1);
        LineTo(right - 1, bottom - 1);
        LineTo(left + 1, bottom - 1);
    }
    ForeColor(blackColor);
    MoveTo(left - 1, top);
    LineTo(left - 1, bottom - 1);
    LineTo(right, bottom);
    LineTo(right, top - 1);
    LineTo(left - 1, top - 1);
    if (uses_gray)
        appearance_foreground_shade(0x2222);
    MoveTo(left + 1, bottom + 1);
    LineTo(right + 1, bottom + 1);
    LineTo(right + 1, top + 1);
    draw_state_restore(&saved);
}

static int carbon_DrawThemeMenuBackground(ROMlib_carbon_ppc_frame* call)
{
    bool patched;
    if (!appearance_dispatch_patch(&patched))
        return 0;
    if (patched)
        return appearance_dispatch(0xbb8, 0x1b, call, call);
    const void* bounds = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], sizeof(Rect), 0) : NULL;
    if (!bounds || !thePort || !ROMlib_valid_qd_port(thePort))
        return appearance_client_result(call, paramErr);
    ROMlib_carbon_reference ref;
    if (appearance_custom_engine("DrawThemeMenuBackground", 0x39110, &ref))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    Rect rect;
    memcpy(&rect, bounds, sizeof rect);
    if (EmptyRect(&rect))
        return appearance_client_result(call, noErr);
    RgnHandle area = NewRgn();
    if (!area)
        return appearance_client_result(call, memFullErr);
    OSErr error = appearance_menu_background_region(&rect, area);
    if (!error)
        error = appearance_draw_clipped(area, appearance_draw_menu_background, &rect);
    DisposeRgn(area);
    return appearance_client_result(call, error);
}

static int appearance_GetThemeMinimumDepth(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (appearance_custom_engine("GetThemeMinimumDepth", 0x3284, &ref))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    return appearance_client_result(call, 1);
}

static int appearance_GetThemeRecommendedDepth(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (appearance_custom_engine("GetThemeRecommendedDepth", 0x32b8, &ref))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    return appearance_client_result(call, 8);
}

static int appearance_theme_names(const ROMlib_carbon_reference* ref, uint32_t theme, uint32_t variant)
{
    uint32_t query[] = {0, theme, variant};
    return appearance_engine_call(ref, 0x128, query, NELEM(query), NULL);
}

static int appearance_get_color(ROMlib_carbon_ppc_frame* call, bool brush)
{
    const uint32_t address = call->words[3];
    void* output = address ? ROMlib_carbon_try_guest_memory(address, sizeof(RGBColor), 1) : NULL;
    if (!output)
        return appearance_client_result(call, paramErr);
    const int16_t color = call->words[0];
    if (color == -1 || color == -2) {
        memset(output, color == -2 ? 0xff : 0, sizeof(RGBColor));
        return appearance_client_result(call, noErr);
    }
    if (color < 1 || color > (brush ? 48 : 47))
        return appearance_client_result(call, brush ? -30560 : -30563);
    ROMlib_carbon_reference ref;
    const char* name = brush ? "GetThemeBrushAsColor" : "GetThemeTextColor";
    if (!ROMlib_carbon_load_reference("AppearanceLib", name, &ref))
        return 0;
    if (!appearance_reference_layout(&ref, brush ? 0x2b88 : 0x2c64))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    uint32_t args[] = {0, (uint32_t)(int32_t)color, (uint32_t)(int32_t)(int16_t)call->words[1], (uint8_t)call->words[2],
                       address};
    uint32_t status;
    return appearance_engine_call(&ref, brush ? 0x2c : 0x30, args, NELEM(args), &status) &&
           appearance_client_result(call, status);
}

static int carbon_GetThemeTextColor(ROMlib_carbon_ppc_frame* call)
{
    return appearance_get_color(call, false);
}

static int appearance_GetThemeBrushAsColor(ROMlib_carbon_ppc_frame* call)
{
    return appearance_get_color(call, true);
}

static int carbon_GetThemeBrushAsColor(ROMlib_carbon_ppc_frame* call)
{
    const int16_t brush = call->words[0];
    if (brush >= 49 && brush <= 51)
        call->words[0] = 1;
    return appearance_GetThemeBrushAsColor(call);
}

static int appearance_window_color(const char* name, uint32_t offset, bool text, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0];
    if (!window && text)
        return appearance_client_result(call, paramErr);
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_load_reference("AppearanceLib", name, &ref))
        return 0;
    if (!appearance_reference_layout(&ref, offset))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    _Alignas(8) uint16_t encoded = 0;
    const uint32_t lookup[] = {window, TICK("wthm"), sizeof encoded, 0, US_TO_SYN68K(&encoded)};
    uint32_t status;
    if (!cfp_private(&ref, 0x3d8d8, lookup, NELEM(lookup), &status))
        return 0;
    if (status)
        return appearance_client_result(call, text ? status : noErr);
    const int16_t brush = CW(encoded);
    uint32_t color = brush;
    if (text) {
        color = brush == 48 ? 47 : 0;
        if (brush >= 1 && brush <= 6)
            color = 2 * ((brush - 1) / 2) + ((uint8_t)call->words[1] ? 1 : 2);
        if (!color)
            return appearance_client_result(call, -30568);
    }
    ROMlib_carbon_ppc_frame arguments = {
        .words = {color, (uint32_t)(int32_t)(int16_t)call->words[text ? 2 : 1], (uint8_t)call->words[text ? 3 : 2]}};
    bool patched;
    if (!appearance_dispatch_patch(&patched))
        return 0;
    const int ok =
        patched ? appearance_dispatch(0x1ab8, text ? 3 : 2, call, &arguments)
                : ROMlib_carbon_call_romaw_service(text ? "SetThemeTextColor" : "SetThemeBackground", &arguments);
    return ok && appearance_client_result(call, noErr);
}

static int carbon_SetThemeTextColorForWindow(ROMlib_carbon_ppc_frame* call)
{
    return appearance_window_color("SetThemeTextColorForWindow", 0x3700, true, call);
}

static int carbon_SetThemeBackgroundForWindow(ROMlib_carbon_ppc_frame* call)
{
    return appearance_window_color("SetThemeBackgroundForWindow", 0x3830, false, call);
}

static int appearance_engine_boolean(const char* name, uint32_t offset, uint32_t slot, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_load_reference("AppearanceLib", name, &ref))
        return 0;
    if (!appearance_reference_layout(&ref, offset))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    uint32_t object, result;
    return appearance_engine_call(&ref, slot, &object, 1, &result) && appearance_client_result(call, (uint8_t)result);
}

static int carbon_CanThemeDrawOnAllMonitors(ROMlib_carbon_ppc_frame* call)
{
    return appearance_engine_boolean("CanThemeDrawOnAllMonitors", 0x32ec, 0x138, call);
}

static int carbon_IsThemeCreditDialogAvailable(ROMlib_carbon_ppc_frame* call)
{
    return appearance_engine_boolean("IsThemeCreditDialogAvailable", 0x50cc, 0x140, call);
}

static void appearance_update_port_highlights(const RGBColor* previous, const RGBColor* color)
{
    uint32_t size;
    const Byte* table = cfp_handle_data(CL((uint32_t)HIDDEN_RAW(ROMlib_qd_port_list_H)), &size, 0);
    uint16_t count;
    if (!table || size < sizeof count)
        return;
    memcpy(&count, table, sizeof count);
    const unsigned ports = MIN((unsigned)CW(count), (size - sizeof count) / sizeof(uint32_t));
    for (unsigned i = 0; i < ports; ++i) {
        uint32_t address;
        memcpy(&address, table + sizeof count + i * sizeof address, sizeof address);
        CGrafPtr port = address ? ROMlib_carbon_try_guest_memory(CL(address), sizeof(CGrafPort), 0) : NULL;
        if (!port || !CGrafPort_p(port))
            continue;
        uint32_t extent;
        GrafVars* vars = cfp_handle_data(CL((uint32_t)PP_RAW(CPORT_GRAFVARS_X(port))), &extent, 1);
        if (vars && extent >= sizeof(RGBColor) * 2 && !memcmp(previous, &vars->rgbHiliteColor, sizeof *previous))
            vars->rgbHiliteColor = *color;
    }
}

static int carbon_SetHighlightColor(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t address = call->words[0];
    const RGBColor* color = address ? ROMlib_carbon_try_guest_memory(address, sizeof *color, 0) : NULL;
    if (!color)
        return appearance_client_result(call, paramErr);
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_load_reference("AppearanceLib", "SetHighlightColor", &ref))
        return 0;
    if (!appearance_reference_layout(&ref, 0x527c))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    _Alignas(8) RGBColor previous;
    const uint32_t output = US_TO_SYN68K(&previous);
    const uint32_t persist[] = {address, 0x60082};
    if (!cfp_private(&ref, 0x3c720, &output, 1, NULL) || !cfp_private(&ref, 0x3c738, &address, 1, NULL) ||
        !cfp_private(&ref, 0x3c750, &address, 1, NULL) || !cfp_private(&ref, 0x5794, persist, NELEM(persist), NULL) ||
        !cfp_private(&ref, 0x57d4, NULL, 0, NULL))
        return 0;
    appearance_update_port_highlights(&previous, color);
    if ((uint8_t)call->words[1]) {
        RgnHandle desktop = MR(GrayRgn);
        if (desktop)
            C_PaintBehind(NULL, desktop);
    }
    const uint32_t broadcast[] = {TICK("hcol"), 0};
    const uint32_t setting[] = {1, TICK("hili"), 0, sizeof *color, address};
    uint32_t status;
    return cfp_private(&ref, 0x3c198, broadcast, NELEM(broadcast), NULL) &&
           cfp_private(&ref, 0x5eb4, setting, NELEM(setting), &status) && appearance_client_result(call, status);
}

static int carbon_GetAppearance(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t theme = call->words[0], variant = call->words[1];
    if (!theme && !variant)
        return appearance_client_result(call, paramErr);
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_load_reference("AppearanceLib", "GetAppearance", &ref))
        return 0;
    if (!appearance_reference_layout(&ref, 0x4f44))
        return ROMlib_carbon_invoke_frame(ref.function, call);

    _Alignas(256) Str255 names[2] = {0};
    if (!appearance_theme_names(&ref, theme ? US_TO_SYN68K(names[0]) : 0, variant ? US_TO_SYN68K(names[1]) : 0))
        return 0;
    void* theme_output = theme ? ROMlib_carbon_try_guest_memory(theme, names[0][0] + 1, 1) : NULL;
    void* variant_output = variant ? ROMlib_carbon_try_guest_memory(variant, names[1][0] + 1, 1) : NULL;
    if ((theme && !theme_output) || (variant && !variant_output))
        return appearance_client_result(call, paramErr);
    if (theme_output)
        memcpy(theme_output, names[0], names[0][0] + 1);
    if (variant_output)
        memcpy(variant_output, names[1], names[1][0] + 1);
    return appearance_client_result(call, noErr);
}

static int carbon_SetAppearance(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t theme = call->words[0], variant = call->words[1];
    if (!appearance_pascal_string(theme) || (variant && !appearance_pascal_string(variant)))
        return appearance_client_result(call, paramErr);
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_load_reference("AppearanceLib", "SetAppearance", &ref))
        return 0;
    if (!appearance_reference_layout(&ref, 0x4fac))
        return ROMlib_carbon_invoke_frame(ref.function, call);

    _Alignas(8) FSSpec file = {0};
    uint32_t status;
    const uint32_t spec = US_TO_SYN68K(&file);
    const uint32_t locate[] = {theme, spec};
    if (!cfp_private(&ref, 0x7c68, locate, NELEM(locate), &status))
        return 0;
    if ((int16_t)status)
        return appearance_client_result(call, (int16_t)status);

    _Alignas(256) Str255 current[2] = {0};
    const uint32_t current_theme = US_TO_SYN68K(current[0]), current_variant = US_TO_SYN68K(current[1]);
    if (!appearance_theme_names(&ref, current_theme, current_variant))
        return 0;
    const uint32_t compare_theme[] = {current_theme, US_TO_SYN68K(file.name), 0, 1};
    const uint32_t compare_variant[] = {current_variant, variant, 0, 1};
    uint32_t same_theme, same_variant = 1, dirty;
    const Byte* requested_variant = variant ? ROMlib_carbon_try_guest_memory(variant, 1, 0) : NULL;
    if (!cfp_private(&ref, 0x3c318, compare_theme, NELEM(compare_theme), &same_theme) ||
        (requested_variant && *requested_variant &&
         !cfp_private(&ref, 0x3c318, compare_variant, NELEM(compare_variant), &same_variant)))
        return 0;
    if ((uint8_t)same_theme && (uint8_t)same_variant) {
        if (!cfp_private(&ref, 0x19248, NULL, 0, &dirty))
            return 0;
        if (!(uint8_t)dirty)
            return appearance_client_result(call, noErr);
    }
    const uint32_t install[] = {spec, variant, call->words[2], 1};
    return cfp_private(&ref, 0x18c60, install, NELEM(install), NULL) && appearance_client_result(call, noErr);
}

static int appearance_client(const char* name, uint32_t offset, enum appearance_client_operation operation,
                             ROMlib_carbon_ppc_frame* call)
{
    enum { already_registered = -30561, not_registered = -30562, process_ref_size = 8, count_size = 2 };

    const int query = operation == appearance_client_query;
    const int add = operation == appearance_client_register;
    if (query && (!call->words[0] || !ROMlib_carbon_try_guest_memory(call->words[0], process_ref_size, 0)))
        return appearance_client_result(call, 0);

    ROMlib_carbon_reference reference;
    if (!ROMlib_carbon_load_reference("AppearanceLib", name, &reference))
        return query ? appearance_client_result(call, 0) : ROMlib_carbon_call_romaw_service(name, call);
    if (!appearance_reference_layout(&reference, offset))
        return ROMlib_carbon_invoke_frame(reference.function, call);

    /* Keep the saved AppearanceLib client table authoritative for mixed callers. */
    _Alignas(8) Byte process[process_ref_size] = {0};
    const uint32_t process_address = US_TO_SYN68K(process);
    if (query) {
        uint32_t converted;
        if (!cfp_private(&reference, 0x3bec8, call->words, 1, &converted))
            return 0;
        const void* stored = converted ? ROMlib_carbon_try_guest_memory(converted, sizeof process, 0) : NULL;
        if (!stored)
            return appearance_client_result(call, 0);
        memcpy(process, stored, sizeof process);
    }
    else if (!cfp_private(&reference, 0x3bdf0, &process_address, 1, NULL))
        return 0;

    const uint32_t slot = US_TO_SYN68K(reference.toc) - 0x75b8;
    uint32_t table, extent = 0;
    if (!cfp_get(slot, &table))
        return 0;
    Byte* entries = NULL;
    uint16_t encoded_count = 0;
    if (table) {
        entries = cfp_handle_data(table, &extent, !query);
        if (!entries || extent < count_size)
            return ROMlib_carbon_invoke_frame(reference.function, call);
        memcpy(&encoded_count, entries, count_size);
    }
    const unsigned count = CW(encoded_count);
    if (count > INT16_MAX || (table && count > (extent - count_size) / process_ref_size))
        return ROMlib_carbon_invoke_frame(reference.function, call);

    unsigned index = 0;
    for (; index < count; ++index) {
        const uint32_t args[] = {US_TO_SYN68K(entries + count_size + index * process_ref_size), process_address};
        uint32_t same;
        if (!cfp_private(&reference, 0x3be20, args, NELEM(args), &same))
            return 0;
        if ((uint8_t)same)
            break;
    }
    if (query)
        return appearance_client_result(call, index != count);
    if (add && index != count)
        return appearance_client_result(call, already_registered);
    if (!add && index == count)
        return appearance_client_result(call, not_registered);

    if (add) {
        if (count == INT16_MAX)
            return appearance_client_result(call, memFullErr);
        const Size size = count_size + (count + 1) * process_ref_size;
        Handle handle = table ? (Handle)SYN68K_TO_US(table) : NewHandleSys(size);
        if (table && extent < (uint32_t)size)
            SetHandleSize(handle, size);
        const OSErr status = MemError();
        if (!handle || (extent < (uint32_t)size && status))
            return appearance_client_result(call, status ? status : memFullErr);
        entries = (Byte*)STARH(handle);
        memcpy(entries + count_size + count * process_ref_size, process, sizeof process);
        encoded_count = CW(count + 1);
        memcpy(entries, &encoded_count, count_size);
        if (!table && !cfp_write(slot, US_TO_SYN68K(handle))) {
            DisposHandle(handle);
            return 0;
        }
    }
    else {
        Byte* record = entries + count_size + index * process_ref_size;
        memmove(record, record + process_ref_size, (count - index - 1) * process_ref_size);
        encoded_count = CW(count - 1);
        memcpy(entries, &encoded_count, count_size);
        SetHandleSize((Handle)SYN68K_TO_US(table), count_size + (count - 1) * process_ref_size);
    }
    return appearance_client_result(call, noErr);
}

static int carbon_RegisterAppearanceClient(ROMlib_carbon_ppc_frame* call)
{
    return appearance_client("RegisterAppearanceClient", 0x6d08, appearance_client_register, call);
}

static int carbon_UnregisterAppearanceClient(ROMlib_carbon_ppc_frame* call)
{
    return appearance_client("UnregisterAppearanceClient", 0x6e1c, appearance_client_unregister, call);
}

static int carbon_IsAppearanceClient(ROMlib_carbon_ppc_frame* call)
{
    return appearance_client("IsAppearanceClient", 0x6f34, appearance_client_query, call);
}

static int appearance_get_preference(const char* name, uint32_t offset, int32_t field, unsigned width,
                                     ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference reference;
    if (!ROMlib_carbon_load_reference("AppearanceLib", name, &reference)) {
        if (field != appearance_arrow_style_field)
            return appearance_client_result(call, width == 1 ? 0 : appearance_unimplemented);
        const uint16_t encoded = CW(appearance_arrow_style);
        void* output = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], sizeof encoded, 1) : NULL;
        if (!output)
            return appearance_client_result(call, paramErr);
        memcpy(output, &encoded, sizeof encoded);
        return appearance_client_result(call, noErr);
    }
    if (!appearance_reference_layout(&reference, offset))
        return ROMlib_carbon_invoke_frame(reference.function, call);

    const Byte* stored = ROMlib_carbon_guest_memory(US_TO_SYN68K(reference.toc) + field, width, 0);
    if (!stored)
        return 0;
    if (width == 1)
        return appearance_client_result(call, *stored);
    void* output = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], width, 1) : NULL;
    if (!output)
        return appearance_client_result(call, paramErr);
    memcpy(output, stored, width);
    return appearance_client_result(call, noErr);
}

static int appearance_set_preference(const char* name, uint32_t offset, int32_t field, uint32_t tag,
                                     uint32_t style_limit, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference reference;
    if (!ROMlib_carbon_load_reference("AppearanceLib", name, &reference)) {
        if (field != appearance_arrow_style_field)
            return appearance_client_result(call, appearance_unimplemented);
        const uint16_t style = call->words[0];
        if (style > style_limit)
            return appearance_client_result(call, paramErr);
        appearance_arrow_style = style;
        if ((uint8_t)call->words[1] && GrayRgn)
            PaintBehind(NULL, MR(GrayRgn));
        return appearance_client_result(call, noErr);
    }
    if (!appearance_reference_layout(&reference, offset))
        return ROMlib_carbon_invoke_frame(reference.function, call);

    const unsigned width = style_limit ? sizeof(uint16_t) : sizeof(uint32_t);
    const uint32_t address = US_TO_SYN68K(reference.toc) + field;
    Byte* stored = ROMlib_carbon_guest_memory(address, width, 1);
    if (!stored)
        return 0;
    if (style_limit) {
        const uint16_t style = call->words[0];
        if (style > style_limit)
            return appearance_client_result(call, paramErr);
        const uint16_t encoded = CW(style);
        memcpy(stored, &encoded, width);
    }
    else {
        const uint32_t encoded = CL(call->words[0]);
        memcpy(stored, &encoded, width);
    }
    if (tag) {
        const uint32_t arguments[] = {1, tag, 0, width, address};
        if (!cfp_private(&reference, 0x5eb4, arguments, NELEM(arguments), NULL))
            return 0;
    }
    if (style_limit && (uint8_t)call->words[1]) {
        const uint32_t arguments[] = {0, cfp_read(0x9ee)};
        if (!cfp_private(&reference, 0x3c1b0, arguments, NELEM(arguments), NULL))
            return 0;
    }
    return appearance_client_result(call, noErr);
}

enum {
    sound_mask_field = 0x4464,
    sound_playlist_field = 0x449c,
    sound_file_field = 0x459c,
    sounds_enabled_field = 0x45a2,
    sound_ready_field = 0x45a3,
};

static int appearance_publish_sound_setting(const ROMlib_carbon_reference* ref, uint32_t tag, uint32_t length,
                                            uint32_t data)
{
    const uint32_t arguments[] = {1, tag, 0, length, data};
    return cfp_private(ref, 0x5eb4, arguments, NELEM(arguments), NULL);
}

static int appearance_get_playlist(const ROMlib_carbon_reference* ref, uint32_t target, uint32_t* result)
{
    const uint32_t toc = US_TO_SYN68K(ref->toc);
    const Byte* stored = ROMlib_carbon_guest_memory(toc + sound_playlist_field, sizeof(Str255), 0);
    Byte* output = target && stored ? ROMlib_carbon_try_guest_memory(target, stored[0] + 1, 1) : NULL;
    if (!stored)
        return 0;
    if (!output) {
        *result = paramErr;
        return 1;
    }
    memmove(output, stored, stored[0] + 1);
    if (!output[0]) {
        *result = -30569; /* No sound set selected. */
        return 1;
    }

    _Alignas(128) struct {
        int16_t volume;
        uint32_t directory;
        FSSpec spec;
        FInfo info;
    } file;

    const uint32_t folder_args[] = {(uint32_t)-32768, TICK("snds"), 1, US_TO_SYN68K(&file.volume),
                                    US_TO_SYN68K(&file.directory)};
    if (!cfp_private(ref, 0x3bf58, folder_args, NELEM(folder_args), result))
        return 0;
    if (!(int16_t)*result) {
        const uint32_t spec_args[] = {CW(file.volume), CL(file.directory), target, US_TO_SYN68K(&file.spec)};
        if (!cfp_private(ref, 0x3c1e0, spec_args, NELEM(spec_args), result))
            return 0;
        if (!(int16_t)*result) {
            const uint32_t info_args[] = {US_TO_SYN68K(&file.spec), US_TO_SYN68K(&file.info)};
            if (!cfp_private(ref, 0x3bf88, info_args, NELEM(info_args), result))
                return 0;
            if (!(int16_t)*result && CL(file.info.fdType) != TICK("tsnd"))
                *result = -30570; /* The selected file is of the wrong type. */
        }
    }
    *result = (int16_t)*result;
    return 1;
}

static int carbon_GetThemePlaylist(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_load_reference("AppearanceLib", "GetThemePlaylist", &ref))
        return 0;
    if (!appearance_reference_layout(&ref, 0x16ef4))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    return appearance_get_playlist(&ref, call->words[0], &call->words[0]);
}

static int carbon_SetThemePlaylist(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_load_reference("AppearanceLib", "SetThemePlaylist", &ref))
        return 0;
    if (!appearance_reference_layout(&ref, 0x16e58))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    const uint32_t input = call->words[0];
    const Byte* first = input ? ROMlib_carbon_try_guest_memory(input, 1, 0) : NULL;
    const uint32_t length = first ? *first + 1 : 0;
    const Byte* source = length ? ROMlib_carbon_try_guest_memory(input, length, 0) : NULL;
    if (!source)
        return appearance_client_result(call, paramErr);

    const uint32_t toc = US_TO_SYN68K(ref.toc);
    uint32_t result;
    const Byte* enabled = ROMlib_carbon_guest_memory(toc + sounds_enabled_field, 1, 0);
    uint32_t mask;
    if (!enabled || !cfp_private(&ref, 0x16bd0, &input, 1, &result) || !cfp_get(toc + sound_mask_field, &mask))
        return 0;
    if ((!*enabled || !mask) && !cfp_private(&ref, 0x169f8, NULL, 0, NULL))
        return 0;
    if (!result && (!appearance_publish_sound_setting(&ref, TICK("tsnd"), length, input) ||
                    !cfp_move(toc + sound_playlist_field, input, length)))
        return 0;
    return appearance_client_result(call, result);
}

static int appearance_update_sound(const ROMlib_carbon_reference* ref, uint32_t toc, bool start)
{
    if (!start)
        return cfp_private(ref, 0x1639c, NULL, 0, NULL) && cfp_private(ref, 0x169f8, NULL, 0, NULL) &&
               cfp_private(ref, 0x165e0, NULL, 0, NULL);
    const Byte* ready = ROMlib_carbon_guest_memory(toc + sound_ready_field, 1, 0);
    if (!ready || (!*ready && !cfp_private(ref, 0x16564, NULL, 0, NULL)))
        return 0;
    _Alignas(256) Str255 playlist;
    const uint32_t address = US_TO_SYN68K(playlist);
    uint32_t ignored;
    if (!appearance_get_playlist(ref, address, &ignored))
        return 0;
    const int16_t* file = ROMlib_carbon_guest_memory(toc + sound_file_field, sizeof *file, 0);
    return file && (CW(*file) != -1 || !playlist[0] || cfp_private(ref, 0x16bd0, &address, 1, NULL));
}

static int appearance_set_sound(const char* name, uint32_t offset, bool toggle, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_load_reference("AppearanceLib", name, &ref))
        return 0;
    if (!appearance_reference_layout(&ref, offset))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    const uint32_t toc = US_TO_SYN68K(ref.toc);
    Byte* enabled = ROMlib_carbon_guest_memory(toc + sounds_enabled_field, 1, toggle);
    if (!enabled)
        return 0;

    const uint32_t setting = call->words[0];
    if (toggle) {
        *enabled = setting;
        if (!appearance_update_sound(&ref, toc, *enabled != 0) ||
            !appearance_publish_sound_setting(&ref, TICK("snd?"), 1, toc + sounds_enabled_field))
            return 0;
    }
    else {
        const int16_t* file = ROMlib_carbon_guest_memory(toc + sound_file_field, sizeof *file, 0);
        if (!file || ((setting != 0) == (CW(*file) == -1) && !appearance_update_sound(&ref, toc, setting && *enabled)))
            return 0;
        if (!cfp_write(toc + sound_mask_field, setting) ||
            !appearance_publish_sound_setting(&ref, TICK("smsk"), 4, toc + sound_mask_field))
            return 0;
    }
    return appearance_client_result(call, noErr);
}

static int carbon_SetThemeSoundsEnabled(ROMlib_carbon_ppc_frame* call)
{
    return appearance_set_sound("SetThemeSoundsEnabled", 0x1682c, true, call);
}

static int carbon_SetThemeSoundMask(ROMlib_carbon_ppc_frame* call)
{
    return appearance_set_sound("SetThemeSoundMask", 0x166f8, false, call);
}

static int appearance_play_sound(const char* name, uint32_t offset, uint32_t looping, uint32_t control,
                                 ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_load_reference("AppearanceLib", name, &ref))
        return appearance_client_result(call, call->words[0] ? appearance_unimplemented : noErr);
    if (!appearance_reference_layout(&ref, offset))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    uint32_t mask, status = noErr;
    if (!cfp_get(US_TO_SYN68K(ref.toc) + sound_mask_field, &mask))
        return 0;
    const uint32_t playback[] = {call->words[0], looping, control};
    return (!mask || cfp_private(&ref, 0x15eb4, playback, NELEM(playback), &status)) &&
           appearance_client_result(call, status);
}

static int carbon_PlayThemeSound(ROMlib_carbon_ppc_frame* call)
{
    return appearance_play_sound("PlayThemeSound", 0x161f0, 0, 0, call);
}

static int carbon_PlayThemeControlSound(ROMlib_carbon_ppc_frame* call)
{
    return appearance_play_sound("PlayThemeControlSound", 0x16230, 0, call->words[1], call);
}

static int carbon_BeginThemeDragSound(ROMlib_carbon_ppc_frame* call)
{
    return appearance_play_sound("BeginThemeDragSound", 0x16270, 1, 0, call);
}

static int carbon_BeginThemeDragIndicatorSound(ROMlib_carbon_ppc_frame* call)
{
    return appearance_play_sound("BeginThemeDragIndicatorSound", 0x162b0, 1, call->words[1], call);
}

static int carbon_EndThemeDragSound(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_load_reference("AppearanceLib", "EndThemeDragSound", &ref))
        return appearance_client_result(call, appearance_unimplemented);
    if (!appearance_reference_layout(&ref, 0x162f0))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    const uint32_t slot = US_TO_SYN68K(ref.toc) + sound_mask_field + 0x10;
    uint32_t playback;
    if (!cfp_get(slot, &playback))
        return 0;
    if (!playback)
        return appearance_client_result(call, noErr);

    /* Reap completed channels first. This can also clear the current drag channel. */
    if (!cfp_private(&ref, 0x15be4, NULL, 0, NULL) || !cfp_get(slot, &playback))
        return 0;
    if (playback) {
        const Byte* finishing = ROMlib_carbon_guest_memory(playback + 0x3d, 1, 0);
        uint32_t channel;
        if (!finishing || !cfp_get(playback, &channel))
            return 0;
        if (!*finishing) {
            _Alignas(8) SndCommand flush = {CWC(flushCmd), 0, 0};
            _Alignas(8) SndCommand complete = {CWC(callBackCmd), CWC(5), CL(playback)};
            const uint32_t immediate[] = {channel, US_TO_SYN68K(&flush)};
            const uint32_t enqueue[] = {channel, US_TO_SYN68K(&complete), 1};
            if (!cfp_private(&ref, 0x3be38, immediate, NELEM(immediate), NULL) ||
                !cfp_private(&ref, 0x3be68, enqueue, NELEM(enqueue), NULL))
                return 0;
        }
    }
    return appearance_client_result(call, noErr);
}

static int scenario_add(const ROMlib_carbon_reference* ref, uint32_t collection, uint32_t tag, uint32_t size,
                        const void* data)
{
    const uint32_t args[] = {collection, tag, 0, size, US_TO_SYN68K(data)};
    return cfp_private(ref, 0x3c570, args, NELEM(args), NULL);
}

static int scenario_add_handle(const ROMlib_carbon_reference* ref, uint32_t collection, uint32_t tag, uint32_t handle)
{
    const uint32_t args[] = {collection, tag, 0, handle};
    const int added = cfp_private(ref, 0x3da40, args, NELEM(args), NULL);
    return cfp_private(ref, 0x3be98, &handle, 1, NULL) && added;
}

static int scenario_save_desktop(const ROMlib_carbon_reference* ref, uint32_t collection)
{
    _Alignas(256) Str255 name = {0};
    _Alignas(8) uint32_t pattern = 0, alignment = 0, alias = 0;
    uint32_t status, device;
    const uint32_t get_pattern[] = {US_TO_SYN68K(&pattern), US_TO_SYN68K(name)};
    if (!cfp_private(ref, 0x3da28, get_pattern, NELEM(get_pattern), &status))
        return 0;
    if (!status) {
        const int named = !name[0] || scenario_add(ref, collection, TICK("patn"), name[0] + 1, name);
        if (!scenario_add_handle(ref, collection, TICK("patt"), CL(pattern)) || !named)
            return 0;
    }
    _Alignas(8) FSSpec picture;
    if (!cfp_private(ref, 0x3bfb8, NULL, 0, &device))
        return 0;
    const uint32_t get_picture[] = {device, US_TO_SYN68K(&picture), US_TO_SYN68K(&alignment)};
    if (!cfp_private(ref, 0x3da58, get_picture, NELEM(get_picture), &status))
        return 0;
    if (status)
        return 1;
    const uint32_t new_alias[] = {US_TO_SYN68K(&picture), US_TO_SYN68K(&alias)};
    if (!cfp_private(ref, 0x3da70, new_alias, NELEM(new_alias), &status))
        return 0;
    return (int16_t)status || (scenario_add_handle(ref, collection, TICK("dpal"), CL(alias)) &&
                               scenario_add(ref, collection, TICK("dpan"), sizeof alignment, &alignment));
}

static int scenario_save_options(const ROMlib_carbon_reference* ref, uint32_t collection)
{
    _Alignas(256) Str255 playlist = {0};
    _Alignas(8) RGBColor highlight;
    _Alignas(8) uint16_t arrows, thumb, smoothing_size;
    _Alignas(8) Byte smoothing;
    _Alignas(8) uint32_t mask;
    uint32_t value, status;
    const uint32_t highlight_ptr = US_TO_SYN68K(&highlight), mask_ptr = US_TO_SYN68K(&mask);
    const uint32_t arrow_ptr = US_TO_SYN68K(&arrows), thumb_ptr = US_TO_SYN68K(&thumb);
    if (!cfp_private(ref, 0x3c720, &highlight_ptr, 1, NULL) ||
        !scenario_add(ref, collection, TICK("hcol"), sizeof highlight, &highlight) ||
        !cfp_private(ref, 0x168ec, NULL, 0, &value))
        return 0;
    _Alignas(8) Byte enabled = value;
    if (!scenario_add(ref, collection, TICK("snds"), 1, &enabled) ||
        !appearance_get_playlist(ref, US_TO_SYN68K(playlist), &status) ||
        (!status && !scenario_add(ref, collection, TICK("sndt"), playlist[0] + 1, playlist)) ||
        !cfp_private(ref, 0x167f4, &mask_ptr, 1, &status) ||
        (!status && !scenario_add(ref, collection, TICK("smsk"), sizeof mask, &mask)) ||
        !cfp_private(ref, 0x97e0, &arrow_ptr, 1, NULL) ||
        !scenario_add(ref, collection, TICK("sbar"), sizeof arrows, &arrows) ||
        !cfp_private(ref, 0x9734, &thumb_ptr, 1, NULL) ||
        !scenario_add(ref, collection, TICK("sbth"), sizeof thumb, &thumb))
        return 0;
    const uint32_t smoothing_args[] = {US_TO_SYN68K(&smoothing), US_TO_SYN68K(&smoothing_size)};
    if (!cfp_private(ref, 0xb50, smoothing_args, NELEM(smoothing_args), NULL) ||
        !scenario_add(ref, collection, TICK("smoo"), 1, &smoothing) ||
        !scenario_add(ref, collection, TICK("smos"), sizeof smoothing_size, &smoothing_size) ||
        !cfp_private(ref, 0x3da88, NULL, 0, &value))
        return 0;
    _Alignas(8) Byte collapse = value == 1;
    return scenario_add(ref, collection, TICK("coll"), 1, &collapse);
}

static int appearance_get_scenario(const char* name, uint32_t offset, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t collection = call->words[0];
    if (!collection)
        return appearance_client_result(call, paramErr);
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_load_reference("AppearanceLib", name, &ref))
        return 0;
    if (!appearance_reference_layout(&ref, offset))
        return ROMlib_carbon_invoke_frame(ref.function, call);

    _Alignas(256) Str255 theme = {0}, variant = {0}, font = {0};
    if (!cfp_private(&ref, 0x3da10, &collection, 1, NULL) ||
        !appearance_theme_names(&ref, US_TO_SYN68K(theme), US_TO_SYN68K(variant)) ||
        !scenario_add(&ref, collection, TICK("thme"), theme[0] + 1, theme) ||
        (variant[0] && !scenario_add(&ref, collection, TICK("varn"), variant[0] + 1, variant)))
        return 0;
    const uint32_t font_args[] = {UINT32_MAX, US_TO_SYN68K(font)};
    if (!cfp_private(&ref, 0x56c, font_args, NELEM(font_args), NULL) ||
        !scenario_add(&ref, collection, TICK("lgsf"), font[0] + 1, font) ||
        !cfp_private(&ref, 0x7c4, font_args, NELEM(font_args), NULL) ||
        !scenario_add(&ref, collection, TICK("smsf"), font[0] + 1, font))
        return 0;
    _Alignas(8) uint16_t size;
    const uint32_t views[] = {UINT32_MAX, US_TO_SYN68K(font), US_TO_SYN68K(&size)};
    return cfp_private(&ref, 0x99c, views, NELEM(views), NULL) &&
           scenario_add(&ref, collection, TICK("vfnt"), font[0] + 1, font) &&
           scenario_add(&ref, collection, TICK("vfsz"), sizeof size, &size) &&
           scenario_save_desktop(&ref, collection) && scenario_save_options(&ref, collection) &&
           appearance_client_result(call, noErr);
}

static int carbon_GetScenario(ROMlib_carbon_ppc_frame* call)
{
    return appearance_get_scenario("GetScenario", 0x82c0, call);
}

static int carbon_GetTheme(ROMlib_carbon_ppc_frame* call)
{
    return appearance_get_scenario("GetTheme", 0x7d0c, call);
}

static int scenario_read(const ROMlib_carbon_reference* ref, uint32_t collection, uint32_t tag, uint32_t capacity,
                         void* output, uint32_t* size)
{
    _Alignas(8) uint32_t length = 0;
    uint32_t status;
    uint32_t args[] = {collection, tag, 0, US_TO_SYN68K(&length), 0};
    if (!cfp_private(ref, 0x3c408, args, NELEM(args), &status))
        return 0;
    *size = 0;
    if ((int16_t)status || !length || CL(length) > capacity)
        return 1;
    args[4] = US_TO_SYN68K(output);
    if (!cfp_private(ref, 0x3c408, args, NELEM(args), &status))
        return 0;
    if (!(int16_t)status && CL(length) <= capacity)
        *size = CL(length);
    return 1;
}

static int scenario_read_string(const ROMlib_carbon_reference* ref, uint32_t collection, uint32_t tag, Str255 text)
{
    uint32_t size;
    if (!scenario_read(ref, collection, tag, sizeof(Str255), text, &size))
        return 0;
    if (!size || text[0] >= size)
        text[0] = 0;
    return 1;
}

static int scenario_set_fonts(const ROMlib_carbon_reference* ref, uint32_t collection)
{
    static const uint32_t fonts[][2] = {{TICK("lgsf"), 0x458}, {TICK("smsf"), 0x5d0}};
    _Alignas(256) Str255 font = {0};
    for (unsigned i = 0; i < NELEM(fonts); ++i) {
        if (!scenario_read_string(ref, collection, fonts[i][0], font))
            return 0;
        const uint32_t args[] = {US_TO_SYN68K(font), UINT32_MAX, 0};
        if (font[0] && !cfp_private(ref, fonts[i][1], args, NELEM(args), NULL))
            return 0;
    }
    uint32_t length;
    _Alignas(8) uint16_t size = 0, current_size = 0, minimum = 0;
    if (!scenario_read_string(ref, collection, TICK("vfnt"), font) ||
        !scenario_read(ref, collection, TICK("vfsz"), sizeof size, &size, &length))
        return 0;
    if (font[0] || length == sizeof size) {
        _Alignas(256) Str255 current = {0};
        const uint32_t query[] = {UINT32_MAX, US_TO_SYN68K(current), US_TO_SYN68K(&current_size)};
        if (!cfp_private(ref, 0x99c, query, NELEM(query), NULL))
            return 0;
        const uint32_t set[] = {US_TO_SYN68K(font[0] ? font : current),
                                (uint32_t)(int16_t)CW(length == sizeof size && size ? size : current_size), UINT32_MAX,
                                0};
        if (!cfp_private(ref, 0x860, set, NELEM(set), NULL))
            return 0;
    }
    _Alignas(8) Byte smooth = 0;
    if (!scenario_read(ref, collection, TICK("smoo"), 1, &smooth, &length))
        return 0;
    if (length == 1) {
        if (!scenario_read(ref, collection, TICK("smos"), sizeof minimum, &minimum, &length))
            return 0;
        const uint32_t update[] = {smooth, length == sizeof minimum ? CW(minimum) : 0, 0};
        if (!cfp_private(ref, 0xa5c, update, NELEM(update), NULL))
            return 0;
    }
    return 1;
}

static int scenario_install_desktop(const ROMlib_carbon_reference* ref, uint32_t collection, uint32_t* status)
{
    uint32_t pattern, copied;
    const uint32_t zero = 0;
    if (!cfp_private(ref, 0x3c420, &zero, 1, &pattern))
        return 0;
    if (!pattern) {
        if (!cfp_private(ref, 0x3c7c8, NULL, 0, status))
            return 0;
        *status = (int16_t)*status;
        return 1;
    }
    const uint32_t get_pattern[] = {collection, TICK("patt"), 0, pattern};
    _Alignas(256) Str255 name = {0};
    const uint32_t install[] = {pattern, US_TO_SYN68K(name)};
    const int ok = cfp_private(ref, 0x3c438, get_pattern, NELEM(get_pattern), &copied) &&
                   ((int16_t)copied || (scenario_read_string(ref, collection, TICK("patn"), name) &&
                                        cfp_private(ref, 0x3c450, install, NELEM(install), NULL)));
    if (!cfp_private(ref, 0x3be98, &pattern, 1, NULL) || !ok)
        return 0;

    _Alignas(8) FSSpec picture;
    _Alignas(8) uint32_t alignment = 0;
    uint32_t device, result;
    const uint32_t resolve[] = {collection, US_TO_SYN68K(&picture), US_TO_SYN68K(&alignment)};
    if (!cfp_private(ref, 0x9404, resolve, NELEM(resolve), &result) || !cfp_private(ref, 0x3bfb8, NULL, 0, &device))
        return 0;
    const uint32_t desktop[] = {device, (int16_t)result ? 0 : resolve[1], (int16_t)result ? 5 : CL(alignment)};
    return cfp_private(ref, 0x3daa0, desktop, NELEM(desktop), NULL);
}

static int scenario_install_options(const ROMlib_carbon_reference* ref, uint32_t collection)
{
    uint32_t length;
    _Alignas(8) RGBColor highlight;
    if (!scenario_read(ref, collection, TICK("hcol"), sizeof highlight, &highlight, &length))
        return 0;
    const uint32_t colors[] = {US_TO_SYN68K(&highlight), 0};
    if (length == sizeof highlight && !cfp_private(ref, 0x527c, colors, NELEM(colors), NULL))
        return 0;

    _Alignas(256) Str255 theme = {0}, variant = {0};
    if (!scenario_read_string(ref, collection, TICK("thme"), theme) ||
        !scenario_read_string(ref, collection, TICK("varn"), variant))
        return 0;
    if (!theme[0] && variant[0] && !appearance_theme_names(ref, US_TO_SYN68K(theme), 0))
        return 0;
    if (theme[0]) {
        ROMlib_carbon_ppc_frame select = {.words = {US_TO_SYN68K(theme), US_TO_SYN68K(variant), 0}};
        if (!carbon_SetAppearance(&select))
            return 0;
    }
    _Alignas(8) Byte enabled = 0, collapse = 0;
    _Alignas(8) uint32_t mask;
    _Alignas(8) uint16_t style;
    _Alignas(256) Str255 playlist = {0};
    if (!scenario_read(ref, collection, TICK("snds"), 1, &enabled, &length))
        return 0;
    const uint32_t enable = enabled;
    if ((length == 1 && !cfp_private(ref, 0x1682c, &enable, 1, NULL)) ||
        !scenario_read(ref, collection, TICK("sndt"), sizeof playlist, playlist, &length))
        return 0;
    const uint32_t sound_set = US_TO_SYN68K(playlist);
    if ((length > playlist[0] && !cfp_private(ref, 0x16e58, &sound_set, 1, NULL)) ||
        !scenario_read(ref, collection, TICK("smsk"), sizeof mask, &mask, &length))
        return 0;
    if (length == sizeof mask) {
        const uint32_t value = CL(mask);
        if (!cfp_private(ref, 0x166f8, &value, 1, NULL))
            return 0;
    }
    static const uint32_t scroll[][2] = {{TICK("sbar"), 0x9758}, {TICK("sbth"), 0x96b4}};
    for (unsigned i = 0; i < NELEM(scroll); ++i) {
        if (!scenario_read(ref, collection, scroll[i][0], sizeof style, &style, &length))
            return 0;
        const uint32_t update[] = {length == sizeof style ? CW(style) : 0, 0};
        if (length == sizeof style && !cfp_private(ref, scroll[i][1], update, NELEM(update), NULL))
            return 0;
    }
    if (!scenario_read(ref, collection, TICK("coll"), 1, &collapse, &length))
        return 0;
    const uint32_t action = collapse != 0;
    return length != 1 || cfp_private(ref, 0x3c480, &action, 1, NULL);
}

static int appearance_set_scenario(const char* name, uint32_t offset, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t collection = call->words[0];
    if (!collection)
        return appearance_client_result(call, paramErr);
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_load_reference("AppearanceLib", name, &ref))
        return 0;
    if (!appearance_reference_layout(&ref, offset))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    uint32_t status = noErr;
    if (!scenario_set_fonts(&ref, collection) || !scenario_install_desktop(&ref, collection, &status))
        return 0;
    if (status)
        return appearance_client_result(call, status);
    if (!scenario_install_options(&ref, collection) || !cfp_private(&ref, 0x3c030, NULL, 0, NULL) ||
        !cfp_private(&ref, 0x3c1c8, NULL, 0, NULL))
        return 0;
    RgnHandle desktop = MR(GrayRgn);
    if (desktop)
        C_PaintBehind(NULL, desktop);
    return appearance_client_result(call, noErr);
}

static int carbon_SetScenario(ROMlib_carbon_ppc_frame* call)
{
    return appearance_set_scenario("SetScenario", 0x7d54, call);
}

static int carbon_SetTheme(ROMlib_carbon_ppc_frame* call)
{
    return appearance_set_scenario("SetTheme", 0x7ce8, call);
}

static int carbon_IsValidAppearanceFileType(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_load_reference("AppearanceLib", "IsValidAppearanceFileType", &ref))
        return 0;
    if (!appearance_reference_layout(&ref, 0x38bc))
        return ROMlib_carbon_invoke_frame(ref.function, call);

    /* The active Appearance engine registers FourCCs in a linked list. */
    uint32_t address = cfp_toc(&ref, 0x45ac);
    for (unsigned i = 0; address && i < 4096; ++i) {
        const uint32_t* record = ROMlib_carbon_try_guest_memory(address, 12, 0);
        if (!record)
            break;
        if (CL(record[0]) == call->words[0])
            return appearance_client_result(call, record[1] != 0);
        address = CL(record[2]);
    }
    return appearance_client_result(call, 0);
}

enum appearance_variant_lookup { appearance_variant_count, appearance_variant_index, appearance_variant_name };

enum { variant_table_header_size = 6, variant_fields_size = 8, variant_name_size = 256 };

static int appearance_read_variant(const ROMlib_carbon_reference* ref, enum appearance_variant_lookup lookup,
                                   ROMlib_carbon_ppc_frame* call)
{
    enum { variant_not_found = -30572 };

    const bool by_name = lookup == appearance_variant_name;
    const uint32_t address = call->words[0];
    const uint32_t target = call->words[lookup == appearance_variant_count ? 1 : 2];
    const size_t length = lookup == appearance_variant_count ? 2 : variant_name_size + variant_fields_size;
    Byte* output = target ? ROMlib_carbon_try_guest_memory(target, length, 1) : NULL;
    uint32_t extent;
    const Byte* data = cfp_handle_data(address, &extent, 0);
    if (!output || !data || extent < variant_table_header_size)
        return appearance_client_result(call, paramErr);
    if (lookup == appearance_variant_count) {
        memcpy(output, data + 4, 2);
        return appearance_client_result(call, noErr);
    }

    const int16_t count = (int16_t)((data[4] << 8) | data[5]);
    const int16_t index = call->words[1];
    if (!by_name && (index < 1 || index > count))
        return appearance_client_result(call, variant_not_found);
    if (by_name) {
        const uint32_t name = call->words[1];
        const Byte* first = name ? ROMlib_carbon_try_guest_memory(name, 1, 0) : NULL;
        if (!first || !ROMlib_carbon_try_guest_memory(name, *first + 1, 0))
            return appearance_client_result(call, paramErr);
    }

    Handle handle = (Handle)MR(address);
    const SignedByte state = by_name ? HGetState(handle) : 0;
    if (by_name)
        HLock(handle);
    uint32_t offset = variant_table_header_size;
    OSErr result = variant_not_found;
    int ok = 1;
    for (int i = 1; i <= count; ++i) {
        if (offset >= extent || data[offset] > 127 ||
            (uint32_t)data[offset] + 1 + variant_fields_size > extent - offset) {
            result = paramErr;
            break;
        }
        const size_t name_size = data[offset] + 1;
        uint32_t found = i == index;
        if (by_name) {
            const uint32_t args[] = {US_TO_SYN68K(data + offset), call->words[1], 0, 1};
            ok = cfp_private(ref, 0x3c318, args, NELEM(args), &found);
            if (!ok)
                break;
        }
        if ((uint8_t)found) {
            memmove(output + variant_name_size, data + offset + name_size, variant_fields_size);
            memmove(output, data + offset, name_size);
            result = noErr;
            break;
        }
        offset += name_size + variant_fields_size;
    }
    if (by_name)
        HSetState(handle, state);
    return ok && appearance_client_result(call, result);
}

#define APPEARANCE_VARIANT(name, offset, lookup)                         \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call)              \
    {                                                                    \
        ROMlib_carbon_reference ref;                                     \
        if (!ROMlib_carbon_load_reference("AppearanceLib", #name, &ref)) \
            return 0;                                                    \
        if (!appearance_reference_layout(&ref, offset))                  \
            return ROMlib_carbon_invoke_frame(ref.function, call);       \
        return appearance_read_variant(&ref, lookup, call);              \
    }

APPEARANCE_VARIANT(CountThemeVariants, 0x9ae4, appearance_variant_count)
APPEARANCE_VARIANT(GetIndThemeVariant, 0x9b30, appearance_variant_index)
APPEARANCE_VARIANT(GetThemeVariantByName, 0x9c54, appearance_variant_name)

#undef APPEARANCE_VARIANT

static int appearance_visit_variants(ROMlib_carbon_ppc_frame* call, bool iterate)
{
    const char* name = iterate ? "IterateThemeVariants" : "GetThemeVariantList";
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_load_reference("AppearanceLib", name, &ref))
        return 0;
    if (!appearance_reference_layout(&ref, iterate ? 0x9988 : 0x987c))
        return ROMlib_carbon_invoke_frame(ref.function, call);

    const uint32_t target = call->words[1];
    uint32_t* output = !iterate && target ? ROMlib_carbon_try_guest_memory(target, sizeof *output, 1) : NULL;
    if (!target || (!iterate && !output))
        return appearance_client_result(call, paramErr);

    _Alignas(8) FSSpec file;
    const uint32_t file_address = US_TO_SYN68K(&file);
    const uint32_t locate[] = {call->words[0], file_address};
    uint32_t result;
    if (!cfp_private(&ref, 0x7c68, locate, NELEM(locate), &result))
        return 0;
    if ((int16_t)result)
        return appearance_client_result(call, (int16_t)result);

    const uint32_t open[] = {file_address, fsRdPerm};
    uint32_t resource_file;
    if (!cfp_private(&ref, 0x3c330, NULL, 0, NULL) || !cfp_private(&ref, 0x3c348, open, NELEM(open), &resource_file))
        return 0;
    resource_file = (int16_t)resource_file;
    if ((int32_t)resource_file == -1)
        return cfp_private(&ref, 0x3c2e8, NULL, 0, &result) && appearance_client_result(call, (int16_t)result);

    const uint32_t find[] = {TICK("tvar"), 128};
    uint32_t handle;
    OSErr status = resNotFound;
    int ok = cfp_private(&ref, 0x3c360, find, NELEM(find), &handle);
    if (ok && handle && !iterate) {
        ok = cfp_private(&ref, 0x3c4f8, &handle, 1, NULL);
        if (ok) {
            const uint32_t encoded = CL(handle);
            memcpy(output, &encoded, sizeof encoded);
            status = noErr;
        }
    }
    else if (ok && handle) {
        uint32_t extent;
        const Byte* data = cfp_handle_data(handle, &extent, 0);
        if (data && extent >= variant_table_header_size) {
            const int16_t count = (int16_t)((data[4] << 8) | data[5]);
            _Alignas(8) Byte variant[variant_name_size + variant_fields_size] = {0};
            const uint32_t address = US_TO_SYN68K(variant);
            status = noErr;
            for (int index = 1; index <= count; ++index) {
                ROMlib_carbon_ppc_frame lookup = {.words = {handle, index, address}};
                ok = appearance_read_variant(&ref, appearance_variant_index, &lookup);
                status = (OSErr)lookup.words[0];
                if (!ok || status)
                    break;
                const uint32_t args[] = {target, 0xf90, index, address, call->words[2]};
                ok = cfp_private(&ref, 0x3c780, args, NELEM(args), &result);
                if (!ok || !result)
                    break;
            }
        }
    }
    const int closed = cfp_private(&ref, 0x3c3a8, &resource_file, 1, NULL);
    return ok && closed && appearance_client_result(call, status);
}

static int carbon_GetThemeVariantList(ROMlib_carbon_ppc_frame* call)
{
    return appearance_visit_variants(call, false);
}

static int carbon_IterateThemeVariants(ROMlib_carbon_ppc_frame* call)
{
    return appearance_visit_variants(call, true);
}

#define APPEARANCE_GET(name, offset, field, width)                           \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call)                  \
    {                                                                        \
        return appearance_get_preference(#name, offset, field, width, call); \
    }

#define APPEARANCE_SET(name, offset, field, tag, style_limit)                           \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call)                             \
    {                                                                                   \
        return appearance_set_preference(#name, offset, field, tag, style_limit, call); \
    }

APPEARANCE_GET(GetThemeScrollBarThumbStyle, 0x9734, -0x75ae, 2)
APPEARANCE_GET(GetThemeScrollBarArrowStyle, 0x97e0, appearance_arrow_style_field, 2)
APPEARANCE_GET(GetThemeCheckBoxStyle, 0x9858, -0x75b2, 2)
APPEARANCE_GET(GetThemeSoundMask, 0x167f4, 0x4464, 4)
APPEARANCE_GET(AreThemeSoundsEnabled, 0x168ec, 0x45a2, 1)
APPEARANCE_GET(GetThemeSoundPanningSpread, 0x169e0, 0x4468, 4)

APPEARANCE_SET(SetThemeScrollBarThumbStyle, 0x96b4, -0x75ae, TICK("tsty"), 1)
APPEARANCE_SET(SetThemeScrollBarArrowStyle, 0x9758, appearance_arrow_style_field, TICK("asty"), 2)
APPEARANCE_SET(SetThemeCheckBoxStyle, 0x9804, -0x75b2, 0, 1)
APPEARANCE_SET(SetThemeSoundPanningSpread, 0x16990, 0x4468, TICK("panp"), 0)

#undef APPEARANCE_GET
#undef APPEARANCE_SET

#endif
