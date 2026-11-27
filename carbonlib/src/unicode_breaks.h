/*+===================================================================
  File:      unicode_breaks.h

  Summary:   Manage Unicode text-break locators and find UTF-16 character boundaries.

  Classes:   None.

  Functions: Private Unicode text-break frame adapters.

  Origin:    CarbonLib 1.4, its UnicodeUtilitiesLib.CL companion, and Universal Interfaces
             3.4.2 UnicodeUtilities.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_UNICODE_BREAKS_H
#define ROMAW_CARBON_UNICODE_BREAKS_H

#include "locale_access.h"
#include "text_conversion.h"
#include "toolbox_vectors.h"

enum {
    text_break_character = 1,
    text_break_supported = 0x55,
    text_break_leading = 1,
    text_break_backward = 2,
    text_break_header_size = 12,
    text_break_entry_size = 20,
    text_break_rule_header = 8,
    text_break_rule_entry = 12,
};

#define TEXT_BREAK_RESOURCE TICK("ubrk")

static Byte* text_break_locator_data(uint32_t handle, uint32_t* extent, unsigned* count, int writing)
{
    Byte* bytes = cfp_handle_data(handle, extent, writing);
    if (!bytes || *extent < text_break_header_size || locale_word(bytes) != TEXT_BREAK_RESOURCE)
        return NULL;
    *count = locale_word(bytes + 8);
    return *count <= (*extent - text_break_header_size) / text_break_entry_size ? bytes : NULL;
}

static void text_break_release(uint32_t stack, uint32_t handle)
{
    uint32_t extent;
    unsigned count;
    const Byte* locator = text_break_locator_data(handle, &extent, &count, 0);
    if (!locator)
        return;
    _Alignas(16) uint32_t properties = CL(locale_word(locator + 4));
    if (properties) {
        ROMlib_carbon_ppc_frame unload = {.words = {(uint32_t)-3, US_TO_SYN68K(&properties)}, .stack = stack};
        carbon_text_UCUnloadCharPropsTablePriv(&unload);
    }

    for (unsigned i = 0; i < count; ++i) {
        locator = text_break_locator_data(handle, &extent, &count, 0);
        if (!locator || i >= count)
            break;
        const Byte* row = locator + text_break_header_size + text_break_entry_size * i;
        locale_operation_resource classes, transitions;
        classes.data = CL(locale_word(row + 4));
        classes.token = CL(locale_word(row + 8));
        transitions.data = CL(locale_word(row + 12));
        transitions.token = CL(locale_word(row + 16));
        locale_release_operation_resource(stack, classes);
        locale_release_operation_resource(stack, transitions);
    }
    ROMlib_carbon_ppc_frame dispose = {.words = {handle}};
    carbon_vector_DisposeHandle(&dispose);
}

static int text_break_load_rules(uint32_t stack, uint32_t locator_handle, uint32_t locale, uint32_t variant,
                                 uint32_t* remaining, int optional)
{
    _Alignas(16) locale_operation_resource directory = {0};
    int status = locale_acquire_operation_resource(stack, TEXT_BREAK_RESOURCE, locale, variant, &directory);
    if (status)
        return optional ? noErr : status;

    uint32_t bytes;
    const Byte* table = cfp_handle_data(CL(directory.data), &bytes, 0);
    if (!table || bytes < text_break_rule_header) {
        locale_release_operation_resource(stack, directory);
        return paramErr;
    }
    const unsigned count = locale_word(table + 4);
    if (count > (bytes - text_break_rule_header) / text_break_rule_entry) {
        locale_release_operation_resource(stack, directory);
        return paramErr;
    }

    for (unsigned i = 0; i < count && *remaining && !status; ++i) {
        table = cfp_handle_data(CL(directory.data), &bytes, 0);
        if (!table || bytes < text_break_rule_header + text_break_rule_entry * (i + 1)) {
            status = paramErr;
            break;
        }
        const Byte* rule = table + text_break_rule_header + text_break_rule_entry * i;
        const uint32_t kind = locale_word(rule);
        if (!(kind & *remaining))
            continue;
        const uint32_t classes_variant = locale_word(rule + 4), transitions_variant = locale_word(rule + 8);
        _Alignas(16) locale_operation_resource classes = {0}, transitions = {0};
        status = locale_acquire_operation_resource(stack, TICK("ubrc"), locale, classes_variant, &classes);
        if (!status)
            status = locale_acquire_operation_resource(stack, TICK("ubrs"), locale, transitions_variant, &transitions);
        if (status) {
            locale_release_operation_resource(stack, classes);
            break;
        }

        uint32_t extent, class_bytes;
        unsigned loaded;
        Byte* locator = text_break_locator_data(locator_handle, &extent, &loaded, 1);
        const Byte* classes_table = cfp_handle_data(CL(classes.data), &class_bytes, 0);
        if (!locator || loaded >= (extent - text_break_header_size) / text_break_entry_size || !classes_table ||
            class_bytes < 16) {
            locale_release_operation_resource(stack, classes);
            locale_release_operation_resource(stack, transitions);
            status = paramErr;
            break;
        }
        const int load_properties = !locale_word(locator + 4) && locale_word(classes_table + 12);
        Byte* row = locator + text_break_header_size + text_break_entry_size * loaded;
        locale_store_word(row, kind);
        locale_store_word(row + 4, CL(classes.data));
        locale_store_word(row + 8, CL(classes.token));
        locale_store_word(row + 12, CL(transitions.data));
        locale_store_word(row + 16, CL(transitions.token));
        locale_store_word(locator + 8, loaded + 1);
        *remaining &= ~kind;

        if (load_properties) {
            _Alignas(16) uint32_t properties = 0;
            ROMlib_carbon_ppc_frame call = {.words = {(uint32_t)-3, US_TO_SYN68K(&properties)}, .stack = stack};
            if (carbon_text_UCLoadCharPropsTablePriv(&call) && !call.words[0]) {
                locator = text_break_locator_data(locator_handle, &extent, &loaded, 1);
                if (locator)
                    locale_store_word(locator + 4, CL(properties));
                else {
                    call =
                        (ROMlib_carbon_ppc_frame){.words = {(uint32_t)-3, US_TO_SYN68K(&properties)}, .stack = stack};
                    carbon_text_UCUnloadCharPropsTablePriv(&call);
                    status = paramErr;
                }
            }
        }
    }
    locale_release_operation_resource(stack, directory);
    return status;
}

static int carbon_UCCreateTextBreakLocator(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t locale = call->words[0], variant = call->words[1];
    const uint32_t types = call->words[2], output = call->words[3];
    if (!locale_output(output, 4))
        return locale_status(call, paramErr);
    if (!cfp_write(output, 0))
        return 0;
    if (types == text_break_character)
        return locale_status(call, noErr);
    if (types & ~text_break_supported)
        return locale_status(call, paramErr);

    _Alignas(16) char root_name[16] = "";
    _Alignas(16) uint32_t root_word = 0;
    ROMlib_carbon_ppc_frame root = {.words = {US_TO_SYN68K(root_name), US_TO_SYN68K(&root_word)}, .stack = call->stack};
    if (!carbon_LocaleRefFromLocaleString(&root))
        return 0;
    if (root.words[0])
        return locale_status(call, root.words[0]);
    const uint32_t root_locale = CL(root_word);

    uint32_t remaining = types & ~text_break_character;
    unsigned rows = 0;
    for (uint32_t bits = remaining; bits; bits &= bits - 1)
        ++rows;
    ROMlib_carbon_ppc_frame allocation = {.words = {text_break_header_size + text_break_entry_size * rows}};
    if (!carbon_vector_NewHandleClear(&allocation))
        return 0;
    const uint32_t handle = allocation.words[0];
    if (!handle)
        return locale_status(call, CW(MemErr));

    uint32_t extent;
    Byte* locator = cfp_handle_data(handle, &extent, 1);
    if (!locator || extent < text_break_header_size) {
        ROMlib_carbon_ppc_frame dispose = {.words = {handle}};
        carbon_vector_DisposeHandle(&dispose);
        return locale_status(call, memFullErr);
    }
    locale_store_word(locator, TEXT_BREAK_RESOURCE);
    int status =
        locale == root_locale ? noErr : text_break_load_rules(call->stack, handle, locale, variant, &remaining, 1);
    if (remaining && !status)
        status = text_break_load_rules(call->stack, handle, root_locale, variant, &remaining, 0);
    if (remaining && !status)
        status = paramErr;
    if (!status && cfp_write(output, handle))
        return locale_status(call, noErr);
    text_break_release(call->stack, handle);
    return status ? locale_status(call, status) : 0;
}

static int text_break_reference(const char* name, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference reference;
    if (ROMlib_carbon_reference_function("CarbonLib", name, &reference) ||
        ROMlib_carbon_load_reference("UnicodeUtilitiesLib.CL", name, &reference))
        return ROMlib_carbon_invoke_frame(reference.function, call);
    return 0;
}

static int carbon_UCDisposeTextBreakLocator(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t output = call->words[0];
    if (!locale_output(output, 4))
        return locale_status(call, paramErr);
    uint32_t handle, extent;
    unsigned count;
    if (!cfp_get(output, &handle))
        return 0;
    if (!handle)
        return locale_status(call, noErr);
    if (!text_break_locator_data(handle, &extent, &count, 0))
        return text_break_reference("UCDisposeTextBreakLocator", call);
    text_break_release(call->stack, handle);
    return cfp_write(output, 0) && locale_status(call, noErr);
}

static int carbon_UCGetTextBreakLocatorBreakTypes(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t handle = call->words[0], output = call->words[1];
    if (!locale_output(output, 4))
        return locale_status(call, paramErr);
    uint32_t types = text_break_character;
    if (handle) {
        uint32_t extent;
        unsigned count;
        const Byte* locator = text_break_locator_data(handle, &extent, &count, 0);
        if (!locator)
            return text_break_reference("UCGetTextBreakLocatorBreakTypes", call);
        for (unsigned i = 0; i < count; ++i)
            types |= locale_word(locator + text_break_header_size + text_break_entry_size * i);
    }
    return cfp_write(output, types) && locale_status(call, noErr);
}

static int text_break_inside_scalar(const Byte* text, uint32_t count, uint32_t position)
{
    if (!position || position >= count)
        return 0;
    const uint16_t before = locale_half(text + (size_t)(position - 1) * 2);
    const uint16_t after = locale_half(text + (size_t)position * 2);
    return (before & 0xfc00) == 0xd800 && (after & 0xfc00) == 0xdc00;
}

typedef struct {
    const Byte* classes;
    const Byte* properties;
    const Byte* transitions;
    uint32_t classes_size;
    uint32_t properties_size;
    uint32_t transitions_size;
    uint32_t columns;
    uint32_t lookaround;
    uint32_t property_class;
} text_break_rules;

static int text_break_rule_table(const Byte* row, int backward, uint32_t properties, text_break_rules* rules)
{
    uint32_t state_size;
    const Byte* states = cfp_handle_data(locale_word(row + 12), &state_size, 0);
    rules->classes = cfp_handle_data(locale_word(row + 4), &rules->classes_size, 0);
    if (!states || state_size < 16 || !rules->classes || rules->classes_size < 0x410)
        return 0;
    rules->property_class = locale_word(rules->classes + 12);
    rules->properties =
        properties && rules->property_class ? cfp_handle_data(properties, &rules->properties_size, 0) : NULL;
    if (properties && rules->property_class && (!rules->properties || rules->properties_size < 0x410))
        return 0;
    rules->lookaround = locale_half(states + 4);
    rules->columns = locale_half(states + 6);
    const uint32_t forward = locale_word(states + 8), reverse = locale_word(states + 12);
    if (!rules->columns || forward < 16 || forward >= reverse || reverse >= state_size)
        return 0;
    rules->transitions = states + (backward ? reverse : forward);
    rules->transitions_size = backward ? state_size - reverse : reverse - forward;
    return !(rules->transitions_size % (2 * rules->columns));
}

static int text_break_property(const Byte* table, uint32_t size, uint16_t unit, unsigned* value)
{
    const uint16_t page = locale_half(table + 0x10 + (unit >> 7) * 2);
    if (!(page & 0x8000)) {
        *value = page;
        return 1;
    }
    const unsigned index = (page & 0x7fff) + (unit & 0x7f);
    const unsigned width = locale_word(table) == 1 ? 1 : 2;
    const unsigned offset = 0x410 + index * width;
    if (offset > size || width > size - offset)
        return 0;
    *value = width == 1 ? table[offset] : locale_half(table + offset);
    return 1;
}

static int text_break_class(const text_break_rules* rules, const Byte* text, uint32_t position, int backward,
                            unsigned* following, unsigned* last_script, unsigned* value)
{
    const uint16_t unit = locale_half(text + (size_t)position * 2);
    unsigned kind;
    if (!text_break_property(rules->classes, rules->classes_size, unit, &kind))
        return 0;
    if (backward) {
        /* The saved reverse scan looks back four units for a pending sequence. */
        for (unsigned i = 1; i <= 4 && i <= position; ++i) {
            unsigned previous;
            const uint16_t previous_unit = locale_half(text + (size_t)(position - i) * 2);
            if (!text_break_property(rules->classes, rules->classes_size, previous_unit, &previous))
                return 0;
            if (previous >= 0x50 + i) {
                if (!text_break_property(rules->classes, rules->classes_size, 0x300, &kind))
                    return 0;
                break;
            }
        }
        if (kind >= 0x50)
            kind = rules->columns - 1;
    }
    else if (*following) {
        --*following;
        if (!text_break_property(rules->classes, rules->classes_size, 0x300, &kind))
            return 0;
    }
    else if (kind >= 0x50) {
        *following = kind - 0x50;
        kind = rules->columns - 1;
    }

    unsigned script = 0;
    if (kind == rules->property_class && rules->properties) {
        if (!text_break_property(rules->properties, rules->properties_size, unit, &script))
            return 0;
        if (script < 8)
            script = 0;
        else if (*last_script && script != *last_script)
            kind = 0;
    }
    *last_script = script;
    *value = kind;
    return kind < rules->columns;
}

