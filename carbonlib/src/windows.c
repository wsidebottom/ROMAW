/*+===================================================================
  File:      windows.c

  Summary:   Window properties, class traversal, attributes, titles, and shared Control Manager properties.

  Classes:   None.

  Functions: ROMlib_carbon_set_port_window_port, ROMlib_carbon_set_window_standard_state,
             ROMlib_carbon_set_window_user_state, ROMlib_carbon_window_from_port, ROMlib_carbon_window_hilited,
             ROMlib_carbon_clear_window_title, ROMlib_carbon_control_property, ROMlib_carbon_window_port,
             ROMlib_carbon_window_port_bounds, ROMlib_carbon_window_owners, ROMlib_carbon_window_closed,
             ROMlib_carbon_windows_move_structure,
             ROMlib_carbon_windows_set_bounds, ROMlib_carbon_windows_get_content_color,
             ROMlib_carbon_windows_set_content_color,
             ROMlib_carbon_window_activation, ROMlib_carbon_window_group,
             ROMlib_carbon_window_property, ROMlib_carbon_window_standard_state,
             ROMlib_carbon_window_update_pending, ROMlib_carbon_window_user_state,
             ROMlib_carbon_windows_compatibility, ROMlib_carbon_window_saved_screen_bits

  Origin:    Existing repository source in packages/carbonlib/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Native implementations of the small window-accessor group. Reference
   offsets identify CarbonLib 1.4 code, not copied implementation bytes. */
#include "rsys/common.h"
#include "rsys/everything.h"
#include "rsys/wind.h"
#include "rsys/cquick.h"
#include "rsys/layers.h"
#include "rsys/configprefs.h"
#include "rsys/mman.h"
#include "rsys/prefs.h"
#include "internal.h"
#include "cf_batch.h"
#include "cfx_internal.h"

GrafPtr ROMlib_carbon_window_port(WindowPtr window)
{
    return (GrafPtr)window;
}

BOOLEAN ROMlib_carbon_window_hilited(WindowPtr window)
{
    return window && WINDOW_HILITED(window) != 0;
}

BOOLEAN ROMlib_carbon_window_update_pending(WindowPtr window)
{
    RgnHandle region = window ? WINDOW_UPDATE_REGION(window) : NULL;
    return region && !C_EmptyRgn(region);
}

static WStateData* window_state(WindowPtr window)
{
    Handle data = window ? WINDOW_DATA(window) : NULL;
    return data && GetHandleSize(data) >= sizeof(WStateData) ? (WStateData*)STARH(data) : NULL;
}

Rect* ROMlib_carbon_window_standard_state(WindowPtr window, Rect* bounds)
{
    WStateData* state = window_state(window);
    if (state && bounds)
        *bounds = state->stdState;
    return bounds;
}

Rect* ROMlib_carbon_window_user_state(WindowPtr window, Rect* bounds)
{
    WStateData* state = window_state(window);
    if (state && bounds)
        *bounds = state->userState;
    return bounds;
}

void ROMlib_carbon_set_window_standard_state(WindowPtr window, const Rect* bounds)
{
    WStateData* state = window_state(window);
    if (state && bounds)
        state->stdState = *bounds;
}

void ROMlib_carbon_set_window_user_state(WindowPtr window, const Rect* bounds)
{
    WStateData* state = window_state(window);
    if (state && bounds)
        state->userState = *bounds;
}

void ROMlib_carbon_set_port_window_port(WindowPtr window)
{
    if (window)
        C_SetPort((GrafPtr)window);
}

Rect* ROMlib_carbon_window_port_bounds(WindowPtr window, Rect* bounds)
{
    if (window && bounds)
        *bounds = PORT_RECT(window);
    return bounds;
}

WindowPtr ROMlib_carbon_window_from_port(GrafPtr port)
{
    if (!port)
        port = thePort;
    for (WindowPeek window = ROMlib_window_first_all(); window; window = ROMlib_window_next_all(window))
        if ((GrafPtr)window == port)
            return (WindowPtr)window;
    return NULL;
}

enum {
    window_invalid = -5600,
    window_attribute_unsupported = -5601,
    control_invalid = -30599,
    window_reserved_property = -5603,
    window_missing_property = -5604,
    window_button_error = -9868,
    collection_missing_item = -5751,
    collection_persistent = 0x40000000,
    window_system_creator = 0x6d616373
};

static int window_result(ROMlib_carbon_ppc_frame* call, int32_t status)
{
    call->words[0] = (uint32_t)status;
    return 1;
}

void ROMlib_carbon_window_closed(uint32_t window)
{
    ROMlib_carbon_reference ref;
    if (ROMlib_carbon_probe_reference("WindowsLib", "RecalculateAllWindowTitleWidths", &ref) &&
        ref.code_offset == 0x2488 && ref.code_size == 0x1cd10 && ref.toc && !ref.toc_offset &&
        ref.toc_size == 0x27ec && cfp_toc(&ref, 0x241c)) {
        /* WindowsLib removes the hash entry and releases its collection before
           freeing the window. The native CloseWindow path owns the rest. */
        cfp_private(&ref, 0x2150, &window, 1, NULL);
    }
}

static int windows_metadata_record(const ROMlib_carbon_reference* ref, uint32_t window, uint32_t* record)
{
    *record = 0;
    if (!window)
        return 1;
    const uint32_t table = cfp_toc(ref, 0x241c);
    if (!table)
        return 1;
    _Alignas(16) const uint32_t key = CL(window);
    _Alignas(16) uint32_t item = 0;
    uint32_t status;
    const uint32_t arguments[] = {table, US_TO_SYN68K(&key), US_TO_SYN68K(&item)};
    if (!cfp_private(ref, 0x1bac0, arguments, 3, &status))
        return 0;
    if (!status)
        *record = CL(item);
    return 1;
}

static int windows_visual_class(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0], output = call->words[1];
    if (!window)
        return window_result(call, window_invalid);
    uint32_t descriptor, status;
    if (!cfp_private(ref, 0x2370, &window, 1, &descriptor))
        return 0;
    if (!descriptor)
        return window_result(call, window_invalid);
    if (!output)
        return window_result(call, paramErr);
    _Alignas(16) uint32_t flags = 0;
    const uint32_t arguments[] = {window, US_TO_SYN68K(&flags)};
    if (!cfp_private(ref, 0x1c2a0, arguments, 2, &status))
        return 0;
    uint32_t visual_class;
    if (status) {
        const WindowPeek record = ROMlib_carbon_guest_memory(window, sizeof(WindowRecord), 0);
        if (!record)
            return 0;
        visual_class = WINDOW_KIND(record) == 2 ? 3 : 6;
    }
    else if (CL(flags) & 8)
        visual_class = 1 + !(CL(flags) & 0x20) * 2 + !!(CL(flags) & 0x40);
    else {
        const uint16_t* style = ROMlib_carbon_guest_memory(descriptor + 0x1e, sizeof *style, 0);
        if (!style)
            return 0;
        uint16_t wdef = CW(*style) >> 4;
        visual_class = wdef == 0x42 || wdef == 0x43 || wdef == 0x7c ? 5 : 6;
    }
    return cfp_write(output, visual_class) && window_result(call, status);
}

static int windows_double_click_action(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call, int set)
{
    const uint32_t address = US_TO_SYN68K(ref->toc) + 0x768;
    if (!set) {
        uint32_t action;
        return cfp_get(address, &action) && window_result(call, action);
    }
    const uint32_t action = call->words[0];
    if (action > 3)
        return 1;
    _Alignas(16) uint32_t preferences = 0;
    const uint32_t pointer = US_TO_SYN68K(&preferences);
    uint32_t status;
    if (!cfp_write(address, action) || !cfp_private(ref, 0x114c0, &pointer, 1, &status))
        return 0;
    if (status)
        return 1;
    _Alignas(16) const uint32_t encoded = CL(action);
    const uint32_t arguments[] = {CL(preferences), 0x74636c6b, 0, 4, US_TO_SYN68K(&encoded)};
    return cfp_private(ref, 0x1ae90, arguments, 5, NULL);
}

static int romaw_window_metadata(uint32_t offset, ROMlib_carbon_ppc_frame* call)
{
    if (offset == 0x7464 || offset == 0x74dc) {
        static uint32_t action;
        if (offset == 0x7464) {
            if (call->words[0] <= 3) {
                action = call->words[0];
                if (ROMlib_configfilename) {
                    const char value[] = {(char)('0' + action), '\0'};
                    config_prefs_set(ROMlib_configfilename, "WindowManager", "DoubleClickAction", value);
                }
            }
            return 1;
        }
        char value[2];
        if (ROMlib_configfilename &&
            config_prefs_get(ROMlib_configfilename, "WindowManager", "DoubleClickAction", value, sizeof value) == 1 &&
            value[0] >= '0' && value[0] <= '3')
            action = value[0] - '0';
        return window_result(call, action);
    }
    const uint32_t address = call->words[0], argument = call->words[1];
    WindowPtr window = address ? ROMlib_carbon_try_guest_memory(address, sizeof(WindowRecord), 0) : NULL;
    const int valid = window && ROMlib_valid_window(window);
    if (offset == 0x330c) {
        if (!argument)
            return 1;
        ProcessSerialNumber* serial = ROMlib_carbon_guest_memory(argument, sizeof *serial, 1);
        if (!serial)
            return 0;
        memset(serial, 0, sizeof *serial);
        if (valid)
            C_GetCurrentProcess(serial);
        return 1;
    }
    if (!valid)
        return window_result(call, offset == 0x3474 ? 0 : window_invalid);
    if (offset == 0x548) {
        if (!argument)
            return window_result(call, paramErr);
        const uint32_t visual_class = WINDOW_KIND(window) == dialogKind ? 3 : 6;
        return cfp_write(argument, visual_class) && window_result(call, noErr);
    }
    ROMlib_window_metadata* metadata = ROMlib_window_get_metadata(window);
    if (!metadata)
        return window_result(call, offset == 0x3474 ? 0 : memFullErr);
    if (offset == 0x33bc)
        metadata->modified = (argument & 1) != 0;
    else if (offset == 0x3474)
        return window_result(call, metadata->modified);
    else if (offset == 0x5c9c)
        metadata->drop_threshold = (uint8_t)argument;
    else {
        if (!argument)
            return window_result(call, paramErr);
        uint8_t* output = ROMlib_carbon_guest_memory(argument, 1, 1);
        if (!output)
            return 0;
        *output = metadata->drop_threshold;
    }
    return window_result(call, noErr);
}

