/*+===================================================================
  File:      text_conversion.c

  Summary:   Implements Text Encoding Converter records and text-to-Unicode dispatch.

  Classes:   None.

  Functions: ROMlib_carbon_text_conversion.

  Origin:    TextCommon and UnicodeConverter from the Mac OS 9.2.1 Text Encoding Converter suite.
             Universal Interfaces 3.4.2 TextCommon.h and UnicodeConverter.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "text_conversion.h"
#include "cf_batch.h"

enum {
    converter_text = 0x7a3,
    converter_unicode = 0x7a2,
    converter_run = 0x7a4,
    mapping_unavailable = -8770,
    conversion_buffer_too_small = -8750,
    mapping_list_too_small = -8751,
    conversion_unrecognized_table = -8747,
    conversion_output_full = -8785,
    conversion_unmappable_character = -8740,
    conversion_unmappable_sequence = -8754,
    conversion_used_fallback = -8783,
    mapping_not_found = -8738,
    converter_text_bom = 0x1b,
};

static int text_result(ROMlib_carbon_ppc_frame* call, uint32_t value)
{
    call->words[0] = value;
    return 1;
}

#include "text_common.h"

static int handle_record(uint32_t handle, uint32_t* record)
{
    *record = 0;
    return !handle || cfp_get(handle, record);
}

static int read_byte(uint32_t address, uint8_t* value)
{
    const Byte* byte = ROMlib_carbon_guest_memory(address, 1, 0);
    if (!byte)
        return 0;
    *value = *byte;
    return 1;
}

static int reset_converter(const ROMlib_carbon_reference* ref, int operation, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t handle = call->words[0];
    uint32_t record;
    if (!handle_record(handle, &record))
        return 0;
    if (!record)
        return text_result(call, paramErr);
    if (operation == text_reset_text)
        return cfp_clear(record + 0x18, 3) && text_result(call, noErr);
    if (operation == text_reset_run) {
        uint32_t status;
        if (!cfp_private(ref, 0x88fc, &handle, 1, &status))
            return 0;
        return (status || cfp_write(record + 0x10c, 0)) && text_result(call, status);
    }
    return cfp_write(record + 0x54, 0) && cfp_write(record + 0xac, 1) && cfp_clear(record + 0xb0, 2) &&
           cfp_write(record + 0xfc, 0) && cfp_write(record + 0x100, 0) && cfp_write(record + 0x104, 1) &&
           text_result(call, noErr);
}

static int dispose_converter(const ROMlib_carbon_reference* ref, int unicode_to_text, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t output = call->words[0];
    uint32_t handle, record, tag;
    if (!output)
        return text_result(call, paramErr);
    if (!cfp_get(output, &handle) || !handle_record(handle, &record))
        return 0;
    if (!record)
        return text_result(call, paramErr);
    if (!cfp_get(record, &tag))
        return 0;
    if (tag != (unicode_to_text ? converter_unicode : converter_text))
        return text_result(call, paramErr);

    uint8_t direct_mapping;
    if (!read_byte(record + (unicode_to_text ? 0x10a : 0x1c), &direct_mapping))
        return 0;
    if (!direct_mapping) {
        uint32_t tables, base, index;
        if (!cfp_private(ref, 0x89ec, NULL, 0, &tables))
            return 0;
        if (tables) {
            if (!cfp_get(tables, &base) || !cfp_get(record + (unicode_to_text ? 0x10c : 4), &index) ||
                base > UINT32_MAX - 16 || index > (UINT32_MAX - base - 16) / 32)
                return 0;
            const uint32_t table = base + 16 + index * 32;
            if (!cfp_private(ref, unicode_to_text ? 0x6f8 : 0x6a0, &table, 1, NULL))
                return 0;
        }
    }
    return cfp_private(ref, 0x89a4, &handle, 1, NULL) && cfp_write(output, 0) && text_result(call, noErr);
}

static int dispose_run(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t output = call->words[0];
    uint32_t handle, record, tag, tables;
    if (!output)
        return text_result(call, paramErr);
    if (!cfp_get(output, &handle) || !handle_record(handle, &record))
        return 0;
    if (!record)
        return text_result(call, paramErr);
    if (!cfp_get(record, &tag))
        return 0;
    if (tag != converter_run)
        return text_result(call, paramErr);
    if (!cfp_private(ref, 0x89ec, NULL, 0, &tables))
        return 0;
    if (tables) {
        uint32_t count;
        if (record > UINT32_MAX - 0x114u || !cfp_get(record + 0x110, &count) ||
            count > (UINT32_MAX - 0x114u - record) / 40)
            return 0;
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t base, index;
            if (!cfp_get(handle, &record) || !cfp_get(tables, &base) || !cfp_get(record + 0x114 + i * 40, &index) ||
                base > UINT32_MAX - 16 || index > (UINT32_MAX - base - 16) / 32)
                return 0;
            const uint32_t entry = base + 16 + index * 32;
            if (!cfp_private(ref, 0x7700, &entry, 1, NULL))
                return 0;
        }
    }
    return cfp_private(ref, 0x89a4, &handle, 1, NULL) && cfp_write(output, 0) && text_result(call, noErr);
}

static int converter_tables(const ROMlib_carbon_reference* ref, uint32_t* globals_handle, uint32_t* tables)
{
    uint32_t globals;
    *tables = 0;
    return cfp_private(ref, 0x892c, NULL, 0, globals_handle) &&
           (!*globals_handle || (cfp_get(*globals_handle, &globals) && cfp_get(globals + 4, tables)));
}

static int resolve_mapping(const ROMlib_carbon_reference* ref, const uint32_t* input, uint32_t mapping[3])
{
    for (unsigned i = 0; i < 2; ++i) {
        const uint32_t encoding = CL(input[i]);
        uint32_t resolved;
        if (!cfp_private(ref, 0x8a04, &encoding, 1, &resolved))
            return 0;
        mapping[i] = CL(resolved);
    }
    mapping[2] = input[2];
    return 1;
}

static int replace_mapping(const ROMlib_carbon_reference* ref, int reverse, uint32_t tables, uint32_t handle,
                           uint32_t* mapping, int changing, uint32_t* status)
{
    const uint32_t format_offset = reverse ? 0x109 : converter_text_bom;
    const uint32_t cached_offset = reverse ? 0x10a : 0x1c;
    const uint32_t index_offset = reverse ? 0x10c : 4;
    uint32_t record;
    if (!cfp_get(handle, &record) || !record)
        return 0;
    Byte* data = ROMlib_carbon_guest_memory(record, reverse ? 0x128 : 0x28, 1);
    if (!data)
        return 0;
    const int utf8 = ((CL(mapping[0]) >> 26) & 0x0f) == 2;
    if (utf8 && (!changing || !data[format_offset]))
        mapping[0] = CL(CL(mapping[0]) & 0x03ffffffu);
    data[format_offset] = utf8;
    if (!changing && !cfp_write(record, reverse ? converter_unicode : converter_text))
        return 0;

    const uint32_t profile = US_TO_SYN68K(mapping);
    uint32_t entry, base, mapping_flags, attributes;
    if (!cfp_private(ref, 0x2d4, &profile, 1, &entry))
        return 0;
    if (!entry) {
        *status = -8738;
        return 1;
    }
    if (!cfp_get(tables, &base) || base > UINT32_MAX - 16 || entry < base + 16 || !cfp_get(entry + 8, &mapping_flags) ||
        !cfp_get(entry + 12, &attributes))
        return 0;

    _Alignas(16) uint32_t outputs[4] = {0};
    const uint32_t slots = US_TO_SYN68K(outputs);
    if (reverse) {
        const uint32_t load_args[] = {profile, entry, slots, slots + 4, slots + 8, slots + 12, 0};
        if (!cfp_private(ref, 0x500, load_args, 7, status))
            return 0;
    }
    else {
        const uint32_t load_args[] = {entry, slots, slots + 4, 0};
        if (!cfp_private(ref, 0x47c, load_args, 4, status))
            return 0;
    }
    if (*status)
        return 1;

    if (!cfp_get(handle, &record) || !(data = ROMlib_carbon_guest_memory(record, reverse ? 0x128 : 0x28, 1)))
        return 0;
    if (changing && !data[cached_offset]) {
        uint32_t previous;
        if (!cfp_get(record + index_offset, &previous) || previous > (UINT32_MAX - base - 16) / 32)
            return 0;
        const uint32_t old_entry = base + 16 + 32 * previous;
        if (!cfp_private(ref, reverse ? 0x6f8 : 0x6a0, &old_entry, 1, NULL) || !cfp_get(handle, &record) ||
            !(data = ROMlib_carbon_guest_memory(record, reverse ? 0x128 : 0x28, 1)))
            return 0;
    }

    data[cached_offset] = 0;
    mapping[2] = CL(mapping_flags);
    memcpy(data + (reverse ? 0x118 : 8), mapping, 12);
    if (!cfp_write(record + index_offset, (entry - base - 16) / 32) ||
        !cfp_write(record + (reverse ? 0x124 : 0x14), attributes))
        return 0;
    if (!reverse)
        return cfp_write(record + 0x20, CL(outputs[0])) && cfp_write(record + 0x24, CL(outputs[1])) &&
               cfp_private(ref, 0x8914, &handle, 1, NULL);

    uint32_t header, descriptor;
    if (!cfp_get(entry + 16, &header) || !cfp_write(record + 0x110, header >> 16) ||
        !cfp_write(record + 0xf4, CL(outputs[0])) || !cfp_write(record + 0xf8, CL(outputs[1])) ||
        !cfp_write(record + 0xa8, CL(outputs[2])) || !cfp_write(record + 0x114, CL(outputs[3])) ||
        !cfp_get(CL(outputs[3]), &descriptor) || !cfp_get(descriptor + 0x14, &header))
        return 0;
    data[0x108] = header & 1;
    return changing || cfp_private(ref, 0x88fc, &handle, 1, NULL);
}

static int install_mapping(const ROMlib_carbon_reference* ref, int reverse, uint32_t tables, uint32_t handle,
                           uint32_t* mapping, int changing, uint32_t* status)
{
    uint32_t previous;
    if (!cfp_private(ref, 0x8974, &tables, 1, &previous))
        return 0;
    const int installed = replace_mapping(ref, reverse, tables, handle, mapping, changing, status);
    const uint32_t unlock[] = {tables, previous};
    return cfp_private(ref, 0x898c, unlock, 2, NULL) && installed;
}

static int change_converter(const ROMlib_carbon_reference* ref, int reverse, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t handle = call->words[0], address = call->words[1];
    uint32_t record, tag, globals, tables, status = noErr;
    if (!handle_record(handle, &record))
        return 0;
    if (!record || !address)
        return text_result(call, paramErr);
    if (!cfp_get(record, &tag))
        return 0;
    if (tag != (reverse ? converter_unicode : converter_text))
        return text_result(call, paramErr);
    if (!converter_tables(ref, &globals, &tables))
        return 0;
    if (!tables)
        return text_result(call, mapping_unavailable);
    const void* input = ROMlib_carbon_guest_memory(address, 12, 0);
    _Alignas(16) uint32_t original[3], mapping[3];
    if (!input)
        return 0;
    memcpy(original, input, sizeof original);
    return resolve_mapping(ref, original, mapping) &&
           install_mapping(ref, reverse, tables, handle, mapping, 1, &status) && text_result(call, status);
}

static int create_converter(const ROMlib_carbon_reference* ref, int reverse, const uint32_t* input, uint32_t output,
                            uint32_t* status)
{
    *status = paramErr;
    if (!input || !output)
        return 1;
    if (!cfp_write(output, 0))
        return 0;
    uint32_t globals_handle, globals, tables;
    if (!converter_tables(ref, &globals_handle, &tables))
        return 0;
    if (!tables) {
        *status = mapping_unavailable;
        return 1;
    }

    _Alignas(16) uint32_t mapping[3];
    if (!resolve_mapping(ref, input, mapping))
        return 0;
    const uint32_t bytes = reverse ? 0x128 : 0x28;
    uint32_t handle;
    if (!cfp_private(ref, 0x8944, &bytes, 1, &handle))
        return 0;
    if (!handle)
        return cfp_private(ref, 0x895c, NULL, 0, status);

    uint32_t cached_handle, cached, record;
    const uint32_t flag = reverse ? 0x10a : 0x1c, mapping_offset = reverse ? 0x118 : 8;
    *status = noErr;
    int ok = cfp_get(globals_handle, &globals) && cfp_get(globals + (reverse ? 0x18 : 0x14), &cached_handle) &&
             handle_record(cached_handle, &cached);
    int reused = 0;
    if (ok && cached) {
        const uint32_t compare[] = {US_TO_SYN68K(mapping), cached + mapping_offset};
        uint32_t same;
        ok = cfp_private(ref, 0x288, compare, 2, &same);
        if (ok && same) {
            ok = cfp_get(handle, &record) && cfp_move(record, cached, bytes);
            Byte* cached_flag = ok ? ROMlib_carbon_guest_memory(record + flag, 1, 1) : NULL;
            if (cached_flag)
                *cached_flag = 1;
            reused = 1;
            ok = cached_flag != NULL;
        }
    }
    if (ok && !reused)
        ok = install_mapping(ref, reverse, tables, handle, mapping, 0, status);
    if (ok && !*status) {
        ok = cfp_write(output, handle);
        if (ok)
            return 1;
    }
    return cfp_private(ref, 0x89a4, &handle, 1, NULL) && ok;
}

static int run_mapping(const ROMlib_carbon_reference* ref, int operation, uint32_t input, uint32_t mapping[3],
                       uint32_t* status)
{
    _Alignas(16) uint32_t requested[3] = {CL(0x100), 0, CL(UINT32_MAX)};
    if (operation == text_create_run) {
        const void* source = ROMlib_carbon_guest_memory(input, sizeof requested, 0);
        if (!source)
            return 0;
        memcpy(requested, source, sizeof requested);
    }
    else if (operation == text_create_run_by_script) {
        uint16_t encoded;
        const void* source = ROMlib_carbon_guest_memory(input, sizeof encoded, 0);
        if (!source)
            return 0;
        memcpy(&encoded, source, sizeof encoded);
        uint32_t script = (int32)(int16)CW(encoded);
        uint32_t args[] = {script, (uint32_t)-128, (uint32_t)-128, 0, US_TO_SYN68K(&requested[1])};
        if (!cfp_private(ref, 0x89bc, args, NELEM(args), status))
            return 0;
        if (*status) {
            if (script & 0xff00) {
                *status = mapping_not_found;
                return 1;
            }
            requested[1] = CL(script);
            *status = noErr;
        }
    }
    else {
        uint32_t encoding;
        if (!cfp_get(input, &encoding))
            return 0;
        requested[1] = CL(encoding);
    }
    return resolve_mapping(ref, requested, mapping);
}

static int install_run_mapping(const ROMlib_carbon_reference* ref, uint32_t tables, uint32_t handle, uint32_t index,
                               uint32_t mapping[3], uint32_t* status)
{
    const int utf8 = (CL(mapping[0]) >> 26 & 15) == 2;
    if (utf8)
        mapping[0] = CL(CL(mapping[0]) & 0x03ffffffU);
    const uint32_t profile = US_TO_SYN68K(mapping);
    uint32_t entry, base, record, attributes, header, version;
    if (!cfp_private(ref, 0x2d4, &profile, 1, &entry))
        return 0;
    if (!entry) {
        *status = mapping_not_found;
        return 1;
    }
    if (!cfp_get(tables, &base) || base > UINT32_MAX - 16 || entry < base + 16 || (entry - base - 16) % 32 ||
        !cfp_get(entry + 8, &version) || !cfp_get(entry + 12, &attributes) || !cfp_get(entry + 16, &header))
        return 0;
    mapping[2] = CL(version);
    _Alignas(32) uint32_t outputs[5] = {0};
    const uint32_t slots = US_TO_SYN68K(outputs);
    uint32_t args[] = {profile, entry, slots, slots + 4, slots + 8, slots + 12, slots + 16};
    if (!cfp_private(ref, 0x7698, args, NELEM(args), status))
        return 0;
    if (*status)
        return 1;

    uint32_t descriptor;
    if (!cfp_get(CL(outputs[3]), &descriptor) || !cfp_get(descriptor + 0x14, &version) || !cfp_get(handle, &record) ||
        !record)
        return 0;
    uint32_t row = record + 0x114 + index * 40;
    Byte* flags = ROMlib_carbon_guest_memory(record + 0x108, 2, 1);
    Byte* ready = ROMlib_carbon_guest_memory(row + 0x24, 1, 1);
    if (!flags || !ready || !cfp_write(row, (entry - base - 16) / 32) || !cfp_write(row + 4, header >> 16) ||
        !cfp_write(row + 8, CL(outputs[3])) || !cfp_move(row + 12, profile, 12) || !cfp_write(row + 24, attributes) ||
        !cfp_write(row + 28, CL(outputs[4])))
        return 0;
    if (!index && (!cfp_write(record + 0xf4, CL(outputs[0])) || !cfp_write(record + 0xf8, CL(outputs[1])) ||
                   !cfp_write(record + 0xa8, CL(outputs[2]))))
        return 0;
    flags[0] |= version & 1;
    flags[1] |= utf8;
    *ready = 0xff;
    return cfp_write(record + 0x110, index + 1);
}

static int create_run(const ROMlib_carbon_reference* ref, int operation, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t count = call->words[0], input = call->words[1], output = call->words[2];
    if (!output)
        return text_result(call, paramErr);
    /* Zero and high-bit counts select installed scripts and default priority,
       which are still managed by the saved TextCommon implementation. */
    if (!count || (count & 0x80000000))
        return ROMlib_carbon_invoke_frame(ref->function, call);
    if (!cfp_write(output, 0))
        return 0;
    unsigned stride = operation == text_create_run ? 12 : operation == text_create_run_by_encoding ? 4 : 2;
    uint64_t needed = 0x114 + (uint64_t)count * 40;
    if (!input || needed > INT32_MAX || (uint64_t)input + (uint64_t)count * stride > UINT64_C(0x100000000))
        return text_result(call, paramErr);
    uint32_t globals, tables;
    if (!converter_tables(ref, &globals, &tables))
        return 0;
    if (!tables)
        return text_result(call, mapping_unavailable);
    uint32_t bytes = needed, handle, record, table_state, status = noErr;
    if (!cfp_private(ref, 0x89d4, &bytes, 1, &handle))
        return 0;
    if (!handle)
        return cfp_private(ref, 0x895c, NULL, 0, &status) && text_result(call, status);
    if (!cfp_get(handle, &record) || !record || !cfp_write(record, converter_run) ||
        !cfp_private(ref, 0x8974, &tables, 1, &table_state)) {
        cfp_private(ref, 0x89a4, &handle, 1, NULL);
        return 0;
    }
    int ok = 1;
    for (uint32_t index = 0; ok && !status && index < count; ++index) {
        _Alignas(16) uint32_t mapping[3];
        ok = run_mapping(ref, operation, input + stride * index, mapping, &status) &&
             (status || install_run_mapping(ref, tables, handle, index, mapping, &status));
    }
    const uint32_t unlock[] = {tables, table_state};
    ok = cfp_private(ref, 0x898c, unlock, NELEM(unlock), NULL) && ok;
    if (ok && !status && cfp_private(ref, 0x8884, &handle, 1, NULL) && cfp_write(output, handle))
        return text_result(call, noErr);
    _Alignas(8) uint32_t temporary = CL(handle);
    ROMlib_carbon_ppc_frame disposal = *call;
    disposal.words[0] = US_TO_SYN68K(&temporary);
    return dispose_run(ref, &disposal) && ok && status && text_result(call, status);
}

