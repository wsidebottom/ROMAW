/*+===================================================================
  File:      control_access.h

  Summary:   Implements Carbon Control Manager calls over live ROMAW controls.

  Classes:   None.

  Functions: Private Control Manager adapters for the export registry.

  Origin:    CarbonLib 1.4, System 9.2.1 AppearanceLib, and Universal Interfaces
             3.4.2 Controls.h and ControlDefinitions.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_CONTROL_ACCESS_H
#define ROMAW_CARBON_CONTROL_ACCESS_H

#include "ResourceMgr.h"
#include "cf_batch.h"
#include "collection_entries.h"
#include "rsys/ctl.h"
#include "rsys/wind.h"

enum carbon_control_operation {
    control_activate,
    control_deactivate,
    control_active,
    control_visible,
    control_valid,
    control_set_focus,
    control_get_focus,
    control_clear_focus,
    control_idle,
    control_dialog_item,
    control_get_data,
    control_set_data,
    control_data_size,
    control_features,
    control_get_id,
    control_set_id,
    control_find_id,
    control_get_command,
    control_set_command,
    control_get_parent,
    control_count_children,
    control_get_child
};

static int carbon_control_result(ROMlib_carbon_ppc_frame* call, int32_t value)
{
    call->words[0] = (uint32_t)value;
    return 1;
}

static int carbon_cycle_keyboard_focus(ROMlib_carbon_ppc_frame* call, bool reverse)
{
    const uint32_t address = 0x17cc;
    uint32_t vector;
    if (!cfp_get(address, &vector))
        return 0;
    if (vector && vector != (uint32_t)(uintptr_t)toolstuff[0x273].orig) {
        uint32_t result;
        return ROMlib_carbon_call_toolbox_vector(address, 0x3a8, reverse ? 0x10 : 0x0f, call, &result) &&
               carbon_control_result(call, (int16_t)result);
    }
    WindowPtr window = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], sizeof(WindowRecord), 0) : NULL;
    return carbon_control_result(call, ROMlib_control_cycle_focus(window, reverse));
}

static int carbon_AdvanceKeyboardFocus(ROMlib_carbon_ppc_frame* call)
{
    return carbon_cycle_keyboard_focus(call, false);
}

static int carbon_ReverseKeyboardFocus(ROMlib_carbon_ppc_frame* call)
{
    return carbon_cycle_keyboard_focus(call, true);
}

static int control_tag_access(ROMlib_carbon_ppc_frame* call, const char* name, int16_t part, uint32_t tag, size_t size,
                              Byte* value, bool write)
{
    const uint32_t address = call->words[0];
    if (!address || !value)
        return carbon_control_result(call, paramErr);
    ControlHandle control = ROMlib_carbon_try_guest_memory(address, sizeof *control, 0);
    if (!control)
        return carbon_control_result(call, paramErr);
    if (!ROMlib_live_control_aux(control)) {
        ROMlib_carbon_reference saved;
        return ROMlib_carbon_load_reference("AppearanceLib", name, &saved) &&
               ROMlib_carbon_invoke_frame(saved.function, call);
    }
    return carbon_control_result(call, ROMlib_control_data(control, part, tag, size, value, NULL,
                                                           write ? ROMLIB_CONTROL_WRITE : ROMLIB_CONTROL_READ));
}

static int carbon_SetDisclosureTriangleLastValue(ROMlib_carbon_ppc_frame* call)
{
    _Alignas(8) uint16_t value = CW(call->words[1]);
    return control_tag_access(call, "SetDisclosureTriangleLastValue", 0, TICK("last"), sizeof value, (Byte*)&value,
                              true);
}

static int carbon_SetTabEnabled(ROMlib_carbon_ppc_frame* call)
{
    _Alignas(8) Byte value = call->words[2];
    return control_tag_access(call, "SetTabEnabled", (int16_t)call->words[1], TICK("enab"), sizeof value, &value, true);
}

static int carbon_GetTabContentRect(ROMlib_carbon_ppc_frame* call)
{
    Byte* output = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], sizeof(Rect), 1) : NULL;
    return control_tag_access(call, "GetTabContentRect", 0, TICK("rect"), sizeof(Rect), output, false);
}

static int carbon_GetBevelButtonMenuHandle(ROMlib_carbon_ppc_frame* call)
{
    if (!call->words[0] || !call->words[1] ||
        !ROMlib_carbon_try_guest_memory(call->words[0], sizeof(HIDDEN_ControlPtr), 0) ||
        !ROMlib_carbon_try_guest_memory(call->words[1], sizeof(uint32_t), 1))
        return carbon_control_result(call, paramErr);

    /* The live Control Manager distinguishes unsupported menu tags from an
       empty menu. The CDEF's direct read cannot make that distinction. */
    const ROMlib_carbon_ppc_frame arguments = {
        .words = {call->words[0], 0, TICK("mhnd"), sizeof(uint32_t), call->words[1], 0},
    };
    uint32_t result;
    return ROMlib_carbon_call_toolbox_vector(0x17cc, 0xffba8, 0x13, &arguments, &result) &&
           carbon_control_result(call, (int16_t)result);
}

/* Both Appearance re-exports set the CDEF's two-byte icon transform. */
static int carbon_control_icon_transform(ROMlib_carbon_ppc_frame* call)
{
    if (!call->words[0])
        return carbon_control_result(call, paramErr);
    ControlHandle control = ROMlib_carbon_try_guest_memory(call->words[0], sizeof(HIDDEN_ControlPtr), 0);
    if (!control)
        return carbon_control_result(call, -30584);
    INTEGER transform = CW((uint16_t)call->words[1]);
    return carbon_control_result(call, ROMlib_control_data(control, 0, TICK("tran"), sizeof transform,
                                                           (Byte*)&transform, NULL, ROMLIB_CONTROL_WRITE));
}

static int carbon_control_title(ROMlib_carbon_ppc_frame* call, bool copy)
{
    const char* name = copy ? "CopyControlTitleAsCFString" : "SetControlTitleWithCFString";
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", name, &ref))
        return 0;
    if (ref.code_offset != (copy ? 0x6a250u : 0x6a1c4u) || ref.code_size != 0xb8fb4 || !ref.toc ||
        ref.toc_offset != 0x2f78 || ref.toc_size != 0x10f0c)
        return ROMlib_carbon_invoke_frame(ref.function, call);
    const uint32_t control = call->words[0], argument = call->words[1];
    if (copy && !argument)
        return carbon_control_result(call, paramErr);
    uint32_t valid;
    if (!cfp_private(&ref, 0xb1b30, &control, 1, &valid))
        return 0;
    if (!valid)
        return carbon_control_result(call, -30599);

    enum { title_conversion_error = -4960 };

    _Alignas(256) Byte text[256] = {0};
    const uint32_t buffer = US_TO_SYN68K(text);
    if (copy) {
        if (!ROMlib_carbon_guest_memory(argument, 4, 1))
            return 0;
        uint32_t encoding;
        if (!cfp_private(&ref, 0xb66d8, (const uint32_t[]){control, buffer}, 2, NULL) ||
            !cfp_private(&ref, 0xb4368, NULL, 0, &encoding))
            return 0;
        const uint32_t title = ROMlib_cfstring_pascal(0, buffer, encoding);
        return cfp_write(argument, title) && carbon_control_result(call, title ? noErr : title_conversion_error);
    }
    uint32_t encoding;
    if (!cfp_private(&ref, 0x3374c, (const uint32_t[]){argument, buffer}, 2, &encoding))
        return 0;
    if (encoding == UINT32_MAX) {
        void* string = argument ? ROMlib_carbon_guest_memory(argument, 1, 0) : NULL;
        if (!string || !ROMlib_carbon_cf_string_pascal(string, text, sizeof text, ROMlib_cfstring_system_encoding()))
            return carbon_control_result(call, title_conversion_error);
    }
    return cfp_private(&ref, 0xb66f0, (const uint32_t[]){control, buffer}, 2, NULL) &&
           carbon_control_result(call, noErr);
}

