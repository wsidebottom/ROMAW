/*+===================================================================
  File:      locale_access.h

  Summary:   Native locale parsing, classic code mapping, operation data, and display names.

  Classes:   None.

  Functions: Static Locale Utilities frame entries and their shared table helpers.

  Origin:    CarbonLib 1.4, its LocalesLib.CL companion, the Mac OS 9.2.1 LocalesLib, and
             Universal Interfaces 3.4.2 MacLocales.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_LOCALE_ACCESS_H
#define ROMAW_CARBON_LOCALE_ACCESS_H

#include "cf_batch.h"
#include "ResourceMgr.h"

enum {
    locale_bad_data = -30002,
    locale_default_name = -30029,
    locale_short_buffer = -30001,
    locale_table_header = 8,
    locale_record_size = 16,
    locale_name_record_size = 96,
    locale_file_record_size = 70,
};

typedef struct {
    Byte bytes[12];
} carbon_locale_parts;

static const uint8_t locale_part_start[] = {0, 3, 6, 8, 9, 11, 12};
static const char locale_part_delimiter[] = {0, '-', '.', '-', '_', '-'};

static int locale_status(ROMlib_carbon_ppc_frame* call, int32 status)
{
    call->words[0] = status;
    return 1;
}

static uint16 locale_half(const Byte* bytes)
{
    uint16 word;
    memcpy(&word, bytes, sizeof word);
    return CW(word);
}

/* Keep the original Carbon initializer and its choice of system or bundled
   locale tables. LocaleRefs are record offsets into that live arena. */
static int locale_reference(const char* name, uint32 offset, ROMlib_carbon_reference* original,
                            ROMlib_carbon_reference* runtime)
{
    if (!ROMlib_carbon_probe_reference("CarbonLib", name, original))
        return 0;
    if (original->code_size != 0xb8fb4 || original->code_offset != offset || original->toc_offset != 0x2f78 ||
        original->toc_size != 0x10f0c || !original->toc)
        return 0;
    const uint32 toc = US_TO_SYN68K(original->toc);
    uint32 initialized_address;
    if (!cfp_get(toc - 0xfec, &initialized_address) || !initialized_address)
        return 0;
    Byte* initialized = ROMlib_carbon_try_guest_memory(initialized_address, 1, 1);
    if (!initialized)
        return 0;
    if (!*initialized) {
        uint32 state, strings, methods, version;
        if (!cfp_private(original, 0xb4f80, NULL, 0, &state) || !state || !cfp_get(toc - 0x1d4, &strings) || !strings ||
            !cfp_get(toc - 0xff0, &methods) || !methods)
            return 0;
        const uint32 arguments[] = {0x0910, state + 0x37, strings, strings + 0x0c, strings + 0x1b, 0x09106000, methods};
        if (!cfp_private(original, 0x325a0, arguments, NELEM(arguments), &version))
            return 0;
        *initialized = (Byte)version;
    }
    uint32 wrapper, instruction, slot, method;
    if (!*initialized || !cfp_get(US_TO_SYN68K(original->function), &wrapper) ||
        !cfp_get(wrapper + 0x20, &instruction) || (instruction & 0xfc1f0000) != 0x80020000)
        return 0;
    /* Offset 0x20 in each CarbonLib 1.4 wrapper loads its dispatch slot.
       Keep guest replacements of individual locale methods callable. */
    const int16 displacement = instruction;
    if (displacement < -0xfe8 || displacement > -0xfb0 || displacement % 4 || !cfp_get(toc + displacement, &slot) ||
        !slot || !cfp_get(slot, &method) || !method)
        return 0;

    for (unsigned system = 0; system < 2; ++system) {
        if (ROMlib_carbon_probe_reference(system ? "LocalesLib" : "LocalesLib.CL", name, runtime) &&
            US_TO_SYN68K(runtime->function) == method)
            return runtime->code_size == (system ? 0x1d4c : 0x217f) && runtime->toc_offset == (system ? 0x84 : 0x8c) &&
                   runtime->toc_size == 200;
    }
    return 0;
}

static int locale_fallback(const ROMlib_carbon_reference* original, ROMlib_carbon_ppc_frame* call)
{
    return original->function ? ROMlib_carbon_invoke_frame(original->function, call) : locale_status(call, -4);
}