static void text_break_action(unsigned action, uint32_t position, uint32_t count, int backward, uint32_t* boundary,
                              uint32_t* marker)
{
    const uint32_t edge = position + !backward;
    const int at_end = backward ? !position : position + 1 == count;
    switch (action) {
    case 1:
        *boundary = edge;
        *marker = edge;
        break;
    case 2:
        if (at_end)
            *boundary = edge;
        break;
    case 3:
        *boundary = position + backward;
        *marker = *boundary;
        break;
    case 4:
        *boundary = at_end ? edge : *marker;
        if (!at_end)
            *marker = edge;
        break;
    case 5:
        *boundary = *marker;
        break;
    }
}

static int text_break_scan(const text_break_rules* rules, const Byte* text, uint32_t count, unsigned options,
                           uint32_t start, uint32_t* boundary)
{
    const int backward = (options & text_break_backward) != 0;
    const uint32_t index = start - !(options & text_break_leading);
    const uint32_t target = index + !backward;
    uint32_t position =
        backward ? index + MIN(count - 1 - index, rules->lookaround) : index - MIN(index, rules->lookaround);
    uint32_t marker = position + !backward, scan_start = position;
    unsigned state = 0, following = 0, script = 0;
    for (;;) {
        unsigned kind;
        if (!text_break_class(rules, text, position, backward, &following, &script, &kind))
            return 0;
        const uint32_t transition = 2 * (state * rules->columns + kind);
        if (transition + 2 > rules->transitions_size)
            return 0;
        text_break_action(rules->transitions[transition], position, count, backward, boundary, &marker);
        state = rules->transitions[transition + 1];
        if (state && (backward ? position != 0 : position + 1 < count)) {
            if (backward)
                --position;
            else
                ++position;
            continue;
        }
        if (backward ? *boundary <= target : *boundary >= target)
            return *boundary <= count;

        position = backward ? *boundary - 1 : *boundary;
        if (position >= count || (backward ? position >= scan_start : position <= scan_start))
            return 0;
        scan_start = position;
        state = following = 0;
    }
}