static int carbon_SetControlTitleWithCFString(ROMlib_carbon_ppc_frame* call)
{
    return carbon_control_title(call, false);
}

static int carbon_CopyControlTitleAsCFString(ROMlib_carbon_ppc_frame* call)
{
    return carbon_control_title(call, true);
}

static bool controls_reference_layout(const ROMlib_carbon_reference* ref, uint32_t entry)
{
    return ref->code_offset == entry && ref->code_size == 0xc580 && ref->toc && ref->toc_offset == 0 &&
           ref->toc_size == 0xfd8;
}

static int carbon_saved_control_range(ROMlib_carbon_ppc_frame* call, ControlHandle control,
                                      enum ROMlib_control_range_field field, bool set, const char* name, uint32_t entry)
{
    ROMlib_carbon_reference ref;
    if (!control || !name || !ROMlib_carbon_load_reference("ControlsLib", name, &ref))
        return carbon_control_result(call, 0);
    if (!controls_reference_layout(&ref, entry))
        return ROMlib_carbon_invoke_frame(ref.function, call);

    const uint32_t address = call->words[0];
    const uint32_t lookup[] = {address, 0};
    uint32_t record;
    if (!cfp_private(&ref, 0x158, lookup, NELEM(lookup), &record))
        return 0;
    if (!record)
        return carbon_control_result(call, 0);

    /* The saved Control Manager owns the range in its hashed record. The
       ControlRecord still exposes its signed, truncated 16-bit fields. */
    if (record > UINT32_MAX - 0x28)
        return 0;
    uint32_t owner, master, range[4];
    if (!cfp_get(record, &owner) || owner != address || !cfp_get(address, &master) || !master ||
        !ROMlib_carbon_try_guest_memory(master, offsetof(ControlRecord, contrlDefProc), set))
        return 0;
    const int16 legacy[] = {CTL_VALUE(control), CTL_MIN(control), CTL_MAX(control)};
    for (unsigned i = 0; i < NELEM(range); ++i) {
        const uint32_t slot = record + 0x18 + i * 4;
        if (!cfp_get(slot, &range[i]))
            return 0;
        if (i < NELEM(legacy) && legacy[i] != ROMlib_control_legacy_value((int32)range[i])) {
            range[i] = (int32)legacy[i];
            if (!cfp_write(slot, range[i]))
                return 0;
        }
    }
    if (!set)
        return carbon_control_result(call, (int32)range[field]);

    const uint32_t value = call->words[1];
    if (value == range[field])
        return carbon_control_result(call, 0);
    if (!cfp_write(record + 0x18 + field * 4, value))
        return 0;
    const int16_t encoded = CW(ROMlib_control_legacy_value((int32)value));
    switch (field) {
    case control_value:
        CTL_VALUE_X(control) = encoded;
        break;
    case control_minimum:
        CTL_MIN_X(control) = encoded;
        break;
    case control_maximum:
        CTL_MAX_X(control) = encoded;
        break;
    case control_view_size: {
        _Alignas(32) Byte glyph_state[32];
        const uint32_t state = US_TO_SYN68K(glyph_state);
        const uint32_t redraw[] = {address, 0, 0x81};
        if (!cfp_private(&ref, 0xbe78, &state, 1, NULL))
            return 0;
        const int drawn = !CTL_VIS(control) || cfp_private(&ref, 0x304, redraw, NELEM(redraw), NULL);
        return cfp_private(&ref, 0xbe90, &state, 1, NULL) && drawn && carbon_control_result(call, 0);
    }
    }
    return cfp_private(&ref, 0x3124, &record, 1, NULL) && carbon_control_result(call, 0);
}

static int carbon_control_range(ROMlib_carbon_ppc_frame* call, enum ROMlib_control_range_field field, bool set,
                                const char* name, uint32_t entry)
{
    const uint32_t address = call->words[0];
    ControlHandle control = address ? ROMlib_carbon_try_guest_memory(address, sizeof *control, 0) : NULL;
    AuxCtlHandle aux = ROMlib_live_control_aux(control);
    if (!aux)
        return carbon_saved_control_range(call, control, field, set, name, entry);
    /* The saved Control Manager links classic AuxCtlRec handles into the
       same list. Only ROMAW-created controls have the appended range data. */
    const INTEGER saved_error = MemErr;
    const Size aux_size = GetHandleSize((Handle)aux);
    MemErr = saved_error;
    if (aux_size < (Size)(sizeof(AuxCtlRec) + sizeof(ROMlib_aux_control_data)))
        return carbon_saved_control_range(call, control, field, set, name, entry);

    int32 range[4];
    ROMlib_control_read_range(control, aux, range);
    if (!set)
        return carbon_control_result(call, range[field]);

    const int32_t new_value = (int32_t)call->words[1];
    if (new_value != range[field]) {
        ROMlib_aux_control_extra(aux)->range[field] = range[field] = new_value;
        if (field != control_view_size) {
            const int32_t min = range[control_minimum], max = range[control_maximum];
            const int32_t value = range[control_value];
            ROMlib_control_store_value(control, aux, value < min ? min : value > max ? max : value);
            CTL_MIN_X(control) = CW(ROMlib_control_legacy_value(min));
            CTL_MAX_X(control) = CW(ROMlib_control_legacy_value(max));
        }
        if (CTL_VIS(control)) {
            uint32_t ignored;
            if (!ROMlib_carbon_call_toolbox_vector(0x13b4, 0xc0, 0, call, &ignored))
                return 0;
        }
    }
    return carbon_control_result(call, 0);
}

static int carbon_control_pointer_message(ROMlib_carbon_ppc_frame* call, bool click)
{
    const uint32_t address = call->words[0], output_address = call->words[3];
    ControlHandle control = address ? ROMlib_carbon_try_guest_memory(address, sizeof *control, 0) : NULL;
    if (!control || !output_address)
        return carbon_control_result(call, paramErr);

    const bool native = ROMlib_live_control_aux(control) != NULL;
    const uint32_t feature = click ? 0x400000 : 0x100000;
    uint32_t supported;
    ROMlib_carbon_reference ref;
    if (native) {
        uint32_t features;
        supported = ROMlib_get_control_features(control, &features) == noErr ? CL(features) & feature : 0;
    }
    else {
        const char* name = click ? "GetControlClickActivation" : "HandleControlSetCursor";
        if (!ROMlib_carbon_load_reference("ControlsLib", name, &ref))
            return carbon_control_result(call, paramErr);
        if (!controls_reference_layout(&ref, click ? 0xe20 : 0xd58))
            return ROMlib_carbon_invoke_frame(ref.function, call);
        uint32_t valid;
        if (!cfp_private(&ref, 0x2c84, &address, 1, &valid))
            return 0;
        if (!(uint8_t)valid)
            return carbon_control_result(call, paramErr);
        const uint32_t test[] = {address, feature};
        if (!cfp_private(&ref, 0xbba8, test, NELEM(test), &supported))
            return 0;
        supported = (uint8_t)supported;
    }

    const size_t result_size = click ? sizeof(uint32_t) : sizeof(uint8_t);
    void* output = ROMlib_carbon_try_guest_memory(output_address, result_size, 1);
    if (!output)
        return carbon_control_result(call, paramErr);
    const uint32_t initial = CL(click ? 2 : 0);
    memcpy(output, &initial, result_size);
    if (!supported)
        return carbon_control_result(call, noErr);

    _Alignas(16) Byte parameter[10];
    const uint32_t point = CL(call->words[1]);
    const uint16_t modifiers = CW((uint16_t)call->words[2]);
    memcpy(parameter, &point, sizeof point);
    memcpy(parameter + 4, &modifiers, sizeof modifiers);
    memcpy(parameter + 6, &initial, sizeof initial);
    const uint32_t message = click ? 0x30 : 0x21;
    const uint32_t arguments[] = {address, message, US_TO_SYN68K(parameter)};
    uint32_t status;
    if (native)
        status = ROMlib_send_control_message(control, message, arguments[2]);
    else if (!cfp_private(&ref, 0xbbc0, arguments, NELEM(arguments), &status))
        return 0;
    if (!status)
        memcpy(output, parameter + 6, result_size);
    return carbon_control_result(call, (int32_t)status);
}

