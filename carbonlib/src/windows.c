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
             ROMlib_carbon_windows_compatibility, ROMlib_carbon_window_saved_screen_bits,
             ROMlib_carbon_transition_window

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
#include "OSUtil.h"
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

enum {
    windows_set_features = 0x5a70,
    windows_get_features = 0x5ae4,
    windows_is_collapsable = 0xf5a8,
    windows_is_collapsed = 0xf5f4,
    windows_is_path_select = 0x49f0,
    windows_transition = 0x1980,
    windows_title_double_click = 0x75b0,
    windows_get_pattern = 0xd5a4,
    windows_set_pattern = 0xd43c,
    windows_set_theme_background = 0xd104,
    windows_next_activation = 0x80cc,
    windows_post_activation = 0x8214,
    windows_post_reshape = 0x8630,
    windows_hilite_drag_frame = 0x6354,
    windows_get_proxy_alias = 0x3d34,
    windows_get_proxy_file = 0x3a70,
    windows_get_proxy_icon = 0x38cc,
    windows_set_proxy_alias = 0x3b14,
    windows_set_proxy_file = 0x39ec,
    windows_set_proxy_icon = 0x36e8,
    windows_set_proxy_type = 0x3640,
    windows_remove_proxy = 0x34f4,
    windows_begin_proxy_drag = 0x453c,
    windows_end_proxy_drag = 0x4798,
    windows_track_proxy_drag = 0x431c,
    windows_track_existing_proxy_drag = 0x402c,
    windows_get_region = 0x6458,
    windows_global_find = 0xa3c4,
    windows_install_paint_proc = 0xc8f8,
    windows_erase_region = 0xcc24,
    windows_set_desktop_pattern = 0x6df8,
    windows_get_desktop_pattern = 0x6f50,
    windows_get_desktop_pixpat = 0x723c,
    windows_draw_desktop = 0x7418,
    windows_rebuild_desktop = 0x6950,
    windows_get_monitor_list = 0x18f44,
    windows_set_monitor_list = 0x18fc8,
    windows_dispose_monitor_list = 0x190d0,
    windows_refresh_monitor_list = 0x19188,
    windows_get_internal_monitor_list = 0x19224,
    windows_monitor_list_changed = 0x19244,
    windows_count_monitor_records = 0x19294,
    windows_get_monitor_record = 0x192f0,
    windows_set_monitor_record = 0x19344,
    windows_monitor_has_picture = 0x19794,
    windows_get_monitor_alignment = 0x19810,
    windows_set_monitor_alignment = 0x19894,
    windows_set_monitor_file = 0x1950c,
    windows_copy_monitor_file = 0x195f8,
    windows_set_device_picture = 0x1999c,
    windows_get_device_picture = 0x19a64,
    windows_file_may_contain_picture = 0x193e0,
    windows_get_picture_from_file = 0x169a4,
    windows_monitor_draw_takes_time = 0x1a270,
    windows_reposition_window = 0xe8b8,
    windows_reshape_windows = 0x11c30,
    windows_recalculate_title_widths = 0x2488,
    windows_collapse_window = 0xf644,
    windows_collapse_all_windows = 0xf934,
};

enum {
    proxy_file_tag = 0x7770666c,
    proxy_icon_tag = 0x7770636e,
    proxy_drag_image_tag = 0x64696d67,
    proxy_drag_item = 0x70727879,
    proxy_not_registered = -5602,
    proxy_unavailable = -4,
};

static int windows_current_process(uint32_t* process)
{
    uint32_t expanded;
    *process = 0;
    if (!cfp_get(0x02b6, &expanded))
        return 0;
    return !expanded || cfp_get(expanded + 0x2a4, process);
}

static int windows_process_state(uint32_t* state)
{
    /* ExpandMem -> current process -> Window Manager bookkeeping. */
    uint32_t process;
    *state = 0;
    return windows_current_process(&process) && (!process || cfp_get(process + 8, state));
}

static int windows_next_activation_event(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t output = call->words[0], active_output = call->words[1];
    Byte* active = active_output ? ROMlib_carbon_try_guest_memory(active_output, 1, 1) : NULL;
    if ((active_output && !active) || (output && !ROMlib_carbon_try_guest_memory(output, sizeof(uint32_t), 1)))
        return window_result(call, 0);

    uint32_t state, queue = 0, count = 0;
    if (!windows_process_state(&state) || (state && !cfp_get(state + 0x10, &queue)))
        return 0;
    if (queue && !cfp_private(ref, 0x1bd60, &queue, 1, &count))
        return 0;
    if (count) {
        _Alignas(8) Byte event[8] = {0};
        const uint32_t args[] = {queue, US_TO_SYN68K(event)};
        uint32_t status;
        if (!cfp_private(ref, 0x1ca68, args, NELEM(args), &status))
            return 0;
        if (status)
            return window_result(call, 0);
        if (active)
            *active = event[4];
        return (!output || cfp_move(output, args[1], sizeof(uint32_t))) && window_result(call, 1);
    }

    /* A consumed slot retains the window address with its sign bit set.
       Deactivation precedes activation when both are pending. */
    const uint32_t consumed = UINT32_C(0x80000000);
    uint32_t deactivate, activate;
    if (!cfp_get(0x0a68, &deactivate) || !cfp_get(0x0a64, &activate))
        return 0;
    const bool active_event = !deactivate || (deactivate & consumed);
    const uint32_t window = active_event ? activate : deactivate;
    if (!window || (window & consumed))
        return window_result(call, 0);
    if (active)
        *active = active_event;
    if (output && !cfp_write(output, window))
        return 0;
    if ((uint8_t)call->words[2]) {
        const uint32_t base = US_TO_SYN68K(ref->toc);
        if (!cfp_write(base + (active_event ? 0x2424 : 0x2420), 0) ||
            !cfp_write(active_event ? 0x0a64 : 0x0a68, window | consumed))
            return 0;
    }
    return window_result(call, 1);
}

static int windows_post_delayed_reshape(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    uint32_t state, queue;
    if (!windows_process_state(&state))
        return 0;
    if (!state)
        return ROMlib_carbon_report_failure("The saved WindowsLib process queue is unavailable");
    if (!cfp_get(state + 0x0c, &queue))
        return 0;
    if (!queue) {
        _Alignas(8) uint32_t encoded = 0;
        const uint32_t create[] = {4, 4, 0x77696e64, 0x6472696c, US_TO_SYN68K(&encoded)};
        uint32_t status;
        if (!cfp_private(ref, 0x1a920, create, NELEM(create), &status))
            return 0;
        if (status)
            return 1;
        queue = CL(encoded);
        if (!queue || !cfp_write(state + 0x0c, queue))
            return 0;
    }
    _Alignas(8) const uint32_t window = CL(call->words[0]);
    const uint32_t push[] = {queue, US_TO_SYN68K(&window)};
    return cfp_private(ref, 0x1ca50, push, NELEM(push), NULL);
}

static int windows_store_activation(const ROMlib_carbon_reference* ref, uint32_t window, uint8_t active)
{
    const uint32_t slot = active ? 0x0a64 : 0x0a68;
    const uint32_t cache = US_TO_SYN68K(ref->toc) + (active ? 0x2424 : 0x2420);
    if (!active) {
        uint32_t pending;
        if (!cfp_get(slot, &pending))
            return 0;
        /* Keep the first pending deactivation until the application reads it. */
        if (pending && !(pending & UINT32_C(0x80000000)))
            return 1;
    }
    if (!cfp_write(cache, window) || !cfp_write(slot, window))
        return 0;
    if (active)
        return cfp_private(ref, 0x1bda8, &window, 1, NULL);
    return cfp_move(0x0a22, window + 0x6c, sizeof(uint16_t));
}

static int windows_queue_activation(const ROMlib_carbon_reference* ref, uint32_t window, uint32_t record,
                                    uint8_t active)
{
    enum { floating_window_class = 5, suppress_activation_events = 0x00020000 };

    uint32_t state, parent;
    if (!cfp_get(record + 8, &state) || !cfp_get(record + 0x5c, &parent))
        return 0;
    if ((!state && !windows_process_state(&state)) || !state)
        return 0;
    const Byte* layer_kind = parent ? ROMlib_carbon_guest_memory(parent + 0x70, 1, 0) : NULL;
    if (!layer_kind)
        return 0;
    const uint32_t hilite_arguments[] = {window, active};
    if (*layer_kind == 1)
        return cfp_private(ref, 0x1bdd8, hilite_arguments, NELEM(hilite_arguments), NULL);

    uint32_t process, system_process;
    const uint32_t system_slot = cfp_toc(ref, 0x610);
    if (!system_slot || !cfp_get(system_slot, &system_process) || !windows_current_process(&process))
        return 0;
    if (process != system_process) {
        _Alignas(8) uint32_t serial[2];
        const uint32_t serial_address = US_TO_SYN68K(serial);
        uint32_t owner, front;
        if (!cfp_move(serial_address, state + 4, sizeof serial) ||
            !cfp_private(ref, 0x1bc70, &serial_address, 1, &owner) || !cfp_private(ref, 0x1bdf0, &owner, 1, &front))
            return 0;
        if (!(uint8_t)front)
            return 1;
    }
    uint32_t attributes, window_class;
    if (!cfp_private(ref, 0x1bdd8, hilite_arguments, NELEM(hilite_arguments), NULL) ||
        !cfp_get(record + 0x18, &attributes) || !cfp_get(record + 0x14, &window_class))
        return 0;
    if (attributes & suppress_activation_events)
        return 1;
    if (window_class == floating_window_class) {
        uint32_t queue;
        if (!cfp_get(state + 0x10, &queue))
            return 0;
        if (!queue)
            return 1;
        _Alignas(8) uint32_t event[2] = {CL(window), CL((uint32_t)active << 24)};
        const uint32_t args[] = {queue, US_TO_SYN68K(event)};
        return cfp_private(ref, 0x1be08, args, NELEM(args), NULL);
    }
    if (!cfp_private(ref, 0x1be20, &window, 1, &parent))
        return 0;
    if (!parent)
        return 1;
    layer_kind = ROMlib_carbon_guest_memory(parent + 0x70, 1, 0);
    return layer_kind && (*layer_kind || windows_store_activation(ref, window, active));
}

static int windows_post_activation_event(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0];
    uint32_t record;
    if (!windows_metadata_record(ref, window, &record))
        return 0;
    return !record || (windows_queue_activation(ref, window, record, (uint8_t)call->words[1]) &&
                       cfp_private(ref, 0x8954, NULL, 0, NULL));
}

static int windows_hilite_drag(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0];
    const uint8_t hilite_state = call->words[1];
    uint32_t record, old_hilite, features;
    if (!windows_metadata_record(ref, window, &record))
        return 0;
    if (!record)
        return window_result(call, window_invalid);
    if (!cfp_get(record + 0x64, &old_hilite) || !cfp_get(record + 0x30, &features))
        return 0;
    if (!((old_hilite ^ hilite_state) & 1) || !(features & 0x80))
        return window_result(call, noErr);
    const uint32_t port = cfp_toc(ref, 0x2460);
    GrafPtr manager_port = port ? ROMlib_carbon_guest_memory(port, sizeof(GrafPort), 0) : NULL;
    if (!manager_port || !cfp_write(record + 0x64, hilite_state))
        return 0;
    const uint32_t sound = hilite_state ? 0x64746869 : 0x64747568;
    if (!cfp_private(ref, 0x1c2b8, &sound, 1, NULL))
        return 0;
    GrafPtr saved_port = thePort;
    SetPort(manager_port);
    const uint32_t wdef[] = {window, 9, hilite_state != 0, 1};
    const int ok = cfp_private(ref, 0x126c4, &window, 1, NULL) && cfp_private(ref, 0x61ac, wdef, NELEM(wdef), NULL);
    SetPort(saved_port);
    return ok && window_result(call, noErr);
}

static int windows_proxy_file(const ROMlib_carbon_reference* ref, uint32_t offset, ROMlib_carbon_ppc_frame* call)
{

    const uint32_t window = call->words[0], output = call->words[1];
    const bool resolve = offset == windows_get_proxy_file;
    uint32_t record, collection;
    if (!windows_metadata_record(ref, window, &record))
        return 0;
    if (!record)
        return window_result(call, window_invalid);
    if (!output || !ROMlib_carbon_try_guest_memory(output, resolve ? sizeof(FSSpec) : sizeof(uint32_t), 1))
        return window_result(call, paramErr);
    if (!resolve && !cfp_write(output, 0))
        return 0;
    if (!cfp_get(record + 0x44, &collection))
        return 0;
    if (!collection)
        return window_result(call, proxy_not_registered);

    _Alignas(8) uint32_t encoded_size = 0;
    const uint32_t size_query[] = {collection, window_system_creator, proxy_file_tag, US_TO_SYN68K(&encoded_size), 0};
    uint32_t status;
    if (!cfp_private(ref, 0x1afc8, size_query, NELEM(size_query), &status))
        return 0;
    const int16_t query_error = (int16_t)status;
    if (query_error)
        return window_result(call, query_error == collection_missing_item ? proxy_not_registered : query_error);
    const uint32_t size = CL(encoded_size);
    if (!size || size > INT32_MAX)
        return window_result(call, paramErr);

    Handle alias = NewHandle(size);
    if (!alias)
        return window_result(call, memFullErr);
    const SignedByte saved_flags = HGetState(alias);
    HLock(alias);
    const uint32_t read[] = {record, window_system_creator, proxy_file_tag, size, 0, US_TO_SYN68K(STARH(alias))};
    int ok = cfp_private(ref, 0x25c8, read, NELEM(read), &status);
    HSetState(alias, saved_flags);
    if (!ok || status) {
        DisposHandle(alias);
        if (!ok)
            return 0;
        return window_result(call, (int32_t)status == window_missing_property ? proxy_not_registered : (int32_t)status);
    }
    if (!resolve) {
        if (cfp_write(output, US_TO_SYN68K(alias)))
            return window_result(call, noErr);
        DisposHandle(alias);
        return 0;
    }

    /* Resolution may move or resize the AliasHandle. If it changed, replace
       the original alias bytes with the updated copy before disposing it. */
    _Alignas(8) Byte changed = 0;
    const uint32_t arguments[] = {0, US_TO_SYN68K(alias), output, US_TO_SYN68K(&changed)};
    ok = cfp_private(ref, 0x1c768, arguments, NELEM(arguments), &status);
    if (ok && !(int16_t)status && changed) {
        HLock(alias);
        const uint32_t replace[] = {record, window_system_creator, proxy_file_tag, GetHandleSize(alias),
                                    US_TO_SYN68K(STARH(alias))};
        ok = cfp_private(ref, 0x2510, replace, NELEM(replace), NULL);
    }
    DisposHandle(alias);
    return ok && window_result(call, (int16_t)status);
}