static uint32 locale_word(const Byte* bytes)
{
    uint32 word;
    memcpy(&word, bytes, sizeof word);
    return CL(word);
}

static bool locale_system_runtime(const ROMlib_carbon_reference* runtime)
{
    return runtime->code_size == 0x1d4c;
}

static uint32 locale_operation_stride(const ROMlib_carbon_reference* runtime)
{
    return locale_system_runtime(runtime) ? 24 : 28;
}

static void locale_store_word(Byte* bytes, uint32 value)
{
    const uint32 encoded = CL(value);
    memcpy(bytes, &encoded, sizeof encoded);
}

static Byte* locale_table(const ROMlib_carbon_reference* runtime, uint32 toc_displacement, uint32* handle,
                          uint32* extent, bool update)
{
    uint32 slot;
    if (!cfp_get(US_TO_SYN68K(runtime->toc) - toc_displacement, &slot) || !slot || !cfp_get(slot, handle))
        return NULL;
    Byte* table = cfp_handle_data(*handle, extent, update);
    return table && *extent >= locale_table_header ? table : NULL;
}

static OSErr locale_parse(uint32 address, carbon_locale_parts* parts)
{
    static const bool uppercase[] = {0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1};
    unsigned part = 0, position = 0;
    memset(parts, 0, sizeof(*parts));
    for (unsigned i = 0; i <= sizeof parts->bytes + NELEM(locale_part_delimiter) - 1; ++i) {
        const Byte* input =
            address && i <= UINT32_MAX - address ? ROMlib_carbon_try_guest_memory(address + i, 1, 0) : NULL;
        if (!input)
            return paramErr;
        const Byte ch = *input;
        if (!ch)
            return noErr;
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) {
            if (position >= locale_part_start[part + 1])
                return paramErr;
            parts->bytes[position] = (ch & ~0x20) | (uppercase[position] ? 0 : 0x20);
            ++position;
        }
        else {
            do {
                ++part;
            } while (part < NELEM(locale_part_delimiter) && locale_part_delimiter[part] != ch);
            if (part == NELEM(locale_part_delimiter))
                return paramErr;
            position = locale_part_start[part];
        }
    }
    return paramErr;
}

static uint32 locale_intern(const ROMlib_carbon_reference* runtime, const carbon_locale_parts* parts)
{
    uint32 handle, extent;
    Byte* table = locale_table(runtime, 0x14, &handle, &extent, true);
    if (!table)
        return 0;
    const uint32 count = locale_word(table), capacity = locale_word(table + 4);
    if (count > capacity || capacity > (extent - locale_table_header) / locale_record_size)
        return 0;
    const uint32 end = locale_table_header + count * locale_record_size;
    for (uint32 offset = locale_table_header; offset < end; offset += locale_record_size)
        if (!memcmp(table + offset, parts, sizeof(*parts)))
            return offset;

    if (count == capacity) {
        if (capacity > (INT32_MAX - locale_table_header) / locale_record_size - 32)
            return 0;
        const uint32 size = locale_table_header + (capacity + 32) * locale_record_size;
        SetHandleSize((Handle)SYN68K_TO_US(handle), size);
        if (MemError())
            return 0;
        table = cfp_handle_data(handle, &extent, 1);
        if (!table || extent < size)
            return 0;
        locale_store_word(table + 4, capacity + 32);
    }
    memcpy(table + end, parts, sizeof(*parts));
    locale_store_word(table + end + sizeof(*parts), UINT32_MAX);
    locale_store_word(table, count + 1);
    return end;
}

/* The classic maps contain fixed-width locale parts and omit fields that do
   not constrain a match. They belong to the active system or Carbon file. */
static bool locale_parts_match(const Byte* pattern, const Byte* value)
{
    for (unsigned part = 0; part < NELEM(locale_part_delimiter); ++part) {
        const unsigned start = locale_part_start[part];
        if (pattern[start] && memcmp(pattern + start, value + start, locale_part_start[part + 1] - start))
            return false;
    }
    return true;
}

static const Byte* locale_resource_data(Handle resource, uint32* extent)
{
    if (!resource)
        return NULL;
    if (!STARH(resource))
        C_LoadResource(resource);
    return cfp_handle_data(US_TO_SYN68K(resource), extent, false);
}