static int carbon_GetControlClickActivation(ROMlib_carbon_ppc_frame* call)
{
    return carbon_control_pointer_message(call, true);
}

static int carbon_HandleControlSetCursor(ROMlib_carbon_ppc_frame* call)
{
    return carbon_control_pointer_message(call, false);
}

static int carbon_HandleControlContextualMenuClick(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t address = call->words[0];
    ControlHandle control = address ? ROMlib_carbon_try_guest_memory(address, sizeof *control, 0) : NULL;
    Byte* displayed = call->words[2] ? ROMlib_carbon_try_guest_memory(call->words[2], 1, 1) : NULL;
    if (!control || !displayed)
        return carbon_control_result(call, paramErr);

    if (!ROMlib_live_control_aux(control)) {
        ROMlib_carbon_reference ref;
        if (ROMlib_carbon_reference_function("CarbonLib", "HandleControlContextualMenuClick", &ref))
            return ROMlib_carbon_invoke_frame(ref.function, call);
        return carbon_control_result(call, paramErr);
    }

    *displayed = 0;
    uint32_t features;
    const OSErr feature_status = ROMlib_get_control_features(control, &features);
    if (feature_status || !(CL(features) & 0x00200000))
        return carbon_control_result(call, feature_status);

    _Alignas(8) struct PACKED {
        uint32_t where;
        Byte displayed;
    } request = {CL(call->words[1]), 0};

    const LONGINT status = ROMlib_send_control_message(control, 0x2f, US_TO_SYN68K(&request));
    if (!status)
        *displayed = request.displayed;
    return carbon_control_result(call, status);
}

static int carbon_control_hierarchy(ROMlib_carbon_ppc_frame* call, enum carbon_control_operation op,
                                    ControlHandle control, AuxCtlHandle aux)
{
    const uint32_t address = call->words[op == control_get_child ? 2 : 1];
    const unsigned width = op == control_count_children ? 2 : 4;
    void* output = address ? ROMlib_carbon_try_guest_memory(address, width, 1) : NULL;
    if (!aux || !output)
        return carbon_control_result(call, paramErr);
    if (op == control_get_parent)
        memset(output, 0, width);

    _Alignas(16) HIDDEN_ControlHandle root;
    ROMlib_carbon_ppc_frame root_call = {0};
    root_call.words[0] = US_TO_SYN68K(CTL_OWNER(control));
    root_call.words[1] = US_TO_SYN68K(&root);
    if (!ROMlib_carbon_call_romaw_service("GetRootControl", &root_call))
        return 0;
    if ((int16_t)root_call.words[0])
        return carbon_control_result(call, (int16_t)root_call.words[0]);

    uint32_t features = 0;
    if (op == control_get_child) {
        if (ROMlib_get_control_features(control, &features) || !(CL(features) & 2))
            return carbon_control_result(call, -30590);
        aux = ROMlib_live_control_aux(control);
        if (!aux)
            return carbon_control_result(call, paramErr);
    }
    const ROMlib_aux_control_data* extra = ROMlib_aux_control_extra(aux);
    uint32_t ancestor = RM(control);
    AuxCtlHandle ancestor_aux = aux;
    for (unsigned depth = 0; ancestor != HIDDEN_RAW(root); ++depth) {
        if (!ancestor_aux || depth == UINT16_MAX)
            return carbon_control_result(call, -30584);
        ancestor = ROMlib_aux_control_extra(ancestor_aux)->parent;
        ancestor_aux = ancestor ? ROMlib_live_control_aux(MR(ancestor)) : NULL;
    }
    if (op == control_get_parent) {
        if (!extra->parent)
            return carbon_control_result(call, paramErr);
        memcpy(output, &extra->parent, 4);
        return carbon_control_result(call, noErr);
    }

    const uint16_t index = (uint16_t)call->words[1];
    if (op == control_get_child && (!index || index > INT16_MAX))
        return carbon_control_result(call, paramErr);
    unsigned count = 0;
    for (uint32_t child = extra->first_child; child;) {
        if (++count == index && op == control_get_child) {
            memcpy(output, &child, 4);
            return carbon_control_result(call, noErr);
        }
        AuxCtlHandle child_aux = ROMlib_live_control_aux(MR(child));
        if (!child_aux || count == UINT16_MAX)
            break;
        child = ROMlib_aux_control_extra(child_aux)->next_sibling;
    }
    if (op == control_get_child)
        return carbon_control_result(call, paramErr);
    const uint16_t stored = CW(count);
    memcpy(output, &stored, 2);
    if (count || (!ROMlib_get_control_features(control, &features) && (CL(features) & 2)))
        return carbon_control_result(call, noErr);
    return carbon_control_result(call, -30590);
}

static int carbon_control(enum carbon_control_operation op, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    if (op == control_features && (!args[0] || !args[1]))
        return carbon_control_result(call, paramErr);
    if ((op >= control_set_focus && op <= control_dialog_item) || op == control_find_id) {
        WindowPtr window = args[0] ? ROMlib_carbon_guest_memory(args[0], sizeof(WindowRecord), 0) : NULL;
        if (op == control_idle) {
            ROMlib_control_idle(window);
            return carbon_control_result(call, 0);
        }
        if (op == control_clear_focus)
            return carbon_control_result(call, ROMlib_control_focus(window, NULL, 0, NULL));
        if (op == control_set_focus) {
            ControlHandle control = args[1] ? ROMlib_carbon_guest_memory(args[1], sizeof *control, 0) : NULL;
            if ((args[1] && !control) || (!control && (int16_t)args[2]))
                return carbon_control_result(call, paramErr);
            return carbon_control_result(call, ROMlib_control_focus(window, control, (int16_t)args[2], NULL));
        }
        const uint32_t address = op == control_get_focus ? args[1] : args[2];
        HIDDEN_ControlHandle* output = address ? ROMlib_carbon_guest_memory(address, sizeof *output, 1) : NULL;
        if (!output)
            return carbon_control_result(call, paramErr);
        if (op == control_get_focus)
            return carbon_control_result(call, ROMlib_control_focus(window, NULL, 0, output));
        if (op == control_dialog_item)
            return carbon_control_result(call, ROMlib_get_dialog_control(window, (int16_t)args[1], output));

        const void* identifier = args[1] ? ROMlib_carbon_guest_memory(args[1], 8, 0) : NULL;
        uint32_t words[2];
        if (!ROMlib_valid_window(window) || !identifier)
            return carbon_control_result(call, paramErr);
        /* Snapshot the ID before clearing an output that may overlap it. */
        memcpy(words, identifier, sizeof words);
        HIDDEN_RAW_SET(*output, 0);
        for (ControlHandle control = WINDOW_CONTROL_LIST(window); control; control = CTL_NEXT_CONTROL(control)) {
            AuxCtlHandle aux = ROMlib_live_control_aux(control);
            if (!aux)
                continue;
            const ROMlib_aux_control_data* extra = ROMlib_aux_control_extra(aux);
            if (words[0] == extra->identifier_signature && words[1] == extra->identifier_id) {
                HIDDEN_RAW_SET(*output, RM(control));
                return carbon_control_result(call, noErr);
            }
        }
        return carbon_control_result(call, -30584);
    }

    if (op >= control_get_parent) {
        ControlHandle control = args[0] ? ROMlib_carbon_try_guest_memory(args[0], sizeof *control, 0) : NULL;
        return carbon_control_hierarchy(call, op, control, ROMlib_live_control_aux(control));
    }
    ControlHandle control = args[0] ? ROMlib_carbon_guest_memory(args[0], sizeof *control, 0) : NULL;
    AuxCtlHandle aux = ROMlib_live_control_aux(control);
    if (op == control_valid)
        return carbon_control_result(call, aux != NULL);
    if (op == control_active)
        return carbon_control_result(call, aux && CTL_HILITE(control) != INACTIVE);
    if (op == control_visible)
        return carbon_control_result(call, aux && CTL_VIS(control));
    if (!aux)
        return carbon_control_result(call, op >= control_get_id ? (args[0] ? -30599 : paramErr) : -30584);
    if (op == control_activate || op == control_deactivate)
        return carbon_control_result(call, ROMlib_control_activate(control, op == control_activate));

    if (op == control_features) {
        uint32_t* output = args[1] ? ROMlib_carbon_guest_memory(args[1], 4, 1) : NULL;
        return carbon_control_result(call, ROMlib_get_control_features(control, output));
    }
    if (op >= control_get_data && op <= control_data_size) {
        const enum ROMlib_control_data_op mode = op == control_get_data   ? ROMLIB_CONTROL_READ
                                                 : op == control_set_data ? ROMLIB_CONTROL_WRITE
                                                                          : ROMLIB_CONTROL_MEASURE;
        const int32_t size = op == control_data_size ? 0 : (int32_t)args[3];
        const uint32_t address = op == control_data_size ? args[3] : op == control_get_data ? args[5] : 0;
        Size* actual = address ? ROMlib_carbon_guest_memory(address, sizeof *actual, 1) : NULL;
        if ((address && !actual) || (op == control_data_size && !actual) || size < 0)
            return carbon_control_result(call, paramErr);
        Byte* data = size && args[4] ? ROMlib_carbon_guest_memory(args[4], size, op == control_get_data) : NULL;
        return carbon_control_result(call,
                                     ROMlib_control_data(control, (int16_t)args[1], args[2], size, data, actual, mode));
    }

    ROMlib_aux_control_data* extra = ROMlib_aux_control_extra(aux);
    if (op == control_set_command) {
        extra->command_id = CL(args[1]);
        return carbon_control_result(call, noErr);
    }
    const size_t length = op == control_get_command ? 4 : 8;
    void* record = args[1] ? ROMlib_carbon_guest_memory(args[1], length, op != control_set_id) : NULL;
    if (!record)
        return carbon_control_result(call, paramErr);
    if (op == control_set_id)
        memcpy(&extra->identifier_signature, record, length);
    else
        memcpy(record, op == control_get_id ? &extra->identifier_signature : &extra->command_id, length);
    return carbon_control_result(call, noErr);
}