int ROMlib_carbon_window_saved_screen_bits(const char* name, uint32_t offset, ROMlib_carbon_ppc_frame* call)
{
    enum { get_enabled = 0xd798, set_enabled = 0xd7a0, move_bits = 0xde28, representative_pixmap = 0xdef4 };

    uint32_t node = call->words[0];
    if ((offset == move_bits || offset == representative_pixmap) && !node)
        return window_result(call, 0);
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_load_reference("WindowsLib", name, &ref))
        return ROMlib_carbon_report_failure("The saved WindowsLib screen bits are unavailable");
    if (ref.code_offset != offset || ref.code_size != 0x1cd10 || !ref.toc || ref.toc_offset || ref.toc_size != 0x27ec)
        return ROMlib_carbon_invoke_frame(ref.function, call);

    if (offset == get_enabled || offset == set_enabled) {
        Byte* enabled = ROMlib_carbon_guest_memory(US_TO_SYN68K(ref.toc) + 0x76c, 1, offset == set_enabled);
        if (!enabled)
            return 0;
        if (offset == get_enabled)
            return window_result(call, *enabled);
        *enabled = (uint8_t)call->words[0] != 0;
        return window_result(call, noErr);
    }
    if (offset == representative_pixmap) {
        uint32_t world, pixmap = 0;
        if (node > UINT32_MAX - 0x18 || !cfp_get(node + 0x14, &world))
            return 0;
        return (!world || cfp_private(&ref, 0x1b6b8, &world, 1, &pixmap)) && window_result(call, pixmap);
    }

    /* SaveScreenBits creates one node per device. Each destination's top
       and left are relative to the new origin, while its extent stays fixed. */
    const int16_t left = (int16_t)call->words[1], top = (int16_t)call->words[2];
    for (unsigned devices = 0; node && devices < UINT16_MAX; ++devices) {
        if (node > UINT32_MAX - 0x18)
            return 0;
        Rect* bounds = ROMlib_carbon_guest_memory(node + 4, sizeof *bounds, 1);
        uint32_t next;
        if (!bounds || !cfp_get(node + 0x10, &next) || next == node)
            return 0;
        const uint32_t move[] = {node + 4, (int16_t)(left - CW(bounds->left)), (int16_t)(top - CW(bounds->top))};
        if (!cfp_private(&ref, 0x1b760, move, NELEM(move), NULL))
            return 0;
        node = next;
    }
    return !node && window_result(call, noErr);
}

int ROMlib_carbon_windows_compatibility(const char* name, uint32_t offset, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_probe_reference("WindowsLib", name, &ref))
        return romaw_window_metadata(offset, call);
    if (ref.code_offset != offset || ref.code_size != 0x1cd10 || !ref.toc || ref.toc_offset || ref.toc_size != 0x27ec)
        return ROMlib_carbon_invoke_frame(ref.function, call);
    if (offset == 0x548)
        return windows_visual_class(&ref, call);
    if (offset == 0x7464 || offset == 0x74dc)
        return windows_double_click_action(&ref, call, offset == 0x7464);

    const uint32_t window = call->words[0], argument = call->words[1];
    uint32_t record;
    if (!windows_metadata_record(&ref, window, &record))
        return 0;
    if (!record && window)
        return romaw_window_metadata(offset, call);
    if (offset == 0x330c) {
        if (!argument)
            return 1;
        if (!record)
            return cfp_clear(argument, 8);
        const uint32_t process = record + 0xc;
        uint32_t serial;
        return cfp_private(&ref, 0x1bc58, &process, 1, &serial) && cfp_move(argument, serial, 8);
    }
    if (!record)
        return window_result(call, offset == 0x3474 ? 0 : window_invalid);
    if (offset == 0x5c9c)
        return cfp_write(record + 0x68, (uint8_t)argument) && window_result(call, noErr);
    if (offset == 0x5d20) {
        if (!argument)
            return window_result(call, paramErr);
        uint8_t* output = ROMlib_carbon_guest_memory(argument, 1, 1);
        uint32_t threshold;
        if (!output || !cfp_get(record + 0x68, &threshold))
            return 0;
        *output = threshold;
        return window_result(call, noErr);
    }
    uint8_t* flags = ROMlib_carbon_guest_memory(record + 0x60, 1, offset == 0x33bc);
    if (!flags)
        return 0;
    const uint8_t modified = (*flags >> 1) & 1;
    if (offset == 0x3474)
        return window_result(call, modified);
    if (modified != (uint8_t)argument) {
        *flags = (*flags & ~2) | ((argument & 1) << 1);
        const uint32_t redraw[] = {window, (uint8_t)argument != 0};
        if (!cfp_private(&ref, 0x62b8, redraw, 2, NULL))
            return 0;
    }
    return window_result(call, noErr);
}

static int new_property_collection(const ROMlib_carbon_reference* ref, uint32_t object, int control, uint32_t* item)
{
    if (!control)
        return cfp_private(ref, 0x56530, NULL, 0, item);

    /* Allocate the child in the control's zone and restore the original zone. */
    uint32_t zone, control_zone;
    if (!cfp_private(ref, 0xb53b8, NULL, 0, &zone) || !cfp_private(ref, 0xb5400, &object, 1, &control_zone) ||
        !cfp_private(ref, 0xb5448, &control_zone, 1, NULL))
        return 0;
    const int allocated = cfp_private(ref, 0xb6240, NULL, 0, item);
    return cfp_private(ref, 0xb5448, &zone, 1, NULL) && allocated;
}

static int object_collection(const ROMlib_carbon_reference* ref, uint32_t object, int control, int create,
                             uint32_t* collection)
{
    const uint32_t root_slot = US_TO_SYN68K(ref->toc) + (control ? 0x6cd4 : 0x6d0c);
    uint32_t root, status;
    *collection = 0;
    if (!cfp_get(root_slot, &root))
        return 0;
    if (create && !root && (!cfp_private(ref, 0x56530, NULL, 0, &root) || !cfp_write(root_slot, root)))
        return 0;
    if (!root)
        return 1;

    _Alignas(8) uint32_t stored = 0;
    const uint32_t lookup[] = {root, object, 0, 0, US_TO_SYN68K(&stored)};
    if (!cfp_private(ref, 0xb6048, lookup, 5, &status))
        return 0;
    if ((int32_t)status != collection_missing_item) {
        if (!status)
            *collection = CL(stored);
        return 1;
    }
    uint32_t item;
    if (!new_property_collection(ref, object, control, &item))
        return 0;
    if (!item)
        return 1;
    const uint32_t defaults[] = {item, collection_persistent, 0};
    stored = CL(item);
    const uint32_t add[] = {root, object, 0, sizeof stored, US_TO_SYN68K(&stored)};
    if (!cfp_private(ref, 0xb7908, defaults, 3, NULL) || !cfp_private(ref, 0xb6060, add, 5, &status))
        return 0;
    if (status) {
        const uint32_t dispose[] = {item};
        return cfp_private(ref, 0xb6270, dispose, 1, NULL);
    }
    *collection = item;
    return 1;
}

static int object_property(const ROMlib_carbon_reference* ref, enum carbon_property_operation operation,
                           uint32_t object, int control, uint32_t creator, const uint32_t* args, uint32_t* status)
{
    const uint32_t tag = args[0], size = args[1];
    *status = paramErr;
    if ((operation == property_get &&
         ((size && !args[3]) || (size && !ROMlib_carbon_guest_memory(args[3], size, 1)))) ||
        ((operation == property_size || operation == property_attributes) && !args[1]) ||
        (operation == property_set && (!args[2] || (size && !ROMlib_carbon_guest_memory(args[2], size, 0)))))
        return 1;

    uint32_t collection;
    if (!object_collection(ref, object, control, operation == property_set, &collection))
        return 0;
    if (!collection) {
        *status = operation == property_set ? memFullErr : window_missing_property;
        return 1;
    }
    const uint32_t item[] = {collection, creator, tag};
    switch (operation) {
    case property_set: {
        const uint32_t add[] = {item[0], item[1], item[2], size, args[2]};
        return cfp_private(ref, 0xb6060, add, 5, status);
    }
    case property_remove:
        if (!cfp_private(ref, 0xb73b0, item, 3, status))
            return 0;
        break;
    case property_attributes: {
        _Alignas(8) uint32_t attributes = 0;
        const uint32_t get[] = {item[0], item[1], item[2], 0, 0, US_TO_SYN68K(&attributes)};
        if (!cfp_private(ref, 0xb62e8, get, 6, status) ||
            (!*status && !cfp_write(args[1], (CL(attributes) & collection_persistent) != 0)))
            return 0;
        break;
    }
    case property_change: {
        const uint32_t set = args[1] & 1 ? collection_persistent : 0;
        const uint32_t clear = args[2] & 1 ? collection_persistent : 0;
        uint32_t change[] = {item[0], item[1], item[2], set, set};
        if (!cfp_private(ref, 0xb7920, change, 5, status))
            return 0;
        if (!*status) {
            change[3] = clear;
            change[4] = 0;
            if (!cfp_private(ref, 0xb7920, change, 5, status))
                return 0;
        }
        break;
    }
    case property_get:
    case property_size: {
        _Alignas(8) uint32_t actual = CL(operation == property_get ? size : 0);
        const uint32_t get[] = {item[0], item[1], item[2], US_TO_SYN68K(&actual),
                                operation == property_get && size ? args[3] : 0};
        if (!cfp_private(ref, 0xb6048, get, 5, status))
            return 0;
        const uint32_t output = operation == property_get ? args[2] : size;
        if (!*status && output && !cfp_write(output, CL(actual)))
            return 0;
        break;
    }
    default:
        return 0;
    }
    if (*status && (operation == property_get || operation == property_size || operation == property_remove ||
                    (int32_t)*status == collection_missing_item))
        *status = window_missing_property;
    return 1;
}

static int mark_window_button(const ROMlib_carbon_reference* ref, uint32_t control, uint32_t tag, Byte marked)
{
    uint32_t visible;
    const uint32_t args[] = {control};
    if (!cfp_private(ref, 0xb7b30, args, 1, &visible))
        return 0;
    if (visible && !cfp_private(ref, 0xb7b48, args, 1, NULL))
        return 0;
    const uint32_t change[] = {control, 0, tag, 1, US_TO_SYN68K(&marked)};
    const int updated = cfp_private(ref, 0xb69d8, change, 5, NULL);
    return (!visible || cfp_private(ref, 0xb7b60, args, 1, NULL)) && updated;
}

static int window_button(const ROMlib_carbon_reference* ref, enum carbon_property_operation operation, uint32_t tag,
                         ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0], button = call->words[1];
    const int get = operation == property_button_get;
    if (get && (!button || !cfp_write(button, 0)))
        return window_result(call, paramErr);
    if (!window)
        return window_result(call, paramErr);

    uint32_t valid, status;
    if (!cfp_private(ref, 0xb2c28, &window, 1, &valid))
        return 0;
    if (!valid)
        return window_result(call, paramErr);
    if (!get && button) {
        if (!cfp_private(ref, 0xb1b30, &button, 1, &valid))
            return 0;
        if (!valid)
            return window_result(call, paramErr);
    }

    _Alignas(8) uint32_t old = 0;
    const uint32_t read[] = {tag, sizeof old, 0, get ? button : US_TO_SYN68K(&old)};
    if (!object_property(ref, property_get, window, 0, window_system_creator, read, &status))
        return 0;
    if (get)
        return window_result(call, !status || (int32_t)status == window_missing_property ? noErr : window_button_error);
    if (!status && old && CL(old) != button && !mark_window_button(ref, CL(old), tag, 0))
        return 0;
    _Alignas(8) uint32_t stored = CL(button);
    const uint32_t write[] = {tag, sizeof stored, US_TO_SYN68K(&stored)};
    if (!object_property(ref, button ? property_set : property_remove, window, 0, window_system_creator, write,
                         &status))
        return 0;
    if (!status && button && !mark_window_button(ref, button, tag, 1))
        return 0;
    return window_result(call, status ? window_button_error : noErr);
}

