/*+===================================================================
  File:      cfx_timezones.c

  Summary:   Implements CoreFoundation fixed-offset and transition-table time zones.

  Classes:   None.

  Functions: ROMlib_cfx_CFTimeZoneCopyAbbreviation, ROMlib_cfx_CFTimeZoneCopyDefault,
             ROMlib_cfx_CFTimeZoneCopySystem, ROMlib_cfx_CFTimeZoneCreate,
             ROMlib_cfx_CFTimeZoneCreateWithTimeIntervalFromGMT, ROMlib_cfx_CFTimeZoneGetData,
             ROMlib_cfx_CFTimeZoneGetName, ROMlib_cfx_CFTimeZoneGetSecondsFromGMT,
             ROMlib_cfx_CFTimeZoneGetTypeID, ROMlib_cfx_CFTimeZoneIsDaylightSavingTime,
             ROMlib_cfx_CFTimeZoneResetSystem, ROMlib_cfx_CFTimeZoneSetDefault,
             ROMlib_cfx_CFTimeZoneCopyAbbreviationDictionary, ROMlib_cfx_CFTimeZoneCopyKnownNames,
             ROMlib_cfx_CFTimeZoneCreateWithName, ROMlib_cfx_CFTimeZoneSetAbbreviationDictionary

  Origin:    Universal Interfaces 3.4.2, local CoreFoundationLib 1.4 runtime layout and TZif format.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "cfx_internal.h"

#include "rsys/osutil.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include "timezone_icu.h"

typedef struct {
    uint32 version, class_name, initialize, copy, finalize, equal, hash, format, describe;
    uint32 type, system, preferred, abbreviations, known_names;
    char name[16];
} timezone_storage;

typedef struct {
    const Byte *transitions, *indices, *types, *abbreviations;
    uint32 time_count, type_count, abbreviation_count, width;
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

static int64 zone_transition(const Byte* bytes, unsigned width)
{
    if (width == 4)
        return (int32)zone_read_u32(bytes);
    return (int64)(((uint64)zone_read_u32(bytes) << 32) | zone_read_u32(bytes + 4));
}

static size_t zone_read_block(const Byte* bytes, size_t length, unsigned width, timezone_table* table)
{
    if (length < tz_header_size || memcmp(bytes, "TZif", 4))
        return 0;
    const uint32 utc_count = zone_read_u32(bytes + 20), wall_count = zone_read_u32(bytes + 24);
    const uint32 leap_count = zone_read_u32(bytes + 28), time_count = zone_read_u32(bytes + 32);
    const uint32 type_count = zone_read_u32(bytes + 36), abbreviation_count = zone_read_u32(bytes + 40);
    const uint64 extent = tz_header_size + (width + 1) * (uint64)time_count + tz_type_size * (uint64)type_count +
                          abbreviation_count + (width + 4) * (uint64)leap_count + utc_count + wall_count;
    if (extent > length || !type_count || type_count > 256 || !abbreviation_count ||
        (utc_count && utc_count != type_count) || (wall_count && wall_count != type_count))
        return 0;

    const Byte* transitions = bytes + tz_header_size;
    const Byte* indices = transitions + width * (size_t)time_count;
    const Byte* types = indices + time_count;
    const Byte* abbreviations = types + tz_type_size * type_count;
    *table =
        (timezone_table){transitions, indices, types, abbreviations, time_count, type_count, abbreviation_count, width};
    return extent;
}

static int zone_read_table(uint32 data, timezone_table* table)
{
    const void* object = SYN68K_TO_US_CHECK0(data);
    const LONGINT length = ROMlib_carbon_cf_data_length(object);
    const Byte* bytes = ROMlib_carbon_cf_data_bytes(object);
    if (!bytes || length < tz_header_size)
        return 0;
    const size_t first = zone_read_block(bytes, length, 4, table);
    if (!first)
        return 0;
    if (bytes[4] == '2' || bytes[4] == '3' || bytes[4] == '4')
        return zone_read_block(bytes + first, length - first, 8, table) != 0;
    return bytes[4] == 0;
}

static int zone_validate_table(const timezone_table* table)
{
    int64 previous = INT64_MIN;
    for (uint32 index = 0; index < table->time_count; ++index) {
        const int64 transition = zone_transition(table->transitions + table->width * (size_t)index, table->width);
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
        const double transition =
            zone_transition(table->transitions + table->width * (size_t)middle, table->width) - 978307200.0;
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

enum {
    icu_calendar_gregorian = 1,
    icu_raw_offset = 15,
    icu_dst_offset = 16,
    icu_next_transition = 0,
    icu_short_standard = 1,
    icu_short_dst = 3,
    zone_max_transitions = 2048,
};

typedef struct {
    int64 dates[zone_max_transitions];
    Byte indices[zone_max_transitions];
    Byte types[256][tz_type_size];
    char abbreviations[256];
    unsigned count, type_count, abbreviation_count;
} named_zone_table;

static int icu_period(const zone_icu* api, void* calendar, double millis, named_zone_table* table)
{
    int32 status = 0;
    api->set_date(calendar, millis, &status);
    int32 offset = api->field(calendar, icu_raw_offset, &status);
    int32 dst = api->field(calendar, icu_dst_offset, &status);
    if (status > 0)
        return -1;
    const int32 seconds = ((int64)offset + dst) / 1000;
    uint16 label[24];
    int32 length =
        api->display_name(calendar, dst ? icu_short_dst : icu_short_standard, "en_US", label, NELEM(label), &status);
    char name[NELEM(label)];
    bool ascii = status <= 0 && length > 0 && length < (int32)NELEM(label);
    for (int32 i = 0; ascii && i < length; ++i) {
        ascii = label[i] && label[i] <= 127;
        name[i] = (char)label[i];
    }
    if (ascii)
        name[length] = 0;
    else {
        const unsigned distance = seconds < 0 ? -seconds : seconds;
        length = snprintf(name, sizeof name, "GMT%c%02u:%02u", seconds < 0 ? '-' : '+', distance / 3600,
                          distance % 3600 / 60);
        if (length < 0 || length >= (int32)sizeof name)
            return -1;
    }
    unsigned abbreviation = 0;
    while (abbreviation < table->abbreviation_count && strcmp(table->abbreviations + abbreviation, name))
        abbreviation += strlen(table->abbreviations + abbreviation) + 1;
    if (abbreviation == table->abbreviation_count) {
        if (abbreviation + length + 1 > sizeof table->abbreviations)
            return -1;
        memcpy(table->abbreviations + abbreviation, name, length + 1);
        table->abbreviation_count += length + 1;
    }
    Byte type[tz_type_size];
    zone_write_u32(type, seconds);
    type[4] = dst != 0;
    type[5] = abbreviation;
    unsigned index = 0;
    while (index < table->type_count && memcmp(table->types[index], type, sizeof type))
        ++index;
    if (index == table->type_count) {
        if (index == NELEM(table->types))
            return -1;
        memcpy(table->types[index], type, sizeof type);
        ++table->type_count;
    }
    return index;
}

static int icu_transitions(const zone_icu* api, void* calendar, named_zone_table* table)
{
    /* Use the provider's rules through 2500 and preserve times past 2038
       in the second, 64-bit TZif block. The last type holds thereafter. */
    double date = -5364662400000.0; /* 1800-01-01 */
    if (icu_period(api, calendar, date, table) < 0)
        return 0;
    int32 status = 0;
    double next;
    while (api->next_transition(calendar, icu_next_transition, &next, &status) && status <= 0 &&
           next < 16725225600000.0) {
        if (next <= date || table->count == NELEM(table->dates))
            return 0;
        const int type = icu_period(api, calendar, next, table);
        if (type < 0)
            return 0;
        date = next;
        table->dates[table->count] = (int64)(date / 1000);
        table->indices[table->count++] = type;
    }
    return status <= 0;
}

