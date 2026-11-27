/*+===================================================================
  File:      unicode_collators.h

  Summary:   Manage guest Unicode collators and construct sort keys.

  Classes:   None.

  Functions: Private Unicode collator lifecycle and collation key frame adapters.

  Origin:    CarbonLib 1.4, its UnicodeUtilitiesLib.CL companion, and Universal Interfaces
             3.4.2 UnicodeUtilities.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_UNICODE_COLLATORS_H
#define ROMAW_CARBON_UNICODE_COLLATORS_H

#include "locale_access.h"
#include "text_conversion.h"
#include "toolbox_vectors.h"
#include <stdlib.h>

enum {
    collator_header_size = 40,
    collator_row_size = 12,
    collator_table_header = 20,
    collator_table_row = 8,
};

#define COLLATOR_RESOURCE TICK("ucol")

static const Byte* collator_table(locale_operation_resource resource, uint32_t* count)
{
    uint32_t extent;
    const Byte* table = cfp_handle_data(CL(resource.data), &extent, 0);
    if (!table || extent < collator_table_header)
        return NULL;
    *count = locale_word(table + 16);
    return *count <= (extent - collator_table_header) / collator_table_row ? table : NULL;
}

static int collator_reference(const char* name, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference reference;
    if (ROMlib_carbon_reference_function("CarbonLib", name, &reference) ||
        ROMlib_carbon_load_reference("UnicodeUtilitiesLib.CL", name, &reference))
        return ROMlib_carbon_invoke_frame(reference.function, call);
    return 0;
}

static uint32_t collator_system_locale(uint32_t stack)
{
    ROMlib_carbon_ppc_frame script_query = {.words = {18}};
    if (!carbon_vector_GetScriptManagerVariable(&script_query))
        return 0;
    const int16_t script = script_query.words[0];
    if (script != 0 && script != 4 && script != 7 && script != 29)
        return 0;

    ROMlib_carbon_ppc_frame region_query = {.words = {script, 20}};
    if (!carbon_vector_GetScriptVariable(&region_query))
        return 0;
    int16_t region = region_query.words[0];
    if (script) {
        static const int16_t compatible_regions[][2] = {
            {0x4600, 16}, {0x4628, 48}, {0x4c00, 49}, {0x4c3e, 62},
            {0x4c48, 72}, {0x782a, 42}, {0x782b, 43}, {0x7838, 56},
        };
        int16_t mapped = -1;
        for (unsigned i = 0; i < NELEM(compatible_regions); ++i) {
            if (compatible_regions[i][0] == region) {
                mapped = compatible_regions[i][1];
                break;
            }
        }
        region = mapped;
    }
    if (region < 0)
        return 0;
    _Alignas(16) uint32_t locale = 0;
    ROMlib_carbon_ppc_frame query = {.words = {(uint32_t)-128, region, US_TO_SYN68K(&locale)}, .stack = stack};
    return carbon_LocaleRefFromLangOrRegionCode(&query) && !query.words[0] ? CL(locale) : 0;
}

static int collator_release(uint32_t stack, uint32_t handle)
{
    uint32_t extent;
    const Byte* collator = cfp_handle_data(handle, &extent, 0);
    if (!collator || extent < collator_header_size || locale_word(collator) != COLLATOR_RESOURCE)
        return paramErr;
    const locale_operation_resource root = {CL(locale_word(collator + 12)), CL(locale_word(collator + 4))};
    const locale_operation_resource locale = {CL(locale_word(collator + 16)), CL(locale_word(collator + 8))};
    _Alignas(16) uint32_t properties = CL(locale_word(collator + 36));
    const int root_status = locale_release_operation_resource(stack, root);
    const int locale_status_code = locale_release_operation_resource(stack, locale);
    int status = root_status ? root_status : locale_status_code;
    if (properties) {
        ROMlib_carbon_ppc_frame unload = {.words = {2, US_TO_SYN68K(&properties)}, .stack = stack};
        if (carbon_text_UCUnloadCharPropsTablePriv(&unload))
            status = unload.words[0];
    }
    ROMlib_carbon_ppc_frame dispose = {.words = {handle}};
    carbon_vector_DisposeHandle(&dispose);
    return status;
}

static int carbon_UCCreateCollator(ROMlib_carbon_ppc_frame* call)
{
    uint32_t locale = call->words[0], variant = call->words[1], options = call->words[2], output = call->words[3];
    if (!locale_output(output, 4))
        return locale_status(call, paramErr);
    if (!cfp_write(output, 0))
        return 0;
    _Alignas(16) char default_name[16] = "";
    _Alignas(16) uint32_t root_word = 0;
    ROMlib_carbon_ppc_frame root_query = {.words = {US_TO_SYN68K(default_name), US_TO_SYN68K(&root_word)},
                                          .stack = call->stack};
    if (!carbon_LocaleRefFromLocaleString(&root_query))
        return 0;
    if (root_query.words[0])
        return locale_status(call, root_query.words[0]);
    if (!locale)
        locale = collator_system_locale(call->stack);
    const uint32_t root_locale = CL(root_word);

    _Alignas(16) locale_operation_resource root = {0}, tailored = {0};
    const int status = locale_acquire_operation_resource(call->stack, COLLATOR_RESOURCE, root_locale, variant, &root);
    if (status)
        return locale_status(call, status);
    if (locale != root_locale)
        locale_acquire_operation_resource(call->stack, COLLATOR_RESOURCE, locale, variant, &tailored);

    uint32_t root_rows, tailored_rows = 0;
    const Byte* base = collator_table(root, &root_rows);
    if (!base || root_rows > (INT32_MAX - collator_header_size) / collator_row_size) {
        locale_release_operation_resource(call->stack, root);
        locale_release_operation_resource(call->stack, tailored);
        return locale_status(call, paramErr);
    }
    const uint32_t requested = collator_header_size + root_rows * collator_row_size;
    ROMlib_carbon_ppc_frame allocation = {.words = {requested}};
    const int allocated = carbon_vector_NewHandleClear(&allocation);
    const uint32_t handle = allocated ? allocation.words[0] : 0;
    if (!handle) {
        const int error = CW(MemErr);
        locale_release_operation_resource(call->stack, root);
        locale_release_operation_resource(call->stack, tailored);
        return allocated ? locale_status(call, error ? error : memFullErr) : 0;
    }
    uint32_t extent;
    Byte* collator = cfp_handle_data(handle, &extent, 1);
    base = collator_table(root, &root_rows);
    const Byte* local = tailored.data ? collator_table(tailored, &tailored_rows) : NULL;
    if (!collator || extent < requested || !base ||
        root_rows != (requested - collator_header_size) / collator_row_size || (tailored.data && !local)) {
        ROMlib_carbon_ppc_frame dispose = {.words = {handle}};
        carbon_vector_DisposeHandle(&dispose);
        locale_release_operation_resource(call->stack, root);
        locale_release_operation_resource(call->stack, tailored);
        return locale_status(call, paramErr);
    }

    const Byte* properties = local ? local : base;
    locale_store_word(collator, COLLATOR_RESOURCE);
    locale_store_word(collator + 4, CL(root.data));
    locale_store_word(collator + 8, CL(tailored.data));
    locale_store_word(collator + 12, CL(root.token));
    locale_store_word(collator + 16, CL(tailored.token));
    memcpy(collator + 20, properties + 4, 8);
    locale_store_word(collator + 28, options | (local ? locale_word(local + 12) : 0));
    locale_store_word(collator + 32, root_rows);

    unsigned tailored_index = 0;
    for (unsigned i = 0; i < root_rows; ++i) {
        const Byte* row = base + collator_table_header + i * collator_table_row;
        const Byte* candidate =
            tailored_index < tailored_rows ? local + collator_table_header + tailored_index * collator_table_row : NULL;
        const int use_tailoring = candidate && locale_half(row) == locale_half(candidate);
        Byte* slot = collator + collator_header_size + i * collator_row_size;
        memcpy(slot, use_tailoring ? candidate : row, collator_table_row);
        locale_store_word(slot + 8, CL(use_tailoring ? tailored.data : root.data));
        tailored_index += use_tailoring;
    }

    _Alignas(16) uint32_t property_handle = 0;
    ROMlib_carbon_ppc_frame load = {.words = {2, US_TO_SYN68K(&property_handle)}, .stack = call->stack};
    const int loaded = carbon_text_UCLoadCharPropsTablePriv(&load);
    if (loaded && !load.words[0]) {
        collator = cfp_handle_data(handle, &extent, 1);
        if (collator && extent >= requested) {
            locale_store_word(collator + 36, CL(property_handle));
            if (cfp_write(output, handle))
                return locale_status(call, noErr);
        }
        else {
            ROMlib_carbon_ppc_frame unload = {.words = {2, US_TO_SYN68K(&property_handle)}, .stack = call->stack};
            carbon_text_UCUnloadCharPropsTablePriv(&unload);
        }
    }
    collator_release(call->stack, handle);
    return loaded && load.words[0] ? locale_status(call, load.words[0]) : 0;
}

static int carbon_UCDisposeCollator(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t output = call->words[0];
    if (!locale_output(output, 4))
        return locale_status(call, paramErr);
    uint32_t handle, extent;
    if (!cfp_get(output, &handle))
        return 0;
    if (!handle)
        return locale_status(call, paramErr);
    const Byte* collator = cfp_handle_data(handle, &extent, 0);
    if (!collator || extent < collator_header_size || locale_word(collator) != COLLATOR_RESOURCE)
        return collator_reference("UCDisposeCollator", call);

    const int status = collator_release(call->stack, handle);
    return cfp_write(output, 0) && locale_status(call, status);
}

typedef struct {
    uint32_t* weights;
    uint32_t count;
    uint32_t capacity;
} collator_weight_level;

static int collator_append_weight(collator_weight_level* level, uint32_t value, int reverse)
{
    if (level->count == level->capacity) {
        const uint32_t capacity = level->capacity ? 2 * level->capacity : 40;
        uint32_t* resized = realloc(level->weights, (size_t)capacity * sizeof *resized);
        if (!resized)
            return 0;
        level->weights = resized;
        level->capacity = capacity;
    }
    if (reverse)
        memmove(level->weights + 1, level->weights, (size_t)level->count * sizeof *level->weights);
    level->weights[reverse ? 0 : level->count] = value;
    ++level->count;
    return 1;
}

static void collator_close_context(collator_weight_level* raw, uint32_t start)
{
    if (start < raw->count) {
        uint32_t bias = 0;
        for (uint32_t i = raw->count; i > start; --i) {
            raw->weights[i - 1] += bias;
            bias += 0x10000;
        }
    }
}

static int collator_append_levels(collator_weight_level* levels, const Byte* weighted, Byte* output, uint32_t* primary,
                                  uint32_t* context)
{
    const uint32_t unit = locale_word(weighted);
    const int mapped = weighted[4] != 0;
    const int contextual = mapped && weighted[5] != 0;
    if (!mapped) {
        locale_store_word(output + 4 * (*primary)++, unit + 0x600000);
    }
    else if (!contextual) {
        const unsigned reverse = locale_half(weighted + 6);
        const uint32_t first = locale_word(weighted + 8);
        if (first)
            locale_store_word(output + 4 * (*primary)++, first);
        for (unsigned i = 1; i < 5; ++i) {
            const uint32_t weight = locale_word(weighted + 8 + 4 * i);
            if (weight && !collator_append_weight(&levels[i - 1], weight, i == reverse))
                return 0;
        }
        if (first && reverse == 0) {
            /* The primary level is already in guest order and reserves its delimiter. */
            memmove(output + 4, output, (size_t)(*primary - 1) * 4);
            locale_store_word(output, first);
        }
    }

    collator_weight_level* raw = &levels[4];
    if (contextual) {
        if (*context == UINT32_MAX)
            *context = raw->count;
    }
    else {
        collator_close_context(raw, *context);
        *context = UINT32_MAX;
    }
    return collator_append_weight(raw, unit, 0);
}