static int windows_proxy_icon(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{

    const uint32_t window = call->words[0], output = call->words[1];
    uint32_t record, status;
    if (!windows_metadata_record(ref, window, &record))
        return 0;
    if (!record)
        return window_result(call, window_invalid);
    if (!output || !ROMlib_carbon_try_guest_memory(output, sizeof(uint32_t), 1))
        return window_result(call, paramErr);
    const uint32_t cached[] = {record, window_system_creator, proxy_icon_tag, 4, 0, output};
    if (!cfp_private(ref, 0x25c8, cached, NELEM(cached), &status))
        return 0;
    if (!status)
        return window_result(call, noErr);

    _Alignas(8) FSSpec spec;
    const uint32_t address = US_TO_SYN68K(&spec);
    ROMlib_carbon_ppc_frame file = {.words = {window, address}};
    if (!windows_proxy_file(ref, windows_get_proxy_file, &file))
        return 0;
    if (file.words[0])
        return window_result(call, (int32_t)file.words[0] == proxy_not_registered ? status : file.words[0]);
    _Alignas(8) int16_t label;
    const uint32_t lookup[] = {address, output, US_TO_SYN68K(&label)};
    if (!cfp_private(ref, 0x1c7f8, lookup, NELEM(lookup), &status))
        return 0;
    if ((int16_t)status)
        return window_result(call, (int16_t)status);

    const uint32_t store[] = {record, proxy_icon_tag, output};
    const int stored = cfp_private(ref, 0x5da4, store, NELEM(store), &status);
    if (stored && !status)
        return window_result(call, noErr);
    uint32_t icon;
    return cfp_get(output, &icon) && cfp_private(ref, 0x1c840, &icon, 1, NULL) && stored && window_result(call, status);
}

static int windows_redraw_proxy(const ROMlib_carbon_reference* ref, uint32_t record, uint32_t window)
{
    GrafPtr saved_port = thePort;
    const uint32_t message[] = {window, 0x0d, 1, 1};
    int ok = cfp_private(ref, 0xabe0, &record, 1, NULL) && cfp_private(ref, 0x61ac, message, NELEM(message), NULL);
    uint32_t parent = 0;
    const Byte* visible = ok ? ROMlib_carbon_guest_memory(window + 0x6e, 1, 0) : NULL;
    if (visible && *visible)
        ok = cfp_get(record + 0x5c, &parent);
    if (ok && parent) {
        const Byte* parent_visible = ROMlib_carbon_guest_memory(parent + 0x6e, 1, 0);
        ok = parent_visible && (!*parent_visible || cfp_private(ref, 0xaef8, &record, 1, NULL));
    }
    SetPort(saved_port);
    return ok && visible;
}

static int windows_clear_proxy_icon(const ROMlib_carbon_reference* ref, uint32_t record)
{
    _Alignas(8) uint32_t encoded = 0;
    const uint32_t get[] = {record, proxy_icon_tag, US_TO_SYN68K(&encoded)};
    uint32_t status;
    if (!cfp_private(ref, 0x5e58, get, NELEM(get), &status))
        return 0;
    if ((int32_t)status == window_missing_property)
        return 1;
    if (status)
        return 0;
    const uint32_t remove[] = {record, window_system_creator, proxy_icon_tag};
    const uint32_t icon = CL(encoded);
    return cfp_private(ref, 0x2678, remove, NELEM(remove), &status) && !status &&
           cfp_private(ref, 0x1c840, &icon, 1, NULL);
}

static int windows_assign_proxy_alias(const ROMlib_carbon_reference* ref, uint32_t window, uint32_t record,
                                      uint32_t address, ROMlib_carbon_ppc_frame* call)
{
    uint32_t size;
    const void* data = cfp_handle_data(address, &size, 0);
    if (!data || !size)
        return window_result(call, paramErr);
    Handle alias = (Handle)SYN68K_TO_US(address);
    const SignedByte flags = HGetState(alias);
    HLock(alias);
    const uint32_t args[] = {record, window_system_creator, proxy_file_tag, size, US_TO_SYN68K(data)};
    uint32_t status;
    const int ok = cfp_private(ref, 0x2510, args, NELEM(args), &status);
    HSetState(alias, flags);
    if (!ok)
        return 0;
    if (status)
        return window_result(call, status);
    return windows_clear_proxy_icon(ref, record) && windows_redraw_proxy(ref, record, window) &&
           window_result(call, noErr);
}

static int windows_assign_proxy_icon(const ROMlib_carbon_reference* ref, uint32_t record, uint32_t window,
                                     uint32_t icon, ROMlib_carbon_ppc_frame* call)
{
    uint32_t status;
    if (!cfp_private(ref, 0x1ca20, &icon, 1, &status))
        return 0;
    if ((int16_t)status)
        return window_result(call, (int16_t)status);

    _Alignas(8) uint32_t previous = 0;
    const uint32_t get[] = {record, proxy_icon_tag, US_TO_SYN68K(&previous)};
    uint32_t old_status;
    if (!cfp_private(ref, 0x5e58, get, NELEM(get), &old_status))
        return 0;
    _Alignas(8) const uint32_t encoded = CL(icon);
    const uint32_t set[] = {record, proxy_icon_tag, US_TO_SYN68K(&encoded)};
    if (!cfp_private(ref, 0x5da4, set, NELEM(set), &status))
        return 0;
    const uint32_t old_icon = old_status ? 0 : CL(previous);
    const uint32_t release = status ? icon : old_icon;
    if (release && !cfp_private(ref, 0x1c840, &release, 1, NULL))
        return 0;
    return windows_redraw_proxy(ref, record, window) && window_result(call, status);
}

static int windows_change_proxy(const ROMlib_carbon_reference* ref, uint32_t offset, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0], value = call->words[1];
    if ((offset == windows_set_proxy_icon && !value) ||
        (offset == windows_set_proxy_file && (!value || !ROMlib_carbon_try_guest_memory(value, sizeof(FSSpec), 0))))
        return window_result(call, paramErr);

    uint32_t record;
    if (!windows_metadata_record(ref, window, &record))
        return 0;
    if (!record)
        return window_result(call, window_invalid);
    if (offset == windows_set_proxy_alias)
        return windows_assign_proxy_alias(ref, window, record, value, call);
    if (offset == windows_set_proxy_file) {
        _Alignas(8) uint32_t output = 0;
        const uint32_t args[] = {0, value, US_TO_SYN68K(&output)};
        uint32_t status;
        if (!cfp_private(ref, 0x1b178, args, NELEM(args), &status))
            return 0;
        if ((int16_t)status)
            return window_result(call, (int16_t)status);
        const uint32_t alias = CL(output);
        const int ok = windows_assign_proxy_alias(ref, window, record, alias, call);
        if (alias)
            DisposHandle((Handle)SYN68K_TO_US(alias));
        return ok;
    }
    if (offset == windows_set_proxy_icon)
        return windows_assign_proxy_icon(ref, record, window, value, call);
    if (offset == windows_set_proxy_type) {
        _Alignas(8) uint32_t output = 0;
        const uint32_t args[] = {(int16_t)call->words[3], value, call->words[2], US_TO_SYN68K(&output)};
        uint32_t status;
        if (!cfp_private(ref, 0x1c810, args, NELEM(args), &status))
            return 0;
        if ((int16_t)status)
            return window_result(call, (int16_t)status);
        const uint32_t icon = CL(output);
        const int ok = windows_assign_proxy_icon(ref, record, window, icon, call);
        return (!icon || cfp_private(ref, 0x1c840, &icon, 1, NULL)) && ok;
    }

    const uint32_t remove[] = {record, window_system_creator, proxy_file_tag};
    return windows_clear_proxy_icon(ref, record) && cfp_private(ref, 0x2678, remove, NELEM(remove), NULL) &&
           windows_redraw_proxy(ref, record, window) && window_result(call, noErr);
}

static int windows_begin_proxy(const ROMlib_carbon_reference* ref, uint32_t window, uint32_t record, uint32_t outline,
                               uint32_t* drag, uint32_t* status)
{
    _Alignas(8) uint32_t icon;
    ROMlib_carbon_ppc_frame get_icon = {.words = {window, US_TO_SYN68K(&icon)}};
    *drag = 0;
    if (!windows_proxy_icon(ref, &get_icon))
        return 0;
    if (get_icon.words[0]) {
        *status = proxy_not_registered;
        return 1;
    }
    _Alignas(8) uint32_t encoded_drag = 0;
    const uint32_t drag_output = US_TO_SYN68K(&encoded_drag);
    if (!cfp_private(ref, 0x1c9c0, &drag_output, 1, status))
        return 0;
    const uint32_t new_drag = CL(encoded_drag);
    *status = (int16_t)*status;
    if (*status)
        return 1;

    _Alignas(8) const Byte flag = 1;
    const uint32_t flavor[] = {new_drag, proxy_drag_item, proxy_drag_item, US_TO_SYN68K(&flag), 1, 4};
    int ok = cfp_private(ref, 0x1c9d8, flavor, NELEM(flavor), status);
    *status = (int16_t)*status;
    if (!ok || *status)
        return ok && cfp_private(ref, 0x1c9a8, &new_drag, 1, NULL);

    /* WDEF message 12 consumes two preallocated regions and can supply a
       GWorld. Keep the whole record in the window until EndWindowProxyDrag. */
    _Alignas(8) uint32_t image[3] = {0};
    const uint32_t image_address = US_TO_SYN68K(image);
    uint32_t region;
    for (unsigned index = 1; index < NELEM(image); ++index) {
        ok = cfp_private(ref, 0x136f8, NULL, 0, &region);
        if (!ok || !region) {
            *status = memFullErr;
            if (ok && index == 2) {
                const uint32_t first_region = CL(image[1]);
                ok = cfp_private(ref, 0x137bc, &first_region, 1, NULL);
            }
            return ok && cfp_private(ref, 0x1c9a8, &new_drag, 1, NULL);
        }
        image[index] = CL(region);
    }
    const uint32_t create[] = {window, new_drag, image_address};
    ok = cfp_private(ref, 0x4928, create, NELEM(create), status);
    if (ok && !*status) {
        const uint32_t copy[] = {CL(image[2]), outline};
        const uint32_t save[] = {record, window_system_creator, proxy_drag_image_tag, sizeof image, image_address};
        ok = cfp_private(ref, 0x1bc28, copy, NELEM(copy), NULL) && cfp_private(ref, 0x2510, save, NELEM(save), status);
    }
    if (ok && !*status) {
        *drag = new_drag;
        return 1;
    }
    return ok && cfp_private(ref, 0x49a4, &image_address, 1, NULL) && cfp_private(ref, 0x1c9a8, &new_drag, 1, NULL);
}

static int windows_end_proxy(const ROMlib_carbon_reference* ref, uint32_t window, uint32_t record, uint32_t drag,
                             uint32_t* status)
{
    ROMlib_carbon_ppc_frame highlight = {.words = {window, 0}};
    if (!cfp_private(ref, 0x1c9a8, &drag, 1, NULL) || !windows_hilite_drag(ref, &highlight))
        return 0;

    _Alignas(8) uint32_t image[3];
    const uint32_t image_address = US_TO_SYN68K(image);
    const uint32_t read[] = {record, window_system_creator, proxy_drag_image_tag, sizeof image, 0, image_address};
    if (!cfp_private(ref, 0x25c8, read, NELEM(read), status))
        return 0;
    if (*status)
        return 1;
    const uint32_t remove[] = {record, window_system_creator, proxy_drag_image_tag};
    return cfp_private(ref, 0x49a4, &image_address, 1, NULL) && cfp_private(ref, 0x2678, remove, NELEM(remove), NULL);
}

static int windows_proxy_mouse_moved(const ROMlib_carbon_reference* ref, bool* moved)
{
    _Alignas(8) Point previous, current;
    const uint32_t previous_address = US_TO_SYN68K(&previous), current_address = US_TO_SYN68K(&current);
    uint32_t start;
    *moved = false;
    if (!cfp_private(ref, 0x1c348, &previous_address, 1, NULL) || !cfp_private(ref, 0x1c5e8, NULL, 0, &start))
        return 0;
    for (;;) {
        uint32_t now, down;
        if (!cfp_private(ref, 0x1c5e8, NULL, 0, &now))
            return 0;
        if ((uint32_t)(now - start) > 15)
            return 1;
        if (!cfp_private(ref, 0x1c330, NULL, 0, &down))
            return 0;
        if (!(uint8_t)down)
            return 1;
        if (!cfp_private(ref, 0x1c348, &current_address, 1, NULL))
            return 0;
        const int horizontal = (int16_t)(CW(current.h) - CW(previous.h));
        const int vertical = (int16_t)(CW(current.v) - CW(previous.v));
        if ((int16_t)(abs(horizontal) + abs(vertical)) > 3) {
            *moved = true;
            return 1;
        }
        previous = current;
        if (!cfp_private(ref, 0x1c360, NULL, 0, NULL))
            return 0;
    }
}

static int windows_track_proxy(const ROMlib_carbon_reference* ref, uint32_t record, const ROMlib_carbon_ppc_frame* call,
                               uint32_t* status)
{
    const uint32_t window = call->words[0], point = call->words[1], drag = call->words[2];
    const uint32_t outline = call->words[3];
    bool moved;
    if (!windows_proxy_mouse_moved(ref, &moved))
        return 0;
    if (moved) {
        *status = -5607;
        return 1;
    }
    const Byte* flags = ROMlib_carbon_guest_memory(record + 0x60, 1, 0);
    if (!flags)
        return 0;
    *status = userCanceledErr;
    if (*flags & 2)
        return 1;
    ROMlib_carbon_ppc_frame highlight = {.words = {window, 1}};
    uint32_t accepted;
    if (!windows_hilite_drag(ref, &highlight) || !cfp_private(ref, 0x1c9f0, &point, 1, &accepted))
        return 0;
    if (!(uint8_t)accepted)
        return 1;

    _Alignas(8) FSSpec file;
    ROMlib_carbon_ppc_frame get_file = {.words = {window, US_TO_SYN68K(&file)}};
    if (!windows_proxy_file(ref, windows_get_proxy_file, &get_file))
        return 0;
    if (!get_file.words[0]) {
        _Alignas(8) Byte flavor[80] = {0};
        const uint32_t pack[] = {get_file.words[1], US_TO_SYN68K(flavor)};
        if (!cfp_private(ref, 0x4440, pack, NELEM(pack), status))
            return 0;
        if (*status)
            return 1;
        const uint32_t add[] = {drag, proxy_drag_item, 0x68667320, pack[1], sizeof flavor, 0};
        if (!cfp_private(ref, 0x1c9d8, add, NELEM(add), status))
            return 0;
        *status = (int16_t)*status;
        if (*status)
            return 1;
    }

    uint32_t tick;
    if (!cfp_private(ref, 0x1c5e8, NULL, 0, &tick))
        return 0;
    _Alignas(8) EventRecord event = {
        .what = CWC(mouseDown),
        .message = CL(window),
        .when = CL(tick),
        .where = {.v = CW(point >> 16), .h = CW(point)},
    };
    const uint32_t track[] = {drag, US_TO_SYN68K(&event), outline};
    if (!cfp_private(ref, 0x1ca08, track, NELEM(track), status))
        return 0;
    *status = (int16_t)*status;
    return 1;
}

static int windows_proxy_drag(const ROMlib_carbon_reference* ref, uint32_t offset, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0], argument = call->words[1];
    uint32_t record, status;
    if (!windows_metadata_record(ref, window, &record))
        return 0;
    if (!record)
        return window_result(call, window_invalid);
    if (offset == windows_end_proxy_drag)
        return !argument ? window_result(call, paramErr)
                         : windows_end_proxy(ref, window, record, argument, &status) && window_result(call, status);
    if (offset == windows_track_existing_proxy_drag)
        return windows_track_proxy(ref, record, call, &status) && window_result(call, status);

    uint32_t drag;
    if (offset == windows_begin_proxy_drag) {
        const uint32_t outline = call->words[2];
        if (!argument || !ROMlib_carbon_try_guest_memory(argument, sizeof(uint32_t), 1))
            return window_result(call, paramErr);
        uint32_t size;
        if (!cfp_write(argument, 0))
            return 0;
        if (!cfp_handle_data(outline, &size, 1) || size < 10)
            return window_result(call, paramErr);
        return windows_begin_proxy(ref, window, record, outline, &drag, &status) && cfp_write(argument, drag) &&
               window_result(call, status);
    }

    _Alignas(8) FSSpec file;
    ROMlib_carbon_ppc_frame get_file = {.words = {window, US_TO_SYN68K(&file)}};
    if (!windows_proxy_file(ref, windows_get_proxy_file, &get_file))
        return 0;
    if (get_file.words[0])
        return window_result(call, get_file.words[0]);
    uint32_t outline;
    if (!cfp_private(ref, 0x136f8, NULL, 0, &outline))
        return 0;
    if (!outline)
        return window_result(call, -666);
    int ok = windows_begin_proxy(ref, window, record, outline, &drag, &status);
    if (ok && !status) {
        ROMlib_carbon_ppc_frame track = {.words = {window, argument, drag, outline}};
        uint32_t cleanup_status;
        ok = windows_track_proxy(ref, record, &track, &status) &&
             windows_end_proxy(ref, window, record, drag, &cleanup_status);
    }
    return ok && cfp_private(ref, 0x137bc, &outline, 1, NULL) && window_result(call, status);
}

static int windows_read_features(const ROMlib_carbon_reference* ref, uint32_t window, uint32_t record,
                                 uint32_t* features, int32_t* status)
{
    enum { queried = 0x80, supports_query = 0x40, features_unavailable = -30580 };

    *features = 0;
    if (!record) {
        *status = window_invalid;
        return 1;
    }
    Byte* flags = ROMlib_carbon_guest_memory(record + 0x60, 1, 1);
    if (!flags)
        return 0;
    if ((*flags & (queried | supports_query)) == supports_query) {
        _Alignas(8) uint32_t reported = 0;
        const uint32_t args[] = {window, 7, US_TO_SYN68K(&reported), 1};
        uint32_t handled;
        if (!cfp_private(ref, 0x61ac, args, NELEM(args), &handled) ||
            !cfp_write(record + 0x30, handled ? CL(reported) : 0))
            return 0;
        /* The WDEF may update the other window state bits while handling the message. */
        *flags |= queried;
        if (!handled)
            *flags &= ~supports_query;
    }
    *status = *flags & supports_query ? noErr : features_unavailable;
    return *status || cfp_get(record + 0x30, features);
}

static int windows_features(const ROMlib_carbon_reference* ref, uint32_t offset, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0], argument = call->words[1];
    if (offset == windows_get_features && !argument)
        return window_result(call, paramErr);
    if (offset == windows_get_features && !cfp_write(argument, 0))
        return 0;
    uint32_t record;
    if (!windows_metadata_record(ref, window, &record))
        return 0;
    if (offset == windows_set_features)
        return !record || cfp_write(record + 0x30, argument);
    if (offset == windows_is_collapsed) {
        const Byte* flags = record ? ROMlib_carbon_guest_memory(record + 0x60, 1, 0) : NULL;
        return (!record || flags) && window_result(call, flags && (*flags & 1));
    }
    uint32_t features;
    int32_t status;
    if (!windows_read_features(ref, window, record, &features, &status))
        return 0;
    if (offset == windows_is_collapsable)
        return window_result(call, !status && (features & 4));
    return cfp_write(argument, features) && window_result(call, status);
}

static int windows_read_window_region(const ROMlib_carbon_reference* ref, uint32_t window, uint16_t region_code,
                                      uint32_t region, uint32_t* result)
{
    uint32_t extent;
    if (!window || !cfp_handle_data(region, &extent, 1) || extent < 10) {
        *result = paramErr;
        return 1;
    }
    uint32_t record, features;
    int32_t status;
    if (!windows_metadata_record(ref, window, &record) ||
        !windows_read_features(ref, window, record, &features, &status))
        return 0;
    if (status || !(features & 0x10)) {
        *result = -30580;
        return 1;
    }
    uint32_t parent;
    if (!cfp_get(record + 0x5c, &parent) || !cfp_private(ref, 0x1a9e0, &region, 1, NULL))
        return 0;
    Byte* visible = ROMlib_carbon_guest_memory(window + 0x6e, 1, 1);
    const Byte* parent_visible = parent ? ROMlib_carbon_guest_memory(parent + 0x6e, 1, 0) : NULL;
    if (!visible || (parent && !parent_visible))
        return 0;
    const Byte saved_visible = *visible;
    const bool needs_regions = !saved_visible || !parent_visible || !*parent_visible;
    if (needs_regions) {
        *visible = 1;
        if (!cfp_private(ref, 0x6558, &window, 1, NULL)) {
            *visible = saved_visible;
            return 0;
        }
    }

    _Alignas(8) const struct {
        uint32_t region;
        uint16_t code;
        uint16_t reserved;
    } data = {CL(region), CW(region_code), 0};

    const uint32_t message[] = {window, 8, US_TO_SYN68K(&data), 1};
    const int ok = cfp_private(ref, 0x61ac, message, NELEM(message), result);
    if (!needs_regions)
        return ok;
    *visible = saved_visible;
    return ok && cfp_private(ref, 0x6558, &window, 1, NULL);
}