enum {
    window_attributes_property = 0x6372696e,
    window_title_conversion_error = -4960,
    window_title_field = 9,
    window_alternate_title_field = 10,
    window_owners_field = 13
};

static int window_info_record(const ROMlib_carbon_reference* ref, enum carbon_property_operation operation,
                              uint32_t window, uint32_t address, uint32_t* status)
{
    const uint32_t size = 0x44;
    const uint32_t get[] = {window_attributes_property, size, 0, address};
    const uint32_t set[] = {window_attributes_property, size, address};
    return object_property(ref, operation, window, 0, window_system_creator, operation == property_get ? get : set,
                           status);
}

static int initialize_window_info(const ROMlib_carbon_reference* ref, uint32_t window, uint32_t* record)
{
    uint32_t parent, application_parent, defaults;
    const uint32_t toc = US_TO_SYN68K(ref->toc);
    if (!cfp_private(ref, 0xb6210, &window, 1, &parent) || !cfp_get(toc - 0x1690, &application_parent) ||
        !cfp_get(application_parent, &application_parent))
        return 0;
    if (parent == application_parent)
        record[1] = CLC(1);
    else {
        if (!cfp_get(toc - 0x1698, &defaults))
            return 0;
        const void* initial = ROMlib_carbon_guest_memory(defaults, 8, 0);
        if (!initial)
            return 0;
        memcpy(record, initial, 8);
    }
    record[window_owners_field] = CLC(1);
    record[14] = CLC(2);
    record[16] = CLC(0x00080000);
    return 1;
}

int ROMlib_carbon_window_owners(uint32_t window, int change, uint32_t* owners, uint32_t* status)
{
    *owners = 0;
    WindowPtr handle = ROMlib_carbon_try_guest_memory(window, sizeof(WindowRecord), 0);
    *status = window_invalid;
    if (!window || !handle || !ROMlib_valid_window(handle))
        return 1;
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_probe_reference("CarbonLib", "GetWindowOwnerCount", &ref) || ref.code_offset != 0xd3ac ||
        ref.code_size != 0xb8fb4 || !ref.toc || ref.toc_offset != 0x2f78 || ref.toc_size != 0x10f0c) {
        *status = ROMlib_window_change_owners(handle, change, owners);
        return 1;
    }

    _Alignas(128) uint32_t record[17] = {0};
    const uint32_t address = US_TO_SYN68K(record);
    if (!window_info_record(&ref, property_get, window, address, status))
        return 0;
    const int missing = *status != 0;
    if (missing) {
        *status = ROMlib_window_change_owners(handle, 0, owners);
        if (*status)
            return 1;
    }
    else
        *owners = CL(record[window_owners_field]);
    if (!change)
        return 1;
    if (change < 0 && *owners <= 1) {
        *owners = 0;
        return 1;
    }
    if (change > 0 && *owners == UINT32_MAX) {
        *status = paramErr;
        return 1;
    }
    if (missing && !initialize_window_info(&ref, window, record))
        return 0;
    const uint32_t count = change > 0 ? *owners + 1 : *owners - 1;
    record[window_owners_field] = CL(count);
    *owners = count;
    return window_info_record(&ref, property_set, window, address, status);
}

static int window_title(const ROMlib_carbon_reference* ref, enum carbon_property_operation operation, int alternate,
                        ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0], argument = call->words[1];
    const int get = operation == property_title_get;
    if ((!alternate || get) && !argument)
        return window_result(call, paramErr);
    if (get && !ROMlib_carbon_guest_memory(argument, 4, 1))
        return 0;
    uint32_t valid, status;
    if (!cfp_private(ref, 0xb2c28, &window, 1, &valid))
        return 0;
    if (!valid)
        return (!get || alternate || cfp_write(argument, 0)) && window_result(call, window_invalid);

    _Alignas(128) uint32_t record[17] = {0};
    const uint32_t record_address = US_TO_SYN68K(record);
    const unsigned field = alternate ? window_alternate_title_field : window_title_field;
    if (!window_info_record(ref, property_get, window, record_address, &status))
        return 0;
    const int present = !status;
    const uint32_t previous = present ? CL(record[field]) : 0;
    uint32_t string = 0;
    if (get) {
        if (previous) {
            const uint32_t copy[] = {0, previous};
            if (!cfp_private(ref, 0xb6690, copy, 2, &string))
                return 0;
        }
        else if (!alternate) {
            _Alignas(256) Byte text[256] = {0};
            const uint32_t get_title[] = {window, US_TO_SYN68K(text)};
            if (!cfp_private(ref, 0xb6870, get_title, 2, NULL))
                return 0;
            string = ROMlib_cfstring_pascal(0, get_title[1], ROMlib_cfstring_system_encoding());
        }
        return cfp_write(argument, string) &&
               window_result(call, string || alternate ? noErr : window_title_conversion_error);
    }

    if (!present && !initialize_window_info(ref, window, record))
        return 0;
    if (argument) {
        const uint32_t copy[] = {0, argument};
        if (!cfp_private(ref, 0xb6690, copy, 2, &string))
            return 0;
        if (!string)
            return window_result(call, window_title_conversion_error);
    }
    _Alignas(256) Byte text[256];
    if (!alternate) {
        uint32_t encoding;
        const uint32_t encode[] = {string, US_TO_SYN68K(text)};
        const int converted = cfp_private(ref, 0x3374c, encode, 2, &encoding);
        if (!converted || encoding == UINT32_MAX) {
            void* object = converted ? ROMlib_carbon_guest_memory(string, 1, 0) : NULL;
            if (!object ||
                !ROMlib_carbon_cf_string_pascal(object, text, sizeof text, ROMlib_cfstring_system_encoding()))
                return cfp_private(ref, 0xb6330, &string, 1, NULL) && converted &&
                       window_result(call, window_title_conversion_error);
        }
    }
    record[field] = CL(string);
    const int published = window_info_record(ref, property_set, window, record_address, &status);
    if (!published || status)
        return (!string || cfp_private(ref, 0xb6330, &string, 1, NULL)) && published && window_result(call, status);
    if (previous && !cfp_private(ref, 0xb6330, &previous, 1, NULL))
        return 0;
    if (!alternate) {
        ROMlib_carbon_ppc_frame title = {.words = {window, US_TO_SYN68K(text)}};
        uint32_t ignored;
        if (!ROMlib_carbon_call_toolbox_vector(0x1268, 0x3c0, 0, &title, &ignored))
            return 0;
    }
    else {
        _Alignas(8) uint32_t attributes = 0;
        const uint32_t query[] = {window, US_TO_SYN68K(&attributes)};
        if (!cfp_private(ref, 0xb2b20, query, 2, NULL))
            return 0;
        if (!(CL(attributes) & 0x08000000))
            return window_result(call, noErr);
    }
    return cfp_private(ref, 0x330ec, &window, 1, NULL) && window_result(call, noErr);
}

static int window_property_profile(const ROMlib_carbon_reference* ref, uint32_t offset, int control)
{
    return ref->code_offset == offset && ref->code_size == 0xb8fb4 && ref->toc && ref->toc_offset == 0x2f78 &&
           ref->toc_size == 0x10f0c && !((const Byte*)ref->toc)[control ? 0x6cd0 : 0x6d08];
}

enum {
    window_info_flags = 5,
    window_class_field = 6,
    window_attribute_field = 7,
    window_scope_field = 14,
    window_class_cached = 8,
    window_sheet_class = 8
};

static int class_mask(const ROMlib_carbon_reference* ref, uint32_t window_class, uint32_t* mask)
{
    *mask = 0;
    if (!window_class || window_class >= 18)
        return 1;
    uint32_t table;
    return cfp_get(US_TO_SYN68K(ref->toc) - 0xf54, &table) && cfp_get(table + window_class * 4, mask);
}

static int window_class_query(const ROMlib_carbon_reference* ref, enum carbon_property_operation operation,
                              ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window_class = call->words[0];
    uint32_t mask;
    if (!class_mask(ref, window_class, &mask))
        return 0;
    if (operation == property_class_valid || !mask)
        return window_result(call, mask != 0);

    uint32_t table;
    if (!cfp_get(US_TO_SYN68K(ref->toc) - 0xf50, &table))
        return 0;
    /* Each mapping row contains the classic WDEF ID, class and attributes. */
    for (unsigned row = 0; row < 256; ++row) {
        const uint32_t address = table + row * 12;
        const void* proc = ROMlib_carbon_guest_memory(address, 2, 0);
        if (!proc)
            return 0;
        uint16_t classic;
        memcpy(&classic, proc, 2);
        if (classic == UINT16_MAX) {
            _Alignas(8) uint32_t attributes = CL(mask);
            const uint32_t args[] = {window_class, US_TO_SYN68K(&attributes)};
            return cfp_private(ref, 0x34064, args, 2, NULL) && window_result(call, CL(attributes));
        }
        uint32_t mapped_class, attributes;
        if (!cfp_get(address + 4, &mapped_class) || !cfp_get(address + 8, &attributes))
            return 0;
        if (mapped_class == window_class)
            mask |= attributes;
    }
    return 0;
}

static int cache_window_class(const ROMlib_carbon_reference* ref, uint32_t* record, uint32_t window_class,
                              uint32_t attributes)
{
    if (CL(record[window_class_field]) != window_class) {
        uint32_t scope, kind;
        if (!cfp_private(ref, 0x34054, &window_class, 1, &scope) || !cfp_private(ref, 0x34044, &window_class, 1, &kind))
            return 0;
        record[window_scope_field] = CL(scope);
        const uint16_t value = CW((uint16_t)kind);
        memcpy((Byte*)record + 0x20, &value, sizeof value);
    }
    record[window_info_flags] = CL(CL(record[window_info_flags]) | window_class_cached);
    record[window_class_field] = CL(window_class);
    record[window_attribute_field] = CL(attributes);
    return 1;
}

static int read_window_class(const ROMlib_carbon_reference* ref, uint32_t window, uint32_t* record, uint32_t* status)
{
    *status = noErr;
    if (CL(record[window_info_flags]) & window_class_cached)
        return 1;

    _Alignas(8) uint32_t window_class = 0, attributes = 0;
    const WindowPtr handle = ROMlib_carbon_guest_memory(window, sizeof(WindowRecord), 0);
    if (!handle)
        return 0;
    const uint32_t map[] = {UINT32_MAX, WINDOW_GO_AWAY_FLAG(handle), window, US_TO_SYN68K(&window_class),
                            US_TO_SYN68K(&attributes)};
    return cfp_private(ref, 0xcfcc, map, 5, NULL) &&
           cache_window_class(ref, record, CL(window_class), CL(attributes)) &&
           window_info_record(ref, property_set, window, US_TO_SYN68K(record), status);
}

