/*+===================================================================
  File:      data_browser.h

  Summary:   Adapt Data Browser callback records and typed item data to guest storage.

  Classes:   None.

  Functions: Private PowerPC adapters for Data Browser item data and callback initialization.

  Origin:    CarbonLib 1.4 DataBrowserLib exports, callback record layout and item-data behavior.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMLIB_CARBON_DATA_BROWSER_H
#define ROMLIB_CARBON_DATA_BROWSER_H

#include "control_access.h"
#include "icon_services.h"
#include "menu_access.h"
#include "rsys/cquick.h"

enum {
    browser_track = 0x7472636b,     /* trck */
    browser_menu = 0x6d656e75,      /* menu */
    browser_text = 0x74657874,      /* text */
    browser_icon_text = 0x7469636e, /* ticn */
    browser_icon = 0x69636e72,      /* icnr */
    browser_draw = 0x64727769,      /* drwi */
    browser_date = 0x64617465,      /* date */
    browser_edit = 0x65646974,      /* edit */
    browser_boolean = 0x626f6f6c,   /* bool */
    browser_item_id = 0x23494423,   /* #ID# */
    browser_unimplemented = -4,
    browser_wrong_type = -4974,
    browser_invalid_value = -30581,
    browser_date_overflow = 0x8800,
};

typedef enum {
    browser_scalar,
    browser_record,
    browser_unsigned_menu_value,
    browser_string,
    browser_narrow_date,
    browser_draw_state,
} browser_transfer;

static int browser_result(ROMlib_carbon_ppc_frame* call, uint32 result)
{
    call->words[0] = result;
    return 1;
}

static int browser_read_type(uint32 address, uint32* type)
{
    return address >= 4 && cfp_get(address - 4, type);
}

static uint32 browser_read_word(const void* bytes)
{
    uint32 word;
    memcpy(&word, bytes, sizeof word);
    return CL(word);
}

static void browser_write_word(void* bytes, uint32 word)
{
    word = CL(word);
    memcpy(bytes, &word, sizeof word);
}

static int browser_item_data(ROMlib_carbon_ppc_frame* call, uint32 first_type, unsigned first_offset,
                             uint32 second_type, unsigned second_offset, unsigned width, browser_transfer transfer,
                             int set)
{
    enum { bad_parameter = -50 };

    uint32 value = call->words[1];
    if (set && transfer == browser_draw_state && value > 1)
        return browser_result(call, browser_invalid_value);
    uint32 address = call->words[0];
    uint32 type;
    if (!browser_read_type(address, &type))
        return browser_result(call, bad_parameter);
    if (type != first_type && (!second_type || type != second_type))
        return browser_result(call, browser_wrong_type);

    unsigned offset = type == first_type ? first_offset : second_offset;
    unsigned argument_width = width;
    if (transfer == browser_unsigned_menu_value && type == browser_menu) {
        if (set && value > UINT16_MAX)
            return browser_result(call, browser_invalid_value);
        width = 2;
    }
    if (transfer == browser_narrow_date)
        argument_width = 4;

    unsigned char* item =
        address <= UINT32_MAX - offset ? ROMlib_carbon_try_guest_memory(address + offset, width, set) : NULL;
    int indirect = !set || transfer == browser_record;
    unsigned char* argument = indirect && value ? ROMlib_carbon_try_guest_memory(value, argument_width, !set) : NULL;
    if (!item || (indirect && !argument))
        return browser_result(call, bad_parameter);

    if (transfer == browser_string) {
        uint32 reference = set ? value : browser_read_word(item);
        if (reference) {
            void* object = ROMlib_carbon_try_guest_memory(reference, 8, 1);
            if (!object)
                return browser_result(call, bad_parameter);
            if (!ROMlib_carbon_cf_retain(object))
                return 0;
        }
        browser_write_word(set ? item : argument, reference);
    }
    else if (transfer == browser_narrow_date) {
        if (set) {
            browser_write_word(item, 0);
            browser_write_word(item + 4, value);
        }
        else {
            uint32 high = browser_read_word(item);
            browser_write_word(argument, browser_read_word(item + 4));
            if (high)
                return browser_result(call, browser_date_overflow);
        }
    }
    else if (transfer == browser_record) {
        memmove(set ? item : argument, set ? argument : item, width);
    }
    else if (set) {
        if (transfer == browser_draw_state) {
            uint32 old = browser_read_word(item);
            if (old != 1 && old != value)
                return browser_result(call, browser_invalid_value);
        }
        for (unsigned i = 0; i < width; ++i)
            item[width - i - 1] = (unsigned char)(value >> (8 * i));
    }
    else if (argument_width != width) {
        browser_write_word(argument, ((uint32)item[0] << 8) | item[1]);
    }
    else {
        memmove(argument, item, width);
    }
    return browser_result(call, 0);
}