static size_t zone_block_size(const named_zone_table* table, unsigned width, unsigned count)
{
    return tz_header_size + (width + 1) * count + tz_type_size * table->type_count + table->abbreviation_count;
}

static void zone_write_block(Byte* bytes, const named_zone_table* table, unsigned width, unsigned start, unsigned count)
{
    memcpy(bytes, "TZif2", 5);
    zone_write_u32(bytes + 32, count);
    zone_write_u32(bytes + 36, table->type_count);
    zone_write_u32(bytes + 40, table->abbreviation_count);
    bytes += tz_header_size;
    for (unsigned i = 0; i < count; ++i) {
        const uint64 date = table->dates[start + i];
        if (width == 8)
            zone_write_u32(bytes + width * i, date >> 32);
        zone_write_u32(bytes + width * i + width - 4, date);
    }
    bytes += width * count;
    memcpy(bytes, table->indices + start, count);
    bytes += count;
    memcpy(bytes, table->types, tz_type_size * table->type_count);
    memcpy(bytes + tz_type_size * table->type_count, table->abbreviations, table->abbreviation_count);
}

static uint32 zone_named_data(uint32 allocator, const uint16* name, int32 length)
{
    const zone_icu* api = ROMlib_carbon_timezone_icu();
    if (!api)
        return 0;
    int32 status = 0;
    uint16 canonical[128];
    int8 system_id = 0;
    const int32 canonical_length = api->canonical(name, length, canonical, NELEM(canonical), &system_id, &status);
    if (status > 0 || canonical_length <= 0 || canonical_length >= (int32)NELEM(canonical))
        return 0;
    if (!system_id && (canonical_length < 3 || canonical[0] != 'G' || canonical[1] != 'M' || canonical[2] != 'T'))
        return 0;
    void* calendar = api->open(canonical, canonical_length, "en_US", icu_calendar_gregorian, &status);
    if (!calendar)
        return 0;
    named_zone_table* table = calloc(1, sizeof *table);
    const int valid = status <= 0 && table && icu_transitions(api, calendar, table);
    api->close(calendar);
    if (!valid) {
        free(table);
        return 0;
    }

    unsigned start = 0, end = 0;
    while (start < table->count && table->dates[start] < INT32_MIN)
        ++start;
    end = start;
    while (end < table->count && table->dates[end] <= INT32_MAX)
        ++end;
    const size_t first = zone_block_size(table, 4, end - start);
    const size_t second = zone_block_size(table, 8, table->count);
    const size_t size = first + second + 2;
    Ptr bytes = NewPtrClear(size);
    if (bytes) {
        zone_write_block((Byte*)bytes, table, 4, start, end - start);
        zone_write_block((Byte*)bytes + first, table, 8, 0, table->count);
        bytes[first + second] = bytes[first + second + 1] = '\n';
    }
    free(table);
    const uint32 data = bytes ? ROMlib_cfdata_create(allocator, US_TO_SYN68K(bytes), size) : 0;
    if (bytes)
        DisposPtr(bytes);
    return data;
}