static const Byte* locale_map(const ROMlib_carbon_reference* runtime, int16 resource_id, uint16 version, uint32 stride,
                              uint32* count, OSErr* status)
{
    Handle resource;
    if (locale_system_runtime(runtime))
        resource = C_GetResource(TICK("lmap"), resource_id);
    else {
        uint32 state_handle, state;
        if (!cfp_get(US_TO_SYN68K(runtime->toc) + 0x94, &state_handle) || !state_handle ||
            !cfp_get(state_handle, &state) || !state) {
            *status = resNotFound;
            return NULL;
        }
        const Byte* file = ROMlib_carbon_try_guest_memory(state + 8, 2, 0);
        if (!file) {
            *status = resNotFound;
            return NULL;
        }
        const int16 previous = C_CurResFile();
        C_UseResFile((int16)locale_half(file));
        *status = C_ResError();
        resource = *status ? NULL : C_Get1Resource(TICK("lmap"), resource_id);
        C_UseResFile(previous);
        if (*status)
            return NULL;
    }

    uint32 extent;
    const Byte* data = locale_resource_data(resource, &extent);
    if (!data) {
        *status = resNotFound;
        return NULL;
    }
    if (extent < locale_table_header || locale_half(data) != version ||
        locale_word(data + 4) > (extent - locale_table_header) / stride) {
        *status = locale_bad_data;
        return NULL;
    }
    *status = noErr;
    *count = locale_word(data + 4);
    return data + locale_table_header;
}

static OSErr locale_get_code_parts(const ROMlib_carbon_reference* runtime, int16 code, int16 resource_id,
                                   carbon_locale_parts* parts)
{
    uint32 count;
    OSErr status;
    const Byte* table = locale_map(runtime, resource_id, 1, sizeof(*parts), &count, &status);
    if (!table)
        return status;
    if ((uint32)code >= count)
        return paramErr;
    memcpy(parts, table + (uint32)code * sizeof(*parts), sizeof(*parts));
    return noErr;
}

static int carbon_LocaleRefFromLangOrRegionCode(ROMlib_carbon_ppc_frame* call)
{
    const int16 language = call->words[0], region = call->words[1];
    Byte* output = call->words[2] ? ROMlib_carbon_try_guest_memory(call->words[2], 4, 1) : NULL;
    if (!output)
        return locale_status(call, paramErr);
    locale_store_word(output, 0);
    if ((language < 0 && (language != -128 || region < 0)) || (region < 0 && region != -128))
        return locale_status(call, paramErr);

    ROMlib_carbon_reference original, runtime;
    if (!locale_reference("LocaleRefFromLangOrRegionCode", 0x3141c, &original, &runtime))
        return locale_fallback(&original, call);

    carbon_locale_parts language_parts = {0}, region_parts;
    OSErr status = language < 0 ? noErr : locale_get_code_parts(&runtime, language, 128, &language_parts);
    if (!status && region >= 0)
        status = locale_get_code_parts(&runtime, region, 129, &region_parts);
    if (status)
        return locale_status(call, status);
    if (language >= 0 && region >= 0 && !locale_parts_match(language_parts.bytes, region_parts.bytes))
        return locale_status(call, paramErr);
    const carbon_locale_parts* parts = region >= 0 ? &region_parts : &language_parts;
    if (parts->bytes[0] || parts->bytes[6] || parts->bytes[9])
        locale_store_word(output, locale_intern(&runtime, parts));
    return locale_status(call, noErr);
}

