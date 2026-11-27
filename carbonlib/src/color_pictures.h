/*+===================================================================
  File:      color_pictures.h

  Summary:   Hosts ColorSync picture matching setup, comments and cleanup.

  Classes:   None.

  Functions: Private Carbon entry points for ColorSync QuickDraw operations.

  Origin:    Mac OS 9.2.1 ColorSync 3.0.4 and Universal Interfaces 3.4.2.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_COLOR_PICTURES_H
#define ROMAW_CARBON_COLOR_PICTURES_H

#include "cf_batch.h"
#include "color_profiles.h"

enum {
    color_picture_header_size = 0x10,
    color_picture_device_size = 0x64,
    color_picture_end_comment = 221,
    color_picture_enable_comment = 222,
    color_picture_disable_comment = 223,
    color_picture_bad_space = -4209,
    color_picture_bad_destination = -181,
    color_picture_no_match = -177,
    color_picture_old_profile = -4204,
};

static unsigned char* color_picture_match_record(uint32_t match, unsigned* count)
{
    unsigned char* record = color_profile_memory(match, color_picture_header_size, 1);
    if (!record || color_read(record, 4) != TICK("MLst"))
        return NULL;
    const int16_t devices = color_read(record + 12, 2);
    *count = devices > 0 ? (unsigned)devices : 0;
    return color_profile_memory(match, color_picture_header_size + *count * color_picture_device_size, 1);
}

static int color_picture_release_devices(const ROMlib_carbon_reference* ref, uint32_t match, unsigned count,
                                         bool remove_search)
{
    int ok = 1;
    for (unsigned i = 0; i < count; ++i) {
        const uint32_t device = match + color_picture_header_size + i * color_picture_device_size;
        const uint32_t args[] = {device + (remove_search ? 0 : 16), 0};
        if (!cfp_private(ref, remove_search ? 0x450 : 0x173d0, args, remove_search ? 2 : 1, NULL))
            ok = 0;
    }
    return ok;
}

static int color_picture_dispose(const ROMlib_carbon_reference* ref, uint32_t match)
{
    unsigned count;
    unsigned char* record = color_picture_match_record(match, &count);
    if (!record)
        return 1;
    const uint32_t shared_state = color_read(record + 8, 4);
    color_write(record, 4, TICK("xxxx"));
    int ok = color_picture_release_devices(ref, match, count, false);
    const uint32_t state_address = match + 8;
    if (shared_state && !cfp_private(ref, 0x17450, &state_address, 1, NULL))
        ok = 0;
    _Alignas(4) uint32_t encoded = CL(match);
    const uint32_t address = US_TO_SYN68K(&encoded);
    return cfp_private(ref, 0x2de5c, &address, 1, NULL) && ok;
}

static int color_picture_profile_space(const ROMlib_carbon_reference* ref, uint32_t profile, uint32_t* result)
{
    _Alignas(4) uint32_t space = CL(TICK("RGB "));
    const uint32_t args[] = {profile, US_TO_SYN68K(&space)};
    if (profile && !cfp_private(ref, 0x159a8, args, NELEM(args), NULL))
        return 0;
    *result = CL(space);
    return 1;
}

static int color_picture_use_profile(const ROMlib_carbon_reference* ref, uint32_t profile, uint32_t flags,
                                     uint32_t* status)
{
    *status = paramErr;
    if (!profile)
        return 1;

    _Alignas(128) unsigned char header[128];
    const uint32_t header_address = US_TO_SYN68K(header);
    const uint32_t profile_header[] = {profile, header_address};
    if (!cfp_private(ref, 0x34ab8, profile_header, NELEM(profile_header), status))
        return 0;
    if (*status)
        return 1;
    if (color_read(header + 8, 4) < 0x02000000) {
        *status = color_picture_old_profile;
        return 1;
    }
    if (flags == 1)
        return cfp_private(ref, 0x944, &profile, 1, status);

    const uint32_t original_flags = color_read(header + 44, 4);
    color_write(header + 44, 4, original_flags | 1);
    if (!cfp_private(ref, 0x34ad0, profile_header, NELEM(profile_header), NULL))
        return 0;

    const uint32_t callback[] = {cfp_toc(ref, (unsigned)-0xe0), 0x3fe0, 1};
    uint32_t upp = 0;
    int ok = cfp_private(ref, 0x350e8, callback, NELEM(callback), &upp);
    if (ok) {
        _Alignas(16) unsigned char context[16] = {0};
        const uint32_t flatten[] = {profile, flags, upp, US_TO_SYN68K(context), 0};
        ok = cfp_private(ref, 0x34aa0, flatten, NELEM(flatten), status);
    }
    if (upp && !cfp_private(ref, 0x35100, &upp, 1, NULL))
        ok = 0;
    color_write(header + 44, 4, original_flags);
    return cfp_private(ref, 0x34ad0, profile_header, NELEM(profile_header), NULL) && ok;
}

static int color_picture_restore_colors(const ROMlib_carbon_reference* ref, uint32_t port)
{
    // Search procedures affect indexed colors, so reset both RGB color selections.
    const unsigned char* saved = port ? color_profile_memory(port + 0x24, 12, 0) : NULL;
    if (!saved)
        return 1;
    _Alignas(16) unsigned char rgb[12];
    memcpy(rgb, saved, sizeof rgb);
    const uint32_t foreground = US_TO_SYN68K(rgb), background = US_TO_SYN68K(rgb + 6);
    const int first = cfp_private(ref, 0x352b0, &foreground, 1, NULL);
    return cfp_private(ref, 0x352c8, &background, 1, NULL) && first;
}

static int color_picture_begin(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    uint32_t source = call->words[0];
    const uint32_t destination = call->words[1], output = call->words[2];
    if (!color_profile_memory(output, 4, 1))
        return color_profile_result(call, paramErr);
    if (!cfp_write(output, 0))
        return 0;
    _Alignas(4) uint32_t encoded_port = 0;
    const uint32_t port_address = US_TO_SYN68K(&encoded_port);
    if (!cfp_private(ref, 0x35268, &port_address, 1, NULL))
        return 0;
    const uint32_t port = CL(encoded_port);
    uint32_t space;
    if (!color_picture_profile_space(ref, source, &space))
        return 0;
    if (space != TICK("RGB "))
        return color_profile_result(call, color_picture_bad_space);
    if (!color_picture_profile_space(ref, destination, &space))
        return 0;
    if (space != TICK("RGB "))
        return color_profile_result(call, color_picture_bad_space);

    _Alignas(4) uint32_t encoded_status = 0, default_profile = 0;
    const uint32_t create[] = {US_TO_SYN68K(&encoded_status), 0, 0};
    uint32_t match;
    if (!cfp_private(ref, 0x200, create, NELEM(create), &match))
        return 0;
    uint32_t status = CL(encoded_status);
    if (!match)
        return color_profile_result(call, status);

    int ok = 1;
    if (!status && !source) {
        const uint32_t address = US_TO_SYN68K(&default_profile);
        ok = cfp_private(ref, 0x34a28, &address, 1, &status);
        source = CL(default_profile);
    }
    // Configuration may replace display profiles while setting up search callbacks.
    if (ok && !status && destination) {
        ok = color_picture_profile_space(ref, destination, &space);
        if (ok && space != TICK("RGB ") && space != TICK("GRAY"))
            status = color_picture_bad_destination;
    }

    unsigned count = 0;
    const unsigned char* record = color_picture_match_record(match, &count);
    if (!record)
        ok = 0;
    const uint32_t legacy = record ? record[4] : 0;
    const bool configured = ok && !status;
    for (unsigned i = 0; ok && !status && i < count; ++i) {
        const uint32_t setup[] = {match + color_picture_header_size + i * color_picture_device_size, source,
                                  destination, legacy, 0};
        ok = cfp_private(ref, 0x5fc, setup, NELEM(setup), &status);
        if (status == (uint32_t)color_picture_no_match)
            status = noErr;
    }
    if (ok && !status)
        ok = color_picture_use_profile(ref, source, 0, &status);
    if (default_profile) {
        const uint32_t profile = CL(default_profile);
        if (!cfp_private(ref, 0x34a70, &profile, 1, NULL))
            ok = 0;
    }

    if (status || !ok) {
        if (configured && !color_picture_release_devices(ref, match, count, true))
            ok = 0;
        if (!color_picture_dispose(ref, match))
            ok = 0;
        match = 0;
    }

    if (!color_picture_restore_colors(ref, port))
        ok = 0;
    if (ok && cfp_write(output, match))
        return color_profile_result(call, status);
    if (match) {
        color_picture_release_devices(ref, match, count, true);
        color_picture_dispose(ref, match);
    }
    return 0;
}

static int color_picture_end(const ROMlib_carbon_reference* ref, uint32_t match)
{
    unsigned count;
    if (!color_picture_match_record(match, &count))
        return 1;
    const uint32_t comment[] = {color_picture_end_comment, 0, 0};
    if (!cfp_private(ref, 0x35070, comment, NELEM(comment), NULL))
        return 0;
    const int removed = color_picture_release_devices(ref, match, count, true);
    return color_picture_dispose(ref, match) && removed;
}

typedef struct {
    unsigned slot;
    unsigned context;
    unsigned toc_offset;
    unsigned kind;
    bool needed;
    bool required;
    uint32_t saved;
    bool opened;
} color_picture_hook;

static int color_picture_finish_draw(const ROMlib_carbon_reference* ref, unsigned char* state, uint32_t match,
                                     unsigned count)
{
    const uint32_t address = US_TO_SYN68K(state);
    int ok = 1;
    if (state[13]) {
        if (state[14]) {
            const uint32_t cache[] = {address + 0x5c, color_read(state + 16, 4)};
            ok = cfp_private(ref, 0x1660, cache, NELEM(cache), NULL);
        }
    }
    else if (state[12]) {
        const uint32_t close_file = (uint32_t)(int16_t)color_read(state + 0x5a, 2);
        const uint32_t remove_file = address + 20;
        const int closed = cfp_private(ref, 0x30878, &close_file, 1, NULL);
        ok = cfp_private(ref, 0x312c4, &remove_file, 1, NULL) && closed;
    }
    if (state[9] && !color_picture_release_devices(ref, match, count, true))
        ok = 0;
    return ok;
}

static int color_picture_draw_on_port(const ROMlib_carbon_reference* ref, uint32_t port, uint32_t match, uint32_t cache,
                                      bool all_displays, const uint32_t draw[2], uint32_t destination, uint32_t* status)
{
    unsigned count;
    const unsigned char* record = color_picture_match_record(match, &count);
    if (!record)
        return 0;
    const uint32_t shared = color_read(record + 8, 4);
    const uint32_t procs_address = port + 0x68;
    uint32_t original_procs, picture_save;
    if (!cfp_get(procs_address, &original_procs) || !cfp_get(port + 0x5c, &picture_save))
        return 0;

    _Alignas(4) uint32_t temporary = 0;
    const uint32_t allocate[] = {US_TO_SYN68K(&temporary), 0x50};
    int ok = 1;
    if (!original_procs) {
        ok = cfp_private(ref, 0x2d6e0, allocate, NELEM(allocate), status);
        if (ok && !*status) {
            const uint32_t table = CL(temporary);
            ok = cfp_private(ref, 0x35298, &table, 1, NULL) && cfp_write(procs_address, table);
        }
    }

    const uint32_t table = original_procs ? original_procs : CL(temporary);
    _Alignas(512) unsigned char state[0x1c4] = {0};
    const uint32_t address = US_TO_SYN68K(state);
    color_write(state + 0x5c, 4, cache);
    const uint32_t toc = US_TO_SYN68K(ref->toc);
    color_picture_hook hooks[] = {
        {0x24, 0x74, 0x7c, 1, true, true, 0, false},
        {0x20, 0x170, 0x64, 3, !picture_save, false, 0, false},
        {0x2c, 0xc8, 0x6c, 2, all_displays, all_displays, 0, false},
        {0x30, 0x11c, 0x74, 2, all_displays, all_displays, 0, false},
    };

    bool prepared = ok && !*status;
    for (unsigned i = 0; ok && !*status && i < NELEM(hooks); ++i) {
        color_picture_hook* hook = &hooks[i];
        if (!hook->needed)
            continue;
        const uint32_t slot = table + hook->slot;
        const uint32_t create[] = {address + hook->context, shared, hook->kind, toc + hook->toc_offset, address};
        uint32_t procedure;
        ok = cfp_get(slot, &hook->saved);
        if (ok) {
            hook->opened = true;
            ok = cfp_private(ref, 0x171a8, create, NELEM(create), &procedure) && cfp_write(slot, procedure);
            if (ok && !procedure && hook->required)
                prepared = false;
        }
    }

    if (ok && prepared) {
        color_write(state, 4, match);
        color_write(state + 4, 4, destination);
        state[8] = 1;
        ok = cfp_private(ref, 0xfe4, &address, 1, status);
        state[9] = ok && !*status;
        if (state[9])
            ok = cfp_private(ref, 0x35280, draw, 2, NULL);
        if (ok)
            *status = color_read(state + 0x60, 4);
        if (!color_picture_finish_draw(ref, state, match, count))
            ok = 0;
    }

    for (unsigned i = 0; i < NELEM(hooks); ++i) {
        const color_picture_hook* hook = &hooks[i];
        if (!hook->opened)
            continue;
        const uint32_t context = address + hook->context;
        if (!cfp_write(table + hook->slot, hook->saved))
            ok = 0;
        if (!cfp_private(ref, 0x173d0, &context, 1, NULL))
            ok = 0;
    }
    if (temporary) {
        if (!cfp_write(procs_address, original_procs))
            ok = 0;
        if (!cfp_private(ref, 0x2de5c, allocate, 1, NULL))
            ok = 0;
    }
    return color_picture_restore_colors(ref, port) && ok;
}

static int color_picture_draw_matched(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t draw[] = {call->words[0], call->words[2]}, destination = call->words[1];
    uint32_t space;
    if (!color_picture_profile_space(ref, destination, &space))
        return 0;
    const uint32_t initial = space == TICK("RGB ") ? noErr : color_picture_bad_space;
    if (!cfp_private(ref, 0x35250, &initial, 1, NULL))
        return 0;
    if (initial)
        return 1;

    _Alignas(4) uint32_t encoded_port = 0, cache = 0, encoded_error = 0;
    const uint32_t port_address = US_TO_SYN68K(&encoded_port);
    if (!cfp_private(ref, 0x35268, &port_address, 1, NULL))
        return 0;
    const uint32_t port = CL(encoded_port);
    const unsigned char* record = color_profile_memory(port, 8, 0);
    if (!record)
        return 0;
    if ((int16_t)color_read(record + 6, 2) > 0)
        return cfp_private(ref, 0x35280, draw, NELEM(draw), NULL);

    const uint32_t allocate[] = {US_TO_SYN68K(&cache), 8};
    uint32_t status;
    if (!cfp_private(ref, 0x2da2c, allocate, NELEM(allocate), &status))
        return 0;
    if (!cache)
        return 1;
    unsigned char all_displays = 0;
    const uint32_t create[] = {US_TO_SYN68K(&encoded_error), 0, US_TO_SYN68K(&all_displays)};
    uint32_t match = 0;
    int ok = cfp_private(ref, 0x200, create, NELEM(create), &match);
    status = CL(encoded_error);
    if (ok && match)
        ok = color_picture_draw_on_port(ref, port, match, CL(cache), all_displays != 0, draw, destination, &status);
    if (!cfp_private(ref, 0x15e0, allocate, 1, NULL))
        ok = 0;
    if (match && !color_picture_dispose(ref, match))
        ok = 0;
    if (!ok || !match)
        return ok;

    uint32_t qd_error;
    if (!cfp_private(ref, 0x352e0, NULL, 0, &qd_error))
        return 0;
    const uint32_t error = (uint32_t)(int16_t)status;
    return qd_error || cfp_private(ref, 0x35250, &error, 1, NULL);
}

#define CARBON_COLOR_PICTURE_REFERENCE(name, offset)                                                                \
    ROMlib_carbon_reference ref;                                                                                    \
    if (!ROMlib_carbon_load_reference("ColorSync", name, &ref))                                                     \
        return 0;                                                                                                   \
    if (ref.code_offset != offset || ref.code_size != 0x366ec || ref.toc_offset != 0x464 || ref.toc_size != 0x1670) \
        return ROMlib_carbon_invoke_frame(ref.function, call);

static int carbon_NCMBeginMatching(ROMlib_carbon_ppc_frame* call)
{
    CARBON_COLOR_PICTURE_REFERENCE("NCMBeginMatching", 0x28ec)
    return color_picture_begin(&ref, call);
}

static int carbon_CMEndMatching(ROMlib_carbon_ppc_frame* call)
{
    CARBON_COLOR_PICTURE_REFERENCE("CMEndMatching", 0x2904)
    return color_picture_end(&ref, call->words[0]);
}

static int carbon_NCMUseProfileComment(ROMlib_carbon_ppc_frame* call)
{
    CARBON_COLOR_PICTURE_REFERENCE("NCMUseProfileComment", 0xa54)
    uint32_t status;
    return color_picture_use_profile(&ref, call->words[0], call->words[1], &status) &&
           color_profile_result(call, status);
}

static int carbon_NCMDrawMatchedPicture(ROMlib_carbon_ppc_frame* call)
{
    CARBON_COLOR_PICTURE_REFERENCE("NCMDrawMatchedPicture", 0x2a48)
    return color_picture_draw_matched(&ref, call);
}

#undef CARBON_COLOR_PICTURE_REFERENCE

static int carbon_CMEnableMatchingComment(ROMlib_carbon_ppc_frame* call)
{
    PicComment((uint8_t)call->words[0] ? color_picture_enable_comment : color_picture_disable_comment, 0, NULL);
    return 1;
}

#endif