static int collator_finish_key(collator_weight_level* levels, Byte* output, uint32_t capacity, uint32_t* written)
{
    if (levels[0].count || levels[1].count)
        locale_store_word(output + 4 * (*written)++, 0);
    if (levels[0].count && levels[1].count && !collator_append_weight(&levels[0], 0, 0))
        return memFullErr;
    if (!collator_append_weight(&levels[1], 1, 0) || (levels[2].count && !collator_append_weight(&levels[2], 0, 0)) ||
        (levels[3].count && !collator_append_weight(&levels[3], 0, 0)))
        return memFullErr;

    for (unsigned level = 0; level < 5; ++level) {
        for (uint32_t i = 0; i < levels[level].count; ++i) {
            if (*written == capacity)
                return -25340;
            locale_store_word(output + 4 * (*written)++, levels[level].weights[i]);
        }
    }
    return noErr;
}

static int collator_key_worker(uint32_t handle, ROMlib_carbon_reference* saved)
{
    uint32_t extent;
    const Byte* collator = cfp_handle_data(handle, &extent, 0);
    return collator && extent >= collator_header_size && locale_word(collator) == COLLATOR_RESOURCE &&
           locale_word(collator + 32) <= (extent - collator_header_size) / collator_row_size &&
           !(locale_word(collator + 28) & 0x20000) &&
           ROMlib_carbon_load_reference("UnicodeUtilitiesLib.CL", "UCGetCollationKey", saved) &&
           saved->code_offset == 0x127c && saved->code_size == 0x3a14 && saved->toc && saved->toc_offset == 0x8c &&
           saved->toc_size == 0x48c;
}

