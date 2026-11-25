/*+===================================================================
  File:      dialogs.c

  Summary:   Carbon dialog and control operations over ROMAW's classic records.

  Classes:   None.

  Functions: ROMlib_carbon_control_bounds, ROMlib_carbon_dialog_default_item, ROMlib_carbon_dialog_from_window,
             ROMlib_carbon_dialog_text_edit, ROMlib_carbon_dialog_window, ROMlib_carbon_get_param_text,
             ROMlib_carbon_insert_dialog_item, ROMlib_carbon_remove_dialog_items, ROMlib_carbon_set_port_dialog_port,
             ROMlib_carbon_std_filter_proc

  Origin:    Existing repository source in packages/carbonlib/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "cf_batch.h"
#include "rsys/ctl.h"
#include "rsys/itm.h"
#include "rsys/wind.h"
#include <string.h>

WindowPtr ROMlib_carbon_dialog_window(DialogPtr dialog)
{
    return dialog;
}

DialogPtr ROMlib_carbon_dialog_from_window(WindowPtr window)
{
    return window;
}

TEHandle ROMlib_carbon_dialog_text_edit(DialogPtr dialog)
{
    return dialog ? DIALOG_TEXTH(dialog) : NULL;
}

INTEGER ROMlib_carbon_dialog_default_item(DialogPtr dialog)
{
    return dialog ? DIALOG_ADEF_ITEM(dialog) : 0;
}

void ROMlib_carbon_set_port_dialog_port(DialogPtr dialog)
{
    if (dialog)
        C_SetPort((GrafPtr)dialog);
}

Rect* ROMlib_carbon_control_bounds(ControlHandle control, Rect* output)
{
    if (control && STARH(control) && output)
        *output = CTL_RECT(control);
    return output;
}

int ROMlib_carbon_get_param_text(ROMlib_carbon_ppc_frame* call)
{
    for (unsigned i = 0; i < 4; ++i) {
        const uint32_t output = call->words[i];
        if (!output)
            continue;
        uint32_t handle, data = 0;
        if (!cfp_clear(output, 1) || !cfp_get(0xaa0 + i * 4, &handle) || (handle && !cfp_get(handle, &data)))
            return 0;
        if (data) {
            const uint8_t* length = ROMlib_carbon_guest_memory(data, 1, 0);
            if (!length || !cfp_move(output, data, *length + 1u))
                return 0;
        }
    }
    return 1;
}

int ROMlib_carbon_std_filter_proc(ROMlib_carbon_ppc_frame* call)
{
    _Alignas(4) uint32_t filter = 0;
    ROMlib_carbon_ppc_frame arguments = {0};
    arguments.words[0] = US_TO_SYN68K(&filter);
    uint32_t result;
    if (!ROMlib_carbon_call_toolbox_vector(0x17a0, 0x3a8, 0x203, &arguments, &result))
        return 0;
    if ((int16_t)result || !filter) {
        call->words[0] = 0;
        return 1;
    }
    if (!ROMlib_carbon_invoke_pascal(CL(filter), 0x0fd0, call, &result))
        return 0;
    call->words[0] = (uint8_t)result;
    return 1;
}

static int carbon_dialog_item_geometry(ROMlib_carbon_ppc_frame* call, bool resize)
{
    const uint32_t* args = call->words;
    DialogPtr dialog = args[0] ? ROMlib_carbon_try_guest_memory(args[0], sizeof(WindowRecord), 0) : NULL;
    uint32_t result = paramErr;
    if (dialog && ROMlib_valid_window(dialog)) {
        result = resize ? ROMlib_size_dialog_item(dialog, args[1], args[2], args[3])
                        : ROMlib_move_dialog_item(dialog, args[1], args[2], args[3]);
    }
    else if (dialog &&
             !ROMlib_carbon_call_toolbox_vector(0x17a0, 0xaba8, resize ? 0x511 : 0x510, call, &result))
        return 0;
    call->words[0] = (int16_t)result;
    return 1;
}

int ROMlib_carbon_move_dialog_item(ROMlib_carbon_ppc_frame* call)
{
    return carbon_dialog_item_geometry(call, false);
}

int ROMlib_carbon_size_dialog_item(ROMlib_carbon_ppc_frame* call)
{
    return carbon_dialog_item_geometry(call, true);
}

static int dialog_memory_call(uint16_t trap_word, uint32_t info, uint32_t first, uint32_t second, uint32_t* result)
{
    const ROMlib_carbon_ppc_frame arguments = {.words = {first, second}};
    return ROMlib_carbon_call_register_vector(0x400 + 4 * (trap_word & 0xff), info, trap_word, 0, &arguments, result);
}

static void* dialog_item_data(uint32_t handle, uint32_t extent, int writing)
{
    uint32_t data;
    return cfp_get(handle, &data) && data ? ROMlib_carbon_guest_memory(data, extent, writing) : NULL;
}

static int dialog_item_offset(const unsigned char* bytes, uint32_t size, unsigned index, uint32_t* offset)
{
    uint32_t cursor = sizeof(INTEGER);
    for (unsigned i = 0; i < index; ++i) {
        if (size - cursor < sizeof(itmstr))
            return 0;
        const itmstr* item = (const itmstr*)(bytes + cursor);
        const unsigned extent = ITEM_LEN(item);
        if (extent > size - cursor)
            return 0;
        cursor += extent;
    }
    *offset = cursor;
    return 1;
}

static OSErr insert_dialog_item(DialogPeek dialog, int index, const itmstr* item)
{
    uint32_t list = CL((uint32_t)(uintptr_t)PP_RAW(dialog->items));
    if (!list) {
        if (!dialog_memory_call(0xa122, 0x33932, sizeof(INTEGER), 0, &list) || !list)
            return memFullErr;
        INTEGER* empty = dialog_item_data(list, sizeof *empty, 1);
        if (!empty)
            return memFullErr;
        *empty = CWC(-1);
        FIELD_ASSIGN_RAW(dialog->items, CL(list));
    }

    uint32_t size, ignored;
    if (!dialog_memory_call(0xa025, 0x133832, list, 0, &size) || size < sizeof(INTEGER) ||
        size > INT32_MAX - sizeof *item)
        return paramErr;
    const unsigned char* before = dialog_item_data(list, size, 0);
    if (!before)
        return paramErr;
    INTEGER last;
    memcpy(&last, before, sizeof last);
    const int count = (int16_t)CW(last) + 1;
    if (count < 0 || count > INT16_MAX || (unsigned)count > (size - sizeof last) / sizeof *item)
        return paramErr;
    if (index > count)
        index = count;
    uint32_t offset;
    if (!dialog_item_offset(before, size, index, &offset))
        return paramErr;

    if (!dialog_memory_call(0xa024, 0x733802, list, size + sizeof *item, &ignored))
        return memFullErr;
    const uint16_t* mem_error = ROMlib_carbon_guest_memory(0x220, sizeof *mem_error, 0);
    if (!mem_error)
        return memFullErr;
    const OSErr status = (int16_t)CW(*mem_error);
    if (status)
        return status;
    unsigned char* bytes = dialog_item_data(list, size + sizeof *item, 1);
    if (!bytes)
        return memFullErr;
    memmove(bytes + offset + sizeof *item, bytes + offset, size - offset);
    memcpy(bytes + offset, item, sizeof *item);
    last = CW(count);
    memcpy(bytes, &last, sizeof last);
    if (DIALOG_EDIT_FIELD(dialog) >= index && DIALOG_EDIT_FIELD(dialog) < INT16_MAX)
        dialog->editField = CW(DIALOG_EDIT_FIELD(dialog) + 1);
    return noErr;
}

int ROMlib_carbon_insert_dialog_item(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0], bounds = call->words[4];
    const int16_t index = (int16_t)call->words[1];
    OSErr result = paramErr;
    if (window && bounds && index >= 0) {
        DialogPeek dialog = ROMlib_carbon_try_guest_memory(window, sizeof *dialog, 1);
        const Rect* rect = ROMlib_carbon_try_guest_memory(bounds, sizeof *rect, 0);
        if (dialog && rect) {
            itmstr item = {0};
            FIELD_ASSIGN_RAW(item.itmhand, CL(call->words[3]));
            item.itmr = *rect;
            item.itmtype = (uint8_t)call->words[2];
            result = insert_dialog_item(dialog, index, &item);
        }
    }
    call->words[0] = (uint32_t)result;
    return 1;
}

int ROMlib_carbon_remove_dialog_items(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0];
    const int first = (int16_t)call->words[1] - 1;
    int count = (int16_t)call->words[2];
    const int dispose = (uint8_t)call->words[3] != 0;
    DialogPeek dialog = window ? ROMlib_carbon_try_guest_memory(window, sizeof *dialog, 1) : NULL;
    call->words[0] = (uint32_t)paramErr;
    if (!dialog || first < 0 || count <= 0)
        return 1;
    const uint32_t list = CL((uint32_t)(uintptr_t)PP_RAW(dialog->items));
    if (!list) {
        call->words[0] = noErr;
        return 1;
    }
    uint32_t size;
    if (!dialog_memory_call(0xa025, 0x133832, list, 0, &size))
        return 0;
    if (size < sizeof(INTEGER) || size > INT32_MAX)
        return 1;
    unsigned char* bytes = dialog_item_data(list, size, 1);
    if (!bytes)
        return 0;
    INTEGER encoded_last;
    memcpy(&encoded_last, bytes, sizeof encoded_last);
    const int total = (int16_t)CW(encoded_last) + 1;
    if (first >= total)
        return 1;
    if (count > total - first)
        count = total - first;
    const int end = first + count;
    uint32_t start_offset, end_offset;
    if (!dialog_item_offset(bytes, size, first, &start_offset) || !dialog_item_offset(bytes, size, end, &end_offset))
        return 1;

    const int edit = DIALOG_EDIT_FIELD(dialog);
    const int replace_edit = edit >= first && edit < end;
    uint32_t flags, ignored, previous_port = 0;
    ROMlib_carbon_reference ref;
    if (dispose || replace_edit) {
        /* The saved Appearance workers own per-item cleanup and reselect the first usable edit control. */
        if (!ROMlib_carbon_probe_reference("CarbonLib", "RemoveDialogItems", &ref) || ref.code_offset != 0x57adc ||
            ref.code_size != 0xb8fb4 || !ref.toc || ref.toc_offset != 0x2f78 || ref.toc_size != 0x10f0c)
            return ROMlib_carbon_report_failure("Missing Carbon dialog item lifecycle workers");
        _Alignas(4) uint32_t saved_port = 0;
        const uint32_t port_output = US_TO_SYN68K(&saved_port);
        if (!cfp_private(&ref, 0xb12f0, &port_output, 1, NULL) || !cfp_private(&ref, 0xb12d8, &window, 1, NULL))
            return 0;
        previous_port = CL(saved_port);
    }

    if (replace_edit) {
        if (!cfp_private(&ref, 0x57028, (const uint32_t[]){window, 0, UINT32_MAX, 0}, 4, NULL))
            return 0;
        bytes = dialog_item_data(list, size, 1);
        if (!bytes)
            return 0;
    }
    if (dispose) {
        if (!dialog_memory_call(0xa069, 0x133812, list, 0, &flags) ||
            !dialog_memory_call(0xa029, 0x133802, list, 0, &ignored))
            return 0;
        bytes = dialog_item_data(list, size, 1);
        if (!bytes)
            return 0;
        uint32_t cursor = start_offset;
        for (int index = first; index < end; ++index) {
            const unsigned extent = ITEM_LEN((const itmstr*)(bytes + cursor));
            const uint32_t args[] = {window, (uint32_t)index, US_TO_SYN68K(bytes + cursor), UINT32_C(0x73686f72)};
            if (!cfp_private(&ref, 0x574f8, args, 4, NULL))
                return 0;
            cursor += extent;
        }
    }

    memmove(bytes + start_offset, bytes + end_offset, size - end_offset);
    encoded_last = CW(total - count - 1);
    memcpy(bytes, &encoded_last, sizeof encoded_last);
    if (dispose && !dialog_memory_call(0xa06a, 0x333802, list, (uint8_t)flags, &ignored))
        return 0;
    if (!dialog_memory_call(0xa024, 0x733802, list, size - (end_offset - start_offset), &ignored))
        return 0;
    if (edit >= end)
        dialog->editField = CW(edit - count);
    if (replace_edit && !cfp_private(&ref, 0x578b0, (const uint32_t[]){window, 0}, 2, NULL))
        return 0;
    if ((dispose || replace_edit) && !cfp_private(&ref, 0xb12d8, &previous_port, 1, NULL))
        return 0;
    call->words[0] = noErr;
    return 1;
}