#define CARBON_BROWSER_FIELD(name, first, offset, second, other, width, kind, set)      \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call)                             \
    {                                                                                   \
        return browser_item_data(call, first, offset, second, other, width, kind, set); \
    }

CARBON_BROWSER_FIELD(SetDataBrowserItemDataText, browser_text, 0, browser_icon_text, 4, 4, browser_string, 1)
CARBON_BROWSER_FIELD(GetDataBrowserItemDataText, browser_text, 0, browser_icon_text, 4, 4, browser_string, 0)
CARBON_BROWSER_FIELD(SetDataBrowserItemDataValue, browser_track, 18, browser_menu, 4, 4, browser_unsigned_menu_value, 1)
CARBON_BROWSER_FIELD(GetDataBrowserItemDataValue, browser_track, 18, browser_menu, 4, 4, browser_unsigned_menu_value, 0)
CARBON_BROWSER_FIELD(SetDataBrowserItemDataItemID, browser_item_id, 0, 0, 0, 4, browser_scalar, 1)
CARBON_BROWSER_FIELD(GetDataBrowserItemDataMenuRef, browser_menu, 0, 0, 0, 4, browser_scalar, 0)
CARBON_BROWSER_FIELD(SetDataBrowserItemDataRGBColor, browser_icon, 4, browser_icon_text, 8, 6, browser_record, 1)
CARBON_BROWSER_FIELD(GetDataBrowserItemDataRGBColor, browser_icon, 4, browser_icon_text, 8, 6, browser_record, 0)
CARBON_BROWSER_FIELD(GetDataBrowserItemDataProperty, browser_edit, 4, 0, 0, 4, browser_scalar, 0)
CARBON_BROWSER_FIELD(SetDataBrowserItemDataDateTime, browser_date, 0, 0, 0, 8, browser_narrow_date, 1)
CARBON_BROWSER_FIELD(GetDataBrowserItemDataDateTime, browser_date, 0, 0, 0, 8, browser_narrow_date, 0)
CARBON_BROWSER_FIELD(SetDataBrowserItemDataMinimum, browser_track, 10, 0, 0, 4, browser_scalar, 1)
CARBON_BROWSER_FIELD(GetDataBrowserItemDataMinimum, browser_track, 10, 0, 0, 4, browser_scalar, 0)
CARBON_BROWSER_FIELD(SetDataBrowserItemDataMaximum, browser_track, 14, 0, 0, 4, browser_scalar, 1)
CARBON_BROWSER_FIELD(GetDataBrowserItemDataMaximum, browser_track, 14, 0, 0, 4, browser_scalar, 0)
CARBON_BROWSER_FIELD(SetDataBrowserItemDataDrawState, browser_draw, 0, 0, 0, 4, browser_draw_state, 1)
CARBON_BROWSER_FIELD(GetDataBrowserItemDataDrawState, browser_draw, 0, 0, 0, 4, browser_scalar, 0)
CARBON_BROWSER_FIELD(SetDataBrowserItemDataButtonValue, browser_draw, 4, 0, 0, 2, browser_scalar, 1)
CARBON_BROWSER_FIELD(GetDataBrowserItemDataButtonValue, browser_draw, 4, 0, 0, 2, browser_scalar, 0)
CARBON_BROWSER_FIELD(SetDataBrowserItemDataBooleanValue, browser_edit, 0, browser_boolean, 0, 1, browser_scalar, 1)
CARBON_BROWSER_FIELD(GetDataBrowserItemDataBooleanValue, browser_edit, 0, browser_boolean, 0, 1, browser_scalar, 0)
CARBON_BROWSER_FIELD(SetDataBrowserItemDataLongDateTime, browser_date, 0, 0, 0, 8, browser_record, 1)
CARBON_BROWSER_FIELD(GetDataBrowserItemDataLongDateTime, browser_date, 0, 0, 0, 8, browser_record, 0)
CARBON_BROWSER_FIELD(SetDataBrowserItemDataIconTransform, browser_icon, 10, browser_icon_text, 14, 2, browser_scalar, 1)
CARBON_BROWSER_FIELD(GetDataBrowserItemDataIconTransform, browser_icon, 10, browser_icon_text, 14, 2, browser_scalar, 0)

