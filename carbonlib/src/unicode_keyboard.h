/*+===================================================================
  File:      unicode_keyboard.h

  Summary:   Translate guest 'uchr' keyboard layouts and dead-key states.

  Classes:   None.

  Functions: Private UCKeyTranslate and UCKeyboardLayoutStringMax frame adapters.

  Origin:    Universal Interfaces 3.4.2 UnicodeUtilities.h and System 9.2.1 UnicodeUtilitiesCoreLib.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_UNICODE_KEYBOARD_H
#define ROMAW_CARBON_UNICODE_KEYBOARD_H

#include "cf_batch.h"

enum {
    uc_keyboard_header = 0x1002,
    uc_layout_features = 0x2001,
    uc_modifier_table = 0x3001,
    uc_character_tables = 0x4001,
    uc_state_records = 0x5001,
    uc_state_terminators = 0x6001,
    uc_sequence_table = 0x7001,
    uc_terminal_entries = 1,
    uc_range_entries = 2,
    uc_state_index = 0x4000,
    uc_sequence_index = 0x8000,
    uc_index_mask = 0x3fff,
    uc_empty_output = 0xfffe,
    uc_short_buffer = -25340,
    uc_action_down = 0,
    uc_action_up = 1,
    uc_action_autokey = 2,
    uc_action_display = 3,
    uc_no_dead_keys = 1,
};

typedef struct {
    uint32_t layout;
    uint32_t sequences;
    uint32_t terminators;
    uint32_t output;
    uint32_t capacity;
    uint32_t count;
} unicode_key_output;

static void* unicode_key_memory(uint32_t base, uint64_t offset, size_t size, int writing)
{
    if (!base || offset > UINT32_MAX - base)
        return NULL;
    return ROMlib_carbon_try_guest_memory((uint32_t)(base + offset), size, writing);
}

static uint16_t unicode_key_read16(const uint8_t* data)
{
    return (uint16_t)data[0] << 8 | data[1];
}

static uint32_t unicode_key_read32(const uint8_t* data)
{
    return (uint32_t)unicode_key_read16(data) << 16 | unicode_key_read16(data + 2);
}

static void unicode_key_write16(uint8_t* data, uint16_t value)
{
    data[0] = value >> 8;
    data[1] = value;
}

static int unicode_key_result(ROMlib_carbon_ppc_frame* call, int32_t status)
{
    call->words[0] = status;
    return 1;
}

static const uint8_t* unicode_key_type(uint32_t layout, uint32_t count, uint32_t device)
{
    const uint8_t* standard = unicode_key_memory(layout, 12, 28, 0);
    if (!standard)
        return NULL;
    for (uint32_t i = 1; i < count; ++i) {
        const uint8_t* entry = unicode_key_memory(layout, 12 + UINT64_C(28) * i, 28, 0);
        if (!entry)
            return NULL;
        if (device >= unicode_key_read32(entry) && device <= unicode_key_read32(entry + 4))
            return entry;
    }
    return standard;
}

static int unicode_key_read_code(uint32_t layout, const uint8_t* type, uint32_t modifiers, uint16_t key, uint16_t* code)
{
    const uint32_t modifier_offset = unicode_key_read32(type + 8);
    const uint8_t* modifier_table = unicode_key_memory(layout, modifier_offset, 8, 0);
    const uint32_t index_offset = unicode_key_read32(type + 12);
    const uint8_t* index_table = unicode_key_memory(layout, index_offset, 8, 0);
    if (!modifier_table || unicode_key_read16(modifier_table) != uc_modifier_table || !index_table ||
        unicode_key_read16(index_table) != uc_character_tables || key >= unicode_key_read16(index_table + 2))
        return 0;

    uint16_t table = unicode_key_read16(modifier_table + 2);
    if (modifiers < unicode_key_read32(modifier_table + 4)) {
        const uint8_t* selected = unicode_key_memory(layout, (uint64_t)modifier_offset + 8 + modifiers, 1, 0);
        if (!selected)
            return 0;
        table = *selected;
    }
    if (table >= unicode_key_read32(index_table + 4))
        return 0;
    const uint8_t* index = unicode_key_memory(layout, (uint64_t)index_offset + 8 + 4 * table, 4, 0);
    if (!index)
        return 0;
    const uint8_t* value = unicode_key_memory(layout, (uint64_t)unicode_key_read32(index) + 2 * key, 2, 0);
    if (!value)
        return 0;
    *code = unicode_key_read16(value);
    return 1;
}

static int unicode_key_append(unicode_key_output* result, uint16_t code)
{
    if (code >= uc_empty_output)
        return noErr;

    if ((code & 0xc000) == uc_sequence_index && result->sequences) {
        const uint8_t* table = unicode_key_memory(result->layout, result->sequences, 4, 0);
        if (!table || unicode_key_read16(table) != uc_sequence_table)
            return paramErr;
        const uint16_t index = code & uc_index_mask;
        if (index < unicode_key_read16(table + 2)) {
            const uint8_t* span = unicode_key_memory(result->layout, (uint64_t)result->sequences + 4 + 2 * index, 4, 0);
            if (!span)
                return paramErr;
            const unsigned start = unicode_key_read16(span), end = unicode_key_read16(span + 2);
            if (end < start || ((end - start) & 1))
                return paramErr;
            const unsigned units = (end - start) / 2;
            const unsigned copied = MIN(units, result->capacity - result->count);
            if (copied) {
                const uint8_t* input =
                    unicode_key_memory(result->layout, (uint64_t)result->sequences + start, 2 * copied, 0);
                uint8_t* output = unicode_key_memory(result->output, 2 * (uint64_t)result->count, 2 * copied, 1);
                if (!input || !output)
                    return paramErr;
                memmove(output, input, 2 * copied);
                result->count += copied;
            }
            return units == copied ? noErr : uc_short_buffer;
        }
    }
    if (result->count == result->capacity)
        return uc_short_buffer;
    uint8_t* output = unicode_key_memory(result->output, 2 * (uint64_t)result->count, 2, 1);
    if (!output)
        return paramErr;
    unicode_key_write16(output, code);
    ++result->count;
    return noErr;
}

static int unicode_key_terminate(unicode_key_output* result, uint16_t state)
{
    if (!result->terminators)
        return noErr;
    const uint8_t* table = unicode_key_memory(result->layout, result->terminators, 4, 0);
    if (!table || unicode_key_read16(table) != uc_state_terminators)
        return paramErr;
    if (!state || state > unicode_key_read16(table + 2))
        return noErr;
    const uint8_t* entry = unicode_key_memory(result->layout, (uint64_t)result->terminators + 2 + 2 * state, 2, 0);
    return entry ? unicode_key_append(result, unicode_key_read16(entry)) : paramErr;
}

static int unicode_key_state_match(uint32_t layout, uint32_t record, const uint8_t* header, uint16_t state,
                                   uint16_t* code, uint16_t* next, int* format)
{
    const unsigned count = unicode_key_read16(header + 4);
    *format = unicode_key_read16(header + 6);
    if (!count)
        return 0;
    if (*format != uc_terminal_entries && *format != uc_range_entries)
        return -1;
    const unsigned width = *format == uc_terminal_entries ? 4 : 8;
    for (unsigned i = 0; i < count; ++i) {
        const uint8_t* entry = unicode_key_memory(layout, (uint64_t)record + 8 + width * i, width, 0);
        if (!entry)
            return -1;
        const unsigned first = unicode_key_read16(entry);
        if (first > state)
            break;
        if (*format == uc_terminal_entries) {
            if (first == state) {
                *code = unicode_key_read16(entry + 2);
                *next = 0;
                return 1;
            }
        }
        else if (state <= first + entry[2]) {
            const unsigned delta = (state - first) * entry[3];
            *code = unicode_key_read16(entry + 4);
            *next = unicode_key_read16(entry + 6);
            if (*code < uc_empty_output)
                *code += delta;
            if (*next)
                *next += delta;
            return 1;
        }
    }
    return 0;
}

static int unicode_key_dead_key(unicode_key_output* result, uint32_t states, uint16_t index, unsigned action,
                                unsigned options, uint16_t current, uint8_t* state)
{
    const uint8_t* table = unicode_key_memory(result->layout, states, 4, 0);
    if (!states || !table || unicode_key_read16(table) != uc_state_records || index >= unicode_key_read16(table + 2))
        return paramErr;
    const uint8_t* offset = unicode_key_memory(result->layout, (uint64_t)states + 4 + 4 * index, 4, 0);
    if (!offset)
        return paramErr;
    const uint32_t record = unicode_key_read32(offset);
    const uint8_t* header = unicode_key_memory(result->layout, record, 8, 0);
    if (!header)
        return paramErr;

    const int transition = !(options & uc_no_dead_keys) && action != uc_action_up;
    const int display = action == uc_action_display;
    uint16_t code = unicode_key_read16(header), next = unicode_key_read16(header + 2);
    if (!current) {
        const int status = unicode_key_append(result, code);
        if (transition)
            unicode_key_write16(state + 2, next);
        if (!status && (!transition || display))
            return unicode_key_terminate(result, next);
        return status;
    }

    int format;
    const int match = unicode_key_state_match(result->layout, record, header, current, &code, &next, &format);
    if (match < 0)
        return paramErr;
    if (!match) {
        int status = display ? noErr : unicode_key_terminate(result, current);
        if (!status)
            status = unicode_key_append(result, code);
        unicode_key_write16(state + 2, transition ? next : 0);
        unicode_key_write16(state, 0);
        return status;
    }

    int status = unicode_key_append(result, code);
    unicode_key_write16(state + 2, transition ? next : 0);
    if (format == uc_range_entries && !status && (!transition || display))
        status = unicode_key_terminate(result, next);
    if (display && !unicode_key_read16(state + 2))
        unicode_key_write16(state, 0x8000);
    return status;
}

static int carbon_UCKeyboardLayoutStringMax(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t layout = call->words[0], output = call->words[1];
    const uint8_t* header = unicode_key_memory(layout, 0, 12, 0);
    if (!header || unicode_key_read16(header) != uc_keyboard_header || !unicode_key_read32(header + 8) ||
        !unicode_key_memory(output, 0, 4, 1))
        return unicode_key_result(call, paramErr);
    if (!cfp_write(output, 1))
        return 0;
    const uint32_t features = unicode_key_read32(header + 4);
    if (features) {
        const uint8_t* info = unicode_key_memory(layout, features, 8, 0);
        if (!info || unicode_key_read16(info) != uc_layout_features)
            return unicode_key_result(call, paramErr);
        return cfp_write(output, unicode_key_read32(info + 4)) && unicode_key_result(call, noErr);
    }

    const uint32_t count = unicode_key_read32(header + 8);
    uint32_t maximum = 1;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* type = unicode_key_memory(layout, 12 + UINT64_C(28) * i, 28, 0);
        if (!type)
            return unicode_key_result(call, paramErr);
        const uint32_t sequences = unicode_key_read32(type + 24);
        unsigned max_units = 1;
        if (sequences) {
            const uint8_t* table = unicode_key_memory(layout, sequences, 4, 0);
            if (!table || unicode_key_read16(table) != uc_sequence_table)
                return unicode_key_result(call, paramErr);
            const unsigned sequence_count = unicode_key_read16(table + 2);
            const uint8_t* offsets = unicode_key_memory(layout, (uint64_t)sequences + 4, 2 * (sequence_count + 1), 0);
            if (!offsets)
                return unicode_key_result(call, paramErr);
            for (unsigned j = 0; j < sequence_count; ++j) {
                const unsigned start = unicode_key_read16(offsets + 2 * j);
                const unsigned end = unicode_key_read16(offsets + 2 * j + 2);
                if (end < start || ((end - start) & 1))
                    return unicode_key_result(call, paramErr);
                max_units = MAX(max_units, (end - start) / 2);
            }
        }
        /* An unmatched dead key may emit its terminator and the new key's text. */
        if (unicode_key_read32(type + 16) && unicode_key_read32(type + 20))
            max_units *= 2;
        maximum = MAX(maximum, max_units);
    }
    return cfp_write(output, maximum) && unicode_key_result(call, noErr);
}