static int set_fallback(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    uint32_t record, tag;
    if (args[2] & ~7u)
        return text_result(call, paramErr);
    if (!handle_record(args[0], &record))
        return 0;
    if (!record)
        return text_result(call, paramErr);
    if (!cfp_get(record, &tag))
        return 0;
    if (tag != converter_unicode && tag != converter_run)
        return text_result(call, paramErr);
    return cfp_write(record + 0xfc, args[1]) && cfp_write(record + 0x100, args[3]) &&
           cfp_write(record + 0x104, args[2]) && text_result(call, noErr);
}

static int copy_conversion_offsets(uint32_t input_length, const uint32_t* args, int round_offsets, uint32_t* status)
{
    const uint32_t count = args[4], input = args[5], count_out = args[6], output = args[7];
    if (!input) {
        *status = count ? paramErr : noErr;
        return 1;
    }
    if (!count_out || !output || count > UINT32_MAX / 4) {
        *status = paramErr;
        return 1;
    }
    const uint32_t* data = count ? ROMlib_carbon_guest_memory(input, count * 4, 0) : NULL;
    uint32_t* copy = count ? ROMlib_carbon_guest_memory(output, count * 4, 1) : NULL;
    if (count && (!data || !copy))
        return 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t offset;
        memcpy(&offset, data + i, sizeof offset);
        if (CL(offset) >= input_length) {
            *status = paramErr;
            return 1;
        }
        if (round_offsets)
            offset = CL(CL(offset) & ~1u);
        memcpy(copy + i, &offset, sizeof offset);
    }
    return cfp_write(count_out, count);
}

