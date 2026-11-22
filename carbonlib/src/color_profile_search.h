/*+===================================================================
  File:      color_profile_search.h

  Summary:   Implements ColorSync profile search records and validation through the installed CMMs.

  Classes:   None.

  Functions: Private PowerPC frame adapters for ColorSync search and validation exports.

  Origin:    Mac OS 9.2.1 ColorSync 3.0.4 and Universal Interfaces 3.4.2.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_COLOR_PROFILE_SEARCH_H
#define ROMAW_CARBON_COLOR_PROFILE_SEARCH_H

#include "cf_batch.h"
#include "color_profiles.h"

enum color_search_operation {
    color_search_create,
    color_search_update,
    color_search_dispose,
    color_search_file,
    color_search_open,
    color_search_validate,
    color_search_preferred_cmm,
};

enum {
    color_search_record_size = 0x84,
    color_search_filter_size = 0x2c,
    color_search_filter = 4,
    color_search_callback = 0x2c,
    color_search_generation = 0x30,
    color_search_refcon = 0x34,
    color_search_count = 0x38,
    color_search_items = 0x3c,
    color_search_invalid = -4206,
};

static unsigned char* color_search_data(uint32_t search, uint32_t* extent)
{
    unsigned char* data = cfp_handle_data(search, extent, 1);
    return data && *extent >= color_search_items && color_read(data, 4) == TICK("pamg") ? data : NULL;
}

static int color_search_run(const ROMlib_carbon_reference* ref, uint32_t handle, uint32_t refcon, uint32_t output,
                            uint32_t* status)
{
    const uint32_t args[] = {handle, refcon, output};
    return cfp_private(ref, 0x16fb4, args, NELEM(args), status);
}

static int color_search_destroy(const ROMlib_carbon_reference* ref, uint32_t handle)
{
    _Alignas(4) uint32_t storage = CL(handle);
    const uint32_t location = US_TO_SYN68K(&storage);
    return cfp_private(ref, 0x16e38, &location, 1, NULL);
}

static int color_search_new(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    const unsigned char* filter = color_profile_memory(args[0], color_search_filter_size, 0);
    unsigned char* output = color_profile_memory(args[3], 4, 1);
    if (!filter || !output)
        return color_profile_result(call, paramErr);

    uint32_t handle, extent, status;
    if (!cfp_private(ref, 0x16de0, NULL, 0, &handle))
        return 0;
    if (!handle)
        return color_profile_result(call, memFullErr);
    unsigned char* data = color_search_data(handle, &extent);
    if (!data || extent < color_search_record_size)
        return color_search_destroy(ref, handle) && color_profile_result(call, color_search_invalid);
    memmove(data + color_search_filter, filter, color_search_filter_size);
    color_write(data + color_search_refcon, 4, 0);
    _Alignas(4) uint32_t generation = 0;
    const uint32_t location = US_TO_SYN68K(&generation);
    if (!cfp_private(ref, 0x30f4, &location, 1, &status)) {
        color_search_destroy(ref, handle);
        return 0;
    }
    if (!status) {
        data = color_search_data(handle, &extent);
        if (!data) {
            color_search_destroy(ref, handle);
            return 0;
        }
        color_write(data + color_search_generation, 4, CL(generation));
        if (!color_search_run(ref, handle, args[1], args[2], &status)) {
            color_search_destroy(ref, handle);
            return 0;
        }
    }
    data = color_search_data(handle, &extent);
    if (!data)
        return 0;
    if (color_read(data + color_search_callback, 4))
        color_write(data + color_search_callback, 4, UINT32_MAX);
    if (status && !color_search_destroy(ref, handle))
        return 0;
    color_write(output, 4, status ? 0 : handle);
    return color_profile_result(call, status);
}

static int color_search_update_existing(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    _Alignas(4) uint32_t generation = 0;
    const uint32_t location = US_TO_SYN68K(&generation);
    uint32_t status, extent;
    if (!cfp_private(ref, 0x30f4, &location, 1, &status))
        return 0;
    if (!status) {
        unsigned char* data = color_search_data(args[0], &extent);
        if (!data)
            return 0;
        if (CL(generation) == color_read(data + color_search_generation, 4)) {
            if (!args[2])
                return color_profile_result(call, paramErr);
            return cfp_write(args[2], color_read(data + color_search_count, 4)) && color_profile_result(call, noErr);
        }
        color_write(data + color_search_generation, 4, CL(generation));
    }
    return color_search_run(ref, args[0], args[1], args[2], &status) && color_profile_result(call, status);
}

static int color_search_entry(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call,
                              enum color_search_operation operation)
{
    const uint32_t* args = call->words;
    if (operation == color_search_create)
        return color_search_new(ref, call);
    if ((operation == color_search_file || operation == color_search_open) && !args[2])
        return color_profile_result(call, paramErr);
    uint32_t extent;
    unsigned char* data = color_search_data(args[0], &extent);
    if (operation == color_search_dispose)
        return (!data || color_search_destroy(ref, args[0])) && color_profile_result(call, noErr);
    if (!data)
        return color_profile_result(call, color_search_invalid);
    if (operation == color_search_update)
        return color_search_update_existing(ref, call);

    const uint32_t index = args[1];
    if (!index || index > color_read(data + color_search_count, 4) ||
        index > (extent - color_search_items) / sizeof(FSSpec))
        return color_profile_result(call, color_profile_index_error);
    const unsigned char* entry = data + color_search_items + (index - 1) * sizeof(FSSpec);
    if (operation == color_search_file) {
        void* output = color_profile_memory(args[2], sizeof(FSSpec), 1);
        if (!output)
            return color_profile_result(call, paramErr);
        memmove(output, entry, sizeof(FSSpec));
        return color_profile_result(call, noErr);
    }

    _Alignas(4) unsigned char location[258] = {0};
    color_write(location, 2, 1);
    memcpy(location + 2, entry, sizeof(FSSpec));
    const uint32_t open[] = {args[2], US_TO_SYN68K(location)};
    uint32_t status;
    return cfp_private(ref, 0x34a10, open, NELEM(open), &status) && color_profile_result(call, status);
}

static int color_profile_cmm_candidates(const ROMlib_carbon_reference* ref, uint32_t profile, uint32_t candidates[4],
                                        unsigned* count)
{
    _Alignas(4) uint32_t preferred = 0, embedded = 0;
    unsigned char unavailable = 0;
    const uint32_t global[] = {US_TO_SYN68K(&preferred), US_TO_SYN68K(&unavailable)};
    const uint32_t local[] = {profile, US_TO_SYN68K(&embedded)};
    const uint32_t standard = TICK("appl");
    uint32_t installed;
    if (!cfp_private(ref, 0x34d40, global, NELEM(global), NULL) ||
        !cfp_private(ref, 0x158b0, local, NELEM(local), NULL) || !cfp_private(ref, 0x3278c, &standard, 1, &installed))
        return 0;

    *count = 0;
    if (preferred && !unavailable)
        candidates[(*count)++] = preferred;
    if (embedded)
        candidates[(*count)++] = embedded == preferred && !unavailable ? 0 : embedded;
    if (installed)
        candidates[(*count)++] =
            (preferred == CL(standard) && !unavailable) || embedded == CL(standard) ? 0 : CL(standard);
    return 1;
}

static int color_profile_validate(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t profile = call->words[0];
    unsigned char* valid = color_profile_memory(call->words[1], 1, 1);
    unsigned char* missing = color_profile_memory(call->words[2], 1, 1);
    uint32_t status = paramErr;
    _Alignas(4) uint32_t candidates[4] = {0}, position = 0, component = 0;
    unsigned char accepted = 0, preferred_missing = 0;
    if (profile) {
        unsigned count;
        if (!color_profile_cmm_candidates(ref, profile, candidates, &count))
            return 0;
        const uint32_t open[] = {US_TO_SYN68K(candidates), count,
                                 US_TO_SYN68K(&position),  8,
                                 US_TO_SYN68K(&component), US_TO_SYN68K(&preferred_missing)};
        if (!cfp_private(ref, 0x102d8, open, NELEM(open), &status))
            return 0;
        const uint32_t connection = CL(component);
        int ok = 1;
        if (!status) {
            const uint32_t validate[] = {connection, profile, US_TO_SYN68K(&accepted)};
            ok = cfp_private(ref, 0x32c4c, validate, NELEM(validate), &status);
        }
        if (connection && !cfp_private(ref, 0x326ec, &connection, 1, NULL))
            return 0;
        if (!ok)
            return 0;
    }
    if (valid)
        *valid = accepted;
    if (missing)
        *missing = preferred_missing;
    return color_profile_result(call, status);
}

static int color_profile_preferred_cmm(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    unsigned char* signature = color_profile_memory(call->words[0], 4, 1);
    unsigned char* available = color_profile_memory(call->words[1], 1, 1);
    uint32_t found = 0;
    if (signature) {
        _Alignas(4) uint32_t preferred = 0;
        const uint32_t lookup[] = {TICK("pcmm"), (uint32_t)-16000, US_TO_SYN68K(&preferred)};
        uint32_t status;
        if (!cfp_private(ref, 0x1cc48, lookup, NELEM(lookup), &status))
            return 0;
        const uint32_t value = CL(preferred);
        if (!(int16_t)status && value) {
            if (!cfp_private(ref, 0x3278c, &value, 1, &status))
                return 0;
            found = status == 0;
        }
        color_write(signature, 4, value);
    }
    if (available)
        *available = found;
    return color_profile_result(call, signature ? noErr : paramErr);
}

static int color_search_operation(ROMlib_carbon_ppc_frame* call, enum color_search_operation operation,
                                  const char* name, uint32_t offset)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_load_reference("ColorSync", name, &ref))
        return ROMlib_carbon_report_failure("Missing ColorSync profile search implementation");
    if (ref.code_offset != offset || ref.code_size != 0x366ec || ref.toc_offset != 0x464 || ref.toc_size != 0x1670)
        return ROMlib_carbon_invoke_frame(ref.function, call);
    if (operation == color_search_validate)
        return color_profile_validate(&ref, call);
    if (operation == color_search_preferred_cmm)
        return color_profile_preferred_cmm(&ref, call);
    return color_search_entry(&ref, call, operation);
}

#define CARBON_COLOR_SEARCH(name, operation, offset)                                  \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call)                           \
    {                                                                                 \
        return color_search_operation(call, color_search_##operation, #name, offset); \
    }

CARBON_COLOR_SEARCH(CMNewProfileSearch, create, 0x16734)
CARBON_COLOR_SEARCH(CMUpdateProfileSearch, update, 0x16844)
CARBON_COLOR_SEARCH(CMDisposeProfileSearch, dispose, 0x168f0)
CARBON_COLOR_SEARCH(CMSearchGetIndProfileFileSpec, file, 0x16930)
CARBON_COLOR_SEARCH(CMSearchGetIndProfile, open, 0x169b0)
CARBON_COLOR_SEARCH(CMValidateProfile, validate, 0x10450)
CARBON_COLOR_SEARCH(CMGetPreferredCMMGlobal, preferred_cmm, 0xffdc)

#undef CARBON_COLOR_SEARCH
#endif