static int carbon_LocaleStringToLangAndRegionCodes(ROMlib_carbon_ppc_frame* call)
{
    const uint32 language_address = call->words[1], region_address = call->words[2];
    Byte* language = language_address ? ROMlib_carbon_try_guest_memory(language_address, 2, 1) : NULL;
    Byte* region = region_address ? ROMlib_carbon_try_guest_memory(region_address, 2, 1) : NULL;
    const uint16 unspecified = CWC((uint16)-128);
    if (language)
        memcpy(language, &unspecified, 2);
    if (region)
        memcpy(region, &unspecified, 2);
    if ((!language && !region) || (language_address && !language) || (region_address && !region))
        return locale_status(call, paramErr);
    carbon_locale_parts parts;
    const OSErr parse_status = locale_parse(call->words[0], &parts);
    if (parse_status)
        return locale_status(call, parse_status);

    ROMlib_carbon_reference original, runtime;
    if (!locale_reference("LocaleStringToLangAndRegionCodes", 0x31638, &original, &runtime))
        return locale_fallback(&original, call);
    uint32 count;
    OSErr status;
    const Byte* table = locale_map(&runtime, 130, 2, locale_record_size, &count, &status);
    if (!table)
        return locale_status(call, status);

    /* The reverse map is ordered by the first two bytes of the language. */
    const uint16 prefix = locale_half(parts.bytes);
    for (uint32 i = 0; i < count; ++i, table += locale_record_size) {
        const uint16 candidate = locale_half(table);
        if (candidate > prefix)
            break;
        if (candidate == prefix && locale_parts_match(table, parts.bytes)) {
            if (language)
                memcpy(language, table + sizeof parts, 2);
            if (region)
                memcpy(region, table + sizeof parts + 2, 2);
            return locale_status(call, noErr);
        }
    }
    return locale_status(call, paramErr);
}

static int carbon_LocaleGetRegionLanguageName(ROMlib_carbon_ppc_frame* call)
{
    const int16 region = call->words[0];
    const uint32 address = call->words[1];
    Byte* first = address ? ROMlib_carbon_try_guest_memory(address, 1, 1) : NULL;
    if (region < 0 || !first)
        return locale_status(call, paramErr);
    ROMlib_carbon_reference original, runtime;
    if (!locale_reference("LocaleGetRegionLanguageName", 0x31ac0, &original, &runtime))
        return locale_fallback(&original, call);
    *first = 0;

    uint32 remaining;
    const Byte* entry = locale_resource_data(C_GetResource(TICK("STR#"), -20957), &remaining);
    if (!entry || remaining < 2 || (uint16)region >= locale_half(entry))
        return locale_status(call, resNotFound);
    entry += 2;
    remaining -= 2;
    for (int16 i = 0; i < region; ++i) {
        if (!remaining || remaining <= *entry)
            return locale_status(call, resNotFound);
        remaining -= 1 + *entry;
        entry += 1 + *entry;
    }
    if (!remaining || !*entry || remaining <= *entry)
        return locale_status(call, resNotFound);
    Byte* output = ROMlib_carbon_try_guest_memory(address, 1 + *entry, 1);
    if (!output)
        return locale_status(call, paramErr);
    memcpy(output, entry, 1 + *entry);
    return locale_status(call, noErr);
}

static int carbon_LocaleRefFromLocaleString(ROMlib_carbon_ppc_frame* call)
{
    Byte* output = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], 4, 1) : NULL;
    if (!output)
        return locale_status(call, paramErr);
    ROMlib_carbon_reference original, runtime;
    if (!locale_reference("LocaleRefFromLocaleString", 0x314d0, &original, &runtime))
        return locale_fallback(&original, call);
    carbon_locale_parts parts;
    const OSErr error = locale_parse(call->words[0], &parts);
    locale_store_word(output, error ? 0 : locale_intern(&runtime, &parts));
    return locale_status(call, error);
}

static const Byte* locale_record(const ROMlib_carbon_reference* runtime, uint32 locale)
{
    uint32 handle, extent;
    const Byte* table = locale_table(runtime, 0x14, &handle, &extent, false);
    if (!table || extent < locale_table_header + locale_record_size || locale < locale_table_header ||
        (locale - locale_table_header) % locale_record_size || locale > extent - locale_record_size ||
        (locale - locale_table_header) / locale_record_size >= locale_word(table))
        return NULL;
    return table + locale;
}