static int convert_text(const ROMlib_carbon_reference* ref, const uint32_t* args, uint32_t* status)
{
    const uint32_t handle = args[0], length = args[1], input = args[2], options = args[3];
    const uint32_t capacity = args[8], consumed = args[9], used = args[10], output = args[11];
    if (!cfp_write(consumed, 0) || !cfp_write(used, 0))
        return 0;
    if (options & UINT32_C(0xfffff99c)) {
        *status = paramErr;
        return 1;
    }
    uint32_t record, tag, tables;
    if (!handle_record(handle, &record))
        return 0;
    if (!record) {
        *status = paramErr;
        return 1;
    }
    if (!cfp_get(record, &tag))
        return 0;
    if (tag != converter_text) {
        *status = paramErr;
        return 1;
    }
    if (!cfp_private(ref, 0x89ec, NULL, 0, &tables))
        return 0;
    if (!tables) {
        *status = mapping_unavailable;
        return 1;
    }
    if ((length && !ROMlib_carbon_guest_memory(input, length, 0)) ||
        (capacity && !ROMlib_carbon_guest_memory(output, capacity, 1)))
        return 0;
    *status = noErr;
    if (!copy_conversion_offsets(length, args, 0, status))
        return 0;
    if (*status)
        return 1;

    if (!(options & 2) && !cfp_private(ref, 0x8914, &handle, 1, NULL))
        return 0;
    uint32_t table_handle, table, kind;
    uint8_t format;
    if (!cfp_get(handle, &record) || !cfp_get(record + 0x24, &table_handle) || !cfp_get(table_handle, &table) ||
        !cfp_get(table, &kind) || !read_byte(record + converter_text_bom, &format))
        return 0;
    _Alignas(8) uint32_t remaining = CL(args[4]);
    const uint32_t offsets = US_TO_SYN68K(&remaining), available = capacity & ~1u;
    if (kind == 1 || kind == 2) {
        const uint32_t engine[] = {table,     length,   input, options, offsets, args[7],
                                   available, consumed, used,  output,  format};
        if (!cfp_private(ref, kind == 1 ? 0x252c : 0x2710, engine, 11, status))
            return 0;
    }
    else if (kind == 5 || kind == 6) {
        const uint32_t engine[] = {record,  table,     length,   input, options, offsets,
                                   args[7], available, consumed, used,  output,  format};
        if (!cfp_private(ref, kind == 5 ? 0x2eec : 0x2ad0, engine, 12, status))
            return 0;
    }
    else
        *status = conversion_unrecognized_table;

    if ((int32_t)*status == conversion_output_full) {
        uint32_t bytes;
        if (!cfp_get(consumed, &bytes))
            return 0;
        if (!bytes)
            *status = conversion_buffer_too_small;
    }
    if (args[6]) {
        uint32_t mapped;
        if (!cfp_get(args[6], &mapped) || !cfp_write(args[6], mapped - CL(remaining)))
            return 0;
    }
    return 1;
}

