/*+===================================================================
  File:      carbon_scrap.c

  Summary:   Implements Carbon Scrap Manager access to the initialized flavor list.

  Classes:   None.

  Functions: ROMlib_carbon_scrap.

  Origin:    CarbonLib 1.4 Scrap Manager and Universal Interfaces 3.4.2 Scrap.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "carbon_scrap.h"
#include "cf_batch.h"

enum {
    scrap_slot_mode = -0xbcc,
    scrap_slot_keeper = -0xbc8,
    scrap_slot_keeper_data = -0xbc4,
    scrap_slot_handle = -0xba0,
    scrap_slot_generation = -0xb9c,
    scrap_bad_process_state = -4991,
    scrap_stale = -4990,
    scrap_flags_invalid = -4997,
    scrap_type_invalid = -4998,
    scrap_size_invalid = -4999,
    scrap_data_missing = -4994,
    scrap_flavor_missing = -102,
    scrap_reserved_type = 0x73726674,
    scrap_sender_only = 1,
    scrap_flavor_bytes = 24,
};

static int scrap_result(ROMlib_carbon_ppc_frame* call, uint32_t status)
{
    call->words[0] = status;
    return 1;
}

static int scrap_slot(const ROMlib_carbon_reference* ref, int offset, uint32_t* slot)
{
    return cfp_get(US_TO_SYN68K(ref->toc) + offset, slot);
}

static int scrap_global(const ROMlib_carbon_reference* ref, int offset, uint32_t* value)
{
    uint32_t slot;
    return scrap_slot(ref, offset, &slot) && cfp_get(slot, value);
}

static int scrap_set(const ROMlib_carbon_reference* ref, int offset, uint32_t value)
{
    uint32_t slot;
    return scrap_slot(ref, offset, &slot) && cfp_write(slot, value);
}

static int scrap_generation(const ROMlib_carbon_reference* ref, uint32_t* generation)
{
    uint32_t info;
    if (!cfp_private(ref, 0xb4ea8, NULL, 0, &info) || !info)
        return 0;
    const uint16_t* data = ROMlib_carbon_guest_memory(info + 8, 2, 0);
    if (!data)
        return 0;
    uint16_t value;
    memcpy(&value, data, sizeof value);
    *generation = CW(value);
    return 1;
}

static int scrap_allocate(const ROMlib_carbon_reference* ref, uint32_t* handle, uint32_t* status)
{
    const uint32_t bytes = 4;
    return cfp_private(ref, 0xb2190, &bytes, 1, handle) && scrap_set(ref, scrap_slot_handle, *handle) &&
           (*handle || cfp_private(ref, 0xb5928, NULL, 0, status));
}

static int clear_scrap(const ROMlib_carbon_reference* ref, uint32_t mode, uint32_t handle,
                       ROMlib_carbon_ppc_frame* call)
{
    if (!(mode & 1))
        return scrap_result(call, scrap_bad_process_state);
    uint32_t status = 0;
    if (handle) {
        if (!cfp_private(ref, 0x51bb4, &handle, 1, &status))
            return 0;
        if (status)
            return scrap_result(call, status);
        if (!scrap_set(ref, scrap_slot_handle, 0))
            return 0;
    }
    if (!scrap_allocate(ref, &handle, &status))
        return 0;
    if (!handle)
        return scrap_result(call, status);
    if (!cfp_private(ref, 0xb4ec0, NULL, 0, &status))
        return 0;
    if (status) {
        if (!cfp_private(ref, 0x51bb4, &handle, 1, NULL) || !scrap_set(ref, scrap_slot_handle, 0))
            return 0;
    }
    else if (!scrap_set(ref, scrap_slot_keeper, 0))
        return 0;
    return scrap_result(call, status);
}

/* The saved loader converts a legacy scrap into the common flavor array once.
   Existing entries can be purged by the Memory Manager. */
static int prepare_flavors(const ROMlib_carbon_reference* ref, uint32_t* handle, uint32_t* status)
{
    *status = 0;
    if (!*handle) {
        if (!scrap_allocate(ref, handle, status))
            return 0;
        if (*handle && !cfp_private(ref, 0x51ab8, handle, 1, status))
            return 0;
    }
    if (*handle) {
        if (!*status && (!cfp_private(ref, 0xb58c8, handle, 1, NULL) || !cfp_private(ref, 0xb5928, NULL, 0, status)))
            return 0;
        if (*status)
            return cfp_private(ref, 0x51bb4, handle, 1, NULL) && scrap_set(ref, scrap_slot_handle, 0);
    }
    return 1;
}

