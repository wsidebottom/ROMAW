/*+===================================================================
  File:      text_common.h

  Summary:   Resolves TextCommon encodings and compares Unicode collation keys.

  Classes:   None.

  Functions: ROMlib_carbon_compare_collation_keys and private TextCommon adapters.

  Origin:    Mac OS 9.2.1 TextCommon and UnicodeUtilitiesCoreLib, and Universal Interfaces 3.4.2.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMLIB_CARBON_TEXT_COMMON_H
#define ROMLIB_CARBON_TEXT_COMMON_H

#include "Gestalt.h"

enum {
    text_unknown_encoding = 0xffff,
    text_no_mapping = -8738,
    text_invalid_table = -8747,
    text_missing_globals = -8770,
    text_unicode_base = 0x100,
    text_unicode_3_1 = 0x104
};

static uint16_t text_table_word(const void* pointer)
{
    uint16_t value;
    memcpy(&value, pointer, sizeof value);
    return CW(value);
}

static uint32_t text_table_long(const void* pointer)
{
    uint32_t value;
    memcpy(&value, pointer, sizeof value);
    return CL(value);
}

static int text_common_globals(const ROMlib_carbon_reference* ref, uint32_t* handle, uint32_t* globals)
{
    *globals = 0;
    return cfp_private(ref, 0xcc, NULL, 0, handle) && (!*handle || cfp_get(*handle, globals));
}

static int text_common_base(uint32_t tables, uint32_t code, uint32_t* resolved)
{
    uint32_t header, aliases, count;
    *resolved = code;
    if (!cfp_get(tables, &tables) || !tables || !cfp_get(tables + 4, &header) || !cfp_get(tables + 8, &aliases) ||
        !cfp_get(header, &count))
        return 0;
    if (!count)
        return 1;
    const Byte* pairs = count <= UINT32_MAX / 8 ? ROMlib_carbon_guest_memory(aliases, count * 8, 0) : NULL;
    if (!pairs)
        return 0;
    for (uint32_t i = 0; i < count; ++i, pairs += 8) {
        if (text_table_long(pairs) == code) {
            *resolved = text_table_long(pairs + 4);
            break;
        }
    }
    return 1;
}

static int text_common_resolve(const ROMlib_carbon_reference* ref, uint32_t original, uint32_t* resolved)
{
    uint32_t handle, globals, tables = 0;
    *resolved = original;
    if (!text_common_globals(ref, &handle, &globals) || (globals && !cfp_get(globals, &tables)))
        return 0;
    if (!tables)
        return 1;

    uint32_t base = original & 0xffff, variant = (original >> 16) & 0x3ff, minimum_variant = 0;
    if (base == text_unicode_base)
        base = text_unicode_3_1;
    else if (!text_common_base(tables, base, &base))
        return 0;

    if (base == 0 || base == 0xfc || base == 0x24 || base == 0x26)
        minimum_variant = 1;
    else if (base == 0x25)
        minimum_variant = 2;
    else if ((base == 7 || base == 0x98) && !variant)
        minimum_variant = 16;
    else if (base == 0xa04 && !variant) {
        switch ((int16_t)C_GetEnvirons(40)) {
        case 21:
            variant = 11;
            break;
        case 24:
            variant = 6;
            break;
        case 39:
            variant = 14;
            break;
        default:
            variant = 2;
            break;
        }
    }

    if (minimum_variant) {
        uint32_t version;
        if (!cfp_get(globals + 0x1c, &version))
            return 0;
        if (!version) {
            LONGINT encoded = 0;
            OSErr err = Gestalt(gestaltSystemVersion, &encoded);
            version = err || !encoded ? 0x870 : CL(encoded);
            if (!cfp_write(globals + 0x1c, version))
                return 0;
        }
        if (minimum_variant == 16) {
            variant = version >= 0x870 ? 3 : base == 7 ? 1 : 2;
            base = 7;
        }
        else if (variant < minimum_variant)
            variant += version >= 0x850 ? minimum_variant * 2 : minimum_variant;
    }
    *resolved = (base & 0xffff) | ((variant & 0x3ff) << 16) | (original & 0x3c000000);
    return 1;
}

static int text_common_nearest(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t best = call->words[1], alternate = call->words[2];
    if (!best)
        return text_result(call, paramErr);
    if (!cfp_write(best, text_unknown_encoding) || (alternate && !cfp_write(alternate, text_unknown_encoding)))
        return 0;
    uint32_t encoding;
    if (!text_common_resolve(ref, call->words[0], &encoding))
        return 0;
    const unsigned family = (encoding >> 8) & 0xff, base = encoding & 0xffff;
    if (!family)
        return cfp_write(best, encoding) && text_result(call, noErr);
    if (family == 1)
        return text_result(call, paramErr);

    uint32_t handle, globals, status;
    if (!text_common_globals(ref, &handle, &globals))
        return 0;
    if (!globals)
        return text_result(call, text_missing_globals);
    _Alignas(4) uint32_t table_handle = 0;
    const uint32_t load[] = {handle, 0, 0, US_TO_SYN68K(&table_handle)};
    if (!cfp_private(ref, 0x1f14, load, NELEM(load), &status))
        return 0;
    if (status)
        return text_result(call, status);

    uint32_t extent;
    const Byte* table = cfp_handle_data(CL(table_handle), &extent, 0);
    int ok = 1;
    status = text_no_mapping;
    const unsigned family_offset = family * 4;
    if (table && family < 16 && extent >= family_offset + 8 && base <= text_table_word(table + family_offset + 6)) {
        const uint32_t offset = text_table_word(table + family_offset + 4) + (base & 0xff) * 4;
        if (offset <= extent && extent - offset >= 4) {
            const uint16_t primary = text_table_word(table + offset);
            if (primary != text_unknown_encoding) {
                ok = cfp_write(best, primary) &&
                     (!alternate || cfp_write(alternate, text_table_word(table + offset + 2)));
                status = noErr;
            }
        }
    }
    const uint32_t unload[] = {handle, 0};
    return cfp_private(ref, 0x2188, unload, NELEM(unload), NULL) && ok && text_result(call, status);
}

static const Byte* text_common_name_record(const Byte* table, uint32_t extent, int16_t region, uint32_t requested,
                                           uint32_t* actual)
{
    if (!table || extent < 20)
        return NULL;
    const uint32_t count = text_table_long(table + 16);
    if (!count || count > (extent - 20) / 16)
        return NULL;

    const Byte* first = table + 20;
    for (uint32_t i = 0; i < count; ++i) {
        const Byte* record = first + 16 * i;
        if ((int16_t)text_table_word(record) != region)
            continue;
        const uint32_t encoding = text_table_long(record + 4);
        if ((requested & 0xff00) == 0x100) {
            if ((encoding & 0xff00) != 0x100 || (encoding >> 16) != (requested >> 16) ||
                (uint16_t)encoding > (uint16_t)requested)
                continue;
            *actual = requested;
            return record;
        }
        if ((uint16_t)requested == text_unknown_encoding || encoding == requested) {
            *actual = encoding;
            return record;
        }
    }
    *actual = text_table_long(first + 4);
    return first;
}

static int text_common_name(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t *args = call->words, type = TICK("tnam");
    uint32_t encoding, requested, status, buffer;
    if (!ROMlib_carbon_frame_word(call, 8, &buffer) || !text_common_resolve(ref, args[0], &encoding) ||
        !text_common_resolve(ref, args[3], &requested))
        return 0;

    _Alignas(4) uint32_t handle = 0;
    const uint32_t load[] = {encoding, type, 0, US_TO_SYN68K(&handle)};
    if (!cfp_private(ref, 0x209c, load, NELEM(load), &status))
        return 0;
    if (status)
        return text_result(call, status);

    uint32_t extent = 0, actual;
    const Byte* table = cfp_handle_data(CL(handle), &extent, 0);
    const Byte* record = text_common_name_record(table, extent, (int16_t)args[2], requested, &actual);
    int ok = 1;
    status = text_invalid_table;
    if (record) {
        const uint32_t name_length = args[1] <= 1 ? text_table_long(record + 8) : 0;
        const uint32_t offset = text_table_long(record + 12);
        const uint32_t bytes = buffer ? MIN(name_length, args[4]) : 0;
        if (!name_length || (offset <= extent && name_length <= extent - offset)) {
            const uint16_t region = CW(text_table_word(record));
            void* destination = bytes ? ROMlib_carbon_guest_memory(buffer, bytes, 1) : NULL;
            void* region_out = args[6] ? ROMlib_carbon_guest_memory(args[6], sizeof region, 1) : NULL;
            ok = (!bytes || destination) && (!args[6] || region_out) && (!args[5] || cfp_write(args[5], name_length)) &&
                 (!args[7] || cfp_write(args[7], actual));
            if (ok && region_out)
                memcpy(region_out, &region, sizeof region);
            if (ok && bytes)
                memcpy(destination, table + offset, bytes);
            status = noErr;
        }
    }
    const uint32_t unload[] = {encoding, type};
    return cfp_private(ref, 0x222c, unload, NELEM(unload), NULL) && ok && text_result(call, status);
}

enum { text_wildcard_script = -128, text_wildcard_iso = 0x2a2a, text_unknown_iso = 0x3f3f };

static int text_common_read_word(uint32_t address, int16_t* value)
{
    const Byte* data = address ? ROMlib_carbon_guest_memory(address, 2, 0) : NULL;
    if (!data)
        return 0;
    *value = (int16_t)text_table_word(data);
    return 1;
}

static int text_common_write_word(uint32_t address, uint16_t value)
{
    void* data = address ? ROMlib_carbon_guest_memory(address, 2, 1) : NULL;
    if (!data)
        return 0;
    value = CW(value);
    memcpy(data, &value, sizeof value);
    return 1;
}

static const Byte* text_common_script_record(const Byte* map, unsigned field, int16_t key)
{
    const uint32_t base = text_table_long(map + field);
    if (!base || key < 0 || base > UINT32_MAX - (uint32_t)key * 4)
        return NULL;
    return ROMlib_carbon_guest_memory(base + (uint32_t)key * 4, 4, 0);
}

static const Byte* text_common_iso_record(const Byte* map, const Byte* counts, unsigned field, unsigned count_field,
                                          unsigned stride, int16_t key, const int16_t* region, int ordered)
{
    const uint32_t base = text_table_long(map + field), count = text_table_long(counts + count_field);
    if (!base || !count || count > UINT32_MAX / stride)
        return NULL;
    const Byte* records = ROMlib_carbon_guest_memory(base, count * stride, 0);
    if (!records)
        return NULL;
    uint32_t first = 0, last = count;
    while (first < last) {
        const uint32_t index = ordered ? first + (last - first - 1) / 2 : first;
        const Byte* record = records + index * stride;
        int difference = key - (int16_t)text_table_word(record);
        if (!difference && region)
            difference = *region - (int16_t)text_table_word(record + 2);
        if (!difference)
            return record;
        if (ordered && difference < 0)
            last = index;
        else
            first = index + 1;
    }
    return NULL;
}

static int text_common_script_to_iso(const Byte* map, const Byte* counts, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t script = call->words[0], language = call->words[1], iso_script = call->words[2],
                   iso_language = call->words[3];
    int16_t code = text_wildcard_script, lang = text_wildcard_script;
    if (script) {
        if (!iso_script)
            return text_result(call, paramErr);
        if (!text_common_read_word(script, &code))
            return 0;
        uint16_t result = text_wildcard_iso;
        if (code != text_wildcard_script) {
            const Byte* record = text_common_script_record(map, 0x30, code);
            if (!record)
                return text_result(call, text_invalid_table);
            const int16_t key = (int16_t)text_table_word(record);
            if (key != code && key != INT16_MAX)
                return text_result(call, text_invalid_table);
            result = key == INT16_MAX ? text_unknown_iso : text_table_word(record + 2);
        }
        if (!text_common_write_word(iso_script, result))
            return 0;
    }

    if (language && !text_common_read_word(language, &lang))
        return 0;
    int default_language = 0;
    if (code != text_wildcard_script && lang == text_wildcard_script) {
        const Byte* record = text_common_script_record(map, 0x1c, code);
        if (!record)
            return text_result(call, text_invalid_table);
        const int16_t key = (int16_t)text_table_word(record);
        if (key == code) {
            lang = (int16_t)text_table_word(record + 2);
            default_language = 1;
        }
        else if (key != INT16_MAX)
            return text_result(call, text_invalid_table);
    }

    if (!language && !default_language)
        return text_result(call, noErr);
    if (!iso_language)
        return text_result(call, default_language ? paramErr : noErr);
    if (lang == text_wildcard_script && !default_language)
        return text_common_write_word(language, text_wildcard_iso) && text_result(call, noErr);
    if (lang == text_wildcard_script)
        return text_result(call, noErr);

    const Byte* record = text_common_iso_record(map, counts, 0x2c, 0x24, 4, lang, NULL, 1);
    return text_common_write_word(iso_language, record ? text_table_word(record + 2) : text_unknown_iso) &&
           text_result(call, noErr);
}

static int text_common_iso_to_script(const Byte* map, const Byte* counts, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t script = call->words[0], language = call->words[1], iso_script = call->words[2],
                   iso_language = call->words[3];
    int16_t iso_lang = text_wildcard_iso, iso_code = text_wildcard_iso;
    if (iso_language && !text_common_read_word(iso_language, &iso_lang))
        return 0;
    if (iso_script) {
        if (!script)
            return text_result(call, paramErr);
        if (!text_common_read_word(iso_script, &iso_code))
            return 0;
        int16_t code = text_wildcard_script;
        if (iso_code != text_wildcard_iso) {
            const Byte* record = text_common_iso_record(map, counts, 0x3c, 0x34, 4, iso_code, NULL, 1);
            if (record)
                code = (int16_t)text_table_word(record + 2);
            else {
                code = -1;
                if (iso_language) {
                    record = text_common_iso_record(map, counts, 0x40, 0x38, 6, iso_code,
                                                    iso_lang == text_wildcard_iso ? NULL : &iso_lang, 0);
                    if (record)
                        code = (int16_t)text_table_word(record + 4);
                }
            }
        }
        if (!text_common_write_word(script, code))
            return 0;
    }

    const Byte* default_language = NULL;
    if (iso_script && iso_code != text_wildcard_iso && iso_language && iso_lang == text_wildcard_iso) {
        int16_t code;
        if (!text_common_read_word(script, &code))
            return 0;
        if (code != -1)
            default_language = text_common_iso_record(map, counts, 0x54, 0x4c, 4, iso_code, NULL, 1);
    }
    if (!iso_language && !default_language)
        return text_result(call, noErr);
    if (!language)
        return text_result(call, default_language ? paramErr : noErr);
    if (default_language)
        iso_lang = (int16_t)text_table_word(default_language + 2);

    int16_t lang = text_wildcard_script;
    if (iso_lang != text_wildcard_iso) {
        const Byte* record = text_common_iso_record(map, counts, 0x34, 0x2c, 4, iso_lang, NULL, 1);
        lang = record ? (int16_t)text_table_word(record + 2) : -1;
        if (!record && iso_script)
            record =
                text_common_iso_record(map, counts, 0x38, 0x30, 6, iso_lang,
                                       iso_code == text_wildcard_iso ? NULL : &iso_code, iso_code != text_wildcard_iso);
        if (record && lang == -1)
            lang = (int16_t)text_table_word(record + 4);
    }
    return text_common_write_word(language, lang) && text_result(call, noErr);
}

static const Byte* text_common_table_entry(uint32_t base, uint32_t index, uint32_t stride)
{
    if (!base || index > (UINT32_MAX - base) / stride)
        return NULL;
    return ROMlib_carbon_try_guest_memory(base + index * stride, stride, 0);
}

static uint32_t text_common_string_at(uint32_t table, uint32_t index, unsigned prefix)
{
    const Byte* first = text_common_table_entry(table, 0, 2);
    if (!first || index >= text_table_word(first))
        return 0;
    const Byte* offset = text_common_table_entry(table, index + 1, 2);
    if (!offset || table > UINT32_MAX - text_table_word(offset) - prefix)
        return 0;
    return table + text_table_word(offset) + prefix;
}

static int text_common_script_tables(const ROMlib_carbon_reference* ref, uint32_t* handle, const Byte** map,
                                     const Byte** descriptor)
{
    uint32_t extent = 0;
    if (!cfp_private(ref, 0x104, NULL, 0, handle))
        return 0;
    *map = cfp_handle_data(*handle, &extent, 0);
    *descriptor = *map && extent >= 0x60 ? ROMlib_carbon_try_guest_memory(text_table_long(*map), 0x24, 0) : NULL;
    return 1;
}

static int text_common_revert_record(const Byte* map, const Byte* descriptor, uint32_t base, const Byte** record)
{
    const uint32_t records = text_table_long(map + 0x10);
    if (base > text_table_long(descriptor + 0x20))
        return paramErr;
    if (base > text_table_long(descriptor + 0x0c) && base <= text_table_long(descriptor + 0x10)) {
        *record = text_common_table_entry(records, base, 16);
        return *record && text_table_long(*record) == base ? noErr : text_invalid_table;
    }

    uint32_t index = base;
    if (base > text_table_long(descriptor + 0x10)) {
        const uint32_t step = text_table_long(descriptor + 0x18), origin = text_table_long(descriptor + 0x14);
        const uint32_t start = text_table_long(descriptor + 0x1c);
        if (!step || origin > base || (base - origin) / step > UINT32_MAX - start)
            return text_invalid_table;
        index = start + (base - origin) / step;
    }

    const Byte* bucket = text_common_table_entry(text_table_long(map + 0x0c), index, 8);
    *record = text_common_table_entry(records, index, 16);
    if (!bucket || !*record)
        return text_invalid_table;
    uint32_t alternative = text_table_long(bucket);
    const uint16_t count = text_table_word(bucket + 4);
    if (!alternative || !count)
        return !alternative && !count ? noErr : text_invalid_table;
    if (text_table_long(*record) == base)
        return noErr;

    for (uint32_t i = 0; i <= count; ++i) {
        if (alternative > UINT32_MAX - i)
            return text_invalid_table;
        *record = text_common_table_entry(records, alternative + i, 16);
        if (!*record)
            return text_invalid_table;
        if (i == count || text_table_long(*record) == base)
            return noErr;
    }
    return text_invalid_table;
}

static int text_common_revert_output(ROMlib_carbon_ppc_frame* call, uint16_t script, uint16_t language, uint32_t font)
{
    if (!call->words[1])
        return text_result(call, paramErr);
    if (!text_common_write_word(call->words[1], script) ||
        (call->words[2] && !text_common_write_word(call->words[2], language)))
        return 0;
    if (call->words[3]) {
        const Byte* length = font ? ROMlib_carbon_guest_memory(font, 1, 0) : NULL;
        if (font && !length)
            return 0;
        if (length) {
            if (!cfp_move(call->words[3], font, (uint32_t)*length + 1))
                return 0;
        }
        else if (!cfp_clear(call->words[3], 1))
            return 0;
    }
    return text_result(call, noErr);
}

static int text_common_revert_script(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    uint32_t base = call->words[0] & 0xffff, variant = (call->words[0] >> 16) & 0x3ff, handle;
    if (!base || base == 0xfc || base == 0x24 || base == 0x26)
        variant = 0;
    else if (base == 0x25)
        variant &= 1;
    else if (base == 7) {
        if (variant == 2 || (variant == 3 && C_GetEnvirons(40) == 62))
            base = 0x98;
        variant = 0;
    }

    const Byte *map, *descriptor;
    if (!text_common_script_tables(ref, &handle, &map, &descriptor))
        return 0;
    if (!descriptor || text_table_long(descriptor) != TICK("SDG!"))
        return text_result(call, text_invalid_table);
    if (!text_common_base(handle, base, &base))
        return 0;
    if (variant) {
        const Byte* counts = ROMlib_carbon_try_guest_memory(text_table_long(map + 4), 0x24, 0);
        if (!counts)
            return text_result(call, text_invalid_table);
        const uint32_t count = text_table_long(counts + 0x20), records = text_table_long(map + 0x28);
        uint32_t best = 0;
        for (uint32_t i = 0; i < count; ++i) {
            const Byte* record = text_common_table_entry(records, i, 8);
            if (!record)
                return text_result(call, text_invalid_table);
            if (text_table_long(record) != base || text_table_word(record + 4) != variant)
                continue;
            const uint32_t entry = text_common_string_at(text_table_long(map + 0x5c), text_table_word(record + 6), 0);
            if (!entry || entry > UINT32_MAX - 16)
                return text_result(call, text_invalid_table);
            const uint32_t name = entry + 16;
            uint32_t installed;
            if (!cfp_private(ref, 0x3204, &name, 1, &installed))
                return 0;
            if (!best || installed)
                best = entry;
            if (installed)
                break;
        }
        if (best) {
            const Byte* record = ROMlib_carbon_guest_memory(best, 16, 0);
            return record &&
                   text_common_revert_output(call, text_table_word(record), text_table_word(record + 2), best + 16);
        }
    }

    const Byte* record;
    const int status = text_common_revert_record(map, descriptor, base, &record);
    if (status)
        return text_result(call, status);
    if (!call->words[1])
        return text_result(call, paramErr);
    uint32_t font = 0;
    const uint32_t index = text_table_long(record + 12);
    if (call->words[3] && index != (uint32_t)text_wildcard_script) {
        font = text_common_string_at(text_table_long(map + 0x58), index, 2);
        if (!font) {
            if (!text_common_write_word(call->words[1], text_table_word(record + 4)) ||
                (call->words[2] && !text_common_write_word(call->words[2], text_table_word(record + 6))))
                return 0;
            return text_result(call, text_invalid_table);
        }
    }
    return text_common_revert_output(call, text_table_word(record + 4), text_table_word(record + 6), font);
}

static int text_common_upgrade_result(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call,
                                      uint32_t encoding)
{
    return text_common_resolve(ref, encoding, &encoding) && cfp_write(call->words[4], encoding) &&
           text_result(call, noErr);
}

static int text_common_upgrade_language(const Byte* map, unsigned table, int16_t key, int16_t* result)
{
    const Byte* record = text_common_script_record(map, table, key);
    if (!record)
        return text_invalid_table;
    const int16_t stored = (int16_t)text_table_word(record);
    if (stored != key)
        return stored == INT16_MAX ? paramErr : text_invalid_table;
    *result = (int16_t)text_table_word(record + 2);
    return noErr;
}

static int text_common_upgrade_script(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    int16_t script = (int16_t)call->words[0], language = (int16_t)call->words[1], region = (int16_t)call->words[2];
    const uint32_t font = call->words[3];
    const int16_t wildcard = text_wildcard_script;
    if (!call->words[4])
        return text_result(call, paramErr);
    if (language == 30)
        language = 15;
    if (region == 47)
        region = 21;

    uint32_t handle;
    const Byte *map, *descriptor;
    if (!text_common_script_tables(ref, &handle, &map, &descriptor))
        return 0;
    if (!descriptor || text_table_long(descriptor) != TICK("SDG!"))
        return text_result(call, text_invalid_table);
    const Byte* counts = ROMlib_carbon_try_guest_memory(text_table_long(map + 4), 0x54, 0);
    if (!counts)
        return text_result(call, text_invalid_table);
    if (script < 0 && script != wildcard) {
        uint32_t status;
        _Alignas(2) int16_t selected = 0;
        const uint32_t args[] = {script, US_TO_SYN68K(&selected)};
        if (!cfp_private(ref, 0x30cc, args, NELEM(args), &status))
            return 0;
        if (status)
            return text_result(call, paramErr);
        script = CW(selected);
    }
    else if (script != wildcard && (uint32_t)script >= text_table_long(counts + 0x0c))
        return text_result(call, paramErr);

    if (region != wildcard) {
        if ((uint32_t)region > text_table_long(counts + 0x14))
            return text_result(call, paramErr);
        int16_t regional_language;
        const int status = text_common_upgrade_language(map, 0x1c, region, &regional_language);
        if (status)
            return text_result(call, status);
        if (language != wildcard && language != regional_language)
            return text_result(call, paramErr);
        language = regional_language;
    }

    if (language != wildcard) {
        if ((uint32_t)language > text_table_long(counts + 0x10))
            return text_result(call, paramErr);
        int16_t default_script;
        const int status = text_common_upgrade_language(map, 0x18, language, &default_script);
        if (status)
            return text_result(call, status);
        if (script == wildcard)
            script = default_script;
        else if (script != default_script) {
            const Byte* record = text_common_iso_record(map, counts, 0x20, 0x18, 4, language, &script, 0);
            if (!record)
                return text_result(call, paramErr);
        }
    }

    const int16_t system_script = (int16_t)C_GetEnvirons(18);
    const Byte* font_length = font ? ROMlib_carbon_guest_memory(font, 1, 0) : NULL;
    if (font && (!font_length || !ROMlib_carbon_guest_memory(font, (uint32_t)*font_length + 1, 0)))
        return 0;
    if (font_length && *font_length) {
        const uint32_t standard = text_table_long(map + 0x58);
        uint32_t count = text_table_long(counts + 0x50);
        int standard_match = 0;
        while (count) {
            const uint32_t name = text_common_string_at(standard, --count, 2);
            uint32_t matches;
            if (!name)
                continue;
            const uint32_t args[] = {name, font, 0, 0};
            if (!cfp_private(ref, 0x4aa8, args, NELEM(args), &matches))
                return 0;
            if (matches) {
                if (!text_common_read_word(name - 2, &script))
                    return 0;
                standard_match = 1;
                break;
            }
        }

        if (!standard_match) {
            const uint32_t variants = text_table_long(map + 0x5c);
            const Byte* header = text_common_table_entry(variants, 0, 2);
            if (!header)
                return text_result(call, text_invalid_table);
            for (uint32_t i = 0; i < text_table_word(header); ++i) {
                const uint32_t entry = text_common_string_at(variants, i, 0);
                if (!entry || entry > UINT32_MAX - 16)
                    return text_result(call, text_invalid_table);
                const uint32_t args[] = {font, entry + 16, 1, 1};
                uint32_t matches;
                if (!cfp_private(ref, 0x4aa8, args, NELEM(args), &matches))
                    return 0;
                if (!matches)
                    continue;
                const Byte* record = ROMlib_carbon_guest_memory(entry, 16, 0);
                if (!record)
                    return 0;
                const int16_t font_script = (int16_t)text_table_word(record);
                const int16_t font_language = (int16_t)text_table_word(record + 2);
                const int16_t font_region = (int16_t)text_table_word(record + 4);
                const int regional = text_table_word(record + 6) != 0;
                if (script != wildcard && script != font_script)
                    return text_result(call, paramErr);
                int compatible = (language == wildcard || font_language == wildcard || language == font_language) &&
                                 (region == wildcard || font_region == wildcard || region == font_region);
                if (compatible && regional && language == wildcard && C_GetEnvirons(40) != font_region)
                    compatible = 0;
                if (compatible) {
                    const uint32_t encoding =
                        (text_table_long(record + 8) & 0xffff) | ((text_table_long(record + 12) & 0x3ff) << 16);
                    return text_common_upgrade_result(ref, call, encoding);
                }
                if (!regional)
                    return text_result(call, paramErr);
                break;
            }
            uint32_t installed, font_script;
            if (!cfp_private(ref, 0x3204, &font, 1, &installed))
                return 0;
            if (!installed)
                return text_result(call, paramErr);
            if (!cfp_private(ref, 0x3284, &font, 1, &font_script))
                return 0;
            if (script != wildcard && (int32_t)font_script != script)
                return text_result(call, paramErr);
            script = (int16_t)font_script;
        }
    }

    if (script == wildcard)
        return text_result(call, paramErr);
    if (language == wildcard && region == wildcard && script == system_script) {
        const int status = text_common_upgrade_language(map, 0x1c, (int16_t)C_GetEnvirons(40), &language);
        if (status)
            return text_result(call, status);
    }

    uint32_t status;
    _Alignas(4) uint32_t encoding = 0;
    const uint32_t args[] = {US_TO_SYN68K(map), script, language, region, font, US_TO_SYN68K(&encoding)};
    if (!cfp_private(ref, 0x32b8, args, NELEM(args), &status))
        return 0;
    if (status)
        return text_result(call, paramErr);
    return text_common_upgrade_result(ref, call, CL(encoding));
}

static int text_common_map_script(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    uint32_t handle, extent = 0;
    if (!cfp_private(ref, 0x104, NULL, 0, &handle))
        return 0;
    const Byte* map = cfp_handle_data(handle, &extent, 0);
    if (!map || extent < 0x58)
        return text_result(call, text_invalid_table);
    const Byte* counts = ROMlib_carbon_guest_memory(text_table_long(map + 4), 0x50, 0);
    const Byte* signature = ROMlib_carbon_guest_memory(text_table_long(map), 4, 0);
    if (!counts || !signature || text_table_long(signature) != TICK("SDG!"))
        return text_result(call, text_invalid_table);
    return (uint8_t)call->words[4] ? text_common_script_to_iso(map, counts, call)
                                   : text_common_iso_to_script(map, counts, call);
}

static int text_application_default(const char* name, uint32_t offset, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", name, &ref))
        return 0;
    if (ref.code_offset != offset || ref.code_size != 0xb8fb4 || !ref.toc || ref.toc_offset != 0x2f78 ||
        ref.toc_size != 0x10f0c)
        return ROMlib_carbon_invoke_frame(ref.function, call);

    const uint32_t toc = US_TO_SYN68K(ref.toc);
    uint32_t ready_address, encoding_address, process_address, process_script, encoding;
    if (!cfp_get(toc - 0xa3c, &ready_address) || !cfp_get(toc - 0xa38, &encoding_address))
        return 0;
    Byte* ready = ROMlib_carbon_guest_memory(ready_address, 1, 1);
    if (!ready)
        return 0;
    if (!*ready) {
        if (!cfp_get(toc - 0x26a4, &process_script) || !cfp_get(toc - 0xa34, &process_address))
            return 0;
        int16_t script, region = text_wildcard_script;
        if (process_script) {
            _Alignas(4) uint32_t process[2];
            if (!cfp_get(process_address, &process[0]) || !cfp_get(process_address + 4, &process[1]))
                return 0;
            process[0] = CL(process[0]);
            process[1] = CL(process[1]);
            _Alignas(2) int16_t selected = 0, selected_region = CW(text_wildcard_script);
            const uint32_t args[] = {US_TO_SYN68K(process), US_TO_SYN68K(&selected), US_TO_SYN68K(&selected_region)};
            if (!cfp_private(&ref, 0xb6a68, args, NELEM(args), NULL))
                return 0;
            script = CW(selected);
            region = CW(selected_region);
        }
        else {
            uint32_t result;
            if (!cfp_private(&ref, 0xb4428, NULL, 0, &result))
                return 0;
            script = (int16_t)result;
        }
        const uint32_t upgrade[] = {script, (uint32_t)text_wildcard_script, region, 0, encoding_address};
        if (!cfp_private(&ref, 0xb6a80, upgrade, NELEM(upgrade), NULL))
            return 0;
        *ready = 1;
    }
    return cfp_get(encoding_address, &encoding) && text_result(call, encoding);
}

static int text_common_property_value(uint32_t text, uint32_t table, uint32_t* result)
{
    int16_t unit;
    if (!text_common_read_word(text, &unit) || table > UINT32_MAX - 0x410)
        return 0;
    const uint32_t character = (uint16_t)unit;
    const Byte* high = text_common_table_entry(table + 0x10, character >> 7, 2);
    if (!high)
        return 0;
    const uint16_t entry = text_table_word(high);
    *result = entry;
    if (entry & 0x8000) {
        uint32_t width;
        if (!cfp_get(table, &width))
            return 0;
        const uint32_t index = (entry & 0x7fff) + (character & 0x7f);
        const Byte* value = text_common_table_entry(table + 0x410, index, width == 1 ? 1 : 2);
        if (!value)
            return 0;
        *result = width == 1 ? *value : text_table_word(value);
    }
    return 1;
}

static int text_common_property_slot(const ROMlib_carbon_reference* ref, int32_t property, uint32_t* slot,
                                     uint32_t* count, uint32_t* handle)
{
    uint32_t globals_handle, globals, registry, directory;
    *slot = *count = *handle = 0;
    if (!ref)
        return 1;
    if (!text_common_globals(ref, &globals_handle, &globals))
        return 0;
    if (!globals)
        return 1;
    if (!cfp_get(globals + 0x10, &registry))
        return 0;
    if (!registry)
        return 1;
    if (!cfp_get(registry, &directory))
        return 0;
    const Byte* header = text_common_table_entry(directory, 0, 8);
    if (!header)
        return 0;
    const int32_t first = (int32_t)text_table_long(header), last = (int32_t)text_table_long(header + 4);
    if (property < first || property > last)
        return 1;

    const uint64_t index = (int64_t)property - first + 1;
    if (index > (UINT32_MAX - directory) / 8)
        return 0;
    *slot = directory + (uint32_t)index * 8;
    const Byte* record = text_common_table_entry(*slot, 0, 8);
    if (!record)
        return 0;
    *count = text_table_long(record);
    *handle = text_table_long(record + 4);
    return 1;
}

static int text_common_properties(const ROMlib_carbon_reference* ref, int operation, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    if (operation == text_use_property) {
        uint32_t value;
        return args[2] && text_common_property_value(args[0], args[2], &value) && text_result(call, value);
    }

    const int query = operation == text_get_property, load = operation == text_load_property;
    const uint32_t output = query ? args[3] : args[1];
    if ((query || load) && output && !cfp_write(output, 0))
        return 0;
    if (!output || (query && (!args[0] || !args[1])))
        return text_result(call, paramErr);

    uint32_t slot, count, handle;
    if (!text_common_property_slot(ref, query ? args[2] : args[0], &slot, &count, &handle))
        return 0;
    if (query || load) {
        if (!handle || !count)
            return text_result(call, resNotFound);
        if (query) {
            uint32_t table, value;
            return cfp_get(handle, &table) && table && text_common_property_value(args[0], table, &value) &&
                   cfp_write(output, value) && text_result(call, noErr);
        }
        return cfp_write(output, handle) && cfp_write(slot, count + 1) && text_result(call, noErr);
    }

    uint32_t requested;
    if (!cfp_get(output, &requested))
        return 0;
    if (!count || requested != handle)
        return cfp_write(output, 0) && text_result(call, paramErr);
    return cfp_write(slot, count - 1) && cfp_write(output, 0) && text_result(call, noErr);
}

static int text_common_dispatch(const char* name, uint32_t offset, int operation, ROMlib_carbon_ppc_frame* call)
{
    const int properties = operation == text_use_property || operation == text_get_property ||
                           operation == text_load_property || operation == text_unload_property;
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_probe_reference("TextCommon", name, &ref) &&
        !ROMlib_carbon_load_reference("TextCommon", name, &ref)) {
        if (operation == text_resolve_default)
            return 1;
        return properties ? text_common_properties(NULL, operation, call) : text_result(call, text_missing_globals);
    }
    if (ref.code_offset != offset || ref.code_size != 0x4da4 || !ref.toc || ref.toc_offset != 0xf4 ||
        ref.toc_size != 0x36c)
        return ROMlib_carbon_invoke_frame(ref.function, call);

    if (properties)
        return text_common_properties(&ref, operation, call);
    if (operation == text_nearest_mac)
        return text_common_nearest(&ref, call);
    if (operation == text_upgrade_script)
        return text_common_upgrade_script(&ref, call);
    if (operation == text_revert_script)
        return text_common_revert_script(&ref, call);
    if (operation == text_map_script_iso)
        return text_common_map_script(&ref, call);
    if (operation == text_encoding_name)
        return text_common_name(&ref, call);
    return text_common_resolve(&ref, call->words[0], &call->words[0]);
}

static const Byte* text_collation_key(uint32 address, uint32 count)
{
    const uint64 size = (uint64)count * 4;
    if (!address || size > (uint64)UINT32_MAX + 1 - address)
        return NULL;
    return ROMlib_carbon_try_guest_memory(address, size ? size : 1, 0);
}

int ROMlib_carbon_compare_collation_keys(ROMlib_carbon_ppc_frame* call)
{
    const uint32 left_count = call->words[1], right_count = call->words[3];
    const Byte* left = text_collation_key(call->words[0], left_count);
    const Byte* right = text_collation_key(call->words[2], right_count);
    Byte* equivalent = call->words[4] ? ROMlib_carbon_try_guest_memory(call->words[4], 1, 1) : NULL;
    Byte* order = call->words[5] ? ROMlib_carbon_try_guest_memory(call->words[5], 4, 1) : NULL;
    if (equivalent)
        *equivalent = true;
    if (order)
        memset(order, 0, 4);
    if (!left || !right || (!equivalent && !order) || (call->words[4] && !equivalent) || (call->words[5] && !order))
        return text_result(call, paramErr);

    /* The reference keys use 0 and 1 to delimit weight levels. A difference
       before both keys reach marker 1 also affects equivalence. */
    bool left_primary = true, right_primary = true;
    bool left_significant = true, right_significant = true;
    int32 result = 0;
    const uint32 common = left_count < right_count ? left_count : right_count;
    for (uint32 i = 0; i < common; ++i, left += 4, right += 4) {
        const uint32 a = text_table_long(left), b = text_table_long(right);
        left_primary &= a > 1;
        right_primary &= b > 1;
        left_significant &= a != 1;
        right_significant &= b != 1;
        if (a != b) {
            result = a < b ? -1 : 1;
            break;
        }
    }
    /* Preserve the classic prefix rule when one key has unmatched words. */
    if (!result && left_count != right_count)
        result = left_primary ? 1 : -1;
    if (result && equivalent)
        *equivalent = !left_significant && !right_significant;
    if (order) {
        const uint32 encoded = CL((uint32)(result * (left_primary || right_primary ? 2 : 1)));
        memcpy(order, &encoded, sizeof encoded);
    }
    return text_result(call, noErr);
}

#endif
