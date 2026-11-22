/*+===================================================================
  File:      cfx_timezones.c

  Summary:   Implements CoreFoundation fixed-offset and transition-table time zones.

  Classes:   None.

  Functions: ROMlib_cfx_CFTimeZoneCopyAbbreviation, ROMlib_cfx_CFTimeZoneCopyDefault,
             ROMlib_cfx_CFTimeZoneCopySystem, ROMlib_cfx_CFTimeZoneCreate,
             ROMlib_cfx_CFTimeZoneCreateWithTimeIntervalFromGMT, ROMlib_cfx_CFTimeZoneGetData,
             ROMlib_cfx_CFTimeZoneGetName, ROMlib_cfx_CFTimeZoneGetSecondsFromGMT,
             ROMlib_cfx_CFTimeZoneGetTypeID, ROMlib_cfx_CFTimeZoneIsDaylightSavingTime,
             ROMlib_cfx_CFTimeZoneResetSystem, ROMlib_cfx_CFTimeZoneSetDefault

  Origin:    Universal Interfaces 3.4.2, local CoreFoundationLib 1.4 runtime layout and TZif format.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "cfx_internal.h"

#include "rsys/osutil.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>

typedef struct {
    uint32 version, class_name, initialize, copy, finalize, equal, hash, format, describe;
    uint32 type, system, preferred;
    char name[16];
} timezone_storage;

typedef struct {
    const Byte *transitions, *indices, *types, *abbreviations;
    uint32 time_count, type_count, abbreviation_count;
} timezone_table;

enum { tz_name_offset = 8, tz_data_offset = 12, tz_header_size = 44, tz_type_size = 6 };

static uint32 zone_read_u32(const Byte* bytes)
{
    uint32 value;
    memcpy(&value, bytes, sizeof value);
    return CL(value);
}

static void zone_write_u32(Byte* bytes, uint32 value)
{
    value = CL(value);
    memcpy(bytes, &value, sizeof value);
}

static int zone_read_table(uint32 data, timezone_table* table)
{
    const void* object = SYN68K_TO_US_CHECK0(data);
    LONGINT length = ROMlib_carbon_cf_data_length(object);
    const Byte* bytes = ROMlib_carbon_cf_data_bytes(object);
    if (!bytes || length < tz_header_size || memcmp(bytes, "TZif", 4))
        return 0;

    uint32 utc_count = zone_read_u32(bytes + 20), wall_count = zone_read_u32(bytes + 24);
    uint32 leap_count = zone_read_u32(bytes + 28), time_count = zone_read_u32(bytes + 32);
    uint32 type_count = zone_read_u32(bytes + 36), abbreviation_count = zone_read_u32(bytes + 40);
    uint64 extent = tz_header_size + 5 * (uint64)time_count + tz_type_size * (uint64)type_count + abbreviation_count +
                    8 * (uint64)leap_count + utc_count + wall_count;
    if (extent > (uint64)length || type_count == 0 || type_count > 256 || abbreviation_count == 0 ||
        (utc_count && utc_count != type_count) || (wall_count && wall_count != type_count))
        return 0;

    const Byte* transitions = bytes + tz_header_size;
    const Byte* indices = transitions + 4 * time_count;
    const Byte* types = indices + time_count;
    const Byte* abbreviations = types + tz_type_size * type_count;
    *table = (timezone_table){transitions, indices, types, abbreviations, time_count, type_count, abbreviation_count};
    return 1;
}

static int zone_validate_table(const timezone_table* table)
{
    int32 previous = INT32_MIN;
    for (uint32 index = 0; index < table->time_count; ++index) {
        int32 transition = (int32)zone_read_u32(table->transitions + 4 * index);
        if ((index && transition <= previous) || table->indices[index] >= table->type_count)
            return 0;
        previous = transition;
    }
    for (uint32 index = 0; index < table->type_count; ++index) {
        const Byte* type = table->types + tz_type_size * index;
        unsigned abbreviation = type[5];
        if (type[4] > 1 || abbreviation >= table->abbreviation_count ||
            !memchr(table->abbreviations + abbreviation, 0, table->abbreviation_count - abbreviation))
            return 0;
    }
    return 1;
}

static const Byte* zone_time_type(const timezone_table* table, double absolute)
{
    uint32 lower = 0, upper = table->time_count;
    while (lower < upper) {
        uint32 middle = lower + (upper - lower) / 2;
        double transition = (int32)zone_read_u32(table->transitions + 4 * middle) - 978307200.0;
        if (absolute < transition)
            upper = middle;
        else
            lower = middle + 1;
    }
    return table->types + tz_type_size * (lower ? table->indices[lower - 1] : 0);
}

static uint32 zone_retain(uint32 object)
{
    return US_TO_SYN68K_CHECK0(ROMlib_carbon_cf_retain(SYN68K_TO_US_CHECK0(object)));
}

static void zone_release(uint32 object)
{
    ROMlib_carbon_cf_release(SYN68K_TO_US_CHECK0(object));
}

static int zone_finalize(ROMlib_carbon_ppc_frame* call_frame)
{
    zone_release(cfp_read(CFX_ARG(0) + tz_name_offset));
    zone_release(cfp_read(CFX_ARG(0) + tz_data_offset));
    return 1;
}

static int zone_equal(ROMlib_carbon_ppc_frame* call_frame)
{
    uint32 first = CFX_ARG(0), second = CFX_ARG(1);
    return CFX_RESULT(ROMlib_cfvalue_equal(cfp_read(first + tz_name_offset), cfp_read(second + tz_name_offset)) &&
                      ROMlib_cfvalue_equal(cfp_read(first + tz_data_offset), cfp_read(second + tz_data_offset)));
}

static int zone_hash(ROMlib_carbon_ppc_frame* call_frame)
{
    return CFX_RESULT(ROMlib_cfvalue_hash(cfp_read(CFX_ARG(0) + tz_name_offset)));
}

static int zone_copy_name(ROMlib_carbon_ppc_frame* call_frame)
{
    return CFX_RESULT(zone_retain(cfp_read(CFX_ARG(0) + tz_name_offset)));
}

static timezone_storage* zone_storage(void)
{
    uint32 address;
    timezone_storage* state = ROMlib_carbon_timezone_storage(sizeof *state, &address);
    if (!state || state->type)
        return state;
    if (!state->finalize) {
        uint32 vector;
        if (!ROMlib_carbon_native_ppc_callback("CFTimeZoneFinalize", zone_finalize, &vector))
            return NULL;
        state->finalize = CL(vector);
    }
    if (!state->equal) {
        uint32 vector;
        if (!ROMlib_carbon_native_ppc_callback("CFTimeZoneEqual", zone_equal, &vector))
            return NULL;
        state->equal = CL(vector);
    }
    if (!state->hash) {
        uint32 vector;
        if (!ROMlib_carbon_native_ppc_callback("CFTimeZoneHash", zone_hash, &vector))
            return NULL;
        state->hash = CL(vector);
    }
    if (!state->describe) {
        uint32 vector;
        if (!ROMlib_carbon_native_ppc_callback("CFTimeZoneCopyName", zone_copy_name, &vector))
            return NULL;
        state->format = state->describe = CL(vector);
    }
    memcpy(state->name, "CFTimeZone", sizeof "CFTimeZone");
    state->class_name = CL(address + offsetof(timezone_storage, name));
    state->type = CL(ROMlib_cfc_CFRuntimeRegisterClass(address));
    return state->type ? state : NULL;
}

static unsigned zone_object_type(uint32 object)
{
    const void* memory = object ? ROMlib_carbon_guest_memory(object, 8, 0) : NULL;
    return memory ? ROMlib_carbon_cf_type_id(memory) : 0;
}

static uint32 zone_create(timezone_storage* state, uint32 allocator, uint32 name, uint32 data)
{
    timezone_table table;
    if (!state || !name || !data || zone_object_type(name) != ROMlib_cfvalue_string_type() ||
        zone_object_type(data) != ROMlib_cfdata_type_id() || !zone_read_table(data, &table) ||
        !zone_validate_table(&table))
        return 0;
    uint32 owned_name = US_TO_SYN68K_CHECK0(ROMlib_carbon_cf_string_copy(allocator, SYN68K_TO_US_CHECK0(name)));
    uint32 owned_data = owned_name ? ROMlib_cfdata_create_copy(allocator, data) : 0;
    uint32 zone = owned_data ? ROMlib_cfc_CFRuntimeCreateInstance(allocator, CL(state->type), 8, 0) : 0;
    if (zone) {
        cfp_write(zone + tz_name_offset, owned_name);
        cfp_write(zone + tz_data_offset, owned_data);
    }
    else {
        zone_release(owned_name);
        zone_release(owned_data);
    }
    return zone;
}

static uint32 zone_fixed(timezone_storage* state, uint32 allocator, int32 seconds)
{
    _Alignas(32) char name[32] = "GMT";
    if (seconds) {
        unsigned magnitude = seconds < 0 ? -(int64)seconds : seconds;
        snprintf(name, sizeof name, "GMT%c%02u%02u", seconds < 0 ? '-' : '+', magnitude / 3600, magnitude % 3600 / 60);
    }
    _Alignas(64) Byte table[64] = {'T', 'Z', 'i', 'f'};
    size_t name_length = strlen(name) + 1;
    zone_write_u32(table + 36, 1);
    zone_write_u32(table + 40, name_length);
    zone_write_u32(table + tz_header_size, seconds);
    memcpy(table + tz_header_size + tz_type_size, name, name_length);
    uint32 string = ROMlib_cfstring_cstring(allocator, US_TO_SYN68K(name), 0x600);
    uint32 data =
        string ? ROMlib_cfdata_create(allocator, US_TO_SYN68K(table), tz_header_size + tz_type_size + name_length) : 0;
    uint32 zone = zone_create(state, allocator, string, data);
    zone_release(data);
    zone_release(string);
    return zone;
}

static uint32 zone_field(timezone_storage* state, uint32 zone, uint32 offset)
{
    return state && zone_object_type(zone) == CL(state->type) ? cfp_read(zone + offset) : 0;
}

static const Byte* zone_period(timezone_storage* state, uint32 zone, double absolute, timezone_table* table)
{
    uint32 data = zone_field(state, zone, tz_data_offset);
    return data && !isnan(absolute) && zone_read_table(data, table) ? zone_time_type(table, absolute) : NULL;
}

static uint32 zone_system(timezone_storage* state)
{
    if (!state)
        return 0;
    if (!state->system)
        state->system = CL(zone_fixed(state, 0, ROMlib_machine_gmt_delta()));
    return CL(state->system);
}

#define ZONE_STATE(name, offset)                                            \
    ROMlib_carbon_reference reference;                                      \
    int disposition = cfx_reference(#name, offset, call_frame, &reference); \
    if (disposition != 1)                                                   \
        return disposition == 2;                                            \
    timezone_storage* state = zone_storage()

int ROMlib_cfx_CFTimeZoneGetTypeID(ROMlib_carbon_ppc_frame* call_frame)
{
    ZONE_STATE(CFTimeZoneGetTypeID, 0x25764);
    return CFX_RESULT(state ? CL(state->type) : 0);
}

int ROMlib_cfx_CFTimeZoneCopySystem(ROMlib_carbon_ppc_frame* call_frame)
{
    ZONE_STATE(CFTimeZoneCopySystem, 0x2576c);
    return CFX_RESULT(zone_retain(zone_system(state)));
}

int ROMlib_cfx_CFTimeZoneResetSystem(ROMlib_carbon_ppc_frame* call_frame)
{
    ZONE_STATE(CFTimeZoneResetSystem, 0x25774);
    if (state) {
        uint32 previous = CL(state->system);
        state->system = 0;
        zone_release(previous);
    }
    return 1;
}

int ROMlib_cfx_CFTimeZoneCopyDefault(ROMlib_carbon_ppc_frame* call_frame)
{
    ZONE_STATE(CFTimeZoneCopyDefault, 0x25778);
    return CFX_RESULT(zone_retain(state && state->preferred ? CL(state->preferred) : zone_system(state)));
}

int ROMlib_cfx_CFTimeZoneSetDefault(ROMlib_carbon_ppc_frame* call_frame)
{
    ZONE_STATE(CFTimeZoneSetDefault, 0x25780);
    uint32 zone = CFX_ARG(0);
    if (state && (!zone || zone_object_type(zone) == CL(state->type))) {
        uint32 previous = CL(state->preferred);
        if (zone && !(zone = zone_retain(zone)))
            return 0;
        state->preferred = CL(zone);
        zone_release(previous);
    }
    return 1;
}

int ROMlib_cfx_CFTimeZoneCreate(ROMlib_carbon_ppc_frame* call_frame)
{
    ZONE_STATE(CFTimeZoneCreate, 0x25798);
    return CFX_RESULT(zone_create(state, CFX_ARG(0), CFX_ARG(1), CFX_ARG(2)));
}

int ROMlib_cfx_CFTimeZoneCreateWithTimeIntervalFromGMT(ROMlib_carbon_ppc_frame* call_frame)
{
    ZONE_STATE(CFTimeZoneCreateWithTimeIntervalFromGMT, 0x257a0);
    double seconds = cfx_double(call_frame, 0);
    if (!isfinite(seconds) || seconds < -64800 || seconds > 64800)
        return CFX_RESULT(0);
    return CFX_RESULT(zone_fixed(state, CFX_ARG(0), (int32)round(seconds / 60) * 60));
}

int ROMlib_cfx_CFTimeZoneGetName(ROMlib_carbon_ppc_frame* call_frame)
{
    ZONE_STATE(CFTimeZoneGetName, 0x257b0);
    return CFX_RESULT(zone_field(state, CFX_ARG(0), tz_name_offset));
}

int ROMlib_cfx_CFTimeZoneGetData(ROMlib_carbon_ppc_frame* call_frame)
{
    ZONE_STATE(CFTimeZoneGetData, 0x257b8);
    return CFX_RESULT(zone_field(state, CFX_ARG(0), tz_data_offset));
}

int ROMlib_cfx_CFTimeZoneGetSecondsFromGMT(ROMlib_carbon_ppc_frame* call_frame)
{
    ZONE_STATE(CFTimeZoneGetSecondsFromGMT, 0x257c0);
    timezone_table table;
    const Byte* type = zone_period(state, CFX_ARG(0), cfx_double(call_frame, 0), &table);
    return cfx_double_result(call_frame, type ? (int32)zone_read_u32(type) : 0);
}

int ROMlib_cfx_CFTimeZoneCopyAbbreviation(ROMlib_carbon_ppc_frame* call_frame)
{
    ZONE_STATE(CFTimeZoneCopyAbbreviation, 0x257c8);
    timezone_table table;
    const Byte* type = zone_period(state, CFX_ARG(0), cfx_double(call_frame, 0), &table);
    if (!type)
        return CFX_RESULT(0);
    const Byte* abbreviation = table.abbreviations + type[5];
    return CFX_RESULT(
        ROMlib_cfstring_bytes(0, US_TO_SYN68K(abbreviation), strlen((const char*)abbreviation), 0x600, 0));
}

int ROMlib_cfx_CFTimeZoneIsDaylightSavingTime(ROMlib_carbon_ppc_frame* call_frame)
{
    ZONE_STATE(CFTimeZoneIsDaylightSavingTime, 0x257d0);
    timezone_table table;
    const Byte* type = zone_period(state, CFX_ARG(0), cfx_double(call_frame, 0), &table);
    return CFX_RESULT(type && type[4]);
}