static int window_class_search(const ROMlib_carbon_reference* ref, int front, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t wanted = call->words[front ? 0 : 1];
    const int visible_only = (Byte)call->words[front ? 1 : 2] != 0;
    uint32_t window = call->words[0];
    if (!cfp_private(ref, front ? 0xb2a18 : 0xb2e20, &window, front ? 0 : 1, &window))
        return 0;
    while (window) {
        int matches = wanted == UINT32_MAX;
        if (!front || !matches) {
            _Alignas(8) uint32_t window_class = 0;
            const uint32_t args[] = {window, US_TO_SYN68K(&window_class)};
            if (!cfp_private(ref, 0xb2b08, args, 2, NULL))
                return 0;
            matches = matches || CL(window_class) == wanted;
        }
        if (matches) {
            /* In CarbonLib 1.4, Next applies visibility only to the wildcard class. */
            uint32_t visible = 1;
            if (visible_only && (front || wanted == UINT32_MAX) && !cfp_private(ref, 0xb2dc0, &window, 1, &visible))
                return 0;
            if (visible)
                break;
        }
        if (!cfp_private(ref, 0xb2e20, &window, 1, &window))
            return 0;
    }
    call->words[0] = window;
    return 1;
}

enum {
    group_windows = 1,
    group_recurse = 2,
    group_visible = 4,
    group_root_children = 5,
    window_group_invalid = -5616,
    window_group_not_found = -5610
};

static int window_group_root(const ROMlib_carbon_reference* ref, uint32_t* root)
{
    uint32_t slot;
    return cfp_get(US_TO_SYN68K(ref->toc) - 0xf38, &slot) && cfp_get(slot, root);
}

static int window_group_first(const ROMlib_carbon_reference* ref, uint32_t group, int visible, uint32_t* first)
{
    _Alignas(8) uint32_t result = 0;
    const uint32_t args[] = {group, 1, visible ? 6 : 2, US_TO_SYN68K(&result)};
    if (!cfp_private(ref, 0xb44d0, args, NELEM(args), NULL))
        return 0;
    *first = CL(result);
    return 1;
}

static int window_group_search_first(const ROMlib_carbon_reference* ref, uint32_t group, uint32_t root, uint32_t* first)
{
    if (!cfp_private(ref, 0xb2a18, NULL, 0, first))
        return 0;
    if (group == root)
        return 1;
    while (*first) {
        uint32_t parent;
        if (!cfp_private(ref, 0xb44a0, first, 1, &parent))
            return 0;
        if (parent == group)
            break;
        if (!cfp_get(*first + 0x90, first))
            return 0;
    }
    return 1;
}

static int window_group_member(const ROMlib_carbon_reference* ref, uint32_t window, uint32_t group, uint32_t options,
                               int stop_at_other_group, int* contained, int* matched)
{
    const uint32_t arguments[] = {window, group};
    uint32_t inside;
    *matched = 0;
    if (!cfp_private(ref, 0xb44b8, arguments, NELEM(arguments), &inside))
        return 0;
    *contained = inside != 0;
    if (!inside)
        return 1;
    if (!(options & group_recurse)) {
        uint32_t layer;
        if (!cfp_private(ref, 0x1076c, &window, 1, &layer))
            return 0;
        if (layer + 2 != group) {
            *contained = !stop_at_other_group;
            return 1;
        }
    }
    if (!(options & group_visible)) {
        *matched = 1;
        return 1;
    }
    const Byte* visible = ROMlib_carbon_guest_memory(window + 0x6e, 1, 0);
    if (!visible)
        return 0;
    *matched = *visible != 0;
    return 1;
}

static int window_group_count(const ROMlib_carbon_reference* ref, uint32_t root, uint32_t group, uint32_t options,
                              ROMlib_carbon_ppc_frame* call)
{
    if (!group)
        return window_result(call, 0);
    if (!(options & group_windows))
        return window_result(call, group == root ? group_root_children : 0);
    uint32_t window, count = 0;
    if (!window_group_first(ref, group, options & group_visible, &window))
        return 0;
    while (window) {
        int contained, matched;
        if (!window_group_member(ref, window, group, options, 1, &contained, &matched))
            return 0;
        if (!contained)
            break;
        count += matched;
        if (!cfp_get(window + 0x90, &window))
            return 0;
    }
    return window_result(call, count);
}

static int window_group_scan(const ROMlib_carbon_reference* ref, uint32_t root, uint32_t group, uint32_t options,
                             uint32_t first_index, uint32_t last_index, uint32_t target, uint32_t output,
                             ROMlib_carbon_ppc_frame* call)
{
    uint32_t window, ordinal = 0;
    if (!window_group_search_first(ref, group, root, &window))
        return 0;
    while (window) {
        int contained, matched;
        if (!window_group_member(ref, window, group, options, 0, &contained, &matched))
            return 0;
        if (!contained)
            break;
        if (matched) {
            ++ordinal;
            if (target && target == window)
                return cfp_write(output, ordinal) && window_result(call, noErr);
            if (!target && ordinal >= first_index && ordinal <= last_index) {
                if (!cfp_write(output + 4 * (ordinal - first_index), window))
                    return 0;
                if (ordinal == last_index)
                    return window_result(call, noErr);
            }
        }
        if (!cfp_get(window + 0x90, &window))
            return 0;
    }
    return window_result(call, window_group_not_found);
}

static int window_group_contents(const ROMlib_carbon_reference* ref, uint32_t root, uint32_t group,
                                 ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    const uint32_t options = args[1], limit = args[2], count_out = args[3], items = args[4];
    if (!group)
        return window_result(call, window_group_invalid);
    uint32_t count = 0;
    if (!(options & group_windows)) {
        if (group == root)
            count = limit ? MIN(limit, group_root_children) : group_root_children;
        if (items && limit) {
            if ((uint64_t)items + 4 * (uint64_t)count > UINT64_C(0x100000000))
                return window_result(call, paramErr);
            for (uint32_t i = 0; i < count; ++i)
                if (!cfp_write(items + 4 * i, i + 2))
                    return 0;
        }
        return (!count_out || cfp_write(count_out, count)) && window_result(call, noErr);
    }

    uint32_t window;
    if (!window_group_first(ref, group, 0, &window))
        return 0;
    while (window && count < limit) {
        uint32_t parent;
        if (!cfp_private(ref, 0xb44a0, &window, 1, &parent))
            return 0;
        if (parent != group)
            break;
        ++count;
        if (!cfp_get(window + 0x90, &window))
            return 0;
    }
    if (count_out && !cfp_write(count_out, count))
        return 0;
    if (!items)
        return window_result(call, noErr);
    if ((uint64_t)items + 4 * (uint64_t)MAX(1, count) > UINT64_C(0x100000000))
        return window_result(call, paramErr);
    return cfp_write(items, 0) && window_group_scan(ref, root, group, options, 1, count, 0, items, call);
}

int ROMlib_carbon_window_group(const char* name, uint32_t offset, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", name, &ref))
        return 0;
    if (!window_property_profile(&ref, offset, 0))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    const uint32_t* args = call->words;
    uint32_t root, value;
    if (!window_group_root(&ref, &root))
        return 0;
    switch (offset) {
    case 0x34210: /* GetWindowGroupOfClass */
        if (args[0] == UINT32_MAX)
            return window_result(call, root);
        if (!cfp_private(&ref, 0xb4740, args, 1, &value))
            return 0;
        if (!value)
            return window_result(call, 0);
        return cfp_private(&ref, 0x34044, args, 1, &value) && window_result(call, value == 6 ? 0 : value + 2);
    case 0x346d8: /* CountWindowGroupContents */
        return window_group_count(&ref, root, args[0] ? args[0] : root, args[1], call);
    case 0x347f0: /* GetWindowGroupContents */
        return window_group_contents(&ref, root, args[0] ? args[0] : root, call);
    case 0x3467c: /* GetIndexedWindow */
        if (!args[1] || !args[3])
            return window_result(call, paramErr);
        return cfp_write(args[3], 0) &&
               window_group_scan(&ref, root, args[0] ? args[0] : root, args[2], args[1], args[1], 0, args[3], call);
    }
    if (!cfp_private(&ref, 0xb2c28, args, 1, &value))
        return 0;
    if (!value)
        return window_result(call, offset == 0x342dc || offset == 0x3441c ? 0 : window_invalid);
    switch (offset) {
    case 0x342dc: /* GetWindowGroup */
        return cfp_private(&ref, 0x1076c, args, 1, &value) && window_result(call, value + 2);
    case 0x3441c: /* IsWindowContainedInGroup */
        return (args[1] == root || cfp_private(&ref, 0xb44a0, args, 1, &value)) &&
               window_result(call, args[1] == root || value == args[1]);
    case 0x34980: /* GetWindowIndex */
        if (!args[3])
            return window_result(call, paramErr);
        return cfp_write(args[3], 0) &&
               window_group_scan(&ref, root, args[1] ? args[1] : root, args[2], 0, 0, args[0], args[3], call);
    case 0x34340: { /* SetWindowGroup */
        if (!args[1])
            return window_result(call, window_group_invalid);
        if (args[1] == root)
            return window_result(call, paramErr);
        if (!cfp_private(&ref, 0xb44a0, args, 1, &value))
            return 0;
        if (value != args[1]) {
            const uint32_t layer[] = {args[0], (int16_t)(args[1] - 2)};
            const uint32_t reorder[] = {args[0], value < args[1] ? UINT32_MAX : 0, 0, 1};
            if (!cfp_private(&ref, 0x107d8, layer, NELEM(layer), NULL) ||
                !cfp_private(&ref, 0xf694, reorder, NELEM(reorder), NULL))
                return 0;
        }
        return window_result(call, paramErr);
    }
    default:
        return 0;
    }
}

static int window_activation_scope(const ROMlib_carbon_reference* ref, uint32_t window, uint32_t* scope)
{
    _Alignas(128) uint32_t record[17] = {0};
    uint32_t status;
    if (!window_info_record(ref, property_get, window, US_TO_SYN68K(record), &status))
        return 0;
    *scope = status ? 2 : CL(record[window_scope_field]);
    return 1;
}

static int front_nonfloating_window(const ROMlib_carbon_reference* ref, uint32_t* result)
{
    uint32_t window;
    if (!cfp_private(ref, 0xb2a18, NULL, 0, &window))
        return 0;
    while (window) {
        uint32_t visible;
        if (!cfp_private(ref, 0xb2dc0, &window, 1, &visible))
            return 0;
        if (visible) {
            uint32_t scope;
            if (!window_activation_scope(ref, window, &scope))
                return 0;
            if (scope != 0 && scope != 1)
                break;
        }
        if (!cfp_private(ref, 0xb2e20, &window, 1, &window))
            return 0;
    }
    *result = window;
    return 1;
}