static int collator_build_key(const ROMlib_carbon_reference* saved, uint32_t handle, uint32_t text, uint32_t count,
                              Byte* output, uint32_t capacity, uint32_t* written, int* status)
{
    collator_weight_level levels[5] = {0};
    uint32_t primary = 0, context = UINT32_MAX;
    _Alignas(16) Byte normalized[240] = {0}, weighted[28] = {0};
    _Alignas(16) uint16_t position = 0, units = 0, index = 0;
    *written = 0;
    *status = noErr;
    int ok = 1;
    while (CW(position) < count || CW(index) < CW(units)) {
        if (primary == capacity - 1) {
            *status = -25340;
            break;
        }
        if (CW(index) >= CW(units)) {
            const uint16_t before = CW(position);
            const uint32_t normalize[] = {
                handle, text, count, US_TO_SYN68K(&position), US_TO_SYN68K(normalized), US_TO_SYN68K(&units)};
            ok = cfp_private(saved, 0x7d4, normalize, NELEM(normalize), NULL);
            if (!ok || CW(position) <= before || CW(units) > sizeof normalized / 12 || !CW(units)) {
                ok = 0;
                break;
            }
            if (CW(position) > count)
                break;
            index = 0;
        }

        const uint32_t weight[] = {handle, US_TO_SYN68K(normalized), CW(units), US_TO_SYN68K(&index),
                                   US_TO_SYN68K(weighted)};
        ok = cfp_private(saved, 0xf48, weight, NELEM(weight), NULL);
        if (!ok || CW(index) >= CW(units)) {
            ok = 0;
            break;
        }
        if (!collator_append_levels(levels, weighted, output, &primary, &context)) {
            *status = memFullErr;
            break;
        }
        index = CW(CW(index) + 1);
    }
    if (ok && !*status) {
        collator_close_context(&levels[4], context);
        *status = collator_finish_key(levels, output, capacity, &primary);
        *written = primary;
    }
    for (unsigned i = 0; i < 5; ++i)
        free(levels[i].weights);
    return ok;
}