static int carbon_LocaleRefGetPartString(ROMlib_carbon_ppc_frame* call)
{
    const uint32 capacity = call->words[2], address = call->words[3];
    char* first = address ? ROMlib_carbon_try_guest_memory(address, 1, 1) : NULL;
    if (!first)
        return locale_status(call, paramErr);
    ROMlib_carbon_reference original, runtime;
    if (!locale_reference("LocaleRefGetPartString", 0x3157c, &original, &runtime) || !call->words[0])
        return locale_fallback(&original, call);

    *first = 0;
    const Byte* record = locale_record(&runtime, call->words[0]);
    if (!record)
        return locale_status(call, paramErr);
    const uint32 mask = call->words[1];
    char formatted[18] = {0};
    size_t used = 0;
    bool previous = true;
    OSErr status = noErr;
    for (unsigned part = 0; part < NELEM(locale_part_delimiter); ++part) {
        const unsigned start = locale_part_start[part];
        const bool present = (mask & (1u << part)) && record[start];
        if (present) {
            char token[6];
            size_t length = 0;
            if ((part & 1) && !previous && locale_part_delimiter[part - 1])
                token[length++] = locale_part_delimiter[part - 1];
            if (locale_part_delimiter[part])
                token[length++] = locale_part_delimiter[part];
            for (unsigned i = start; i < locale_part_start[part + 1] && record[i]; ++i)
                token[length++] = record[i];
            if (used + length >= capacity) {
                status = locale_short_buffer;
                break;
            }
            memcpy(formatted + used, token, length);
            used += length;
        }
        previous = present;
    }
    char* output = ROMlib_carbon_try_guest_memory(address, used + 1, 1);
    if (!output)
        return locale_status(call, paramErr);
    memcpy(output, formatted, used + 1);
    return locale_status(call, status);
}

static int locale_store_variant(uint32 list, uint32 index, uint32 locale, uint32 variant)
{
    const uint64 address = (uint64)list + (uint64)index * 8;
    Byte* output = address <= UINT32_MAX - 7 ? ROMlib_carbon_try_guest_memory(address, 8, 1) : NULL;
    if (!output)
        return 0;
    locale_store_word(output, locale);
    locale_store_word(output + 4, variant);
    return 1;
}

static int locale_enumerate(ROMlib_carbon_ppc_frame* call, bool list)
{
    const uint32_t klass = call->words[0], capacity = call->words[1];
    const uint32_t count_address = call->words[list ? 2 : 1];
    Byte* count_output = count_address ? ROMlib_carbon_try_guest_memory(count_address, 4, 1) : NULL;
    if (!count_output)
        return locale_status(call, paramErr);
    locale_store_word(count_output, 0);
    if (!klass || (list && !call->words[3]))
        return locale_status(call, paramErr);
    ROMlib_carbon_reference original, runtime;
    if (!locale_reference(list ? "LocaleOperationGetLocales" : "LocaleOperationCountLocales", list ? 0x31798 : 0x316ec,
                          &original, &runtime))
        return locale_fallback(&original, call);
    uint32 handle, extent, operation_extent;
    const Byte* table = locale_table(&runtime, 0x14, &handle, &extent, false);
    const Byte* operations = locale_table(&runtime, 0x1c, &handle, &operation_extent, false);
    if (!table || !operations)
        return locale_status(call, paramErr);
    const uint32 count = locale_word(table);
    const uint32 stride = locale_operation_stride(&runtime);
    const uint32 available = locale_word(operations);
    if (available > (operation_extent - locale_table_header) / stride)
        return locale_status(call, paramErr);
    if (!count || count > (extent - locale_table_header) / locale_record_size)
        return locale_status(call, paramErr);

    uint32 found = 0;
    for (uint32 i = 1; i < count; ++i) {
        const uint32 locale = locale_table_header + i * locale_record_size;
        const Byte* record = table + locale;
        int32 index = (int32)locale_word(record + 12);
        if (record[9] || index < 0)
            continue;
        const uint32 before = found;
        for (uint32 visited = 0; index >= 0 && visited < available; ++visited) {
            if ((uint32)index >= available)
                return locale_status(call, paramErr);
            const Byte* operation = operations + locale_table_header + (uint32)index * stride;
            if (locale_word(operation + 4) == klass) {
                if (list && found < capacity &&
                    !locale_store_variant(call->words[3], found, locale, locale_word(operation + 8)))
                    return locale_status(call, paramErr);
                ++found;
            }
            index = (int32)locale_word(operation);
        }
        if (index >= 0)
            return locale_status(call, paramErr);
        if (found == before) {
            if (list && found < capacity && !locale_store_variant(call->words[3], found, locale, 0))
                return locale_status(call, paramErr);
            ++found;
        }
    }
    locale_store_word(count_output, list && found > capacity ? capacity : found);
    return locale_status(call, list && found > capacity ? locale_short_buffer : noErr);
}

static int carbon_LocaleOperationCountLocales(ROMlib_carbon_ppc_frame* call)
{
    return locale_enumerate(call, false);
}