static int floating_window_visibility(const ROMlib_carbon_reference* ref, int show, ROMlib_carbon_ppc_frame* call)
{
    uint32_t address;
    if (!cfp_get(US_TO_SYN68K(ref->toc) - 0x160c, &address))
        return 0;
    Byte* visible = ROMlib_carbon_guest_memory(address, 1, 1);
    if (!visible)
        return 0;

    const uint32_t args[] = {5, *visible, 0, 1, show};
    uint32_t status;
    /* The window worker sees the new state on show and the old state on hide. */
    if (show)
        *visible = 1;
    if (!cfp_private(ref, 0x121d0, args, NELEM(args), &status))
        return 0;
    if (!show)
        *visible = 0;
    return window_result(call, status);
}

static int is_window_active(const ROMlib_carbon_reference* ref, uint32_t window, ROMlib_carbon_ppc_frame* call)
{
    const Byte* state = ROMlib_carbon_guest_memory(window + 0x6e, 2, 0);
    if (!state)
        return 0;
    if (!state[0])
        return window_result(call, 0);
    uint32_t scope, active;
    if (!window_activation_scope(ref, window, &scope))
        return 0;
    if (scope == 2) {
        if (!front_nonfloating_window(ref, &active))
            return 0;
        if (active == window)
            return window_result(call, 1);
    }
    if (!state[1])
        return window_result(call, 0);

    uint32_t root, layer;
    if (!window_group_root(ref, &root) || !cfp_private(ref, 0x1076c, &window, 1, &layer) ||
        !window_group_search_first(ref, layer + 2, root, &active))
        return 0;
    /* CarbonLib's secondary check tests whether the group has no front window. */
    return window_result(call, layer + 2 == root || !active);
}

static int lookup_window_global(const ROMlib_carbon_reference* ref, int slot, uint32_t* value)
{
    const uint32_t address = cfp_toc(ref, (uint32_t)slot);
    return address && cfp_get(address, value);
}

static int window_delegate(const ROMlib_carbon_reference* ref, int flag_slot, uint32_t delegate_slot, int names_slot,
                           unsigned library_offset, unsigned name_offset, uint32_t* delegate)
{
    const uint32_t flag = cfp_toc(ref, (uint32_t)flag_slot);
    Byte* initialized = flag ? ROMlib_carbon_guest_memory(flag, 1, 1) : NULL;
    if (!initialized || !delegate_slot)
        return 0;
    if (*initialized)
        return cfp_get(delegate_slot, delegate);
    const uint32_t strings = cfp_toc(ref, (uint32_t)names_slot);
    if (!strings)
        return 0;
    const uint32_t names[] = {strings + library_offset, strings + name_offset};
    if (!cfp_private(ref, 0x55c5c, names, NELEM(names), delegate) || !cfp_write(delegate_slot, *delegate))
        return 0;
    *initialized = 1;
    return 1;
}

static int first_process_window(const ROMlib_carbon_reference* ref, uint32_t candidate, uint32_t* window)
{
    const uint32_t owner = cfp_toc(ref, (uint32_t)-0x1698);
    if (!owner)
        return 0;
    _Alignas(128) uint32_t attributes[17];
    _Alignas(8) Byte belongs;
    while (candidate) {
        belongs = 0;
        memset(attributes, 0, sizeof attributes);
        const uint32_t properties = US_TO_SYN68K(attributes);
        const uint32_t compare[] = {owner, properties, US_TO_SYN68K(&belongs)};
        uint32_t status;
        if (!window_info_record(ref, property_get, candidate, properties, &status) ||
            (!status && !cfp_private(ref, 0xb1188, compare, NELEM(compare), &status)))
            return 0;
        if (!status && belongs) {
            *window = candidate;
            return 1;
        }
        if (!cfp_get(candidate + 0x90, &candidate))
            return 0;
    }
    uint32_t layer;
    return lookup_window_global(ref, -0x168c, &layer) && cfp_private(ref, 0xb61c8, &layer, 1, window);
}

static int get_window_list(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t restrictions = US_TO_SYN68K(ref->toc) + 0x6ea4;
    uint32_t count, container, first = 0;
    if (!cfp_get(restrictions, &count))
        return 0;
    if (count && (!lookup_window_global(ref, -0x1690, &container) || (container && !cfp_get(container + 0x94, &first))))
        return 0;
    return first_process_window(ref, first, &first) && window_result(call, first);
}

static int get_next_window(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0];
    uint32_t valid, next;
    if (!cfp_private(ref, 0xb2c28, &window, 1, &valid))
        return 0;
    if (!valid)
        return window_result(call, 0);
    const uint16_t* kind = ROMlib_carbon_guest_memory(window + 0x4a, sizeof *kind, 0);
    if (!kind || !cfp_get(window + 0x90, &next))
        return 0;
    if (CW(*kind) == 0xdead)
        return window_result(call, next);
    _Alignas(8) uint32_t window_class = 0;
    const uint32_t classify[] = {window, US_TO_SYN68K(&window_class)};
    if (!cfp_private(ref, 0xb2b08, classify, NELEM(classify), NULL))
        return 0;
    return (CL(window_class) != 8 || first_process_window(ref, next, &next)) && window_result(call, next);
}

static int default_focus_window(const ROMlib_carbon_reference* ref, uint32_t* window)
{
    return cfp_private(ref, 0xb4470, NULL, 0, window);
}

static int get_focus_window(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    uint32_t window;
    return lookup_window_global(ref, -0xf5c, &window) && (window != UINT32_MAX || default_focus_window(ref, &window)) &&
           window_result(call, window);
}

static int notify_focus_window(const ROMlib_carbon_reference* ref, const ROMlib_carbon_ppc_frame* caller,
                               uint32_t window, uint32_t kind)
{
    _Alignas(8) uint32_t event_output = 0;
    ROMlib_carbon_ppc_frame create = *caller;
    if (!cfx_private_frame(ref, 0xb3288, &create))
        return 0;
    create.words[0] = kind;
    create.words[3] = window;
    create.words[4] = US_TO_SYN68K(&event_output);
    if (!cfx_private_frame(ref, 0x4f164, &create))
        return 0;
    if (create.words[0])
        return 1;
    const uint32_t event = CL(event_output);
    uint32_t send[] = {event, 0, 2};
    const int sent =
        cfp_private(ref, 0xb3528, &window, 1, &send[1]) && cfp_private(ref, 0x46bb8, send, NELEM(send), NULL);
    return cfp_private(ref, 0xb33f0, &event, 1, NULL) && sent;
}

static int focus_classic_window(const ROMlib_carbon_reference* ref, uint32_t window)
{
    const uint32_t can_focus[] = {window, 1};
    uint32_t status;
    if (!cfp_private(ref, 0xb4ab8, can_focus, NELEM(can_focus), &status))
        return 0;
    if (status)
        return 1;
    uint32_t tracker, state;
    if (!cfp_private(ref, 0xb4f80, NULL, 0, &tracker) || !tracker || !cfp_write(tracker + 0x26, window))
        return 0;
    const uint32_t callback = cfp_toc(ref, (uint32_t)-0xf64), initializer = tracker + 0x2a;
    const uint32_t tracking_state = cfp_toc(ref, (uint32_t)-0xf6c);
    if (!cfp_private(ref, 0xb11b8, &initializer, 1, NULL) || !cfp_write(tracker + 0x32, callback) || !tracking_state ||
        !cfp_get(tracking_state + 4, &state) || (!state && !cfp_private(ref, 0xb11b8, &tracking_state, 1, NULL)))
        return 0;
    const uint32_t listener_flag = cfp_toc(ref, (uint32_t)-0xf68);
    Byte* registered = ROMlib_carbon_guest_memory(listener_flag, 1, 1);
    if (!registered)
        return 0;
    if (!*registered) {
        const uint32_t listener[] = {cfp_toc(ref, (uint32_t)-0xf60), TICK("achg"), 0};
        if (!cfp_private(ref, 0xb6618, listener, NELEM(listener), NULL))
            return 0;
        *registered = 1;
    }
    return 1;
}

static int set_focus_window(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    uint32_t window = call->words[0], valid, previous;
    if (window == UINT32_MAX) {
        if (!default_focus_window(ref, &window))
            return 0;
    }
    else {
        if (!cfp_private(ref, 0xb2c28, &window, 1, &valid))
            return 0;
        if (!valid)
            return window_result(call, paramErr);
    }
    const uint32_t slot = cfp_toc(ref, (uint32_t)-0xf5c);
    if (!slot || !cfp_get(slot, &previous))
        return 0;
    if (previous == window)
        return window_result(call, noErr);
    if ((previous && !notify_focus_window(ref, call, previous, 201)) || !cfp_write(slot, window))
        return 0;
    if (window) {
        _Alignas(8) uint32_t window_class = 0;
        const uint32_t args[] = {window, US_TO_SYN68K(&window_class)};
        if (!cfp_private(ref, 0xb2b08, args, NELEM(args), NULL) ||
            (CL(window_class) == 8 && !focus_classic_window(ref, window)) ||
            !notify_focus_window(ref, call, window, 200))
            return 0;
    }
    const uint32_t no_focus_control = 0;
    return cfp_private(ref, 0xb4440, &no_focus_control, 1, NULL) && window_result(call, noErr);
}

static int window_ideal_user_state(const ROMlib_carbon_reference* ref, int set, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0], rectangle = call->words[1];
    uint32_t valid, delegate;
    if (!cfp_private(ref, 0xb2c28, &window, 1, &valid))
        return 0;
    if (!valid)
        return window_result(call, window_invalid);
    if (!rectangle)
        return window_result(call, paramErr);
    const uint32_t delegate_slot = US_TO_SYN68K(ref->toc) + (set ? 0x78a4 : 0x789c);
    if (!window_delegate(ref, set ? -0x1474 : -0x147c, delegate_slot, -0x1e8, 0, set ? 0x25 : 0xc, &delegate))
        return 0;
    const uint32_t args[] = {window, rectangle};
    uint32_t status;
    if (delegate)
        return cfp_callback(delegate, args, NELEM(args), &status) && window_result(call, status);

    _Alignas(128) uint32_t attributes[17] = {0};
    const uint32_t properties = US_TO_SYN68K(attributes);
    if (!window_info_record(ref, property_get, window, properties, &status))
        return 0;
    if (status) {
        memset(attributes, 0, sizeof attributes);
        if (!initialize_window_info(ref, window, attributes))
            return 0;
    }
    const uint32_t ideal = properties + 11 * sizeof(uint32_t);
    if (set && (!cfp_move(ideal, rectangle, sizeof(Rect)) ||
                !window_info_record(ref, property_set, window, properties, &status)))
        return 0;
    return (set || cfp_move(rectangle, ideal, sizeof(Rect))) && window_result(call, noErr);
}

static int near_window_edge(int16_t ideal, int16_t current)
{
    return current >= (int16_t)(ideal - 7) && current < (int16_t)(ideal + 7);
}