static int reverse_step(const ROMlib_carbon_reference* ref, uint32_t record, uint32_t options, uint32_t codec_options,
                        uint32_t continuation, uint32_t input, uint32_t filled, int normalize, int* used_fallback,
                        uint32_t* status)
{
    // The mapper saves the single-unit retry state for an unmappable cluster.
    _Alignas(128) uint32_t retry[19] = {0};
    const uint32_t mapping[] = {record + 0xa8, US_TO_SYN68K(retry), record + 0xf4, record + 4, record + 0x54,
                                options,       continuation};
    const uint32_t encode[] = {record + 0x114, record + 0x54, record + 0x9c, codec_options};
    const uint32_t fallback[] = {
        record + 0xfc, record + 0x114, record + 0x54, record + 0x9c, codec_options | 0x08000000, 1};
    if (!cfp_private(ref, 0x41c4, mapping, 7, status))
        return 0;
    if ((int32_t)*status == conversion_unmappable_character && (options & 1)) {
        if (!cfp_write(record + 0x58, 2) || !cfp_move(record + 0x5c, cfp_read(record + 8), 2))
            return 0;
    }
    else if (*status)
        return 1;
    else {
        uint8_t stateful;
        uint32_t flags;
        if (!read_byte(record + 0x108, &stateful) || !cfp_get(record + 0x54, &flags))
            return 0;
        if (stateful && (flags & 0x300) != 0x100 && (flags & 0x300) != 0x200 &&
            !cfp_write(record + 0x54, (flags & ~0x300u) | 0x200))
            return 0;
        if (!cfp_private(ref, 0x54a0, encode, 4, status))
            return 0;
        if ((int32_t)*status != conversion_unmappable_sequence)
            return 1;
        if (cfp_read(record + 0x58) > 2) {
            if (!cfp_write(record + 0x58, 2) || !cfp_move(record + 0x5c, cfp_read(record + 8), 2) ||
                !cfp_move(record + 0xa8, US_TO_SYN68K(retry), sizeof retry))
                return 0;
            if (normalize) {
                const uint32_t next = input + filled;
                const uint32_t remaining = cfp_read(record + 0xc) + (cfp_read(record + 0x10) - next);
                if (!cfp_write(record + 0xc, remaining) || !cfp_write(record + 0x10, next))
                    return 0;
            }
            if (!cfp_private(ref, 0x54a0, encode, 4, status))
                return 0;
        }
        if ((int32_t)*status != conversion_unmappable_sequence || !(options & 1))
            return 1;
    }
    *used_fallback = 1;
    return cfp_private(ref, 0x19ac, fallback, 6, status);
}

static int refill_utf8(const ROMlib_carbon_reference* ref, uint32_t record, uint32_t* input, uint32_t* filled,
                       uint32_t* status)
{
    _Alignas(8) uint32_t input_used = 0;
    uint32_t buffer, length;
    if (!cfp_get(record + 0x10, input) || !cfp_get(record + 8, &buffer) || !cfp_get(record + 0xc, &length))
        return 0;
    const uint32_t fill_args[] = {*input, *input + length, buffer, buffer + 64, US_TO_SYN68K(&input_used), record + 4};
    if (!cfp_private(ref, 0x8378, fill_args, 6, status))
        return 0;
    *filled = CL(input_used);
    return *status || (cfp_write(record + 0x10, *input + *filled) && cfp_write(record + 0xc, length - *filled));
}