static int carbon_LocaleOperationGetLocales(ROMlib_carbon_ppc_frame* call)
{
    return locale_enumerate(call, true);
}

static OSErr locale_current_record(const ROMlib_carbon_reference* runtime, uint32 locale, const Byte** record)
{
    if (!locale) {
        ROMlib_carbon_ppc_frame script = {0};
        script.words[0] = 0x28; /* Classic Script Manager region code. */
        if (!carbon_vector_GetScriptManagerVariable(&script))
            return paramErr;
        carbon_locale_parts parts;
        const int16 region = script.words[0];
        const OSErr status = region < 0 ? paramErr : locale_get_code_parts(runtime, region, 129, &parts);
        if (status)
            return status;
        if (parts.bytes[0] || parts.bytes[6] || parts.bytes[9])
            locale = locale_intern(runtime, &parts);
    }
    *record = locale_record(runtime, locale);
    return *record ? noErr : paramErr;
}

static Byte* locale_operations_table(const ROMlib_carbon_reference* runtime, uint32* count)
{
    uint32 handle, extent;
    Byte* table = locale_table(runtime, 0x1c, &handle, &extent, true);
    if (!table || locale_word(table) > (extent - locale_table_header) / locale_operation_stride(runtime))
        return NULL;
    *count = locale_word(table);
    return table + locale_table_header;
}

static int16 locale_open_operation_file(const ROMlib_carbon_reference* runtime, int16 index)
{
    if (index < 0)
        return 0;
    uint32 handle, extent;
    if (!cfp_get(US_TO_SYN68K(runtime->toc) + 0x94, &handle))
        return -1;
    const Byte* table = cfp_handle_data(handle, &extent, false);
    if (!table || extent < locale_table_header || (uint16)index >= locale_word(table) ||
        (uint16)index >= (extent - locale_table_header) / locale_file_record_size)
        return -1;
    const Byte* entry = table + locale_table_header + (uint16)index * locale_file_record_size;
    if (!index && !locale_system_runtime(runtime))
        return (int16)locale_half(entry);
    FSSpec file;
    memcpy(&file, entry, sizeof file);
    return C_FSpOpenResFile(&file, fsRdPerm);
}

static OSErr locale_load_operation(const ROMlib_carbon_reference* runtime, uint32 klass, uint32 locale, uint32 variant,
                                   uint32* token, uint32* data)
{
    const Byte* record;
    OSErr status = locale_current_record(runtime, locale, &record);
    if (status)
        return status;
    int32 index = locale_word(record + 12);
    const unsigned stride = locale_operation_stride(runtime);
    uint32 count;
    Byte* operations = locale_operations_table(runtime, &count);
    if (!operations)
        return paramErr;

    Byte* entry = NULL;
    for (uint32 visited = 0; index >= 0 && (uint32)index < count && visited < count; ++visited) {
        Byte* candidate = operations + (uint32)index * stride;
        if (locale_word(candidate + 4) == klass && (!variant || locale_word(candidate + 8) == variant)) {
            entry = candidate;
            break;
        }
        index = locale_word(candidate);
    }
    if (!entry)
        return paramErr;
    const uint32 refs = locale_word(entry + stride - 8);
    if (refs) {
        const uint32 handle = locale_word(entry + stride - 4);
        if (!handle || refs == UINT32_MAX)
            return locale_bad_data;
        locale_store_word(entry + stride - 8, refs + 1);
        *token = index;
        *data = handle;
        return noErr;
    }

    const int16 file_index = locale_half(entry + 12);
    const int16 resource_id = locale_half(entry + 14);
    const int16 previous = C_CurResFile();
    const int16 file = locale_open_operation_file(runtime, file_index);
    if (file == -1)
        return noErr;
    C_UseResFile(file);
    status = C_ResError();
    Handle resource = NULL;
    if (!status) {
        resource = C_Get1Resource(klass, resource_id);
        status = C_ResError();
        if (!status && !resource)
            status = resNotFound;
        if (!status && !STARH(resource)) {
            C_LoadResource(resource);
            status = C_ResError();
        }
        if (!status) {
            C_DetachResource(resource);
            status = C_ResError();
        }
    }
    if (file != previous && file != 0 && (file_index != 0 || locale_system_runtime(runtime)))
        C_CloseResFile(file);
    if (previous != -1)
        C_UseResFile(previous);
    if (status)
        return status;

    operations = locale_operations_table(runtime, &count);
    if (!operations || (uint32)index >= count) {
        DisposHandle(resource);
        return locale_bad_data;
    }
    entry = operations + (uint32)index * stride;
    *token = index;
    *data = US_TO_SYN68K(resource);
    locale_store_word(entry + stride - 4, *data);
    locale_store_word(entry + stride - 8, 1);
    return noErr;
}