#undef CARBON_BROWSER_FIELD

static int carbon_GetDataBrowserItemDataType(ROMlib_carbon_ppc_frame* call)
{
    uint32 type = 0;
    browser_read_type(call->words[0], &type);
    return browser_result(call, type);
}

static int carbon_SetDataBrowserItemDataIcon(ROMlib_carbon_ppc_frame* call)
{
    uint32 address = call->words[0], type;
    if (!browser_read_type(address, &type))
        return browser_result(call, paramErr);
    if (type != browser_icon && type != browser_icon_text)
        return browser_result(call, browser_wrong_type);
    void* value = ROMlib_carbon_try_guest_memory(address, 4, 1);
    if (!value)
        return browser_result(call, paramErr);
    ROMlib_carbon_ppc_frame acquire = *call;
    acquire.words[0] = call->words[1];
    if (acquire.words[0] && !carbon_AcquireIconRef(&acquire))
        return 0;
    browser_write_word(value, call->words[1]);
    return browser_result(call, noErr);
}

static int carbon_SetDataBrowserItemDataMenuRef(ROMlib_carbon_ppc_frame* call)
{
    uint32 address = call->words[0], type;
    if (!browser_read_type(address, &type))
        return browser_result(call, paramErr);
    if (type != browser_menu)
        return browser_result(call, browser_wrong_type);
    void* item = ROMlib_carbon_try_guest_memory(address, 4, 1);
    if (!item)
        return browser_result(call, paramErr);
    const uint32 menu = call->words[1];
    browser_write_word(item, menu);
    if (menu) {
        ROMlib_carbon_ppc_frame retain = *call;
        retain.words[0] = menu;
        if (!carbon_RetainMenu(&retain))
            return 0;
    }
    return browser_result(call, noErr);
}

static int browser_init_callbacks(ROMlib_carbon_ppc_frame* call, unsigned size)
{
    uint32 address = call->words[0];
    unsigned char* record = address ? ROMlib_carbon_try_guest_memory(address, size, 1) : NULL;
    if (!record || browser_read_word(record))
        return browser_result(call, -50);
    memset(record + 4, 0, size - 4);
    return browser_result(call, 0);
}

static int carbon_InitDataBrowserCallbacks(ROMlib_carbon_ppc_frame* call)
{
    return browser_init_callbacks(call, 44);
}

static int carbon_InitDataBrowserCustomCallbacks(ROMlib_carbon_ppc_frame* call)
{
    return browser_init_callbacks(call, 32);
}

typedef enum {
    browser_get_buffer,
    browser_set_buffer,
    browser_set_scalar,
    browser_get_packet,
    browser_set_packet,
    browser_get_scroll,
    browser_get_items,
    browser_get_item_count,
} browser_control_transfer;

