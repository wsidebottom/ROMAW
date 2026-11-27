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
    color_search_get_preferred_cmm,
    color_search_set_preferred_cmm,
    color_search_create_identifier,
    color_search_identifier_list,
    color_search_identifier_folder,
    color_search_world_info,
    color_search_iterate_cmms,
    color_search_iterate_folder,
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

static int color_profile_set_preferred_cmm(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t signature = call->words[0], output = call->words[1];
    unsigned char* missing = color_profile_memory(output, 1, 1);
    if (output && !missing)
        return color_profile_result(call, paramErr);
    const uint32_t preference[] = {TICK("pcmm"), (uint32_t)-16000, signature};
    uint32_t status, installed = 0;
    if (!cfp_private(ref, 0x1ccc4, preference, NELEM(preference), &status))
        return 0;
    if (!(int16_t)status && signature && !cfp_private(ref, 0x3278c, &signature, 1, &installed))
        return 0;
    if (missing)
        *missing = !(int16_t)status && signature && !installed;
    return color_profile_result(call, (int16_t)status);
}

enum {
    color_identifier_calibration = 0x80,
    color_identifier_name_length = 0x8c,
    color_identifier_fixed_size = 0x90,
};

static int color_identifier_read_tag(const ROMlib_carbon_reference* ref, uint32_t profile, uint32_t tag, uint32_t start,
                                     uint32_t* size, uint32_t output, uint32_t* status)
{
    const uint32_t args[] = {profile, tag, start, US_TO_SYN68K(size), output};
    return cfp_private(ref, 0x349b0, args, NELEM(args), status);
}

static int color_profile_identifier(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t profile = call->words[0], address = call->words[1], size_address = call->words[2];
    unsigned char* size_output = color_profile_memory(size_address, 4, 1);
    if (!profile || !size_output)
        return color_profile_result(call, paramErr);
    if (!address)
        color_write(size_output, 4, 0);

    _Alignas(4) uint32_t amount = CLC(4), description_size = 0;
    uint32_t status;
    if (!color_identifier_read_tag(ref, profile, TICK("desc"), 8, &amount, US_TO_SYN68K(&description_size), &status))
        return 0;
    if (status || CL(amount) != 4 || CL(description_size) > UINT32_MAX - color_identifier_fixed_size)
        return color_profile_result(call, status ? status : color_profile_invalid);
    const uint32_t required = color_identifier_fixed_size + CL(description_size);
    if (!address) {
        color_write(size_output, 4, required);
        return color_profile_result(call, noErr);
    }
    if (color_read(size_output, 4) < required)
        return color_profile_result(call, memSCErr);

    unsigned char* identifier = color_profile_memory(address, required, 1);
    if (!identifier)
        return color_profile_result(call, paramErr);
    ROMlib_carbon_ppc_frame header = {.words = {profile, address}};
    if (!carbon_CMGetProfileHeader(&header))
        return 0;
    if (header.words[0])
        return color_profile_result(call, header.words[0]);

    amount = CLC(12);
    if (!color_identifier_read_tag(ref, profile, TICK("calt"), 8, &amount, address + color_identifier_calibration,
                                   &status))
        return 0;
    if (status)
        memset(identifier + color_identifier_calibration, 0,
               color_identifier_name_length - color_identifier_calibration);
    color_write(identifier + color_identifier_name_length, 4, CL(description_size));
    return color_identifier_read_tag(ref, profile, TICK("desc"), 12, &description_size,
                                     address + color_identifier_fixed_size, &status) &&
           color_profile_result(call, status);
}

static const unsigned char* color_identifier_data(uint32_t address, uint32_t* length)
{
    const unsigned char* data = color_profile_memory(address, color_identifier_fixed_size, 0);
    if (!data)
        return NULL;
    const uint32_t bytes = color_read(data + color_identifier_name_length, 4);
    if (bytes > INT32_MAX - color_identifier_fixed_size - 2)
        return NULL;
    *length = bytes;
    return color_profile_memory(address, color_identifier_fixed_size + bytes, 0);
}