static uint32 zone_text(const char* ascii)
{
    /* Host DLL and compiler string storage need not be guest-addressable. */
    _Alignas(128) char buffer[128];
    const size_t length = strlen(ascii);
    if (length >= sizeof buffer)
        return 0;
    memcpy(buffer, ascii, length + 1);
    return ROMlib_cfstring_cstring(0, US_TO_SYN68K(buffer), 0x600);
}

static uint32 zone_default_abbreviations(const ROMlib_carbon_reference* reference)
{
    static const struct {
        const char *abbreviation, *zone;
    } mappings[] = {
        {"GMT", "GMT"},
        {"UTC", "UTC"},
        {"PST", "America/Los_Angeles"},
        {"PDT", "America/Los_Angeles"},
        {"MST", "America/Denver"},
        {"MDT", "America/Denver"},
        {"CST", "America/Chicago"},
        {"CDT", "America/Chicago"},
        {"EST", "America/New_York"},
        {"EDT", "America/New_York"},
        {"AST", "America/Halifax"},
        {"ADT", "America/Halifax"},
        {"HST", "Pacific/Honolulu"},
        {"AKST", "America/Anchorage"},
        {"AKDT", "America/Anchorage"},
        {"NST", "America/St_Johns"},
        {"NDT", "America/St_Johns"},
        {"BST", "Europe/London"},
        {"WET", "Europe/Lisbon"},
        {"WEST", "Europe/Lisbon"},
        {"CET", "Europe/Paris"},
        {"CEST", "Europe/Paris"},
        {"EET", "Europe/Athens"},
        {"EEST", "Europe/Athens"},
        {"MSK", "Europe/Moscow"},
        {"IST", "Asia/Kolkata"},
        {"JST", "Asia/Tokyo"},
        {"KST", "Asia/Seoul"},
        {"HKT", "Asia/Hong_Kong"},
        {"SGT", "Asia/Singapore"},
        {"ICT", "Asia/Bangkok"},
        {"AEST", "Australia/Sydney"},
        {"AEDT", "Australia/Sydney"},
        {"ACST", "Australia/Adelaide"},
        {"ACDT", "Australia/Adelaide"},
        {"AWST", "Australia/Perth"},
        {"NZST", "Pacific/Auckland"},
        {"NZDT", "Pacific/Auckland"},
        {"SAST", "Africa/Johannesburg"},
        {"BRT", "America/Sao_Paulo"},
    };

    const uint32 toc = US_TO_SYN68K(reference->toc);
    const uint32 mutable = ROMlib_cfc_CFDictionaryCreateMutable(0, NELEM(mappings), toc + 0x33d8, toc + 0x33c0);
    if (!mutable)
        return 0;
    bool complete = true;
    for (unsigned i = 0; i < NELEM(mappings) && complete; ++i) {
        const uint32 key = zone_text(mappings[i].abbreviation);
        const uint32 value = zone_text(mappings[i].zone);
        if (key && value)
            ROMlib_cfc_CFDictionarySetValue(mutable, key, value);
        else
            complete = false;
        zone_release(key);
        zone_release(value);
    }
    complete = complete && ROMlib_cfc_CFDictionaryGetCount(mutable) == NELEM(mappings);
    const uint32 result = complete ? ROMlib_cfc_CFDictionaryCreateCopy(0, mutable) : 0;
    zone_release(mutable);
    return result;
}

