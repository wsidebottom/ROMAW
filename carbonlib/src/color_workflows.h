/*+===================================================================
  File:      color_workflows.h

  Summary:   Implements ColorSync workflow contexts, metadata, and update operations.

  Classes:   None.

  Functions: Private PowerPC frame adapters for ColorSync workflow exports.

  Origin:    Mac OS 9.2.1 ColorSync 3.0.4 workflow records and saved workers.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_COLOR_WORKFLOWS_H
#define ROMAW_CARBON_COLOR_WORKFLOWS_H

#include "cf_batch.h"
#include "color_profiles.h"

enum color_workflow_operation {
    workflow_init,
    workflow_term,
    workflow_save,
    workflow_revert,
    workflow_count,
    workflow_index,
    workflow_name,
    workflow_description,
    workflow_set_name,
    workflow_set_description,
    workflow_dates,
    workflow_active,
    workflow_modified,
    workflow_activate,
    workflow_activate_current,
    workflow_delete,
    workflow_get_entry,
    workflow_set_entry,
    workflow_copy,
    workflow_export,
    workflow_import,
};

enum {
    workflow_context_size = 0x28,
    workflow_context_count = 8,
    workflow_context_version = 0x20000,
    workflow_flags = 0x25c,
    workflow_deleted = 0x20,
    workflow_new_file = 0x10,
    workflow_unavailable = -4220,
};

static void color_workflow_initialize(unsigned char* context, uint32_t reload)
{
    color_write(context, 4, TICK("cfgs"));
    color_write(context + 4, 4, reload);
    color_write(context + workflow_context_count, 4, 0);
    color_write(context + 12, 4, workflow_context_version);
}

static uint32_t color_workflow_count_slots(uint32_t context)
{
    const unsigned char* header = (context & 1) ? NULL : color_profile_memory(context, workflow_context_size, 0);
    return header && color_read(header + 12, 4) == workflow_context_version
               ? color_read(header + workflow_context_count, 4)
               : 0;
}

static int color_workflow_free(const ROMlib_carbon_reference* ref, uint32_t* encoded)
{
    const uint32_t pointer = US_TO_SYN68K(encoded);
    return !*encoded || cfp_private(ref, 0x2de5c, &pointer, 1, NULL);
}

static int color_workflow_init(const ROMlib_carbon_reference* ref, uint32_t output, ROMlib_carbon_ppc_frame* call)
{
    if (!output)
        return color_profile_result(call, paramErr);
    if (!cfp_write(output, 0))
        return 0;
    _Alignas(4) uint32_t storage = 0, fallback = 0;
    const uint32_t allocate[] = {US_TO_SYN68K(&storage), workflow_context_size};
    uint32_t status;
    if (!cfp_private(ref, 0x2dbb8, allocate, NELEM(allocate), &status))
        return 0;
    uint32_t context = CL(storage);
    if (!status) {
        unsigned char* header = color_profile_memory(context, workflow_context_size, 1);
        if (!header)
            return color_workflow_free(ref, &storage) && color_profile_result(call, memFullErr);
        color_workflow_initialize(header, 0);
        if (!cfp_private(ref, 0xb0cc, &context, 1, &status))
            return 0;
        if (status || !color_workflow_count_slots(context)) {
            const uint32_t slot = US_TO_SYN68K(&fallback);
            if (!cfp_private(ref, 0xc1f4, &slot, 1, &status))
                return 0;
            if (!status) {
                const uint32_t args[] = {context, CL(fallback)};
                if (!cfp_private(ref, 0xc3e0, args, NELEM(args), &status))
                    return 0;
                if (!status && !cfp_private(ref, 0xc4c4, args, NELEM(args), &status))
                    return 0;
            }
        }
    }
    if (!status)
        return cfp_write(output, context) && color_profile_result(call, noErr);
    if (!color_workflow_free(ref, &storage))
        return 0;
    const uint32_t release[] = {0, CL(fallback), 0};
    return (!fallback || cfp_private(ref, 0xc06c, release, NELEM(release), NULL)) && color_profile_result(call, status);
}

static int color_workflow_get(const ROMlib_carbon_reference* ref, uint32_t context, uint32_t index, int all,
                              uint32_t* workflow, uint32_t* status)
{
    _Alignas(4) uint32_t encoded = 0;
    const uint32_t address = US_TO_SYN68K(&encoded);
    const uint32_t args[] = {context, address, index, all};
    const int active = index == UINT32_MAX && !all;
    if (!cfp_private(ref, active ? 0xaf5c : 0xad34, args, active ? 2 : NELEM(args), status))
        return 0;
    *workflow = CL(encoded);
    return 1;
}

static int color_workflow_valid(const ROMlib_carbon_reference* ref, uint32_t workflow, int strict, uint32_t* result)
{
    const uint32_t args[] = {workflow, 1, strict};
    return cfp_private(ref, 0xcea4, args, NELEM(args), result);
}

static int color_workflow_scan(const ROMlib_carbon_reference* ref, uint32_t context, int saving,
                               ROMlib_carbon_ppc_frame* call)
{
    uint32_t index = 0, valid_count = 0, status = noErr;
    do {
        uint32_t handle, valid;
        if (!color_workflow_get(ref, context, index, saving, &handle, &status))
            return 0;
        if (status || (!saving && !handle))
            break;
        if (!color_workflow_valid(ref, handle, saving, &valid))
            return 0;
        if (valid && saving) {
            const uint32_t record = color_profile_indirect(handle);
            uint32_t flags;
            if (!record || !cfp_get(record + workflow_flags, &flags))
                return 0;
            if (flags & workflow_deleted) {
                const uint32_t discard[] = {context, handle, 1};
                if (!cfp_private(ref, 0xc06c, discard, NELEM(discard), NULL))
                    return 0;
                continue;
            }
            if (flags & workflow_new_file) {
                const uint32_t clear[] = {handle, workflow_new_file, 0};
                if (!cfp_private(ref, 0xaed8, clear, NELEM(clear), &status))
                    return 0;
                if (status)
                    break;
            }
            if (!cfp_private(ref, 0xa880, &handle, 1, &status))
                return 0;
            if (status)
                break;
        }
        if (valid)
            ++valid_count;
        ++index;
    } while (index < color_workflow_count_slots(context));
    return color_profile_result(call, saving ? status : valid_count);
}

static int color_workflow_index(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    unsigned char* active = color_profile_memory(args[3], 1, 1);
    if (!args[2])
        return color_profile_result(call, paramErr);
    if (!cfp_write(args[2], 0))
        return 0;
    if (active)
        *active = 0;
    uint32_t handle, status;
    if (!color_workflow_get(ref, args[0], args[1], 0, &handle, &status))
        return 0;
    if (handle) {
        uint32_t valid;
        if (!color_workflow_valid(ref, handle, 0, &valid))
            return 0;
        if (!valid)
            return color_profile_result(call, workflow_unavailable);
        if (active) {
            uint32_t flags;
            const uint32_t record = color_profile_indirect(handle);
            if (!record || !cfp_get(record + workflow_flags, &flags))
                return 0;
            *active = flags & 1;
        }
    }
    return (status || cfp_write(args[2], handle)) && color_profile_result(call, status);
}

static int color_workflow_text(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call, uint32_t field)
{
    const uint32_t workflow = call->words[1], output = call->words[2], count = call->words[3];
    if (!output || !count)
        return color_profile_result(call, paramErr);
    uint32_t capacity, status;
    if (!cfp_get(count, &capacity))
        return 0;
    _Alignas(4) uint32_t encoded = 0, length = 0;
    const uint32_t read[] = {workflow, field, US_TO_SYN68K(&encoded), US_TO_SYN68K(&length)};
    int ok = cfp_private(ref, 0xba90, read, NELEM(read), &status);
    if (ok && !status) {
        const uint32_t bytes = CL(length), size = MIN(capacity, bytes);
        ok = cfp_write(count, bytes);
        if (ok && size >= 4) {
            const uint32_t units = (size - 4) / 2;
            ok = cfp_write(output, units) && (!units || cfp_move(output + 4, CL(encoded) + 4, units * 2));
        }
    }
    return color_workflow_free(ref, &encoded) && ok && color_profile_result(call, status);
}

static int color_workflow_set_text(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call, int rename)
{
    const uint32_t context = call->words[0], workflow = call->words[1], text = call->words[2];
    if (!text)
        return color_profile_result(call, paramErr);
    uint32_t units;
    if (!cfp_get(text, &units))
        return 0;
    if (units > (UINT32_MAX - 4) / 2)
        return color_profile_result(call, paramErr);
    const unsigned char* proposed = color_profile_memory(text, 4 + 2 * units, 0);
    if (!proposed)
        return color_profile_result(call, paramErr);

    _Alignas(4) uint32_t previous = 0, length = 0;
    const uint32_t field = rename ? TICK("Name") : TICK("Desc");
    const uint32_t read[] = {workflow, field, US_TO_SYN68K(&previous), US_TO_SYN68K(&length)};
    uint32_t status;
    int ok = cfp_private(ref, 0xba90, read, NELEM(read), &status);
    if (ok && !status) {
        const unsigned char* prior = color_profile_memory(CL(previous), CL(length), 0);
        const int equal = prior && CL(length) >= 4 + units * 2 && color_read(prior, 4) == units &&
                          memcmp(prior + 4, proposed + 4, units * 2) == 0;
        if (!equal) {
            if (rename) {
                const uint32_t duplicate[] = {context, text, workflow, 0, 0};
                uint32_t unique;
                ok = cfp_private(ref, 0xe1f8, duplicate, NELEM(duplicate), &unique);
                if (ok && unique) {
                    const uint32_t change[] = {workflow, CL(previous), text, 1};
                    ok = cfp_private(ref, 0xc97c, change, NELEM(change), &status);
                }
                else if (ok)
                    status = dupFNErr;
            }
            else {
                const uint32_t change[] = {workflow, field, text};
                ok = cfp_private(ref, 0xb4e0, change, NELEM(change), &status);
            }
        }
    }
    return color_workflow_free(ref, &previous) && ok && color_profile_result(call, status);
}

static int color_workflow_activate(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call, int current)
{
    const uint32_t* args = call->words;
    const uint32_t context = args[0], output = args[1];
    uint32_t handle = output, status;
    if (current) {
        if ((uint8_t)args[2] && (uint8_t)args[3])
            return color_profile_result(call, paramErr);
        if (output && !cfp_write(output, 0))
            return 0;
        if (!color_workflow_get(ref, context, UINT32_MAX, 0, &handle, &status))
            return 0;
        if (status || !handle)
            return color_profile_result(call, status);
    }
    _Alignas(4) unsigned char location[258] = {0};
    const uint32_t activate[] = {handle, current ? (uint8_t)args[2] : 1, current ? (uint8_t)args[3] : 0,
                                 current ? args[4] : US_TO_SYN68K(location)};
    if (!cfp_private(ref, 0xded4, activate, NELEM(activate), &status))
        return 0;
    if (status)
        return color_profile_result(call, status);
    if (!current) {
        const uint32_t save[] = {context, handle};
        return cfp_private(ref, 0xc3e0, save, NELEM(save), &status) && color_profile_result(call, status);
    }
    return cfp_private(ref, 0xa880, &handle, 1, &status) && (status || !output || cfp_write(output, handle)) &&
           color_profile_result(call, status);
}

static int color_workflow_delete(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    enum { workflow_locked = -47 };

    const uint32_t context = call->words[0], handle = call->words[1];
    if (color_workflow_count_slots(context) <= 1)
        return color_profile_result(call, workflow_locked);
    uint32_t valid;
    if (!color_workflow_valid(ref, handle, 0, &valid))
        return 0;
    if (!valid)
        return color_profile_result(call, paramErr);
    const uint32_t record = color_profile_indirect(handle);
    uint32_t flags;
    if (!record || !cfp_get(record + workflow_flags, &flags))
        return 0;
    if (flags & 1)
        return color_profile_result(call, workflow_locked);

    const uint32_t mark[] = {handle, workflow_deleted, 1};
    uint32_t status;
    if (!cfp_private(ref, 0xaed8, mark, NELEM(mark), &status))
        return 0;
    if (status)
        return color_profile_result(call, status);
    const uint32_t delete_file[] = {context, handle, 1};
    if (!cfp_private(ref, 0xe4f8, delete_file, 2, &status))
        return 0;
    return (!status || cfp_private(ref, 0xc06c, delete_file, NELEM(delete_file), NULL)) &&
           color_profile_result(call, noErr);
}

enum {
    workflow_entry_profile = 3,
    workflow_entry_text = 8,
    workflow_entry_cmm = 10,
    workflow_entry_avid = 12,
    workflow_profile_location_size = 0x102
};

static unsigned color_workflow_entry_kind(uint32_t selector)
{
    switch (selector) {
    case T('N', 'a', 'm', 'e'):
    case T('D', 'e', 's', 'c'):
        return workflow_entry_text;
    case T('C', 'M', 'M', ' '):
        return workflow_entry_cmm;
    case T('A', 'V', 'I', 'D'):
        return workflow_entry_avid;
    case T('C', 'M', 'Y', 'K'):
    case T('G', 'R', 'A', 'Y'):
    case T('L', 'a', 'b', ' '):
    case T('N', 'A', 'M', 'E'):
    case T('R', 'G', 'B', ' '):
    case T('S', 'y', 's', ' '):
    case T('X', 'Y', 'Z', ' '):
    case T('d', 'p', 'l', 'y'):
    case T('h', 'e', 'x', 'a'):
    case T('i', 'n', 'p', 't'):
    case T('o', 'u', 't', 'p'):
    case T('p', 'r', 'u', 'f'):
        return workflow_entry_profile;
    default:
        return 0;
    }
}

static int color_workflow_profile_mode(const ROMlib_carbon_reference* ref, uint32_t selector, uint32_t* mode)
{
    *mode = 0;
    if (selector == TICK("Sys "))
        return 1;
    _Alignas(8) unsigned char definition[50];
    const uint32_t lookup[] = {selector, US_TO_SYN68K(definition)};
    uint32_t status;
    if (!cfp_private(ref, 0xd40c, lookup, NELEM(lookup), &status))
        return 0;
    *mode = status ? 1 : (int16_t)color_read(definition + 8, 2);
    return 1;
}

static int color_workflow_get_entry(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call,
                                    uint32_t selector, unsigned kind)
{
    const uint32_t size = call->words[3], output = call->words[4];
    uint32_t capacity;
    if (!size || !color_profile_memory(size, 4, 1) || !cfp_get(size, &capacity) ||
        (output && capacity && !color_profile_memory(output, capacity, 1)))
        return color_profile_result(call, paramErr);
    const uint32_t read[] = {call->words[1], selector, kind, size, output};
    uint32_t status, actual;
    if (!cfp_private(ref, 0xb8f4, read, NELEM(read), &status))
        return 0;
    if (status || kind != workflow_entry_profile)
        return color_profile_result(call, status);
    if (!cfp_get(size, &actual))
        return 0;
    if (actual != workflow_profile_location_size)
        return color_profile_result(call, noErr);

    uint32_t usable;
    if (!cfp_private(ref, 0x5ee4, &output, 1, &usable))
        return 0;
    if (usable)
        return color_profile_result(call, noErr);
    uint32_t mode;
    if (!color_workflow_profile_mode(ref, selector, &mode))
        return 0;
    const uint32_t repair[] = {call->words[1], selector, mode};
    if (!cfp_private(ref, 0xb79c, repair, NELEM(repair), &status))
        return 0;
    return (status || cfp_private(ref, 0xb8f4, read, NELEM(read), &status)) && color_profile_result(call, status);
}

static int color_workflow_set_entry(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call,
                                    uint32_t selector, unsigned kind)
{
    const uint32_t handle = call->words[1], size = call->words[3], address = call->words[4];
    if (!address || !color_profile_memory(address, size, 0))
        return color_profile_result(call, paramErr);
    uint32_t status;
    switch (kind) {
    case workflow_entry_profile: {
        if (size != workflow_profile_location_size)
            return color_profile_result(call, paramErr);
        uint32_t mode;
        if (!color_workflow_profile_mode(ref, selector, &mode))
            return 0;
        const uint32_t profile[] = {handle, selector, mode, address};
        return cfp_private(ref, 0xb6f4, profile, NELEM(profile), &status) && color_profile_result(call, status);
    }
    case workflow_entry_text: {
        uint32_t units;
        if (size < 4 || !cfp_get(address, &units) || units > (size - 4) / 2)
            return color_profile_result(call, paramErr);
        const uint32_t string[] = {handle, selector, address};
        return cfp_private(ref, 0xb4e0, string, NELEM(string), &status) && color_profile_result(call, status);
    }
    default: {
        if (size != 4)
            return color_profile_result(call, paramErr);
        const uint32_t scalar[] = {handle, 0, address};
        return cfp_private(ref, kind == workflow_entry_cmm ? 0xb5d0 : 0xb66c, scalar, NELEM(scalar), &status) &&
               color_profile_result(call, status);
    }
    }
}

static int color_workflow_copy(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t context = call->words[0], source = call->words[1], output = call->words[2], name = call->words[3];
    uint32_t units;
    if (!source || !output || !color_profile_memory(output, 4, 1) || !name || !cfp_get(name, &units) ||
        units > (UINT32_MAX - 4) / 2 || !color_profile_memory(name, 4 + 2 * units, 0))
        return color_profile_result(call, paramErr);
    if (!cfp_write(output, 0))
        return 0;
    const uint32_t check_name[] = {context, name, 0, 0, 0};
    uint32_t unique, status;
    if (!cfp_private(ref, 0xe1f8, check_name, NELEM(check_name), &unique))
        return 0;
    if (!unique)
        return color_profile_result(call, dupFNErr);

    _Alignas(256) Str255 file_name = {0};
    _Alignas(4) uint32_t copied = 0;
    const uint32_t convert_name[] = {name, US_TO_SYN68K(file_name)};
    if (!cfp_private(ref, 0xb2c0, convert_name, NELEM(convert_name), &status))
        return 0;
    if (status)
        return color_profile_result(call, status);
    const uint32_t clone[] = {US_TO_SYN68K(&copied), US_TO_SYN68K(file_name), TICK("cswi"), source};
    int ok = cfp_private(ref, 0xa8ac, clone, NELEM(clone), &status);
    const uint32_t copy = CL(copied);
    if (ok && !status) {
        const uint32_t set_name[] = {copy, TICK("Name"), name};
        ok = cfp_private(ref, 0xb4e0, set_name, NELEM(set_name), &status);
    }
    if (ok && !status) {
        const uint32_t record = color_profile_indirect(copy);
        const uint32_t date[] = {record + 0x248, 1};
        const uint32_t staged[] = {copy, workflow_new_file, 1};
        const uint32_t inactive[] = {copy, 1, 0};
        const uint32_t publish[] = {context, copy};
        ok = record && cfp_private(ref, 0x2dfb4, date, NELEM(date), NULL) &&
             cfp_private(ref, 0xaed8, staged, NELEM(staged), &status) &&
             (status || cfp_private(ref, 0xaed8, inactive, NELEM(inactive), &status)) &&
             (status || cfp_private(ref, 0xc4c4, publish, NELEM(publish), &status));
    }
    if (ok && !status)
        return cfp_write(output, copy) && color_profile_result(call, noErr);
    const uint32_t release[] = {context, copy, 1};
    return (!copy || cfp_private(ref, 0xc06c, release, NELEM(release), NULL)) && ok &&
           color_profile_result(call, status);
}

static bool color_workflow_names_equal(uint32_t left, uint32_t right)
{
    if (left == right)
        return true;
    uint32_t units, other_units;
    if (!cfp_get(left, &units) || !cfp_get(right, &other_units) || units != other_units || units > UINT32_MAX / 2)
        return false;
    const unsigned char* first = color_profile_memory(left + 4, units * 2, 0);
    const unsigned char* second = color_profile_memory(right + 4, units * 2, 0);
    return first && second && !memcmp(first, second, units * 2);
}

static int color_workflow_export_profiles(const ROMlib_carbon_reference* ref, uint32_t copy, uint32_t types,
                                          uint32_t* status)
{
    _Alignas(4) uint32_t count = 0;
    const uint32_t count_entries[] = {copy, US_TO_SYN68K(&count)};
    if (!cfp_private(ref, 0xa8e0, count_entries, NELEM(count_entries), status))
        return 0;
    if (*status)
        return 1;

    uint32_t entries = CL(count);
    for (uint32_t index = 1; index && index <= entries; ++index) {
        _Alignas(4) uint32_t key = 0, bytes = 0;
        const uint32_t item[] = {copy, index, US_TO_SYN68K(&key), US_TO_SYN68K(&bytes), 0};
        if (!cfp_private(ref, 0xa910, item, NELEM(item), status))
            return 0;
        if (*status)
            return 1;

        const uint32_t selector = CL(key);
        const uint32_t is_text[] = {types, selector, 4}, is_scalar[] = {types, selector, 7};
        uint32_t match;
        if (!cfp_private(ref, 0xbc78, is_text, NELEM(is_text), &match))
            return 0;
        if (!match && selector != TICK("AVID"))
            continue;
        if (match) {
            if (!cfp_private(ref, 0xbc78, is_scalar, NELEM(is_scalar), &match))
                return 0;
            if (match) {
                const uint32_t mode[] = {types, selector, 5};
                uint32_t method;
                _Alignas(512) unsigned char location[workflow_profile_location_size];
                _Alignas(4) uint32_t encoded = CL(US_TO_SYN68K(location));
                const uint32_t retrieve[] = {copy, selector, US_TO_SYN68K(&encoded)};
                if (!cfp_private(ref, 0xbc78, mode, NELEM(mode), &method) ||
                    !cfp_private(ref, 0xbdb0, retrieve, NELEM(retrieve), status))
                    return 0;
                if (*status)
                    return 1;
                _Alignas(128) FSSpec profile;
                const uint32_t convert[] = {US_TO_SYN68K(&profile), CL(encoded)};
                if (!cfp_private(ref, 0x318e4, convert, NELEM(convert), status))
                    return 0;
                if (*status)
                    return 1;
                const uint32_t embed[] = {convert[0], selector, method, copy};
                if (!cfp_private(ref, 0xebb0, embed, NELEM(embed), status))
                    return 0;
                if (*status)
                    return 1;
                continue;
            }
        }
        const uint32_t remove[] = {copy, selector};
        if (!cfp_private(ref, 0xa9c0, remove, NELEM(remove), status))
            return 0;
        if (*status)
            return 1;
        --index;
        --entries;
    }
    return 1;
}

static int color_workflow_export_name(const ROMlib_carbon_reference* ref, uint32_t copy, uint32_t location,
                                      uint32_t* previous, uint32_t* replacement, uint32_t* status)
{
    _Alignas(128) FSSpec file;
    const uint32_t convert[] = {US_TO_SYN68K(&file), location};
    if (!cfp_private(ref, 0x318e4, convert, NELEM(convert), status))
        return 0;
    if (*status)
        return 1;

    _Alignas(128) unsigned char name[65] = {0};
    _Alignas(4) uint32_t units = CL(64);
    const uint32_t read[] = {convert[0], 64, US_TO_SYN68K(&units), US_TO_SYN68K(name + 1)};
    if (!cfp_private(ref, 0x31804, read, NELEM(read), status))
        return 0;
    if (*status)
        return 1;
    name[0] = CL(units);
    const uint32_t allocate[] = {US_TO_SYN68K(replacement), 4 + CL(units) * 2};
    if (!cfp_private(ref, 0x2d6e0, allocate, NELEM(allocate), status))
        return 0;
    if (*status)
        return 1;
    const uint32_t text = CL(*replacement);
    const uint32_t translate[] = {0, US_TO_SYN68K(name), text, text + 4};
    if (!cfp_write(text, CL(units)) || !cfp_private(ref, 0x323f4, translate, NELEM(translate), status))
        return 0;
    if (*status)
        return 1;
    _Alignas(4) uint32_t size = 0;
    const uint32_t old_name[] = {copy, TICK("Name"), US_TO_SYN68K(previous), US_TO_SYN68K(&size)};
    if (!cfp_private(ref, 0xba90, old_name, NELEM(old_name), status))
        return 0;
    if (*status || color_workflow_names_equal(CL(*previous), text))
        return 1;
    const uint32_t rename[] = {copy, TICK("Name"), text};
    return cfp_private(ref, 0xb4e0, rename, NELEM(rename), status);
}

static int color_workflow_export_file(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t source = call->words[1], location = call->words[2];
    if (!source || !color_profile_memory(location, workflow_profile_location_size, 0))
        return color_profile_result(call, paramErr);
    uint32_t valid;
    if (!color_workflow_valid(ref, source, 0, &valid))
        return 0;
    if (!valid)
        return color_profile_result(call, paramErr);
    if (!cfp_private(ref, 0x5ee4, &location, 1, &valid))
        return 0;
    const uint32_t remove_old[] = {location, 1};
    if (valid && !cfp_private(ref, 0x62c8, remove_old, NELEM(remove_old), NULL))
        return 0;

    _Alignas(4) uint32_t storage = 0, types = 0, previous = 0, replacement = 0;
    const uint32_t clone[] = {US_TO_SYN68K(&storage), location, TICK("csw "), source};
    uint32_t status;
    int ok = cfp_private(ref, 0xa8ac, clone, NELEM(clone), &status);
    const uint32_t copy = CL(storage), types_address = US_TO_SYN68K(&types);
    const uint32_t writable[] = {copy, 1};
    if (ok && !status)
        ok = cfp_private(ref, 0xaa9c, writable, NELEM(writable), NULL) &&
             cfp_private(ref, 0xb16c, &types_address, 1, &status);
    if (ok && !status)
        ok = color_workflow_export_profiles(ref, copy, CL(types), &status);
    if (ok && !status) {
        const uint32_t portable[] = {copy, 0x100, 1};
        ok = cfp_private(ref, 0xaed8, portable, NELEM(portable), &status);
    }
    if (ok && !status)
        ok = color_workflow_export_name(ref, copy, location, &previous, &replacement, &status);
    if (ok && !status)
        ok = cfp_private(ref, 0xa880, &copy, 1, &status);
    if (copy) {
        const uint32_t read_only[] = {copy, 0};
        if (ok && !status && !cfp_private(ref, 0xaa9c, read_only, NELEM(read_only), NULL))
            ok = 0;
        uint32_t close_status;
        if (!cfp_private(ref, 0xa854, &copy, 1, &close_status))
            ok = 0;
        else if (!status)
            status = close_status;
    }
    const int free_types = color_workflow_free(ref, &types);
    const int free_previous = color_workflow_free(ref, &previous);
    const int free_replacement = color_workflow_free(ref, &replacement);
    return ok && free_types && free_previous && free_replacement && color_profile_result(call, status);
}

static int color_workflow_extract_profile(const ROMlib_carbon_reference* ref, uint32_t copy, uint32_t selector,
                                          uint32_t entry, const FSSpec* directory, uint32_t parent, uint32_t* status)
{
    _Alignas(64) unsigned char name[64] = {0};
    _Alignas(128) FSSpec file;
    const uint32_t get[] = {copy, selector, entry, US_TO_SYN68K(name)};
    if (!cfp_private(ref, 0xf244, get, NELEM(get), status))
        return 0;
    if (*status)
        return 1;
    const uint32_t file_address = US_TO_SYN68K(&file);
    const uint32_t make[] = {(uint32_t)(int32_t)(int16_t)CW(directory->vRefNum), parent, get[3], file_address};
    if (!cfp_private(ref, 0x35220, make, NELEM(make), status))
        return 0;
    if (*status && *status != (uint32_t)fnfErr)
        return 1;
    const uint32_t extract[] = {file_address, selector, get[2], copy};
    if (!cfp_private(ref, 0xf2ac, extract, NELEM(extract), status))
        return 0;
    if (*status)
        return 1;
    _Alignas(512) unsigned char location[workflow_profile_location_size];
    const uint32_t convert[] = {US_TO_SYN68K(location), file_address};
    if (!cfp_private(ref, 0x31890, convert, NELEM(convert), status))
        return 0;
    if (*status)
        return 1;
    uint32_t mode;
    if (!cfp_get(entry + 12, &mode))
        return 0;
    const uint32_t install[] = {copy, selector, mode, convert[0]};
    return cfp_private(ref, 0xb6f4, install, NELEM(install), status);
}

static int color_workflow_import_profiles(const ROMlib_carbon_reference* ref, uint32_t copy, uint32_t types,
                                          const FSSpec* directory, uint32_t parent, uint32_t* status)
{
    const uint32_t clear[] = {types, 0, 0};
    _Alignas(4) uint32_t count = 0;
    const uint32_t count_entries[] = {copy, US_TO_SYN68K(&count)};
    if (!cfp_private(ref, 0xbc78, clear, NELEM(clear), NULL) ||
        !cfp_private(ref, 0xa8e0, count_entries, NELEM(count_entries), status))
        return 0;
    if (*status)
        return 1;

    const uint32_t entries = CL(count);
    for (uint32_t index = 1; index && index <= entries; ++index) {
        _Alignas(4) uint32_t key = 0, size = 0;
        const uint32_t item[] = {copy, index, US_TO_SYN68K(&key), US_TO_SYN68K(&size), 0};
        if (!cfp_private(ref, 0xa910, item, NELEM(item), status))
            return 0;
        if (*status)
            return 1;
        const uint32_t selector = CL(key);
        const uint32_t disabled[] = {types, selector, 2}, kind[] = {types, selector, 4};
        uint32_t result;
        if (!cfp_private(ref, 0xbc78, disabled, NELEM(disabled), NULL) ||
            !cfp_private(ref, 0xbc78, kind, NELEM(kind), &result))
            return 0;
        if (!result)
            continue;
        const uint32_t portable[] = {types, selector, 7};
        if (!cfp_private(ref, 0xbc78, portable, NELEM(portable), &result))
            return 0;
        if (result) {
            _Alignas(32) unsigned char embedded[26] = {0};
            size = 0;
            const uint32_t entry = US_TO_SYN68K(embedded);
            const uint32_t find[] = {copy, selector, 2, US_TO_SYN68K(&size), entry};
            if (!cfp_private(ref, 0xb8f4, find, NELEM(find), NULL))
                return 0;
            if (color_read(embedded, 4) == TICK("prof")) {
                if (!color_workflow_extract_profile(ref, copy, selector, entry, directory, parent, status))
                    return 0;
                if (*status)
                    return 1;
                continue;
            }
        }
        const uint32_t missing[] = {types, selector, 1};
        if (!cfp_private(ref, 0xbc78, missing, NELEM(missing), NULL))
            return 0;
    }

    const unsigned char* header = color_profile_memory(types, 2, 0);
    if (!header)
        return 0;
    const unsigned slots = color_read(header, 2);
    for (unsigned index = 0; index < slots; ++index) {
        const uint32_t available[] = {types, index, 3};
        uint32_t exists;
        if (!cfp_private(ref, 0xbc78, available, NELEM(available), &exists))
            return 0;
        if (exists)
            continue;
        const uint32_t lookup[] = {types, index, 8};
        uint32_t entry, selector;
        if (!cfp_private(ref, 0xbc78, lookup, NELEM(lookup), &entry) || !cfp_get(entry, &selector))
            return 0;
        const unsigned char* mode = color_profile_memory(entry + 8, 2, 0);
        if (!mode)
            return 0;
        const uint32_t fallback[] = {types, selector, 6};
        uint32_t enabled;
        if (!cfp_private(ref, 0xbc78, fallback, NELEM(fallback), &enabled))
            return 0;
        if (enabled) {
            const uint32_t restore[] = {copy, selector == TICK("Sys ") ? 0 : selector,
                                        (uint32_t)(int16_t)color_read(mode, 2)};
            if (!cfp_private(ref, 0xb79c, restore, NELEM(restore), status))
                return 0;
            if (*status)
                return 1;
        }
    }
    return 1;
}

static int color_workflow_import_name(const ROMlib_carbon_reference* ref, uint32_t source, uint32_t supplied,
                                      uint32_t size_output, uint32_t* allocated, uint32_t* result, uint32_t* status)
{
    uint32_t units = 0;
    if (supplied && !cfp_get(supplied, &units))
        return 0;
    if (units) {
        if (units > (UINT32_MAX - 4) / 2 || !color_profile_memory(supplied, 4 + units * 2, 0)) {
            *status = paramErr;
            return 1;
        }
        *result = supplied;
        return 1;
    }

    _Alignas(4) uint32_t size = 0;
    const uint32_t name[] = {source, TICK("Name"), US_TO_SYN68K(allocated), US_TO_SYN68K(&size)};
    if (!cfp_private(ref, 0xba90, name, NELEM(name), status))
        return 0;
    if (*status)
        return 1;
    *result = CL(*allocated);
    if (!supplied)
        return 1;
    uint32_t capacity;
    if (!cfp_get(size_output, &capacity))
        return 0;
    const uint32_t actual = CL(size), bytes = capacity < actual ? capacity : actual;
    return (!bytes || cfp_move(supplied, *result, bytes)) && cfp_write(size_output, actual);
}

static int color_workflow_import_conflict(const ROMlib_carbon_reference* ref, uint32_t context, uint32_t name,
                                          bool replace, uint32_t* active, uint32_t* status)
{
    _Alignas(4) uint32_t conflict = 0;
    const uint32_t unique[] = {context, name, 0, US_TO_SYN68K(&conflict), 0};
    uint32_t available;
    if (!cfp_private(ref, 0xe1f8, unique, NELEM(unique), &available))
        return 0;
    if (available)
        return 1;
    if (!replace) {
        *status = dupFNErr;
        return 1;
    }
    const uint32_t existing = CL(conflict);
    uint32_t flags;
    if (!cfp_get(color_profile_indirect(existing) + workflow_flags, &flags))
        return 0;
    *active = flags & 1;
    const uint32_t remove[] = {existing, workflow_deleted, 1};
    if (!cfp_private(ref, 0xaed8, remove, NELEM(remove), status))
        return 0;
    if (*status)
        return 1;
    const uint32_t save[] = {context, existing};
    if (!cfp_private(ref, 0xe4f8, save, NELEM(save), status))
        return 0;
    if (*status == (uint32_t)dupFNErr) {
        const uint32_t release[] = {context, existing, 1};
        *status = noErr;
        return cfp_private(ref, 0xc06c, release, NELEM(release), NULL);
    }
    return 1;
}

static int color_workflow_import_file(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t context = call->words[0], output = call->words[1], supplied = call->words[2];
    const uint32_t name_size = call->words[3], location = call->words[4];
    const bool replace = (uint8_t)call->words[5] != 0;
    if (!color_profile_memory(output, 4, 1) || !color_profile_memory(location, workflow_profile_location_size, 0) ||
        (supplied && (!color_profile_memory(supplied, 4, 0) || !color_profile_memory(name_size, 4, 1))))
        return color_profile_result(call, paramErr);
    if (!cfp_write(output, 0))
        return 0;

    _Alignas(4) uint32_t source = 0, copy = 0, stored_name = 0, types = 0, directory_id = 0;
    const uint32_t open[] = {US_TO_SYN68K(&source), TICK("csw "), location};
    uint32_t status, name = 0, active = 0;
    int ok = cfp_private(ref, 0xa830, open, NELEM(open), &status);
    if (ok && !status) {
        const uint32_t check[] = {CL(source), 2, 0};
        uint32_t valid;
        ok = cfp_private(ref, 0xcea4, check, NELEM(check), &valid);
        if (ok && !valid)
            status = -4226;
    }
    if (ok && !status)
        ok = color_workflow_import_name(ref, CL(source), supplied, name_size, &stored_name, &name, &status);
    if (ok && !status)
        ok = color_workflow_import_conflict(ref, context, name, replace, &active, &status);

    _Alignas(128) FSSpec directory;
    if (ok && !status) {
        const uint32_t spec[] = {name, US_TO_SYN68K(&directory)};
        ok = cfp_private(ref, 0xbbe0, spec, NELEM(spec), &status);
        if (ok && !status) {
            unsigned char empty = 0;
            const uint32_t inspect[] = {spec[1], US_TO_SYN68K(&directory_id), US_TO_SYN68K(&empty)};
            uint32_t exists;
            ok = cfp_private(ref, 0x31a20, inspect, NELEM(inspect), &exists);
            if (ok && exists && !empty && !replace)
                status = -4271;
            else if (ok && (!exists || empty))
                ok = cfp_private(ref, 0x31ae4, inspect, 2, &status);
        }
    }
    if (ok && !status) {
        _Alignas(256) Str255 file_name;
        const uint32_t convert[] = {name, US_TO_SYN68K(file_name)};
        const uint32_t clone[] = {US_TO_SYN68K(&copy), convert[1], TICK("cswi"), CL(source)};
        ok = cfp_private(ref, 0xb2c0, convert, NELEM(convert), &status) &&
             (status || cfp_private(ref, 0xa8ac, clone, NELEM(clone), &status));
    }
    if (ok && !status) {
        const uint32_t imported = CL(copy);
        const uint32_t original = CL(source);
        ok = cfp_private(ref, 0xa854, &original, 1, NULL);
        if (ok)
            source = 0;
        const uint32_t rename[] = {imported, TICK("Name"), name};
        const uint32_t publish[] = {context, imported};
        const uint32_t allocate = US_TO_SYN68K(&types);
        ok = ok && cfp_private(ref, 0xb4e0, rename, NELEM(rename), &status) &&
             (status || cfp_private(ref, 0xc4c4, publish, NELEM(publish), &status)) &&
             (status || cfp_private(ref, 0xb16c, &allocate, 1, &status));
        if (ok && !status)
            ok = color_workflow_import_profiles(ref, imported, CL(types), &directory, CL(directory_id), &status);
        if (ok && !status && !color_workflow_names_equal(CL(stored_name), name))
            ok = cfp_private(ref, 0xb4e0, rename, NELEM(rename), &status);
        if (ok && !status) {
            const uint32_t bind[] = {imported, 1, 0};
            ok = cfp_private(ref, 0xb66c, bind, NELEM(bind), &status);
        }
        if (ok && !status) {
            const uint32_t flags[][3] = {
                {imported, workflow_new_file, 1}, {imported, 0x100, 0}, {imported, 1, active}, {imported, 0x200, 1}};
            for (unsigned i = 0; ok && i < NELEM(flags); ++i)
                ok = cfp_private(ref, 0xaed8, flags[i], NELEM(flags[i]), i == NELEM(flags) - 1 ? &status : NULL);
        }
        if (ok && !status)
            ok = cfp_private(ref, 0xa880, &imported, 1, &status);
        if (ok && !status)
            ok = cfp_write(output, imported);
    }

    const int free_types = color_workflow_free(ref, &types);
    const int free_name = color_workflow_free(ref, &stored_name);
    const uint32_t release[] = {context, CL(copy), 1};
    const int removed = !copy || (ok && !status) || cfp_private(ref, 0xc06c, release, NELEM(release), NULL);
    const uint32_t original = CL(source);
    const int closed = !source || cfp_private(ref, 0xa854, &original, 1, NULL);
    return ok && free_types && free_name && removed && closed && color_profile_result(call, status);
}

static int color_workflow_dispatch(ROMlib_carbon_ppc_frame* call, enum color_workflow_operation operation,
                                   const char* name, uint32_t offset)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_load_reference("ColorSync", name, &ref))
        return ROMlib_carbon_report_failure("Missing ColorSync workflow implementation");
    if (ref.code_offset != offset || ref.code_size != 0x366ec || ref.toc_offset != 0x464 || ref.toc_size != 0x1670)
        return ROMlib_carbon_invoke_frame(ref.function, call);

    const uint32_t context = call->words[0], handle = call->words[1];
    const int query = operation == workflow_count || operation == workflow_active || operation == workflow_modified;
    if (operation == workflow_init)
        return color_workflow_init(&ref, context, call);
    if (!color_workflow_count_slots(context))
        return color_profile_result(call, query ? 0 : paramErr);
    switch (operation) {
    case workflow_count:
    case workflow_save:
        return color_workflow_scan(&ref, context, operation == workflow_save, call);
    case workflow_index:
        return color_workflow_index(&ref, call);
    case workflow_term:
    case workflow_revert: {
        const uint32_t clear[] = {context, operation == workflow_revert};
        if (!cfp_private(&ref, 0xcbb0, clear, NELEM(clear), NULL))
            return 0;
        if (operation == workflow_term) {
            _Alignas(4) uint32_t storage = CL(context);
            return color_workflow_free(&ref, &storage) && color_profile_result(call, noErr);
        }
        unsigned char* header = color_profile_memory(context, workflow_context_size, 1);
        if (!header)
            return 0;
        color_workflow_initialize(header, 1);
        uint32_t status;
        return cfp_private(&ref, 0xb0cc, &context, 1, &status) && color_profile_result(call, status);
    }
    case workflow_activate_current:
        return color_workflow_activate(&ref, call, 1);
    case workflow_delete:
        return color_workflow_delete(&ref, call);
    case workflow_import:
        return color_workflow_import_file(&ref, call);
    case workflow_export:
        return color_workflow_export_file(&ref, call);
    case workflow_copy:
        return color_workflow_copy(&ref, call);
    default:
        break;
    }

    uint32_t valid;
    if (!color_workflow_valid(&ref, handle, operation == workflow_activate, &valid))
        return 0;
    if (!valid)
        return color_profile_result(call, query ? 0 : paramErr);
    switch (operation) {
    case workflow_name:
    case workflow_description:
        return color_workflow_text(&ref, call, operation == workflow_name ? TICK("Name") : TICK("Desc"));
    case workflow_set_name:
    case workflow_set_description:
        return color_workflow_set_text(&ref, call, operation == workflow_set_name);
    case workflow_activate:
        return color_workflow_activate(&ref, call, 0);
    case workflow_get_entry:
    case workflow_set_entry: {
        const uint32_t selector = call->words[2] ? call->words[2] : TICK("Sys ");
        const unsigned kind = color_workflow_entry_kind(selector);
        if (!kind)
            return color_profile_result(call, paramErr);
        return operation == workflow_get_entry ? color_workflow_get_entry(&ref, call, selector, kind)
                                               : color_workflow_set_entry(&ref, call, selector, kind);
    }
    default:
        break;
    }

    const uint32_t record = color_profile_indirect(handle);
    if (!record)
        return 0;
    if (operation == workflow_dates) {
        const uint32_t created = call->words[2], modified = call->words[3];
        if (!created && !modified)
            return color_profile_result(call, paramErr);
        return (!created || cfp_move(created, record + 0x248, 12)) &&
               (!modified || cfp_move(modified, record + 0x274, 12)) && color_profile_result(call, noErr);
    }
    uint32_t flags;
    return cfp_get(record + (operation == workflow_active ? workflow_flags : 8), &flags) &&
           color_profile_result(call, operation == workflow_active ? (flags & 1) : (flags != 0));
}

#define CARBON_WORKFLOW(name, operation, offset)                                   \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call)                        \
    {                                                                              \
        return color_workflow_dispatch(call, workflow_##operation, #name, offset); \
    }

CARBON_WORKFLOW(CMInitWorkflows, init, 0xc620)
CARBON_WORKFLOW(CMTermWorkflows, term, 0xcd04)
CARBON_WORKFLOW(CMSaveWorkflows, save, 0xd0b0)
CARBON_WORKFLOW(CMRevertWorkflows, revert, 0xd1c4)
CARBON_WORKFLOW(CMCountWorkflows, count, 0xd238)
CARBON_WORKFLOW(CMGetIndWorkflow, index, 0xd2e8)
CARBON_WORKFLOW(CMGetWorkflowName, name, 0xe80c)
CARBON_WORKFLOW(CMGetWorkflowDesc, description, 0xe8a8)
CARBON_WORKFLOW(CMSetWorkflowName, set_name, 0xe944)
CARBON_WORKFLOW(CMSetWorkflowDesc, set_description, 0xea94)
CARBON_WORKFLOW(CMGetWorkflowDates, dates, 0xfbb0)
CARBON_WORKFLOW(CMWorkflowIsActive, active, 0xfadc)
CARBON_WORKFLOW(CMWorkflowIsModified, modified, 0xfb44)
CARBON_WORKFLOW(CMActivateWorkflow, activate, 0xe058)
CARBON_WORKFLOW(CMActivateCurrentWorkflow, activate_current, 0xe110)
CARBON_WORKFLOW(CMDeleteWorkflow, delete, 0xe640)
CARBON_WORKFLOW(CMGetWorkflowEntry, get_entry, 0xd504)
CARBON_WORKFLOW(CMSetWorkflowEntry, set_entry, 0xd78c)
CARBON_WORKFLOW(CMCopyWorkflow, copy, 0xe354)
CARBON_WORKFLOW(CMExportWorkflow, export, 0xee20)
CARBON_WORKFLOW(CMImportWorkflow, import, 0xf448)

#undef CARBON_WORKFLOW
#endif