static int text_break_match_rules(ROMlib_carbon_ppc_frame* call, const Byte* text, uint32_t* boundary)
{
    const uint32_t handle = call->words[0], kind = call->words[1], options = call->words[2];
    if (!handle)
        return locale_status(call, paramErr);
    uint32_t extent;
    unsigned count;
    const Byte* locator = text_break_locator_data(handle, &extent, &count, 0);
    if (!locator)
        return text_break_reference("UCFindTextBreak", call);
    for (unsigned i = 0; i < count; ++i) {
        const Byte* row = locator + text_break_header_size + text_break_entry_size * i;
        if (locale_word(row) != kind)
            continue;
        text_break_rules rules;
        if (!text_break_rule_table(row, options & text_break_backward, locale_word(locator + 4), &rules) ||
            !text_break_scan(&rules, text, call->words[4], options, call->words[5], boundary))
            return text_break_reference("UCFindTextBreak", call);
        return cfp_write(call->words[6], *boundary) && locale_status(call, noErr);
    }
    return locale_status(call, -25341);
}

static int carbon_UCFindTextBreak(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t kind = call->words[1], options = call->words[2], text = call->words[3];
    const uint32_t count = call->words[4], start = call->words[5], output = call->words[6];
    if (!locale_output(output, 4))
        return locale_status(call, paramErr);
    if (!cfp_write(output, start))
        return 0;
    const uint64_t extent = (uint64_t)count * 2;
    const int leading = (options & text_break_leading) != 0;
    if (!text || !count || start > count || extent > UINT64_C(0x100000000) - text || (!start && !leading) ||
        (start == count && leading))
        return locale_status(call, paramErr);
    const Byte* characters = ROMlib_carbon_try_guest_memory(text, extent, 0);
    if (!characters)
        return locale_status(call, paramErr);

    uint32_t boundary = start;
    if (kind != text_break_character)
        return text_break_match_rules(call, characters, &boundary);

    if (options & text_break_backward) {
        if (!leading)
            --boundary;
        if (text_break_inside_scalar(characters, count, boundary))
            --boundary;
    }
    else {
        if (leading)
            ++boundary;
        if (text_break_inside_scalar(characters, count, boundary))
            ++boundary;
    }
    return cfp_write(output, boundary) && locale_status(call, noErr);
}

#endif
