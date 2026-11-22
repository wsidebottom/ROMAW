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

static int color_default_get_display(const ROMlib_carbon_reference* ref, uint32_t display, uint32_t output,
                                     uint32_t* status)
{
    if (display && output) {
        const uint32_t capabilities = cfp_toc(ref, (unsigned)-0x2c);
        uint32_t flags;
        if (!capabilities || !cfp_get(capabilities, &flags))
            return 0;
        if (!flags) {
            const uint32_t gestalt[] = {color_default_display_use, capabilities};
            if (!cfp_private(ref, 0x35190, gestalt, NELEM(gestalt), status))
                return 0;
            if (*status && !cfp_write(capabilities, 0))
                return 0;
            if (!cfp_get(capabilities, &flags))
                return 0;
        }
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
CARBON_COLOR_DEFAULT(CMGetSystemProfile, system, 0x180c0)
CARBON_COLOR_DEFAULT(CMGetSystemProfilePriv, system_private, 0x18030)
CARBON_COLOR_DEFAULT(CMSetDefaultProfileBySpace, set_space, 0x18700)
CARBON_COLOR_DEFAULT(CMSetDefaultProfileByUse, set_use, 0x187fc)
CARBON_COLOR_DEFAULT(CMSetSystemProfile, set_system, 0x1867c)
CARBON_COLOR_DEFAULT(NCMSetSystemProfile, set_location, 0x18590)

#undef CARBON_COLOR_DEFAULT
#endif