static int windows_global_find_window(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t point = call->words[0], output = call->words[1];
    if (!output || !ROMlib_carbon_try_guest_memory(output, sizeof(uint32_t), 1))
        return window_result(call, 0);
    uint32_t root;
    if (!cfp_private(ref, 0x14c4c, NULL, 0, &root))
        return 0;
    const uint32_t manager = cfp_toc(ref, 0x2464);
    GrafPtr port = manager ? ROMlib_carbon_guest_memory(manager, sizeof(GrafPort), 0) : NULL;
    if (!port || !cfp_write(output, 0))
        return 0;
    GrafPtr saved_port = thePort;
    SetPort(port);
    const uint32_t menu[] = {1, 0, point};
    uint32_t result;
    int ok = cfp_private(ref, 0x1ad58, menu, NELEM(menu), &result);
    int16_t part = 0;
    if (ok && (int32_t)result >= 0)
        part = 1;
    else if (ok && root) {
        uint32_t window;
        ok = cfp_get(root + 0x94, &window);
        for (unsigned visited = 0; ok && window && visited < UINT16_MAX; ++visited) {
            const Byte* info = ROMlib_carbon_guest_memory(window, 0x98, 0);
            if (!info) {
                ok = 0;
                break;
            }
            if (info[0x6e]) {
                uint32_t region, contains = 0;
                ok = cfp_get(window + 0x72, &region);
                if (ok && region) {
                    const uint32_t args[] = {point, region};
                    ok = cfp_private(ref, 0x1bc40, args, NELEM(args), &contains);
                }
                if (!ok)
                    break;
                if ((uint8_t)contains) {
                    /* The saved manager identifies layer records by their port marker. */
                    if (info[0x4a] == 0xde && info[0x4b] == 0xad) {
                        ok = cfp_get(window + 0x94, &window);
                        continue;
                    }
                    const uint32_t hit[] = {window, 1, point, 1};
                    ok = cfp_private(ref, 0x61ac, hit, NELEM(hit), &result);
                    if (ok && (int16_t)result > 0) {
                        part = (int16_t)result + 2;
                        ok = cfp_write(output, window);
                        break;
                    }
                }
            }
            ok = ok && cfp_get(window + 0x90, &window);
        }
    }
    SetPort(saved_port);
    return ok && window_result(call, part);
}

static int windows_install_content_paint(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0], procedure = call->words[1], options = call->words[2];
    uint32_t record;
    if (!windows_metadata_record(ref, window, &record))
        return 0;
    if (!record)
        return window_result(call, window_invalid);
    if (options)
        return window_result(call, paramErr);
    return cfp_write(record + 0x38, procedure) && cfp_write(record + 0x3c, procedure ? call->words[3] : 0) &&
           window_result(call, noErr);
}

static int windows_erase_content(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0], region = call->words[1];
    uint32_t extent;
    if (!window || !cfp_handle_data(region, &extent, 1) || extent < 10)
        return window_result(call, paramErr);
    uint32_t record, painter, window_background;
    if (!windows_metadata_record(ref, window, &record))
        return 0;
    if (!record)
        return window_result(call, window_invalid);
    if (!cfp_get(record + 0x38, &painter) || !cfp_get(record + 0x40, &window_background))
        return 0;

    /* Keep the saved ToolboxUtils pen/color state record opaque. The saved
       paint worker also implements the Mixed Mode ABI for the client UPP. */
    _Alignas(8) Byte drawing_state[0x60];
    const uint32_t drawing[] = {US_TO_SYN68K(drawing_state), 1};
    if (!cfp_private(ref, 0x1c120, drawing, NELEM(drawing), NULL))
        return 0;
    _Alignas(8) Byte erased = 0;
    const uint32_t args[] = {record, region, US_TO_SYN68K(&erased)};
    uint32_t remaining = (uint32_t)-666;
    int ok = !painter || cfp_private(ref, 0xc6c8, args, NELEM(args), &remaining);
    if (ok && remaining && window_background) {
        ROMlib_carbon_ppc_frame background = *call;
        memcpy(background.words, args, sizeof args);
        ok = ROMlib_carbon_invoke_transition_vector(window_background, &background);
        remaining = background.words[0];
    }
    if (ok && remaining)
        ok = cfp_private(ref, 0x1c090, &region, 1, NULL);
    return ok && cfp_private(ref, 0x1c180, drawing, NELEM(drawing), NULL) && window_result(call, noErr);
}

enum { desktop_pattern_key = 0x64747074, desktop_pattern_name_key = 0x6474706e };

static int windows_desktop_preferences(const ROMlib_carbon_reference* ref, uint32_t* prefs, uint32_t* status)
{
    _Alignas(8) uint32_t encoded = 0;
    const uint32_t output = US_TO_SYN68K(&encoded);
    const int ok = cfp_private(ref, 0x114c0, &output, 1, status);
    *prefs = CL(encoded);
    return ok;
}

static int windows_save_desktop_pattern(const ROMlib_carbon_reference* ref, uint32_t prefs, uint32_t pattern,
                                        const Byte* name, uint32_t* status)
{
    const uint32_t data[] = {prefs, desktop_pattern_key, 0, pattern};
    const uint32_t label[] = {prefs, desktop_pattern_name_key, 0, (unsigned)name[0] + 1, US_TO_SYN68K(name)};
    return cfp_private(ref, 0x1ae78, data, NELEM(data), status) &&
           (*status || cfp_private(ref, 0x1ae90, label, NELEM(label), status));
}

static int windows_read_desktop_preferences(const ROMlib_carbon_reference* ref, uint32_t prefs, bool want_pattern,
                                            Byte* name, Handle* pattern, uint32_t* status)
{
    if (!cfp_private(ref, 0x1aa70, &prefs, 1, status))
        return 0;
    if (*status)
        return 1;
    _Alignas(8) uint32_t size = CLC(256);
    const uint32_t label[] = {prefs, desktop_pattern_name_key, 0, US_TO_SYN68K(&size), name ? US_TO_SYN68K(name) : 0};
    int ok = !name || cfp_private(ref, 0x1add0, label, NELEM(label), status);
    if (ok && want_pattern && !*status) {
        size = 0;
        const uint32_t query[] = {prefs, desktop_pattern_key, 0, US_TO_SYN68K(&size), 0};
        ok = cfp_private(ref, 0x1add0, query, NELEM(query), status);
        if (ok && !*status) {
            const uint32_t bytes = CL(size);
            *pattern = bytes && bytes <= INT32_MAX ? NewHandleClear(bytes) : NULL;
            if (!*pattern)
                *status = memFullErr;
            else {
                const uint32_t get[] = {prefs, desktop_pattern_key, 0, US_TO_SYN68K(*pattern)};
                ok = cfp_private(ref, 0x1ae00, get, NELEM(get), status);
            }
        }
    }
    if (*pattern && (!ok || *status)) {
        DisposHandle(*pattern);
        *pattern = NULL;
    }
    return ok && cfp_private(ref, 0x1ae30, &prefs, 1, NULL);
}

static int windows_default_desktop_pattern(const ROMlib_carbon_reference* ref, uint32_t prefs, bool want_pattern,
                                           Byte* name, Handle* pattern, uint32_t* status)
{
    const uint32_t resource[] = {0x70706174, 16};
    uint32_t handle;
    if (!cfp_private(ref, 0x1aab8, resource, NELEM(resource), &handle) || !cfp_private(ref, 0x1ae48, NULL, 0, status))
        return 0;
    *status = (int16_t)*status;
    if (*status || !handle) {
        if (!*status)
            *status = memFullErr;
        return 1;
    }
    _Alignas(8) Str255 label = {0};
    _Alignas(8) uint32_t type;
    _Alignas(8) int16_t id;
    const uint32_t info[] = {handle, US_TO_SYN68K(&id), US_TO_SYN68K(&type), US_TO_SYN68K(label)};
    int ok = cfp_private(ref, 0x1ae60, info, NELEM(info), NULL) &&
             windows_save_desktop_pattern(ref, prefs, handle, label, status);
    if (ok && !*status) {
        if (want_pattern) {
            Handle copy = (Handle)SYN68K_TO_US(handle);
            *status = HandToHand(&copy);
            if (!*status)
                *pattern = copy;
        }
        if (name)
            memcpy(name, label, (unsigned)label[0] + 1);
    }
    return ok && cfp_private(ref, 0x1aed8, &handle, 1, NULL);
}

static int windows_decode_desktop_pattern(const ROMlib_carbon_reference* ref, Handle flattened, uint32_t output,
                                          uint32_t* status)
{
    const SignedByte held = HGetState(flattened);
    HLock(flattened);
    const uint32_t arguments[] = {US_TO_SYN68K(STARH(flattened)), GetHandleSize(flattened), output};
    const int ok = cfp_private(ref, 0x1af20, arguments, NELEM(arguments), status);
    HSetState(flattened, held);
    *status = (int16_t)*status;
    return ok;
}

static int windows_install_desktop_pattern(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t handle = call->words[0], label = call->words[1];
    uint32_t size;
    const Byte* name = label ? ROMlib_carbon_try_guest_memory(label, 1, 0) : NULL;
    if (!cfp_handle_data(handle, &size, 0) || !size || !name ||
        !ROMlib_carbon_try_guest_memory(label, (unsigned)*name + 1, 0))
        return window_result(call, paramErr);

    uint32_t prefs, status;
    if (!windows_desktop_preferences(ref, &prefs, &status))
        return 0;
    if (status)
        return window_result(call, status);

    GrafPtr saved_port = thePort;
    THz saved_zone = GetZone();
    SetZone(SystemZone());
    _Alignas(8) uint32_t pattern = 0;
    int ok = windows_decode_desktop_pattern(ref, (Handle)SYN68K_TO_US(handle), US_TO_SYN68K(&pattern), &status);
    if (ok && !status) {
        ok = windows_save_desktop_pattern(ref, prefs, handle, name, &status);
        const uint32_t decoded = CL(pattern);
        if (ok && !status)
            ok = cfp_private(ref, 0x7390, &decoded, 1, NULL) && cfp_private(ref, 0x14f60, NULL, 0, NULL);
        else if (decoded)
            DisposPixPat((PixPatHandle)SYN68K_TO_US(decoded));
    }
    SetZone(saved_zone);
    SetPort(saved_port);
    return ok && window_result(call, status);
}

static int windows_get_desktop(const ROMlib_carbon_reference* ref, bool decode, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t output = call->words[0], label = call->words[1];
    Byte* name = label ? ROMlib_carbon_try_guest_memory(label, sizeof(Str255), 1) : NULL;
    if ((label && !name) || (output && !ROMlib_carbon_try_guest_memory(output, sizeof(uint32_t), 1)))
        return window_result(call, paramErr);
    if (name)
        *name = 0;
    if (output && !cfp_write(output, 0))
        return 0;

    uint32_t prefs, status;
    Handle flattened = NULL;
    if (!windows_desktop_preferences(ref, &prefs, &status))
        return 0;
    if (status)
        return window_result(call, status);
    if (!windows_read_desktop_preferences(ref, prefs, output != 0, name, &flattened, &status) ||
        (status && !windows_default_desktop_pattern(ref, prefs, output != 0, name, &flattened, &status)))
        return 0;
    if (!flattened)
        return window_result(call, status);
    if (!decode) {
        if (cfp_write(output, US_TO_SYN68K(flattened)))
            return window_result(call, status);
        DisposHandle(flattened);
        return 0;
    }
    const int ok = windows_decode_desktop_pattern(ref, flattened, output, &status);
    DisposHandle(flattened);
    return ok && window_result(call, status);
}

static int windows_draw_saved_desktop(const ROMlib_carbon_reference* ref)
{
    uint32_t port, region;
    if (!cfp_get(0x0d2c, &port) || !port || !cfp_get(port + 0x1c, &region) || !region)
        return 0;
    const uint32_t clip[] = {region, INT16_MIN, INT16_MIN, INT16_MAX, INT16_MAX};
    const uint32_t redraw[] = {0, US_TO_SYN68K(MR(GrayRgn))};
    return cfp_private(ref, 0x1bfd0, clip, NELEM(clip), NULL) && cfp_private(ref, 0x1b940, redraw, NELEM(redraw), NULL);
}

static int windows_rebuild_saved_desktop(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t manager = cfp_toc(ref, 0x2464);
    GrafPtr port = manager ? ROMlib_carbon_guest_memory(manager, sizeof(GrafPort), 0) : NULL;
    if (!port)
        return 0;
    uint32_t desktop_region;
    if (!cfp_get(0x09ee, &desktop_region))
        return 0;
    if (!desktop_region || desktop_region == UINT32_MAX) {
        RgnHandle new_gray = NewRgn();
        if (!new_gray)
            return window_result(call, QDError());
        desktop_region = US_TO_SYN68K(new_gray);
        if (!cfp_write(0x09ee, desktop_region)) {
            DisposeRgn(new_gray);
            return 0;
        }
    }
    uint32_t scratch, port_visible;
    if (!cfp_get(manager + 0x18, &port_visible) || !cfp_private(ref, 0x136f8, NULL, 0, &scratch))
        return 0;
    if (!scratch)
        return window_result(call, MemError());
    GrafPtr saved_port = thePort;
    _Alignas(8) Rect limit;
    const uint32_t state = US_TO_SYN68K(ref->toc);
    int ok = cfp_move(US_TO_SYN68K(&limit), state + 0x125c, sizeof limit);
    if (ok) {
        SetPort(port);
        ClipRect(&limit);
        ok = cfp_private(ref, 0x1a9e0, &port_visible, 1, NULL) && cfp_private(ref, 0x1a9e0, &desktop_region, 1, NULL);
    }
    _Alignas(8) uint32_t callback[3] = {CL(scratch), CLC(3), 0};
    const uint32_t enumerate[] = {0, state + 0x11fc, US_TO_SYN68K(callback), 5};
    if (ok)
        ok = cfp_private(ref, 0x1ad28, enumerate, NELEM(enumerate), NULL);

    uint32_t status = CL(callback[2]), process_manager;
    if (ok && !status) {
        ok = cfp_private(ref, 0x1ad40, NULL, 0, &process_manager);
        if (ok && (uint8_t)process_manager) {
            const uint32_t menu[] = {12, 0, scratch};
            const uint32_t subtract[] = {desktop_region, scratch, desktop_region};
            ok = cfp_private(ref, 0x1ad58, menu, NELEM(menu), NULL) &&
                 cfp_private(ref, 0x1ad70, subtract, NELEM(subtract), NULL);
            status = QDError();
        }
    }
    if (ok && status)
        RectRgn((RgnHandle)SYN68K_TO_US(port_visible), &limit);
    if (ok)
        SetClip((RgnHandle)SYN68K_TO_US(port_visible));
    SetPort(saved_port);
    return ok && cfp_private(ref, 0x137bc, &scratch, 1, NULL) && window_result(call, status);
}

enum {
    monitor_desktop_list_slot = 0x7a4,
    monitor_desktop_record_size = 0x174,
    monitor_desktop_picture = 0,
    monitor_desktop_preview = 8,
    monitor_desktop_alignment = 0x0c,
    monitor_desktop_flags = 0x10,
    monitor_desktop_device = 0x18,
    monitor_desktop_world = 0x1c,
    monitor_desktop_cache_flags = 0x20,
    monitor_desktop_tile = 5,
};

typedef struct {
    uint32_t records;
    uint32_t count;
} monitor_desktop_list;

static uint32_t windows_monitor_word(const Byte* address)
{
    uint32_t word;
    memcpy(&word, address, sizeof word);
    return CL(word);
}

static void windows_set_monitor_word(Byte* address, uint32_t word)
{
    const uint32_t encoded = CL(word);
    memcpy(address, &encoded, sizeof encoded);
}

static uint32_t windows_monitor_alignment(const Byte* record)
{
    return windows_monitor_word(record + monitor_desktop_flags) & 1
               ? monitor_desktop_tile
               : windows_monitor_word(record + monitor_desktop_alignment);
}

static void windows_update_monitor_alignment(Byte* record, uint32_t alignment)
{
    const uint32_t old_flags = windows_monitor_word(record + monitor_desktop_flags);
    const uint32_t old_alignment = windows_monitor_word(record + monitor_desktop_alignment);
    const bool tiled = alignment == monitor_desktop_tile;
    const uint32_t new_alignment = tiled ? 0 : alignment;
    const uint32_t flags = (old_flags & ~1u) | tiled;
    if (old_alignment != new_alignment || flags != old_flags) {
        windows_set_monitor_word(record + monitor_desktop_flags, flags);
        windows_set_monitor_word(record + monitor_desktop_alignment, new_alignment);
        const uint32_t cache_flags = windows_monitor_word(record + monitor_desktop_cache_flags);
        windows_set_monitor_word(record + monitor_desktop_cache_flags, cache_flags & ~1u);
    }
}

static int windows_resolve_monitor_list(const ROMlib_carbon_reference* ref, uint32_t* handle)
{
    return *handle != UINT32_MAX || cfp_get(US_TO_SYN68K(ref->toc) + monitor_desktop_list_slot, handle);
}

static bool windows_monitor_list(uint32_t handle, monitor_desktop_list* list)
{
    uint32_t extent;
    const Byte* header = cfp_handle_data(handle, &extent, 0);
    if (!header || extent < 12 || windows_monitor_word(header) != monitor_desktop_record_size)
        return false;
    list->count = windows_monitor_word(header + 4);
    list->records = windows_monitor_word(header + 8);
    return cfp_handle_data(list->records, &extent, 0) && list->count <= extent / monitor_desktop_record_size;
}

static Byte* windows_monitor_record(const monitor_desktop_list* list, uint32_t index, bool writing)
{
    if (index == INT32_MAX)
        index = list->count;
    if (!index || index > list->count)
        return NULL;
    uint32_t extent;
    Byte* records = cfp_handle_data(list->records, &extent, writing);
    return records && index <= extent / monitor_desktop_record_size
               ? records + (index - 1) * monitor_desktop_record_size
               : NULL;
}

static int windows_dispose_monitor_records(const ROMlib_carbon_reference* ref, uint32_t handle)
{
    monitor_desktop_list list;
    if (!windows_monitor_list(handle, &list))
        return 0;
    for (uint32_t index = 1; index <= list.count; ++index) {
        const Byte* record = windows_monitor_record(&list, index, false);
        if (!record)
            return 0;
        const uint32_t picture = windows_monitor_word(record + monitor_desktop_picture);
        const uint32_t preview = windows_monitor_word(record + monitor_desktop_preview);
        const uint32_t world = windows_monitor_word(record + monitor_desktop_world);
        if (preview)
            DisposHandle((Handle)SYN68K_TO_US(preview));
        if (picture)
            DisposHandle((Handle)SYN68K_TO_US(picture));
        if (world && !cfp_private(ref, 0x1b220, &world, 1, NULL))
            return 0;
    }
    DisposHandle((Handle)SYN68K_TO_US(list.records));
    DisposHandle((Handle)SYN68K_TO_US(handle));
    return 1;
}

