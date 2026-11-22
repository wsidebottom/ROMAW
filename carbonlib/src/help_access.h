/*+===================================================================
  File:      help_access.h

  Summary:   Carbon Help Manager properties, content, and display state.

  Classes:   None.

  Functions: Private frame handlers for the Carbon export registry.

  Origin:    CarbonLib 1.4 disassembly, with classic Window Manager and
             Menu Manager property and coordinate conventions.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#ifndef ROMAW_CARBON_HELP_ACCESS_H
#define ROMAW_CARBON_HELP_ACCESS_H

#include "cf_batch.h"
#include "cfx_internal.h"

enum help_owner { help_control, help_window, help_menu_title, help_menu_item };

enum help_action { help_read, help_write, help_remove, help_mark };

enum { help_absent = -5604, help_content_extent = 0x216, help_item_extent = 24 };

static int help_result(ROMlib_carbon_ppc_frame* call, uint32_t status)
{
    call->words[0] = status;
    return 1;
}

static int help_property(const ROMlib_carbon_reference* ref, enum help_owner owner, enum help_action action,
                         uint32_t object, uint16_t item, uint32_t tag, uint32_t size, uint32_t buffer, uint32_t* status)
{
    static const uint32_t workers[][4] = {
        [help_control] = {0xb45c0, 0xb45d8, 0xb45f0, 0xb45a8},
        [help_window] = {0xb4530, 0xb4500, 0xb4518, 0xb44e8},
        [help_menu_title] = {0xb4608, 0xb4650, 0xb4668, 0xb4620},
        [help_menu_item] = {0xb4608, 0xb4650, 0xb4668, 0xb4620},
    };
    _Alignas(4) uint32_t actual = 0;
    uint32_t args[6];
    unsigned count = 0;
    args[count++] = object;
    if (owner == help_menu_item || owner == help_menu_title)
        args[count++] = item;
    args[count++] = tag;
    if (action == help_mark) {
        args[count++] = 1;
        args[count++] = 0;
    }
    else if (action != help_remove) {
        args[count++] = size;
        if (action == help_read)
            args[count++] = US_TO_SYN68K(&actual);
        args[count++] = buffer;
    }
    return cfp_private(ref, workers[owner][action], args, count, status);
}

static int help_callback(const ROMlib_carbon_reference* ref, enum help_owner owner, bool install,
                         ROMlib_carbon_ppc_frame* call)
{
    const uint32_t object = call->words[0], value = call->words[1];
    const uint32_t tag = owner == help_menu_item ? TICK("hcli") : TICK("hcal");
    uint32_t status;
    if (!install) {
        if (!value)
            return help_result(call, paramErr);
        if (!help_property(ref, owner, help_read, object, 0, tag, 4, value, &status))
            return 0;
        if ((int32_t)status == help_absent)
            return cfp_write(value, 0) && help_result(call, noErr);
        return help_result(call, status);
    }
    _Alignas(4) uint32_t procedure = CL(value);
    if (value)
        return help_property(ref, owner, help_write, object, 0, tag, 4, US_TO_SYN68K(&procedure), &status) &&
               help_result(call, status);
    return cfp_private(ref, 0x6318c, NULL, 0, NULL) &&
           help_property(ref, owner, help_remove, object, 0, tag, 0, 0, &status) &&
           help_result(call, (int32_t)status == help_absent ? noErr : status);
}

static int help_release_content(const ROMlib_carbon_reference* ref, uint32_t content)
{
    for (unsigned offset = 0xe; offset <= 0x112; offset += 0x104) {
        uint32_t type, value;
        if (!cfp_get(content + offset, &type))
            return 0;
        if (type == TICK("cfst") &&
            (!cfp_get(content + offset + 4, &value) || (value && !cfp_private(ref, 0xb6330, &value, 1, NULL))))
            return 0;
    }
    return 1;
}

static int help_cleanup_handler(const ROMlib_carbon_reference* ref, enum help_owner owner, uint32_t object)
{
    const bool control = owner == help_control;
    const uint32_t tag = control ? TICK("chlp") : TICK("whlp");
    _Alignas(4) uint32_t handler = 0;
    const uint32_t output = US_TO_SYN68K(&handler);
    uint32_t status;
    if (!help_property(ref, owner, help_read, object, 0, tag, 4, output, &status))
        return 0;
    if ((int32_t)status != help_absent)
        return 1;
    _Alignas(8) uint32_t event[2];
    const uint32_t event_type = US_TO_SYN68K(event);
    uint32_t target;
    if (!cfp_move(event_type, cfp_toc(ref, (uint32_t)(control ? -0x674 : -0x66c)), sizeof event) ||
        !cfp_private(ref, control ? 0xb3540 : 0xb3528, &object, 1, &target))
        return 0;
    const uint32_t install[] = {target, cfp_toc(ref, (uint32_t)(control ? -0x670 : -0x668)), 1, event_type, 0, output};
    return cfp_private(ref, 0xb35a0, install, NELEM(install), &status) &&
           (status || help_property(ref, owner, help_write, object, 0, tag, 4, output, &status));
}

static int help_content_property(const ROMlib_carbon_reference* ref, enum help_owner owner, bool set,
                                 ROMlib_carbon_ppc_frame* call)
{
    const uint32_t object = call->words[0], argument = call->words[owner == help_menu_item ? 2 : 1];
    const uint16_t item = owner == help_menu_item ? (uint16_t)call->words[1] : 0;
    const uint32_t tag = TICK("help");
    uint32_t status;
    if (!object || (!set && !argument))
        return help_result(call, paramErr);
    if (!set)
        return help_property(ref, owner, help_read, object, item, tag, help_content_extent, argument, &status) &&
               help_result(call, status);
    _Alignas(1024) Byte old[help_content_extent], copy[help_content_extent];
    const uint32_t previous = US_TO_SYN68K(old);
    if (!help_property(ref, owner, help_read, object, item, tag, sizeof old, previous, &status))
        return 0;
    const bool had_content = status == noErr;
    if (!argument) {
        if (!help_property(ref, owner, help_remove, object, item, tag, 0, 0, &status))
            return 0;
        if ((int32_t)status == help_absent)
            status = noErr;
        return (status || !had_content || help_release_content(ref, previous)) && help_result(call, status);
    }
    uint32_t payload;
    if (!ROMlib_carbon_guest_memory(argument, sizeof copy, 0) ||
        !cfp_private(ref, 0x61efc, (const uint32_t[]){argument, US_TO_SYN68K(copy)}, 2, &payload))
        return 0;
    if (!help_property(ref, owner, help_write, object, item, tag, sizeof copy, payload, &status))
        return 0;
    if (status)
        return (payload == argument || help_release_content(ref, payload)) && help_result(call, status);
    if (had_content && !help_release_content(ref, previous))
        return 0;
    return (owner == help_menu_item || help_cleanup_handler(ref, owner, object)) &&
           help_property(ref, owner, help_mark, object, item, tag, 0, 0, &status) && help_result(call, status);
}

static int help_control_rect(const ROMlib_carbon_reference* ref, uint32_t control, uint32_t output)
{
    _Alignas(4) uint32_t port = 0;
    _Alignas(8) Rect bounds;
    uint32_t owner;
    if (!cfp_private(ref, 0xb12f0, (const uint32_t[]){US_TO_SYN68K(&port)}, 1, NULL) ||
        !cfp_private(ref, 0xb1bc0, &control, 1, &owner) || !cfp_private(ref, 0xb2e80, &owner, 1, NULL))
        return 0;
    const uint32_t rect = US_TO_SYN68K(&bounds);
    const int read = cfp_private(ref, 0xb1ba8, (const uint32_t[]){control, rect}, 2, NULL) &&
                     cfp_private(ref, 0xb1650, &rect, 1, NULL) &&
                     cfp_private(ref, 0xb1650, (const uint32_t[]){rect + 4}, 1, NULL);
    return cfp_private(ref, 0xb12d8, (const uint32_t[]){CL(port)}, 1, NULL) && read &&
           cfp_move(output, rect, sizeof bounds);
}

static int help_window_rect(const ROMlib_carbon_reference* ref, uint32_t window, uint32_t output)
{
    uint32_t region;
    if (!cfp_private(ref, 0xb1488, NULL, 0, &region) || !region)
        return 0;
    const int read = cfp_private(ref, 0xb2c40, (const uint32_t[]){window, 33, region}, 3, NULL) &&
                     cfp_private(ref, 0xb18f0, (const uint32_t[]){region, output}, 2, NULL);
    return cfp_private(ref, 0xb14d0, &region, 1, NULL) && read;
}

static int help_menu_title_rect(const ROMlib_carbon_reference* ref, uint32_t menu, uint32_t output, uint32_t* status)
{
    _Alignas(8) Rect bounds;
    const uint32_t rect = US_TO_SYN68K(&bounds);
    uint32_t id, height;
    if (!cfp_private(ref, 0xb2940, &menu, 1, &id) ||
        !cfp_private(ref, 0xb6cf0, (const uint32_t[]){id, rect}, 2, status))
        return 0;
    if (*status)
        return 1;
    return cfp_private(ref, 0xb6c48, NULL, 0, &height) &&
           cfp_private(ref, 0xb6cc0, (const uint32_t[]){rect, 0, (int16_t)(height - CW(bounds.top))}, 3, NULL) &&
           cfp_move(output, rect, sizeof bounds);
}

static int help_get_content(const ROMlib_carbon_reference* ref, enum help_owner owner, ROMlib_carbon_ppc_frame* call)
{
    const bool menu = owner == help_menu_title || owner == help_menu_item;
    const uint32_t source = call->words[0], provided = call->words[menu ? 2 : 3], output = call->words[menu ? 3 : 4];
    if (!provided)
        return help_result(call, paramErr);
    uint32_t object = source;
    uint16_t item = 0;
    if (owner == help_menu_item) {
        const Byte* data = source ? ROMlib_carbon_guest_memory(source, help_item_extent, 0) : NULL;
        if (!data)
            return help_result(call, paramErr);
        if (!cfp_get(source, &object))
            return 0;
        item = ((uint16_t)data[6] << 8) | data[7];
    }
    _Alignas(1024) Byte record[help_content_extent] = {0};
    const uint32_t buffer = US_TO_SYN68K(record);
    const uint32_t args[] = {object, menu ? item : buffer, buffer};
    const uint32_t backend = menu ? 0xb3198 : owner == help_control ? 0xb3180 : 0xb30f0;
    uint32_t status;
    if (!cfp_private(ref, backend, args, menu ? 3 : 2, &status))
        return 0;
    if (status)
        return help_result(call, noErr);
    uint16_t* supplied = ROMlib_carbon_guest_memory(provided, 2, 1);
    if (!supplied || !output || !cfp_move(output, buffer, sizeof record))
        return 0;
    Rect rect;
    memcpy(&rect, record + 4, sizeof rect);
    if (CW(rect.bottom) <= CW(rect.top) || CW(rect.right) <= CW(rect.left)) {
        const uint32_t bounds = output + 4;
        int ok;
        switch (owner) {
        case help_control:
            ok = help_control_rect(ref, object, bounds);
            break;
        case help_window:
            ok = help_window_rect(ref, object, bounds);
            break;
        case help_menu_title:
            ok = help_menu_title_rect(ref, object, bounds, &status);
            break;
        case help_menu_item:
            ok = cfp_move(bounds, source + 8, sizeof rect);
            break;
        default:
            return 0;
        }
        if (!ok)
            return 0;
    }
    if (!status)
        *supplied = 0;
    return help_result(call, status);
}

static unsigned help_system_menu_count(uint32_t bar)
{
    const unsigned header = offsetof(menulist, mulist), stride = sizeof(muelem);
    uint32_t extent;
    const Byte* data = cfp_handle_data(bar, &extent, 0);
    if (!data || extent < header)
        return 0;
    uint16_t value;
    memcpy(&value, data, 2);
    const unsigned visible_end = header + CW(value), hidden_start = visible_end + stride;
    if (visible_end > extent || (visible_end - header) % stride)
        return 0;
    unsigned hidden_end = hidden_start;
    if (hidden_start <= extent) {
        memcpy(&value, data + hidden_start - 2, 2);
        const unsigned end = header + CW(value);
        if (end >= hidden_start && end <= extent && (end - hidden_start) % stride == 0)
            hidden_end = end;
    }
    for (unsigned offset = header; offset < hidden_end && offset + stride <= extent; offset += stride) {
        if (offset == visible_end)
            continue;
        uint32_t encoded, menu_extent;
        memcpy(&encoded, data + offset, 4);
        const uint32_t menu = CL(encoded);
        const Byte* info = cfp_handle_data(menu, &menu_extent, 0);
        if (info && menu_extent >= sizeof value) {
            memcpy(&value, info, sizeof value);
            if ((int16_t)CW(value) == -16490) {
                const int count = ROMlib_menu_item_count(ROMlib_carbon_try_guest_memory(menu, 4, 0));
                return count < 0 ? 0 : count;
            }
        }
    }
    return 0;
}

static int help_menu(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t handle = call->words[0], first_item = call->words[1];
    if (handle) {
        const uint32_t slot = cfp_toc(ref, (uint32_t)-0x678);
        uint32_t cached, status;
        if (!slot || !cfp_get(slot, &cached))
            return 0;
        if (!cached) {
            if (!cfp_private(ref, 0xb6960, &slot, 1, &status))
                return 0;
            if (status)
                return help_result(call, status);
            if (!cfp_get(slot, &cached) || !cfp_private(ref, 0x5b8e4, &cached, 1, NULL) || !cfp_get(slot, &cached))
                return 0;
        }
        if (!cfp_write(handle, cached))
            return 0;
    }
    if (first_item) {
        uint16_t* output = ROMlib_carbon_guest_memory(first_item, 2, 1);
        uint32_t system_bar;
        if (!output || !cfp_get(0x286, &system_bar))
            return 0;
        *output = CW((uint16_t)(help_system_menu_count(system_bar) + 2));
    }
    return help_result(call, noErr);
}

static int help_set_menu_item(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t source = call->words[0], stored = cfp_toc(ref, (uint32_t)-0x65c);
    uint32_t item, previous;
    if (!source || !stored)
        return help_result(call, paramErr);
    if (!cfp_get(source + 4, &item) || !cfp_get(stored + 4, &previous))
        return 0;
    if ((uint16_t)item == (uint16_t)previous)
        return help_result(call, noErr);
    return cfp_private(ref, 0x6318c, NULL, 0, NULL) && cfp_move(stored, source, help_item_extent) &&
           cfp_private(ref, 0x648a8, NULL, 0, NULL) && help_result(call, noErr);
}

static int help_display(const ROMlib_carbon_reference* ref, bool set, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t address = cfp_toc(ref, (uint32_t)-0x664);
    Byte* enabled = address ? ROMlib_carbon_guest_memory(address, 1, set) : NULL;
    if (!enabled)
        return 0;
    if (!set)
        return help_result(call, *enabled);
    const Byte value = (uint8_t)call->words[0];
    uint32_t status = 0;
    if (value) {
        const uint32_t stored = cfp_toc(ref, (uint32_t)-0x65c);
        if (!stored || !cfp_clear(stored + 6, 2))
            return 0;
    }
    else if (!cfp_private(ref, 0x6318c, NULL, 0, &status) || !cfp_private(ref, 0x6484c, NULL, 0, NULL))
        return 0;
    *enabled = value;
    return help_result(call, status);
}

static uint64_t help_double_bits(const Byte* bytes)
{
    uint64_t value = 0;
    for (unsigned index = 0; index < 8; ++index)
        value = (value << 8) | bytes[index];
    return value;
}

static double help_double(const Byte* bytes)
{
    const uint64_t bits = help_double_bits(bytes);
    double value;
    memcpy(&value, &bits, 8);
    return value;
}

static double help_single(const Byte* bytes)
{
    uint32_t bits;
    float value;
    memcpy(&bits, bytes, 4);
    bits = CL(bits);
    memcpy(&value, &bits, 4);
    return value;
}

static int help_delay(const ROMlib_carbon_reference* ref, bool set, ROMlib_carbon_ppc_frame* call)
{
    if (!set && !call->words[0])
        return help_result(call, paramErr);
    const uint32_t address = cfp_toc(ref, (uint32_t)-0x660), table = cfp_toc(ref, (uint32_t)-0x134);
    Byte* delay = address ? ROMlib_carbon_guest_memory(address, 8, set) : NULL;
    const Byte* constants = table ? ROMlib_carbon_guest_memory(table, 0x30, 0) : NULL;
    if (!delay || !constants)
        return 0;
    if (set) {
        const int32_t ticks = (int32_t)call->words[0];
        const int32_t magnitude = ticks < 0 ? (int32_t)(0u - (uint32_t)ticks) : ticks;
        double value = magnitude * help_double(constants + (ticks < 0 ? 0x10 : 0x18));
        uint64_t bits;
        memcpy(&bits, &value, 8);
        for (unsigned index = 0; index < 8; ++index)
            delay[index] = (uint8_t)(bits >> (56 - 8 * index));
        return help_result(call, noErr);
    }
    const double stored = help_double(delay), ticks = stored * help_single(constants + 4);
    ROMlib_carbon_ppc_frame integer = *call;
    memcpy(&integer.floating[0], &ticks, 8);
    if (!cfx_private_frame(ref, 0xb0f24, &integer))
        return 0;
    const uint64_t integer_bits = UINT64_C(0x4330000000000000) | integer.words[0];
    double extended;
    memcpy(&extended, &integer_bits, 8);
    const double whole = extended - help_double(constants + 0x28);
    const double result = ticks - whole == whole ? ticks : -stored * help_single(constants);
    const uint32_t encoded =
        result >= INT32_MIN && result < (double)INT32_MAX + 1 ? (uint32_t)(int32_t)result : UINT32_C(0x80000000);
    return cfp_write(call->words[0], encoded) && help_result(call, noErr);
}

#define HELP_ENTRY(name, offset, body)                                                                       \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call)                                                  \
    {                                                                                                        \
        ROMlib_carbon_reference ref;                                                                         \
        if (!ROMlib_carbon_reference_function("CarbonLib", #name, &ref))                                     \
            return 0;                                                                                        \
        if (ref.code_offset != offset || ref.code_size != 0xb8fb4 || !ref.toc || ref.toc_offset != 0x2f78 || \
            ref.toc_size != 0x10f0c)                                                                         \
            return ROMlib_carbon_invoke_frame(ref.function, call);                                           \
        return body;                                                                                         \
    }

HELP_ENTRY(HMGetHelpMenu, 0x61e2c, help_menu(&ref, call))
HELP_ENTRY(HMSetControlHelpContent, 0x6214c, help_content_property(&ref, help_control, true, call))
HELP_ENTRY(HMGetControlHelpContent, 0x62354, help_content_property(&ref, help_control, false, call))
HELP_ENTRY(HMSetWindowHelpContent, 0x623ac, help_content_property(&ref, help_window, true, call))
HELP_ENTRY(HMGetWindowHelpContent, 0x625b4, help_content_property(&ref, help_window, false, call))
HELP_ENTRY(HMSetMenuItemHelpContent, 0x6260c, help_content_property(&ref, help_menu_item, true, call))
HELP_ENTRY(HMGetMenuItemHelpContent, 0x62794, help_content_property(&ref, help_menu_item, false, call))
HELP_ENTRY(HMInstallControlContentCallback, 0x627f0, help_callback(&ref, help_control, true, call))
HELP_ENTRY(HMInstallMenuItemContentCallback, 0x62870, help_callback(&ref, help_menu_item, true, call))
HELP_ENTRY(HMInstallMenuTitleContentCallback, 0x628f8, help_callback(&ref, help_menu_title, true, call))
HELP_ENTRY(HMInstallWindowContentCallback, 0x62980, help_callback(&ref, help_window, true, call))
HELP_ENTRY(HMGetControlContentCallback, 0x62a00, help_callback(&ref, help_control, false, call))
HELP_ENTRY(HMGetMenuItemContentCallback, 0x62a68, help_callback(&ref, help_menu_item, false, call))
HELP_ENTRY(HMGetMenuTitleContentCallback, 0x62ad4, help_callback(&ref, help_menu_title, false, call))
HELP_ENTRY(HMGetWindowContentCallback, 0x62b40, help_callback(&ref, help_window, false, call))
HELP_ENTRY(HMAreHelpTagsDisplayed, 0x62ba8, help_display(&ref, false, call))
HELP_ENTRY(HMSetHelpTagsDisplayed, 0x62bb4, help_display(&ref, true, call))
HELP_ENTRY(HMSetTagDelay, 0x62c18, help_delay(&ref, true, call))
HELP_ENTRY(HMGetTagDelay, 0x62c80, help_delay(&ref, false, call))
HELP_ENTRY(HMSetMenuItem, 0x62d9c, help_set_menu_item(&ref, call))
HELP_ENTRY(HMGetControlContent, 0x62e0c, help_get_content(&ref, help_control, call))
HELP_ENTRY(HMGetWindowContent, 0x62f0c, help_get_content(&ref, help_window, call))
HELP_ENTRY(HMGetMenuTitleContent, 0x62fdc, help_get_content(&ref, help_menu_title, call))
HELP_ENTRY(HMGetMenuItemContent, 0x630d8, help_get_content(&ref, help_menu_item, call))

#undef HELP_ENTRY
#endif