#define CARBON_CONTROL(name, operation)                     \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call) \
    {                                                       \
        return carbon_control(operation, call);             \
    }

CARBON_CONTROL(ActivateControl, control_activate)
CARBON_CONTROL(DeactivateControl, control_deactivate)
CARBON_CONTROL(IsControlActive, control_active)
CARBON_CONTROL(IsControlVisible, control_visible)
CARBON_CONTROL(IsValidControlHandle, control_valid)
CARBON_CONTROL(SetKeyboardFocus, control_set_focus)
CARBON_CONTROL(GetKeyboardFocus, control_get_focus)
CARBON_CONTROL(ClearKeyboardFocus, control_clear_focus)
CARBON_CONTROL(IdleControls, control_idle)
CARBON_CONTROL(GetDialogItemAsControl, control_dialog_item)
CARBON_CONTROL(GetControlData, control_get_data)
CARBON_CONTROL(SetControlData, control_set_data)
CARBON_CONTROL(GetControlDataSize, control_data_size)
CARBON_CONTROL(GetControlFeatures, control_features)
CARBON_CONTROL(GetControlID, control_get_id)
CARBON_CONTROL(SetControlID, control_set_id)
CARBON_CONTROL(GetControlByID, control_find_id)
CARBON_CONTROL(GetControlCommandID, control_get_command)
CARBON_CONTROL(SetControlCommandID, control_set_command)
CARBON_CONTROL(GetSuperControl, control_get_parent)
CARBON_CONTROL(CountSubControls, control_count_children)
CARBON_CONTROL(GetIndexedSubControl, control_get_child)

#undef CARBON_CONTROL

#define CARBON_RANGE(name, field, set, entry)                        \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call)          \
    {                                                                \
        return carbon_control_range(call, field, set, #name, entry); \
    }

CARBON_RANGE(GetControl32BitValue, control_value, false, 0x3418)
CARBON_RANGE(SetControl32BitValue, control_value, true, 0x34e8)
CARBON_RANGE(GetControl32BitMinimum, control_minimum, false, 0x37b8)
CARBON_RANGE(SetControl32BitMinimum, control_minimum, true, 0x3888)
CARBON_RANGE(GetControl32BitMaximum, control_maximum, false, 0x35e8)
CARBON_RANGE(SetControl32BitMaximum, control_maximum, true, 0x36b8)
CARBON_RANGE(GetControlViewSize, control_view_size, false, 0x3228)
CARBON_RANGE(SetControlViewSize, control_view_size, true, 0x32f8)

#undef CARBON_RANGE

static ControlHandle carbon_geometry_control(uint32_t address)
{
    ControlHandle control = address ? ROMlib_carbon_try_guest_memory(address, sizeof *control, 0) : NULL;
    return ROMlib_live_control_aux(control) ? control : NULL;
}

static int carbon_SetControlColorProc(ROMlib_carbon_ppc_frame* call)
{
    ControlHandle control = carbon_geometry_control(call->words[0]);
    uint32 procedure = call->words[1];
    if (!control || (procedure && !ROMlib_carbon_try_guest_memory(procedure, 2, 0)))
        return carbon_control_result(call, paramErr);
    ROMlib_aux_control_extra(ROMlib_live_control_aux(control))->color_proc = procedure;
    return carbon_control_result(call, noErr);
}

static int carbon_control_color(ROMlib_carbon_ppc_frame* call, boolean_t text)
{
    ControlHandle control = carbon_geometry_control(call->words[0]);
    return carbon_control_result(
        call, ROMlib_set_up_control_color(control, (int16)call->words[1], (Byte)call->words[2], text));
}

static int carbon_SetUpControlBackground(ROMlib_carbon_ppc_frame* call)
{
    return carbon_control_color(call, FALSE);
}

static int carbon_SetUpControlTextColor(ROMlib_carbon_ppc_frame* call)
{
    return carbon_control_color(call, TRUE);
}

static int carbon_GetBestControlRect(ROMlib_carbon_ppc_frame* call)
{
    ControlHandle control = carbon_geometry_control(call->words[0]);
    Rect* bounds = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], sizeof *bounds, 1) : NULL;
    INTEGER* baseline = call->words[2] ? ROMlib_carbon_try_guest_memory(call->words[2], sizeof *baseline, 1) : NULL;
    if (!control || !bounds || !baseline)
        return carbon_control_result(call, paramErr);
    return carbon_control_result(call, ROMlib_control_best_rect(control, bounds, baseline));
}

static int carbon_GetControlRegion(ROMlib_carbon_ppc_frame* call)
{
    ControlHandle control = carbon_geometry_control(call->words[0]);
    const uint32_t address = call->words[2];
    RgnHandle region = address ? ROMlib_carbon_try_guest_memory(address, sizeof *region, 0) : NULL;
    uint32_t data;
    if (!control || !region || !cfp_get(address, &data) || !data || !ROMlib_carbon_try_guest_memory(data, 10, 0))
        return carbon_control_result(call, paramErr);
    return carbon_control_result(call, ROMlib_control_region(control, (int16_t)call->words[1], region));
}

static int custom_control_item(uint32 collection, uint32 tag, uint32* size, void* data)
{
    ROMlib_carbon_ppc_frame item = {0};
    item.words[0] = collection;
    item.words[1] = tag;
    item.words[3] = US_TO_SYN68K(size);
    item.words[4] = data ? US_TO_SYN68K(data) : 0;
    return carbon_GetCollectionItem(&item) && !item.words[0];
}