static int windows_copy_monitor_list(const ROMlib_carbon_reference* ref, uint32_t offset, ROMlib_carbon_ppc_frame* call)
{
    const bool replace = offset == windows_set_monitor_list;
    const bool current = offset == windows_get_monitor_list;
    const uint32_t input = call->words[0], output = current ? input : call->words[1];
    if (!replace && (!output || !ROMlib_carbon_try_guest_memory(output, sizeof(uint32_t), 1)))
        return window_result(call, paramErr);
    const uint32_t slot = US_TO_SYN68K(ref->toc) + monitor_desktop_list_slot;
    uint32_t live;
    if (!cfp_get(slot, &live))
        return 0;
    const uint32_t source = current ? live : input;
    monitor_desktop_list validated;
    if (((!current || source) && !windows_monitor_list(source, &validated)) ||
        (replace && live && !windows_monitor_list(live, &validated)))
        return window_result(call, paramErr);

    /* The saved display worker matches devices by ID and duplicates their
       pictures. Hold the system zone through the copy and publish it only
       after the worker has finished rebuilding the list. */
    THz saved_zone = GetZone();
    SetZone(SystemZone());
    _Alignas(8) uint32_t result = 0;
    const uint32_t args[] = {source, US_TO_SYN68K(&result), 1};
    uint32_t status;
    int ok = cfp_private(ref, 0x152e8, args, NELEM(args), &status);
    if (ok && !status) {
        const uint32_t copy = CL(result);
        if (replace) {
            ok = cfp_write(slot, copy);
            if (ok && live)
                ok = windows_dispose_monitor_records(ref, live);
            if (ok)
                ok = cfp_private(ref, 0x14fd4, NULL, 0, NULL);
        }
        else
            ok = cfp_write(output, copy);
    }
    SetZone(saved_zone);
    return ok && window_result(call, status);
}

static int windows_monitor_record_access(const ROMlib_carbon_reference* ref, uint32_t offset,
                                         ROMlib_carbon_ppc_frame* call)
{
    const uint32_t handle = call->words[0], index = call->words[1], argument = call->words[2];
    uint32_t resolved = handle;
    if (!windows_resolve_monitor_list(ref, &resolved))
        return 0;
    monitor_desktop_list list = {0};
    const bool count_uninitialized = offset == windows_count_monitor_records && handle == UINT32_MAX && !resolved;
    if (!count_uninitialized && !windows_monitor_list(resolved, &list))
        return window_result(call, paramErr);
    if (offset == windows_count_monitor_records) {
        if (!index || !ROMlib_carbon_try_guest_memory(index, sizeof(uint32_t), 1))
            return window_result(call, paramErr);
        return cfp_write(index, list.count) && window_result(call, noErr);
    }
    const bool set_record = offset == windows_set_monitor_record;
    const bool set_alignment = offset == windows_set_monitor_alignment;
    Byte* record = windows_monitor_record(&list, index, set_record || set_alignment);
    if (!record)
        return window_result(call, paramErr);
    if (set_alignment) {
        if (!argument || argument > monitor_desktop_tile)
            return window_result(call, paramErr);
        windows_update_monitor_alignment(record, argument);
    }
    else if (offset == windows_get_monitor_alignment) {
        if (!argument || !ROMlib_carbon_try_guest_memory(argument, sizeof(uint32_t), 1))
            return window_result(call, paramErr);
        if (!cfp_write(argument, windows_monitor_alignment(record)))
            return 0;
    }
    else if (offset == windows_monitor_has_picture) {
        Byte* output = argument ? ROMlib_carbon_try_guest_memory(argument, 1, 1) : NULL;
        if (!output)
            return window_result(call, paramErr);
        *output = windows_monitor_word(record + monitor_desktop_picture) != 0;
    }
    else {
        void* bytes =
            argument ? ROMlib_carbon_try_guest_memory(argument, monitor_desktop_record_size, !set_record) : NULL;
        if (!bytes)
            return window_result(call, paramErr);
        if (set_record)
            memmove(record, bytes, monitor_desktop_record_size);
        else
            memmove(bytes, record, monitor_desktop_record_size);
    }
    return window_result(call, noErr);
}

static int windows_set_monitor_picture(const ROMlib_carbon_reference* ref, const monitor_desktop_list* list,
                                       uint32_t index, uint32_t file, uint32_t alignment, ROMlib_carbon_ppc_frame* call)
{
    const Byte* previous = windows_monitor_record(list, index, false);
    if (!previous || !alignment || alignment > monitor_desktop_tile ||
        (file && !ROMlib_carbon_try_guest_memory(file, sizeof(FSSpec), 1)))
        return window_result(call, paramErr);
    _Alignas(8) Byte updated[monitor_desktop_record_size];
    memcpy(updated, previous, sizeof updated);
    const uint32_t args[] = {US_TO_SYN68K(updated), file};
    uint32_t status;
    if (!cfp_private(ref, 0x16100, args, NELEM(args), &status))
        return 0;
    if (!status) {
        windows_update_monitor_alignment(updated, alignment);
        Byte* destination = windows_monitor_record(list, index, true);
        if (!destination)
            return 0;
        memcpy(destination, updated, sizeof updated);
    }
    return window_result(call, status);
}

static int windows_copy_monitor_picture(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    uint32_t source_handle = call->words[0], destination_handle = call->words[2];
    if (!windows_resolve_monitor_list(ref, &source_handle) || !windows_resolve_monitor_list(ref, &destination_handle))
        return 0;
    monitor_desktop_list source_list, destination_list;
    if (!windows_monitor_list(source_handle, &source_list) ||
        !windows_monitor_list(destination_handle, &destination_list))
        return window_result(call, paramErr);
    const Byte* source = windows_monitor_record(&source_list, call->words[1], false);
    const Byte* destination = windows_monitor_record(&destination_list, call->words[3], false);
    if (!source || !destination || source == destination)
        return window_result(call, paramErr);

    _Alignas(8) Byte updated[monitor_desktop_record_size], snapshot[monitor_desktop_record_size];
    memcpy(updated, destination, sizeof updated);
    memcpy(snapshot, source, sizeof snapshot);
    const uint32_t old_alias = windows_monitor_word(updated + monitor_desktop_picture);
    const uint32_t old_preview = windows_monitor_word(updated + monitor_desktop_preview);
    windows_set_monitor_word(updated + monitor_desktop_picture, 0);
    windows_set_monitor_word(updated + monitor_desktop_preview, 0);
    const uint32_t alias = windows_monitor_word(snapshot + monitor_desktop_picture);
    const uint32_t preview = alias ? windows_monitor_word(snapshot + monitor_desktop_preview) : 0;
    Handle new_alias = alias ? (Handle)SYN68K_TO_US(alias) : NULL;
    Handle new_preview = preview ? (Handle)SYN68K_TO_US(preview) : NULL;

    THz zone = GetZone();
    SetZone(SystemZone());
    const OSErr alias_error = new_alias ? HandToHand(&new_alias) : noErr;
    const OSErr preview_error = !alias_error && new_preview ? HandToHand(&new_preview) : noErr;
    SetZone(zone);
    if (alias_error || preview_error) {
        if (!alias_error && new_alias)
            DisposHandle(new_alias);
        return window_result(call, alias_error ? alias_error : preview_error);
    }

    windows_set_monitor_word(updated + monitor_desktop_picture, new_alias ? US_TO_SYN68K(new_alias) : 0);
    windows_set_monitor_word(updated + monitor_desktop_preview, new_preview ? US_TO_SYN68K(new_preview) : 0);
    if (alias) {
        memcpy(updated + 4, snapshot + 4, 4);
        memcpy(updated + monitor_desktop_alignment, snapshot + monitor_desktop_alignment, 8);
        memcpy(updated + 0x24, snapshot + 0x24, 12);
        memcpy(updated + 0x30, snapshot + 0x30, 0x140);
    }
    else {
        windows_set_monitor_word(updated + 4, 0);
        memset(updated + 0x24, 0, 12);
        updated[0x30] = updated[0x70] = 0;
    }
    windows_set_monitor_word(updated + 0x170, 0);
    const uint32_t flags = windows_monitor_word(updated + monitor_desktop_cache_flags);
    windows_set_monitor_word(updated + monitor_desktop_cache_flags, flags & ~1u);
    Byte* target = windows_monitor_record(&destination_list, call->words[3], true);
    if (!target) {
        if (new_alias)
            DisposHandle(new_alias);
        if (new_preview)
            DisposHandle(new_preview);
        return 0;
    }
    memcpy(target, updated, sizeof updated);
    if (old_alias)
        DisposHandle((Handle)SYN68K_TO_US(old_alias));
    if (old_preview)
        DisposHandle((Handle)SYN68K_TO_US(old_preview));
    return window_result(call, noErr);
}

static int windows_persist_monitor_list(const ROMlib_carbon_reference* ref)
{
    THz saved_zone = GetZone();
    SetZone(SystemZone());
    const int ok = cfp_private(ref, 0x14fd4, NULL, 0, NULL);
    SetZone(saved_zone);
    return ok;
}

static int windows_device_picture(const ROMlib_carbon_reference* ref, bool set, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t device = call->words[0], file = call->words[1], alignment = call->words[2];
    FSSpec* output = !set && file ? ROMlib_carbon_try_guest_memory(file, sizeof *output, 1) : NULL;
    if (!device || (!set && (!output || (alignment && !ROMlib_carbon_try_guest_memory(alignment, 4, 1)))))
        return window_result(call, paramErr);
    uint32_t handle = UINT32_MAX;
    if (!windows_resolve_monitor_list(ref, &handle))
        return 0;
    monitor_desktop_list list;
    if (!windows_monitor_list(handle, &list))
        return window_result(call, paramErr);
    for (uint32_t index = 1; index <= list.count; ++index) {
        const Byte* record = windows_monitor_record(&list, index, false);
        if (!record)
            return 0;
        if (windows_monitor_word(record + monitor_desktop_device) != device)
            continue;
        if (set)
            return windows_set_monitor_picture(ref, &list, index, file, alignment, call) &&
                   (call->words[0] || windows_persist_monitor_list(ref));
        const uint32_t alias = windows_monitor_word(record + monitor_desktop_picture);
        if (!alias)
            return window_result(call, -5605);
        const uint32_t mode = windows_monitor_alignment(record);
        _Alignas(8) FSSpec match = {0};
        _Alignas(8) INTEGER matches = CWC(1);
        _Alignas(8) Byte changed = 0;
        const uint32_t args[] = {0, 0x10b, alias, US_TO_SYN68K(&matches), US_TO_SYN68K(&match), US_TO_SYN68K(&changed),
                                 0, 0};
        uint32_t status;
        if (!cfp_private(ref, 0x1b328, args, NELEM(args), &status))
            return 0;
        if (!(int16_t)status) {
            memcpy(output, &match, sizeof match);
            if (alignment && !cfp_write(alignment, mode))
                return 0;
        }
        return window_result(call, (int16_t)status);
    }
    return window_result(call, paramErr);
}

enum { desktop_picture_type = 0x50494354, desktop_clipping_type = 0x636c7070, desktop_drop_folder_type = 0x66647270 };

static const FSSpec* windows_judged_picture_spec(uint32_t address)
{
    const FSSpec* spec = address ? ROMlib_carbon_try_guest_memory(address, sizeof *spec, 0) : NULL;
    return spec && spec->name[0] < sizeof spec->name ? spec : NULL;
}

static int windows_file_may_be_picture(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const FSSpec* spec = windows_judged_picture_spec(call->words[0]);
    const uint32_t output = call->words[1];
    Byte* possible = output ? ROMlib_carbon_try_guest_memory(output, 1, 1) : NULL;
    if (!spec || !possible)
        return window_result(call, paramErr);
    *possible = 0;
    _Alignas(8) Str255 name;
    memcpy(name, spec->name, (unsigned)spec->name[0] + 1);
    _Alignas(8) CInfoPBRec query = {0};
    PP_RAW(query.hFileInfo.ioNamePtr) = CL(US_TO_SYN68K(name));
    query.hFileInfo.ioVRefNum = spec->vRefNum;
    query.hFileInfo.ioDirID = spec->parID;
    const uint32_t address = US_TO_SYN68K(&query);
    uint32_t status;
    if (!cfp_private(ref, 0x1b370, &address, 1, &status))
        return 0;
    if ((int16_t)status)
        return window_result(call, (int16_t)status);
    const uint32_t type = CL(query.hFileInfo.ioFlFndrInfo.fdType);
    if ((query.hFileInfo.ioFlAttrib & 0x10) || type == desktop_picture_type || type == desktop_clipping_type ||
        type == desktop_drop_folder_type)
        *possible = 1;
    else {
        uint32_t matched;
        const uint32_t check[] = {type, US_TO_SYN68K(name)};
        if (!cfp_private(ref, 0x1716c, check, NELEM(check), &matched))
            return 0;
        *possible = (uint8_t)matched != 0;
    }
    return window_result(call, noErr);
}

static int windows_read_picture_data_fork(const ROMlib_carbon_reference* ref, uint32_t file, uint32_t output,
                                          ROMlib_carbon_ppc_frame* call)
{
    enum { pict_file_prefix_size = 512, pict_header_size = 10 };

    _Alignas(8) INTEGER encoded_file_ref = 0;
    const uint32_t open[] = {file, 1, US_TO_SYN68K(&encoded_file_ref)};
    uint32_t status;
    if (!cfp_private(ref, 0x1b3e8, open, NELEM(open), &status))
        return 0;
    if ((int16_t)status)
        return window_result(call, (int16_t)status);
    const uint32_t file_ref = (int16_t)CW(encoded_file_ref);
    _Alignas(8) uint32_t end = 0, length = 0;
    const uint32_t query[] = {file_ref, US_TO_SYN68K(&end)};
    int ok = cfp_private(ref, 0x1b400, query, NELEM(query), &status);
    Handle picture = NULL;
    if (ok && !(int16_t)status) {
        const int32_t extent = CL(end);
        if (extent < pict_file_prefix_size + pict_header_size)
            status = eofErr;
        else {
            length = CL(extent - pict_file_prefix_size);
            const uint32_t position[] = {file_ref, 3, pict_file_prefix_size};
            ok = cfp_private(ref, 0x1b418, position, NELEM(position), &status);
            if (ok && !(int16_t)status) {
                picture = NewHandleSys(CL(length));
                if (!picture)
                    status = memFullErr;
            }
        }
    }
    if (picture) {
        HLock(picture);
        const uint32_t read[] = {file_ref, US_TO_SYN68K(&length), US_TO_SYN68K(STARH(picture))};
        ok = cfp_private(ref, 0x1b490, read, NELEM(read), &status);
        HUnlock(picture);
    }
    if (ok)
        ok = cfp_private(ref, 0x1b4a8, &file_ref, 1, NULL);
    if (ok && !(int16_t)status && picture && cfp_write(output, US_TO_SYN68K(picture)))
        return window_result(call, noErr);
    if (picture)
        DisposHandle(picture);
    return ok && (int16_t)status && window_result(call, (int16_t)status);
}

static int windows_picture_from_file(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t file = call->words[0], output = call->words[1];
    if (!windows_judged_picture_spec(file) || !output || !ROMlib_carbon_try_guest_memory(output, 4, 1))
        return window_result(call, paramErr);
    if (!cfp_write(output, 0))
        return 0;
    _Alignas(8) FInfo info;
    const uint32_t query[] = {file, US_TO_SYN68K(&info)};
    uint32_t status;
    if (!cfp_private(ref, 0x1b190, query, NELEM(query), &status))
        return 0;
    if ((int16_t)status)
        return window_result(call, (int16_t)status);
    const uint32_t type = CL(info.fdType);
    if (type == desktop_picture_type)
        return windows_read_picture_data_fork(ref, file, output, call);
    const uint32_t arguments[] = {file, output};
    return cfp_private(ref, type == desktop_clipping_type ? 0x16f74 : 0x1733c, arguments, NELEM(arguments), &status) &&
           window_result(call, status);
}

static int windows_monitor_draw_duration(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    uint32_t handle = call->words[0];
    const uint32_t index = call->words[1], target_port = call->words[2], bounds_address = call->words[3];
    const uint32_t output = call->words[4];
    const Rect* bounds = bounds_address ? ROMlib_carbon_try_guest_memory(bounds_address, sizeof *bounds, 0) : NULL;
    Byte* slow = output ? ROMlib_carbon_try_guest_memory(output, 1, 1) : NULL;
    if (!handle || !target_port || !bounds || !slow)
        return window_result(call, paramErr);
    if (!windows_resolve_monitor_list(ref, &handle))
        return 0;
    monitor_desktop_list list;
    const Byte* record = windows_monitor_list(handle, &list) ? windows_monitor_record(&list, index, false) : NULL;
    if (!record)
        return window_result(call, paramErr);
    if (!windows_monitor_word(record + monitor_desktop_picture)) {
        *slow = 0;
        return window_result(call, noErr);
    }
    const uint32_t format = windows_monitor_word(record + 0x2c);
    const uint32_t basis = windows_monitor_word(record + 0x0c);
    const uint32_t preview = windows_monitor_word(record + monitor_desktop_preview);
    const uint32_t world = windows_monitor_word(record + monitor_desktop_world);
    const uint32_t cache_flags = windows_monitor_word(record + monitor_desktop_cache_flags);
    const bool classic_picture = format == desktop_picture_type || format == desktop_clipping_type;
    if (!preview || !basis || (classic_picture ? basis == 1 : !(cache_flags & 1) || !world)) {
        *slow = 1;
        return window_result(call, noErr);
    }
    if (classic_picture) {
        *slow = 0;
        return window_result(call, noErr);
    }
    const Rect* cached_bounds = ROMlib_carbon_try_guest_memory(world + 0x10, sizeof *cached_bounds, 0);
    if (!cached_bounds)
        return window_result(call, paramErr);
    const Rect cached = *cached_bounds;
    _Alignas(8) Rect target = *bounds;
    if (basis != 1) {
        _Alignas(8) Rect display = {0}, original;
        memcpy(&original, record + 0x24, sizeof original);
        const uint32_t monitor[] = {windows_monitor_word(record + monitor_desktop_device), 0, 0,
                                    US_TO_SYN68K(&display)};
        const uint32_t calculate[] = {bounds_address,
                                      US_TO_SYN68K(&display),
                                      US_TO_SYN68K(&original),
                                      basis,
                                      windows_monitor_word(record + monitor_desktop_flags),
                                      US_TO_SYN68K(&target)};
        if (!cfp_private(ref, 0x18848, monitor, NELEM(monitor), NULL) ||
            !cfp_private(ref, 0x175d4, calculate, NELEM(calculate), NULL))
            return 0;
    }
    *slow = memcmp(&target, &cached, sizeof target) != 0;
    return window_result(call, noErr);
}