static int reverse_stream(const ROMlib_carbon_reference* ref, uint32_t record, const uint32_t* args, uint32_t* status)
{
    uint8_t normalize, stateful;
    const uint32_t length = args[1], options = args[3], consumed = args[9], used = args[10];
    uint32_t codec_options;
    if (!(options & 2) && !cfp_private(ref, 0x1b30, &record, 1, NULL))
        return 0;
    const uint32_t codec[] = {options, cfp_read(record + 0x124)};
    if (!cfp_private(ref, 0x554c, codec, 2, &codec_options) || !read_byte(record + 0x109, &normalize) ||
        !read_byte(record + 0x108, &stateful) || !cfp_write(record + 4, normalize ? 0 : length) ||
        !cfp_write(record + 8, normalize ? record + 0x14 : args[2]) ||
        !cfp_write(record + 0x10, normalize ? args[2] : 0) || (normalize && !cfp_write(record + 0xc, length)) ||
        !cfp_write(record + 0x9c, args[8]) || !cfp_write(record + 0xa4, args[11]))
        return 0;

    uint32_t offsets_left = args[4], offset = args[7], bytes_read = 0, bytes_written = 0;
    int substituted = 0, used_fallback = 0;
    while (bytes_read < length && (!*status || (int32_t)*status == conversion_used_fallback)) {
        if (offsets_left && cfp_read(offset) <= bytes_read) {
            if (!cfp_write(offset, bytes_written))
                return 0;
            --offsets_left;
            offset += 4;
        }

        uint32_t input = 0, filled = 0;
        if (normalize) {
            if (!refill_utf8(ref, record, &input, &filled, status))
                return 0;
            if (*status)
                break;
        }

        if (!reverse_step(ref, record, options, codec_options, stateful || (options & 2), input, filled, normalize,
                          &used_fallback, status))
            return 0;
        substituted |= used_fallback && (int32_t)*status == conversion_used_fallback;
        if (*status && !used_fallback)
            break;
        const uint32_t length_used = normalize ? cfp_read(record + 0x10) - input : cfp_read(record + 0x58);
        if (!normalize && (!cfp_write(record + 4, cfp_read(record + 4) - length_used) ||
                           !cfp_write(record + 8, cfp_read(record + 8) + (length_used & ~1u))))
            return 0;
        const uint32_t size_used = cfp_read(record + 0xa0);
        bytes_read += length_used;
        bytes_written += size_used;
        if (!cfp_write(record + 0x9c, cfp_read(record + 0x9c) - size_used) ||
            !cfp_write(record + 0xa4, cfp_read(record + 0xa4) + size_used) || !cfp_write(consumed, bytes_read) ||
            !cfp_write(used, bytes_written))
            return 0;
    }

    if (!*status && substituted)
        *status = conversion_used_fallback;
    else if ((int32_t)*status == conversion_output_full && !bytes_read)
        *status = conversion_buffer_too_small;
    return !args[6] || cfp_write(args[6], cfp_read(args[6]) - offsets_left);
}

static int convert_unicode(const ROMlib_carbon_reference* ref, const uint32_t* args, uint32_t* status)
{
    const uint32_t handle = args[0];
    uint32_t record, tag, tables;
    *status = noErr;
    if (!cfp_write(args[9], 0) || !cfp_write(args[10], 0))
        return 0;
    if ((args[3] & 0xfffff980u) || !handle) {
        *status = paramErr;
        return 1;
    }
    if (!handle_record(handle, &record) || !record || !cfp_get(record, &tag))
        return 0;
    if (tag != converter_unicode) {
        *status = paramErr;
        return 1;
    }
    if (!cfp_private(ref, 0x89ec, NULL, 0, &tables))
        return 0;
    if (!tables) {
        *status = mapping_unavailable;
        return 1;
    }
    uint8_t normalize;
    if ((args[1] && !ROMlib_carbon_guest_memory(args[2], args[1], 0)) ||
        (args[8] && !ROMlib_carbon_guest_memory(args[11], args[8], 1)) || !read_byte(record + 0x109, &normalize) ||
        !copy_conversion_offsets(args[1], args, !normalize, status))
        return 0;
    if (*status)
        return 1;

    uint32_t callback, fallback_options, table_handle;
    if (!cfp_get(record + 0xfc, &callback) || !cfp_get(record + 0x104, &fallback_options))
        return 0;
    if (!callback || !(fallback_options & 4))
        return reverse_stream(ref, record, args, status);

    uint32_t context_state, table_state;
    if (!cfp_private(ref, 0x8974, &handle, 1, &context_state))
        return 0;
    const int locked = cfp_get(handle, &record) && cfp_get(record + 0x114, &table_handle) &&
                       cfp_private(ref, 0x8974, &table_handle, 1, &table_state);
    int ok = locked && reverse_stream(ref, record, args, status);
    if (locked) {
        const uint32_t unlock[] = {table_handle, table_state & 255};
        ok = cfp_private(ref, 0x898c, unlock, 2, NULL) && ok;
    }
    const uint32_t unlock[] = {handle, context_state & 255};
    return cfp_private(ref, 0x898c, unlock, 2, NULL) && ok;
}

static int truncate_text(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    const uint32_t limit = args[1] < args[3] ? args[1] : args[3];
    uint32_t record, status, kind;
    const uint32_t validate[] = {args[0], converter_text};
    if (!cfp_write(args[4], limit) || !cfp_private(ref, 0x1350, validate, 2, &status))
        return 0;
    if (status)
        return text_result(call, status);
    if (!cfp_get(args[0], &record) || !cfp_get(cfp_read(record + 0x24), &record) || !cfp_get(record, &kind))
        return 0;
    if (kind == 1 || kind == 2 || kind == 6)
        return text_result(call, noErr);
    if (kind != 5)
        return text_result(call, conversion_unrecognized_table);

    uint32_t table;
    if (!cfp_get(args[0], &record) || !cfp_get(cfp_read(record + 0x20), &table))
        return 0;
    const Byte* input = limit ? ROMlib_carbon_guest_memory(args[2], limit, 0) : NULL;
    if (limit && !input)
        return 0;
    uint32_t safe = 0, cursor = 0;
    uint16_t state = 0;
    while (cursor < limit) {
        uint8_t category;
        if (!read_byte(table + 0x10 + input[cursor], &category))
            return 0;
        const Byte* transition = ROMlib_carbon_guest_memory(table + 0x110 + 4u * (state + category), 3, 0);
        if (!transition)
            return 0;
        const uint8_t action = transition[2];
        const uint16_t next = (uint16_t)transition[0] << 8 | transition[1];
        if (action == 2)
            return text_result(call, -8739);
        if (action == 3 || action > 5 || (action == 1 && next == state))
            return text_result(call, conversion_unrecognized_table);
        if (action < 2)
            safe = cursor + (action == 0);
        cursor += action != 1;
        state = next;
    }
    return cfp_write(args[4], safe) && text_result(call, noErr);
}

static int truncate_unicode(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    if (args[3] & ~0x43u)
        return text_result(call, paramErr);
    const uint32_t validate[] = {args[0], converter_unicode};
    uint32_t status, source;
    if (!cfp_private(ref, 0x1350, validate, 2, &status))
        return 0;
    if (status)
        return text_result(call, status);
    if (!cfp_get(args[0], &source) || (args[1] && !ROMlib_carbon_guest_memory(args[2], args[1], 0)))
        return 0;

    // Truncation reads the same clusters as conversion, but never advances the live context.
    _Alignas(512) Byte scratch[0x128];
    const uint32_t record = US_TO_SYN68K(scratch);
    if (!cfp_move(record, source, sizeof scratch))
        return 0;
    const int utf8 = scratch[0x109] != 0;
    if (!cfp_clear(record + 0xb0, 2) || !cfp_write(record + 0x54, 1) || !cfp_write(record + 4, utf8 ? 0 : args[1]) ||
        !cfp_write(record + 8, utf8 ? record + 0x14 : args[2]) || !cfp_write(record + 0x10, utf8 ? args[2] : 0) ||
        (utf8 && !cfp_write(record + 0xc, args[1])))
        return 0;

    const uint32_t limit = args[1] < args[4] ? args[1] : args[4];
    const uint32_t mapping[] = {record + 0xa8, 0, record + 0xf4, record + 4, record + 0x54, args[3] & ~3u, 0};
    uint32_t used = 0;
    do {
        uint32_t input, filled, step;
        if (utf8) {
            if (!refill_utf8(ref, record, &input, &filled, &status))
                return 0;
            if (status) {
                if (used)
                    status = noErr;
                break;
            }
        }
        if (!cfp_private(ref, 0x41c4, mapping, 7, &status))
            return 0;
        if (status) {
            if ((int32_t)status == -8755 && !used)
                status = noErr;
            break;
        }
        if (utf8)
            step = cfp_read(record + 0x10) - input;
        else {
            if (!cfp_get(record + 0x58, &step) || !cfp_write(record + 4, cfp_read(record + 4) - step) ||
                !cfp_write(record + 8, cfp_read(record + 8) + (step & ~1u)))
                return 0;
        }
        if (step > limit - used || (!step && !utf8))
            break;
        used += step;
    } while (used < limit);
    return cfp_write(args[5], used) && text_result(call, status);
}