static void custom_control_title(ControlHandle control, uint32 collection)
{
    _Alignas(16) uint32 size = 0;
    _Alignas(16) Str255 title;
    if (!custom_control_item(collection, TICK("uttl"), &size, NULL)) {
        size = CL(sizeof title - 1);
        if (custom_control_item(collection, TICK("titl"), &size, title + 1)) {
            title[0] = MIN(CL(size), sizeof title - 1);
            memcpy(CTL_TITLE(control), title, title[0] + 1);
        }
        return;
    }
    uint32 length = CL(size);
    uint32 data = ROMlib_cfdata_create_mutable(0, length);
    if (!data)
        return;
    ROMlib_cfdata_set_length(data, length);
    void* object = SYN68K_TO_US(data);
    if (ROMlib_carbon_cf_data_length(object) == (int32)length) {
        void* bytes = ROMlib_carbon_cf_data_bytes(object);
        if ((!length || bytes) && custom_control_item(collection, TICK("uttl"), &size, bytes)) {
            uint32 encoding = ROMlib_cfstring_system_encoding();
            uint32 string = ROMlib_cfstring_from_external(0, data, encoding);
            if (string) {
                void* text = SYN68K_TO_US(string);
                if (ROMlib_carbon_cf_string_pascal(text, title, sizeof title, encoding))
                    memcpy(CTL_TITLE(control), title, title[0] + 1);
                ROMlib_carbon_cf_release(text);
            }
        }
    }
    ROMlib_carbon_cf_release(object);
}

static int32_t create_custom_control(WindowPtr window, const Rect* bounds, uint32 definition, uint32 collection,
                                     uint32* result_control)
{
    ControlHandle control = ROMlib_alloc_custom_control(window, bounds, definition);
    if (!control)
        return memFullErr;
    const uint32 address = US_TO_SYN68K(control);
    if (collection) {
        static const struct {
            uint32 tag;
            enum ROMlib_control_range_field field;
        } fields[] = {{TICK("min "), control_minimum}, {TICK("max "), control_maximum}, {TICK("valu"), control_value}};

        for (unsigned i = 0; i < NELEM(fields); ++i) {
            _Alignas(16) uint32 size = CLC(4), value;
            if (custom_control_item(collection, fields[i].tag, &size, &value) && CL(size) == 4) {
                ROMlib_carbon_ppc_frame range = {0};
                range.words[0] = address;
                range.words[1] = CL(value);
                carbon_control_range(&range, fields[i].field, true, NULL, 0);
            }
        }
        _Alignas(16) uint32 size = CLC(4), reference;
        if (custom_control_item(collection, TICK("refc"), &size, &reference) && CL(size) == 4)
            HxX(control, contrlRfCon) = reference;
        custom_control_title(control, collection);
        _Alignas(16) Byte visible;
        size = CLC(1);
        if (custom_control_item(collection, TICK("visi"), &size, &visible) && CL(size) == 1)
            CTL_VIS_X(control) = visible ? 255 : 0;
        _Alignas(16) INTEGER variant;
        size = CLC(2);
        if (custom_control_item(collection, TICK("varc"), &size, &variant) && CL(size) == 2) {
            AuxCtlHandle aux = ROMlib_live_control_aux(control);
            HxX(aux, acFlags) = variant;
        }
    }
    int32_t status = ROMlib_send_control_message(control, initCntl, collection);
    if (!ROMlib_live_control_aux(control))
        return status ? status : paramErr;
    if (status)
        ROMlib_abort_custom_control(control);
    else
        *result_control = address;
    return status;
}

static int carbon_CreateCustomControl(ROMlib_carbon_ppc_frame* call)
{
    const uint32 output = call->words[4];
    if (!output || !cfp_clear(output, 4))
        return carbon_control_result(call, paramErr);
    WindowPtr window = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], sizeof(WindowRecord), 0) : NULL;
    const Rect* bounds = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], sizeof *bounds, 0) : NULL;
    uint32 spec = call->words[2], type, definition;
    if (!spec || spec > UINT32_MAX - 4 || !cfp_get(spec, &type) || !cfp_get(spec + 4, &definition))
        return carbon_control_result(call, paramErr);
    if (type)
        return 0;
    if (!ROMlib_valid_window(window) || !bounds || !definition || !ROMlib_carbon_try_guest_memory(definition, 2, 0))
        return carbon_control_result(call, paramErr);
    Rect rect = *bounds;
    uint32 control;
    int32_t status = create_custom_control(window, &rect, definition, call->words[3], &control);
    if (!status && !cfp_write(output, control)) {
        C_DisposeControl(SYN68K_TO_US(control));
        status = paramErr;
    }
    return carbon_control_result(call, status);
}

struct builtin_control_options {
    int16_t definition, value, minimum, maximum;
    uint32_t title, initial_value, lower_bound, upper_bound, view_size, action;
    uint32_t data_tag, data_address, font_style, text;
    uint16_t data_size, data;
    uint32_t tabs;
    uint16_t tab_count;
    bool set_value, set_range, set_view_size, set_action, title_at_creation, invalid;
};

static int control_pascal_text(const ROMlib_carbon_reference* ref, uint32_t text, Str255 buffer)
{
    uint32_t encoding;
    return cfp_private(ref, 0xb69c0, NULL, 0, &encoding) &&
           cfp_private(ref, 0xb64f8, (uint32_t[]){text, US_TO_SYN68K(buffer), sizeof(Str255), encoding}, 4, NULL);
}

static bool control_tab_icon(const Byte* tab, uint16_t* icon)
{
    uint32_t pointer;
    memcpy(&pointer, tab, sizeof pointer);
    *icon = 0;
    if (!pointer)
        return true;
    const Byte* content = ROMlib_carbon_try_guest_memory(CL(pointer), 4, 0);
    if (!content || content[0] != 0 || content[1] != 1)
        return false;
    memcpy(icon, content + 2, sizeof *icon);
    return true;
}

static int configure_control_tabs(const ROMlib_carbon_reference* ref, uint32_t control, const Byte* tabs,
                                  uint16_t count)
{
    _Alignas(8) struct {
        uint16_t version, icon;
        Str255 title;
    } info = {0};

    for (unsigned index = 0; index < count; ++index) {
        const Byte* tab = tabs + 10 * index;
        if (!control_tab_icon(tab, &info.icon))
            return ROMlib_carbon_report_failure("Tab data changed during initialization");
        uint32_t text;
        memcpy(&text, tab + 4, sizeof text);
        info.title[0] = 0;
        if (text && !control_pascal_text(ref, CL(text), info.title))
            return 0;
        const uint32_t args[] = {control, (int16_t)(index + 1), TICK("tabi"), sizeof info, US_TO_SYN68K(&info)};
        if (!cfp_private(ref, 0xb69d8, args, NELEM(args), NULL))
            return 0;
    }
    return 1;
}