static int window_in_standard_state(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0], requested_size = call->words[1], output = call->words[2];
    uint32_t valid, delegate;
    if (!cfp_private(ref, 0xb2c28, &window, 1, &valid))
        return 0;
    if (!valid)
        return window_result(call, 0);
    if (!window_delegate(ref, -0x1468, US_TO_SYN68K(ref->toc) + 0x78ac, -0x1e8, 0, 0x54, &delegate))
        return 0;
    uint32_t version = 0;
    if (delegate && !cfp_private(ref, 0x56014, NULL, 0, &version))
        return 0;
    _Alignas(8) Rect bounds;
    const uint32_t get_bounds[] = {window, US_TO_SYN68K(&bounds)};
    if ((!delegate || version <= 0x90f) && !cfp_private(ref, 0x59144, get_bounds, NELEM(get_bounds), NULL))
        return 0;
    uint32_t size = 0;
    if (requested_size && (!delegate || version <= 0x90f)) {
        if (!cfp_get(requested_size, &size))
            return 0;
        const int16_t target_bottom = (int16_t)(Cx(bounds.top) + (int16_t)(size >> 16));
        const int16_t target_right = (int16_t)(Cx(bounds.left) + (int16_t)size);
        if (near_window_edge(target_bottom, Cx(bounds.bottom)) && near_window_edge(target_right, Cx(bounds.right)))
            return window_result(call, 1);
    }
    if (delegate) {
        const uint32_t args[] = {window, requested_size, output};
        uint32_t status;
        return cfp_callback(delegate, args, NELEM(args), &status) && window_result(call, status);
    }
    _Alignas(8) Rect ideal = {0};
    const uint32_t calculate[] = {window, size, US_TO_SYN68K(&ideal)};
    const uint32_t standard[] = {window, US_TO_SYN68K(&ideal)};
    const int ok = requested_size ? cfp_private(ref, 0x251b8, calculate, NELEM(calculate), NULL)
                                  : cfp_private(ref, 0xb2e38, standard, NELEM(standard), NULL);
    return ok && (!output || cfp_move(output, US_TO_SYN68K(&ideal), sizeof ideal)) &&
           window_result(call, near_window_edge(Cx(ideal.top), Cx(bounds.top)) &&
                                   near_window_edge(Cx(ideal.left), Cx(bounds.left)) &&
                                   near_window_edge(Cx(ideal.bottom), Cx(bounds.bottom)) &&
                                   near_window_edge(Cx(ideal.right), Cx(bounds.right)));
}

static int window_widget_hilite(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    uint32_t delegate;
    if (!window_delegate(ref, -0x153c, US_TO_SYN68K(ref->toc) + 0x7550, -0x1f0, 0, 0xc, &delegate))
        return 0;
    const uint32_t args[] = {call->words[0], call->words[1]};
    uint32_t status;
    if (delegate)
        return cfp_callback(delegate, args, NELEM(args), &status) && window_result(call, status);
    const uint32_t hilite_state = cfp_toc(ref, (uint32_t)-0x1534);
    if (!args[1])
        return window_result(call, paramErr);
    return hilite_state && cfp_move(args[1], hilite_state, sizeof(uint16_t)) && window_result(call, noErr);
}

static int window_structure_widths(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t args[] = {call->words[0], call->words[1]};
    uint32_t valid;
    if (!cfp_private(ref, 0xb2c28, args, 1, &valid))
        return 0;
    if (!valid)
        return window_result(call, window_invalid);
    if (!args[1])
        return window_result(call, paramErr);
    uint32_t delegate, status;
    if (!window_delegate(ref, -0x970, cfp_toc(ref, (uint32_t)-0x974), -0x14c, 0x18, 0xb4, &delegate))
        return 0;
    if (!delegate)
        return window_result(call, memFullErr);
    return cfp_callback(delegate, args, NELEM(args), &status) && window_result(call, status);
}

static int read_window_content_color(const ROMlib_carbon_reference* ref, CTabHandle table, RGBColor* output,
                                     ROMlib_carbon_ppc_frame* call)
{
    if (!table)
        return window_result(call, noErr);
    if (!STARH(table)) {
        const uint32_t handle = US_TO_SYN68K(table);
        if (!cfp_private(ref, 0xb1fc8, &handle, 1, NULL))
            return 0;
        if (!STARH(table)) {
            uint32_t status;
            return cfp_private(ref, 0xb1ff8, NULL, 0, &status) && window_result(call, status);
        }
    }
    const Size extent = GetHandleSize((Handle)table);
    if (extent < (Size)sizeof(ColorTable))
        return window_result(call, noErr);
    const ColorTable* colors = STARH(table);
    const int entries = (extent - offsetof(ColorTable, ctTable)) / sizeof(ColorSpec);
    int index = CTAB_SIZE(table);
    if (index >= entries)
        index = entries - 1;
    for (; index > 0 && CW(colors->ctTable[index].value) != wContentColor; --index)
        ;
    *output = colors->ctTable[index < 0 ? 0 : index].rgb;
    return window_result(call, noErr);
}

static int window_content_color(const ROMlib_carbon_reference* ref, bool set, bool use_delegate,
                                ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0], address = call->words[1];
    const uint32_t args[] = {window, address};
    if (!set && !address)
        return window_result(call, paramErr);
    if (!window)
        return window_result(call, window_invalid);
    RGBColor* color = address ? ROMlib_carbon_guest_memory(address, sizeof *color, !set) : NULL;
    if (address && !color)
        return 0;
    if (!set) {
        const uint32_t default_color = cfp_toc(ref, (uint32_t)-0x954);
        if (!default_color || !cfp_move(address, default_color, sizeof *color))
            return 0;
    }
    uint32_t status;
    if (use_delegate) {
        uint32_t delegate;
        const int delegate_slot = set ? -0x95c : -0x950;
        if (!window_delegate(ref, set ? -0x960 : -0x958, cfp_toc(ref, (uint32_t)delegate_slot), -0x14c, 0x18,
                             set ? 0xe3 : 0xfa, &delegate))
            return 0;
        if (delegate)
            return cfp_callback(delegate, args, NELEM(args), &status) && window_result(call, status);
    }
    if (!color)
        return window_result(call, paramErr);

    _Alignas(4) uint32_t encoded_aux = 0;
    const uint32_t get_aux[] = {window, US_TO_SYN68K(&encoded_aux)};
    if (!cfp_private(ref, 0xb63a8, get_aux, NELEM(get_aux), &status))
        return 0;
    if (!status || !encoded_aux)
        return window_result(call, memFullErr);
    AuxWinHandle aux = ROMlib_carbon_guest_memory(CL(encoded_aux), sizeof *aux, 0);
    if (!aux || !STARH(aux))
        return 0;
    CTabHandle previous = HxP(aux, awCTable);
    if (!set)
        return read_window_content_color(ref, previous, color, call);

    /* A private one-entry table follows the original classic fallback.
       The window's private metadata owns the allocation until replacement or close. */
    const RGBColor value = *color;
    uint32_t seed;
    const ROMlib_carbon_ppc_frame seed_call = {0};
    if (!ROMlib_carbon_call_toolbox_vector(0x16a0, 0x30, 0, &seed_call, &seed))
        return 0;
    CTabHandle replacement = (CTabHandle)NewHandle(sizeof(ColorTable));
    if (!replacement)
        return window_result(call, memFullErr);
    ColorTable* table = STARH(replacement);
    *table = (ColorTable){.ctSeed = CL(seed), .ctTable = {{CWC(wContentColor), value}}};
    const ROMlib_carbon_ppc_frame update = {.words = {window, US_TO_SYN68K(replacement)}};
    uint32_t ignored;
    const int updated = ROMlib_carbon_call_toolbox_vector(0x1704, 0x3c0, 0, &update, &ignored);
    if (!STARH(aux) || HxP(aux, awCTable) != replacement)
        DisposHandle((Handle)replacement);
    else if (GetHandleSize((Handle)aux) >= (Size)ROMLIB_AUX_WINDOW_SIZE) {
        ROMlib_window_metadata* metadata = (ROMlib_window_metadata*)((Byte*)(STARH(aux) + 1) + sizeof(uint32));
        metadata->owned_content_colors = US_TO_SYN68K(replacement);
    }
    return updated && window_result(call, noErr);
}

static int windows_content_color(const char* name, uint32_t offset, bool set, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (ROMlib_carbon_probe_reference("CarbonLib", name, &ref) && window_property_profile(&ref, offset, 0))
        return window_content_color(&ref, set, false, call);
    if (!ROMlib_carbon_load_reference("WindowsLib", name, &ref))
        return window_result(call, -4);
    return ROMlib_carbon_invoke_frame(ref.function, call);
}

int ROMlib_carbon_windows_get_content_color(ROMlib_carbon_ppc_frame* call)
{
    return windows_content_color("GetWindowContentColor", 0x5a404, false, call);
}

int ROMlib_carbon_windows_set_content_color(ROMlib_carbon_ppc_frame* call)
{
    return windows_content_color("SetWindowContentColor", 0x5a228, true, call);
}

static int content_bounds_for_structure(const ROMlib_carbon_reference* ref, uint32_t window, Rect* bounds,
                                        uint32_t* status);

static int move_classic_structure(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0];
    Rect position = {.top = CW((int16_t)call->words[2]), .left = CW((int16_t)call->words[1])};
    uint32_t status;
    if (!content_bounds_for_structure(ref, window, &position, &status))
        return 0;
    const uint32_t move[] = {window, Cx(position.left), Cx(position.top), 0};
    return (status || cfp_private(ref, 0xb64c8, move, NELEM(move), NULL)) && window_result(call, status);
}

static int move_window_structure(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0];
    uint32_t valid, delegate;
    if (!cfp_private(ref, 0xb2c28, &window, 1, &valid))
        return 0;
    if (!valid)
        return window_result(call, window_invalid);
    if (!window_delegate(ref, -0x96c, cfp_toc(ref, (uint32_t)-0x968), -0x14c, 0x18, 0xce, &delegate))
        return 0;
    const int16_t left = call->words[1], top = call->words[2];
    if (delegate) {
        uint32_t version, collapsed = 0;
        if (!cfp_private(ref, 0x56014, NULL, 0, &version) ||
            (version <= 0x90f && !cfp_private(ref, 0xb64e0, &window, 1, &collapsed)))
            return 0;
        if (!collapsed) {
            const uint32_t args[] = {window, (int32_t)left, (int32_t)top};
            uint32_t status;
            return cfp_callback(delegate, args, NELEM(args), &status) && window_result(call, status);
        }
    }
    return move_classic_structure(ref, call);
}

int ROMlib_carbon_windows_move_structure(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_probe_reference("CarbonLib", "MoveWindowStructure", &ref) ||
        !window_property_profile(&ref, 0x59c40, 0)) {
        if (!ROMlib_carbon_load_reference("WindowsLib", "MoveWindowStructure", &ref))
            return window_result(call, -4);
        return ROMlib_carbon_invoke_frame(ref.function, call);
    }
    uint32_t valid;
    if (!cfp_private(&ref, 0xb2c28, call->words, 1, &valid))
        return 0;
    return valid ? move_classic_structure(&ref, call) : window_result(call, window_invalid);
}