static int carbon_UCGetCollationKey(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t handle = call->words[0], text = call->words[1], count = call->words[2], capacity = call->words[3];
    const uint32_t actual = call->words[4], output_address = call->words[5];
    if (!locale_output(actual, 4))
        return locale_status(call, paramErr);
    if (!cfp_write(actual, 0))
        return 0;
    if (!handle || !text || !output_address)
        return locale_status(call, paramErr);
    if (!capacity)
        return locale_status(call, -25340);
    const uint64_t output_size = (uint64_t)capacity * 4;
    if (output_size > UINT64_C(0x100000000) - output_address)
        return locale_status(call, paramErr);
    Byte* output = ROMlib_carbon_try_guest_memory(output_address, output_size, 1);
    if (!output)
        return locale_status(call, paramErr);

    ROMlib_carbon_reference saved;
    if (count > INT16_MAX || !collator_key_worker(handle, &saved))
        return collator_reference("UCGetCollationKey", call);
    if (!ROMlib_carbon_try_guest_memory(text, count ? count * 2 : 1, 0))
        return locale_status(call, paramErr);

    uint32_t written;
    int status;
    if (!collator_build_key(&saved, handle, text, count, output, capacity, &written, &status))
        return collator_reference("UCGetCollationKey", call);
    return cfp_write(actual, written) && locale_status(call, status);
}

static Byte* collator_comparison_key(const ROMlib_carbon_reference* worker, uint32_t handle, uint32_t text,
                                     uint32_t count, uint32_t* length)
{
    uint32_t capacity = 6 * count + 32;
    for (;;) {
        Byte* bytes = malloc((size_t)capacity * 4);
        if (!bytes)
            return NULL;
        int status;
        if (!collator_build_key(worker, handle, text, count, bytes, capacity, length, &status)) {
            free(bytes);
            return NULL;
        }
        if (!status)
            return bytes;
        free(bytes);
        if (status != -25340 || capacity > 120 * count + 32)
            return NULL;
        capacity *= 2;
    }
}