static int create_builtin_control(ROMlib_carbon_ppc_frame* call, const char* name, uint32_t offset, unsigned out_arg,
                                  const struct builtin_control_options* options)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_load_reference("CarbonLib", name, &ref))
        return 0;
    if (ref.code_offset != offset || ref.code_size != 0xb8fb4 || !ref.toc || ref.toc_offset != 0x2f78 ||
        ref.toc_size != 0x10f0c)
        return ROMlib_carbon_invoke_frame(ref.function, call);

    uint32_t output;
    if (options->invalid || !ROMlib_carbon_frame_word(call, out_arg, &output) || !output || !call->words[1] ||
        !ROMlib_carbon_try_guest_memory(call->words[1], sizeof(Rect), 0) ||
        !ROMlib_carbon_try_guest_memory(output, 4, 1))
        return carbon_control_result(call, paramErr);

    const Byte* tabs = NULL;
    if (options->tabs && options->tab_count) {
        tabs = ROMlib_carbon_try_guest_memory(options->tabs, (size_t)options->tab_count * 10, 0);
        if (!tabs)
            return carbon_control_result(call, paramErr);
        for (unsigned index = 0; index < options->tab_count; ++index) {
            uint16_t icon;
            if (!control_tab_icon(tabs + 10 * index, &icon))
                return carbon_control_result(call, paramErr);
        }
    }

    uint32_t title = cfp_read(US_TO_SYN68K(ref.toc) - 0x120);
    _Alignas(8) Str255 pascal_title = {0};
    if (options->title_at_creation && options->title) {
        title = US_TO_SYN68K(pascal_title);
        if (!cfp_private(&ref, 0x3374c, (uint32_t[]){options->title, title}, 2, NULL))
            return 0;
    }
    const uint32_t arguments[] = {call->words[0],   call->words[1],      title, 0, options->value, options->minimum,
                                  options->maximum, options->definition, 0};
    uint32_t control;
    if (!cfp_private(&ref, 0xb5a00, arguments, NELEM(arguments), &control) || !cfp_write(output, control))
        return 0;
    if (!control)
        return carbon_control_result(call, memFullErr);

    if (options->font_style && !cfp_private(&ref, 0xb69a8, (uint32_t[]){control, options->font_style}, 2, NULL))
        return 0;
    if (options->text) {
        const uint32_t buffer = US_TO_SYN68K(pascal_title);
        if (!control_pascal_text(&ref, options->text, pascal_title) ||
            !cfp_private(&ref, 0xb69d8, (uint32_t[]){control, 0, TICK("text"), pascal_title[0], buffer + 1}, 5, NULL))
            return 0;
    }
    if (tabs && !configure_control_tabs(&ref, control, tabs, options->tab_count))
        return 0;
    if (options->title && !options->title_at_creation &&
        !cfp_private(&ref, 0xb36c0, (uint32_t[]){control, options->title}, 2, NULL))
        return 0;
    if (options->set_range && (!cfp_private(&ref, 0xb69f0, (uint32_t[]){control, options->lower_bound}, 2, NULL) ||
                               !cfp_private(&ref, 0xb6a08, (uint32_t[]){control, options->upper_bound}, 2, NULL)))
        return 0;
    if ((options->set_value || options->set_range) &&
        !cfp_private(&ref, 0xb6978, (uint32_t[]){control, options->initial_value}, 2, NULL))
        return 0;
    if (options->set_view_size && !cfp_private(&ref, 0xb6a20, (uint32_t[]){control, options->view_size}, 2, NULL))
        return 0;
    if (options->set_action && !cfp_private(&ref, 0xb6a38, (uint32_t[]){control, options->action}, 2, NULL))
        return 0;
    if (options->data_size) {
        _Alignas(8) Byte data[2] = {options->data_size == 1 ? options->data : options->data >> 8, options->data};
        const uint32_t address = options->data_address ? options->data_address : US_TO_SYN68K(data);
        const uint32_t arguments[] = {control, 0, options->data_tag, options->data_size, address};
        if (!cfp_private(&ref, 0xb69d8, arguments, NELEM(arguments), NULL))
            return 0;
    }
    return cfp_private(&ref, 0xb6990, (uint32_t[]){control, 1, 0}, 3, NULL) && carbon_control_result(call, noErr);
}

#define BUILTIN_CONTROL(name, offset, output, ...)                            \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call)                   \
    {                                                                         \
        const struct builtin_control_options options = {__VA_ARGS__};         \
        return create_builtin_control(call, #name, offset, output, &options); \
    }

BUILTIN_CONTROL(CreateChasingArrowsControl, 0x6ab8c, 2, .definition = 0x70)
BUILTIN_CONTROL(CreateSeparatorControl, 0x6ade8, 2, .definition = 0x90)
BUILTIN_CONTROL(CreateGroupBoxControl, 0x6ae88, 4, .definition = 0xa0 + ((uint8_t)call->words[3] ? 0 : 4),
                .title = call->words[2])
BUILTIN_CONTROL(CreatePopupArrowControl, 0x6b210, 4,
                .definition = 0xc0 + (uint16_t)call->words[2] + ((uint16_t)call->words[3] == 1 ? 4 : 0))
BUILTIN_CONTROL(CreatePlacardControl, 0x6b2cc, 2, .definition = 0xe0)
BUILTIN_CONTROL(CreateClockControl, 0x6b36c, 4, .definition = 0xf0 + (uint16_t)call->words[2], .value = call->words[3])
BUILTIN_CONTROL(CreateUserPaneControl, 0x6b414, 3, .definition = 0x100, .value = call->words[2])
BUILTIN_CONTROL(CreateWindowHeaderControl, 0x6b8d8, 3, .definition = 0x150 + ((uint8_t)call->words[2] != 0))
BUILTIN_CONTROL(CreatePushButtonControl, 0x6bad8, 3, .definition = 0x170, .maximum = 1, .title = call->words[2])
BUILTIN_CONTROL(CreateCheckBoxControl, 0x6bd7c, 5, .definition = 0x171 + ((uint8_t)call->words[4] ? 2 : 0),
                .maximum = 2, .title = call->words[2], .set_value = true, .initial_value = call->words[3])
BUILTIN_CONTROL(CreateRadioButtonControl, 0x6bca4, 5, .definition = 0x172 + ((uint8_t)call->words[4] ? 2 : 0),
                .maximum = 2, .title = call->words[2], .set_value = true, .initial_value = call->words[3])
BUILTIN_CONTROL(CreateRadioGroupControl, 0x6c054, 2, .definition = 0x1a0)
BUILTIN_CONTROL(CreateDisclosureTriangleControl, 0x6a8a4, 7,
                .definition = 0x40 + ((uint16_t)call->words[2] == 2) + ((uint8_t)call->words[6] ? 2 : 0),
                .title = call->words[3], .maximum = 1, .set_value = true, .initial_value = call->words[4],
                .invalid = (uint16_t)call->words[2] == 0 || (uint8_t)call->words[5] != 0)
BUILTIN_CONTROL(CreateCheckGroupBoxControl, 0x6af58, 6, .definition = 0xa1 + ((uint8_t)call->words[4] ? 0 : 4),
                .title = call->words[2], .title_at_creation = true, .maximum = 2, .set_value = true,
                .initial_value = call->words[3], .invalid = (uint8_t)call->words[5] != 0)
BUILTIN_CONTROL(CreateScrollingTextBoxControl, 0x6c0f4, 7, .definition = 0x1b0 + ((uint8_t)call->words[3] != 0),
                .value = call->words[2], .minimum = call->words[4], .maximum = call->words[5], .data_tag = TICK("samt"),
                .data_size = 2, .data = call->words[6])

BUILTIN_CONTROL(CreateEditTextControl, 0x6b4b4, 6,
                .definition = (uint8_t)call->words[3]   ? 0x112
                              : (uint8_t)call->words[4] ? 0x114
                                                        : 0x110,
                .text = call->words[2], .font_style = call->words[5])
BUILTIN_CONTROL(CreateStaticTextControl, 0x6b5d8, 4, .definition = 0x120, .text = call->words[2],
                .font_style = call->words[3])
BUILTIN_CONTROL(CreateTabsControl, 0x6ac2c, 6,
                .definition = 0x80 + (uint16_t)call->words[2] + 2 * (uint16_t)call->words[3], .maximum = call->words[4],
                .tab_count = call->words[4], .tabs = call->words[5])

#undef BUILTIN_CONTROL

static int carbon_CreateProgressBarControl(ROMlib_carbon_ppc_frame* call)
{
    WindowPtr window = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], sizeof(WindowRecord), 0) : NULL;
    const Rect* bounds = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], sizeof *bounds, 0) : NULL;
    const uint32_t output = call->words[6];
    if (!output || !ROMlib_carbon_try_guest_memory(output, 4, 1) || !window || !bounds)
        return carbon_control_result(call, paramErr);

    if (!ROMlib_valid_window(window) || GetResource(TICK("CDEF"), ROMlib_progress_bar_proc >> 4)) {
        const struct builtin_control_options options = {.definition = ROMlib_progress_bar_proc,
                                                        .maximum = 1,
                                                        .set_range = true,
                                                        .initial_value = call->words[2],
                                                        .lower_bound = call->words[3],
                                                        .upper_bound = call->words[4],
                                                        .data_tag = TICK("inde"),
                                                        .data_size = 1,
                                                        .data = (uint8_t)call->words[5]};
        return create_builtin_control(call, "CreateProgressBarControl", 0x6a9b0, 6, &options);
    }

    cfp_write(output, 0);
    const int32_t minimum = (int32_t)call->words[3], maximum = (int32_t)call->words[4];
    const int32_t initial = (int32_t)call->words[2];
    const int32_t value = initial < minimum ? minimum : initial > maximum ? maximum : initial;
    Str255 title = {0};
    Rect rect = *bounds;
    ControlHandle control = C_NewControl(window, &rect, title, FALSE, ROMlib_control_legacy_value(value),
                                         ROMlib_control_legacy_value(minimum), ROMlib_control_legacy_value(maximum),
                                         ROMlib_progress_bar_proc, 0);
    if (!control)
        return carbon_control_result(call, memFullErr);

    AuxCtlHandle aux = HIDDEN_NATIVE(*lookup_aux_ctl(control));
    ROMlib_aux_control_data* extra = ROMlib_aux_control_extra(aux);
    extra->range[control_minimum] = minimum;
    extra->range[control_maximum] = maximum;
    ROMlib_control_store_value(control, aux, value);
    extra->progress_indeterminate = (uint8_t)call->words[5] != 0;
    C_ShowControl(control);
    if (!cfp_write(output, US_TO_SYN68K(control))) {
        C_DisposeControl(control);
        return 0;
    }
    return carbon_control_result(call, noErr);
}