static int windows_monitor_lists(const ROMlib_carbon_reference* ref, uint32_t offset, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t argument = call->words[0];
    switch (offset) {
    case windows_get_monitor_list:
    case windows_set_monitor_list:
    case windows_refresh_monitor_list:
        return windows_copy_monitor_list(ref, offset, call);
    case windows_dispose_monitor_list: {
        monitor_desktop_list list;
        if (!windows_monitor_list(argument, &list))
            return window_result(call, paramErr);
        return windows_dispose_monitor_records(ref, argument) && window_result(call, noErr);
    }
    case windows_get_internal_monitor_list:
        if (!argument || !ROMlib_carbon_try_guest_memory(argument, sizeof(uint32_t), 1))
            return window_result(call, paramErr);
        /* The original API exposes a tag for the current list, not its handle. */
        return cfp_write(argument, UINT32_MAX) && window_result(call, noErr);
    case windows_monitor_list_changed:
        return windows_persist_monitor_list(ref);
    case windows_set_monitor_file: {
        uint32_t handle = argument;
        if (!windows_resolve_monitor_list(ref, &handle))
            return 0;
        monitor_desktop_list list;
        if (!windows_monitor_list(handle, &list))
            return window_result(call, paramErr);
        return windows_set_monitor_picture(ref, &list, call->words[1], call->words[2], call->words[3], call);
    }
    case windows_copy_monitor_file:
        return windows_copy_monitor_picture(ref, call);
    case windows_set_device_picture:
    case windows_get_device_picture:
        return windows_device_picture(ref, offset == windows_set_device_picture, call);
    case windows_file_may_contain_picture:
        return windows_file_may_be_picture(ref, call);
    case windows_get_picture_from_file:
        return windows_picture_from_file(ref, call);
    case windows_monitor_draw_takes_time:
        return windows_monitor_draw_duration(ref, call);
    default:
        return windows_monitor_record_access(ref, offset, call);
    }
}

static int windows_reposition(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0], parent = call->words[1], method = call->words[2];
    if (!method || method > 9 || window == parent)
        return window_result(call, paramErr);
    const uint32_t table = US_TO_SYN68K(ref->toc) + 0x131c;
    const Byte* placement = ROMlib_carbon_guest_memory(table + method * 4, 4, 0);
    if (!placement)
        return 0;
    if (placement[3] && !parent)
        return window_result(call, paramErr);
    uint32_t record;
    if (!windows_metadata_record(ref, window, &record))
        return 0;
    if (!record)
        return window_result(call, window_invalid);
    const uint32_t position[] = {record, parent, placement[0], placement[1], placement[2]};
    return cfp_private(ref, 0xea20, position, NELEM(position), NULL) && window_result(call, noErr);
}

static int windows_recalculate_title(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t record = call->words[1];
    uint32_t window;
    if (!cfp_get(record, &window))
        return 0;
    const Byte* tag = ROMlib_carbon_guest_memory(window + 0x4a, 2, 0);
    if (!tag)
        return 0;
    if (tag[0] != 0xde || tag[1] != 0xad) {
        ROMlib_carbon_reference ref;
        const uint32_t args[] = {window, 13, 1, 1};
        if (!ROMlib_carbon_probe_reference("WindowsLib", "RecalculateAllWindowTitleWidths", &ref) ||
            !cfp_private(&ref, 0xabe0, &record, 1, NULL) || !cfp_private(&ref, 0x61ac, args, NELEM(args), NULL))
            return 0;
    }
    return window_result(call, 0);
}

static int windows_recalculate_titles(const ROMlib_carbon_reference* ref)
{
    uint32_t painter;
    const uint32_t table = cfp_toc(ref, 0x241c);
    if (!table ||
        !ROMlib_carbon_native_ppc_callback("RecalculateWindowTitleWidth", windows_recalculate_title, &painter))
        return 0;
    const uint32_t args[] = {table, painter, 0};
    return cfp_private(ref, 0x1c588, args, NELEM(args), NULL);
}

static int windows_rebuild_layer_regions(const ROMlib_carbon_reference* ref)
{
    const uint32_t root = cfp_toc(ref, 0x242c);
    if (!root)
        return 1;
    uint32_t window, structure;
    if (!cfp_get(root + 0x72, &structure) || !cfp_get(root + 0x94, &window) ||
        !cfp_private(ref, 0x1a9e0, &structure, 1, NULL))
        return 0;
    for (unsigned visited = 0; window && visited < UINT16_MAX; ++visited) {
        uint32_t child_structure;
        if (!cfp_get(window + 0x72, &child_structure))
            return 0;
        const uint32_t add[] = {child_structure, structure, structure};
        if (!cfp_private(ref, 0x1ac20, add, NELEM(add), NULL) || !cfp_get(window + 0x90, &window))
            return 0;
    }
    return !window;
}

static int windows_reshape_all(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    if (!windows_rebuild_saved_desktop(ref, call))
        return 0;
    const uint32_t menu[] = {US_TO_SYN68K(MR(GrayRgn)), 0};
    uint32_t affected, updated;
    if (!cfp_private(ref, 0x1cbb8, menu, NELEM(menu), NULL) || !cfp_private(ref, 0x136f8, NULL, 0, &affected))
        return 0;
    if (!affected)
        return 1;
    if (!cfp_private(ref, 0x136f8, NULL, 0, &updated))
        return 0;
    if (!updated)
        return cfp_private(ref, 0x137bc, &affected, 1, NULL);

    _Alignas(8) uint32_t data[] = {0, CL(updated), CL(affected), 0};
    const uint32_t visit[] = {UINT32_MAX, UINT32_MAX, cfp_toc(ref, 0x2430), 2, cfp_toc(ref, 0x704), US_TO_SYN68K(data)};
    const uint32_t unite[] = {updated, affected, affected};
    const uint32_t reclip[] = {0, affected};
    const int ok = cfp_private(ref, 0x14a34, visit, NELEM(visit), NULL) &&
                   cfp_private(ref, 0x1ac20, unite, NELEM(unite), NULL) &&
                   cfp_private(ref, 0x1bdc0, reclip, NELEM(reclip), NULL) && windows_rebuild_layer_regions(ref);
    return ok && cfp_private(ref, 0x137bc, &updated, 1, NULL) && cfp_private(ref, 0x137bc, &affected, 1, NULL);
}

static int windows_path_select_click(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0], address = call->words[1];
    const EventRecord* event = address ? ROMlib_carbon_try_guest_memory(address, sizeof *event, 0) : NULL;
    if (!window || !event || CW(event->what) != mouseDown || !(CW(event->modifiers) & cmdKey))
        return window_result(call, 0);
    Point location = event->where;
    uint32_t region, status, hit = 0;
    if (!cfp_private(ref, 0x136f8, NULL, 0, &region))
        return 0;
    if (!region)
        return window_result(call, 0);
    const int ok = windows_read_window_region(ref, window, 1, region, &status);
    if (ok && !status)
        hit = PtInRgn(location, (RgnHandle)SYN68K_TO_US(region));
    const int released = cfp_private(ref, 0x137bc, &region, 1, NULL);
    return ok && released && window_result(call, hit != 0);
}

static int windows_visual_class(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call);

static int windows_collapse_visible(const ROMlib_carbon_reference* ref, uint32_t window, Byte* flags, uint32_t region,
                                    uint32_t sound, int32_t* status)
{
    const uint32_t calculate[] = {window, 2, 0, 1};
    const uint32_t desktop[] = {US_TO_SYN68K(MR(GrayRgn)), region};
    const uint32_t inset[] = {region, 4, 4};
    uint32_t structure, empty;
    *flags |= 1;
    int ok = cfp_private(ref, 0x61ac, calculate, NELEM(calculate), NULL) && cfp_get(window + 0x72, &structure) &&
             cfp_private(ref, 0x1bc28, desktop, NELEM(desktop), NULL) &&
             cfp_private(ref, 0x1c300, inset, NELEM(inset), NULL);
    const uint32_t clip[] = {region, ok ? structure : 0, region};
    if (ok)
        ok = cfp_private(ref, 0x1b310, clip, NELEM(clip), NULL);
    *flags &= ~1u;
    if (!ok || !cfp_private(ref, 0x61ac, calculate, NELEM(calculate), NULL) ||
        !cfp_private(ref, 0x127f4, &window, 1, NULL) || !cfp_private(ref, 0x1b280, &region, 1, &empty))
        return 0;
    /* Reject a collapse whose title frame would lie entirely off-screen. */
    if ((uint8_t)empty) {
        *status = -666;
        return 1;
    }
    if (!cfp_private(ref, 0x1c2b8, &sound, 1, NULL) || !cfp_private(ref, 0x1c1e0, &window, 1, NULL))
        return 0;
    *flags |= 1;
    uint32_t content;
    if (!cfp_private(ref, 0x61ac, calculate, NELEM(calculate), NULL) || !cfp_get(window + 0x76, &content) ||
        !cfp_get(window + 0x72, &structure))
        return 0;
    const uint32_t copy[] = {content, region};
    const uint32_t repaint[] = {window, structure};
    const uint32_t reveal[] = {window, 1};
    return cfp_private(ref, 0x1bc28, copy, NELEM(copy), NULL) &&
           cfp_private(ref, 0x1b940, repaint, NELEM(repaint), NULL) &&
           cfp_private(ref, 0x1c1f8, reveal, NELEM(reveal), NULL);
}

static int windows_toggle_collapse(const ROMlib_carbon_reference* ref, uint32_t window, bool collapse, int32_t* status)
{
    uint32_t record, features;
    if (!windows_metadata_record(ref, window, &record))
        return 0;
    if (!record) {
        *status = window_invalid;
        return 1;
    }
    if (!windows_read_features(ref, window, record, &features, status))
        return 0;
    if (*status || !(features & 4)) {
        *status = -30580;
        return 1;
    }
    *status = noErr;
    Byte* flags = ROMlib_carbon_guest_memory(record + 0x60, 1, 1);
    const Byte* visible = ROMlib_carbon_guest_memory(window + 0x6e, 1, 0);
    if (!flags || !visible)
        return 0;
    if (collapse == ((*flags & 1) != 0))
        return 1;
    if (!*visible) {
        *flags = (*flags & ~1u) | collapse;
        return 1;
    }
    _Alignas(8) uint32_t visual_class = CLC(6);
    ROMlib_carbon_ppc_frame visual = {.words = {window, US_TO_SYN68K(&visual_class)}};
    if (!windows_visual_class(ref, &visual))
        return 0;
    const bool utility = !visual.words[0] && CL(visual_class) == 5;
    if (!collapse) {
        *flags &= ~1u;
        const Rect* bounds = ROMlib_carbon_guest_memory(window + 0x10, sizeof *bounds, 0);
        if (!bounds)
            return 0;
        const uint32_t sound = utility ? 0x75657870 : 0x77657870;
        const uint32_t size[] = {window, RECT_WIDTH(bounds), RECT_HEIGHT(bounds), 1};
        return cfp_private(ref, 0x1c2b8, &sound, 1, NULL) && cfp_private(ref, 0x1c3f0, size, NELEM(size), NULL);
    }
    uint32_t region;
    if (!cfp_private(ref, 0x136f8, NULL, 0, &region))
        return 0;
    if (!region) {
        *status = memFullErr;
        return 1;
    }
    const uint32_t sound = utility ? 0x75636f6c : 0x77636f6c;
    const int ok = windows_collapse_visible(ref, window, flags, region, sound, status);
    return cfp_private(ref, 0x137bc, &region, 1, NULL) && ok;
}

static int windows_collapse_group(const ROMlib_carbon_reference* ref, uint32_t offset, ROMlib_carbon_ppc_frame* call)
{
    int32_t status;
    if (offset == windows_collapse_window)
        return windows_toggle_collapse(ref, call->words[0], (uint8_t)call->words[1] != 0, &status) &&
               window_result(call, status);
    uint32_t window;
    const bool collapse = (uint8_t)call->words[0] != 0;
    if (!cfp_private(ref, 0x1c498, NULL, 0, &window))
        return 0;
    for (unsigned visited = 0; window && visited < UINT16_MAX; ++visited) {
        const Byte* visible = ROMlib_carbon_guest_memory(window + 0x6e, 1, 0);
        if (!visible || (*visible && !windows_toggle_collapse(ref, window, collapse, &status)) ||
            !cfp_get(window + 0x90, &window))
            return 0;
    }
    return !window && window_result(call, noErr);
}

enum {
    window_background_pattern = 0x77707074,
    window_background_color = 0x77726762,
    window_background_brush = 0x7774686d
};

static int windows_stored_pattern(const ROMlib_carbon_reference* ref, uint32_t record, PixPatHandle* pattern,
                                  uint32_t* status)
{
    _Alignas(8) uint32_t encoded = 0;
    const uint32_t get[] = {record, window_system_creator, window_background_pattern, 4, 0, US_TO_SYN68K(&encoded)};
    if (!cfp_private(ref, 0x25c8, get, NELEM(get), status))
        return 0;
    *pattern = !*status && encoded ? (PixPatHandle)SYN68K_TO_US(CL(encoded)) : NULL;
    return 1;
}

static int windows_content_pattern(const ROMlib_carbon_reference* ref, uint32_t offset, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0], argument = call->words[1];
    uint32_t record, extent;
    if (!windows_metadata_record(ref, window, &record))
        return 0;
    if (!record)
        return window_result(call, window_invalid);
    const bool set = offset == windows_set_pattern;
    if (!cfp_handle_data(argument, &extent, !set) || extent < sizeof(PixPat))
        return window_result(call, paramErr);
    const PixPatHandle pattern = (PixPatHandle)SYN68K_TO_US(argument);

    PixPatHandle previous;
    uint32_t status;
    if (!windows_stored_pattern(ref, record, &previous, &status))
        return 0;
    if (status && (!set || (int32_t)status != window_missing_property))
        return window_result(call, status);
    if (!set) {
        if (!previous)
            return window_result(call, window_missing_property);
        CopyPixPat(previous, pattern);
        return window_result(call, noErr);
    }
    const uint32_t paint_proc = cfp_toc(ref, 0x640);
    if (!paint_proc)
        return 0;
    THz zone = GetZone();
    SetZone(SystemZone());
    const PixPatHandle replacement = NewPixPat();
    const OSErr allocation_error = QDError();
    SetZone(zone);
    if (!replacement || allocation_error) {
        if (replacement)
            DisposPixPat(replacement);
        return window_result(call, allocation_error ? allocation_error : memFullErr);
    }
    CopyPixPat(pattern, replacement);
    const OSErr copy_error = QDError();
    if (copy_error) {
        DisposPixPat(replacement);
        return window_result(call, copy_error);
    }
    _Alignas(8) const uint32_t encoded = CL(US_TO_SYN68K(replacement));
    const uint32_t install[] = {record, window_system_creator, window_background_pattern, 4, US_TO_SYN68K(&encoded)};
    const int installed = cfp_private(ref, 0x2510, install, NELEM(install), &status);
    if (!installed || status) {
        DisposPixPat(replacement);
        return installed && window_result(call, status);
    }
    if (previous)
        DisposPixPat(previous);
    const uint32_t remove_color[] = {record, window_system_creator, window_background_color};
    const uint32_t remove_brush[] = {record, window_system_creator, window_background_brush};
    return cfp_write(record + 0x40, paint_proc) && cfp_private(ref, 0x2678, remove_color, NELEM(remove_color), NULL) &&
           cfp_private(ref, 0x2678, remove_brush, NELEM(remove_brush), NULL) && window_result(call, noErr);
}

static int windows_theme_background(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0];
    uint32_t record, status;
    if (!windows_metadata_record(ref, window, &record))
        return 0;
    if (!record)
        return window_result(call, window_invalid);
    PixPatHandle previous;
    if (!windows_stored_pattern(ref, record, &previous, &status))
        return 0;
    if (status && (int32_t)status != window_missing_property)
        return window_result(call, status);

    const uint32_t paint_proc = cfp_toc(ref, 0x648);
    if (!paint_proc)
        return 0;
    const int16_t brush = (int16_t)call->words[1];
    _Alignas(8) const uint16_t encoded = CW((uint16_t)brush);
    const uint32_t install[] = {record, window_system_creator, window_background_brush, 2, US_TO_SYN68K(&encoded)};
    if (!cfp_private(ref, 0x2510, install, NELEM(install), &status))
        return 0;
    if (status)
        return window_result(call, status);
    if (previous) {
        const uint32_t remove_pattern[] = {record, window_system_creator, window_background_pattern};
        if (!cfp_private(ref, 0x2678, remove_pattern, NELEM(remove_pattern), &status))
            return 0;
        if (status)
            return window_result(call, status);
        DisposPixPat(previous);
    }
    const uint32_t remove_color[] = {record, window_system_creator, window_background_color};
    if (!cfp_private(ref, 0x2678, remove_color, NELEM(remove_color), NULL) || !cfp_write(record + 0x40, paint_proc))
        return 0;

    WindowPeek window_ptr = ROMlib_carbon_guest_memory(window, sizeof *window_ptr, 0);
    if (!window_ptr)
        return 0;
    uint32_t layer = 0;
    const bool redraw = (uint8_t)call->words[2] && WINDOW_VISIBLE_X(window_ptr);
    if (redraw && !cfp_get(record + 0x5c, &layer))
        return 0;
    const WindowRecord* parent = layer ? ROMlib_carbon_guest_memory(layer, sizeof *parent, 0) : NULL;
    if (layer && !parent)
        return 0;
    GrafPtr previous_port = thePort;
    SetPort((GrafPtr)window_ptr);
    const uint32_t set_brush[] = {(uint32_t)(int32_t)brush, 32, 1};
    int ok = cfp_private(ref, 0x1cb70, set_brush, NELEM(set_brush), NULL);
    if (ok && parent && WINDOW_VISIBLE_X(parent)) {
        const uint32_t invalidate[] = {window, window + 0x10};
        ok = cfp_private(ref, 0x12fbc, invalidate, NELEM(invalidate), NULL) &&
             cfp_private(ref, 0x1cb88, &invalidate[1], 1, NULL);
    }
    SetPort(previous_port);
    return ok && window_result(call, noErr);
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