static OSErr locale_unload_operation(const ROMlib_carbon_reference* runtime, uint32 token, uint32 data)
{
    uint32 count;
    Byte* operations = locale_operations_table(runtime, &count);
    if (!data || !operations || token >= count)
        return paramErr;
    const unsigned stride = locale_operation_stride(runtime);
    Byte* entry = operations + token * stride;
    const uint32 refs = locale_word(entry + stride - 8);
    if (!refs || data != locale_word(entry + stride - 4))
        return paramErr;
    locale_store_word(entry + stride - 8, refs - 1);
    if (refs == 1) {
        locale_store_word(entry + stride - 4, 0);
        DisposHandle((Handle)SYN68K_TO_US(data));
    }
    return noErr;
}

static Byte* locale_output(uint32 address, uint32 size)
{
    return address && address <= UINT32_MAX - (size ? size - 1 : 0) ? ROMlib_carbon_try_guest_memory(address, size, 1)
                                                                    : NULL;
}

static int carbon_LocaleOperationLoadData(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference original, runtime;
    if (!locale_reference("LocaleOperationLoadData", 0x31da8, &original, &runtime))
        return locale_fallback(&original, call);
    Byte* token_output = locale_output(call->words[3], 4);
    Byte* data_output = locale_output(call->words[4], 4);
    uint32 token = UINT32_MAX, data = 0;
    const OSErr status = token_output && data_output ? locale_load_operation(&runtime, call->words[0], call->words[1],
                                                                             call->words[2], &token, &data)
                                                     : paramErr;
    if (token_output)
        locale_store_word(token_output, token);
    if (data_output)
        locale_store_word(data_output, data);
    return locale_status(call, status);
}

static int carbon_LocaleOperationUnloadData(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference original, runtime;
    if (!locale_reference("LocaleOperationUnloadData", 0x31e6c, &original, &runtime))
        return locale_fallback(&original, call);
    Byte* output = locale_output(call->words[1], 4);
    if (!output)
        return locale_status(call, paramErr);
    const OSErr status = locale_unload_operation(&runtime, call->words[0], locale_word(output));
    if (!status)
        locale_store_word(output, 0);
    return locale_status(call, status);
}

typedef struct {
    uint32_t token;
    uint32_t data;
} locale_operation_resource;

static int locale_acquire_operation_resource(uint32_t stack, uint32_t operation, uint32_t locale, uint32_t variant,
                                             locale_operation_resource* resource)
{
    ROMlib_carbon_ppc_frame call = {
        .words = {operation, locale, variant, US_TO_SYN68K(&resource->token), US_TO_SYN68K(&resource->data)},
        .stack = stack};
    return carbon_LocaleOperationLoadData(&call) ? (int32_t)call.words[0] : resNotFound;
}

static int locale_release_operation_resource(uint32_t stack, locale_operation_resource resource)
{
    if (!resource.data)
        return noErr;
    _Alignas(16) uint32_t handle = resource.data;
    ROMlib_carbon_ppc_frame call = {.words = {CL(resource.token), US_TO_SYN68K(&handle)}, .stack = stack};
    return carbon_LocaleOperationUnloadData(&call) ? (int32_t)call.words[0] : resNotFound;
}

enum locale_name_query { locale_name_lookup, locale_name_count, locale_name_index };