static int carbon_CreateLittleArrowsControl(ROMlib_carbon_ppc_frame* call)
{
    const struct builtin_control_options options = {.definition = 0x60,
                                                    .invalid = call->words[5] != 1,
                                                    .maximum = 1,
                                                    .set_range = true,
                                                    .initial_value = call->words[2],
                                                    .lower_bound = call->words[3],
                                                    .upper_bound = call->words[4]};
    return create_builtin_control(call, "CreateLittleArrowsControl", 0x6aaa8, 6, &options);
}

static int carbon_CreateSliderControl(ROMlib_carbon_ppc_frame* call)
{
    uint32_t action;
    if (!ROMlib_carbon_frame_word(call, 8, &action))
        return carbon_control_result(call, paramErr);
    const uint16_t direction = call->words[5], ticks = call->words[6];
    const bool live = (uint8_t)call->words[7] != 0;
    const struct builtin_control_options options = {.definition = 0x30 + (direction <= 2 ? 4 * direction : 0) +
                                                                  (ticks ? 2 : 0) + live,
                                                    .value = ticks,
                                                    .maximum = 1,
                                                    .set_range = true,
                                                    .initial_value = call->words[2],
                                                    .lower_bound = call->words[3],
                                                    .upper_bound = call->words[4],
                                                    .set_action = live,
                                                    .action = action};
    return create_builtin_control(call, "CreateSliderControl", 0x6a764, 9, &options);
}

static int carbon_CreateScrollBarControl(ROMlib_carbon_ppc_frame* call)
{
    const bool live = (uint8_t)call->words[6] != 0;
    const struct builtin_control_options options = {.definition = 0x180 + live,
                                                    .set_range = true,
                                                    .initial_value = call->words[2],
                                                    .lower_bound = call->words[3],
                                                    .upper_bound = call->words[4],
                                                    .set_view_size = true,
                                                    .view_size = call->words[5],
                                                    .set_action = live,
                                                    .action = call->words[7]};
    return create_builtin_control(call, "CreateScrollBarControl", 0x6be54, 8, &options);
}

static int carbon_CreateImageWellControl(ROMlib_carbon_ppc_frame* call)
{
    struct builtin_control_options options = {.definition = 0xb0};
    if (call->words[2]) {
        const Byte* content = ROMlib_carbon_try_guest_memory(call->words[2], 4, 0);
        if (!content)
            return carbon_control_result(call, paramErr);
        options.minimum = (content[0] << 8) | content[1];
        options.value = (content[2] << 8) | content[3];
    }
    return create_builtin_control(call, "CreateImageWellControl", 0x6b15c, 3, &options);
}

static int carbon_CreateBevelButtonControl(ROMlib_carbon_ppc_frame* call)
{
    uint32_t placement;
    if (!ROMlib_carbon_frame_word(call, 8, &placement))
        return 0;
    struct builtin_control_options options = {.definition = 0x20 + (uint16_t)call->words[3] + (uint16_t)placement,
                                              .value = call->words[6],
                                              .minimum = call->words[4] | call->words[7],
                                              .title = call->words[2]};
    if (call->words[5]) {
        const Byte* content = ROMlib_carbon_try_guest_memory(call->words[5], 4, 0);
        options.invalid = !content;
        if (content) {
            options.minimum |= (content[0] << 8) | content[1];
            options.maximum = (content[2] << 8) | content[3];
        }
    }
    return create_builtin_control(call, "CreateBevelButtonControl", 0x6a668, 9, &options);
}

static int carbon_CreatePopupGroupBoxControl(ROMlib_carbon_ppc_frame* call)
{
    uint32_t style;
    if (!ROMlib_carbon_frame_word(call, 8, &style))
        return 0;
    const struct builtin_control_options options = {.definition = 0xa2 + ((uint8_t)call->words[3] ? 0 : 4),
                                                    .invalid = (uint8_t)call->words[5] == 0,
                                                    .title = call->words[2],
                                                    .title_at_creation = true,
                                                    .value = ((uint8_t)style << 8) | (uint8_t)call->words[7],
                                                    .minimum = call->words[4],
                                                    .maximum = call->words[6]};
    return create_builtin_control(call, "CreatePopupGroupBoxControl", 0x6b058, 9, &options);
}

static int carbon_CreatePushButtonWithIconControl(ROMlib_carbon_ppc_frame* call)
{
    const Byte* content = call->words[3] ? ROMlib_carbon_try_guest_memory(call->words[3], 4, 0) : NULL;
    const uint16_t alignment = call->words[4];
    struct builtin_control_options options = {.definition = 0x170 + alignment,
                                              .invalid = !content || content[0] || content[1] != 2 ||
                                                         (alignment != 6 && alignment != 7),
                                              .title = call->words[2]};
    if (content)
        options.maximum = (content[2] << 8) | content[3];
    return create_builtin_control(call, "CreatePushButtonWithIconControl", 0x6bb9c, 5, &options);
}

static int carbon_CreatePopupButtonControl(ROMlib_carbon_ppc_frame* call)
{
    const struct builtin_control_options options = {.definition = 0x190 + ((uint8_t)call->words[4] ? 0 : 1),
                                                    .title = call->words[2],
                                                    .value = ((uint8_t)call->words[7] << 8) | (uint8_t)call->words[6],
                                                    .minimum = call->words[3],
                                                    .maximum = call->words[5]};
    return create_builtin_control(call, "CreatePopupButtonControl", 0x6bf7c, 8, &options);
}