static bool windows_legacy_title(uint32_t window, bool variant_only)
{
    const WindowPeek record = window ? ROMlib_carbon_guest_memory(window, sizeof(WindowRecord), 0) : NULL;
    if (!record || (variant_only && GetWVariant((WindowPtr)record) != 5))
        return false;
    uint32_t title;
    if (!cfp_get(window + 0x7e, &title) || !title)
        return false;
    uint32_t size;
    const Byte* data = cfp_handle_data(title, &size, 0);
    return data && size >= 10 && ((uint16_t)data[8] << 8 | data[9]) == (variant_only ? 0 : 1);
}

static int windows_title_double_click_hit(const ROMlib_carbon_reference* ref, uint32_t window, uint32_t features,
                                          Point point, bool* hit)
{
    *hit = false;
    if (features & 0x10) {
        RgnHandle region = NewRgn();
        if (region) {
            uint32_t status;
            const uint32_t query[] = {window, 0, US_TO_SYN68K(region)};
            const int ok = cfp_private(ref, 0x1ca80, query, NELEM(query), &status);
            if (ok && !status)
                *hit = PtInRgn(point, region);
            DisposeRgn(region);
            if (!ok || !status)
                return ok;
        }
    }

    uint32_t structure, content, structure_size, content_size;
    if (!cfp_get(window + 0x72, &structure) || !cfp_get(window + 0x76, &content))
        return 0;
    const Byte* structure_data = cfp_handle_data(structure, &structure_size, 0);
    const Byte* content_data = cfp_handle_data(content, &content_size, 0);
    if (!structure_data || structure_size < 10 || !content_data || content_size < 10)
        return 0;
    Point structure_origin;
    memcpy(&structure_origin, structure_data + 2, sizeof structure_origin);
    structure_origin.v = CW(structure_origin.v);
    structure_origin.h = CW(structure_origin.h);
    if (!PtInRgn(structure_origin, (RgnHandle)SYN68K_TO_US(structure)) && !windows_legacy_title(window, false))
        return 1;
    if (EmptyRgn((RgnHandle)SYN68K_TO_US(content))) {
        *hit = true;
        return 1;
    }
    Rect structure_bounds, content_bounds;
    memcpy(&structure_bounds, structure_data + 2, sizeof structure_bounds);
    memcpy(&content_bounds, content_data + 2, sizeof content_bounds);
    const int16_t top_gap = CW(content_bounds.top) - CW(structure_bounds.top);
    *hit = top_gap > 3 && top_gap >= CW(content_bounds.left) - CW(structure_bounds.left);
    return 1;
}

static int windows_handle_title_double_click(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    uint32_t action;
    if (!cfp_get(US_TO_SYN68K(ref->toc) + 0x768, &action))
        return 0;
    if (!action || action > 3)
        return window_result(call, false);
    const uint32_t window = call->words[0];
    const uint32_t event_address = call->words[1];
    EventRecord* event = event_address ? ROMlib_carbon_try_guest_memory(event_address, sizeof *event, 1) : NULL;
    if (!event || !window || !ROMlib_carbon_try_guest_memory(window, sizeof(WindowRecord), 0))
        return window_result(call, false);

    uint32_t front, status;
    _Alignas(8) uint32_t features = 0;
    const uint32_t output = US_TO_SYN68K(&features);
    if (!cfp_private(ref, 0x1c498, NULL, 0, &front))
        return 0;
    if (front) {
        const uint32_t query[] = {front, output};
        if (!cfp_private(ref, 0x1c2a0, query, NELEM(query), &status))
            return 0;
        if (!status && (CL(features) & 8))
            return window_result(call, false);
    }
    const uint32_t query[] = {window, output};
    if (!cfp_private(ref, 0x1c2a0, query, NELEM(query), &status))
        return 0;
    const uint32_t flags = status ? 0 : CL(features);
    if ((flags & 0x48) == 0x48 || windows_legacy_title(front, true) || windows_legacy_title(window, true))
        return window_result(call, false);

    bool hit;
    const Point point = {CW(event->where.v), CW(event->where.h)};
    if (!windows_title_double_click_hit(ref, window, flags, point, &hit))
        return 0;
    if (!hit)
        return window_result(call, false);
    const uint32_t arguments[] = {window, action == 1 ? event_address : action};
    uint32_t consumed = 1;
    if (action == 1) {
        if (!cfp_private(ref, 0x7854, arguments, NELEM(arguments), NULL))
            return 0;
    }
    else if (!cfp_private(ref, 0x7958, arguments, NELEM(arguments), &consumed))
        return 0;
    if ((uint8_t)consumed) {
        event->what = 0;
        event->where = (Point){0};
    }
    return window_result(call, (uint8_t)consumed);
}

static Rect transition_center(const Rect* bounds)
{
    const int16_t x = CW(bounds->left) + (CW(bounds->right) - CW(bounds->left)) / 2;
    const int16_t y = CW(bounds->top) + (CW(bounds->bottom) - CW(bounds->top)) / 2;
    return (Rect){CW(y), CW(x), CW(y), CW(x)};
}

int32_t ROMlib_carbon_transition_window(uint32_t window, uint32_t effect, uint32_t action, const void* origin)
{
    WindowPtr target = window ? ROMlib_carbon_try_guest_memory(window, sizeof(WindowRecord), 0) : NULL;
    if (!target || !ROMlib_valid_window(target))
        return window_invalid;
    if (effect != 1 || (action != 1 && action != 2) ||
        (origin && !ROMlib_carbon_try_guest_memory(US_TO_SYN68K(origin), sizeof(Rect), 0)))
        return paramErr;
    const bool show = action == 1;
    const ROMlib_carbon_export* zoom = ROMlib_carbon_find_export("ZoomRects", 2);
    if (!zoom || !(zoom->signature & ROMLIB_CARBON_PPC_FRAME))
        return -4;
    Rect* rectangles = (Rect*)NewPtrSysClear(2 * sizeof *rectangles);
    if (!rectangles)
        return memFullErr;
    if (!WINDOW_VISIBLE(target))
        THEPORT_SAVE_EXCURSION(MR(wmgr_port), { WINDCALL(target, wCalcRgns, 0); });
    Rect bounds = HxX(WINDOW_STRUCT_REGION(target), rgnBBox);
    Rect small = transition_center(&bounds);
    if (origin)
        memcpy(&small, origin, sizeof small);
    rectangles[0] = show ? small : bounds;
    rectangles[1] = show ? bounds : small;
    ROMlib_carbon_ppc_frame animation = {.words = {US_TO_SYN68K(rectangles), US_TO_SYN68K(rectangles + 1), 6, 0}};
    const int handled = ((ROMlib_carbon_frame_entry)zoom->function)(&animation);
    DisposPtr((Ptr)rectangles);
    if (!handled)
        return -4;
    if (!animation.words[0]) {
        if (show)
            ShowWindow(target);
        else
            HideWindow(target);
    }
    return animation.words[0];
}

static int windows_transition_with_reference(const ROMlib_carbon_reference* ref, uint32_t window, const Rect* origin,
                                             bool show, ROMlib_carbon_ppc_frame* call)
{
    uint32_t metadata;
    if (!cfp_private(ref, 0x2370, &window, 1, &metadata))
        return 0;
    if (!metadata)
        return window_result(call, window_invalid);
    uint32_t manager_address;
    if (!cfp_get(US_TO_SYN68K(ref->toc) + 0x2460, &manager_address))
        return 0;
    GrafPtr manager = manager_address ? ROMlib_carbon_guest_memory(manager_address, sizeof *manager, 0) : NULL;
    RgnHandle desktop = MR(GrayRgn);
    if (!manager || !desktop)
        return 0;

    uint32_t clip;
    if (!cfp_private(ref, 0x136f8, NULL, 0, &clip))
        return 0;
    if (!clip)
        return window_result(call, memFullErr);
    const GrafPtr previous = thePort;
    SetPort(manager);
    GetClip((RgnHandle)SYN68K_TO_US(clip));
    Rect desktop_bounds = HxX(desktop, rgnBBox);
    ClipRect(&desktop_bounds);
    _Alignas(8) Rect bounds = {0};
    const uint32_t structure[] = {window, 0x20, US_TO_SYN68K(&bounds)};
    _Alignas(8) uint32_t style = 0;
    ROMlib_carbon_ppc_frame visual = {.words = {window, US_TO_SYN68K(&style)}};
    int ok = cfp_private(ref, 0x1c108, &window, 1, NULL) &&
             cfp_private(ref, 0xed90, structure, NELEM(structure), NULL) && windows_visual_class(ref, &visual);
    if (ok && !show) {
        const uint32_t hide[] = {window, 0};
        uint32_t parent;
        ok = cfp_private(ref, 0x1bf10, hide, NELEM(hide), NULL) && cfp_get(metadata + 0x5c, &parent);
        if (ok) {
            const uint32_t recalc[] = {metadata, parent, 0};
            ok = cfp_private(ref, 0x9264, recalc, NELEM(recalc), NULL);
        }
        if (ok) {
            ClipRect(&desktop_bounds);
            ok = cfp_private(ref, 0x1c108, &window, 1, NULL);
        }
    }
    if (ok) {
        static const uint32_t sounds[2][4] = {{0x77636c73, 0x616c7463, 0x646c6763, 0x75636c73},
                                              {0x776f706e, 0x616f706e, 0x646f706e, 0x756f706e}};
        const uint32_t kind = visual.words[0] ? 6 : CL(style);
        const unsigned family = kind == 5 ? 3 : (kind >= 1 && kind <= 4 ? (kind + 1) / 2 : 0);
        _Alignas(8) Rect small = origin ? *origin : transition_center(&bounds);
        const uint32_t animation[] = {US_TO_SYN68K(&small), US_TO_SYN68K(&bounds), show};
        ok = cfp_private(ref, 0x1c2b8, &sounds[show][family], 1, NULL) &&
             cfp_private(ref, 0x1d34, animation, NELEM(animation), NULL);
    }
    SetClip((RgnHandle)SYN68K_TO_US(clip));
    SetPort(previous);
    const int released = cfp_private(ref, 0x137bc, &clip, 1, NULL);
    if (!ok || !released)
        return 0;
    return (!show || cfp_private(ref, 0x1bd90, &window, 1, NULL)) && window_result(call, noErr);
}

static int windows_transition_entry(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0], effect = call->words[1], action = call->words[2];
    if (effect != 1 || (action != 1 && action != 2))
        return window_result(call, paramErr);
    Rect origin;
    const uint32_t address = call->words[3];
    const void* source = address ? ROMlib_carbon_try_guest_memory(address, sizeof origin, 0) : NULL;
    if (address && !source)
        return window_result(call, paramErr);
    if (!ref)
        return window_result(call, ROMlib_carbon_transition_window(window, effect, action, source));
    if (source)
        memcpy(&origin, source, sizeof origin);
    return windows_transition_with_reference(ref, window, source ? &origin : NULL, action == 1, call);
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
    if (offset == windows_global_find) {
        HIDDEN_WindowPtr* output = argument ? ROMlib_carbon_try_guest_memory(argument, sizeof *output, 1) : NULL;
        const Point point = {.v = CW(address >> 16), .h = CW(address)};
        return window_result(call, output ? FindWindow(point, output) : 0);
    }
    if (offset == windows_get_region)
        return window_result(call, address && call->words[2] ? -30580 : paramErr);
    if (offset == windows_reposition_window || offset == windows_reshape_windows ||
        offset == windows_recalculate_title_widths)
        return window_result(call, -4);
    WindowPtr window = address ? ROMlib_carbon_try_guest_memory(address, sizeof(WindowRecord), 0) : NULL;
    const int valid = window && ROMlib_valid_window(window);
    if (offset == windows_collapse_window)
        return window_result(call, valid ? -4 : window_invalid);
    if (offset == windows_is_collapsed)
        return window_result(call, 0);
    if (offset == windows_get_features) {
        if (!argument)
            return window_result(call, paramErr);
        if (!cfp_write(argument, 0))
            return 0;
        if (valid)
            ROMlib_windcall(window, 7, argument);
        return window_result(call, valid ? noErr : window_invalid);
    }
    if (offset == windows_is_collapsable || offset == windows_collapse_all_windows) {
        /* The classic Window Manager has no WindowShade state. A request
           for all windows is already satisfied when none can collapse. */
        if (offset == windows_collapse_all_windows) {
            for (WindowPtr item = MR(WindowList); item; item = WINDOW_NEXT_WINDOW(item)) {
                _Alignas(8) uint32_t features = 0;
                if (WINDOW_VISIBLE(item))
                    ROMlib_windcall(item, 7, US_TO_SYN68K(&features));
                if (CL(features) & 4)
                    return window_result(call, -4);
            }
            return window_result(call, noErr);
        }
        _Alignas(8) uint32_t features = 0;
        if (valid)
            ROMlib_windcall(window, 7, US_TO_SYN68K(&features));
        return window_result(call, (CL(features) & 4) != 0);
    }
    if (offset == windows_install_paint_proc) {
        if (!valid)
            return window_result(call, window_invalid);
        return window_result(call, call->words[2] ? paramErr : argument ? proxy_unavailable : noErr);
    }
    if (offset == windows_erase_region) {
        uint32_t extent;
        if (!address || !cfp_handle_data(argument, &extent, 1) || extent < 10)
            return window_result(call, paramErr);
        if (!valid)
            return window_result(call, window_invalid);
        EraseRgn((RgnHandle)SYN68K_TO_US(argument));
        return window_result(call, noErr);
    }
    if (offset == windows_get_proxy_icon)
        return window_result(call, valid && !argument ? paramErr : window_invalid);
    if (offset == windows_set_proxy_icon)
        return window_result(call, argument ? window_invalid : paramErr);
    if (offset == windows_set_proxy_file) {
        if (!valid)
            return window_result(call, window_invalid);
        return window_result(call, argument ? proxy_unavailable : paramErr);
    }
    if (offset == windows_get_proxy_alias || offset == windows_get_proxy_file || offset == windows_set_proxy_alias ||
        offset == windows_set_proxy_type || offset == windows_remove_proxy || offset == windows_begin_proxy_drag ||
        offset == windows_end_proxy_drag || offset == windows_track_proxy_drag ||
        offset == windows_track_existing_proxy_drag)
        return window_result(call, valid ? proxy_unavailable : window_invalid);
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

static int screen_bits_next(uint32_t node, uint32_t* next)
{
    *next = 0;
    return !node || (node <= UINT32_MAX - 0x1c && cfp_get(node + 0x10, next));
}

static int dispose_screen_bits(const ROMlib_carbon_reference* ref, uint32_t node)
{
    /* Check the chain before releasing any GWorld or node. Cycles would cause
       a second release from a freed allocation. */
    unsigned remaining = UINT16_MAX;
    for (uint32_t current = node, ahead = node; current;) {
        if (!remaining-- || !screen_bits_next(current, &current) || !screen_bits_next(ahead, &ahead) ||
            !screen_bits_next(ahead, &ahead) || (current && current == ahead))
            return 0;
    }
    const uint32_t reserved = US_TO_SYN68K(ref->toc) + 0x2434;
    Byte* reserved_in_use = ROMlib_carbon_guest_memory(US_TO_SYN68K(ref->toc) + 0x76d, 1, 1);
    if (!reserved_in_use)
        return 0;
    while (node) {
        uint32_t next, world;
        if (!screen_bits_next(node, &next) || !cfp_get(node + 0x14, &world) ||
            (world && !cfp_private(ref, 0x1b220, &world, 1, NULL)))
            return 0;
        if (node == reserved)
            *reserved_in_use = 0;
        else {
            _Alignas(8) uint32_t encoded = CL(node);
            const uint32_t pointer = US_TO_SYN68K(&encoded);
            if (!cfp_private(ref, 0x1be50, &pointer, 1, NULL))
                return 0;
        }
        node = next;
    }
    return 1;
}

static int windows_capture_screen_bits(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t bounds = call->words[0], output = call->words[2];
    if (!bounds || !output || !ROMlib_carbon_try_guest_memory(bounds, sizeof(Rect), 0) ||
        !ROMlib_carbon_try_guest_memory(output, sizeof(uint32_t), 1))
        return window_result(call, paramErr);
    const uint32_t state = US_TO_SYN68K(ref->toc);
    const Byte* enabled = ROMlib_carbon_guest_memory(state + 0x76c, 1, 0);
    if (!enabled)
        return 0;
    if (!*enabled)
        return window_result(call, memFullErr);
    uint32_t region;
    if (!cfp_private(ref, 0x136f8, NULL, 0, &region))
        return 0;
    if (!region)
        return window_result(call, MemError());
    const uint32_t rectangle[] = {region, bounds};
    if (!cfp_write(output, 0) || !cfp_private(ref, 0x1ac08, rectangle, NELEM(rectangle), NULL))
        return 0;

    _Alignas(8) struct {
        Rect bounds;
        uint32_t first;
        uint32_t status;
        Byte flags;
    } saved = {.flags = call->words[1]};

    if (!cfp_move(US_TO_SYN68K(&saved.bounds), bounds, sizeof saved.bounds))
        return 0;
    THz zone = GetZone();
    SetZone(SystemZone());
    const uint32_t args[] = {region, state + 0x12fc, US_TO_SYN68K(&saved), 1};
    const int captured = cfp_private(ref, 0x1ad28, args, NELEM(args), NULL);
    SetZone(zone);
    if (!captured || !cfp_private(ref, 0x137bc, &region, 1, NULL))
        return 0;
    const uint32_t first = CL(saved.first), status = CL(saved.status);
    if (status)
        return dispose_screen_bits(ref, first) && window_result(call, status);
    return cfp_write(output, first) && window_result(call, noErr);
}