static int carbon_UCKeyTranslate(ROMlib_carbon_ppc_frame* call)
{
    uint32_t actual_address, output_address;
    if (!ROMlib_carbon_frame_word(call, 8, &actual_address) || !ROMlib_carbon_frame_word(call, 9, &output_address))
        return 0;
    const uint32_t layout = call->words[0];
    const uint16_t key = call->words[1], action = call->words[2];
    const uint8_t* header = unicode_key_memory(layout, 0, 12, 0);
    uint8_t* state = unicode_key_memory(call->words[6], 0, 4, 1);
    uint8_t* actual = unicode_key_memory(actual_address, 0, 4, 1);
    if (!header || unicode_key_read16(header) != uc_keyboard_header || !unicode_key_read32(header + 8) ||
        action > uc_action_display || !state || !actual || !output_address)
        return unicode_key_result(call, paramErr);

    memset(actual, 0, 4);
    const uint16_t current = unicode_key_read16(state + (action == uc_action_autokey ? 0 : 2));
    if (action != uc_action_autokey)
        unicode_key_write16(state, action == uc_action_down ? current : 0);

    const uint8_t* type = unicode_key_type(layout, unicode_key_read32(header + 8), call->words[4]);
    uint16_t code;
    if (!type || !unicode_key_read_code(layout, type, call->words[3], key, &code))
        return unicode_key_result(call, paramErr);

    unicode_key_output result = {
        layout, unicode_key_read32(type + 24), unicode_key_read32(type + 20), output_address, call->words[7], 0};
    int status = noErr;
    if ((code & 0xc000) == uc_state_index) {
        status = unicode_key_dead_key(&result, unicode_key_read32(type + 16), code & uc_index_mask, action,
                                      call->words[5], current, state);
    }
    else {
        if (current) {
            if (action != uc_action_display)
                status = unicode_key_terminate(&result, current);
            memset(state, 0, 4);
        }
        if (!status)
            status = unicode_key_append(&result, code);
    }
    const uint32_t encoded = CL(result.count);
    memcpy(actual, &encoded, sizeof encoded);
    return unicode_key_result(call, status);
}

#endif