static int color_identifier_matches(const ROMlib_carbon_reference* ref, uint32_t profile,
                                    const unsigned char* identifier, uint32_t length, bool* matches, uint32_t* status)
{
    _Alignas(128) unsigned char header[128] = {0};
    const uint32_t get_header[] = {profile, US_TO_SYN68K(header)};
    *matches = false;
    if (!cfp_private(ref, 0x34ab8, get_header, NELEM(get_header), status))
        return 0;
    if (*status || (color_read(identifier, 4) && memcmp(identifier, header, 4)) ||
        memcmp(identifier + 8, header + 8, 28) || memcmp(identifier + 0x30, header + 0x30, 16) ||
        memcmp(identifier + 0x44, header + 0x44, 12))
        return 1;

    static const unsigned char no_date[12];
    if (memcmp(identifier + color_identifier_calibration, no_date, sizeof no_date)) {
        _Alignas(8) unsigned char date[12] = {0};
        _Alignas(4) uint32_t amount = CLC(sizeof date);
        uint32_t tag_status;
        if (!color_identifier_read_tag(ref, profile, TICK("calt"), 8, &amount, US_TO_SYN68K(date), &tag_status))
            return 0;
        if (tag_status || CL(amount) != sizeof date ||
            memcmp(identifier + color_identifier_calibration, date, sizeof date))
            return 1;
    }
    if (length) {
        _Alignas(4) uint32_t amount = CLC(4), actual = 0;
        uint32_t tag_status;
        if (!color_identifier_read_tag(ref, profile, TICK("desc"), 8, &amount, US_TO_SYN68K(&actual), &tag_status))
            return 0;
        if (tag_status || CL(amount) != 4 || CL(actual) != length)
            return 1;

        _Alignas(256) unsigned char chunk[256];
        const uint32_t buffer = US_TO_SYN68K(chunk);
        for (uint32_t offset = 0; offset < length;) {
            const uint32_t bytes = MIN(length - offset, sizeof chunk);
            amount = CL(bytes);
            if (!color_identifier_read_tag(ref, profile, TICK("desc"), 12 + offset, &amount, buffer, &tag_status))
                return 0;
            if (tag_status || CL(amount) != bytes ||
                memcmp(identifier + color_identifier_fixed_size + offset, chunk, bytes))
                return 1;
            offset += bytes;
        }
    }
    *matches = true;
    return 1;
}

static int color_identifier_list_search(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t list = call->words[1], count = call->words[2], count_address = call->words[3];
    const uint32_t output_address = call->words[4];
    uint32_t name_length;
    const unsigned char* identifier = color_identifier_data(call->words[0], &name_length);
    unsigned char* matched_count = color_profile_memory(count_address, 4, 1);
    if (!identifier || !list || !matched_count || count > UINT32_MAX / 4)
        return color_profile_result(call, paramErr);
    const uint32_t capacity = output_address ? MIN(count, color_read(matched_count, 4)) : count;
    if (!capacity) {
        color_write(matched_count, 4, 0);
        return color_profile_result(call, noErr);
    }
    const unsigned char* profiles = color_profile_memory(list, count * 4, 0);
    unsigned char* output = color_profile_memory(output_address, capacity * 4, 1);
    if (!profiles || (output_address && !output))
        return color_profile_result(call, paramErr);

    uint32_t matches = 0;
    for (uint32_t i = 0; i < count && matches < capacity; ++i) {
        const uint32_t profile = color_read(profiles + i * 4, 4);
        bool matching;
        uint32_t status;
        if (!color_identifier_matches(ref, profile, identifier, name_length, &matching, &status))
            return 0;
        if (status)
            return color_profile_result(call, status);
        if (matching) {
            if (output)
                color_write(output + matches * 4, 4, profile);
            ++matches;
        }
    }
    color_write(matched_count, 4, matches);
    return color_profile_result(call, noErr);
}