static int windows_restore_screen_node(const ROMlib_carbon_reference* ref, uint32_t node, uint32_t mask, CGrafPtr port,
                                       int32_t* status)
{
    uint32_t world, screen;
    if (!cfp_get(node + 0x14, &world) || !cfp_get(node + 0x18, &screen))
        return 0;
    if (!world || !screen)
        return 0;
    GDHandle destination = (GDHandle)SYN68K_TO_US(screen);
    PORT_RECT(port) = GD_RECT(destination);
    ClipRect(&PORT_RECT(port));

    GDHandle disabled[32];
    unsigned count = 0;
    unsigned visited = 0;
    for (GDHandle device = GetDeviceList(); device && visited < 32; device = GD_NEXT_GD(device), ++visited) {
        if (device != destination && (CW(GD_FLAGS_X(device)) & 0x8000)) {
            if (count == NELEM(disabled))
                break;
            GD_FLAGS_X(device) = CW(CW(GD_FLAGS_X(device)) & 0x7fff);
            disabled[count++] = device;
        }
    }
    uint32_t pixmap, locked;
    int ok = cfp_private(ref, 0x1b6b8, &world, 1, &pixmap) && pixmap && cfp_private(ref, 0x1b6d0, &pixmap, 1, &locked);
    if (ok && (uint8_t)locked) {
        uint32_t pixels = 0;
        ok = cfp_get(pixmap, &pixels);
        const uint32_t copy[] = {pixels, US_TO_SYN68K(port) + 2, world + 0x10, node + 4, 0, mask};
        const int copied = ok && cfp_private(ref, 0x1b790, copy, NELEM(copy), NULL);
        ok = cfp_private(ref, 0x1b730, &pixmap, 1, NULL) && copied;
    }
    else if (ok)
        *status = memFullErr;
    while (count) {
        GDHandle device = disabled[--count];
        GD_FLAGS_X(device) = CW(CW(GD_FLAGS_X(device)) | 0x8000);
    }
    return ok;
}

static int windows_restore_screen_bits(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    uint32_t node = call->words[0];
    const uint32_t mask = call->words[1];
    uint32_t mask_size;
    if (mask && (!cfp_handle_data(mask, &mask_size, 0) || mask_size < 10))
        return window_result(call, paramErr);
    if (!node)
        return window_result(call, noErr);
    _Alignas(8) uint32_t manager = 0;
    const uint32_t output = US_TO_SYN68K(&manager);
    if (!cfp_private(ref, 0x1cba0, &output, 1, NULL))
        return 0;
    const uint32_t manager_address = CL(manager);
    CGrafPtr port = manager_address ? ROMlib_carbon_guest_memory(manager_address, sizeof *port, 1) : NULL;
    if (!port)
        return 0;
    GrafPtr saved_port = thePort;
    GDHandle saved_device = MR(TheGDevice);
    SetPort((GrafPtr)port);
    Rect original = PORT_RECT(port);
    RgnHandle clip = NewRgn();
    if (clip)
        GetClip(clip);
    ForeColor(blackColor);
    BackColor(whiteColor);
    int32_t status = noErr;
    int ok = 1;
    for (unsigned devices = 0; node && devices < UINT16_MAX; ++devices) {
        uint32_t next;
        if (node > UINT32_MAX - 0x1c || !cfp_get(node + 0x10, &next) || next == node ||
            !windows_restore_screen_node(ref, node, mask, port, &status)) {
            ok = 0;
            break;
        }
        node = next;
    }
    PORT_RECT(port) = original;
    if (clip) {
        SetClip(clip);
        DisposeRgn(clip);
    }
    else
        ClipRect(&original);
    SetPort(saved_port);
    SetGDevice(saved_device);
    return ok && !node && window_result(call, status);
}

int ROMlib_carbon_window_saved_screen_bits(const char* name, uint32_t offset, ROMlib_carbon_ppc_frame* call)
{
    enum {
        get_enabled = 0xd798,
        set_enabled = 0xd7a0,
        move_bits = 0xde28,
        save_bits = 0xd7c0,
        restore_bits = 0xdbe0,
        dispose_bits = 0xde84,
        representative_pixmap = 0xdef4
    };

    uint32_t node = call->words[0];
    if ((offset == move_bits || offset == dispose_bits || offset == representative_pixmap) && !node)
        return window_result(call, 0);
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_load_reference("WindowsLib", name, &ref))
        return ROMlib_carbon_report_failure("The saved WindowsLib screen bits are unavailable");
    if (ref.code_offset != offset || ref.code_size != 0x1cd10 || !ref.toc || ref.toc_offset || ref.toc_size != 0x27ec)
        return ROMlib_carbon_invoke_frame(ref.function, call);
    if (offset == save_bits)
        return windows_capture_screen_bits(&ref, call);
    if (offset == restore_bits)
        return windows_restore_screen_bits(&ref, call);
    if (offset == dispose_bits)
        return dispose_screen_bits(&ref, node);

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
    const bool desktop = offset == windows_set_desktop_pattern || offset == windows_get_desktop_pattern ||
                         offset == windows_get_desktop_pixpat || offset == windows_draw_desktop ||
                         offset == windows_rebuild_desktop;
    const bool monitors = offset == windows_get_monitor_list || offset == windows_set_monitor_list ||
                          offset == windows_dispose_monitor_list || offset == windows_refresh_monitor_list ||
                          offset == windows_get_internal_monitor_list || offset == windows_monitor_list_changed ||
                          offset == windows_count_monitor_records || offset == windows_get_monitor_record ||
                          offset == windows_set_monitor_record || offset == windows_monitor_has_picture ||
                          offset == windows_get_monitor_alignment || offset == windows_set_monitor_alignment ||
                          offset == windows_set_monitor_file || offset == windows_copy_monitor_file ||
                          offset == windows_set_device_picture || offset == windows_get_device_picture ||
                          offset == windows_file_may_contain_picture || offset == windows_get_picture_from_file ||
                          offset == windows_monitor_draw_takes_time;
    const bool proxy = offset == windows_get_proxy_alias || offset == windows_get_proxy_file ||
                       offset == windows_get_proxy_icon || offset == windows_set_proxy_alias ||
                       offset == windows_set_proxy_file || offset == windows_set_proxy_icon ||
                       offset == windows_set_proxy_type || offset == windows_remove_proxy ||
                       offset == windows_begin_proxy_drag || offset == windows_end_proxy_drag ||
                       offset == windows_track_proxy_drag || offset == windows_track_existing_proxy_drag;
    const bool geometry = offset == windows_get_region || offset == windows_global_find ||
                          offset == windows_erase_region || offset == windows_install_paint_proc ||
                          offset == windows_reposition_window || offset == windows_reshape_windows ||
                          offset == windows_recalculate_title_widths || offset == windows_collapse_window ||
                          offset == windows_collapse_all_windows || offset == windows_title_double_click ||
                          offset == windows_transition;
    const bool extra =
        desktop || monitors || proxy || geometry || offset == windows_set_features || offset == windows_get_features ||
        offset == windows_is_collapsable || offset == windows_is_collapsed || offset == windows_is_path_select ||
        offset == windows_get_pattern || offset == windows_set_pattern || offset == windows_set_theme_background ||
        offset == windows_next_activation || offset == windows_post_activation || offset == windows_post_reshape ||
        offset == windows_hilite_drag_frame;
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_probe_reference("WindowsLib", name, &ref) &&
        (!extra || !ROMlib_carbon_load_reference("WindowsLib", name, &ref))) {
        if (desktop || monitors) {
            if (offset == windows_draw_desktop && GrayRgn) {
                PaintOne(NULL, MR(GrayRgn));
                return 1;
            }
            return window_result(call, -4);
        }
        if (extra && !proxy && !geometry && offset != windows_get_features && offset != windows_is_collapsable &&
            offset != windows_is_collapsed)
            return ROMlib_carbon_report_failure("The saved WindowsLib window metadata is unavailable");
        if (offset == windows_title_double_click)
            return 0;
        if (offset == windows_transition)
            return windows_transition_entry(NULL, call);
        return romaw_window_metadata(offset, call);
    }
    if (ref.code_offset != offset || ref.code_size != 0x1cd10 || !ref.toc || ref.toc_offset || ref.toc_size != 0x27ec)
        return ROMlib_carbon_invoke_frame(ref.function, call);
    if (monitors)
        return windows_monitor_lists(&ref, offset, call);
    switch (offset) {
    case windows_set_features:
    case windows_get_features:
    case windows_is_collapsable:
    case windows_is_collapsed:
        return windows_features(&ref, offset, call);
    case windows_is_path_select:
        return windows_path_select_click(&ref, call);
    case windows_get_pattern:
    case windows_set_pattern:
        return windows_content_pattern(&ref, offset, call);
    case windows_set_theme_background:
        return windows_theme_background(&ref, call);
    case windows_next_activation:
        return windows_next_activation_event(&ref, call);
    case windows_post_activation:
        return windows_post_activation_event(&ref, call);
    case windows_post_reshape:
        return windows_post_delayed_reshape(&ref, call);
    case windows_hilite_drag_frame:
        return windows_hilite_drag(&ref, call);
    case windows_get_proxy_alias:
    case windows_get_proxy_file:
        return windows_proxy_file(&ref, offset, call);
    case windows_get_proxy_icon:
        return windows_proxy_icon(&ref, call);
    case windows_set_proxy_alias:
    case windows_set_proxy_file:
    case windows_set_proxy_icon:
    case windows_set_proxy_type:
    case windows_remove_proxy:
        return windows_change_proxy(&ref, offset, call);
    case windows_begin_proxy_drag:
    case windows_end_proxy_drag:
    case windows_track_proxy_drag:
    case windows_track_existing_proxy_drag:
        return windows_proxy_drag(&ref, offset, call);
    case windows_get_region: {
        uint32_t status;
        return windows_read_window_region(&ref, call->words[0], call->words[1], call->words[2], &status) &&
               window_result(call, status);
    }
    case windows_global_find:
        return windows_global_find_window(&ref, call);
    case windows_erase_region:
        return windows_erase_content(&ref, call);
    case windows_install_paint_proc:
        return windows_install_content_paint(&ref, call);
    case windows_set_desktop_pattern:
        return windows_install_desktop_pattern(&ref, call);
    case windows_get_desktop_pattern:
    case windows_get_desktop_pixpat:
        return windows_get_desktop(&ref, offset == windows_get_desktop_pixpat, call);
    case windows_draw_desktop:
        return windows_draw_saved_desktop(&ref);
    case windows_rebuild_desktop:
        return windows_rebuild_saved_desktop(&ref, call);
    case windows_reposition_window:
        return windows_reposition(&ref, call);
    case windows_reshape_windows:
        return windows_reshape_all(&ref, call);
    case windows_recalculate_title_widths:
        return windows_recalculate_titles(&ref);
    case windows_collapse_window:
    case windows_collapse_all_windows:
        return windows_collapse_group(&ref, offset, call);
    case windows_title_double_click:
        return windows_handle_title_double_click(&ref, call);
    case windows_transition:
        return windows_transition_entry(&ref, call);
    }
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
    if (((const Byte*)ref->toc)[0x6d08]) {
        const uint32_t get[] = {window, window_attributes_property, size, 0, address};
        const uint32_t set[] = {window, window_attributes_property, size, address};
        return operation == property_get ? cfp_private(ref, 0xb4530, get, NELEM(get), status)
                                         : cfp_private(ref, 0xb4500, set, NELEM(set), status);
    }
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
    if (ROMlib_carbon_probe_reference("WindowsLib", "GetWindowOwnerCount", &ref) && ref.code_offset == 0xefc &&
        ref.code_size == 0x1cd10 && ref.toc && !ref.toc_offset && ref.toc_size == 0x27ec) {
        uint32_t metadata;
        if (!windows_metadata_record(&ref, window, &metadata))
            return 0;
        if (metadata) {
            /* Saved CloneWindow increments this field directly. All native
               ownership entry points must observe the same count. */
            const uint32_t count_address = metadata + 0x24;
            if (!cfp_get(count_address, owners))
                return 0;
            *status = noErr;
            if (!change)
                return 1;
            if (change > 0 && *owners == UINT32_MAX) {
                *status = paramErr;
                return 1;
            }
            if (change < 0 && *owners <= 1) {
                *owners = 0;
                return 1;
            }
            *owners = change > 0 ? *owners + 1 : *owners - 1;
            return cfp_write(count_address, *owners);
        }
    }
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

static int window_property_layout(const ROMlib_carbon_reference* ref, uint32_t offset)
{
    return ref->code_offset == offset && ref->code_size == 0xb8fb4 && ref->toc && ref->toc_offset == 0x2f78 &&
           ref->toc_size == 0x10f0c;
}

static int window_property_profile(const ROMlib_carbon_reference* ref, uint32_t offset, int control)
{
    return window_property_layout(ref, offset) && !((const Byte*)ref->toc)[control ? 0x6cd0 : 0x6d08];
}

enum {
    window_info_flags = 5,
    window_class_field = 6,
    window_attribute_field = 7,
    window_scope_field = 14,
    window_latent_reasons_field = 15,
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

static int zoom_window_to_ideal(const ROMlib_carbon_reference* ref, uint32_t window, uint32_t size, uint32_t* status)
{
    uint32_t collapsed, packed_size;
    if (!size || !ROMlib_carbon_try_guest_memory(size, sizeof(Point), 1)) {
        *status = paramErr;
        return 1;
    }
    if (!cfp_private(ref, 0xb64e0, &window, 1, &collapsed))
        return 0;
    if (collapsed) {
        const uint32_t expand[] = {window, 0};
        if (!cfp_private(ref, 0xb7998, expand, NELEM(expand), NULL))
            return 0;
    }
    if (!cfp_get(size, &packed_size))
        return 0;
    _Alignas(8) Rect ideal = {0}, current = {0};
    const uint32_t constrain[] = {window, packed_size, US_TO_SYN68K(&ideal)};
    if (!cfp_private(ref, 0x251b8, constrain, NELEM(constrain), status))
        return 0;
    if (*status)
        return 1;
    const uint16_t height = Cx(ideal.bottom) - Cx(ideal.top), width = Cx(ideal.right) - Cx(ideal.left);
    const uint32_t current_args[] = {window, US_TO_SYN68K(&current)};
    const uint32_t ideal_args[] = {window, US_TO_SYN68K(&ideal)};
    const uint32_t zoom[] = {window, 8, 0};
    return cfp_write(size, ((uint32_t)height << 16) | width) &&
           cfp_private(ref, 0x59144, current_args, NELEM(current_args), NULL) &&
           cfp_private(ref, 0xb2e68, current_args, NELEM(current_args), NULL) &&
           cfp_private(ref, 0xb2da8, current_args, NELEM(current_args), NULL) &&
           cfp_private(ref, 0xb2e50, ideal_args, NELEM(ideal_args), NULL) &&
           cfp_private(ref, 0xb2d18, zoom, NELEM(zoom), NULL);
}

static int window_zoom_changed(const ROMlib_carbon_reference* ref, uint32_t window, Rect* before)
{
    _Alignas(8) Rect after = {0};
    const uint32_t bounds[] = {window, US_TO_SYN68K(&after)};
    const uint32_t update[] = {window, US_TO_SYN68K(before), bounds[1]};
    uint32_t constants, bits, entry;
    if (!cfp_private(ref, 0x59144, bounds, NELEM(bounds), NULL) ||
        (memcmp(before, &after, sizeof after) && !cfp_private(ref, 0x5953c, update, NELEM(update), NULL)) ||
        !cfp_get(US_TO_SYN68K(ref->toc) - 0x1e8, &constants) || !constants || !cfp_get(constants + 0x50, &bits))
        return 0;
    float interval;
    memcpy(&interval, &bits, sizeof interval);
    const double timeout = interval;
    memcpy(&entry, ref->function, sizeof entry);
    _Alignas(16) uint32_t vector[] = {CL(CL(entry) - ref->code_offset + 0x4c96c), CL(US_TO_SYN68K(ref->toc))};
    ROMlib_carbon_ppc_frame event = {.words = {window, 0x4c}};
    memcpy(&event.floating[0], &timeout, sizeof timeout);
    return ROMlib_carbon_invoke_frame(vector, &event);
}

static int window_zoom_ideal(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0], size = call->words[2];
    const int16_t direction = call->words[1];
    uint32_t valid, delegate;
    if (!cfp_private(ref, 0xb2c28, &window, 1, &valid))
        return 0;
    if (!valid)
        return window_result(call, window_invalid);
    if (!window_delegate(ref, -0x1470, US_TO_SYN68K(ref->toc) + 0x7894, -0x1e8, 0, 0x3e, &delegate))
        return 0;
    if (delegate) {
        uint32_t version;
        if (!cfp_private(ref, 0x56014, NULL, 0, &version))
            return 0;
        if ((int32_t)version <= 0x8ff && direction == 8) {
            _Alignas(8) uint32_t standard = 0;
            const uint32_t query[] = {window, 33, US_TO_SYN68K(&standard), 0};
            if (!cfp_private(ref, 0xb3ab0, query, NELEM(query), NULL))
                return 0;
            if (!standard)
                delegate = 0;
        }
    }

    _Alignas(8) Rect before = {0}, user = {0};
    const uint32_t bounds[] = {window, US_TO_SYN68K(&before)};
    if (!cfp_private(ref, 0x59144, bounds, NELEM(bounds), NULL))
        return 0;
    uint32_t status = noErr;
    if (delegate) {
        const uint32_t args[] = {window, (uint32_t)(int32_t)direction, size};
        if (!cfp_callback(delegate, args, NELEM(args), &status))
            return 0;
    }
    else {
        if (direction != 7 && direction != 8)
            return window_result(call, paramErr);
        _Alignas(8) uint32_t window_class = 0;
        const uint32_t query[] = {window, US_TO_SYN68K(&window_class)};
        uint32_t sound_available;
        if (!cfp_private(ref, 0xb2b08, query, NELEM(query), NULL) ||
            !cfp_get(US_TO_SYN68K(ref->toc) - 0x2cbc, &sound_available))
            return 0;
        if (sound_available) {
            const bool utility = CL(window_class) == 5;
            const uint32_t sound =
                direction == 7 ? (utility ? 0x757a6d69 : 0x777a6d69) : (utility ? 0x757a6d6f : 0x777a6d6f);
            if (!cfp_private(ref, 0xb61e0, &sound, 1, NULL))
                return 0;
        }
        if (direction == 7) {
            const uint32_t state[] = {window, US_TO_SYN68K(&user)};
            const uint32_t zoom[] = {window, 7, 0};
            if (!cfp_private(ref, 0xb2d90, state, NELEM(state), NULL) ||
                !cfp_private(ref, 0xb2e68, state, NELEM(state), NULL) ||
                !cfp_private(ref, 0xb2d18, zoom, NELEM(zoom), NULL))
                return 0;
        }
        else if (!zoom_window_to_ideal(ref, window, size, &status))
            return 0;
    }
    return (status || window_zoom_changed(ref, window, &before)) && window_result(call, status);
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

static int window_device_for_bounds(const ROMlib_carbon_reference* ref, const Rect* search_bounds, uint32_t* device,
                                    Rect* result)
{
    _Alignas(64) struct {
        uint32_t device;
        uint32_t reserved[2];
        Rect window_bounds;
        uint32_t area;
    } search = {.window_bounds = *search_bounds};

    if (!cfp_private(ref, 0xb17b8, NULL, 0, device))
        return 0;
    search.device = CL(*device);
    const uint32_t enumerate[] = {0, US_TO_SYN68K(ref->toc) + 0x7870, US_TO_SYN68K(&search), 5};
    if (!cfp_private(ref, 0xb17e8, enumerate, NELEM(enumerate), NULL))
        return 0;
    *device = CL(search.device);
    const uint32_t read_bounds[] = {*device, US_TO_SYN68K(result)};
    return cfp_private(ref, 0xb4458, read_bounds, NELEM(read_bounds), NULL);
}

static int window_greatest_device(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0], device_output = call->words[2], bounds_output = call->words[3];
    uint32_t valid;
    if (!cfp_private(ref, 0xb2c28, &window, 1, &valid))
        return 0;
    if (!valid)
        return window_result(call, window_invalid);