static int pascal_conversion(const ROMlib_carbon_reference* ref, int to_unicode, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    _Alignas(8) uint32_t consumed, output_length = 0;
    const uint32_t count = US_TO_SYN68K(&consumed);
    uint32_t status;
    if (to_unicode) {
        uint8_t length;
        if (!args[1] || !read_byte(args[1], &length))
            return text_result(call, paramErr);
        const uint32_t conversion[] = {args[0], length, args[1] + 1, 1, 0, 0, 0, 0, args[2], count, args[3], args[4]};
        if (!convert_text(ref, conversion, &status))
            return 0;
    }
    else {
        Byte* output = ROMlib_carbon_guest_memory(args[3], 256, 1);
        if (!output || !args[3])
            return 0;
        const uint32_t conversion[] = {
            args[0], args[1], args[2], 0x21, 0, 0, 0, 0, 255, count, US_TO_SYN68K(&output_length), args[3] + 1};
        if (!convert_unicode(ref, conversion, &status))
            return 0;
        output[0] = CL(output_length);
    }
    return text_result(call, status);
}

static uint32_t encoding_match_mask(unsigned fields)
{
    static const uint32_t masks[] = {0, 0xffff, 0x03ff0000, 0x03ffffff, 0x3c000000, 0x3c00ffff, 0x3fff0000, 0x3fffffff};
    return masks[fields & 7];
}

static unsigned mapping_variants(uint32_t base, int querying, unsigned* first)
{
    *first = 0;
    switch (base) {
    case 0x101:
        *first = 2;
        return 1;
    case 0x102:
        *first = 1;
        return 1;
    case 0x104:
        return 1;
    case 0x1fc:
        /* The 1.1 query emits one legacy alias, while count reserves two. */
        return querying ? 1 : 2;
    case 0x1fe:
        return 2;
    case 0x1ff:
        return 3;
    default:
        return 0;
    }
}

static int mapping_query_locked(const ROMlib_carbon_reference* ref, uint32_t table, int querying, const uint32_t* args,
                                uint32_t* status)
{
    uint32_t record, rows;
    if (!handle_record(table, &record) || !record || !cfp_get(record + 8, &rows))
        return 0;
    Size extent = GetHandleSize(SYN68K_TO_US_CHECK0(table));
    if (extent < 16 || rows > (uint32_t)(extent - 16) / 32)
        return cfp_failure("Invalid Unicode mapping table extent");

    uint32_t requested_unicode = 0, requested_other = 0;
    unsigned filter = args[0];
    uint32_t unicode_mask = encoding_match_mask(filter), other_mask = encoding_match_mask(filter >> 3);
    if (filter && (!cfp_get(args[1], &requested_unicode) || !cfp_get(args[1] + 4, &requested_other)))
        return 0;
    int unicode_format_alias = (filter & 4) && (requested_unicode >> 26 & 15) == 2;
    if (unicode_format_alias)
        requested_unicode &= ~0x3c000000U;
    uint32_t count = 0, maximum = querying ? args[2] : 0;
    for (uint32_t index = 0; index < rows; ++index) {
        uint32_t slot = record + 16 + index * 32, unicode, other, resolved_unicode, resolved_other;
        if (!cfp_get(slot, &unicode) || !cfp_get(slot + 4, &other))
            return 0;
        uint32_t unicode_key = (unicode & ~unicode_mask) | (requested_unicode & unicode_mask);
        uint32_t other_key = (other & ~other_mask) | (requested_other & other_mask);
        if (!cfp_private(ref, 0x8a04, &unicode_key, 1, &resolved_unicode) ||
            !cfp_private(ref, 0x8a04, &other_key, 1, &resolved_other))
            return 0;
        if (resolved_other != other)
            continue;

        uint32_t base = resolved_unicode & 0xffff;
        unsigned first = 0, variants = 1;
        if (filter & 1) {
            uint32_t family = base == 0x103 ? 0x102 : base;
            if (!(unicode & family & 0xff) || (resolved_unicode & 0xffff0000) != (unicode & 0xffff0000))
                continue;
        }
        else {
            if (resolved_unicode != unicode)
                continue;
            variants = mapping_variants(base, querying, &first);
        }
        for (unsigned variant = 0; variant < variants; ++variant) {
            if (count < maximum) {
                uint32_t output = args[4] + 12 * count;
                uint32_t format = unicode_format_alias ? 2 : unicode >> 26 & 15;
                uint32_t code = filter & 1 ? base : cfp_toc(ref, 0x244 + 4 * (first + variant)) & 0xffff;
                uint32_t version;
                if (!cfp_get(slot + 8, &version) ||
                    !cfp_write(output, code | (unicode & 0x03ff0000) | (format << 26)) ||
                    !cfp_write(output + 4, other) || !cfp_write(output + 8, version))
                    return 0;
            }
            ++count;
        }
    }
    *status = querying && count > maximum ? mapping_list_too_small : noErr;
    return cfp_write(args[querying ? 3 : 2], count);
}

static int mapping_query(const ROMlib_carbon_reference* ref, int querying, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    uint32_t count = args[querying ? 3 : 2];
    if (!count || !cfp_write(count, 0))
        return text_result(call, paramErr);
    if ((args[0] && !args[1]) ||
        (querying && args[2] && (!args[4] || (uint64)args[4] + 12 * (uint64)args[2] > UINT64_C(0x100000000))))
        return text_result(call, paramErr);
    uint32_t table, previous, status;
    if (!cfp_private(ref, 0x89ec, NULL, 0, &table))
        return 0;
    if (!table)
        return text_result(call, mapping_unavailable);
    if (!cfp_private(ref, 0x8974, &table, 1, &previous))
        return 0;
    int ok = mapping_query_locked(ref, table, querying, args, &status);
    uint32_t unlock[] = {table, previous & 255};
    return cfp_private(ref, 0x898c, unlock, 2, NULL) && ok && text_result(call, status);
}