static int flavor_table(uint32_t handle, uint32_t* entries, uint32_t* count)
{
    uint32_t table;
    if (!handle || !cfp_get(handle, &table) || !table || !cfp_get(table, count) || table > UINT32_MAX - 4 ||
        *count > (UINT32_MAX - table - 4) / scrap_flavor_bytes)
        return 0;
    *entries = table + 4;
    return 1;
}

static int find_flavor(uint32_t handle, uint32_t type, uint32_t* match)
{
    uint32_t entries, count;
    if (!flavor_table(handle, &entries, &count))
        return 0;
    *match = 0;
    while (count) {
        const uint32_t entry = entries + --count * scrap_flavor_bytes;
        uint32_t candidate;
        if (!cfp_get(entry, &candidate))
            return 0;
        if (candidate == type) {
            *match = entry;
            break;
        }
    }
    return 1;
}

static int put_flavor(const ROMlib_carbon_reference* ref, uint32_t handle, uint32_t mode, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t type = call->words[1], flags = call->words[2], data = call->words[4];
    const int32_t length = call->words[3];
    if (length < -1)
        return scrap_result(call, scrap_size_invalid);
    if (type == scrap_reserved_type)
        return scrap_result(call, scrap_type_invalid);
    if (flags & ~scrap_sender_only)
        return scrap_result(call, scrap_flags_invalid);
    if (length > 0 && !data)
        return scrap_result(call, scrap_data_missing);
    if (!handle)
        return scrap_result(call, noScrapErr);
    uint32_t generation, existing, status;
    if (!scrap_generation(ref, &generation))
        return 0;
    if (call->words[0] != generation)
        return scrap_result(call, scrap_stale);
    if (!(mode & 5))
        return scrap_result(call, scrap_bad_process_state);
    if (length > 0 && !ROMlib_carbon_guest_memory(data, (uint32_t)length, 0))
        return 0;

    _Alignas(32) struct {
        uint32_t type;
        uint32_t flags;
        uint32_t length;
        uint32_t reserved;
        uint32_t data;
        uint16_t attributes;
        uint16_t padding;
    } flavor = {CL(type), CL(flags), CL((uint32_t)length), 0, 0, CW(!data || length == -1 ? 0x4000 : 0x2000), 0};

    if (!find_flavor(handle, type, &existing))
        return 0;
    const uint32_t worker = existing ? 0x502e0 : (flags & scrap_sender_only ? 0x52110 : 0x5201c);
    const uint32_t args[] = {handle, US_TO_SYN68K(&flavor), data};
    return cfp_private(ref, worker, args, 3, &status) && scrap_result(call, status);
}

static int list_flavors(uint32_t handle, uint32_t capacity, uint32_t output, uint32_t count_output)
{
    uint32_t entries, count;
    if (!flavor_table(handle, &entries, &count))
        return 0;
    if (count > capacity)
        count = capacity;
    if (count && (count > (UINT32_MAX - output) / 8 || !ROMlib_carbon_guest_memory(output, count * 8, 1)))
        return 0;
    for (uint32_t i = 0; i < count; ++i)
        if (!cfp_move(output + 8 * i, entries + scrap_flavor_bytes * i, 8))
            return 0;
    return cfp_write(count_output, count);
}

static int flavor_payload(const ROMlib_carbon_reference* ref, uint32_t handle, int operation, uint32_t capacity,
                          ROMlib_carbon_ppc_frame* call)
{
    const uint32_t type = call->words[1], output = call->words[2];
    const uint32_t args[] = {call->words[0], handle, type, output};
    uint32_t status;
    if (!cfp_private(ref, 0x504f8, args, 4, &status))
        return 0;
    if (status || operation == carbon_scrap_size)
        return scrap_result(call, status);
    uint32_t entry, flags;
    if (!find_flavor(handle, type, &entry) || !entry || !cfp_get(entry + 4, &flags))
        return 0;
    if (!(flags & scrap_sender_only)) {
        const uint32_t read_args[] = {type, capacity, call->words[3]};
        return cfp_private(ref, 0x521cc, read_args, 3, &status) && scrap_result(call, status);
    }
    uint32_t size, payload_handle, payload;
    if (!cfp_get(output, &size) || (int32_t)size < 0)
        return 0;
    const uint32_t bytes = size < capacity ? size : capacity;
    if (bytes && (!cfp_get(entry + 16, &payload_handle) || !cfp_get(payload_handle, &payload) ||
                  !cfp_move(call->words[3], payload, bytes)))
        return 0;
    return scrap_result(call, noErr);
}