static int color_identifier_folder_search(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t count = call->words[1], output = call->words[2];
    uint32_t name_length;
    const unsigned char* identifier = color_identifier_data(call->words[0], &name_length);
    if (!output || !color_profile_memory(output, 4, 1))
        return color_profile_result(call, paramErr);
    if (!cfp_write(output, 0))
        return 0;
    if (!identifier || (count && !color_profile_memory(count, 4, 1)))
        return color_profile_result(call, paramErr);

    const uint32_t identifier_size = color_identifier_fixed_size + name_length;
    uint32_t callback;
    if (!cfp_get(US_TO_SYN68K(ref->toc) - 0x10, &callback) || !callback)
        return ROMlib_carbon_invoke_frame(ref->function, call);

    uint32_t search;
    if (!cfp_private(ref, 0x16de0, NULL, 0, &search))
        return 0;
    if (!search)
        return color_profile_result(call, memFullErr);

    _Alignas(4) uint32_t reference = 0, generation = 0;
    const uint32_t storage = US_TO_SYN68K(&reference);
    const uint32_t allocate[] = {storage, identifier_size + 2};
    const uint32_t generation_address = US_TO_SYN68K(&generation);
    const uint32_t descriptor_args[] = {callback, 0x3d0, 1};
    uint32_t status, descriptor = 0, extent;
    int ok = cfp_private(ref, 0x2dbb8, allocate, NELEM(allocate), &status);
    if (ok && !status)
        ok = cfp_move(CL(reference), call->words[0], identifier_size) &&
             cfp_private(ref, 0x350e8, descriptor_args, NELEM(descriptor_args), &descriptor);
    if (ok && !status && !descriptor)
        status = memFullErr;
    if (ok && !status) {
        unsigned char* data = color_search_data(search, &extent);
        if (!data || extent < color_search_record_size)
            ok = 0;
        else {
            unsigned char* filter = data + color_search_filter;
            memset(filter, 0, color_search_filter_size);
            memcpy(filter + 4, identifier + 0x0c, 12);
            memcpy(filter + 16, identifier + 0x30, 16);
            color_write(filter + 0x24, 4, 0x7e);
            color_write(data + color_search_callback, 4, descriptor);
            color_write(data + color_search_refcon, 4, CL(reference));
            descriptor = reference = 0; // The search owns both allocations.
            ok = cfp_private(ref, 0x30f4, &generation_address, 1, &status);
            if (ok && !status) {
                data = color_search_data(search, &extent);
                if (!data)
                    ok = 0;
                else {
                    color_write(data + color_search_generation, 4, CL(generation));
                    ok = color_search_run(ref, search, 0, count, &status);
                }
            }
        }
    }
    if (ok && !status && cfp_write(output, search))
        return color_profile_result(call, noErr);

    const int descriptor_released = !descriptor || cfp_private(ref, 0x35100, &descriptor, 1, NULL);
    const int reference_released = !reference || cfp_private(ref, 0x2de5c, &storage, 1, NULL);
    return color_search_destroy(ref, search) && descriptor_released && reference_released && ok &&
           color_profile_result(call, status ? status : paramErr);
}

static int color_world_info(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    uint32_t extent;
    const unsigned char* world = cfp_handle_data(call->words[0], &extent, 0);
    unsigned char* output = color_profile_memory(call->words[1], 20, 1);
    if (!world || extent < 20 || !output)
        return color_profile_result(call, paramErr);

    uint32_t info[5] = {0};
    const bool second = (color_read(world, 4) & 2) != 0;
    info[0] = second ? 2 : 1;
    for (unsigned i = 0; i < info[0]; ++i) {
        _Alignas(8) unsigned char description[20] = {0};
        const uint32_t component[] = {color_read(world + 4 + 4 * i, 4), US_TO_SYN68K(description), 0, 0, 0};
        uint32_t status;
        if (!cfp_private(ref, 0x326a0, component, NELEM(component), &status))
            return 0;
        if (status)
            return color_profile_result(call, status);
        world = cfp_handle_data(call->words[0], &extent, 0);
        if (!world || extent < 20)
            return color_profile_result(call, paramErr);
        info[1 + i * 2] = color_read(description + 4, 4);
        info[2 + i * 2] = color_read(world + 12 + 4 * i, 4);
    }
    for (unsigned i = 0; i < NELEM(info); ++i)
        color_write(output + 4 * i, 4, info[i]);
    return color_profile_result(call, noErr);
}

static int color_enumeration_release(const ROMlib_carbon_reference* ref, uint32_t* encoded)
{
    const uint32_t address = US_TO_SYN68K(encoded);
    return !*encoded || cfp_private(ref, 0x2de5c, &address, 1, NULL);
}