static int lock_mapping_table(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t mapping = call->words[0];
    if (!mapping || mapping > UINT32_MAX - 8)
        return text_result(call, paramErr);
    uint32_t table, previous;
    if (!cfp_private(ref, 0x89ec, NULL, 0, &table))
        return 0;
    if (!table)
        return text_result(call, mapping_unavailable);
    if (!cfp_private(ref, 0x8974, &table, 1, &previous))
        return 0;

    _Alignas(4) uint32_t normalized[3];
    uint32_t key, record, status = mapping_not_found;
    int ok = 1;
    for (unsigned i = 0; i < 3; ++i) {
        if (!cfp_get(mapping + 4 * i, &key) || (i < 2 && !cfp_private(ref, 0x8a04, &key, 1, &key))) {
            ok = 0;
            break;
        }
        normalized[i] = CL(key);
    }
    const uint32_t request = US_TO_SYN68K(normalized);
    if (ok)
        ok = cfp_private(ref, 0x2d4, &request, 1, &record);
    if (ok && record) {
        int16_t flags;
        status = noErr;
        ok = text_common_read_word(record + 0x12, &flags);
        if (ok && !(flags & 1)) {
            const uint32_t prepare[] = {record, 0, 0, 1};
            const uint32_t retain[] = {request, record, 0, 0, 0, 0, 1};
            ok = cfp_private(ref, 0x47c, prepare, NELEM(prepare), &status);
            if (ok && !status)
                ok = cfp_private(ref, 0x500, retain, NELEM(retain), &status);
            if (ok && !status) {
                ok = text_common_read_word(record + 0x12, &flags);
                if (ok)
                    ok = text_common_write_word(record + 0x12, flags | 1);
            }
        }
    }
    const uint32_t restore[] = {table, previous & 255};
    return cfp_private(ref, 0x898c, restore, NELEM(restore), NULL) && ok && text_result(call, status);
}

static int lock_converter(const ROMlib_carbon_reference* ref, int to_unicode, ROMlib_carbon_ppc_frame* call)
{
    uint32_t globals, global_data, catalogue;
    const uint32_t handle = call->words[0];
    if (!cfp_private(ref, 0x892c, NULL, 0, &globals) || !handle_record(globals, &global_data))
        return 0;
    if (!global_data)
        return text_result(call, mapping_unavailable);
    if (!cfp_get(global_data + 4, &catalogue))
        return 0;
    if (!catalogue)
        return text_result(call, mapping_unavailable);

    uint32_t zone, system, status;
    if (!cfp_private(ref, 0x8a94, &handle, 1, &zone) || !cfp_private(ref, 0x895c, NULL, 0, &status))
        return 0;
    if (status)
        return text_result(call, status);
    if (!cfp_private(ref, 0x8aac, NULL, 0, &system) ||
        !cfp_private(ref, zone == system ? 0x8ac4 : 0x8adc, &handle, 1, NULL) ||
        !cfp_private(ref, 0x8974, &handle, 1, NULL))
        return 0;

    uint32_t converter, records, index;
    if (!handle_record(handle, &converter) || !converter || !cfp_get(catalogue, &records) ||
        converter > UINT32_MAX - 0x10c || !cfp_get(converter + (to_unicode ? 4 : 0x10c), &index) || !records ||
        records > UINT32_MAX - 0x2d || index > (UINT32_MAX - records - 0x2d) / 32)
        return 0;
    const uint32_t record = records + index * 32;
    int16_t table;
    if (!text_common_read_word(record + (to_unicode ? 0x24 : 0x2c), &table))
        return 0;
    uint32_t load[] = {globals, table, 1, 0};
    if (!cfp_private(ref, 0x8af4, load, NELEM(load), &status))
        return 0;
    if (!status && to_unicode) {
        if (!text_common_read_word(record + 0x26, &table))
            return 0;
        load[1] = table;
        if (!cfp_private(ref, 0x8af4, load, NELEM(load), &status))
            return 0;
    }
    return text_result(call, status);
}

static int valid_conversion_length(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    uint32_t count = args[2];
    if (!count)
        return text_result(call, noErr);
    if (!args[3] || !args[4] || 4 * (uint64)count + MAX(args[3], args[4]) > UINT64_C(0x100000000))
        return text_result(call, paramErr);
    _Alignas(16) uint32_t mapping[3] = {CL(0x100), 0, CL(UINT32_MAX)};
    for (uint32_t index = 0; index < count; ++index) {
        uint32_t encoding, status;
        if (!cfp_get(args[3] + 4 * index, &encoding))
            return 0;
        mapping[1] = CL(encoding);
        uint32_t worker[] = {US_TO_SYN68K(mapping), args[0], args[1], 0x03000000, args[4] + 4 * index};
        if (!cfp_private(ref, 0x6f04, worker, NELEM(worker), &status))
            return 0;
        if (status)
            return text_result(call, status);
    }
    return text_result(call, noErr);
}

static int prepare_run_offsets(uint32_t record, const uint32_t* args, uint32_t* status)
{
    const uint32_t count = args[4], input = args[5], output = args[7];
    *status = paramErr;
    if (!input) {
        if (!count)
            *status = noErr;
        return 1;
    }
    if (!args[6] || !output || (uint64)MAX(input, output) + 4 * (uint64)count > UINT64_C(0x100000000))
        return 1;

    uint8_t normalize;
    if (!read_byte(record + 0x109, &normalize))
        return 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t offset;
        if (!cfp_get(input + 4 * i, &offset))
            return 0;
        if (offset >= args[1])
            return 1;
        if (!cfp_write(output + 4 * i, normalize ? offset : offset & ~1u))
            return 0;
    }
    *status = noErr;
    return cfp_write(args[6], count);
}

static int run_worker(const ROMlib_carbon_reference* ref, uint32_t record, uint32_t lock, const uint32_t* args,
                      uint32_t* remaining, uint32_t* status)
{
    if (!(args[3] & 2) && !cfp_private(ref, 0x668c, &record, 1, NULL))
        return 0;
    uint8_t normalize;
    if (!read_byte(record + 0x109, &normalize))
        return 0;
    if (normalize) {
        if (!cfp_write(record + 4, 0) || !cfp_write(record + 8, record + 0x14) || !cfp_write(record + 0xc, args[1]) ||
            !cfp_write(record + 0x10, args[2]))
            return 0;
    }
    else if (!cfp_write(record + 4, args[1]) || !cfp_write(record + 8, args[2]) || !cfp_write(record + 0x10, 0))
        return 0;
    if (!cfp_write(record + 0x9c, args[8]) || !cfp_write(record + 0xa4, args[11]))
        return 0;

    const uint32_t mode = args[3] & 0x180u;
    if (mode == 0x100u) {
        *status = paramErr;
        return 1;
    }
    uint32_t worker[] = {record,   args[3], US_TO_SYN68K(remaining), args[7], args[9], args[10], args[12], args[13],
                         args[14], lock};
    return cfp_private(ref, mode ? 0x5d48 : 0x582c, worker, NELEM(worker), status);
}