    _Alignas(8) Rect search = {0}, bounds = {0};
    const uint32_t lookup[] = {window, (uint16_t)call->words[1], US_TO_SYN68K(&search)};
    uint32_t status, device;
    if (!cfp_private(ref, 0xb2d30, lookup, NELEM(lookup), &status))
        return 0;
    if (status)
        return window_result(call, status);
    return window_device_for_bounds(ref, &search, &device, &bounds) &&
           (!device_output || cfp_write(device_output, device)) &&
           (!bounds_output || cfp_move(bounds_output, US_TO_SYN68K(&bounds), sizeof bounds)) &&
           window_result(call, noErr);
}

enum {
    window_constrain_resize = 1,
    window_constrain_move_regardless = 2,
    window_constrain_allow_partial = 4,
    window_constrain_calculate_only = 8,
    window_cannot_fit = -5611
};

static bool window_intersection_fits(const Rect* bounds, const Rect* intersection)
{
    const uint32_t width = Cx(intersection->right) - Cx(intersection->left);
    const uint32_t height = Cx(intersection->bottom) - Cx(intersection->top);
    return width * height > 24 && (Cx(bounds->right) - Cx(bounds->left) < 7 || width > 6) &&
           (Cx(bounds->bottom) - Cx(bounds->top) < 7 || height > 6);
}

static int16_t window_constrain_offset(int start, int end, int hit_start, int hit_end, int limit_start, int limit_end)
{
    if (start < limit_start && end > limit_end)
        return 0;
    if (hit_start == hit_end) {
        if (start < limit_start)
            return limit_start - start + 3;
        return end > limit_end ? limit_end - end - 3 : 0;
    }
    const int span = end - start - (hit_end - hit_start);
    int16_t offset = (int16_t)span ? span + 3 : 0;
    if (end > limit_end)
        offset = -offset;
    return start + offset < limit_start ? limit_start - start : offset;
}

static bool constrain_window_bounds(Rect* region, Rect* screen, Rect* structure, uint32_t options)
{
    const bool partial = options & window_constrain_allow_partial;
    Rect intersection;
    SectRect(region, screen, &intersection);
    if (!memcmp(region, &intersection, sizeof *region) || (partial && window_intersection_fits(region, &intersection)))
        return true;
    Rect shifted = *region;
    const int dx = window_constrain_offset(Cx(region->left), Cx(region->right), Cx(intersection.left),
                                           Cx(intersection.right), Cx(screen->left), Cx(screen->right));
    const int dy = window_constrain_offset(Cx(region->top), Cx(region->bottom), Cx(intersection.top),
                                           Cx(intersection.bottom), Cx(screen->top), Cx(screen->bottom));
    OffsetRect(&shifted, dx, dy);
    OffsetRect(structure, dx, dy);
    SectRect(&shifted, screen, &intersection);
    if (!memcmp(&shifted, &intersection, sizeof shifted) ||
        (partial && window_intersection_fits(region, &intersection)))
        return true;
    if (!(options & window_constrain_resize))
        return false;
    if (Cx(shifted.right) - Cx(shifted.left) > Cx(screen->right) - Cx(screen->left)) {
        structure->left = CW(Cx(shifted.left) - Cx(structure->left) - Cx(screen->left) + 3);
        structure->right = CW(Cx(screen->right) + Cx(structure->right) - Cx(shifted.right) - 3);
    }
    if (Cx(shifted.bottom) - Cx(shifted.top) > Cx(screen->bottom) - Cx(screen->top)) {
        structure->top = CW(Cx(shifted.top) - Cx(structure->top) - Cx(screen->top) + 3);
        structure->bottom = CW(Cx(screen->bottom) + Cx(structure->bottom) - Cx(shifted.bottom) - 3);
    }
    return true;
}

static int constrain_window_to_screen(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0], options = call->words[2];
    const uint16_t region = call->words[1];
    uint32_t valid, status = noErr;
    if (!cfp_private(ref, 0xb2c28, &window, 1, &valid))
        return 0;
    if (!valid)
        return window_result(call, window_invalid);
    _Alignas(8) Rect requested = {0}, screen = {0}, target = {0};
    const uint32_t content[] = {window, US_TO_SYN68K(&requested)};
    const uint32_t bounds[] = {window, region, content[1]};
    if (region == 33 ? !cfp_private(ref, 0x59144, content, NELEM(content), NULL)
                     : !cfp_private(ref, 0xb2d30, bounds, NELEM(bounds), &status))
        return 0;
    if (status)
        return window_result(call, status);
    if (call->words[3]) {
        if (!cfp_move(US_TO_SYN68K(&screen), call->words[3], sizeof screen))
            return 0;
    }
    else {
        uint32_t device;
        if (!window_device_for_bounds(ref, &requested, &device, &screen))
            return 0;
    }
    const uint32_t structure[] = {window, 32, US_TO_SYN68K(&target)};
    if (!cfp_private(ref, 0xb2d30, structure, NELEM(structure), NULL))
        return 0;
    if (!constrain_window_bounds(&requested, &screen, &target, options) &&
        !(options & window_constrain_move_regardless))
        return window_result(call, window_cannot_fit);
    const uint32_t output = call->words[4];
    return (!output || cfp_move(output, structure[2], sizeof target)) &&
           ((options & window_constrain_calculate_only) ||
            cfp_private(ref, 0xb2d60, structure, NELEM(structure), NULL)) &&
           window_result(call, noErr);
}

int ROMlib_carbon_window_activation(const char* name, uint32_t offset, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", name, &ref))
        return 0;
    if (!window_property_profile(&ref, offset, 0))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    switch (offset) {
    case 0x24f30: /* ConstrainWindowToScreen */
        return constrain_window_to_screen(&ref, call);
    case 0x254f8: /* ZoomWindowIdeal */
        return window_zoom_ideal(&ref, call);
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
    if ((operation == property_metadata_get && !argument) || (operation == property_scope_set && argument >= 3))
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
    WindowPtr handle = window ? (WindowPtr)SYN68K_TO_US(window) : NULL;
    if (!handle || !ROMlib_valid_window(handle) || !ROMlib_carbon_probe_reference("CarbonLib", "SetWTitle", &ref) ||
        !window_property_layout(&ref, 0xfbbc))
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

static int window_sheet_parent(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0];
    uint32_t valid;
    if (!cfp_private(ref, 0xb2c28, &window, 1, &valid))
        return 0;
    if (!valid)
        return window_result(call, window_invalid);
    const uint32_t result = call->words[1];
    if (!result || !cfp_write(result, 0))
        return window_result(call, paramErr);
    const uint32_t query[] = {window, 0x73706172, sizeof(uint32_t), 0, result}; /* 'spar' */
    return cfp_private(ref, 0xb4530, query, NELEM(query), NULL) && window_result(call, noErr);
}

static int window_latent_visibility(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0];
    uint32_t valid, visible;
    if (!cfp_private(ref, 0xb2c28, &window, 1, &valid))
        return 0;
    if (!valid)
        return window_result(call, false);
    if (!cfp_private(ref, 0xb2dc0, &window, 1, &visible))
        return 0;
    const uint32_t result = call->words[1];
    if (!result)
        return window_result(call, visible);

    _Alignas(8) uint32_t record[17] = {0};
    uint32_t status;
    if (!window_info_record(ref, property_get, window, US_TO_SYN68K(record), &status))
        return 0;
    uint32_t reasons = status ? 0 : CL(record[window_latent_reasons_field]);
    const Byte* own_visibility = ROMlib_carbon_guest_memory(window + 0x6e, 1, 0);
    if (!visible && own_visibility && (*own_visibility || reasons)) {
        uint32_t parent;
        if (!cfp_private(ref, 0xb6210, &window, 1, &parent))
            return 0;
        const Byte* parent_visibility = parent ? ROMlib_carbon_guest_memory(parent + 0x6e, 1, 0) : NULL;
        if (parent_visibility && !*parent_visibility)
            reasons |= 8;
    }
    return cfp_write(result, reasons) && window_result(call, visible);
}

enum {
    scroll_window_invalidate = 1,
    scroll_window_erase = 2,
    scroll_window_options = scroll_window_invalidate | scroll_window_erase
};

static bool scroll_window_offsets_fit(const Rect* bounds, int16_t dx, int16_t dy)
{
    return !(dy < 0 && CW(bounds->top) + dy < -INT16_MAX) && !(dy > 0 && CW(bounds->bottom) + dy > INT16_MAX) &&
           !(dx < 0 && CW(bounds->left) + dx < -INT16_MAX) && !(dx > 0 && CW(bounds->right) + dx > INT16_MAX);
}

static int scroll_window_update(const ROMlib_carbon_reference* ref, uint32_t window, uint32_t port, RgnHandle area,
                                RgnHandle exposed, RgnHandle update, int16_t dx, int16_t dy, uint32_t options,
                                bool invalidate, uint32_t* status)
{
    const uint32_t update_address = US_TO_SYN68K(update);
    const uint32_t query[] = {window, 34, update_address}; /* kWindowUpdateRgn */
    if (!cfp_private(ref, 0xb2c40, query, NELEM(query), status))
        return 0;
    if (*status)
        return 1;
    const uint32_t local[] = {port, update_address};
    if (!cfp_private(ref, 0xb4548, local, NELEM(local), NULL))
        return 0;
    SectRgn(area, update, update);
    if (!EmptyRgn(update)) {
        const uint32_t affected[] = {window, update_address};
        if (!cfp_private(ref, 0xb2ca0, affected, NELEM(affected), NULL))
            return 0;
        OffsetRgn(update, dx, dy);
        SectRgn(area, update, update);
        if (!cfp_private(ref, 0xb2c70, affected, NELEM(affected), NULL))
            return 0;
    }
    if (options & scroll_window_erase) {
        GrafPtr previous = thePort;
        SetPort((GrafPtr)SYN68K_TO_US(port));
        EraseRgn(exposed);
        SetPort(previous);
    }
    const uint32_t invalid[] = {window, US_TO_SYN68K(exposed)};
    return !invalidate || cfp_private(ref, 0xb2c70, invalid, NELEM(invalid), NULL);
}

static int scroll_window_region(const ROMlib_carbon_reference* ref, uint32_t window, uint32_t input, int16_t dx,
                                int16_t dy, uint32_t options, uint32_t output, uint32_t* status)
{
    uint32_t valid;
    if (!cfp_private(ref, 0xb2c28, &window, 1, &valid))
        return 0;
    if (!valid) {
        *status = window_invalid;
        return 1;
    }
    uint32_t size;
    const Byte* region = cfp_handle_data(input, &size, 0);
    if (!region || size < 10 || (!dx && !dy) || (options & ~scroll_window_options) ||
        (output && (!cfp_handle_data(output, &size, 1) || size < 10))) {
        *status = paramErr;
        return 1;
    }
    Rect bounds;
    memcpy(&bounds, region + 2, sizeof bounds);
    *status = noErr;
    if (!scroll_window_offsets_fit(&bounds, dx, dy))
        return 1;

    uint32_t port_address;
    if (!cfp_private(ref, 0xb2df0, &window, 1, &port_address))
        return 0;
    GrafPtr port = port_address ? ROMlib_carbon_guest_memory(port_address, sizeof *port, 0) : NULL;
    if (!port)
        return 0;
    RgnHandle area = NewRgn();
    RgnHandle exposed = output ? (RgnHandle)SYN68K_TO_US(output) : NULL;
    RgnHandle copied = NULL, update = NULL;
    int ok = 1;
    if (area) {
        CopyRgn((RgnHandle)SYN68K_TO_US(input), area);
        SectRgn(PORT_VIS_REGION(port), area, area);
        SectRgn(PORT_CLIP_REGION(port), area, area);
        if (!output)
            exposed = NewRgn();
        if (exposed)
            copied = NewRgn();
        if (copied) {
            CopyRgn(area, copied);
            CopyRgn(area, exposed);
            /* Keep the device-dependent pixel move on the saved QuickDraw back end. */
            const uint32_t transfer[] = {port_address, US_TO_SYN68K(copied), (uint32_t)(int32_t)dx,
                                         (uint32_t)(int32_t)dy};
            ok = cfp_private(ref, 0x9cc8, transfer, NELEM(transfer), status);
            if (ok && !*status) {
                DiffRgn(exposed, copied, exposed);
                update = NewRgn();
                if (update)
                    ok = scroll_window_update(ref, window, port_address, area, exposed, update, dx, dy, options,
                                              !output || (options & scroll_window_invalidate), status);
            }
        }
    }
    if (ok && !*status && !update)
        *status = MemError();
    if (update)
        DisposeRgn(update);
    if (copied)
        DisposeRgn(copied);
    if (exposed && !output)
        DisposeRgn(exposed);
    if (area)
        DisposeRgn(area);
    return ok;
}

static int window_scroll(const ROMlib_carbon_reference* ref, bool rectangle, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    const uint32_t window = args[0];
    uint32_t status;
    if (!rectangle)
        return scroll_window_region(ref, window, args[1], args[2], args[3], args[4], args[5], &status) &&
               window_result(call, status);

    Rect* bounds = args[1] ? ROMlib_carbon_try_guest_memory(args[1], sizeof *bounds, 0) : NULL;
    if (!bounds)
        return window_result(call, paramErr);
    RgnHandle region = NewRgn();
    if (!region)
        return window_result(call, MemError());
    RectRgn(region, bounds);
    const int ok = scroll_window_region(ref, window, US_TO_SYN68K(region), args[2], args[3], args[4], args[5], &status);
    DisposeRgn(region);
    /* CarbonLib 1.4 returns only allocation errors from the rectangle wrapper. */
    return ok && window_result(call, noErr);
}

static int window_hit_for_process(const ROMlib_carbon_reference* ref, uint32_t window, bool* owned)
{
    uint32_t parent, application_layer, process_layer;
    if (!cfp_private(ref, 0xb6210, &window, 1, &parent) || !lookup_window_global(ref, -0x168c, &application_layer) ||
        !lookup_window_global(ref, -0x1690, &process_layer))
        return 0;
    *owned = parent == application_layer;
    if (*owned || parent != process_layer)
        return 1;

    _Alignas(8) uint32_t properties[17] = {0};
    _Alignas(8) Byte belongs = 0;
    const uint32_t record = US_TO_SYN68K(properties);
    uint32_t status;
    if (!window_info_record(ref, property_get, window, record, &status))
        return 0;
    if (!status) {
        const uint32_t query[] = {cfp_toc(ref, (uint32_t)-0x1698), record, US_TO_SYN68K(&belongs)};
        if (!cfp_private(ref, 0xb1188, query, NELEM(query), &status))
            return 0;
        *owned = !status && belongs;
    }
    return 1;
}

static int find_window_of_class(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    uint16_t part_code = 0;
    void* part = args[3] ? ROMlib_carbon_try_guest_memory(args[3], sizeof part_code, 1) : NULL;
    if ((args[2] && !cfp_write(args[2], 0)) || (args[3] && !part))
        return window_result(call, paramErr);
    if (part)
        memcpy(part, &part_code, sizeof part_code);
    uint32_t point;
    if (!args[0] || !cfp_get(args[0], &point))
        return window_result(call, paramErr);

    _Alignas(8) uint32_t result = 0;
    const uint32_t hit[] = {point, US_TO_SYN68K(&result), args[3]};
    if (!cfp_private(ref, 0x10f2c, hit, NELEM(hit), NULL))
        return 0;
    uint32_t window = CL(result);
    if (window) {
        bool owned;
        if (!window_hit_for_process(ref, window, &owned))
            return 0;
        if (!owned)
            window = 0;
        else {
            _Alignas(8) uint32_t window_class = 0;
            const uint32_t query[] = {window, US_TO_SYN68K(&window_class)};
            if (!cfp_private(ref, 0xb2b08, query, NELEM(query), NULL))
                return 0;
            if (args[1] != UINT32_MAX && args[1] != CL(window_class))
                window = 0;
        }
    }
    int32_t status = noErr;
    if (!window && part) {
        memcpy(&part_code, part, sizeof part_code);
        if (CW(part_code) != inMenuBar) {
            part_code = 0;
            memcpy(part, &part_code, sizeof part_code);
            status = window_group_not_found;
        }
    }
    return (!args[2] || cfp_write(args[2], window)) && window_result(call, status);
}

static int dispatch_property(const char* name, uint32_t offset, enum carbon_property_operation operation,
                             uint32_t argument, int control, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", name, &ref))
        return 0;
    if ((operation == property_title_get || operation == property_title_set) && window_property_layout(&ref, offset))
        return window_title(&ref, operation, argument, call);
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
    if (operation == property_class_valid || operation == property_available_attributes)
        return window_class_query(&ref, operation, call);
    if (operation == property_window_class_search)
        return window_class_search(&ref, argument, call);
    if (operation == property_find_window_of_class)
        return find_window_of_class(&ref, call);
    if (operation == property_sheet_parent)
        return window_sheet_parent(&ref, call);
    if (operation == property_latent_visibility)
        return window_latent_visibility(&ref, call);
    if (operation == property_scroll_region || operation == property_scroll_rect)
        return window_scroll(&ref, operation == property_scroll_rect, call);
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