static int color_iterate_cmms(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    enum { cmm_info_size = 0x378, cmm_minimum_version = 0x10000 };

    const uint32_t callback = call->words[0], count_address = call->words[1], context = call->words[2];
    if ((!callback && !count_address) || (count_address && !color_profile_memory(count_address, 4, 1)))
        return color_profile_result(call, paramErr);

    _Alignas(4) uint32_t encoded = 0;
    const uint32_t allocate[] = {US_TO_SYN68K(&encoded), cmm_info_size};
    uint32_t status;
    if (!cfp_private(ref, 0x2d6e0, allocate, NELEM(allocate), &status))
        return 0;
    if (status)
        return color_enumeration_release(ref, &encoded) && color_profile_result(call, status);

    const uint32_t info = CL(encoded);
    unsigned char* bytes = color_profile_memory(info, cmm_info_size, 1);
    if (!bytes)
        return color_enumeration_release(ref, &encoded) && color_profile_result(call, memFullErr);
    _Alignas(8) uint32_t description[5] = {CL(TICK("cmm "))};
    uint32_t next[] = {0, US_TO_SYN68K(description)};
    uint32_t delivered = 0;
    int ok = cfp_private(ref, 0x35028, next, NELEM(next), &next[0]);
    while (ok && !status && next[0]) {
        const uint32_t component = next[0];
        uint32_t instance;
        if (!cfp_private(ref, 0x35040, &component, 1, &instance)) {
            ok = 0;
            break;
        }
        if (!instance)
            status = -175;
        else {
            memset(bytes, 0, cmm_info_size);
            color_write(bytes, 4, cmm_info_size);
            const uint32_t read[] = {instance, info};
            ok = cfp_private(ref, 0x327e4, read, NELEM(read), &status);
            if (ok && !status && color_read(bytes + 12, 4) >= cmm_minimum_version) {
                if (callback) {
                    const uint32_t invoke[] = {callback, 0x3e0, info, context};
                    ok = cfp_private(ref, 0x350d0, invoke, NELEM(invoke), &status);
                    status = (int16_t)status;
                }
                if (ok && !status)
                    ++delivered;
            }
            ok = cfp_private(ref, 0x35058, &instance, 1, NULL) && ok;
        }
        if (ok)
            ok = cfp_private(ref, 0x35028, next, NELEM(next), &next[0]);
    }
    if (ok && count_address)
        ok = cfp_write(count_address, delivered);
    return color_enumeration_release(ref, &encoded) && ok && color_profile_result(call, status);
}

static int color_folder_callback(const ROMlib_carbon_reference* ref, uint32_t source, uint32_t callback,
                                 uint32_t context, uint32_t* status)
{
    _Alignas(16) unsigned char item[0x298] = {0};
    const unsigned char* profile = color_profile_memory(source, 0x600, 0);
    if (!profile)
        return 0;
    color_write(item, 4, 0x20000);
    memcpy(item + 4, profile, 128);
    const uint32_t location[] = {US_TO_SYN68K(item + 0x186), source + 0x80};
    if (!cfp_private(ref, 0x31890, location, NELEM(location), status))
        return 0;
    if (*status) {
        *status = noErr; // The file no longer has an accessible location.
        return 1;
    }

    if (profile[0xc8]) {
        memcpy(item + 0x84, profile + 0xc6, 2);
        memcpy(item + 0x86, profile + 0xc8, (size_t)profile[0xc8] + 1);
    }
    else {
        const uint32_t ascii_size = color_read(profile + 0x1c8, 4);
        if (ascii_size) {
            const unsigned count = MIN(ascii_size - 1, 255u);
            color_write(item + 0x84, 2, UINT16_MAX);
            item[0x86] = count;
            memcpy(item + 0x87, profile + 0x1cc, count);
        }
    }
    color_write(item + 0x288, 4, color_read(profile + 0x2cc, 4));
    color_write(item + 0x28c, 4, source + 0x2d0);
    color_write(item + 0x290, 4, source + 0x1cc);
    if (color_read(profile + 0x4d0, 4))
        color_write(item + 0x294, 4, source + 0x4d4);
    const uint32_t invoke[] = {callback, 0x3e0, US_TO_SYN68K(item), context};
    if (!cfp_private(ref, 0x350d0, invoke, NELEM(invoke), status))
        return 0;
    *status = (int16_t)*status;
    return 1;
}