static void browser_pack_word(unsigned char* record, unsigned char* scalar, uint32 value, int width)
{
    if (width < 0) {
        browser_write_word(scalar, value);
        value = US_TO_SYN68K(scalar + 4 + width);
    }
    browser_write_word(record, width ? value : 0);
}

static ControlHandle browser_live_control(uint32 address)
{
    ControlHandle control = address ? ROMlib_carbon_try_guest_memory(address, sizeof *control, 0) : NULL;
    return ROMlib_live_control_aux(control) ? control : NULL;
}

static int browser_matches_reference(const ROMlib_carbon_reference* saved, uint32 offset)
{
    return saved->code_offset == offset && saved->code_size == 0x419ac && !saved->toc_offset &&
           saved->toc_size == 0x6744 && saved->toc;
}

static int browser_reference(const char* name, uint32 offset, ROMlib_carbon_reference* saved)
{
    return ROMlib_carbon_probe_reference("DataBrowserLib", name, saved) && !browser_matches_reference(saved, offset);
}

static int carbon_CreateDataBrowserControl(ROMlib_carbon_ppc_frame* call)
{
    const uint32 output = call->words[3];
    if (!output || !cfp_clear(output, 4))
        return browser_result(call, paramErr);
    WindowPtr window = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], sizeof(WindowRecord), 0) : NULL;
    const Rect* bounds = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], sizeof *bounds, 0) : NULL;
    if (!ROMlib_valid_window(window) || !bounds)
        return browser_result(call, paramErr);
    Rect rect = *bounds;

    ROMlib_carbon_reference saved;
    if (!ROMlib_carbon_load_reference("DataBrowserLib", "CreateDataBrowserControl", &saved))
        return browser_result(call, browser_unimplemented);
    if (!browser_matches_reference(&saved, 0x38728))
        return ROMlib_carbon_invoke_frame(saved.function, call);

    /* The initialized fragment supplies the runtime-owned CDEF descriptor. */
    uint32 type = cfp_toc(&saved, 0x5ce8), definition = cfp_toc(&saved, 0x884);
    if (type || !definition)
        return ROMlib_carbon_invoke_frame(saved.function, call);
    ROMlib_carbon_ppc_frame collection = {0};
    if (!carbon_NewCollection(&collection))
        return 0;
    if (!collection.words[0])
        return browser_result(call, memFullErr);

    _Alignas(16) uint32 style = CL(call->words[2]);
    ROMlib_carbon_ppc_frame item = {0};
    item.words[0] = collection.words[0];
    item.words[1] = 0x13;
    item.words[3] = sizeof style;
    item.words[4] = US_TO_SYN68K(&style);
    if (!carbon_AddCollectionItem(&item)) {
        carbon_DisposeCollection(&collection);
        return 0;
    }
    uint32 control = 0;
    int32 status = item.words[0];
    if (!status)
        status = create_custom_control(window, &rect, definition, collection.words[0], &control);
    if (!carbon_DisposeCollection(&collection))
        return 0;
    if (status)
        return browser_result(call, status);

    if (!cfp_write(output, control)) {
        C_DisposeControl(SYN68K_TO_US(control));
        return browser_result(call, paramErr);
    }
    ROMlib_carbon_ppc_frame visibility = {0};
    visibility.words[0] = control;
    visibility.words[1] = 1;
    if (!ROMlib_carbon_call_romaw_service("SetControlVisibility", &visibility))
        return 0;
    uint32 drag = cfp_toc(&saved, 0x34c);
    const uint32 drag_arguments[] = {control, 1};
    if (drag && !cfp_callback(drag, drag_arguments, NELEM(drag_arguments), NULL))
        return 0;
    return browser_result(call, noErr);
}

/* The original Data Browser CDEF owns the control state. These wrappers only
   translate the public arguments to its control-data record. */