static int convert_text_run(const ROMlib_carbon_reference* ref, const uint32_t* args, uint32_t* status)
{
    if (!args[9] || !args[10] || !args[13]) {
        *status = paramErr;
        return 1;
    }
    if (!cfp_write(args[10], 0) || !cfp_write(args[9], 0) || !cfp_write(args[13], 0))
        return 0;
    uint32_t validate[] = {args[0], converter_run};
    if (!cfp_private(ref, 0x1350, validate, NELEM(validate), status))
        return 0;
    if (*status)
        return 1;
    if (!args[11] || !args[12] || !args[14] || (uint64)args[14] + 8 * (uint64)args[12] > UINT64_C(0x100000000)) {
        *status = paramErr;
        return 1;
    }
    uint32_t record, count, fallback, flags;
    if (!cfp_get(args[0], &record) || !prepare_run_offsets(record, args, status))
        return 0;
    if (*status)
        return 1;
    if (args[3] & 0xfffff000u) {
        *status = paramErr;
        return 1;
    }
    if (!cfp_get(record + 0xfc, &fallback) || !cfp_get(record + 0x104, &flags))
        return 0;
    const uint32_t lock = !fallback || !(flags & 4);
    if (lock && !cfp_private(ref, 0x8974, args, 1, NULL))
        return 0;

    _Alignas(16) uint32_t remaining = CL(args[4]);
    const int ok = (!lock || cfp_get(args[0], &record)) && run_worker(ref, record, lock, args, &remaining, status);
    const uint32_t unlock[] = {args[0], 0};
    if ((lock && !cfp_private(ref, 0x898c, unlock, NELEM(unlock), NULL)) || !ok)
        return 0;
    if (*status == (uint32_t)conversion_output_full) {
        uint32_t consumed;
        if (!cfp_get(args[9], &consumed))
            return 0;
        if (!consumed)
            *status = conversion_buffer_too_small;
    }
    return !args[6] || (cfp_get(args[6], &count) && cfp_write(args[6], count - CL(remaining)));
}

static int copy_script_runs(uint32_t input, uint32_t output, uint32_t count, uint32_t* status)
{
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t offset, encoding;
        if (!cfp_get(input + 8 * i, &offset) || !cfp_get(input + 8 * i + 4, &encoding) ||
            !cfp_write(output + 6 * i, offset))
            return 0;
        if (encoding & 0xff00u) {
            *status = mapping_not_found;
            continue;
        }
        void* script = ROMlib_carbon_guest_memory(output + 6 * i + 4, 2, 1);
        if (!script)
            return 0;
        const uint16_t encoded = CW((uint16_t)encoding);
        memcpy(script, &encoded, sizeof encoded);
    }
    return 1;
}

static int convert_script_run(const ROMlib_carbon_reference* ref, uint32_t* args, uint32_t* status)
{
    const uint32_t capacity = args[12], output = args[14];
    if (!args[13] || capacity > INT32_MAX / 8 ||
        (capacity && (!output || (uint64)output + 6 * (uint64)capacity > UINT64_C(0x100000000)))) {
        *status = paramErr;
        return 1;
    }
    const uint32_t bytes = capacity * 8;
    uint32_t handle;
    if (!cfp_private(ref, 0x8944, &bytes, 1, &handle))
        return 0;
    if (!handle)
        return cfp_private(ref, 0x895c, NULL, 0, status);
    if (!cfp_private(ref, 0x8974, &handle, 1, NULL)) {
        cfp_private(ref, 0x89a4, &handle, 1, NULL);
        return 0;
    }

    uint32_t count;
    const int ok = cfp_get(handle, &args[14]) && convert_text_run(ref, args, status) && cfp_get(args[13], &count) &&
                   count <= capacity && copy_script_runs(args[14], output, count, status);
    const uint32_t unlock[] = {handle, 0};
    const int unlocked = cfp_private(ref, 0x898c, unlock, NELEM(unlock), NULL);
    return cfp_private(ref, 0x89a4, &handle, 1, NULL) && unlocked && ok;
}

int ROMlib_carbon_text_conversion(const char* name, uint32_t offset, int operation, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    switch (operation) {
    case text_make_encoding:
        return text_result(call, (args[0] & 0xffffu) | ((args[1] & 0x3ffu) << 16) | ((args[2] & 0xfu) << 26));
    case text_get_base:
        return text_result(call, args[0] & 0xffffu);
    case text_get_variant:
        return text_result(call, (args[0] >> 16) & 0x3ffu);
    case text_get_format:
        return text_result(call, (args[0] >> 26) & 0xfu);
    case text_application_encoding:
        return text_application_default(name, offset, call);
    case text_resolve_default:
    case text_nearest_mac:
    case text_encoding_name:
    case text_map_script_iso:
    case text_revert_script:
    case text_upgrade_script:
    case text_get_property:
    case text_use_property:
    case text_load_property:
    case text_unload_property:
        return text_common_dispatch(name, offset, operation, call);
    }

    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("UnicodeConverter", name, &ref))
        return 0;
    if (ref.code_offset != offset || ref.code_size != 0x8c58 || !ref.toc || ref.toc_offset != 0x124 ||
        ref.toc_size != 0x3cc)
        return ROMlib_carbon_invoke_frame(ref.function, call);

    switch (operation) {
    case text_reset_text:
    case text_reset_unicode:
    case text_reset_run:
        return reset_converter(&ref, operation, call);
    case text_dispose_text:
    case text_dispose_unicode:
        return dispose_converter(&ref, operation == text_dispose_unicode, call);
    case text_create_text:
    case text_create_unicode:
    case text_create_text_by_encoding:
    case text_create_unicode_by_encoding: {
        const int reverse = operation == text_create_unicode || operation == text_create_unicode_by_encoding;
        uint32_t mapping[] = {CL(0x100), CL(args[0]), CL(UINT32_MAX)}, status;
        const uint32_t* input = mapping;
        if (operation == text_create_text || operation == text_create_unicode) {
            input = args[0] ? ROMlib_carbon_guest_memory(args[0], sizeof mapping, 0) : NULL;
            if (args[0] && !input)
                return 0;
        }
        return create_converter(&ref, reverse, input, args[1], &status) && text_result(call, status);
    }
    case text_truncate_text:
        return truncate_text(&ref, call);
    case text_truncate_unicode:
        return truncate_unicode(&ref, call);
    case text_lock_mapping:
        return lock_mapping_table(&ref, call);
    case text_lock_text:
    case text_lock_unicode:
        return lock_converter(&ref, operation == text_lock_text, call);
    case text_count_mappings:
    case text_query_mappings:
        return mapping_query(&ref, operation == text_query_mappings, call);
    case text_valid_conversion_length:
        return valid_conversion_length(&ref, call);
    case text_create_run:
    case text_create_run_by_encoding:
    case text_create_run_by_script:
        return create_run(&ref, operation, call);
    case text_dispose_run:
        return dispose_run(&ref, call);
    case text_change_text:
    case text_change_unicode:
        return change_converter(&ref, operation == text_change_unicode, call);
    case text_set_fallback:
        return set_fallback(call);
    case text_from_pascal:
    case text_to_pascal:
        return pascal_conversion(&ref, operation == text_from_pascal, call);
    case text_from_unicode_run:
    case text_from_unicode_script_run: {
        uint32_t conversion[15], status;
        for (unsigned i = 0; i < NELEM(conversion); ++i)
            if (!ROMlib_carbon_frame_word(call, i, &conversion[i]))
                return 0;
        const int converted = operation == text_from_unicode_run ? convert_text_run(&ref, conversion, &status)
                                                                 : convert_script_run(&ref, conversion, &status);
        return converted && text_result(call, status);
    }
    case text_to_unicode:
    case text_from_unicode: {
        uint32_t conversion[12], status;
        for (unsigned i = 0; i < 12; ++i)
            if (!ROMlib_carbon_frame_word(call, i, &conversion[i]))
                return 0;
        const int converted = operation == text_to_unicode ? convert_text(&ref, conversion, &status)
                                                           : convert_unicode(&ref, conversion, &status);
        return converted && text_result(call, status);
    }
    default:
        return 0;
    }
}
