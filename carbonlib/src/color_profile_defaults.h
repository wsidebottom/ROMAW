/*+===================================================================
  File:      color_profile_defaults.h

  Summary:   Implements ColorSync default profile selection and preference updates.

  Classes:   None.

  Functions: Private PowerPC frame adapters for default and system profiles.

  Origin:    Mac OS 9.2.1 ColorSync 3.0.4 and Universal Interfaces 3.4.2.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_COLOR_PROFILE_DEFAULTS_H
#define ROMAW_CARBON_COLOR_PROFILE_DEFAULTS_H

#include "cf_batch.h"
#include "color_profiles.h"

enum color_default_operation {
    color_default_by_space,
    color_default_by_use,
    color_default_for_display,
    color_default_system,
    color_default_system_private,
    color_default_set_space,
    color_default_set_use,
    color_default_set_system,
    color_default_set_location,
    color_default_get_system_avid,
    color_default_set_system_avid,
    color_default_set_display,
    color_default_get_gamma,
    color_default_set_gamma,
    color_default_new_display,
    color_default_launch_panel,
};

enum { color_default_location_size = 258, color_default_display_use = 0x64706c79 };

static int color_default_open_file(const ROMlib_carbon_reference* ref, const FSSpec* file, uint32_t output,
                                   uint32_t* status)
{
    _Alignas(4) unsigned char location[color_default_location_size] = {0};
    color_write(location, 2, 1);
    memcpy(location + 2, file, sizeof *file);
    const uint32_t args[] = {output, US_TO_SYN68K(location)};
    return cfp_private(ref, 0x34a10, args, NELEM(args), status);
}

static int color_default_get(const ROMlib_carbon_reference* ref, uint32_t kind, uint32_t key, uint32_t output,
                             uint32_t* status)
{
    *status = paramErr;
    if (!output)
        return 1;
    if (!cfp_write(output, 0))
        return 0;
    _Alignas(4) unsigned char setting[56];
    _Alignas(4) FSSpec file;
    const uint32_t setting_address = US_TO_SYN68K(setting), file_address = US_TO_SYN68K(&file);
    const uint32_t query[] = {kind, key, setting_address};
    if (!cfp_private(ref, 0x17a48, query, NELEM(query), status))
        return 0;
    if (*status)
        return 1;

    const uint32_t lookup[] = {(int16_t)color_read(setting + 6, 2), file_address};
    if (!cfp_private(ref, 0x1ccf4, lookup, NELEM(lookup), status))
        return 0;
    if (*status) {
        const uint32_t alternate[] = {kind, key, file_address};
        if (!cfp_private(ref, 0x17c24, alternate, NELEM(alternate), status))
            return 0;
        if (*status)
            return 1;
        if (!key && !cfp_private(ref, 0x17d98, &file_address, 1, NULL))
            return 0;
    }
    return color_default_open_file(ref, &file, output, status);
}

static int color_display_capabilities(const ROMlib_carbon_reference* ref, uint32_t* flags)
{
    const uint32_t address = cfp_toc(ref, (unsigned)-0x2c);
    if (!address || !cfp_get(address, flags))
        return 0;
    if (*flags)
        return 1;
    const uint32_t gestalt[] = {color_default_display_use, address};
    uint32_t status;
    if (!cfp_private(ref, 0x35190, gestalt, NELEM(gestalt), &status))
        return 0;
    return (!status || cfp_write(address, 0)) && cfp_get(address, flags);
}

static int color_system_avid(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call, bool set)
{
    const uint32_t argument = call->words[0];
    if (!set && !color_profile_memory(argument, 4, 1))
        return color_profile_result(call, paramErr);
    uint32_t flags, status;
    if (!color_display_capabilities(ref, &flags))
        return 0;
    // Some Display Manager versions own per-AVID profiles but have no system selector.
    if ((flags & 0x20) && !(flags & 0x40))
        status = -4;
    else {
        const uint32_t preference[] = {TICK("spID"), (uint32_t)-16000, argument};
        if (!cfp_private(ref, set ? 0x1ccc4 : 0x1cc48, preference, NELEM(preference), &status))
            return 0;
        if (!set && status) {
            const uint32_t active = 1;
            uint32_t device;
            if (!cfp_private(ref, 0x351d8, &active, 1, &device))
                return 0;
            const uint32_t get_id[] = {device, argument, 1};
            if (!cfp_private(ref, 0x351f0, get_id, NELEM(get_id), &status))
                return 0;
        }
    }
    return (set || !status || cfp_write(argument, 0)) && color_profile_result(call, status);
}

static int color_default_get_display(const ROMlib_carbon_reference* ref, uint32_t display, uint32_t output,
                                     uint32_t* status)
{
    if (display && output) {
        uint32_t flags;
        if (!color_display_capabilities(ref, &flags))
            return 0;
        if (flags & (1u << 5)) {
            const uint32_t lookup[] = {display, output};
            if (!cfp_private(ref, 0x351a8, lookup, NELEM(lookup), status))
                return 0;
            if (!*status)
                return 1;
        }
        _Alignas(4) FSSpec file;
        const uint32_t lookup[] = {(int16_t)display, US_TO_SYN68K(&file)};
        if (!cfp_private(ref, 0x1ccf4, lookup, NELEM(lookup), status))
            return 0;
        if (!*status) {
            if (!color_default_open_file(ref, &file, output, status))
                return 0;
            if (!*status)
                return 1;
        }
    }
    return color_default_get(ref, 0, 0, output, status);
}

static int color_default_get_system(const ROMlib_carbon_reference* ref, uint32_t output, uint32_t flags,
                                    uint32_t* status)
{
    _Alignas(4) uint32_t display = 0;
    const uint32_t location = US_TO_SYN68K(&display);
    if (!cfp_private(ref, 0x34e90, &location, 1, status))
        return 0;
    if (!*status && display && !(flags & 1))
        return color_default_get_display(ref, CL(display), output, status);
    return color_default_get(ref, 0, 0, output, status);
}

static int color_default_set_file(const ROMlib_carbon_reference* ref, uint32_t kind, uint32_t key, const FSSpec* file,
                                  uint32_t* status)
{
    const uint32_t address = US_TO_SYN68K(file);
    unsigned char suitable = 0;
    const uint32_t validate[] = {kind, key, address, US_TO_SYN68K(&suitable)};
    if (!cfp_private(ref, 0x180e8, validate, NELEM(validate), status))
        return 0;
    if (*status)
        return 1;
    if (!suitable) {
        *status = paramErr;
        return 1;
    }

    _Alignas(4) unsigned char setting[56];
    const uint32_t query[] = {kind, key, US_TO_SYN68K(setting)};
    if (!cfp_private(ref, 0x17a48, query, NELEM(query), status))
        return 0;
    if (*status)
        return 1;
    const uint32_t save[] = {(int16_t)color_read(setting + 6, 2), address};
    if (!cfp_private(ref, 0x1ce30, save, NELEM(save), status))
        return 0;
    return *status || key || cfp_private(ref, 0x17d98, &address, 1, status);
}

static int color_default_parse_location(const ROMlib_carbon_reference* ref, uint32_t address, FSSpec* file,
                                        uint32_t* status)
{
    const unsigned char* data = color_profile_memory(address, 2, 0);
    if (!data) {
        *status = paramErr;
        return 1;
    }
    switch (color_read(data, 2)) {
    case 1:
        data = color_profile_memory(address, 2 + sizeof *file, 0);
        if (!data) {
            *status = paramErr;
            return 1;
        }
        memcpy(file, data + 2, sizeof *file);
        *status = noErr;
        return 1;
    case 5: {
        const uint32_t args[] = {US_TO_SYN68K(file), address};
        return cfp_private(ref, 0x318e4, args, NELEM(args), status);
    }
    default:
        *status = color_profile_invalid_location;
        return 1;
    }
}

static int color_default_set_system_profile(const ROMlib_carbon_reference* ref, uint32_t address, uint32_t* status)
{
    _Alignas(4) FSSpec file;
    if (!color_default_parse_location(ref, address, &file, status))
        return 0;
    if (*status)
        return 1;
    if (!color_default_set_file(ref, 0, 0, &file, status))
        return 0;
    if (*status)
        return 1;

    _Alignas(4) uint32_t display = 0, profile = 0;
    const uint32_t display_address = US_TO_SYN68K(&display);
    uint32_t ignored;
    if (!cfp_private(ref, 0x34e90, &display_address, 1, &ignored))
        return 0;
    if (!ignored && display) {
        const uint32_t open[] = {US_TO_SYN68K(&profile), address};
        if (!cfp_private(ref, 0x34a10, open, NELEM(open), &ignored))
            return 0;
        if (!ignored) {
            const uint32_t update[] = {CL(display), CL(profile)};
            const int updated = cfp_private(ref, 0x34d28, update, NELEM(update), NULL);
            return cfp_private(ref, 0x34a70, &update[1], 1, NULL) && updated;
        }
    }
    return 1;
}

static int color_default_set(const ROMlib_carbon_reference* ref, enum color_default_operation operation, uint32_t key,
                             uint32_t profile, uint32_t* status)
{
    if (!key) {
        *status = paramErr;
        return 1;
    }
    _Alignas(4) unsigned char location[color_default_location_size] = {0};
    _Alignas(4) uint32_t capacity = CL(sizeof location);
    const uint32_t address = US_TO_SYN68K(location);
    const uint32_t args[] = {profile, address, US_TO_SYN68K(&capacity)};
    if (!cfp_private(ref, 0x34cf8, args, NELEM(args), status))
        return 0;
    if (*status)
        return 1;
    if (operation == color_default_set_use && key == color_default_display_use)
        return color_default_set_system_profile(ref, address, status);
    _Alignas(4) FSSpec file;
    if (!color_default_parse_location(ref, address, &file, status))
        return 0;
    if (*status)
        return 1;
    return color_default_set_file(ref, operation == color_default_set_space ? 1 : 2, key, &file, status);
}

static int color_release_display_buffer(const ROMlib_carbon_reference* ref, uint32_t* encoded)
{
    const uint32_t location = US_TO_SYN68K(encoded);
    return !*encoded || cfp_private(ref, 0x2de5c, &location, 1, NULL);
}

static int color_display_set_gamma(const ROMlib_carbon_reference* ref, uint32_t display, uint32_t gamma,
                                   uint32_t* status)
{
    const unsigned char* record = color_profile_memory(gamma, 4, 0);
    *status = paramErr;
    if (!display || !record)
        return 1;

    _Alignas(4) uint32_t encoded = 0;
    const uint32_t acquire[] = {display, US_TO_SYN68K(&encoded)};
    int ok = cfp_private(ref, 0x332b0, acquire, NELEM(acquire), status);
    if (ok && !*status) {
        switch (color_read(record, 4)) {
        case 0: {
            record = color_profile_memory(gamma, 10, 0);
            if (!record) {
                *status = paramErr;
                break;
            }
            const uint64_t bytes =
                (uint64_t)color_read(record + 4, 2) * color_read(record + 6, 2) * color_read(record + 8, 2);
            const uint32_t table[] = {gamma + 4, CL(encoded)};
            if (bytes > INT32_MAX - 10 || !color_profile_memory(gamma, bytes + 10, 0))
                *status = paramErr;
            else
                ok = cfp_private(ref, 0x3376c, table, NELEM(table), status);
            break;
        }
        case 1: {
            const uint32_t formula[] = {gamma + 4, CL(encoded)};
            if (!color_profile_memory(gamma, 40, 0))
                *status = paramErr;
            else
                ok = cfp_private(ref, 0x333b4, formula, NELEM(formula), status);
            break;
        }
        default:
            *status = paramErr;
            break;
        }
        if (ok && !*status) {
            const uint32_t apply[] = {display, CL(encoded)};
            ok = cfp_private(ref, 0x33af0, apply, NELEM(apply), status);
        }
    }
    return color_release_display_buffer(ref, &encoded) && ok;
}

static int color_display_get_gamma(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t display = call->words[0], gamma = call->words[1], size = call->words[2];
    unsigned char* size_word = color_profile_memory(size, 4, 1);
    if (!display || !size_word)
        return color_profile_result(call, paramErr);
    uint32_t capacity = gamma ? color_read(size_word, 4) : 0;
    if (gamma && (capacity < 4 || !color_profile_memory(gamma, capacity, 1)))
        return color_profile_result(call, paramErr);

    _Alignas(4) uint32_t encoded = 0;
    const uint32_t query[] = {display, US_TO_SYN68K(&encoded)};
    uint32_t status;
    int ok = cfp_private(ref, 0x32e3c, query, NELEM(query), &status);
    if (ok && !status) {
        _Alignas(4) uint32_t bytes = CL(gamma ? capacity - 4 : 0);
        const uint32_t read[] = {gamma ? gamma + 4 : 0, CL(encoded), US_TO_SYN68K(&bytes)};
        ok = (!gamma || cfp_write(gamma, 0)) && cfp_private(ref, 0x32f84, read, NELEM(read), &status);
        if (ok && !gamma)
            color_write(size_word, 4, CL(bytes) + 4);
    }
    return color_release_display_buffer(ref, &encoded) && ok && color_profile_result(call, status);
}

static int color_display_set_profile(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t display = call->words[0], profile = call->words[1];
    if (!display || !profile)
        return color_profile_result(call, paramErr);
    _Alignas(4) uint32_t version = 0, profile_class = 0;
    const uint32_t version_args[] = {profile, US_TO_SYN68K(&version)};
    const uint32_t class_args[] = {profile, US_TO_SYN68K(&profile_class)};
    if (!cfp_private(ref, 0x1592c, version_args, NELEM(version_args), NULL) ||
        !cfp_private(ref, 0x15aa0, class_args, NELEM(class_args), NULL))
        return 0;
    if (CL(version) < 0x02000000 || CL(profile_class) != TICK("mntr"))
        return color_profile_result(call, color_profile_invalid);

    uint32_t flags, status;
    if (!color_display_capabilities(ref, &flags))
        return 0;
    if (flags & 0x20) {
        const uint32_t update[] = {display, profile};
        if (!cfp_private(ref, 0x351c0, update, NELEM(update), &status))
            return 0;
        if (!status)
            return color_profile_result(call, noErr);
    }
    _Alignas(4) unsigned char location[color_default_location_size] = {0};
    _Alignas(4) uint32_t size = CL(sizeof location);
    const uint32_t details[] = {profile, US_TO_SYN68K(location), US_TO_SYN68K(&size)};
    if (!cfp_private(ref, 0x34cf8, details, NELEM(details), &status))
        return 0;
    if (status || color_read(location, 2) != 1)
        return color_profile_result(call, status);
    const uint32_t save[] = {(int16_t)display, US_TO_SYN68K(location + 2)};
    if (!cfp_private(ref, 0x1ce30, save, NELEM(save), &status))
        return 0;
    if (status)
        return color_profile_result(call, status);

    _Alignas(4) uint32_t gamma_size = 0, gamma = 0;
    const uint32_t measure[] = {profile, TICK("vcgt"), US_TO_SYN68K(&gamma_size)};
    if (!cfp_private(ref, 0x3002c, measure, NELEM(measure), &status))
        return 0;
    if (status || CL(gamma_size) < 12)
        return color_profile_result(call, noErr);
    const uint32_t allocate[] = {US_TO_SYN68K(&gamma), CL(gamma_size)};
    if (!cfp_private(ref, 0x2dbb8, allocate, NELEM(allocate), &status))
        return 0;
    if (status)
        return color_profile_result(call, status);
    const uint32_t read[] = {profile, TICK("vcgt"), CL(gamma_size), CL(gamma)};
    const int ok = cfp_private(ref, 0x306d0, read, NELEM(read), &status) &&
                   (status || color_display_set_gamma(ref, display, CL(gamma) + 8, &status));
    return color_release_display_buffer(ref, &gamma) && ok && color_profile_result(call, noErr);
}

static uint64_t color_display_align_tag(uint64_t size)
{
    return (size + 3) & ~(uint64_t)3;
}

static int color_display_profile_size(const unsigned char* data, uint32_t* result)
{
    const bool extended = color_read(data, 4) >= 0x58;
    const uint32_t channels = extended ? color_read(data + 0x4e, 2) : 0;
    const uint32_t entries = channels ? color_read(data + 0x50, 2) : 0;
    const uint32_t width = channels ? color_read(data + 0x52, 2) : 0;
    const bool custom = color_read(data + 0x46, 4) != 0;
    const uint32_t name_address = color_read(data + 0x42, 4);
    const unsigned char* name = color_profile_memory(name_address, 1, 0);
    if (name_address && !name)
        return 0;

    const uint64_t description = (uint64_t)color_read(data + 0x30, 4) + 2 * (uint64_t)color_read(data + 0x38, 4) + 90 +
                                 (name && *name > 67 ? *name - 67 : 0);
    const uint64_t trc = channels && !custom ? 12 + 2 * entries : 14;
    const uint64_t gamma = channels ? 62 + channels * entries * width : 56;
    const uint64_t response = channels && custom ? 18 + channels * 256 : 48;
    const uint64_t extent = 0x13c + 0x80 + 3 * color_display_align_tag(trc) + color_display_align_tag(gamma) +
                            color_display_align_tag(response) + color_display_align_tag(description);
    if (extent > UINT32_MAX)
        return 0;
    *result = extent;
    return 1;
}

static int color_create_display_profile(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t output = call->words[0], location = call->words[1], size_output = call->words[2];
    const uint32_t parameters = call->words[3];
    if ((!output && !size_output) || (output && !color_profile_memory(output, 4, 1)) ||
        (size_output && !color_profile_memory(size_output, 4, 1)))
        return color_profile_result(call, paramErr);

    uint32_t size = 0, status = paramErr;
    _Alignas(4) uint32_t encoded = 0;
    const unsigned char* data = color_profile_memory(parameters, 4, 0);
    if (data) {
        const uint32_t bytes = color_read(data, 4);
        data = color_profile_memory(parameters, bytes == 0x4e ? 0x4e : 0x58, 0);
    }
    if (data) {
        if (!cfp_private(ref, 0x18a74, &parameters, 1, &status))
            return 0;
        if (!status && size_output && !color_display_profile_size(data, &size))
            status = paramErr;
    }

    int ok = 1;
    if (!status && output) {
        _Alignas(128) unsigned char header[128];
        const uint32_t address = US_TO_SYN68K(header);
        const uint32_t toc = US_TO_SYN68K(ref->toc);
        const uint32_t create[] = {US_TO_SYN68K(&encoded), location};
        ok = cfp_private(ref, 0x34a58, create, NELEM(create), &status);
        if (ok && !status) {
            const uint32_t profile = CL(encoded);
            const uint32_t time[] = {address + 24, 0};
            const uint32_t set_header[] = {profile, address};
            const uint32_t tags[] = {profile, parameters};
            const uint32_t name[] = {profile,
                                     color_read(data + 0x34, 4),
                                     color_read(data + 0x30, 4),
                                     color_read(data + 0x42, 4),
                                     (uint32_t)(int16_t)color_read(data + 0x40, 2),
                                     color_read(data + 0x3c, 4),
                                     color_read(data + 0x38, 4)};
            const uint32_t copyright[] = {profile, TICK("cprt"), 44, toc + 0x1494};
            ok = cfp_move(address, toc + 0x14c4, sizeof header) && cfp_private(ref, 0x2dfb4, time, NELEM(time), NULL) &&
                 cfp_private(ref, 0x34ad0, set_header, NELEM(set_header), &status) &&
                 (status || cfp_private(ref, 0x190e4, tags, NELEM(tags), &status)) &&
                 (status || cfp_private(ref, 0x193a4, tags, NELEM(tags), &status)) &&
                 (status || cfp_private(ref, 0x34d88, name, NELEM(name), &status)) &&
                 (status || cfp_private(ref, 0x34b00, copyright, NELEM(copyright), &status)) &&
                 (status || cfp_private(ref, 0x19cec, tags, NELEM(tags), &status)) &&
                 (status || cfp_private(ref, 0x1a19c, tags, NELEM(tags), &status));
        }
    }

    if ((status || !ok) && encoded) {
        const uint32_t profile = CL(encoded);
        if (!cfp_private(ref, 0x34a70, &profile, 1, NULL))
            ok = 0;
        encoded = 0;
    }
    if (ok && (!size_output || cfp_write(size_output, size)) && (!output || cfp_write(output, CL(encoded))))
        return color_profile_result(call, status);
    if (encoded) {
        const uint32_t profile = CL(encoded);
        cfp_private(ref, 0x34a70, &profile, 1, NULL);
    }
    return 0;
}

static int color_launch_control_panel(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    if (call->words[0])
        return color_profile_result(call, paramErr);
    const uint32_t signature = TICK("synC");
    _Alignas(8) unsigned char serial[8];
    const uint32_t find_process[] = {signature, US_TO_SYN68K(serial), 0};
    uint32_t status;
    if (!cfp_private(ref, 0x113c8, find_process, NELEM(find_process), &status))
        return 0;
    if (!status)
        return cfp_private(ref, 0x35700, &find_process[1], 1, &status) && color_profile_result(call, status);
    if (status != (uint32_t)procNotFound)
        return color_profile_result(call, status);

    _Alignas(4) uint32_t folder = 0;
    _Alignas(2) uint16_t system_volume = 0, volume = 0;
    const uint32_t find_system[] = {(uint32_t)(int32_t)-32768, kSystemFolderType, 0, US_TO_SYN68K(&system_volume),
                                    US_TO_SYN68K(&folder)};
    if (!cfp_private(ref, 0x35118, find_system, NELEM(find_system), &status))
        return 0;
    if (status)
        return color_profile_result(call, status);
    volume = system_volume;

    const int32_t desktop_item_missing = -5012;
    for (int16_t index = 0;;) {
        if (!index || volume != system_volume) {
            _Alignas(128) unsigned char parameters[128] = {0};
            _Alignas(32) unsigned char attributes[24] = {0};
            const uint32_t address = US_TO_SYN68K(parameters);
            color_write(parameters + 22, 2, CW(volume));
            color_write(parameters + 32, 4, US_TO_SYN68K(attributes));
            color_write(parameters + 36, 4, sizeof attributes);
            if (!cfp_private(ref, 0x35718, &address, 1, &status))
                return 0;
            if (status)
                return color_profile_result(call, status);
            if (color_read(attributes + 2, 4) & 0x1000) {
                _Alignas(128) FSSpec spec;
                const uint32_t search[] = {signature, (uint32_t)(int32_t)(int16_t)CW(volume), US_TO_SYN68K(&spec)};
                if (!cfp_private(ref, 0x114ec, search, NELEM(search), &status))
                    return 0;
                if (!status) {
                    _Alignas(64) unsigned char launch[44] = {0};
                    color_write(launch + 6, 2, extendedBlock);
                    color_write(launch + 8, 4, extendedBlockLen);
                    color_write(launch + 12, 2, 0x800);
                    color_write(launch + 14, 2, launchContinue | 0x800);
                    color_write(launch + 16, 4, search[2]);
                    const uint32_t launch_address = US_TO_SYN68K(launch);
                    return cfp_private(ref, 0x35730, &launch_address, 1, &status) && color_profile_result(call, status);
                }
                if (status != (uint32_t)desktop_item_missing)
                    return color_profile_result(call, status);
            }
        }
        if (index == INT16_MAX)
            return color_profile_result(call, fnfErr);
        const uint32_t next[] = {++index, US_TO_SYN68K(&volume)};
        if (!cfp_private(ref, 0x11464, next, NELEM(next), &status))
            return 0;
        if (status)
            return color_profile_result(
                call, status == (uint32_t)nsvErr || status == (uint32_t)desktop_item_missing ? fnfErr : status);
    }
}

static int color_default_dispatch(ROMlib_carbon_ppc_frame* call, enum color_default_operation operation,
                                  const char* name, uint32_t offset)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_load_reference("ColorSync", name, &ref))
        return ROMlib_carbon_report_failure("Missing ColorSync default profile implementation");
    if (ref.code_offset != offset || ref.code_size != 0x366ec || ref.toc_offset != 0x464 || ref.toc_size != 0x1670)
        return ROMlib_carbon_invoke_frame(ref.function, call);
    const uint32_t first = call->words[0], second = call->words[1];
    uint32_t status;
    switch (operation) {
    case color_default_by_space:
    case color_default_by_use:
        if (!first)
            return color_profile_result(call, paramErr);
        if (operation == color_default_by_use && first == color_default_display_use)
            return color_default_get_system(&ref, second, 0, &status) && color_profile_result(call, status);
        return color_default_get(&ref, operation == color_default_by_space ? 1 : 2, first, second, &status) &&
               color_profile_result(call, status);
    case color_default_for_display:
        return color_default_get_display(&ref, first, second, &status) && color_profile_result(call, status);
    case color_default_new_display:
        return color_create_display_profile(&ref, call);
    case color_default_launch_panel:
        return color_launch_control_panel(&ref, call);
    case color_default_set_display:
        return color_display_set_profile(&ref, call);
    case color_default_get_system_avid:
    case color_default_set_system_avid:
        return color_system_avid(&ref, call, operation == color_default_set_system_avid);
    case color_default_get_gamma:
        return color_display_get_gamma(&ref, call);
    case color_default_set_gamma:
        return color_display_set_gamma(&ref, first, second, &status) && color_profile_result(call, status);
    case color_default_system:
    case color_default_system_private:
        return color_default_get_system(&ref, first, operation == color_default_system ? 0 : second, &status) &&
               color_profile_result(call, status);
    case color_default_set_space:
    case color_default_set_use:
        return color_default_set(&ref, operation, first, second, &status) && color_profile_result(call, status);
    case color_default_set_system: {
        const void* file = color_profile_memory(first, sizeof(FSSpec), 0);
        if (!file)
            return color_profile_result(call, paramErr);
        _Alignas(4) unsigned char location[color_default_location_size] = {0};
        color_write(location, 2, 1);
        memcpy(location + 2, file, sizeof(FSSpec));
        return color_default_set_system_profile(&ref, US_TO_SYN68K(location), &status) &&
               color_profile_result(call, status);
    }
    case color_default_set_location:
        return color_default_set_system_profile(&ref, first, &status) && color_profile_result(call, status);
    }
    return 0;
}

#define CARBON_COLOR_DEFAULT(name, operation, offset)                                  \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call)                            \
    {                                                                                  \
        return color_default_dispatch(call, color_default_##operation, #name, offset); \
    }

CARBON_COLOR_DEFAULT(CMGetDefaultProfileBySpace, by_space, 0x186dc)
CARBON_COLOR_DEFAULT(CMGetDefaultProfileByUse, by_use, 0x187a0)
CARBON_COLOR_DEFAULT(CMGetProfileByAVID, for_display, 0x17f34)
CARBON_COLOR_DEFAULT(CMSetProfileByAVID, set_display, 0x1839c)
CARBON_COLOR_DEFAULT(CMGetSystemProfileAVID, get_system_avid, 0x17474)
CARBON_COLOR_DEFAULT(CMSetSystemProfileAVID, set_system_avid, 0x188b8)
CARBON_COLOR_DEFAULT(CMGetGammaByAVID, get_gamma, 0x11928)
CARBON_COLOR_DEFAULT(CMSetGammaByAVID, set_gamma, 0x1194c)
CARBON_COLOR_DEFAULT(CMNewDisplayProfile, new_display, 0x1a2ec)
CARBON_COLOR_DEFAULT(CMLaunchControlPanel, launch_panel, 0x118dc)
CARBON_COLOR_DEFAULT(CMGetSystemProfile, system, 0x180c0)
CARBON_COLOR_DEFAULT(CMGetSystemProfilePriv, system_private, 0x18030)
CARBON_COLOR_DEFAULT(CMSetDefaultProfileBySpace, set_space, 0x18700)
CARBON_COLOR_DEFAULT(CMSetDefaultProfileByUse, set_use, 0x187fc)
CARBON_COLOR_DEFAULT(CMSetSystemProfile, set_system, 0x1867c)
CARBON_COLOR_DEFAULT(NCMSetSystemProfile, set_location, 0x18590)

#undef CARBON_COLOR_DEFAULT
#endif