static int browser_control_data(ROMlib_carbon_ppc_frame* call, const char* name, uint32 offset, unsigned tag,
                                unsigned size, browser_control_transfer transfer, unsigned required, int first_width,
                                int second_width, int insert_key)
{
    ROMlib_carbon_reference saved;
    if (browser_reference(name, offset, &saved))
        return ROMlib_carbon_invoke_frame(saved.function, call);

    const uint32 first = insert_key ? 0 : call->words[1];
    const uint32 second = call->words[insert_key ? 1 : 2];
    if (((required & 1) && !first) || ((required & 2) && !second) || (required == 4 && !first && !second))
        return browser_result(call, paramErr);
    ControlHandle control = browser_live_control(call->words[0]);
    if (!control)
        return browser_result(call, -30599);

    const int writing =
        transfer == browser_set_buffer || transfer == browser_set_scalar || transfer == browser_set_packet;
    _Alignas(32) unsigned char packet[24] = {0};
    Byte* data = packet;
    switch (transfer) {
    case browser_get_buffer:
    case browser_set_buffer:
        data = first ? ROMlib_carbon_try_guest_memory(first, size, !writing) : NULL;
        if (!data)
            return browser_result(call, paramErr);
        break;
    case browser_set_scalar:
        for (unsigned i = 0; i < size; ++i)
            packet[size - 1 - i] = (unsigned char)(first >> (8 * i));
        break;
    case browser_get_packet:
    case browser_set_packet:
        browser_pack_word(packet, packet + 16, first, first_width);
        if (size > 4)
            browser_pack_word(packet + 4, packet + 20, second, second_width);
        if (size > 8)
            browser_write_word(packet + 8, call->words[3]);
        if (size > 12)
            browser_write_word(packet + 12, call->words[4]);
        break;
    case browser_get_items:
    case browser_get_item_count:
        browser_write_word(packet, first);
        browser_write_word(packet + 4, call->words[3]);
        if (transfer == browser_get_items) {
            browser_write_word(packet + 8, call->words[4]);
            packet[12] = (Byte)second;
        }
        else {
            packet[8] = (Byte)second;
            browser_write_word(packet + 12, call->words[4]);
        }
        break;
    case browser_get_scroll:
        break;
    }
    const int16 status =
        ROMlib_control_data(control, 0, tag, size, data, NULL, writing ? ROMLIB_CONTROL_WRITE : ROMLIB_CONTROL_READ);
    if (!status && transfer == browser_get_scroll) {
        if ((first && !cfp_write(first, browser_read_word(packet))) ||
            (second && !cfp_write(second, browser_read_word(packet + 4))))
            return 0;
    }
    return browser_result(call, status);
}

#define BROWSER_CONTROL(name, offset, tag, size, transfer, required, first_width, second_width, insert_key)        \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call)                                                        \
    {                                                                                                              \
        return browser_control_data(call, #name, offset, tag, size, transfer, required, first_width, second_width, \
                                    insert_key);                                                                   \
    }

#include "data_browser_control_entries.h"

#undef BROWSER_CONTROL

static int browser_send(ROMlib_carbon_ppc_frame* call, ControlHandle control, int16 message, uint32 parameter)
{
    return browser_result(call, ROMlib_send_control_message(control, message, parameter));
}

static int carbon_SendControlMessage(ROMlib_carbon_ppc_frame* call)
{
    ControlHandle control = browser_live_control(call->words[0]);
    return control ? browser_send(call, control, (int16)call->words[1], call->words[2]) : browser_result(call, -30599);
}

enum {
    browser_edit_enabled = 1000,
    browser_add_items = 1002,
    browser_visit_items = 1005,
    browser_remove_items = 1006,
    browser_add_column = 1011,
    browser_autosize_columns = 1013,
};