static int content_bounds_for_structure(const ROMlib_carbon_reference* ref, uint32_t window, Rect* bounds,
                                        uint32_t* status)
{
    _Alignas(16) Rect actual[2];
    for (unsigned i = 0; i < NELEM(actual); ++i) {
        const uint32_t lookup[] = {window, 32 + i, US_TO_SYN68K(&actual[i])};
        if (!cfp_private(ref, 0xb2d30, lookup, NELEM(lookup), status))
            return 0;
        if (*status)
            return 1;
    }
    bounds->top = CW(Cx(bounds->top) + Cx(actual[1].top) - Cx(actual[0].top));
    bounds->left = CW(Cx(bounds->left) + Cx(actual[1].left) - Cx(actual[0].left));
    bounds->bottom = CW(Cx(bounds->bottom) + Cx(actual[1].bottom) - Cx(actual[0].bottom));
    bounds->right = CW(Cx(bounds->right) + Cx(actual[1].right) - Cx(actual[0].right));
    return 1;
}

static int apply_window_bounds(const ROMlib_carbon_reference* ref, uint32_t window, const Rect* previous,
                               const Rect* target)
{
    const int16_t left = Cx(target->left), top = Cx(target->top);
    const int width = Cx(target->right) - left, height = Cx(target->bottom) - top;
    const uint32_t move[] = {window, left, top, 0};
    const uint32_t size[] = {window, (int16_t)width, (int16_t)height, 1};
    const bool same_origin = previous->left == target->left && previous->top == target->top;
    const bool same_extent =
        width == Cx(previous->right) - Cx(previous->left) && height == Cx(previous->bottom) - Cx(previous->top);
    return (same_origin || cfp_private(ref, 0xb64c8, move, NELEM(move), NULL)) &&
           (same_extent || cfp_private(ref, 0xb2ce8, size, NELEM(size), NULL));
}

int ROMlib_carbon_windows_set_bounds(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_probe_reference("CarbonLib", "SetWindowBounds", &ref) ||
        !window_property_profile(&ref, 0x59810, 0)) {
        if (!ROMlib_carbon_load_reference("WindowsLib", "SetWindowBounds", &ref))
            return window_result(call, -4);
        return ROMlib_carbon_invoke_frame(ref.function, call);
    }

    const uint32_t window = call->words[0], input = call->words[2];
    const uint16_t region = call->words[1];
    _Alignas(16) Rect previous = {0}, target = {0};
    uint32_t valid, status = noErr;
    if (!cfp_private(&ref, 0xb2c28, &window, 1, &valid))
        return 0;
    if (!valid)
        return window_result(call, window_invalid);
    if (!input || (region != 32 && region != 33))
        return window_result(call, paramErr);
    if (!cfp_move(US_TO_SYN68K(&target), input, sizeof target))
        return 0;
    if (Cx(target.top) >= Cx(target.bottom) || Cx(target.left) >= Cx(target.right))
        return window_result(call, paramErr);
    if (region == 32 && !content_bounds_for_structure(&ref, window, &target, &status))
        return 0;
    if (status)
        return window_result(call, status);
    const uint32_t lookup[] = {window, US_TO_SYN68K(&previous)};
    return cfp_private(&ref, 0x59144, lookup, NELEM(lookup), NULL) &&
           apply_window_bounds(&ref, window, &previous, &target) && window_result(call, noErr);
}

static int set_window_bounds(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0], input = call->words[2];
    const uint16_t region = call->words[1];
    uint32_t valid;
    if (!cfp_private(ref, 0xb2c28, &window, 1, &valid))
        return 0;
    if (!valid)
        return window_result(call, window_invalid);
    if (!input || (region != 32 && region != 33))
        return window_result(call, paramErr);
    uint32_t delegate;
    if (!window_delegate(ref, -0x978, cfp_toc(ref, (uint32_t)-0x97c), -0x14c, 0x18, 0xa3, &delegate))
        return 0;
    _Alignas(16) Rect previous = {0}, target = {0};
    const uint32_t old_bounds[] = {window, US_TO_SYN68K(&previous)};
    if (!cfp_private(ref, 0x59144, old_bounds, NELEM(old_bounds), NULL))
        return 0;
    uint32_t status = noErr;
    if (delegate) {
        const uint32_t args[] = {window, region, input};
        if (!cfp_callback(delegate, args, NELEM(args), &status))
            return 0;
    }
    else {
        if (!cfp_move(US_TO_SYN68K(&target), input, sizeof target) ||
            (region == 32 && !content_bounds_for_structure(ref, window, &target, &status)))
            return 0;
        if (!status && !apply_window_bounds(ref, window, &previous, &target))
            return 0;
    }
    if (status)
        return window_result(call, status);
    const uint32_t new_bounds[] = {window, US_TO_SYN68K(&target)};
    if (!cfp_private(ref, 0x59144, new_bounds, NELEM(new_bounds), NULL))
        return 0;
    const uint32_t notify[] = {window, old_bounds[1], new_bounds[1]};
    return (!memcmp(&previous, &target, sizeof target) || cfp_private(ref, 0x5953c, notify, NELEM(notify), NULL)) &&
           window_result(call, noErr);
}

static int available_window_bounds(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t device = call->words[0], output = call->words[1];
    if (!device || !output)
        return window_result(call, paramErr);
    uint32_t record, main_device;
    if (!cfp_get(device, &record) || !record || !cfp_private(ref, 0xb17b8, NULL, 0, &main_device))
        return 0;
    const Rect* screen = ROMlib_carbon_guest_memory(record + 0x22, sizeof *screen, 0);
    Rect* bounds = ROMlib_carbon_guest_memory(output, sizeof *bounds, 1);
    if (!screen || !bounds)
        return 0;
    Rect result = *screen;
    if (device == main_device) {
        uint32_t shown, height;
        if (!cfp_private(ref, 0xb6c30, NULL, 0, &shown))
            return 0;
        if (shown) {
            if (!cfp_private(ref, 0xb6c48, NULL, 0, &height))
                return 0;
            result.top = CW(Cx(result.top) + (int16_t)height);
        }
    }
    *bounds = result;
    return window_result(call, noErr);
}

static int window_greatest_device(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0], device_output = call->words[2], bounds_output = call->words[3];
    uint32_t valid;
    if (!cfp_private(ref, 0xb2c28, &window, 1, &valid))
        return 0;
    if (!valid)
        return window_result(call, window_invalid);

    _Alignas(64) struct {
        uint32_t device;
        uint32_t reserved[2];
        Rect window_bounds;
        uint32_t area;
    } search = {0};

    const uint32_t lookup[] = {window, (uint16_t)call->words[1], US_TO_SYN68K(&search.window_bounds)};
    uint32_t status, device;
    if (!cfp_private(ref, 0xb2d30, lookup, NELEM(lookup), &status))
        return 0;
    if (status)
        return window_result(call, status);
    if (!cfp_private(ref, 0xb17b8, NULL, 0, &device))
        return 0;
    search.device = CL(device);
    const uint32_t enumerate[] = {0, US_TO_SYN68K(ref->toc) + 0x7870, US_TO_SYN68K(&search), 5};
    if (!cfp_private(ref, 0xb17e8, enumerate, NELEM(enumerate), NULL))
        return 0;
    device = CL(search.device);
    _Alignas(8) Rect bounds;
    const uint32_t read_bounds[] = {device, US_TO_SYN68K(&bounds)};
    return cfp_private(ref, 0xb4458, read_bounds, NELEM(read_bounds), NULL) &&
           (!device_output || cfp_write(device_output, device)) &&
           (!bounds_output || cfp_move(bounds_output, read_bounds[1], sizeof bounds)) && window_result(call, noErr);
}

int ROMlib_carbon_window_activation(const char* name, uint32_t offset, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", name, &ref))
        return 0;
    if (!window_property_profile(&ref, offset, 0))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    switch (offset) {
    case 0xd2a4: /* GetNextWindow */
        return get_next_window(&ref, call);
    case 0xed38: /* GetWindowList */
        return get_window_list(&ref, call);
    case 0x33b64: /* GetUserFocusWindow */
        return get_focus_window(&ref, call);
    case 0x33894: /* SetUserFocusWindow */
        return set_focus_window(&ref, call);
    case 0x250cc: /* GetWindowIdealUserState */
    case 0x2531c: /* SetWindowIdealUserState */
        return window_ideal_user_state(&ref, offset == 0x2531c, call);
    case 0x257b4: /* IsWindowInStandardState */
        return window_in_standard_state(&ref, call);
    case 0x1d998: /* GetWindowWidgetHilite */
        return window_widget_hilite(&ref, call);
    case 0x59b5c: /* GetWindowStructureWidths */
        return window_structure_widths(&ref, call);
    case 0x5a228: /* SetWindowContentColor */
    case 0x5a404: /* GetWindowContentColor */
        return window_content_color(&ref, offset == 0x5a228, true, call);
    case 0x59c40: /* MoveWindowStructure */
        return move_window_structure(&ref, call);
    case 0x59810: /* SetWindowBounds */
        return set_window_bounds(&ref, call);
    case 0x249e0: /* GetAvailableWindowPositioningBounds */
        return available_window_bounds(&ref, call);
    case 0x24b0c: /* GetWindowGreatestAreaDevice */
        return window_greatest_device(&ref, call);
    case 0xf058:    /* FrontNonFloatingWindow */
    case 0x34ae8: { /* ActiveNonFloatingWindow */
        uint32_t window;
        return front_nonfloating_window(&ref, &window) && window_result(call, window);
    }
    case 0x11838: { /* AreFloatingWindowsVisible */
        const Byte* visible = ROMlib_carbon_guest_memory(US_TO_SYN68K(ref.toc) + 0x7090, 1, 0);
        return visible && window_result(call, *visible);
    }
    case 0x12618: /* ShowFloatingWindows */
    case 0x1263c: /* HideFloatingWindows */
        return floating_window_visibility(&ref, offset == 0x12618, call);
    }

    const uint32_t* args = call->words;
    uint32_t valid;
    if (!cfp_private(&ref, 0xb2c28, args, 1, &valid))
        return 0;
    if (offset == 0x34b1c)
        return valid ? is_window_active(&ref, args[0], call) : window_result(call, 0);
    if (!valid)
        return window_result(call, window_invalid);
    const uint32_t activate[] = {args[0], (Byte)args[1]};
    return cfp_private(&ref, 0xd854, activate, NELEM(activate), NULL) && window_result(call, noErr);
}

static int window_attribute_backend(const ROMlib_carbon_reference* ref, uint32_t* backend)
{
    const uint32_t toc = US_TO_SYN68K(ref->toc);
    Byte* initialized = ROMlib_carbon_guest_memory(toc + 0x7160, 1, 1);
    uint32_t slot;
    if (!initialized || !cfp_get(toc - 0x164c, &slot))
        return 0;
    if (*initialized)
        return cfp_get(slot, backend);

    uint32_t strings;
    if (!cfp_get(toc - 0x204, &strings))
        return 0;
    const uint32_t lookup[] = {strings + 0x1f, strings + 0xa2};
    if (!cfp_private(ref, 0x55c5c, lookup, 2, backend) || !cfp_write(slot, *backend))
        return 0;
    *initialized = 1;
    return 1;
}