static int color_iterate_folder(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t callback = call->words[0], seed_address = call->words[1], count_address = call->words[2];
    unsigned char* seed = color_profile_memory(seed_address, 4, 1);
    unsigned char* count = color_profile_memory(count_address, 4, 1);
    if ((seed_address && !seed) || (count_address && !count))
        return color_profile_result(call, paramErr);
    _Alignas(4) uint32_t summary[3] = {0};
    const uint32_t folder[] = {US_TO_SYN68K(&summary[0]), US_TO_SYN68K(&summary[1]), US_TO_SYN68K(&summary[2])};
    uint32_t status;
    if (!cfp_private(ref, 0x3b0c, folder, NELEM(folder), &status))
        return 0;
    if (status)
        return color_profile_result(call, status);
    if (count)
        color_write(count, 4, CL(summary[1]));
    if (seed) {
        const uint32_t previous = color_read(seed, 4);
        color_write(seed, 4, CL(summary[0]));
        if (previous == CL(summary[0]))
            return color_profile_result(call, noErr);
    }
    if (!callback)
        return color_profile_result(call, noErr);

    _Alignas(4) uint16_t file = 0;
    _Alignas(4) uint32_t encoded = 0;
    const uint32_t open[] = {US_TO_SYN68K(&file), 1};
    int ok = cfp_private(ref, 0x3c10, open, NELEM(open), &status);
    if (ok && !status) {
        const uint32_t allocate[] = {US_TO_SYN68K(&encoded), 0x600};
        ok = cfp_private(ref, 0x2dbb8, allocate, NELEM(allocate), &status);
    }
    for (uint32_t index = 0; ok && !status; ++index) {
        const uint32_t read[] = {(int16_t)CW(file), index, CL(encoded)};
        ok = cfp_private(ref, 0x3d14, read, NELEM(read), &status);
        if (ok && status == (uint32_t)paramErr) {
            status = noErr;
            break;
        }
        if (ok && !status)
            ok = color_folder_callback(ref, CL(encoded), callback, call->words[3], &status);
    }
    const int released = color_enumeration_release(ref, &encoded);
    const int closed = (int16_t)CW(file) <= 0 || cfp_private(ref, 0x3d84, open, 1, NULL);
    return released && closed && ok && color_profile_result(call, status);
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
    if (operation == color_search_get_preferred_cmm) {
        uint32_t entry, wrapper;
        if (!cfp_get(US_TO_SYN68K(ref.toc) + 0x254, &entry) || !cfp_get(US_TO_SYN68K(ref.function), &wrapper))
            return 0;
        if (entry != wrapper - (0x10094 - 0xffdc))
            return ROMlib_carbon_invoke_frame(ref.function, call);
        return color_profile_preferred_cmm(&ref, call);
    }
    if (operation == color_search_set_preferred_cmm)
        return color_profile_set_preferred_cmm(&ref, call);
    if (operation == color_search_create_identifier)
        return color_profile_identifier(&ref, call);
    if (operation == color_search_identifier_list)
        return color_identifier_list_search(&ref, call);
    if (operation == color_search_identifier_folder)
        return color_identifier_folder_search(&ref, call);
    if (operation == color_search_world_info)
        return color_world_info(&ref, call);
    if (operation == color_search_iterate_cmms)
        return color_iterate_cmms(&ref, call);
    if (operation == color_search_iterate_folder)
        return color_iterate_folder(&ref, call);
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
CARBON_COLOR_SEARCH(CMGetPreferredCMM, get_preferred_cmm, 0x10094)
CARBON_COLOR_SEARCH(CMSetPreferredCMMGlobal, set_preferred_cmm, 0x11130)
CARBON_COLOR_SEARCH(CMCreateProfileIdentifier, create_identifier, 0x1ac4c)
CARBON_COLOR_SEARCH(CMProfileIdentifierListSearch, identifier_list, 0x1ab2c)
CARBON_COLOR_SEARCH(CMProfileIdentifierFolderSearch, identifier_folder, 0x1a594)
CARBON_COLOR_SEARCH(CMGetCWInfo, world_info, 0xfea8)
CARBON_COLOR_SEARCH(CMIterateCMMInfo, iterate_cmms, 0xffb8)
CARBON_COLOR_SEARCH(CMIterateColorSyncFolder, iterate_folder, 0x16a60)

#undef CARBON_COLOR_SEARCH
#endif