static int read_flavors(const ROMlib_carbon_reference* ref, uint32_t handle, uint32_t mode, int operation,
                        ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    const uint32_t output = operation == carbon_scrap_count || operation == carbon_scrap_list ? args[1] : args[2];
    const uint32_t type = args[1];
    uint32_t capacity = 0, generation, status;
    if (!output || (operation == carbon_scrap_list && !args[2]) || (operation == carbon_scrap_data && !args[3]))
        return scrap_result(call, paramErr);
    if ((operation == carbon_scrap_list || operation == carbon_scrap_data) && !cfp_get(output, &capacity))
        return 0;
    if (operation == carbon_scrap_list && !capacity)
        return scrap_result(call, noErr);
    if (operation == carbon_scrap_data && (int32_t)capacity < 0)
        return scrap_result(call, paramErr);
    if (operation >= carbon_scrap_flags && type == scrap_reserved_type)
        return scrap_result(call, scrap_type_invalid);
    if (!ROMlib_carbon_guest_memory(output, 4, 1) ||
        (operation == carbon_scrap_data && capacity && !ROMlib_carbon_guest_memory(args[3], capacity, 1)) ||
        !scrap_generation(ref, &generation))
        return 0;
    if (args[0] != generation)
        return scrap_result(call, scrap_stale);
    if (!(mode & ~2u))
        return scrap_result(call, scrap_bad_process_state);
    if (!prepare_flavors(ref, &handle, &status))
        return 0;
    if (status)
        return scrap_result(call, status);
    if (operation == carbon_scrap_count) {
        uint32_t entries, count;
        return flavor_table(handle, &entries, &count) && cfp_write(output, count) && scrap_result(call, noErr);
    }
    if (operation == carbon_scrap_list)
        return list_flavors(handle, capacity, args[2], output) && scrap_result(call, noErr);
    if (operation == carbon_scrap_flags) {
        uint32_t entry, flags;
        if (!find_flavor(handle, type, &entry))
            return 0;
        if (!entry)
            return scrap_result(call, scrap_flavor_missing);
        return cfp_get(entry + 4, &flags) && cfp_write(output, flags) && scrap_result(call, noErr);
    }
    return flavor_payload(ref, handle, operation, capacity, call);
}

int ROMlib_carbon_scrap(const char* name, uint32_t offset, int operation, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", name, &ref))
        return 0;
    if (ref.code_offset != offset || ref.code_size != 0xb8fb4 || !ref.toc || ref.toc_offset != 0x2f78 ||
        ref.toc_size != 0x10f0c)
        return ROMlib_carbon_invoke_frame(ref.function, call);

    uint32_t mode, handle, generation, status;
    if (!scrap_global(&ref, scrap_slot_mode, &mode) || !scrap_global(&ref, scrap_slot_handle, &handle))
        return 0;
    switch (operation) {
    case carbon_scrap_get:
        if (!call->words[0])
            return scrap_result(call, paramErr);
        return ((mode & 5) ? scrap_generation(&ref, &generation)
                           : scrap_global(&ref, scrap_slot_generation, &generation)) &&
               cfp_write(call->words[0], generation) && scrap_result(call, noErr);
    case carbon_scrap_clear:
        return clear_scrap(&ref, mode, handle, call);
    case carbon_scrap_put:
        return put_flavor(&ref, handle, mode, call);
    case carbon_scrap_promises:
        if (!(mode & 5))
            return scrap_result(call, scrap_bad_process_state);
        if (!handle)
            return scrap_result(call, noErr);
        return cfp_private(&ref, 0x51cb4, &handle, 1, &status) && scrap_result(call, status);
    case carbon_scrap_keeper:
        if (!scrap_generation(&ref, &generation))
            return 0;
        if (call->words[0] != generation)
            return scrap_result(call, scrap_stale);
        if (!(mode & 255))
            return scrap_result(call, scrap_bad_process_state);
        return scrap_set(&ref, scrap_slot_keeper, call->words[1]) &&
               scrap_set(&ref, scrap_slot_keeper_data, call->words[2]) && scrap_result(call, noErr);
    default:
        return read_flavors(&ref, handle, mode, operation, call);
    }
}