static int browser_message(ROMlib_carbon_ppc_frame* call, const char* name, uint32 offset, int16 message,
                           unsigned words, int byte_argument)
{
    ROMlib_carbon_reference saved;
    if (browser_reference(name, offset, &saved))
        return ROMlib_carbon_invoke_frame(saved.function, call);
    if (message == browser_add_items && !call->words[2])
        return browser_result(call, noErr);
    ControlHandle control = browser_live_control(call->words[0]);
    if (!control)
        return browser_result(call, message == browser_edit_enabled ? 0 : -30599);
    if (message == browser_remove_items && call->words[2] && !call->words[3])
        return browser_result(call, paramErr);
    if (message == browser_add_column) {
        _Alignas(16) uint32 style = CLC(0x3f3f3f3f);
        ROMlib_control_data(control, 0, 0x13, sizeof style, (Byte*)&style, NULL, ROMLIB_CONTROL_READ);
        if (CL(style) != TICK("lstv") || !call->words[1])
            return browser_result(call, paramErr);
    }
    if (!words)
        return browser_send(call, control, message, message == browser_autosize_columns ? 0 : call->words[1]);

    _Alignas(32) uint32 record[5] = {0};
    if (message == browser_visit_items) {
        record[0] = CL(call->words[1]);
        record[1] = CL(call->words[3]);
        record[2] = CL(call->words[4]);
        record[3] = CL(call->words[5]);
    }
    else
        for (unsigned i = 0; i < words; ++i)
            record[i] = CL(call->words[i + 1]);
    if (byte_argument)
        ((Byte*)&record[words])[0] = (Byte)call->words[byte_argument];
    return browser_send(call, control, message, US_TO_SYN68K(record));
}

static int carbon_IsDataBrowserItemSelected(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference saved;
    if (browser_reference("IsDataBrowserItemSelected", 0x3914c, &saved))
        return ROMlib_carbon_invoke_frame(saved.function, call);
    ControlHandle control = browser_live_control(call->words[0]);
    if (!control)
        return browser_result(call, 0);
    _Alignas(16) uint32 record[3] = {CL(call->words[1]), 0, 0};
    record[1] = CL(US_TO_SYN68K(record + 2));
    ROMlib_control_data(control, 0, 0x19, 8, (Byte*)record, NULL, ROMLIB_CONTROL_READ);
    return browser_result(call, CL(record[2]) & 1);
}

#define BROWSER_MESSAGE(name, offset, message, words, byte_argument)                \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call)                         \
    {                                                                               \
        return browser_message(call, #name, offset, message, words, byte_argument); \
    }

BROWSER_MESSAGE(EnableDataBrowserEditCommand, 0x38ebc, 1000, 0, 0)
BROWSER_MESSAGE(ExecuteDataBrowserEditCommand, 0x38f1c, 1001, 0, 0)
BROWSER_MESSAGE(AddDataBrowserItems, 0x38ab8, 1002, 4, 0)
BROWSER_MESSAGE(RevealDataBrowserItem, 0x38b3c, 1003, 2, 3)
BROWSER_MESSAGE(UpdateDataBrowserItems, 0x38b9c, 1004, 5, 0)
BROWSER_MESSAGE(ForEachDataBrowserItem, 0x38ff0, 1005, 4, 2)
BROWSER_MESSAGE(RemoveDataBrowserItems, 0x38c18, 1006, 4, 0)
BROWSER_MESSAGE(OpenDataBrowserContainer, 0x38ca4, 1007, 0, 0)
BROWSER_MESSAGE(SortDataBrowserContainer, 0x38d04, 1008, 1, 2)
BROWSER_MESSAGE(CloseDataBrowserContainer, 0x38d60, 1009, 0, 0)
BROWSER_MESSAGE(MoveDataBrowserSelectionAnchor, 0x38e48, 1010, 1, 2)
BROWSER_MESSAGE(AddDataBrowserListViewColumn, 0x3a810, 1011, 2, 0)
BROWSER_MESSAGE(RemoveDataBrowserTableViewColumn, 0x3a2ac, 1012, 1, 0)
BROWSER_MESSAGE(AutoSizeDataBrowserListViewColumns, 0x3a8f8, 1013, 0, 0)

#undef BROWSER_MESSAGE

#endif