static int create_graphic_control(ROMlib_carbon_ppc_frame* call, bool picture)
{
    const uint32_t address = call->words[2];
    struct builtin_control_options options = {.definition = (picture ? 0x130 : 0x140) + ((uint8_t)call->words[3] != 0)};
    if (address) {
        const Byte* data = ROMlib_carbon_try_guest_memory(address, 4, 0);
        options.invalid = !data;
        if (data) {
            const uint16_t type = (data[0] << 8) | data[1];
            const uint16_t resource = (data[2] << 8) | data[3];
            if (!picture) {
                options.definition += type == 0x84 ? 4 : type == 1 ? 2 : 0;
                options.value = type == 0x84 ? 0 : resource;
            }
            else if (type == 3)
                options.value = resource;
            else if (type == 0x83) {
                uint32_t handle;
                const Byte* record = ROMlib_carbon_try_guest_memory(address, 6, 0);
                options.invalid = !record;
                if (record) {
                    memcpy(&handle, record + 2, sizeof handle);
                    if (handle) {
                        options.data_tag = TICK("pich");
                        options.data_address = address + 2;
                        options.data_size = 4;
                    }
                }
            }
        }
    }
    return picture ? create_builtin_control(call, "CreatePictureControl", 0x6b6d8, 4, &options)
                   : create_builtin_control(call, "CreateIconControl", 0x6b7f4, 4, &options);
}

static int carbon_CreatePictureControl(ROMlib_carbon_ppc_frame* call)
{
    return create_graphic_control(call, true);
}

static int carbon_CreateIconControl(ROMlib_carbon_ppc_frame* call)
{
    return create_graphic_control(call, false);
}

static int carbon_CreateListBoxControl(ROMlib_carbon_ppc_frame* call)
{
    const char* name = "CreateListBoxControl";
    const uint32_t entry = 0x6b984;
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_load_reference("CarbonLib", name, &ref))
        return 0;
    if (ref.code_offset != entry || ref.code_size != 0xb8fb4 || !ref.toc || ref.toc_offset != 0x2f78 ||
        ref.toc_size != 0x10f0c)
        return ROMlib_carbon_invoke_frame(ref.function, call);

    uint32_t width, grow, definition, output;
    if (!ROMlib_carbon_frame_word(call, 8, &width) || !ROMlib_carbon_frame_word(call, 9, &grow) ||
        !ROMlib_carbon_frame_word(call, 10, &definition) || !ROMlib_carbon_frame_word(call, 11, &output))
        return 0;
    if (!call->words[1] || !ROMlib_carbon_try_guest_memory(call->words[1], sizeof(Rect), 0) || !output ||
        !ROMlib_carbon_try_guest_memory(output, 4, 1))
        return carbon_control_result(call, paramErr);

    const int16_t descriptor_id = -20970;
    const uint32_t get[] = {TICK("ldes"), descriptor_id};
    uint32_t handle;
    if (!cfp_private(&ref, 0xb1fb0, get, NELEM(get), &handle))
        return 0;
    if (!handle)
        return carbon_control_result(call, memFullErr);

    uint32_t size;
    Byte* descriptor = cfp_handle_data(handle, &size, 1);
    int ok;
    if (!descriptor || size < 17)
        ok = carbon_control_result(call, resNotFound);
    else {
        const uint16_t fields[] = {0, CW(call->words[3]), CW(call->words[4]), CW(call->words[7]), CW(width)};
        memcpy(descriptor, fields, sizeof fields);
        descriptor[10] = call->words[6];
        descriptor[12] = call->words[5];
        descriptor[16] = grow;
        _Alignas(8) uint16_t ldef;
        uint32_t result;
        const uint32_t lookup[] = {definition, US_TO_SYN68K(&ldef)};
        ok = cfp_private(&ref, 0x60050, lookup, NELEM(lookup), &result);
        if (ok && (int32_t)result)
            ok = carbon_control_result(call, result);
        else if (ok) {
            descriptor = cfp_handle_data(handle, &size, 1);
            if (!descriptor || size < 17)
                ok = carbon_control_result(call, resNotFound);
            else {
                memcpy(descriptor + 14, &ldef, sizeof ldef);
                const struct builtin_control_options options = {.definition = 0x160 + ((uint8_t)call->words[2] != 0),
                                                                .value = descriptor_id};
                ok = create_builtin_control(call, name, entry, 11, &options);
            }
        }
    }
    const int released = cfp_private(&ref, 0xb1f98, &handle, 1, NULL);
    return ok && released;
}

enum toolbox_definition_operation {
    toolbox_definition_install_direct,
    toolbox_definition_install,
    toolbox_definition_find,
    toolbox_definition_remove,
};

static int carbon_toolbox_definition(const char* name, uint32_t offset, enum toolbox_definition_operation operation,
                                     ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference reference;
    if (!ROMlib_carbon_load_reference("CarbonLib", name, &reference))
        return 0;
    if (reference.code_offset != offset || reference.code_size != 0xb8fb4 || reference.toc_offset != 0x2f78 ||
        reference.toc_size != 0x10f0c)
        return ROMlib_carbon_invoke_frame(reference.function, call);

    const uint32_t instance = call->words[0], second = call->words[1], third = call->words[2];
    const uint32_t list_slot = cfp_toc(&reference, (unsigned)-0xa68);
    uint32_t list = list_slot ? cfp_read(list_slot) : 0;
    const bool installing = operation <= toolbox_definition_install;
    if (installing) {
        uint32_t definition = second;
        if (operation == toolbox_definition_install) {
            const uint32_t definition_slot = cfp_toc(&reference, (unsigned)-0xa64);
            if (!second || !cfp_get(definition_slot, &definition) || !cfp_write(second, definition))
                return 0;
            if (third) {
                void* style = ROMlib_carbon_try_guest_memory(third, 2, 1);
                const uint32_t style_slot = cfp_toc(&reference, (unsigned)-0xa60);
                uint32_t value;
                if (!style || !cfp_get(style_slot, &value))
                    return 0;
                const uint16_t encoded = CW((uint16_t)value);
                memcpy(style, &encoded, sizeof encoded);
            }
        }
        _Alignas(8) uint32_t record[] = {CL(instance), CL(definition)};
        const uint32_t push[] = {list, US_TO_SYN68K(record)};
        uint32_t status;
        if (!cfp_private(&reference, 0xb5610, push, NELEM(push), &status))
            return 0;
        call->words[0] = status == noErr;
        return 1;
    }

    const bool finding = operation == toolbox_definition_find;
    if (finding && (!second || !cfp_write(second, 0)))
        return 0;
    _Alignas(4) uint32_t index = 0;
    uint32_t found = 0;
    const uint32_t find[] = {list, instance, US_TO_SYN68K(&index)};
    if (list && !cfp_private(&reference, 0xb5628, find, NELEM(find), &found))
        return 0;
    if (found) {
        // Reload the list just as the CFM routines do after a Toolbox callback.
        if (!cfp_get(list_slot, &list))
            return 0;
        if (!finding) {
            const uint32_t remove[] = {list, CL(index)};
            return cfp_private(&reference, 0xb5658, remove, NELEM(remove), NULL);
        }
        _Alignas(8) uint32_t record[2] = {0};
        const uint32_t read[] = {list, CL(index), US_TO_SYN68K(record)};
        uint32_t status;
        if (!cfp_private(&reference, 0xb5640, read, NELEM(read), &status))
            return 0;
        found = status == noErr;
        if (found && !cfp_write(second, CL(record[1])))
            return 0;
    }
    call->words[0] = found != 0;
    return 1;
}

static int carbon_InstallTBInstanceDefinitionDirect(ROMlib_carbon_ppc_frame* call)
{
    return carbon_toolbox_definition("InstallTBInstanceDefinitionDirect", 0x55cd0, toolbox_definition_install_direct,
                                     call);
}

static int carbon_InstallTBInstanceDefinition(ROMlib_carbon_ppc_frame* call)
{
    return carbon_toolbox_definition("InstallTBInstanceDefinition", 0x55d10, toolbox_definition_install, call);
}

static int carbon_FindDefinitionFromTBInstance(ROMlib_carbon_ppc_frame* call)
{
    return carbon_toolbox_definition("FindDefinitionFromTBInstance", 0x55d74, toolbox_definition_find, call);
}

static int carbon_RemoveTBInstanceDefinition(ROMlib_carbon_ppc_frame* call)
{
    return carbon_toolbox_definition("RemoveTBInstanceDefinition", 0x55e40, toolbox_definition_remove, call);
}

#endif
