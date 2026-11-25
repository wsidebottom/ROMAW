/*+===================================================================
  File:      appearance_state.h

  Summary:   Implements Appearance Manager client registration and preferences.

  Classes:   None.

  Functions: Private Appearance Manager entry points for the export table.

  Origin:    Mac OS 9 AppearanceLib client table, process-reference workers and preference collection.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_APPEARANCE_STATE_H
#define ROMAW_CARBON_APPEARANCE_STATE_H

#include "MemoryMgr.h"
#include "ResourceMgr.h"
#include "cf_batch.h"
#include "rsys/cquick.h"
#include "rsys/stubify.h"
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

static int carbon_DrawThemeEditTextFrame(ROMlib_carbon_ppc_frame* call)
{
    if (!appearance_has_rect(call->words[0]))
        return appearance_client_result(call, paramErr);
    call->words[1] = appearance_classic_draw_state(call->words[1]);
    return appearance_classic_service(call, 0xfb8, 0x09, "DrawThemeEditTextFrame");
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

static int appearance_theme_names(const ROMlib_carbon_reference* ref, uint32_t theme, uint32_t variant)
{
    uint32_t query[] = {0, theme, variant};
    return appearance_engine_call(ref, 0x128, query, NELEM(query), NULL);
}

static int carbon_GetThemeTextColor(ROMlib_carbon_ppc_frame* call)
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
    if (color < 1 || color > 47)
        return appearance_client_result(call, -30563);
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_load_reference("AppearanceLib", "GetThemeTextColor", &ref))
        return 0;
    if (!appearance_reference_layout(&ref, 0x2c64))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    uint32_t args[] = {0, (uint32_t)(int32_t)color, (uint32_t)(int32_t)(int16_t)call->words[1], (uint8_t)call->words[2],
                       address};
    uint32_t status;
    return appearance_engine_call(&ref, 0x30, args, NELEM(args), &status) && appearance_client_result(call, status);
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
    if (!ROMlib_carbon_load_reference("AppearanceLib", name, &reference))
        return appearance_client_result(call, width == 1 ? 0 : appearance_unimplemented);
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
    if (!ROMlib_carbon_load_reference("AppearanceLib", name, &reference))
        return appearance_client_result(call, appearance_unimplemented);
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
APPEARANCE_GET(GetThemeScrollBarArrowStyle, 0x97e0, -0x75b0, 2)
APPEARANCE_GET(GetThemeCheckBoxStyle, 0x9858, -0x75b2, 2)
APPEARANCE_GET(GetThemeSoundMask, 0x167f4, 0x4464, 4)
APPEARANCE_GET(AreThemeSoundsEnabled, 0x168ec, 0x45a2, 1)
APPEARANCE_GET(GetThemeSoundPanningSpread, 0x169e0, 0x4468, 4)

APPEARANCE_SET(SetThemeScrollBarThumbStyle, 0x96b4, -0x75ae, TICK("tsty"), 1)
APPEARANCE_SET(SetThemeScrollBarArrowStyle, 0x9758, -0x75b0, TICK("asty"), 2)
APPEARANCE_SET(SetThemeCheckBoxStyle, 0x9804, -0x75b2, 0, 1)
APPEARANCE_SET(SetThemeSoundPanningSpread, 0x16990, 0x4468, TICK("panp"), 0)

#undef APPEARANCE_GET
#undef APPEARANCE_SET

#endif