static int redraw_window_attributes(const ROMlib_carbon_reference* ref, uint32_t window)
{
    _Alignas(8) uint32_t saved_port = 0;
    const uint32_t save[] = {US_TO_SYN68K(&saved_port)};
    if (!cfp_private(ref, 0xb12f0, save, 1, NULL))
        return 0;
    uint32_t manager_port, clipping = 0, structure = 0;
    const uint32_t layout[] = {0, window, 0, 0, 1};
    const uint32_t draw[] = {window, 1};
    const int saved = cfp_private(ref, 0xb6360, &window, 1, NULL);
    const int regions = saved && cfp_get(0x9de, &manager_port) && cfp_get(window + 0x72, &structure) &&
                        cfp_get(manager_port + 0x1c, &clipping);
    const uint32_t copy[] = {structure, clipping};
    const int drawn = regions && cfp_private(ref, 0xb14e8, copy, 2, NULL) &&
                      cfp_private(ref, 0xb63c0, &window, 1, NULL) && cfp_private(ref, 0x1da28, layout, 5, NULL) &&
                      cfp_private(ref, 0xb6378, draw, 2, NULL);
    const uint32_t restore = CL(saved_port);
    return cfp_private(ref, 0xb12d8, &restore, 1, NULL) && drawn;
}

static int change_window_wdef(const ROMlib_carbon_reference* ref, uint32_t window, uint32_t window_class,
                              uint32_t previous, uint32_t attributes, int* redraw, uint32_t* status)
{
    _Alignas(8) uint16_t old_proc = 0, new_proc = 0;
    const uint32_t get_proc[] = {window, US_TO_SYN68K(&old_proc)};
    uint32_t found;
    if (!cfp_private(ref, 0xcf04, get_proc, 2, &found))
        return 0;
    if (!found)
        return 1;

    const int32_t classic_proc = (int16_t)CW(old_proc);
    new_proc = old_proc;
    if ((classic_proc & ~0xf) != 0x7b0) {
        const uint32_t map[] = {window_class, attributes, (uint32_t)classic_proc, US_TO_SYN68K(&new_proc)};
        if (!cfp_private(ref, 0x33e0c, map, 4, &found))
            return 0;
        if (!found) {
            *status = window_attribute_unsupported;
            return 1;
        }
    }
    _Alignas(8) uint32_t aux_handle = 0;
    const uint32_t get_aux[] = {window, US_TO_SYN68K(&aux_handle)};
    if (!cfp_private(ref, 0xb63a8, get_aux, 2, &found))
        return 0;
    if (!found) {
        *status = memFullErr;
        return 1;
    }
    WindowPtr handle = ROMlib_carbon_guest_memory(window, sizeof(WindowRecord), 1);
    uint32_t aux, flags;
    const uint32_t variant = CW(new_proc) & 0xf;
    if (!handle || !cfp_get(CL(aux_handle), &aux) || !cfp_get(aux + 0x10, &flags) ||
        !cfp_write(aux + 0x10, (flags & ~0x0f000000u) | (variant << 24)))
        return 0;
    WINDOW_GO_AWAY_FLAG_X(handle) = (Byte)(attributes & 1);
    *redraw = variant != (CW(old_proc) & 0xf) || ((previous ^ attributes) & 1);

    const uint32_t collapse_state = 0x02000000;
    if ((previous ^ attributes) & collapse_state) {
        uint32_t state;
        return cfp_private(ref, 0xb3528, &window, 1, &state) &&
               cfp_private(ref, attributes & collapse_state ? 0xb35b8 : 0x46ce8, &state, 1, NULL);
    }
    return 1;
}

static int change_window_attributes(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0], set = call->words[1], clear = call->words[2];
    uint32_t valid, status;
    if (!cfp_private(ref, 0xb2c28, &window, 1, &valid))
        return 0;
    if (!valid)
        return window_result(call, window_invalid);
    if (!(set | clear))
        return window_result(call, noErr);
    if (set == clear)
        return window_result(call, paramErr);

    _Alignas(128) uint32_t record[17] = {0};
    const uint32_t address = US_TO_SYN68K(record);
    if (!window_info_record(ref, property_get, window, address, &status) ||
        (status && !initialize_window_info(ref, window, record)) || !read_window_class(ref, window, record, &status))
        return 0;
    if (status)
        return window_result(call, status);

    const uint32_t window_class = CL(record[window_class_field]);
    const uint32_t previous = CL(record[window_attribute_field]);
    const uint32_t attributes = (previous | set) & ~clear;
    uint32_t backend;
    if (!window_attribute_backend(ref, &backend))
        return 0;
    if (backend && (window_class == 6 || window_class == window_sheet_class) && ((set | clear) & 0xf09fffff))
        return ROMlib_carbon_invoke_frame(ref->function, call);

    int redraw = 0;
    if (!change_window_wdef(ref, window, window_class, previous, attributes, &redraw, &status))
        return 0;
    if (status)
        return window_result(call, status);
    if (!cache_window_class(ref, record, window_class, attributes) ||
        !window_info_record(ref, property_set, window, address, &status))
        return 0;
    return (!redraw || redraw_window_attributes(ref, window)) && window_result(call, noErr);
}

static int window_metadata(const ROMlib_carbon_reference* ref, enum carbon_property_operation operation, unsigned field,
                           ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0], argument = call->words[1];
    uint32_t valid, status;
    if (!cfp_private(ref, 0xb2c28, &window, 1, &valid))
        return 0;
    if (!valid)
        return window_result(call, operation == property_class_set ? paramErr : window_invalid);
    if ((operation == property_metadata_get && !argument) || (operation == property_scope_set && argument > 3))
        return window_result(call, paramErr);
    if (operation == property_class_set) {
        uint32_t mask;
        if (!class_mask(ref, argument, &mask))
            return 0;
        if (!mask || argument == window_sheet_class)
            return window_result(call, paramErr);
    }

    _Alignas(128) uint32_t record[17] = {0};
    const uint32_t address = US_TO_SYN68K(record);
    if (!window_info_record(ref, property_get, window, address, &status) ||
        (status && !initialize_window_info(ref, window, record)))
        return 0;
    if (operation == property_class_set ||
        (operation == property_metadata_get && (field == window_class_field || field == window_attribute_field))) {
        if (!read_window_class(ref, window, record, &status))
            return 0;
        if (status)
            return window_result(call, status);
    }
    else
        status = noErr;
    if (operation == property_metadata_get)
        return cfp_write(argument, CL(record[field])) && window_result(call, status);
    if (operation == property_scope_set) {
        record[window_scope_field] = CL(argument);
        return window_info_record(ref, property_set, window, address, &status) && window_result(call, status);
    }

    const uint32_t previous = CL(record[window_class_field]);
    if (previous == argument || previous == window_sheet_class)
        return window_result(call, noErr);
    _Alignas(8) uint32_t attributes = record[window_attribute_field];
    const uint32_t adjust[] = {argument, US_TO_SYN68K(&attributes)};
    if (!cfp_private(ref, 0x34064, adjust, 2, NULL) || !cache_window_class(ref, record, argument, CL(attributes)) ||
        !window_info_record(ref, property_set, window, address, &status))
        return 0;
    const uint32_t regroup[] = {window, UINT32_MAX, window, 1};
    return (status || cfp_private(ref, 0xf694, regroup, 4, NULL)) && window_result(call, status);
}

int ROMlib_carbon_clear_window_title(uint32_t window)
{
    ROMlib_carbon_reference ref;
    WindowPtr handle = window ? SYN68K_TO_US(window) : NULL;
    if (!handle || !ROMlib_valid_window(handle) || !ROMlib_carbon_probe_reference("CarbonLib", "SetWTitle", &ref) ||
        !window_property_profile(&ref, 0xfbbc, 0))
        return 1;

    _Alignas(128) uint32_t record[17] = {0};
    const uint32_t address = US_TO_SYN68K(record);
    uint32_t status;
    if (!window_info_record(&ref, property_get, window, address, &status))
        return 0;
    if (status || !record[window_title_field])
        return 1;
    const uint32_t previous = CL(record[window_title_field]);
    record[window_title_field] = 0;
    return window_info_record(&ref, property_set, window, address, &status) &&
           (status || cfp_private(&ref, 0xb6330, &previous, 1, NULL));
}

static int dispatch_property(const char* name, uint32_t offset, enum carbon_property_operation operation,
                             uint32_t argument, int control, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", name, &ref))
        return 0;
    if (!window_property_profile(&ref, offset, control))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    if (control) {
        /* CarbonLib switches to ControlsLib's own property collections when
           its initialized backend flag is set. Keep both callers on that store. */
        const Byte* saved_controls = ROMlib_carbon_guest_memory(US_TO_SYN68K(ref.toc) + 0x6cd0, 1, 0);
        if (!saved_controls)
            return 0;
        if (*saved_controls)
            return ROMlib_carbon_invoke_frame(ref.function, call);
    }
    if (operation == property_button_get || operation == property_button_set)
        return window_button(&ref, operation, argument, call);
    if (operation == property_title_get || operation == property_title_set)
        return window_title(&ref, operation, argument, call);
    if (operation == property_class_valid || operation == property_available_attributes)
        return window_class_query(&ref, operation, call);
    if (operation == property_window_class_search)
        return window_class_search(&ref, argument, call);
    if (operation == property_window_attributes_change)
        return change_window_attributes(&ref, call);
    if (operation == property_metadata_get || operation == property_scope_set || operation == property_class_set)
        return window_metadata(&ref, operation, argument, call);
    const uint32_t* args = call->words;
    const uint32_t creator = argument ? window_system_creator : args[1];
    if (!argument && creator == window_system_creator)
        return window_result(call, window_reserved_property);
    uint32_t valid, status;
    if (!cfp_private(&ref, control ? 0xb1b30 : 0xb2c28, args, 1, &valid))
        return 0;
    if (!valid)
        return window_result(call, control ? control_invalid : window_invalid);
    const uint32_t* property = args + (argument ? 1 : 2);
    if ((control || argument) && operation == property_get && !property[3])
        return window_result(call, paramErr);
    return object_property(&ref, operation, args[0], control, creator, property, &status) &&
           window_result(call, status);
}

int ROMlib_carbon_window_property(const char* name, uint32_t offset, enum carbon_property_operation operation,
                                  uint32_t argument, ROMlib_carbon_ppc_frame* call)
{
    return dispatch_property(name, offset, operation, argument, 0, call);
}

int ROMlib_carbon_control_property(const char* name, uint32_t offset, enum carbon_property_operation operation,
                                   uint32_t system, ROMlib_carbon_ppc_frame* call)
{
    return dispatch_property(name, offset, operation, system, 1, call);
}
