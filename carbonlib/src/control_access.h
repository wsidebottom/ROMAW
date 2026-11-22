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
        uint32_t encoding, title;
        if (!cfp_private(&ref, 0xb66d8, (const uint32_t[]){control, buffer}, 2, NULL) ||
            !cfp_private(&ref, 0xb4368, NULL, 0, &encoding) ||
            !cfp_private(&ref, 0xb6510, (const uint32_t[]){0, buffer, encoding}, 3, &title))
            return 0;
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

static int carbon_control_range(ROMlib_carbon_ppc_frame* call, enum ROMlib_control_range_field field, int set)
{
    const uint32_t address = call->words[0];
    ControlHandle control = address ? ROMlib_carbon_try_guest_memory(address, sizeof *control, 0) : NULL;
    AuxCtlHandle aux = ROMlib_live_control_aux(control);
    if (!aux)
        return carbon_control_result(call, 0);

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

#define CARBON_RANGE(name, field, set)                      \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call) \
    {                                                       \
        return carbon_control_range(call, field, set);      \
    }

CARBON_RANGE(GetControl32BitValue, control_value, 0)
CARBON_RANGE(SetControl32BitValue, control_value, 1)
CARBON_RANGE(GetControl32BitMinimum, control_minimum, 0)
CARBON_RANGE(SetControl32BitMinimum, control_minimum, 1)
CARBON_RANGE(GetControl32BitMaximum, control_maximum, 0)
CARBON_RANGE(SetControl32BitMaximum, control_maximum, 1)
CARBON_RANGE(GetControlViewSize, control_view_size, 0)
CARBON_RANGE(SetControlViewSize, control_view_size, 1)

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
                carbon_control_range(&range, fields[i].field, 1);
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

#endif
