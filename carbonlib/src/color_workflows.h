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

#undef CARBON_WORKFLOW
#endif