static uint32 zone_abbreviations(timezone_storage* state, const ROMlib_carbon_reference* reference)
{
    if (!state->abbreviations)
        state->abbreviations = CL(zone_default_abbreviations(reference));
    return CL(state->abbreviations);
}

int ROMlib_cfx_CFTimeZoneCopyAbbreviationDictionary(ROMlib_carbon_ppc_frame* call_frame)
{
    ZONE_STATE(CFTimeZoneCopyAbbreviationDictionary, 0x2578c);
    return CFX_RESULT(state ? zone_retain(zone_abbreviations(state, &reference)) : 0);
}

int ROMlib_cfx_CFTimeZoneSetAbbreviationDictionary(ROMlib_carbon_ppc_frame* call_frame)
{
    ZONE_STATE(CFTimeZoneSetAbbreviationDictionary, 0x25794);
    const uint32 supplied = CFX_ARG(0);
    if (!state || (supplied && zone_object_type(supplied) != ROMlib_cfc_CFDictionaryGetTypeID()))
        return 1;
    const uint32 replacement = supplied ? ROMlib_cfc_CFDictionaryCreateCopy(0, supplied) : 0;
    if (supplied && !replacement)
        return 1;
    const uint32 previous = CL(state->abbreviations);
    state->abbreviations = CL(replacement);
    zone_release(previous);
    return 1;
}

int ROMlib_cfx_CFTimeZoneCopyKnownNames(ROMlib_carbon_ppc_frame* call_frame)
{
    ZONE_STATE(CFTimeZoneCopyKnownNames, 0x25784);
    if (!state)
        return CFX_RESULT(0);
    if (state->known_names)
        return CFX_RESULT(zone_retain(CL(state->known_names)));
    const zone_icu* api = ROMlib_carbon_timezone_icu();
    int32 status = 0;
    void* names = api ? api->open_names(&status) : NULL;
    if (!names || status > 0) {
        if (names)
            api->close_names(names);
        return ROMlib_carbon_invoke_frame(reference.function, call_frame);
    }
    const uint32 array = ROMlib_cfarray_create_mutable(0, 0, US_TO_SYN68K(reference.toc) + 0x3234);
    const char* name;
    int32 length;
    unsigned count = 0;
    bool complete = array != 0;
    while (complete && (name = api->next_name(names, &length, &status)) && status <= 0) {
        const uint32 string = zone_text(name);
        if (!string) {
            complete = false;
            break;
        }
        ROMlib_cfarray_append(array, string);
        zone_release(string);
        ++count;
    }
    api->close_names(names);
    complete = complete && status <= 0 && count && CFW_CALL("CFArrayGetCount", array) == count;
    if (complete)
        state->known_names = CL(ROMlib_cfarray_create_copy(0, array));
    zone_release(array);
    return CFX_RESULT(zone_retain(CL(state->known_names)));
}

int ROMlib_cfx_CFTimeZoneCreateWithName(ROMlib_carbon_ppc_frame* call_frame)
{
    ZONE_STATE(CFTimeZoneCreateWithName, 0x257a8);
    uint32 name = CFX_ARG(1);
    const uint32 allocator = CFX_ARG(0);
    if (!state || zone_object_type(name) != ROMlib_cfvalue_string_type())
        return CFX_RESULT(0);
    if ((Byte)CFX_ARG(2)) {
        const uint32 dictionary = zone_abbreviations(state, &reference);
        const uint32 mapped = dictionary ? ROMlib_cfc_CFDictionaryGetValue(dictionary, name) : 0;
        if (mapped && zone_object_type(mapped) == ROMlib_cfvalue_string_type())
            name = mapped;
    }
    void* string = SYN68K_TO_US_CHECK0(name);
    const LONGINT length = ROMlib_carbon_cf_string_length(string);
    uint16 id[128];
    if (length <= 0 || length >= (LONGINT)NELEM(id))
        return CFX_RESULT(0);
    for (LONGINT i = 0; i < length; ++i) {
        const uint32 character = ROMlib_carbon_cf_string_character(string, i);
        if (!character || character > 127)
            return CFX_RESULT(0);
        id[i] = character;
    }
    const uint32 data = zone_named_data(allocator, id, length);
    if (!data) {
        if (length == 3 && id[0] == 'G' && id[1] == 'M' && id[2] == 'T')
            return CFX_RESULT(zone_fixed(state, allocator, 0));
        return ROMlib_carbon_invoke_frame(reference.function, call_frame);
    }
    const uint32 result = zone_create(state, allocator, name, data);
    zone_release(data);
    return CFX_RESULT(result);
}
