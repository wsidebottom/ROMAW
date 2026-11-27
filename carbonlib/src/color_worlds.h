/*+===================================================================
  File:      color_worlds.h

  Summary:   Dispatches ColorSync world operations through the installed color management modules.

  Classes:   None.

  Functions: Private ColorSync world adapters for the Carbon export registry.

  Origin:    Mac OS 9.2.1 ColorSync 3.0.4 and Universal Interfaces 3.4.2.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_COLOR_WORLDS_H
#define ROMAW_CARBON_COLOR_WORLDS_H

#include "cf_batch.h"
#include "color_profiles.h"

enum color_world_operation {
    color_world_new,
    color_world_dispose,
    color_world_match_colors,
    color_world_check_colors,
    color_world_match_bitmap,
    color_world_check_bitmap,
    color_world_match_pixmap,
    color_world_check_pixmap,
    color_world_concat_legacy,
    color_world_concat_extended,
    color_world_link_legacy,
    color_world_link_extended,
};

enum {
    color_world_size = 36,
    color_world_bad_space = -4209,
    color_world_bad_source = -4210,
    color_world_bad_destination = -4211,
    color_world_cmm_cancelled = -4217,
    color_bitmap_size = 32,
};

static unsigned char* color_world_data(uint32_t handle, int writing)
{
    uint32_t size;
    unsigned char* record = cfp_handle_data(handle, &size, writing);
    return record && size >= color_world_size ? record : NULL;
}

static int color_world_release(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t handle = call->words[0];
    if (!handle)
        return 1;
    unsigned char* world = color_world_data(handle, 1);
    if (!world)
        return ROMlib_carbon_invoke_frame(ref->function, call);

    for (unsigned offset = 4; offset <= 8; offset += 4) {
        const uint32_t component = color_read(world + offset, 4);
        if (component && !cfp_private(ref, 0x326ec, &component, 1, NULL))
            return 0;
        world = color_world_data(handle, 1);
        if (!world)
            return 0;
        color_write(world + offset, 4, 0);
    }
    _Alignas(4) uint32_t encoded = CL(handle);
    const uint32_t address = US_TO_SYN68K(&encoded);
    return cfp_private(ref, 0x2deb0, &address, 1, NULL);
}

static int color_world_colors(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call, bool check)
{
    const uint32_t handle = call->words[0], colors = call->words[1], count = call->words[2];
    const uint32_t gamut = call->words[3];
    const unsigned char* world = color_world_data(handle, 0);
    if (!world || !colors || count > UINT32_MAX / 8 || !color_profile_memory(colors, count * 8, 1) ||
        (check && (!gamut || !color_profile_memory(gamut, 4 * (((uint64_t)count + 31) / 32), 1))))
        return color_profile_result(call, paramErr);

    const uint32_t first = color_read(world + 4, 4), second = color_read(world + 8, 4);
    if (!first)
        return color_profile_result(call, paramErr);
    const uint32_t match[] = {first, colors, count};
    uint32_t status = noErr;
    if (!check || second) {
        if (!cfp_private(ref, 0x32c04, match, NELEM(match), &status))
            return 0;
        if (status || !second)
            return color_profile_result(call, status);

        world = color_world_data(handle, 0);
        if (!world)
            return 0;
        const uint32_t convert[] = {color_read(world + 20, 4), color_read(world + 24, 4), colors, count};
        if (!cfp_private(ref, 0x14820, convert, NELEM(convert), &status))
            return 0;
        if (status)
            return color_profile_result(call, status);
    }
    const uint32_t final[] = {second ? second : first, colors, count, gamut};
    return cfp_private(ref, check ? 0x32c28 : 0x32c04, final, check ? 4 : 3, &status) &&
           color_profile_result(call, status);
}

static int color_world_legacy_rgb(const ROMlib_carbon_reference* ref, unsigned char* source, unsigned char* destination)
{
    const bool source_24 = color_read(source + 20, 4) == 0x801 && color_read(source + 16, 4) == 24;
    const bool destination_24 =
        destination && color_read(destination + 20, 4) == 0x801 && color_read(destination + 16, 4) == 24;
    if (!source_24 && !destination_24)
        return 1;
    _Alignas(4) uint32_t application = 0;
    const uint32_t output = US_TO_SYN68K(&application);
    uint32_t status;
    if (!cfp_private(ref, 0x1246c, &output, 1, &status))
        return 0;
    if (!status && (CL(application) == TICK("8BIM") || CL(application) == TICK("AD65"))) {
        if (source_24)
            color_write(source + 16, 4, 32);
        if (destination_24)
            color_write(destination + 16, 4, 32);
    }
    return 1;
}

static int color_world_bitmap(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call, bool check)
{
    const uint32_t handle = call->words[0], source_address = call->words[1];
    const uint32_t callback = call->words[2], context = call->words[3];
    uint32_t destination_address = call->words[4];
    const unsigned char* world = color_world_data(handle, 0);
    if (!world)
        return color_profile_result(call, paramErr);
    unsigned char* source = color_profile_memory(source_address, color_bitmap_size, !check);
    if (!source)
        return color_profile_result(call, color_world_bad_source);
    unsigned char* destination = color_profile_memory(destination_address, color_bitmap_size, !check);
    if ((check || destination_address) && !destination)
        return color_profile_result(call, color_world_bad_destination);

    if (!check && !color_world_legacy_rgb(ref, source, destination))
        return 0;
    uint32_t status;
    if (!cfp_private(ref, 0x12c54, &source_address, 1, &status))
        return 0;
    if (status && !check)
        return color_profile_result(call, color_world_bad_source);
    world = color_world_data(handle, 0);
    if (!world)
        return 0;

    _Alignas(32) unsigned char in_place[color_bitmap_size];
    const bool same = !destination_address;
    if (same) {
        destination_address = US_TO_SYN68K(in_place);
        destination = in_place;
        const uint32_t choose[] = {source_address, destination_address, color_read(world + 32, 4)};
        if (!cfp_private(ref, 0x2f90c, choose, NELEM(choose), &status))
            return 0;
        if (status)
            return color_profile_result(call, color_world_bad_destination);
    }
    if (!check) {
        if (!cfp_private(ref, 0x12c54, &destination_address, 1, &status))
            return 0;
        if (status)
            return color_profile_result(call, color_world_bad_destination);
    }

    uint32_t component;
    world = color_world_data(handle, 0);
    if (!world)
        return 0;
    component = color_read(world + 4, 4);
    if (color_read(world, 4) & 1) {
        const uint32_t feature[] = {component, check ? 10 : 9};
        uint32_t supported;
        if (!cfp_private(ref, 0x326c4, feature, NELEM(feature), &supported))
            return 0;
        if (supported) {
            const uint32_t direct[] = {component, source_address, callback, context, destination_address};
            if (!cfp_private(ref, check ? 0x32c94 : 0x32c70, direct, NELEM(direct), &status))
                return 0;
            if (!status && same)
                memcpy(source, in_place, sizeof in_place);
            if (!status || status == (uint32_t)(check ? color_world_cmm_cancelled : userCanceledErr))
                return color_profile_result(call, status);
        }
    }

    if (memcmp(source + 4, destination + 4, 8) ||
        (check && (color_read(destination + 20, 4) != 0xb0c || color_read(destination + 16, 4) != 1)))
        return color_profile_result(call, color_world_bad_destination);
    world = color_world_data(handle, 0);
    if (!world)
        return 0;
    _Alignas(4) unsigned char preserve_alpha = 0;
    const uint32_t source_space[] = {color_read(world + 28, 4), source_address, 0};
    uint32_t source_ok, destination_ok = 1;
    if (!cfp_private(ref, 0x12bb0, source_space, NELEM(source_space), &source_ok))
        return 0;
    if (!check) {
        const uint32_t destination_space[] = {color_read(world + 32, 4), destination_address,
                                              US_TO_SYN68K(&preserve_alpha)};
        if (!cfp_private(ref, 0x12bb0, destination_space, NELEM(destination_space), &destination_ok))
            return 0;
    }
    if (!source_ok || !destination_ok)
        return color_profile_result(call, color_world_bad_space);

    const uint32_t generic[] = {check, handle, source_address, destination_address, preserve_alpha, callback, context};
    if (!cfp_private(ref, 0x120fc, generic, NELEM(generic), &status))
        return 0;
    if (!status && same)
        memcpy(source, in_place, sizeof in_place);
    return color_profile_result(call, status);
}

static int color_world_pixmap(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call, bool check)
{
    const uint32_t handle = call->words[0], pixel_map = call->words[1], bitmap = call->words[4];
    const unsigned char* world = color_world_data(handle, 0);
    const unsigned char* pixels = color_profile_memory(pixel_map, sizeof(PixMap), 0);
    const unsigned char* mask = check ? color_profile_memory(bitmap, sizeof(BitMap), 0) : NULL;
    if (!world)
        return color_profile_result(call, paramErr);
    if (!pixels)
        return color_profile_result(call, color_world_bad_source);
    if (check && !mask)
        return color_profile_result(call, color_world_bad_destination);
    if ((int16_t)color_read(pixels + 0x20, 2) <= 0)
        return color_profile_result(call, color_world_bad_source);
    if (color_read(world + 28, 4) != TICK("RGB ") || (!check && color_read(world + 32, 4) != TICK("RGB ")))
        return color_profile_result(call, color_world_bad_space);
    if (check && memcmp(pixels + 6, mask + 6, 8))
        return color_profile_result(call, color_world_bad_destination);

    _Alignas(32) unsigned char source[color_bitmap_size], destination[color_bitmap_size];
    _Alignas(4) uint32_t palette = 0;
    const uint32_t source_address = US_TO_SYN68K(source), destination_address = US_TO_SYN68K(destination);
    const uint32_t convert[] = {pixel_map, source_address, US_TO_SYN68K(&palette)};
    uint32_t status;
    if (!cfp_private(ref, 0x12d08, convert, NELEM(convert), &status))
        return 0;
    if (status)
        return color_profile_result(call, color_world_bad_source);
    if (check) {
        const uint32_t mask_args[] = {bitmap, destination_address};
        if (!cfp_private(ref, 0x12df0, mask_args, NELEM(mask_args), &status))
            return 0;
        if (status)
            return color_profile_result(call, color_world_bad_destination);
        if (palette) {
            const uint32_t indexed[] = {CL(palette),         handle,         source_address,
                                        destination_address, call->words[2], call->words[3]};
            return cfp_private(ref, 0x11d10, indexed, NELEM(indexed), &status) && color_profile_result(call, status);
        }
    }
    else if (palette) {
        const uint32_t indexed[] = {CL(palette), handle};
        return cfp_private(ref, 0x11fcc, indexed, NELEM(indexed), &status) && color_profile_result(call, status);
    }
    else
        memcpy(destination, source, sizeof destination);

    ROMlib_carbon_ppc_frame arguments = {
        .words = {handle, source_address, call->words[2], call->words[3], destination_address}};
    return color_world_bitmap(ref, &arguments, check) && color_profile_result(call, arguments.words[0]);
}

typedef struct {
    uint32_t set;
    uint32_t cmm;
    uint32_t key_profile;
    uint32_t first_profile;
    uint32_t last_profile;
    uint32_t first_transform;
    uint32_t last_transform;
} color_world_chain;

static int color_world_read_chain(uint32_t address, bool extended, bool link, color_world_chain* chain)
{
    const unsigned char* data = color_profile_memory(address, extended ? 16 : 4, 0);
    if (!data)
        return paramErr;
    // The CMM receives the full count. The reference only supports a 16-bit chain index.
    const uint32_t entries = color_read(data + (extended ? 12 : 2), extended ? 4 : 2);
    if (entries > UINT16_MAX)
        return paramErr;
    const unsigned key = extended ? 0 : color_read(data, 2);
    const unsigned first = extended ? 24 : 4;
    const unsigned stride = extended ? 12 : 4;
    if (!entries || key >= entries || !color_profile_memory(address, first + (entries - 1) * stride + 4, 0))
        return paramErr;

    const unsigned char* begin = data + first;
    const unsigned char* end = begin + (entries - 1) * stride;
    *chain = (color_world_chain){
        .set = address,
        .cmm = extended ? color_read(data, 4) : 0,
        .key_profile = extended ? 0 : color_read(begin + key * stride, 4),
        .first_profile = color_read(begin, 4),
        .last_profile = color_read(end, 4),
        .first_transform = extended ? color_read(begin - 4, 4) : 1,
        .last_transform = extended ? color_read(end - 4, 4) : (entries == 1 ? 1 : 2),
    };
    if (!extended && !chain->key_profile)
        return paramErr;
    if (extended && !link && (!chain->first_profile || !chain->last_profile))
        return -176;
    return noErr;
}

static int color_world_chain_candidates(const ROMlib_carbon_reference* ref, const color_world_chain* chain,
                                        uint32_t choices[4], unsigned* count)
{
    _Alignas(4) uint32_t preferred = 0, embedded = 0;
    unsigned char unavailable = 0;
    const uint32_t preference[] = {US_TO_SYN68K(&preferred), US_TO_SYN68K(&unavailable)};
    const uint32_t profile[] = {chain->key_profile, US_TO_SYN68K(&embedded)};
    const uint32_t apple = TICK("appl");
    uint32_t installed;
    if (!cfp_private(ref, 0x34d40, preference, NELEM(preference), NULL) ||
        (chain->key_profile && !cfp_private(ref, 0x158b0, profile, NELEM(profile), NULL)) ||
        !cfp_private(ref, 0x3278c, &apple, 1, &installed))
        return 0;

    *count = 0;
    if (chain->cmm)
        choices[(*count)++] = CL(chain->cmm);
    if (preferred && !unavailable)
        choices[(*count)++] = preferred;
    if (embedded)
        choices[(*count)++] = embedded;
    if (installed)
        choices[(*count)++] = CL(apple);
    for (unsigned i = 0; i < *count; ++i)
        for (unsigned j = i + 1; j < *count; ++j)
            if (choices[i] == choices[j])
                choices[j] = 0;
    return 1;
}

typedef struct {
    uint32_t selector;
    uint32_t component_call;
    uint32_t generator;
} color_postscript_method;

static int color_world_postscript(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call,
                                  color_postscript_method method)
{
    const bool size_query = method.selector == 17;
    const bool two_profiles = size_query || method.selector == 13;
    const uint32_t source = call->words[0], destination = two_profiles ? call->words[1] : 0;
    const uint32_t callback = size_query ? 0 : call->words[two_profiles ? 3 : 2];
    const uint32_t size_output = size_query ? call->words[2] : 0;
    const uint32_t missing_output = call->words[size_query ? 3 : (two_profiles ? 5 : 4)];
    _Alignas(4) uint32_t size = 0;
    unsigned char missing = 0;
    uint32_t status = paramErr;

    if ((size_output && !color_profile_memory(size_output, 4, 1)) ||
        (missing_output && !color_profile_memory(missing_output, 1, 1)))
        return color_profile_result(call, paramErr);

    if (source && (!two_profiles || destination) && (size_query || callback)) {
        _Alignas(16) uint32_t choices[4] = {0};
        _Alignas(4) uint32_t position = 0, instance = 0;
        const color_world_chain chain = {.key_profile = source};
        unsigned count;
        if (!color_world_chain_candidates(ref, &chain, choices, &count))
            return 0;

        const uint32_t open[] = {US_TO_SYN68K(choices),   count,
                                 US_TO_SYN68K(&position), method.selector,
                                 US_TO_SYN68K(&instance), US_TO_SYN68K(&missing)};
        if (!cfp_private(ref, 0x102d8, open, NELEM(open), &status))
            return 0;
        const uint32_t component = CL(instance);
        const uint32_t arguments[] = {component,      source,
                                      call->words[1], size_query ? US_TO_SYN68K(&size) : call->words[2],
                                      call->words[3], call->words[4]};
        const unsigned words = size_query ? 3 : (two_profiles ? 5 : 4);
        int ok = status || cfp_private(ref, method.component_call, arguments, words + 1, &status);
        if (ok && status) {
            missing = 0;
            ok = cfp_private(ref, method.generator, arguments + 1, words, &status);
        }
        if (component && !cfp_private(ref, 0x326ec, &component, 1, NULL))
            ok = 0;
        if (!ok)
            return 0;
    }

    if (missing_output) {
        unsigned char* result = color_profile_memory(missing_output, 1, 1);
        if (!result)
            return 0;
        *result = missing;
    }
    if (size_output && !cfp_write(size_output, CL(size)))
        return 0;
    return color_profile_result(call, status);
}

static int color_world_chain_run(const ROMlib_carbon_reference* ref, const color_world_chain* chain,
                                 ROMlib_carbon_ppc_frame* call, bool extended, bool link, uint32_t* encoded_profile,
                                 uint32_t* component, uint32_t* status)
{
    _Alignas(16) uint32_t choices[4] = {0};
    _Alignas(4) uint32_t position = 0, instance = 0;
    unsigned char missing = 0;
    unsigned count;
    if (!color_world_chain_candidates(ref, chain, choices, &count))
        return 0;
    const unsigned selector = link ? (extended ? 19 : 16) : (extended ? 18 : 7);
    const uint32_t open[] = {US_TO_SYN68K(choices), count, US_TO_SYN68K(&position), selector, US_TO_SYN68K(&instance),
                             US_TO_SYN68K(&missing)};
    uint32_t last_error = 0;
    do {
        if (!cfp_private(ref, 0x102d8, open, NELEM(open), status))
            return 0;
        *component = CL(instance);
        if (*status) {
            if (last_error)
                *status = last_error;
            return 1;
        }

        const uint32_t context = call->words[link ? 4 : 3];
        const uint32_t callback = call->words[link ? 3 : 2];
        const uint32_t concatenate[] = {*component, chain->set, callback, context};
        const uint32_t make_link[] = {*component, extended ? CL(*encoded_profile) : US_TO_SYN68K(encoded_profile),
                                      extended ? chain->set : call->words[1], extended ? callback : chain->set,
                                      context};
        const uint32_t worker = link ? (extended ? 0x32d24 : 0x32d00) : (extended ? 0x32cdc : 0x32cb8);
        if (!cfp_private(ref, worker, link ? make_link : concatenate, extended ? (link ? 5 : 4) : (link ? 4 : 2),
                         status))
            return 0;
        if (!*status)
            return 1;

        last_error = *status;
        if (*component && !cfp_private(ref, 0x326ec, component, 1, NULL))
            return 0;
        *component = 0;
    } while (CL(position) < count);
    return 1;
}

static int color_world_store_chain(const ROMlib_carbon_reference* ref, const color_world_chain* chain, uint32_t world,
                                   uint32_t component)
{
    _Alignas(4) uint32_t input_space = 0, output_space = 0;
    uint32_t version;
    if (!cfp_private(ref, 0x3267c, &component, 1, &version))
        return 0;
    const uint32_t spaces[] = {chain->first_profile, chain->first_transform, US_TO_SYN68K(&input_space),
                               chain->last_profile,  chain->last_transform,  US_TO_SYN68K(&output_space)};
    if (!cfp_private(ref, 0x111c0, spaces, NELEM(spaces), NULL))
        return 0;
    unsigned char* record = color_world_data(world, 1);
    if (!record)
        return 0;
    color_write(record, 4, 1);
    color_write(record + 4, 4, component);
    color_write(record + 12, 4, version);
    color_write(record + 28, 4, CL(input_space));
    color_write(record + 32, 4, CL(output_space));
    return 1;
}

static int color_world_construct(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call, bool extended,
                                 bool link)
{
    const uint32_t output = call->words[0], target = link ? call->words[1] : 0;
    const uint32_t set = call->words[link ? 2 : 1];
    if (!color_profile_memory(output, 4, 1))
        return color_profile_result(call, paramErr);
    if (!cfp_write(output, 0))
        return 0;
    if (link && ((!extended && !target) || (target && !color_profile_memory(target, 2, 0))))
        return color_profile_result(call, paramErr);

    color_world_chain chain;
    const int error = color_world_read_chain(set, extended, link, &chain);
    if (error)
        return color_profile_result(call, error);

    _Alignas(4) uint32_t result = 0;
    const uint32_t result_address = US_TO_SYN68K(&result);
    uint32_t status = noErr, component = 0;
    int ok = 1;
    if (link && extended) {
        const uint32_t create[] = {result_address, target};
        ok = cfp_private(ref, 0x34a58, create, NELEM(create), &status);
    }
    else if (!link) {
        const uint32_t allocate[] = {result_address, color_world_size};
        ok = cfp_private(ref, 0x2da2c, allocate, NELEM(allocate), &status);
    }
    if (ok && !status)
        ok = color_world_chain_run(ref, &chain, call, extended, link, &result, &component, &status);
    if (ok && !status && link && target) {
        const unsigned char* location = color_profile_memory(target, 2, 0);
        const uint32_t profile = CL(result);
        if (!location)
            ok = 0;
        else if (color_read(location, 2))
            ok = cfp_private(ref, 0x34a40, &profile, 1, &status);
    }
    if (ok && !status && !link)
        ok = color_world_store_chain(ref, &chain, CL(result), component);

    if (component && (link || !ok || status) && !cfp_private(ref, 0x326ec, &component, 1, NULL))
        ok = 0;
    if (ok && !status)
        return cfp_write(output, CL(result)) && color_profile_result(call, noErr);
    if (result) {
        const uint32_t discard = link ? CL(result) : result_address;
        if (!cfp_private(ref, link ? 0x34a70 : 0x2deb0, &discard, 1, NULL))
            return 0;
    }
    return ok && color_profile_result(call, status);
}

typedef struct {
    uint32_t component[2];
    uint32_t version[2];
} color_world_components;

static int color_world_try_profiles(const ROMlib_carbon_reference* ref, const uint32_t profiles[2],
                                    color_world_components* result, uint32_t* status)
{
    const uint32_t apple = TICK("appl");
    _Alignas(4) uint32_t signatures[2] = {0};
    const uint32_t preference[] = {US_TO_SYN68K(&signatures[0]), 0};
    if (!cfp_private(ref, 0x3278c, &apple, 1, NULL) || !cfp_private(ref, 0x34d40, preference, NELEM(preference), NULL))
        return 0;
    signatures[1] = signatures[0];

    uint32_t installed[2];
    for (unsigned i = 0; i < 2; ++i) {
        const uint32_t embedded[] = {profiles[i], US_TO_SYN68K(&signatures[i])};
        if (!signatures[i] && !cfp_private(ref, 0x158b0, embedded, NELEM(embedded), NULL))
            return 0;
        signatures[i] = CL(signatures[i]);
        if (!cfp_private(ref, 0x3278c, &signatures[i], 1, &installed[i]))
            return 0;
    }

    // Try a complete transform first, then pairs joined through the profile connection space.
    const uint32_t source = signatures[0], destination = signatures[1];
    const bool source_cmm = source != apple && installed[0];
    const bool destination_cmm = destination != apple && installed[1];
    const bool distinct = source != destination;

    const struct {
        bool enabled;
        uint32_t first;
        uint32_t second;
    } candidates[] = {
        {destination_cmm, destination, 0},
        {source_cmm, source, 0},
        {distinct && installed[0] && installed[1], source, destination},
        {distinct && source_cmm, source, apple},
        {distinct && destination_cmm, apple, destination},
        {true, apple, 0},
    };

    for (unsigned i = 0; i < NELEM(candidates); ++i) {
        if (!candidates[i].enabled)
            continue;
        const uint32_t first = candidates[i].first, second = candidates[i].second;
        const uint32_t single[] = {US_TO_SYN68K(&result->component[0]), US_TO_SYN68K(&result->version[0]), first,
                                   profiles[0], profiles[1]};
        const uint32_t paired[] = {US_TO_SYN68K(&result->component[0]),
                                   US_TO_SYN68K(&result->component[1]),
                                   US_TO_SYN68K(&result->version[0]),
                                   US_TO_SYN68K(&result->version[1]),
                                   first,
                                   second,
                                   profiles[0],
                                   profiles[1]};
        if (!cfp_private(ref, second ? 0x13ad0 : 0x139e8, second ? paired : single,
                         second ? NELEM(paired) : NELEM(single), status))
            return 0;
        if (!*status)
            return 1;
    }
    return 1;
}

static int color_world_new_from_profiles(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t output = call->words[0];
    uint32_t profiles[] = {call->words[1], call->words[2]};
    if (!color_profile_memory(output, 4, 1))
        return color_profile_result(call, paramErr);
    if (!cfp_write(output, 0))
        return 0;
    if (!profiles[0] && !profiles[1])
        return color_profile_result(call, paramErr);
    for (unsigned i = 0; i < 2; ++i) {
        uint32_t valid;
        if (profiles[i]) {
            if (!cfp_private(ref, 0x112ec, &profiles[i], 1, &valid))
                return 0;
            if (!valid)
                return color_profile_result(call, paramErr);
        }
    }

    _Alignas(4) uint32_t handle = 0;
    const uint32_t handle_address = US_TO_SYN68K(&handle);
    const uint32_t allocate[] = {handle_address, color_world_size};
    uint32_t status = noErr;
    int ok = cfp_private(ref, 0x2da2c, allocate, NELEM(allocate), &status);
    bool close_profile[2] = {false, false};
    for (unsigned i = 0; ok && !status && i < 2; ++i) {
        if (!profiles[i]) {
            _Alignas(4) uint32_t system_profile = 0;
            const uint32_t address = US_TO_SYN68K(&system_profile);
            ok = cfp_private(ref, 0x34a28, &address, 1, &status);
            profiles[i] = CL(system_profile);
            close_profile[i] = profiles[i] != 0;
        }
    }

    _Alignas(4) color_world_components components = {0};
    if (ok && !status)
        ok = color_world_try_profiles(ref, profiles, &components, &status);
    if (ok && !status) {
        _Alignas(4) uint32_t classes[2] = {0}, spaces[2] = {0};
        const uint32_t source[] = {profiles[0], US_TO_SYN68K(&classes[0])};
        const uint32_t destination[] = {profiles[1], US_TO_SYN68K(&classes[1])};
        const uint32_t transform[] = {profiles[0], 1, US_TO_SYN68K(&spaces[0]),
                                      profiles[1], 2, US_TO_SYN68K(&spaces[1])};
        ok = cfp_private(ref, 0x15a24, source, NELEM(source), NULL) &&
             cfp_private(ref, 0x15a24, destination, NELEM(destination), NULL) &&
             cfp_private(ref, 0x111c0, transform, NELEM(transform), NULL);
        unsigned char* record = ok ? color_world_data(CL(handle), 1) : NULL;
        if (record) {
            color_write(record, 4, components.component[1] ? 2 : 1);
            for (unsigned i = 0; i < 2; ++i) {
                color_write(record + 4 + i * 4, 4, CL(components.component[i]));
                color_write(record + 12 + i * 4, 4, CL(components.version[i]));
                color_write(record + 20 + i * 4, 4, CL(classes[i]));
                color_write(record + 28 + i * 4, 4, CL(spaces[i]));
            }
        }
        else
            ok = 0;
    }

    for (unsigned i = 0; i < 2; ++i) {
        if (close_profile[i] && !cfp_private(ref, 0x34a70, &profiles[i], 1, NULL))
            ok = 0;
    }
    if (ok && !status && cfp_write(output, CL(handle)))
        return color_profile_result(call, noErr);

    for (unsigned i = 0; i < 2; ++i) {
        const uint32_t component = CL(components.component[i]);
        if (component && !cfp_private(ref, 0x326ec, &component, 1, NULL))
            ok = 0;
    }
    if (handle && !cfp_private(ref, 0x2deb0, &handle_address, 1, NULL))
        ok = 0;
    return ok && status && color_profile_result(call, status);
}

static int color_world_dispatch(ROMlib_carbon_ppc_frame* call, enum color_world_operation operation, const char* name,
                                uint32_t offset)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_load_reference("ColorSync", name, &ref))
        return 0;
    if (ref.code_offset != offset || ref.code_size != 0x366ec || ref.toc_offset != 0x464 || ref.toc_size != 0x1670)
        return ROMlib_carbon_invoke_frame(ref.function, call);

    switch (operation) {
    case color_world_new:
        return color_world_new_from_profiles(&ref, call);
    case color_world_dispose:
        return color_world_release(&ref, call);
    case color_world_match_colors:
    case color_world_check_colors:
        return color_world_colors(&ref, call, operation == color_world_check_colors);
    case color_world_match_bitmap:
    case color_world_check_bitmap:
        return color_world_bitmap(&ref, call, operation == color_world_check_bitmap);
    case color_world_match_pixmap:
    case color_world_check_pixmap:
        return color_world_pixmap(&ref, call, operation == color_world_check_pixmap);
    case color_world_concat_legacy:
    case color_world_concat_extended:
        return color_world_construct(&ref, call, operation == color_world_concat_extended, false);
    case color_world_link_legacy:
    case color_world_link_extended:
        return color_world_construct(&ref, call, operation == color_world_link_extended, true);
    }
    return 0;
}

#define CARBON_COLOR_WORLD(name, operation, offset)                                \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call)                        \
    {                                                                              \
        return color_world_dispatch(call, color_world_##operation, #name, offset); \
    }

CARBON_COLOR_WORLD(NCWNewColorWorld, new, 0x13e98)
CARBON_COLOR_WORLD(CWDisposeColorWorld, dispose, 0x1477c)
CARBON_COLOR_WORLD(CWMatchColors, match_colors, 0x148f0)
CARBON_COLOR_WORLD(CWCheckColors, check_colors, 0x149bc)
CARBON_COLOR_WORLD(CWMatchBitmap, match_bitmap, 0x124f4)
CARBON_COLOR_WORLD(CWCheckBitmap, check_bitmap, 0x128bc)
CARBON_COLOR_WORLD(CWMatchPixMap, match_pixmap, 0x127a8)
CARBON_COLOR_WORLD(CWCheckPixMap, check_pixmap, 0x12a34)
CARBON_COLOR_WORLD(CWConcatColorWorld, concat_legacy, 0x14344)
CARBON_COLOR_WORLD(NCWConcatColorWorld, concat_extended, 0x14aa0)
CARBON_COLOR_WORLD(CWNewLinkProfile, link_legacy, 0x145ac)
CARBON_COLOR_WORLD(NCWNewLinkProfile, link_extended, 0x14d00)

#undef CARBON_COLOR_WORLD

#define CARBON_COLOR_POSTSCRIPT(name, offset, selector, component_call, generator)                                 \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call)                                                        \
    {                                                                                                              \
        ROMlib_carbon_reference ref;                                                                               \
        if (!ROMlib_carbon_load_reference("ColorSync", #name, &ref))                                               \
            return 0;                                                                                              \
        if (ref.code_offset != offset || ref.code_size != 0x366ec || ref.toc_offset != 0x464 ||                    \
            ref.toc_size != 0x1670)                                                                                \
            return ROMlib_carbon_invoke_frame(ref.function, call);                                                 \
        return color_world_postscript(&ref, call, (color_postscript_method){selector, component_call, generator}); \
    }

CARBON_COLOR_POSTSCRIPT(CMGetPS2ColorSpace, 0x10cc4, 11, 0x32d48, 0x262b0)
CARBON_COLOR_POSTSCRIPT(CMGetPS2ColorRenderingIntent, 0x10dd8, 12, 0x32d6c, 0x1cef4)
CARBON_COLOR_POSTSCRIPT(CMGetPS2ColorRendering, 0x10eec, 13, 0x32d90, 0x2cab4)
CARBON_COLOR_POSTSCRIPT(CMGetPS2ColorRenderingVMSize, 0x11014, 17, 0x32db4, 0x2cff0)

#undef CARBON_COLOR_POSTSCRIPT
#endif