static int32_t collator_compare_weight_keys(const Byte* left, uint32_t left_count, const Byte* right,
                                            uint32_t right_count, int* equivalent)
{
    int primary = 1, significant = 1;
    const uint32_t common = MIN(left_count, right_count);
    for (uint32_t i = 0; i < common; ++i) {
        const uint32_t a = locale_word(left + 4 * i), b = locale_word(right + 4 * i);
        if (a != b) {
            *equivalent = !significant;
            const int magnitude = primary && (a > 1 || b > 1) ? 2 : 1;
            return a < b ? -magnitude : magnitude;
        }
        if (!a || a == 1)
            primary = 0;
        if (a == 1)
            significant = 0;
    }
    *equivalent = left_count == right_count || !significant;
    return (left_count > right_count) - (left_count < right_count);
}

static int carbon_UCCompareText(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t handle = call->words[0], left = call->words[1], right = call->words[3];
    const uint32_t left_count = call->words[2], right_count = call->words[4];
    Byte* equivalent = call->words[5] ? locale_output(call->words[5], 1) : NULL;
    Byte* order = call->words[6] ? locale_output(call->words[6], 4) : NULL;
    if (equivalent)
        *equivalent = 1;
    if (order)
        memset(order, 0, 4);
    if (!handle || !left || !right || (!equivalent && !order) || (call->words[5] && !equivalent) ||
        (call->words[6] && !order))
        return locale_status(call, paramErr);

    ROMlib_carbon_reference worker;
    if (MAX(left_count, right_count) > INT16_MAX || !collator_key_worker(handle, &worker))
        return collator_reference("UCCompareText", call);
    const Byte* left_text = ROMlib_carbon_try_guest_memory(left, left_count ? left_count * 2 : 1, 0);
    const Byte* right_text = ROMlib_carbon_try_guest_memory(right, right_count ? right_count * 2 : 1, 0);
    if (!left_text || !right_text)
        return locale_status(call, paramErr);
    if (left_count == right_count && !memcmp(left_text, right_text, (size_t)left_count * 2))
        return locale_status(call, noErr);
    if (!left_count || !right_count)
        return collator_reference("UCCompareText", call);

    uint32_t left_size, right_size;
    Byte* left_key = collator_comparison_key(&worker, handle, left, left_count, &left_size);
    Byte* right_key = left_key ? collator_comparison_key(&worker, handle, right, right_count, &right_size) : NULL;
    if (!left_key || !right_key) {
        free(left_key);
        return collator_reference("UCCompareText", call);
    }
    int same;
    const int32_t compared = collator_compare_weight_keys(left_key, left_size, right_key, right_size, &same);
    free(right_key);
    free(left_key);
    /* The saved routine applies its original-character tie breakers to equivalent strings. */
    if (same)
        return collator_reference("UCCompareText", call);
    if (equivalent)
        *equivalent = same;
    if (order)
        locale_store_word(order, compared);
    return locale_status(call, noErr);
}

static int carbon_UCCompareTextDefault(ROMlib_carbon_ppc_frame* call)
{
    Byte* equivalent = call->words[5] ? locale_output(call->words[5], 1) : NULL;
    Byte* order = call->words[6] ? locale_output(call->words[6], 4) : NULL;
    if (equivalent)
        *equivalent = 1;
    if (order)
        memset(order, 0, 4);
    if ((call->words[5] && !equivalent) || (call->words[6] && !order))
        return locale_status(call, paramErr);

    ROMlib_carbon_reference saved;
    if (!ROMlib_carbon_load_reference("UnicodeUtilitiesLib.CL", "UCCompareTextDefault", &saved) ||
        saved.code_offset != 0x2a7c || saved.code_size != 0x3a14 || !saved.toc || saved.toc_offset != 0x8c ||
        saved.toc_size != 0x48c)
        return collator_reference("UCCompareTextDefault", call);
    const uint32_t handle = cfp_toc(&saved, 0x60);
    uint32_t extent;
    Byte* collator = cfp_handle_data(handle, &extent, 1);
    if (!collator || extent < collator_header_size || locale_word(collator) != COLLATOR_RESOURCE)
        return locale_status(call, paramErr);

    /* The fragment owns the default handle. Callers supply a fresh option word
       on every comparison, even when more than one caller shares the cache. */
    locale_store_word(collator + 28, call->words[0]);
    ROMlib_carbon_ppc_frame arguments = *call;
    arguments.words[0] = handle;
    if (!carbon_UCCompareText(&arguments))
        return 0;
    return locale_status(call, arguments.words[0]);
}

#endif