static OSErr locale_read_names(const ROMlib_carbon_reference* runtime, uint32 data, enum locale_name_query query,
                               uint32 selector, uint32 maximum, uint32 name_address, Byte* length, Byte* display)
{
    carbon_locale_parts display_parts;
    bool have_display = false;
    if (query == locale_name_lookup) {
        const Byte* record;
        if (!locale_current_record(runtime, selector, &record)) {
            memcpy(&display_parts, record, sizeof display_parts);
            have_display = true;
        }
    }
    uint32 extent;
    const Byte* table = cfp_handle_data(data, &extent, false);
    if (!table || extent < locale_table_header || locale_half(table) != 1)
        return locale_bad_data;
    const uint32 count = locale_word(table + 4);
    if (count > (extent - locale_table_header) / locale_name_record_size)
        return locale_bad_data;
    if (query == locale_name_count) {
        locale_store_word(length, count);
        return noErr;
    }
    table += locale_table_header;
    const Byte* entry = table;
    OSErr status = noErr;
    if (query == locale_name_index) {
        if (selector >= count)
            return paramErr;
        entry += selector * locale_name_record_size;
    }
    else {
        if (!count)
            return have_display ? noErr : paramErr;
        uint32 index = 0;
        if (have_display)
            while (index < count && !locale_parts_match(entry, display_parts.bytes)) {
                entry += locale_name_record_size;
                ++index;
            }
        if (!have_display || index == count) {
            entry = table;
            status = locale_default_name;
        }
    }
    const uint32 characters = locale_word(entry + sizeof display_parts);
    if (characters > (locale_name_record_size - locale_record_size) / 2)
        return locale_bad_data;
    if (characters > maximum)
        return locale_short_buffer;
    if (characters) {
        Byte* name = locale_output(name_address, characters * 2);
        if (!name)
            return paramErr;
        memmove(name, entry + locale_record_size, characters * 2);
    }
    locale_store_word(length, characters);
    if (display) {
        memcpy(&display_parts, entry, sizeof display_parts);
        locale_store_word(display, locale_intern(runtime, &display_parts));
    }
    return status;
}

static int locale_name(ROMlib_carbon_ppc_frame* call, const char* function_name, uint32 offset,
                       enum locale_name_query query, bool operation)
{
    ROMlib_carbon_reference original, runtime;
    if (!locale_reference(function_name, offset, &original, &runtime))
        return locale_fallback(&original, call);

    /* Operation-class names use the first locale and the class as its variant.
       The Classic fragments look up the variant regardless of the name mask. */
    const uint32 locale = operation ? locale_table_header : call->words[0];
    const uint32 variant = call->words[operation ? 0 : 1];
    const unsigned tail = operation ? 1 : 3;
    const bool count_only = query == locale_name_count;
    Byte* length = locale_output(call->words[tail + (count_only ? 0 : 2)], 4);
    const uint32 name = count_only ? 0 : call->words[tail + 3];
    Byte* display = query == locale_name_index ? locale_output(call->words[tail + 4], 4) : NULL;
    if (!length || (!count_only && !locale_output(name, 2)) || (query == locale_name_index && !display))
        return locale_status(call, paramErr);
    locale_store_word(length, 0);
    if (display)
        locale_store_word(display, 0);

    uint32 token = UINT32_MAX, data = 0;
    OSErr status = locale_load_operation(&runtime, TICK("lnam"), locale, variant, &token, &data);
    if (!status && data) {
        status =
            locale_read_names(&runtime, data, query, call->words[tail], call->words[tail + 1], name, length, display);
        locale_unload_operation(&runtime, token, data);
    }
    else if (!status)
        status = resNotFound;
    return locale_status(call, status);
}

static int carbon_LocaleGetName(ROMlib_carbon_ppc_frame* call)
{
    return locale_name(call, "LocaleGetName", 0x31854, locale_name_lookup, false);
}

static int carbon_LocaleCountNames(ROMlib_carbon_ppc_frame* call)
{
    return locale_name(call, "LocaleCountNames", 0x31928, locale_name_count, false);
}

static int carbon_LocaleGetIndName(ROMlib_carbon_ppc_frame* call)
{
    return locale_name(call, "LocaleGetIndName", 0x319e4, locale_name_index, false);
}

static int carbon_LocaleOperationGetName(ROMlib_carbon_ppc_frame* call)
{
    return locale_name(call, "LocaleOperationGetName", 0x31b6c, locale_name_lookup, true);
}

static int carbon_LocaleOperationCountNames(ROMlib_carbon_ppc_frame* call)
{
    return locale_name(call, "LocaleOperationCountNames", 0x31c30, locale_name_count, true);
}

static int carbon_LocaleOperationGetIndName(ROMlib_carbon_ppc_frame* call)
{
    return locale_name(call, "LocaleOperationGetIndName", 0x31cdc, locale_name_index, true);
}

#endif
