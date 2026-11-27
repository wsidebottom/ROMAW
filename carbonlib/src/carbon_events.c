/*+===================================================================
  File:      carbon_events.c

  Summary:   Implements Carbon event objects and access to shared event-manager state.

  Classes:   None.

  Functions: ROMlib_carbon_event_access.

  Origin:    CarbonLib 1.4 disassembly and Universal Interfaces 3.4.2 CarbonEvents.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "carbon_events.h"
#include "cf_batch.h"
#include "cfx_internal.h"
#include "EventMgr.h"

#include <math.h>

enum {
    event_class_field = 0x0c,
    event_kind_field = 0x10,
    event_attributes_field = 0x14,
    event_time_field = 0x18,
    event_priority_field = 0x20,
    event_retains_field = 0x24,
    event_classic_field = 0x30,
    event_parameters_field = 0x44,
    event_allocator_field = 0x48,
    event_lock_field = 0x4c,
    event_size = 0x50,
    event_parameter_not_found = -9870,
    event_loop_timed_out = -9875,
    window_invalid = -5600,
    window_not_visible = -5615,
    window_wrong_state = -5614,
    window_property_not_found = -5604,
    window_already_modal = 10000,
    modal_state_property = 0x61706d64, /* 'apmd' */
    modality_property = 0x6d6f6465,    /* 'mode' */
    modality_none = 0,
    modality_system = 1,
    modality_window = 3,
};

static int result_word(ROMlib_carbon_ppc_frame* call, uint32_t value)
{
    call->words[0] = value;
    return 1;
}

static int toc_pointer(const ROMlib_carbon_reference* ref, int field, uint32_t* value)
{
    return cfp_get(US_TO_SYN68K(ref->toc) + field, value);
}

static int global_word(const ROMlib_carbon_reference* ref, int field, uint32_t* value)
{
    uint32_t address;
    return toc_pointer(ref, field, &address) && cfp_get(address, value);
}

static int toc_float(const ROMlib_carbon_reference* ref, int slot, uint32_t offset, float* value)
{
    uint32_t address, bits;
    if (!toc_pointer(ref, slot, &address) || !cfp_get(address + offset, &bits))
        return 0;
    memcpy(value, &bits, sizeof bits);
    return 1;
}

static int event_key(const ROMlib_carbon_reference* ref, uint32_t* key)
{
    uint32_t initialized;
    if (!global_word(ref, -0xf74, &initialized))
        return 0;
    return initialized ? global_word(ref, -0xf70, key) : cfp_private(ref, 0x3381c, NULL, 0, key);
}

static int unmask_event(const ROMlib_carbon_reference* ref, uint32_t event, uint32_t* object, uint32_t* key)
{
    *object = 0;
    if (!event)
        return 1;
    if (!event_key(ref, key))
        return 0;
    *object = event ^ *key;
    return 1;
}

static uint32_t load_word(const Byte* object, size_t offset)
{
    uint32_t word;
    memcpy(&word, object + offset, sizeof word);
    return CL(word);
}

static void store_word(Byte* object, size_t offset, uint32_t word)
{
    word = CL(word);
    memcpy(object + offset, &word, sizeof word);
}

static uint64_t load_double_bits(const Byte* object, size_t offset)
{
    return ((uint64_t)load_word(object, offset) << 32) | load_word(object, offset + 4);
}

static void store_double_bits(Byte* object, size_t offset, uint64_t bits)
{
    store_word(object, offset, bits >> 32);
    store_word(object, offset + 4, (uint32_t)bits);
}

static int read_double_bits(uint32_t address, uint64_t* result)
{
    const Byte* data = ROMlib_carbon_guest_memory(address, 8, 0);
    if (!data)
        return 0;
    *result = load_double_bits(data, 0);
    return 1;
}

static int read_event_clock(const ROMlib_carbon_reference* ref, uint64_t* bits)
{
    _Alignas(16) uint32_t uptime[2], nanoseconds[2];
    const uint32_t uptime_address = US_TO_SYN68K(uptime);
    if (!cfp_private(ref, 0xb5970, &uptime_address, 1, NULL))
        return 0;
    const uint32_t arguments[] = {US_TO_SYN68K(nanoseconds), CL(uptime[0]), CL(uptime[1])};
    if (!cfp_private(ref, 0xb65a0, arguments, 3, NULL))
        return 0;
    const uint64_t elapsed = ((uint64_t)CL(nanoseconds[0]) << 32) | CL(nanoseconds[1]);
    const double seconds = (double)elapsed * 1.0e-9;
    memcpy(bits, &seconds, sizeof seconds);
    return 1;
}

static int allocate_event(uint32_t allocator, uint32_t* object)
{
    if (!cfp_call("CFRetain", &allocator, 1, NULL))
        return 0;
    const uint32_t arguments[] = {allocator, event_size, 0};
    if (!cfp_call("CFAllocatorAllocate", arguments, 3, object))
        return 0;
    return *object || cfp_call("CFRelease", &allocator, 1, NULL);
}

static int dispose_event(const ROMlib_carbon_reference* ref, uint32_t address, Byte* object)
{
    const uint32_t allocator = load_word(object, event_allocator_field);
    const uint32_t parameters[] = {allocator, address + event_parameters_field};
    const uint32_t storage[] = {allocator, address};
    return (!load_word(object, event_parameters_field) || cfp_private(ref, 0xb4da0, parameters, 2, NULL)) &&
           cfp_call("CFAllocatorDeallocate", storage, 2, NULL) && cfp_call("CFRelease", &allocator, 1, NULL);
}

static int create_event(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    uint32_t allocator = call->words[0];
    const uint32_t output = call->words[6], attributes = call->words[5];
    if (!output)
        return result_word(call, paramErr);
    if (!cfp_write(output, 0))
        return 0;
    if (attributes & ~7u)
        return result_word(call, paramErr);

    uint32_t key;
    uint64_t time = call->floating[0];
    if (!event_key(ref, &key) || (!(time & UINT64_C(0x7fffffffffffffff)) && !read_event_clock(ref, &time)) ||
        (!allocator && !cfp_private(ref, 0x494c4, NULL, 0, &allocator)))
        return 0;
    uint32_t address;
    if (!allocate_event(allocator, &address))
        return 0;
    if (!address)
        return result_word(call, memFullErr);
    Byte* object = ROMlib_carbon_guest_memory(address, event_size, 1);
    if (!object)
        return 0;
    memset(object, 0, event_size);
    store_word(object, event_class_field, call->words[1]);
    store_word(object, event_kind_field, call->words[2]);
    store_word(object, event_attributes_field, attributes);
    store_double_bits(object, event_time_field, time);
    store_word(object, event_retains_field, 1);
    store_word(object, event_priority_field, 1);
    store_word(object, event_allocator_field, allocator);
    return cfp_write(output, address ^ key) && result_word(call, noErr);
}

static int copy_event(const ROMlib_carbon_reference* ref, uint32_t key, const Byte* source,
                      ROMlib_carbon_ppc_frame* call)
{
    /* A custom allocator may run guest callbacks. Snapshot the source before
       calling it, while the caller still owns the parameter-list reference. */
    Byte snapshot[event_size];
    memcpy(snapshot, source, sizeof snapshot);
    const uint32_t allocator = load_word(snapshot, event_allocator_field);
    uint32_t address;
    if (!allocate_event(allocator, &address))
        return 0;
    if (!address)
        return result_word(call, 0);
    Byte* destination = ROMlib_carbon_guest_memory(address, event_size, 1);
    if (!destination)
        return 0;
    memset(destination, 0, event_size);
    memcpy(destination + event_class_field, snapshot + event_class_field, 0x20);
    store_word(destination, event_retains_field, 1);
    destination[event_classic_field] = snapshot[event_classic_field];
    memcpy(destination + 0x32, snapshot + 0x32, 16);
    store_word(destination, event_allocator_field, allocator);

    const uint32_t parameters[] = {allocator, load_word(snapshot, event_parameters_field),
                                   address + event_parameters_field};
    if (parameters[1]) {
        uint32_t status;
        if (!cfp_private(ref, 0xb4d88, parameters, 3, &status))
            return 0;
        if (status)
            return dispose_event(ref, address, destination) && result_word(call, 0);
    }
    return result_word(call, address ^ key);
}

static int event_lifetime(const ROMlib_carbon_reference* ref, uint32_t address, Byte* object,
                          ROMlib_carbon_ppc_frame* call, int retaining)
{
    volatile uint32_t* lock = (volatile uint32_t*)(object + event_lock_field);
    /* The reference takes the same in-record CompareAndSwap lock. Stop if a
       malformed record or callback leaves it permanently held. */
    unsigned retries = 0;
    while (!ROMlib_carbon_compare_and_swap(0, 1, lock))
        if (++retries == 1024)
            return ROMlib_carbon_report_failure("Carbon event reference count is locked");

    const uint32_t owners = load_word(object, event_retains_field);
    if (retaining) {
        if (owners && owners != UINT32_MAX)
            store_word(object, event_retains_field, owners + 1);
    }
    else if (owners) {
        store_word(object, event_retains_field, owners - 1);
        if (owners == 1)
            return dispose_event(ref, address, object);
    }
    ROMlib_carbon_compare_and_swap(1, 0, lock);
    if (retaining && (!owners || owners == UINT32_MAX))
        call->words[0] = 0;
    return 1;
}

static int set_parameter(const ROMlib_carbon_reference* ref, uint32_t address, const Byte* object,
                         ROMlib_carbon_ppc_frame* call)
{
    const uint32_t data = call->words[4], size = call->words[3];
    if (!data)
        return result_word(call, paramErr);
    if (size && !ROMlib_carbon_guest_memory(data, size, 0))
        return 0;
    _Alignas(8) uint32_t status = 0, removed = 0;
    const uint32_t allocator = load_word(object, event_allocator_field), slot = address + event_parameters_field;
    const uint32_t arguments[] = {allocator, slot, call->words[1], call->words[2], data, size, US_TO_SYN68K(&status)};
    if (!cfp_private(ref, 0xb4d58, arguments, 7, NULL))
        return 0;
    if ((int32_t)CL(status) == -960) {
        const uint32_t remove_arguments[] = {allocator, slot, call->words[1], US_TO_SYN68K(&removed), 0, 0};
        if (!cfp_private(ref, 0xb4d70, remove_arguments, 6, NULL) || !cfp_private(ref, 0xb4d58, arguments, 7, NULL))
            return 0;
    }
    return result_word(call, CL(status));
}

static int get_parameter(const ROMlib_carbon_reference* ref, const Byte* object, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t wanted = call->words[2], type_output = call->words[3], capacity = call->words[4];
    const uint32_t size_output = call->words[5], output = call->words[6], wildcard = UINT32_C(0x2a2a2a2a);
    if ((!type_output && !size_output && !output) || (output && (!capacity || wanted == wildcard)))
        return result_word(call, paramErr);
    const uint32_t lookup[] = {load_word(object, event_parameters_field), call->words[1]};
    uint32_t parameter;
    if (!cfp_private(ref, 0xb4d40, lookup, 2, &parameter))
        return 0;
    if (!parameter)
        return result_word(call, event_parameter_not_found);
    uint32_t type, size;
    if (!cfp_get(parameter + 0x12, &type) || !cfp_get(parameter + 0x16, &size))
        return 0;
    if (wanted == wildcard || wanted == type)
        return (!type_output || cfp_write(type_output, type)) && (!size_output || cfp_write(size_output, size)) &&
               (!output || cfp_move(output, parameter + 0x1a, size < capacity ? size : capacity)) &&
               result_word(call, noErr);

    _Alignas(16) uint32_t descriptor[2] = {CL(UINT32_C(0x6e756c6c)), 0};
    const uint32_t desc = US_TO_SYN68K(descriptor);
    const uint32_t coerce[] = {type, parameter + 0x1a, size, wanted, desc};
    uint32_t status;
    if (!cfp_private(ref, 0xb6588, coerce, 5, &status))
        return 0;
    if (status)
        return result_word(call, status);
    int ok = !type_output || cfp_write(type_output, CL(descriptor[0]));
    if (ok && size_output)
        ok = cfp_private(ref, 0xb1d58, &desc, 1, &size) && cfp_write(size_output, size);
    if (ok && output) {
        const uint32_t copy[] = {desc, output, capacity};
        ok = ROMlib_carbon_guest_memory(output, capacity, 1) && cfp_private(ref, 0xb1d40, copy, 3, &status);
    }
    const int disposed = cfp_private(ref, 0xb5af0, &desc, 1, NULL);
    return disposed && ok && result_word(call, status);
}

static int event_record_access(const ROMlib_carbon_reference* ref, enum carbon_event_operation operation, int field,
                               ROMlib_carbon_ppc_frame* call)
{
    uint32_t address, key = 0;
    if (!unmask_event(ref, call->words[0], &address, &key))
        return 0;
    if (!address) {
        if (operation == event_read_time) {
            uint32_t constants;
            return toc_pointer(ref, -0x18c, &constants) && read_double_bits(constants + 8, call->floating);
        }
        return result_word(call, operation == event_set_parameter || operation == event_get_parameter ||
                                         operation == event_write_time
                                     ? paramErr
                                     : 0);
    }
    Byte* object = ROMlib_carbon_guest_memory(address, event_size,
                                              operation == event_retain || operation == event_release ||
                                                  operation == event_write_time || operation == event_set_parameter);
    if (!object)
        return 0;
    switch (operation) {
    case event_copy:
        return copy_event(ref, key, object, call);
    case event_read_word:
        return result_word(call, load_word(object, field));
    case event_read_time:
        call->floating[0] = load_double_bits(object, event_time_field);
        return 1;
    case event_write_time:
        store_double_bits(object, event_time_field, call->floating[0]);
        return result_word(call, noErr);
    case event_retain:
    case event_release:
        return event_lifetime(ref, address, object, call, operation == event_retain);
    case event_set_parameter:
        return set_parameter(ref, address, object, call);
    case event_get_parameter:
        return get_parameter(ref, object, call);
    default:
        return 0;
    }
}

static int read_event_parameter(const ROMlib_carbon_reference* ref, const Byte* event, uint32_t name, uint32_t type,
                                void* output, uint32_t size, uint32_t* status)
{
    ROMlib_carbon_ppc_frame query = {.words = {0, name, type, 0, size, 0, US_TO_SYN68K(output)}};
    if (!get_parameter(ref, event, &query))
        return 0;
    *status = query.words[0];
    return 1;
}

static uint16_t classic_event_mask(uint32_t event_class, uint32_t kind)
{
    switch (event_class) {
    case T('m', 'o', 'u', 's'):
        switch (kind) {
        case 1:
            return mDownMask;
        case 2:
            return mUpMask;
        case 5:
        case 6:
            return (uint16_t)app4Mask;
        }
        break;
    case T('k', 'e', 'y', 'b'):
        switch (kind) {
        case 1:
            return keyDownMask;
        case 2:
            return autoKeyMask;
        case 3:
            return keyUpMask;
        }
        break;
    case T('w', 'i', 'n', 'd'):
        switch (kind) {
        case 1:
            return updateMask;
        case 5:
        case 6:
            return activMask;
        case 40:
            return (uint16_t)app4Mask;
        }
        break;
    case T('a', 'p', 'p', 'l'):
        return kind == 1 || kind == 2 ? (uint16_t)app4Mask : 0;
    case T('e', 'p', 'p', 'c'):
        return kind == 1 ? highLevelEventMask : 0;
    case T('v', 'o', 'l', ' '):
        return kind == TICK("disk") ? diskMask : 0;
    }
    return 0;
}

static int is_event_in_classic_mask(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    uint32_t address, key;
    if (!unmask_event(ref, call->words[0], &address, &key))
        return 0;
    if (!address)
        return result_word(call, 0);
    const Byte* event = ROMlib_carbon_guest_memory(address, event_size, 0);
    if (!event)
        return 0;
    const uint32_t event_class = load_word(event, event_class_field), kind = load_word(event, event_kind_field);
    if (!(classic_event_mask(event_class, kind) & (uint16_t)call->words[1]))
        return result_word(call, 0);
    if (event_class == TICK("mous") && (kind == 1 || kind == 2)) {
        _Alignas(8) uint16_t button = 0;
        uint32_t status;
        return read_event_parameter(ref, event, TICK("mbtn"), TICK("mbtn"), &button, sizeof button, &status) &&
               result_word(call, !status && (CW(button) == 1 || CW(button) == 2));
    }
    return result_word(call, 1);
}

static uint32_t classic_event_converter(uint32_t event_class)
{
    switch (event_class) {
    case T('m', 'o', 'u', 's'):
        return 0x49cd0;
    case T('k', 'e', 'y', 'b'):
        return 0x48ec8;
    case T('w', 'i', 'n', 'd'):
        return 0x4f3a0;
    case T('v', 'o', 'l', ' '):
        return 0x48a40;
    case T('a', 'p', 'p', 'l'):
        return 0x3a78c;
    case T('e', 'p', 'p', 'c'):
        return 0x483f4;
    }
    return 0;
}

static int convert_classic_event(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t event = call->words[0], output = call->words[1];
    if (!output)
        return result_word(call, 0);
    EventRecord* record = ROMlib_carbon_guest_memory(output, event ? sizeof *record : sizeof record->what, event != 0);
    if (!record)
        return 0;
    if (!event)
        return result_word(call, record->what != 0);

    uint32_t address, key;
    if (!unmask_event(ref, event, &address, &key))
        return 0;
    const Byte* object = ROMlib_carbon_guest_memory(address, event_size, 0);
    if (!object)
        return 0;
    const uint32_t converter = classic_event_converter(load_word(object, event_class_field));
    _Alignas(16) EventRecord original = {0};
    const uint32_t fetch[] = {event, US_TO_SYN68K(&original)};
    uint32_t found;
    if (!cfp_private(ref, 0xb4cb0, fetch, NELEM(fetch), &found))
        return 0;
    if (!found) {
        double time;
        const uint64_t bits = load_double_bits(object, event_time_field);
        memcpy(&time, &bits, sizeof time);
        float ticks_per_second;
        if (!toc_float(ref, -0x1ac, 0, &ticks_per_second))
            return 0;
        time *= ticks_per_second;
        ROMlib_carbon_ppc_frame ticks = *call;
        memcpy(&ticks.floating[0], &time, sizeof time);
        if (!cfx_private_frame(ref, 0xb0f24, &ticks))
            return 0;
        original.when = CL(ticks.words[0]);
    }

    *record = (EventRecord){.when = original.when};
    const uint32_t point = output + offsetof(EventRecord, where);
    const uint32_t convert[] = {event, output};
    return cfp_private(ref, 0xb1050, &point, 1, NULL) &&
           (!converter || cfp_private(ref, converter, convert, NELEM(convert), NULL)) &&
           result_word(call, record->what != 0);
}

static int flush_classic_queue(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t classic_queue_mask = 0x7abf;
    const uint32_t flush_mask = call->words[0] & classic_queue_mask, stop_mask = call->words[1] & classic_queue_mask;
    uint32_t main_queue, iterator, event;
    if (!cfp_private(ref, 0xb34b0, NULL, 0, &main_queue) || !cfp_private(ref, 0x43bf0, &main_queue, 1, &iterator))
        return 0;
    if (!iterator)
        return 1;
    int ok = cfp_private(ref, 0x43cfc, &iterator, 1, &event);
    while (ok && event) {
        ROMlib_carbon_ppc_frame check = {.words = {event, stop_mask}};
        ok = is_event_in_classic_mask(ref, &check);
        if (!ok || check.words[0])
            break;
        check.words[0] = event;
        check.words[1] = flush_mask;
        ok = is_event_in_classic_mask(ref, &check);
        if (ok && check.words[0]) {
            uint32_t remove[] = {0, event};
            ok = cfp_private(ref, 0xb34b0, NULL, 0, &remove[0]) &&
                 cfp_private(ref, 0xb33c0, remove, NELEM(remove), NULL);
        }
        if (ok)
            ok = cfp_private(ref, 0x43d8c, &iterator, 1, &event);
    }
    return cfp_private(ref, 0x43ed0, &iterator, 1, NULL) && ok;
}

static int classic_mouse_button(const ROMlib_carbon_reference* ref, int delegate_slot, ROMlib_carbon_ppc_frame* call)
{
    uint32_t nested_wait, depth_address, delegate;
    if (!global_word(ref, -0xea0, &nested_wait) || !toc_pointer(ref, -0xeac, &depth_address))
        return 0;
    if (nested_wait) {
        uint32_t loop;
        if (!cfp_private(ref, 0xb3468, NULL, 0, &loop) || !cfp_private(ref, 0x41298, &loop, 1, NULL))
            return 0;
    }
    Byte* depth = ROMlib_carbon_guest_memory(depth_address, sizeof(uint32_t), 1);
    if (!depth || !global_word(ref, delegate_slot, &delegate))
        return 0;
    store_word(depth, 0, load_word(depth, 0) + 1);
    const uint32_t args[] = {delegate, 0x10};
    uint32_t result;
    const int ok = cfp_private(ref, 0xb5670, args, NELEM(args), &result);
    store_word(depth, 0, load_word(depth, 0) - 1);
    return ok && result_word(call, (uint8_t)result);
}

static int classic_os_event(const ROMlib_carbon_reference* ref, enum carbon_event_operation operation,
                            int delegate_slot, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t mask = (uint16_t)call->words[0], record = call->words[1];
    uint32_t result = 0;
    if (operation == event_classic_available) {
        uint32_t loop;
        const uint32_t lookup[] = {mask, record, 0, 0, 0};
        if (!cfp_private(ref, 0xb3450, NULL, 0, &loop) || !cfp_private(ref, 0x41298, &loop, 1, NULL) ||
            !cfp_private(ref, 0x3b618, lookup, NELEM(lookup), &result))
            return 0;
    }
    else if (mask) {
        uint32_t depth_address;
        if (!toc_pointer(ref, -0xeac, &depth_address))
            return 0;
        Byte* depth = ROMlib_carbon_guest_memory(depth_address, sizeof(uint32_t), 1);
        if (!depth)
            return 0;
        store_word(depth, 0, load_word(depth, 0) + 1);
        const uint32_t lookup[] = {mask & 0x3e, record, operation == event_classic_os_get, 0};
        const int ok = cfp_private(ref, 0x3bf60, lookup, NELEM(lookup), &result);
        store_word(depth, 0, load_word(depth, 0) - 1);
        if (!ok)
            return 0;
    }
    if (!result) {
        uint32_t delegate;
        const uint32_t args[] = {mask, record};
        if (!global_word(ref, delegate_slot, &delegate) || !cfp_callback(delegate, args, NELEM(args), &result))
            return 0;
    }
    return result_word(call, result);
}

static int is_cancel_key_event(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    uint32_t address, key;
    const uint32_t event = call->words[0];
    if (!unmask_event(ref, event, &address, &key))
        return 0;
    if (!address)
        return result_word(call, 0);
    const Byte* object = ROMlib_carbon_guest_memory(address, event_size, 0);
    if (!object)
        return 0;
    const uint32_t kind = load_word(object, event_kind_field);
    if (load_word(object, event_class_field) != TICK("keyb") || (kind != 1 && kind != 2))
        return result_word(call, 0);

    _Alignas(16) uint32_t values[4] = {0};
    const uint32_t output = US_TO_SYN68K(values);
    const uint32_t modifiers[] = {event, output};
    const uint32_t character[] = {event, output + 4};
    const uint32_t code[] = {event, output + 8};
    uint32_t status;
    if (!cfp_private(ref, 0x48cd4, modifiers, NELEM(modifiers), NULL) ||
        !cfp_private(ref, 0x48d18, character, NELEM(character), &status))
        return 0;
    if (status)
        return result_word(call, 0);
    if (!cfp_private(ref, 0x48e64, code, NELEM(code), &status))
        return 0;
    if (status)
        return result_word(call, 0);
    const uint32_t script_variable = smKCHRCache;
    uint32_t translation, characters = CL(values[1]);
    if (!cfp_private(ref, 0xb5ac0, &script_variable, 1, &translation))
        return 0;
    if (translation) {
        const uint32_t args[] = {translation, (uint16_t)((CL(values[0]) & 0xfe00) | CL(values[2])), output + 12};
        if (!cfp_private(ref, 0xb65b8, args, NELEM(args), &characters))
            return 0;
    }
    const Byte first = characters, second = characters >> 16;
    return result_word(call, first == 0x1b || second == 0x1b ||
                                 ((CL(values[0]) & cmdKey) && (first == '.' || second == '.')));
}

// Discard input already queued before the cancel. Other event classes and
// input posted after the cancel are still needed by the application.
static int discard_input_through_cancel(const ROMlib_carbon_reference* ref, uint32_t queue, uint32_t cancel)
{
    uint32_t iterator, event = 0;
    if (!cfp_private(ref, 0x43bf0, &queue, 1, &iterator))
        return 0;
    int ok = cfp_private(ref, 0x43cfc, &iterator, 1, &event);
    while (ok && event) {
        uint32_t event_class;
        ok = cfp_private(ref, 0xb3360, &event, 1, &event_class);
        if (ok && (event == cancel || event_class == TICK("mous") || event_class == TICK("keyb"))) {
            const uint32_t remove[] = {queue, event};
            ok = cfp_private(ref, 0xb33c0, remove, NELEM(remove), NULL);
        }
        if (event == cancel) {
            const uint32_t monitor[] = {0x10, event};
            ok = ok && cfp_private(ref, 0xb4c80, monitor, NELEM(monitor), NULL);
            break;
        }
        if (ok)
            ok = cfp_private(ref, 0x43d8c, &iterator, 1, &event);
    }
    return cfp_private(ref, 0x43ed0, &iterator, 1, NULL) && ok;
}

static int check_event_queue_for_cancel(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    uint32_t queue, iterator, event = 0;
    if (!cfp_private(ref, 0xb34b0, NULL, 0, &queue) || !cfp_private(ref, 0x43bf0, &queue, 1, &iterator))
        return 0;
    int ok = cfp_private(ref, 0x43cfc, &iterator, 1, &event);
    bool cancelled = false;
    while (ok && event) {
        ROMlib_carbon_ppc_frame check = {.words = {event}};
        ok = is_cancel_key_event(ref, &check);
        if (ok && check.words[0]) {
            // Keep the outer iterator open while the inner pass removes the
            // cancel. The outer iterator still owns a reference to that event.
            ok = discard_input_through_cancel(ref, queue, event);
            cancelled = true;
            break;
        }
        if (ok)
            ok = cfp_private(ref, 0x43d8c, &iterator, 1, &event);
    }
    if (!cfp_private(ref, 0x43ed0, &iterator, 1, NULL) || !ok)
        return 0;
    if (cancelled)
        return result_word(call, 1);

    _Alignas(8) EventRecord classic = {0};
    const uint32_t output = US_TO_SYN68K(&classic);
    uint32_t status;
    return cfp_private(ref, 0xb4cf8, &output, 1, &status) && result_word(call, status != 0);
}

static int is_context_menu_event(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    uint32_t address, key;
    if (!unmask_event(ref, call->words[0], &address, &key))
        return 0;
    if (!address)
        return result_word(call, 0);
    const Byte* event = ROMlib_carbon_guest_memory(address, event_size, 0);
    if (!event)
        return 0;
    const uint32_t event_class = load_word(event, event_class_field), kind = load_word(event, event_kind_field);
    if (!((event_class == TICK("mous") && kind == 1) || (event_class == TICK("wind") && (kind == 37 || kind == 85))))
        return result_word(call, 0);

    _Alignas(8) uint16_t button = 0;
    _Alignas(8) uint32_t modifiers = 0;
    uint32_t status;
    if (!read_event_parameter(ref, event, TICK("mbtn"), TICK("mbtn"), &button, sizeof button, &status))
        return 0;
    if (status)
        return result_word(call, 0);
    const uint32_t context_modifiers = ControlKey | optionKey | shiftKey | cmdKey;
    return read_event_parameter(ref, event, TICK("kmod"), TICK("magn"), &modifiers, sizeof modifiers, &status) &&
           result_word(call, !status && (CW(button) == 2 || (CL(modifiers) & context_modifiers) == ControlKey));
}

static int could_be_menu_key_event(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    uint32_t address, key;
    if (!unmask_event(ref, call->words[1], &address, &key))
        return 0;
    if (!address)
        return result_word(call, 0);
    const Byte* event = ROMlib_carbon_guest_memory(address, event_size, 0);
    if (!event)
        return 0;
    const uint32_t kind = load_word(event, event_kind_field);
    if (kind != 1 && kind != 2)
        return result_word(call, 0);
    uint32_t pending, flags;
    if (!global_word(ref, -0xc8c, &pending) || !global_word(ref, -0x15a8, &flags))
        return 0;
    if (pending || !(flags & 1))
        return result_word(call, 1);

    _Alignas(8) uint32_t modifiers = 0;
    uint32_t status, required;
    return read_event_parameter(ref, event, TICK("kmod"), TICK("magn"), &modifiers, sizeof modifiers, &status) &&
           global_word(ref, -0x15a4, &required) && result_word(call, (CL(modifiers) & required) == required);
}

static int masked_ref(const ROMlib_carbon_reference* ref, int key_slot, uint32_t handle, uint32_t* object)
{
    uint32_t key;
    *object = 0;
    if (!handle)
        return 1;
    if (!global_word(ref, key_slot, &key))
        return 0;
    *object = handle ^ key;
    return 1;
}

static int get_flag(const ROMlib_carbon_reference* ref, int slot, Byte** flag)
{
    uint32_t address;
    if (!toc_pointer(ref, slot, &address))
        return 0;
    *flag = ROMlib_carbon_guest_memory(address, 1, 1);
    return *flag != NULL;
}

enum control_drag_operation {
    control_drag_track,
    control_drag_receive,
    control_drag_enable,
    control_drag_query,
    window_drag_enable,
    window_drag_query,
};

enum {
    control_drag_property = T('d', 'r', 'a', 'g'),
    window_drag_property = T('c', 'd', 'r', 'g'),
    control_drag_feature = 0x1000,
    drag_not_accepted = -1857
};

static int control_drag_delegate(const ROMlib_carbon_reference* ref, enum control_drag_operation operation,
                                 uint32_t* delegate)
{
    static const struct {
        int flag, vector, name;
    } entries[] = {
        {-0x540, -0x53c, 0x0d}, {-0x538, -0x534, 0x28}, {-0x528, -0x524, 0x42},
        {-0x520, -0x51c, 0x61}, {-0x518, -0x514, 0x7f},
    };

    Byte* initialized;
    uint32_t slot;
    if (!get_flag(ref, entries[operation].flag, &initialized) || !toc_pointer(ref, entries[operation].vector, &slot))
        return 0;
    if (*initialized)
        return cfp_get(slot, delegate);
    uint32_t strings;
    if (!toc_pointer(ref, -0x11c, &strings))
        return 0;
    const uint32_t names[] = {strings, strings + entries[operation].name};
    if (!cfp_private(ref, 0x55c5c, names, NELEM(names), delegate) || !cfp_write(slot, *delegate))
        return 0;
    *initialized = 1;
    return 1;
}

static int install_window_drag_handlers(const ROMlib_carbon_reference* ref, uint32_t window, int enabled)
{
    uint32_t track, receive;
    if (!toc_pointer(ref, -0x530, &track) || !toc_pointer(ref, -0x52c, &receive))
        return 0;
    const uint32_t tracker[] = {track, window, 0}, receiver[] = {receive, window, 0};
    return cfp_private(ref, enabled ? 0xb5a60 : 0xb5a90, tracker, enabled ? 3 : 2, NULL) &&
           cfp_private(ref, enabled ? 0xb5a78 : 0xb5aa8, receiver, enabled ? 3 : 2, NULL);
}

static int window_drag(const ROMlib_carbon_reference* ref, enum control_drag_operation operation,
                       ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0];
    _Alignas(8) Byte enabled = 0;
    const uint32_t property = US_TO_SYN68K(&enabled);
    uint32_t status;
    if (operation == window_drag_query) {
        Byte* output = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], 1, 1) : NULL;
        if (!output)
            return result_word(call, paramErr);
        const uint32_t query[] = {window, window_drag_property, 1, 0, property};
        if (!cfp_private(ref, 0xb4530, query, NELEM(query), &status))
            return 0;
        if ((int32_t)status == window_property_not_found)
            status = noErr;
        if (!status)
            *output = enabled;
        return result_word(call, status);
    }

    const int enable = (uint8_t)call->words[1] != 0;
    if (!enable) {
        const uint32_t query[] = {window, window_drag_property, 1, 0, property};
        if (!cfp_private(ref, 0xb4530, query, NELEM(query), &status))
            return 0;
        if (status || enabled != 1)
            return result_word(call, noErr);
    }
    enabled = enable;
    const uint32_t update[] = {window, window_drag_property, 1, property};
    return cfp_private(ref, 0xb4500, update, NELEM(update), &status) &&
           ((enable && status) || install_window_drag_handlers(ref, window, enable)) && result_word(call, noErr);
}

static int enable_control_drag(const ROMlib_carbon_reference* ref, uint32_t control, Byte enable,
                               ROMlib_carbon_ppc_frame* call)
{
    _Alignas(8) Byte enabled = enable;
    const uint32_t set[] = {control, control_drag_property, 1, US_TO_SYN68K(&enabled)};
    if (!cfp_private(ref, enable ? 0xb45d8 : 0xb45f0, set, enable ? NELEM(set) : 2, NULL))
        return 0;
    uint32_t owner, master, status;
    const uint32_t query[] = {control, control_drag_property, 1, 0, US_TO_SYN68K(&enabled)};
    if (!cfp_get(control, &master) || !master || !cfp_get(master + 4, &owner) ||
        !cfp_private(ref, 0xb45c0, query, NELEM(query), &status))
        return 0;
    if (status)
        return result_word(call, noErr);
    _Alignas(8) uint32_t features = 0;
    const uint32_t feature[] = {control, US_TO_SYN68K(&features)};
    if (!cfp_private(ref, 0xb5a30, feature, NELEM(feature), &status))
        return 0;
    if (status || !(CL(features) & control_drag_feature))
        return result_word(call, noErr);
    const uint32_t auto_tracking[] = {owner, US_TO_SYN68K(&enabled)};
    if (!cfp_private(ref, 0xb1b90, auto_tracking, NELEM(auto_tracking), &status))
        return 0;
    if (status || !enabled)
        return result_word(call, noErr);
    enabled = 1;
    const uint32_t set_window[] = {owner, window_drag_property, 1, US_TO_SYN68K(&enabled)};
    return cfp_private(ref, 0xb4500, set_window, NELEM(set_window), &status) &&
           (status || install_window_drag_handlers(ref, owner, 1)) && result_word(call, noErr);
}

static int control_drag(const ROMlib_carbon_reference* ref, enum control_drag_operation operation,
                        ROMlib_carbon_ppc_frame* call)
{
    const uint32_t control = call->words[0];
    if (operation != window_drag_query) {
        uint32_t delegate;
        if (!control_drag_delegate(ref, operation, &delegate))
            return 0;
        if (delegate) {
            uint32_t args[] = {control, call->words[1], call->words[2], call->words[3]}, status;
            if (operation == control_drag_track)
                args[1] = (int16_t)args[1];
            else if (operation == control_drag_enable || operation == window_drag_enable)
                args[1] = (uint8_t)args[1];
            return cfp_callback(delegate, args, operation == control_drag_track ? 4 : 2, &status) &&
                   result_word(call, status);
        }
    }
    if (operation >= window_drag_enable)
        return window_drag(ref, operation, call);
    uint32_t valid;
    if (!cfp_private(ref, 0xb1b30, &control, 1, &valid))
        return 0;
    if (!valid)
        return result_word(call, paramErr);
    if (operation == control_drag_enable)
        return enable_control_drag(ref, control, (uint8_t)call->words[1], call);

    const int tracking = operation == control_drag_track;
    const int16_t message = call->words[1];
    static const uint32_t tracking_messages[] = {0x26, 0x28, 0x27};
    if (tracking && (message < 2 || message > 4))
        return result_word(call, paramErr);
    Byte* accepted = NULL;
    if (tracking && call->words[3]) {
        accepted = ROMlib_carbon_guest_memory(call->words[3], 1, 1);
        if (!accepted)
            return 0;
        *accepted = 0;
    }
    Byte* enabled = NULL;
    if (operation == control_drag_query) {
        enabled = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], 1, 1) : NULL;
        if (!enabled)
            return result_word(call, paramErr);
    }
    _Alignas(8) Byte marker = 0;
    uint32_t status;
    const uint32_t query[] = {control, control_drag_property, 1, 0, US_TO_SYN68K(&marker)};
    if (!cfp_private(ref, 0xb45c0, query, NELEM(query), &status))
        return 0;
    if (enabled) {
        *enabled = status == 0;
        return result_word(call, noErr);
    }
    if (status)
        return result_word(call, tracking ? noErr : drag_not_accepted);
    _Alignas(8) uint32_t features = 0;
    const uint32_t feature[] = {control, US_TO_SYN68K(&features)};
    if (!cfp_private(ref, 0xb5a30, feature, NELEM(feature), &status))
        return 0;
    if (status || !(CL(features) & control_drag_feature))
        return result_word(call, status ? status : tracking ? noErr : drag_not_accepted);
    const uint32_t send[] = {control, tracking ? tracking_messages[message - 2] : 0x29, call->words[tracking ? 2 : 1]};
    if (!cfp_private(ref, 0xb5a48, send, NELEM(send), &status))
        return 0;
    if (accepted)
        *accepted = (Byte)status;
    return result_word(call, tracking ? noErr : status);
}

static int new_event_manager_object(const ROMlib_carbon_reference* ref, int class_slot, uint32_t* result)
{
    uint32_t creation[] = {0, 0, 0};
    return global_word(ref, -0x2e7c, &creation[0]) && global_word(ref, class_slot, &creation[1]) &&
           cfp_private(ref, 0x4bef0, creation, 3, result);
}

static int new_queue(const ROMlib_carbon_reference* ref, uint32_t* queue)
{
    Byte* initialized;
    if (!get_flag(ref, -0xd70, &initialized))
        return 0;
    if (!*initialized) {
        uint32_t description, class_slot, key_slot, class_ref, ticks;
        if (!toc_pointer(ref, -0xd7c, &description) || !toc_pointer(ref, -0xd78, &class_slot) ||
            !toc_pointer(ref, -0xd74, &key_slot) || !cfp_private(ref, 0x4be28, &description, 1, &class_ref) ||
            !cfp_write(class_slot, class_ref) || !cfp_private(ref, 0xb10f8, NULL, 0, &ticks) ||
            !cfp_write(key_slot, ticks ^ UINT32_C(0x777a7a70)))
            return 0;
        *initialized = 1;
    }
    return new_event_manager_object(ref, -0xd78, queue);
}

static int new_loop(const ROMlib_carbon_reference* ref, uint32_t* loop)
{
    if (!new_event_manager_object(ref, -0xdcc, loop))
        return 0;
    if (!*loop)
        return 1;
    Byte* record = ROMlib_carbon_guest_memory(*loop, 0x26, 1);
    if (!record)
        return 0;
    store_word(record, 0x10, 0);
    store_word(record, 0x14, 0);
    record[0x18] = 1;
    record[0x24] = record[0x25] = 0;
    const uint32_t lock = *loop + 0x1c, queue = *loop + 0x20;
    return cfp_private(ref, 0xb22f8, &lock, 1, NULL) && cfp_private(ref, 0xb2280, &queue, 1, NULL);
}

static int current_manager_object(const ROMlib_carbon_reference* ref, int is_loop, uint32_t* result)
{
    uint32_t preemptive;
    if (!cfp_private(ref, 0xb2358, NULL, 0, NULL) || !cfp_private(ref, 0xb2310, NULL, 0, &preemptive))
        return 0;
    if (!preemptive)
        return global_word(ref, is_loop ? -0xdb4 : -0xd6c, result);

    Byte* initialized;
    uint32_t storage, key;
    if (!get_flag(ref, is_loop ? -0xdb0 : -0xd68, &initialized) ||
        !toc_pointer(ref, is_loop ? -0xdac : -0xd64, &storage))
        return 0;
    if (!*initialized) {
        if (!cfp_private(ref, 0xb2340, &storage, 1, NULL))
            return 0;
        *initialized = 1;
    }
    if (!cfp_get(storage, &key) || !cfp_private(ref, 0xb2370, &key, 1, result))
        return 0;
    if (*result)
        return 1;
    if (!(is_loop ? new_loop(ref, result) : new_queue(ref, result)))
        return 0;
    const uint32_t arguments[] = {key, *result};
    return cfp_private(ref, 0xb22b0, arguments, 2, NULL);
}

static int get_queue(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call, int current)
{
    uint32_t slot, queue, result;
    if (current) {
        if (!current_manager_object(ref, 0, &queue))
            return 0;
    }
    else {
        if (!toc_pointer(ref, -0xd6c, &slot) || !cfp_get(slot, &queue))
            return 0;
        if (!queue && (!new_queue(ref, &queue) || !cfp_write(slot, queue)))
            return 0;
    }
    return masked_ref(ref, -0xd74, queue, &result) && result_word(call, result);
}

static int enter_region(const ROMlib_carbon_reference* ref, uint32_t lock)
{
    const uint32_t lock_args[] = {lock, INT32_MAX};
    return cfp_private(ref, 0xb2328, lock_args, 2, NULL);
}

static int leave_region(const ROMlib_carbon_reference* ref, uint32_t lock)
{
    return cfp_private(ref, 0xb2388, &lock, 1, NULL);
}

/* All callers hold the queue lock. Notify registered iterators before
   rewriting the links or releasing the queue's reference to the event. */
static int detach_queued_event(const ROMlib_carbon_reference* ref, uint32_t queue, Byte* record, uint32_t address,
                               Byte* event)
{
    const uint32_t observer_args[] = {queue, address};
    if (load_word(record, 0x110) && !cfp_private(ref, 0x43120, observer_args, 2, NULL))
        return 0;
    const uint32_t next = load_word(event, 4), previous = load_word(event, 8);
    if (!next || !previous)
        return ROMlib_carbon_report_failure("Missing Carbon event queue link");
    Byte* successor = ROMlib_carbon_guest_memory(next, 12, 1);
    Byte* predecessor = ROMlib_carbon_guest_memory(previous, 12, 1);
    if (!successor || !predecessor)
        return 0;
    store_word(predecessor, 4, next);
    store_word(successor, 8, previous);
    memset(event, 0, 12);
    store_word(record, 0x10c, load_word(record, 0x10c) - 1);
    return 1;
}

static int unlink_queued_event(const ROMlib_carbon_reference* ref, uint32_t queue, uint32_t address, Byte* event,
                               ROMlib_carbon_ppc_frame* call)
{
    Byte* record = ROMlib_carbon_guest_memory(queue, 0x11c, 1);
    if (!record)
        return 0;
    const uint32_t lock = load_word(record, 0x118);
    if (!enter_region(ref, lock))
        return 0;
    uint32_t status = -9877;
    int ok = 1;
    if (load_word(event, 0) == call->words[0]) {
        ok = detach_queued_event(ref, queue, record, address, event);
        if (ok)
            status = noErr;
    }
    const int unlocked = leave_region(ref, lock);
    if (!ok || !unlocked)
        return 0;
    if (!status) {
        Byte* logging;
        const uint32_t opaque = call->words[1];
        if (!cfp_private(ref, 0xb33f0, &opaque, 1, NULL) || !get_flag(ref, -0xd60, &logging))
            return 0;
        if (*logging) {
            uint32_t message;
            if (!toc_pointer(ref, -0x190, &message))
                return 0;
            const uint32_t log_args[] = {message + 0x35, opaque, 1};
            if (!cfp_private(ref, 0x40d90, log_args, 3, NULL))
                return 0;
        }
    }
    return result_word(call, status);
}

static int queue_operation(const ROMlib_carbon_reference* ref, enum carbon_event_operation operation,
                           ROMlib_carbon_ppc_frame* call)
{
    uint32_t queue, result = 0;
    const uint32_t opaque_queue = call->words[0], opaque_event = call->words[1];
    if (operation == event_post_queue) {
        const int16_t priority = call->words[2];
        if (!opaque_queue || priority < 0 || priority >= 3)
            return result_word(call, paramErr);
        const uint32_t arguments[] = {opaque_queue, opaque_event, (uint32_t)priority, 1};
        return cfp_private(ref, 0xb4c98, arguments, 4, &result) && result_word(call, result);
    }
    if (!masked_ref(ref, -0xd74, opaque_queue, &queue))
        return 0;
    if (operation == event_count_queue)
        return (!queue || cfp_get(queue + 0x10c, &result)) && result_word(call, result);

    uint32_t address, key;
    if (!unmask_event(ref, opaque_event, &address, &key))
        return 0;
    if (!queue || !address)
        return result_word(call, operation == event_remove_queue ? paramErr : 0);
    Byte* event = ROMlib_carbon_guest_memory(address, event_size, operation == event_remove_queue);
    if (!event)
        return 0;
    if (operation == event_is_queued)
        return result_word(call, load_word(event, 0) == opaque_queue);
    return unlink_queued_event(ref, queue, address, event, call);
}

enum queue_selection { queue_flush, queue_peek, queue_take };

static int event_matches(const Byte* event, uint32_t count, const Byte* types)
{
    if (!types)
        return 1;
    const uint32_t event_class = load_word(event, event_class_field);
    const uint32_t kind = load_word(event, event_kind_field);
    for (uint32_t i = 0; i < count; ++i, types += 8)
        if (event_class == load_word(types, 0) && kind == load_word(types, 4))
            return 1;
    return 0;
}

static int select_queued_events(const ROMlib_carbon_reference* ref, uint32_t queue, uint32_t count, uint32_t filters,
                                enum queue_selection selection, uint32_t* result)
{
    const Byte* types = NULL;
    if (count && filters) {
        if (count > UINT32_MAX / 8)
            return ROMlib_carbon_report_failure("Carbon event filter list is too large");
        types = ROMlib_carbon_guest_memory(filters, count * 8, 0);
        if (!types)
            return 0;
    }
    *result = 0;
    if (filters && !count)
        return 1;
    Byte* record = ROMlib_carbon_guest_memory(queue, 0x11c, 1);
    uint32_t key;
    if (!record || !event_key(ref, &key))
        return 0;
    const uint32_t lock = load_word(record, 0x118);
    if (!enter_region(ref, lock))
        return 0;
    int ok = 1;
    uint64_t remaining = (uint64_t)load_word(record, 0x10c) + 2;
    for (uint32_t address = load_word(record, 0x14); address != queue + 0x10;) {
        if (!address || !remaining--) {
            ok = ROMlib_carbon_report_failure("Invalid Carbon event queue list");
            break;
        }
        Byte* event = ROMlib_carbon_guest_memory(address, event_classic_field, selection != queue_peek);
        if (!event) {
            ok = 0;
            break;
        }
        const uint32_t next = load_word(event, 4);
        const int sentinel = address == queue + 0x60 || address == queue + 0xb0;
        if (!sentinel && (selection == queue_flush || !load_word(event, 0x2c)) && event_matches(event, count, types)) {
            const uint32_t opaque = address ^ key;
            if (selection == queue_peek) {
                ok = cfp_private(ref, 0xb3330, &opaque, 1, NULL);
                if (ok)
                    *result = opaque;
                break;
            }
            ok = detach_queued_event(ref, queue, record, address, event);
            if (ok && selection == queue_take) {
                *result = opaque;
                break;
            }
            if (!ok || !cfp_private(ref, 0xb33f0, &opaque, 1, NULL)) {
                ok = 0;
                break;
            }
        }
        address = next;
    }
    const int unlocked = leave_region(ref, lock);
    return ok && unlocked;
}

static int flush_classic_events(const ROMlib_carbon_reference* ref, int matching)
{
    Byte* initialized;
    uint32_t slot, delegate;
    if (!get_flag(ref, matching ? -0xd54 : -0xd4c, &initialized) ||
        !toc_pointer(ref, matching ? -0xd58 : -0xd50, &slot))
        return 0;
    if (!*initialized) {
        uint32_t strings;
        if (!toc_pointer(ref, -0x190, &strings))
            return 0;
        const uint32_t names[] = {strings + 0x44, strings + 0x54};
        if (!cfp_private(ref, 0x55c5c, names, 2, &delegate) || !cfp_write(slot, delegate))
            return 0;
        *initialized = 1;
    }
    if (!cfp_get(slot, &delegate))
        return 0;
    const uint32_t args[] = {matching ? 0 : UINT16_MAX, 0};
    return cfp_callback(delegate, args, 2, NULL);
}

static int flush_event_queue(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call, int matching)
{
    uint32_t queue, main_queue, ignored;
    const uint32_t count = matching ? call->words[1] : 0, types = matching ? call->words[2] : 0;
    if (!masked_ref(ref, -0xd74, call->words[0], &queue))
        return 0;
    if (!queue || (matching && (!count || !types)))
        return result_word(call, paramErr);
    if (!matching && !cfp_private(ref, 0xb34b0, NULL, 0, &main_queue))
        return 0;
    if ((matching || main_queue == call->words[0]) && !flush_classic_events(ref, matching))
        return 0;
    return select_queued_events(ref, queue, count, types, queue_flush, &ignored) && result_word(call, noErr);
}

static int compare_queued_events(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call, int remove)
{
    const uint32_t queue = call->words[0], comparator = call->words[1];
    uint32_t iterator, event = 0;
    if (!queue || !comparator)
        return result_word(call, remove ? paramErr : 0);
    if (!cfp_private(ref, 0x43bf0, &queue, 1, &iterator))
        return 0;
    if (!iterator)
        return result_word(call, 0);
    int ok = cfp_private(ref, 0x43cfc, &iterator, 1, &event);
    while (ok && event) {
        const uint32_t args[] = {comparator, 0x3d0, event, call->words[2]};
        uint32_t match;
        ok = cfp_private(ref, 0xb5670, args, 4, &match);
        if (!ok)
            break;
        if ((uint8_t)match) {
            if (!remove)
                break;
            const uint32_t item[] = {queue, event};
            if (!cfp_private(ref, 0xb33c0, item, NELEM(item), NULL)) {
                ok = 0;
                break;
            }
        }
        ok = cfp_private(ref, 0x43d8c, &iterator, 1, &event);
    }
    const int closed = cfp_private(ref, 0x43ed0, &iterator, 1, NULL);
    return ok && closed && result_word(call, remove ? noErr : event);
}

static int install_handler(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* words = call->words;
    if (!words[0] || !words[1] || words[1] == UINT32_MAX || words[2] > INT32_MAX / 8 || (words[2] && !words[3]))
        return result_word(call, paramErr);
    if ((words[2] && !ROMlib_carbon_guest_memory(words[3], words[2] * 8, 0)) ||
        (words[5] && !ROMlib_carbon_guest_memory(words[5], 4, 1)))
        return 0;
    const uint32_t args[] = {words[0], words[1], 2, words[2], words[3], words[4], words[5]};
    uint32_t status;
    return cfp_private(ref, 0x46230, args, 7, &status) && result_word(call, status);
}

static int dispatch_event(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call, int next)
{
    _Alignas(16) Byte context[16] = {0};
    uint32_t arguments[] = {call->words[0], call->words[1], US_TO_SYN68K(context)};
    if (next) {
        Byte* incoming = ROMlib_carbon_guest_memory(call->words[0], sizeof context, 1);
        if (!incoming || !call->words[0])
            return result_word(call, paramErr);
        memcpy(context, incoming, sizeof context);
        incoming[8] = 1;
        context[8] = 0;
        arguments[0] = call->words[1];
        arguments[1] = load_word(context, 0);
    }
    uint32_t status;
    return cfp_private(ref, 0x46994, arguments, 3, &status) && result_word(call, status);
}

static int handler_registry(const ROMlib_carbon_reference* ref, uint32_t* registry)
{
    uint32_t slot;
    if (!toc_pointer(ref, -0xd40, &slot) || !cfp_get(slot, registry))
        return 0;
    if (*registry)
        return 1;
    uint32_t creation[] = {0, 0, 0, 0};
    return cfp_private(ref, 0x494c4, NULL, 0, &creation[0]) &&
           cfp_call("CFDictionaryCreateMutable", creation, 4, registry) && cfp_write(slot, *registry);
}

static int remove_handler(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t handler = call->words[0];
    if (!handler)
        return result_word(call, paramErr);
    uint32_t lookup[] = {0, handler}, found = 0;
    if (!handler_registry(ref, &lookup[0]) || (lookup[0] && !cfp_call("CFDictionaryGetValue", lookup, 2, &found)))
        return 0;
    if (!found)
        return result_word(call, paramErr);
    const Byte* record = ROMlib_carbon_guest_memory(handler, 0x24, 0);
    if (!record)
        return 0;
    const uint32_t types = load_word(record, 0x20), next = load_word(record, 0x10), previous = load_word(record, 0x14);
    if (types) {
        const uint32_t args[] = {load_word(record, 0), handler, load_word(record, 0x1c), types};
        if (!cfp_private(ref, 0x47040, args, 4, NULL))
            return 0;
    }
    return cfp_write(previous + 0x10, next) && cfp_write(next + 0x14, previous) &&
           cfp_call("CFDictionaryRemoveValue", lookup, 2, NULL) &&
           (!types || cfp_private(ref, 0x49480, &types, 1, NULL)) && cfp_private(ref, 0x49480, &handler, 1, NULL) &&
           result_word(call, noErr);
}

static int edit_handler_types(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call, int add)
{
    const uint32_t handler = call->words[0], count = call->words[1], types = call->words[2];
    if (!handler || !count || count > INT32_MAX / 8 || !types)
        return result_word(call, paramErr);
    Byte* record = ROMlib_carbon_guest_memory(handler, 0x24, !add);
    const Byte* specs = ROMlib_carbon_guest_memory(types, count * 8, 0);
    if (!record || !specs)
        return 0;
    uint32_t status = 0;
    const uint32_t args[] = {load_word(record, 0), handler, count, types};
    if (add) {
        const uint32_t grow[] = {handler, count, types};
        if (!cfp_private(ref, 0x46e58, grow, 3, &status))
            return 0;
        if (!status && !cfp_private(ref, 0x45ef4, args, 4, &status))
            return 0;
        return result_word(call, status);
    }
    uint32_t length = load_word(record, 0x1c);
    if (length > INT32_MAX / 8 || !cfp_private(ref, 0x47040, args, 4, NULL))
        return 0;
    if (length) {
        Byte* active = ROMlib_carbon_guest_memory(load_word(record, 0x20), length * 8, 1);
        if (!active)
            return 0;
        for (uint32_t i = 0; i < count && length; ++i) {
            for (uint32_t j = 0; j < length; ++j) {
                if (!memcmp(specs + i * 8, active + j * 8, 8)) {
                    --length;
                    memmove(active + j * 8, active + (j + 1) * 8, (length - j) * 8);
                    break;
                }
            }
        }
        store_word(record, 0x1c, length);
    }
    return result_word(call, noErr);
}

static double frame_time(const ROMlib_carbon_ppc_frame* call, unsigned index)
{
    double value;
    memcpy(&value, &call->floating[index], sizeof value);
    return value;
}

static int wait_for_events(const ROMlib_carbon_reference* ref, const ROMlib_carbon_ppc_frame* caller, double timeout,
                           uint32_t* status)
{
    ROMlib_carbon_ppc_frame wait = *caller;
    wait.words[2] = 0;
    wait.words[3] = 1;
    memcpy(&wait.floating[0], &timeout, sizeof timeout);
    if (!cfx_private_frame(ref, 0x41434, &wait))
        return 0;
    *status = wait.words[0];
    return 1;
}

static int run_current_event_loop(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const double timeout = frame_time(call, 0);
    if (!isfinite(timeout) || timeout < 0.0) {
        call->words[2] = 0;
        call->words[3] = 0;
        return cfx_private_frame(ref, 0x41434, call);
    }

    uint64_t bits;
    double now;
    if (!read_event_clock(ref, &bits))
        return 0;
    memcpy(&now, &bits, sizeof now);
    const double deadline = now + timeout;
    /* Wakeups for queued events can keep the legacy loop from reaching its
       timeout branch. Recheck the deadline after each wakeup. */
    do {
        uint32_t status;
        if (!wait_for_events(ref, call, deadline - now, &status))
            return 0;
        if (status)
            return result_word(call, status);
        if (!read_event_clock(ref, &bits))
            return 0;
        memcpy(&now, &bits, sizeof now);
    } while (now < deadline);
    return result_word(call, event_loop_timed_out);
}

static int take_next_event(const ROMlib_carbon_reference* ref, uint32_t count, uint32_t filters, uint8_t remove,
                           uint32_t* event)
{
    uint32_t opaque, queue;
    if (!cfp_private(ref, 0xb3300, NULL, 0, &opaque) || !masked_ref(ref, -0xd74, opaque, &queue) || !queue ||
        !select_queued_events(ref, queue, count, filters, remove ? queue_take : queue_peek, event))
        return 0;
    if (!*event || !remove)
        return 1;
    Byte* logging;
    const uint32_t monitor[] = {0x10, *event};
    if (!cfp_private(ref, 0xb4c80, monitor, 2, NULL) || !get_flag(ref, -0xd60, &logging))
        return 0;
    if (!*logging)
        return 1;
    uint32_t message;
    if (!toc_pointer(ref, -0x190, &message))
        return 0;
    const uint32_t log_args[] = {message + 0x65, *event, 1};
    return cfp_private(ref, 0x40d90, log_args, 3, NULL);
}

static int receive_next_event(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call, int retain_peeked)
{
    const uint32_t count = call->words[0], filters = call->words[1], output = call->words[5];
    const uint8_t remove = call->words[4];
    if (remove > 1 || (remove && !output))
        return result_word(call, paramErr);
    if (output && !ROMlib_carbon_guest_memory(output, 4, 1))
        return 0;

    uint32_t status, event;
    if (!wait_for_events(ref, call, 0.0, &status) || !take_next_event(ref, count, filters, remove, &event))
        return 0;
    if (!event) {
        const double timeout = frame_time(call, 0);
        double remaining = timeout, deadline = 0;
        if (timeout != -1.0) {
            uint64_t bits;
            double start;
            if (!read_event_clock(ref, &bits))
                return 0;
            memcpy(&start, &bits, sizeof start);
            deadline = start + timeout;
        }
        while (!event) {
            /* Unmatched events can wake the loop before the deadline.
               Never send an expired timeout back to the wait worker. */
            if (timeout != -1.0 && remaining <= 0) {
                status = event_loop_timed_out;
                break;
            }
            if (!wait_for_events(ref, call, remaining, &status) ||
                !take_next_event(ref, count, filters, remove, &event))
                return 0;
            if (event || status)
                break;
            if (timeout != -1.0) {
                uint64_t bits;
                double now;
                if (!read_event_clock(ref, &bits))
                    return 0;
                memcpy(&now, &bits, sizeof now);
                remaining = deadline - now;
            }
        }
    }
    if (output && !cfp_write(output, event))
        return 0;
    if (event && (!output || (!remove && !retain_peeked)) && !cfp_private(ref, 0xb33f0, &event, 1, NULL))
        return 0;
    return result_word(call, event ? noErr : status);
}

enum {
    tracking_down = 1,
    tracking_up = 2,
    tracking_exited = 3,
    tracking_entered = 4,
    tracking_dragged = 5,
    tracking_modifiers = 6,
    tracking_cancelled = 7,
    tracking_timeout = 8,
    tracking_key_class = 0x6b657962,
    tracking_location_param = 0x6d6c6f63,
    tracking_location_type = 0x51447074,
    tracking_modifiers_param = 0x6b6d6f64,
    tracking_modifiers_type = 0x6d61676e
};

static int tracking_set_port(const ROMlib_carbon_reference* ref, uint32_t port, uint32_t* previous)
{
    *previous = 0;
    if (!port || port == UINT32_MAX)
        return 1;
    _Alignas(8) uint32_t saved;
    const uint32_t output = US_TO_SYN68K(&saved);
    if (!cfp_private(ref, 0xb12f0, &output, 1, NULL) || !cfp_private(ref, 0xb12d8, &port, 1, NULL))
        return 0;
    *previous = CL(saved);
    return 1;
}

static int tracking_finish(const ROMlib_carbon_reference* ref, uint32_t port, uint32_t previous,
                           ROMlib_carbon_ppc_frame* call, int ok, uint32_t status)
{
    if (port && port != UINT32_MAX && !cfp_private(ref, 0xb12d8, &previous, 1, NULL))
        return 0;
    return ok && result_word(call, status);
}

static int tracking_receive(const ROMlib_carbon_reference* ref, const ROMlib_carbon_ppc_frame* caller, uint32_t count,
                            uint32_t filters, int remove, double timeout, uint32_t* event, uint32_t* status)
{
    _Alignas(8) uint32_t received = 0;
    ROMlib_carbon_ppc_frame call = *caller;
    call.words[0] = count;
    call.words[1] = filters;
    call.words[4] = remove;
    call.words[5] = US_TO_SYN68K(&received);
    memcpy(&call.floating[0], &timeout, sizeof timeout);
    if (!receive_next_event(ref, &call, 1))
        return 0;
    *event = CL(received);
    *status = call.words[0];
    return 1;
}

static int tracking_parameter(const ROMlib_carbon_reference* ref, uint32_t event, int location, uint32_t out)
{
    if (!out)
        return 1;
    const uint32_t args[] = {event,
                             location ? tracking_location_param : tracking_modifiers_param,
                             location ? tracking_location_type : tracking_modifiers_type,
                             0,
                             4,
                             0,
                             out};
    return cfp_private(ref, 0xb3240, args, NELEM(args), NULL);
}

static int tracking_mouse_point(const ROMlib_carbon_reference* ref, uint32_t port, uint32_t out)
{
    return !out || cfp_private(ref, port == UINT32_MAX ? 0xb1050 : 0xb6468, &out, 1, NULL);
}

static int tracking_remove_event(const ROMlib_carbon_reference* ref, uint32_t event)
{
    uint32_t args[] = {0, event};
    const uint32_t monitor[] = {0x10, event};
    return cfp_private(ref, 0xb3300, NULL, 0, &args[0]) && cfp_private(ref, 0xb33c0, args, NELEM(args), NULL) &&
           cfp_private(ref, 0xb4c80, monitor, NELEM(monitor), NULL);
}

static int track_location_events(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call, uint32_t port,
                                 uint32_t options, double timeout, double default_timeout, uint32_t point,
                                 uint32_t modifiers, uint16_t* outcome, uint32_t* status)
{
    uint32_t queue, obsolete, filters;
    if (!cfp_private(ref, 0xb3300, NULL, 0, &queue) || !toc_pointer(ref, -0xd38, &obsolete) ||
        !toc_pointer(ref, -0xd34, &filters))
        return 0;
    const uint32_t flush[] = {queue, 2, obsolete};
    if (!cfp_private(ref, 0xb3270, flush, NELEM(flush), NULL))
        return 0;

    while (!*outcome) {
        uint32_t event;
        if (!tracking_receive(ref, call, 7, filters, 0, timeout, &event, status))
            return 0;
        if (*status == (uint32_t)event_loop_timed_out && timeout != default_timeout) {
            uint32_t current;
            *status = noErr;
            *outcome = CW(tracking_timeout);
            return tracking_mouse_point(ref, port, point) &&
                   (!modifiers || (cfp_private(ref, 0xb1098, NULL, 0, &current) && cfp_write(modifiers, current)));
        }
        if (*status)
            return 1;

        uint32_t event_class, kind;
        int ok = cfp_private(ref, 0xb3360, &event, 1, &event_class) && cfp_private(ref, 0xb33d8, &event, 1, &kind);
        if (ok && event_class == tracking_key_class) {
            uint32_t cancelled = 0;
            ok = (kind == 4 || cfp_private(ref, 0xb32a0, &event, 1, &cancelled)) && tracking_remove_event(ref, event);
            if (kind == 4 || cancelled) {
                *outcome = CW(kind == 4 ? tracking_modifiers : tracking_cancelled);
                ok = ok && tracking_mouse_point(ref, port, point) && tracking_parameter(ref, event, 0, modifiers);
            }
        }
        else if (ok) {
            ok = ((options & 1 && kind == 2) || tracking_remove_event(ref, event)) &&
                 tracking_parameter(ref, event, 1, point) && tracking_parameter(ref, event, 0, modifiers) &&
                 (port == UINT32_MAX || cfp_private(ref, 0xb1668, &point, 1, NULL));
            if (kind == 1 || kind == 2)
                *outcome = CW(kind);
            else if (kind == 5 || kind == 6)
                *outcome = CW(kind == 5 ? tracking_up : tracking_dragged);
        }
        if (!cfp_private(ref, 0xb33f0, &event, 1, NULL) || !ok)
            return 0;
    }
    return 1;
}

static int track_mouse_location(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call, int with_options)
{
    const uint32_t port = call->words[0], point = call->words[with_options ? 4 : 1];
    const uint32_t modifiers = with_options ? call->words[5] : 0, output = call->words[with_options ? 6 : 2];
    uint16_t* outcome = output ? ROMlib_carbon_try_guest_memory(output, sizeof *outcome, 1) : NULL;
    if (!point || !ROMlib_carbon_try_guest_memory(point, sizeof(Point), 1) || !outcome ||
        (modifiers && !ROMlib_carbon_try_guest_memory(modifiers, sizeof(uint32_t), 1)))
        return result_word(call, paramErr);

    float default_timeout;
    uint32_t previous, status = noErr;
    if (!toc_float(ref, -0x184, 0x30, &default_timeout) || !tracking_set_port(ref, port, &previous))
        return 0;
    const double timeout = with_options ? frame_time(call, 0) : default_timeout;
    *outcome = 0;
    const int ok = track_location_events(ref, call, port, with_options ? call->words[1] : 0, timeout, default_timeout,
                                         point, modifiers, outcome, &status);
    return tracking_finish(ref, port, previous, call, ok, status);
}

static int tracking_region_point(const ROMlib_carbon_reference* ref, uint32_t port, uint32_t region, uint32_t point,
                                 Byte* was_inside, uint16_t* outcome)
{
    uint32_t argument[2], inside;
    argument[1] = region;
    if ((port != UINT32_MAX && !cfp_private(ref, 0xb1668, &point, 1, NULL)) || !cfp_get(point, &argument[0]) ||
        !cfp_private(ref, 0xb1680, argument, NELEM(argument), &inside))
        return 0;
    if (inside != *was_inside) {
        *was_inside = inside != 0;
        *outcome = CW(inside ? tracking_entered : tracking_exited);
    }
    return 1;
}

static int track_mouse_region(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t port = call->words[0], region = call->words[1], state = call->words[2], output = call->words[3];
    Byte* was_inside = state ? ROMlib_carbon_try_guest_memory(state, 1, 1) : NULL;
    uint16_t* outcome = output ? ROMlib_carbon_try_guest_memory(output, sizeof *outcome, 1) : NULL;
    if (!region || !was_inside || !outcome)
        return result_word(call, paramErr);
    uint32_t empty;
    if (!cfp_private(ref, 0xb15c0, &region, 1, &empty))
        return 0;
    if (empty)
        return result_word(call, paramErr);

    float timeout;
    uint32_t filters, previous, status = noErr;
    if (!toc_float(ref, -0x184, 0x30, &timeout) || !toc_pointer(ref, -0xd30, &filters) ||
        !tracking_set_port(ref, port, &previous))
        return 0;
    *outcome = 0;
    _Alignas(8) Point location;
    const uint32_t point = US_TO_SYN68K(&location);
    int ok = tracking_mouse_point(ref, UINT32_MAX, point) &&
             tracking_region_point(ref, port, region, point, was_inside, outcome);
    while (ok && !*outcome) {
        uint32_t event, kind;
        ok = tracking_receive(ref, call, 3, filters, 1, timeout, &event, &status);
        if (!ok || status)
            break;
        ok = cfp_private(ref, 0xb33d8, &event, 1, &kind);
        if (ok && (kind == 1 || kind == 2))
            *outcome = CW(kind);
        else if (ok && kind == 6)
            ok = tracking_parameter(ref, event, 1, point) &&
                 tracking_region_point(ref, port, region, point, was_inside, outcome);
        ok = cfp_private(ref, 0xb33f0, &event, 1, NULL) && ok;
    }
    return tracking_finish(ref, port, previous, call, ok, status);
}

static int quit_event_loop(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    uint32_t loop;
    if (!masked_ref(ref, -0xdc8, call->words[0], &loop))
        return 0;
    if (!loop)
        return result_word(call, paramErr);
    const Byte* record = ROMlib_carbon_guest_memory(loop, 0x24, 0);
    if (!record)
        return 0;
    if (!record[0x18])
        return cfp_private(ref, 0x42710, NULL, 0, NULL) && result_word(call, noErr);
    const uint32_t args[] = {load_word(record, 0x20), 3, 0, 0};
    return cfp_private(ref, 0xb22c8, args, 4, NULL) && result_word(call, noErr);
}

static void store_time(Byte* record, unsigned offset, double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    store_double_bits(record, offset, bits);
}

static int timer_fire_time(const ROMlib_carbon_reference* ref, double delay, int installing, double* result)
{
    float sentinel;
    if (!toc_float(ref, -0x19c, installing ? 0x20 : 0, &sentinel))
        return 0;
    if (!installing && delay == sentinel) {
        if (!toc_float(ref, -0x2540, 0, &sentinel))
            return 0;
        *result = sentinel;
        return 1;
    }
    uint64_t bits;
    if (!read_event_clock(ref, &bits))
        return 0;
    memcpy(result, &bits, sizeof bits);
    if (!installing || delay != sentinel)
        *result += delay;
    return 1;
}

static int install_timer(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t opaque_loop = call->words[0], output = call->words[7];
    uint32_t loop;
    if (!masked_ref(ref, -0xdc8, opaque_loop, &loop))
        return 0;
    if (!loop)
        return result_word(call, paramErr);
    if (output && !ROMlib_carbon_guest_memory(output, 4, 1))
        return 0;
    Byte* record = ROMlib_carbon_guest_memory(loop, 0x20, 1);
    double fire;
    uint32_t timer;
    if (!record || !timer_fire_time(ref, frame_time(call, 0), 1, &fire) ||
        !new_event_manager_object(ref, -0xdb8, &timer))
        return 0;
    if (!timer)
        return result_word(call, memFullErr);
    Byte* entry = ROMlib_carbon_guest_memory(timer, 0x30, 1);
    const uint32_t lock = load_word(record, 0x1c);
    if (!entry || !enter_region(ref, lock)) {
        cfp_private(ref, 0x4bfe8, &timer, 1, NULL);
        return 0;
    }
    store_time(entry, 0x14, fire);
    store_double_bits(entry, 0x1c, call->floating[1]);
    store_word(entry, 0x24, call->words[5]);
    store_word(entry, 0x28, call->words[6]);
    store_word(entry, 0x2c, loop);
    store_word(entry, 0x10, load_word(record, 0x10));
    store_word(record, 0x10, timer);
    return leave_region(ref, lock) && cfp_private(ref, 0x4172c, &opaque_loop, 1, NULL) &&
           (!output || (masked_ref(ref, -0xdc4, timer, &timer) && cfp_write(output, timer))) &&
           result_word(call, noErr);
}

static int remove_timer(const ROMlib_carbon_reference* ref, uint32_t timer, uint32_t loop,
                        ROMlib_carbon_ppc_frame* call)
{
    Byte* record = ROMlib_carbon_guest_memory(loop, 0x20, 1);
    if (!record || !loop)
        return result_word(call, paramErr);
    const uint32_t lock = load_word(record, 0x1c);
    if (!enter_region(ref, lock))
        return 0;
    uint32_t link = loop + 0x10, next;
    int ok = cfp_get(link, &next);
    unsigned links = 0;
    while (ok && next && next != timer) {
        if (++links == UINT16_MAX) {
            ok = ROMlib_carbon_report_failure("Carbon timer list has too many links");
            break;
        }
        link = next + 0x10;
        ok = cfp_get(link, &next);
    }
    if (ok && next == timer) {
        ok = cfp_get(timer + 0x10, &next) && cfp_write(link, next);
        if (ok && load_word(record, 0x14) == timer)
            store_word(record, 0x14, next);
        if (ok)
            ok = cfp_private(ref, 0x4bfe8, &timer, 1, NULL);
    }
    return leave_region(ref, lock) && ok && result_word(call, noErr);
}

static int timer_operation(const ROMlib_carbon_reference* ref, enum carbon_event_operation operation,
                           ROMlib_carbon_ppc_frame* call)
{
    uint32_t timer;
    if (!masked_ref(ref, -0xdc4, call->words[0], &timer))
        return 0;
    if (!timer)
        return result_word(call, paramErr);
    Byte* entry = ROMlib_carbon_guest_memory(timer, 0x30, operation == event_reschedule_timer);
    if (!entry)
        return 0;
    const uint32_t loop = load_word(entry, 0x2c);
    if (operation == event_remove_timer)
        return remove_timer(ref, timer, loop, call);
    double fire;
    uint32_t opaque_loop;
    if (!timer_fire_time(ref, frame_time(call, 0), 0, &fire) || !masked_ref(ref, -0xdc8, loop, &opaque_loop))
        return 0;
    store_time(entry, 0x14, fire);
    return cfp_private(ref, 0x4172c, &opaque_loop, 1, NULL) && result_word(call, noErr);
}

static int copy_toc_record(const ROMlib_carbon_reference* ref, int slot, void* output, uint32_t length)
{
    uint32_t address;
    if (!toc_pointer(ref, slot, &address))
        return 0;
    return cfp_move(US_TO_SYN68K(output), address, length);
}

static int service_current_loop_timers(const ROMlib_carbon_reference* ref, uint32_t* head)
{
    uint32_t loop, opaque;
    return current_manager_object(ref, 1, &loop) && cfp_get(loop + 0x10, head) &&
           (!*head || (masked_ref(ref, -0xdc8, loop, &opaque) && cfp_private(ref, 0x41298, &opaque, 1, NULL)));
}

static int run_application_event_loop(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    _Alignas(16) Byte quit = 0;
    _Alignas(64) Byte filters[5 * 8];
    _Alignas(16) uint32_t handler = 0;
    uint32_t target, procedure, depth_address;
    float timeout;
    if (!copy_toc_record(ref, -0xe5c, filters, sizeof filters) || !toc_pointer(ref, -0xe60, &procedure) ||
        !toc_pointer(ref, -0xe4c, &depth_address) || !toc_float(ref, -0x1a4, 12, &timeout) ||
        !cfp_private(ref, 0xb3570, NULL, 0, &target))
        return 0;
    Byte* depth = ROMlib_carbon_guest_memory(depth_address, sizeof(uint32_t), 1);
    if (!depth)
        return 0;
    const uint32_t install[] = {
        target, procedure, 5, US_TO_SYN68K(filters), US_TO_SYN68K(&quit), US_TO_SYN68K(&handler)};
    if (!cfp_private(ref, 0x4648c, install, NELEM(install), NULL))
        return 0;
    const uint32_t installed = CL(handler);
    if (!installed)
        return 1;

    const int prepared = cfp_private(ref, 0x495fc, NULL, 0, NULL);
    const int activated = prepared && cfp_private(ref, 0x3d93c, NULL, 0, NULL);
    int ok = activated;
    if (activated) {
        store_word(depth, 0, load_word(depth, 0) + 1);
        while (ok && !quit) {
            uint32_t timers;
            ok = service_current_loop_timers(ref, &timers);
            if (!ok || quit)
                break;
            uint32_t event, status;
            ok = wait_for_events(ref, call, timers ? 0.1 : timeout, &status) && take_next_event(ref, 0, 0, 1, &event);
            if (ok && event) {
                const int dispatched = cfp_private(ref, 0xb4bd8, &event, 1, NULL);
                ok = cfp_private(ref, 0xb33f0, &event, 1, NULL) && dispatched;
            }
        }
        store_word(depth, 0, load_word(depth, 0) - 1);
    }
    const int deactivated = !activated || cfp_private(ref, 0x3da64, NULL, 0, NULL);
    const int finished = !prepared || cfp_private(ref, 0x496c4, NULL, 0, NULL);
    const int removed = cfp_private(ref, 0xb35d0, &installed, 1, NULL);
    return ok && deactivated && finished && removed;
}

static int quit_application_event_loop(const ROMlib_carbon_reference* ref, const ROMlib_carbon_ppc_frame* call)
{
    _Alignas(8) uint32_t event_output = 0;
    float event_time;
    if (!toc_float(ref, -0x1a4, 16, &event_time))
        return 0;
    ROMlib_carbon_ppc_frame create = *call;
    create.words[0] = 3;
    create.words[1] = 0;
    create.words[2] = 0;
    create.words[3] = US_TO_SYN68K(&event_output);
    const double time = event_time;
    memcpy(&create.floating[0], &time, sizeof time);
    if (!cfx_private_frame(ref, 0x3a6d0, &create))
        return 0;
    if (create.words[0])
        return 1;
    uint32_t send[] = {CL(event_output), 0};
    const int dispatched =
        cfp_private(ref, 0xb3570, NULL, 0, &send[1]) && cfp_private(ref, 0xb35e8, send, NELEM(send), NULL);
    const int released = cfp_private(ref, 0xb33f0, send, 1, NULL);
    uint32_t loop;
    return dispatched && released && cfp_private(ref, 0xb3450, NULL, 0, &loop) &&
           cfp_private(ref, 0xb3408, &loop, 1, NULL);
}

static int label_event_target(const ROMlib_carbon_reference* ref, uint32_t target, int label_slot)
{
    uint32_t literal, label;
    if (!toc_pointer(ref, label_slot, &literal) || !cfp_call("__CFStringMakeConstantString", &literal, 1, &label))
        return 0;
    const uint32_t arguments[] = {target, label};
    return cfp_private(ref, 0x47580, arguments, 2, NULL);
}

static int get_global_target(const ROMlib_carbon_reference* ref, int field, ROMlib_carbon_ppc_frame* call)
{
    uint32_t slot, target;
    if (!toc_pointer(ref, field, &slot) || !cfp_get(slot, &target))
        return 0;
    if (target)
        return result_word(call, target);
    _Alignas(32) Byte description[24];
    const uint32_t argument = US_TO_SYN68K(description);
    if (!copy_toc_record(ref, field + 4, description, sizeof description) ||
        !cfp_private(ref, 0x456ec, &argument, 1, &target) || !cfp_write(slot, target))
        return 0;
    if (target && field == -0xe58) {
        uint32_t hook;
        if (!toc_pointer(ref, -0xe50, &hook))
            return 0;
        const uint32_t args[] = {target, hook, 0};
        if (!cfp_private(ref, 0x45aa8, args, 3, NULL) || !label_event_target(ref, target, -0x1a4))
            return 0;
    }
    if (target && field == -0xe0c) {
        _Alignas(8) Byte type[8];
        uint32_t hook;
        if (!toc_pointer(ref, -0xe00, &hook) || !copy_toc_record(ref, -0xe04, type, sizeof type))
            return 0;
        const uint32_t args[] = {target, hook, 1, US_TO_SYN68K(type), 0, 0};
        if (!cfp_private(ref, 0x4648c, args, 6, NULL))
            return 0;
    }
    return result_word(call, target);
}

typedef struct {
    uint32_t validate;
    uint32_t get_property;
    uint32_t set_property;
    int descriptor_slot;
    int dispose_slot;
    int label_slot;
} event_target_adapter;

static int label_object_target(const ROMlib_carbon_reference* ref, uint32_t target, uint32_t object, int label_slot)
{
    uint32_t literal, format, label;
    if (!toc_pointer(ref, label_slot, &literal) || !cfp_call("__CFStringMakeConstantString", &literal, 1, &format))
        return 0;
    const uint32_t format_args[] = {0, 0, format, object};
    if (!cfp_call("CFStringCreateWithFormat", format_args, 4, &label))
        return 0;
    if (!label)
        return 1;
    const uint32_t label_args[] = {target, label};
    const int labeled = cfp_private(ref, 0x47580, label_args, 2, NULL);
    return cfp_call("CFRelease", &label, 1, NULL) && labeled;
}

static int get_object_target(const ROMlib_carbon_reference* ref, int kind, ROMlib_carbon_ppc_frame* call)
{
    static const event_target_adapter adapters[] = {
        {0xb2c28, 0xb4530, 0xb4500, -0xc38, -0xc34, -0x164},
        {0xb1b30, 0xb45c0, 0xb45d8, 0, -0xedc, -0x1bc},
        {0x6e8dc, 0xb4608, 0xb4650, -0xe14, -0xe10, 0},
    };
    const event_target_adapter* adapter = &adapters[kind];
    const uint32_t object = call->words[0], event_tag = UINT32_C(0x65766e74);
    if (!object)
        return result_word(call, 0);
    uint32_t valid;
    if (!cfp_private(ref, adapter->validate, &object, 1, &valid))
        return 0;
    if (!valid)
        return result_word(call, 0);
    _Alignas(8) uint32_t property = 0, size = 0;
    const uint32_t value_out = US_TO_SYN68K(&property), size_out = US_TO_SYN68K(&size);
    const uint32_t menu_get[] = {object, 0, event_tag, 4, size_out, value_out};
    const uint32_t get[] = {object, event_tag, 4, size_out, value_out};
    uint32_t status;
    if (!cfp_private(ref, adapter->get_property, kind == 2 ? menu_get : get, kind == 2 ? 6 : 5, &status))
        return 0;
    if ((int32_t)status != -5604)
        return result_word(call, status ? 0 : CL(property));

    _Alignas(32) Byte description[24] = {0};
    if (adapter->descriptor_slot) {
        if (!copy_toc_record(ref, adapter->descriptor_slot, description, sizeof description))
            return 0;
    }
    else {
        uint32_t methods;
        if (!toc_pointer(ref, -0xee0, &methods))
            return 0;
        store_word(description, 12, methods);
    }
    store_word(description, 4, object);
    if (kind == 0) {
        const uint32_t get_class[] = {object, UINT32_C(0x6f636c73), 4, size_out, value_out};
        if (!cfp_private(ref, adapter->get_property, get_class, 5, &status))
            return 0;
        store_word(description, 8, status ? 0 : CL(property));
    }
    const uint32_t argument = US_TO_SYN68K(description);
    uint32_t target, disposal;
    if (!cfp_private(ref, 0x456ec, &argument, 1, &target))
        return 0;
    if (!target)
        return result_word(call, 0);
    if (!toc_pointer(ref, adapter->dispose_slot, &disposal))
        return 0;
    const uint32_t observe[] = {event_tag, object, disposal};
    if (!cfp_private(ref, 0x458f8, observe, 3, NULL) ||
        (adapter->label_slot && !label_object_target(ref, target, object, adapter->label_slot)))
        return 0;

    property = CL(target);
    const uint32_t menu_set[] = {object, 0, event_tag, 4, value_out};
    const uint32_t set[] = {object, event_tag, 4, value_out};
    return cfp_private(ref, adapter->set_property, kind == 2 ? menu_set : set, kind == 2 ? 5 : 4, NULL) &&
           result_word(call, target);
}

static int install_standard_handler(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t target = call->words[0];
    if (!target)
        return result_word(call, paramErr);
    _Alignas(8) uint32_t event_word;
    ROMlib_carbon_ppc_frame creation = {0};
    creation.words[1] = UINT32_C(0x65747267);
    creation.words[2] = 2;
    creation.words[6] = US_TO_SYN68K(&event_word);
    if (!create_event(ref, &creation))
        return 0;
    uint32_t status = creation.words[0];
    if (status)
        return result_word(call, status);
    _Alignas(16) uint32_t context[] = {0, 0, 0, CL(1)};
    const uint32_t event = CL(event_word);
    const uint32_t dispatch[] = {event, target, US_TO_SYN68K(context)};
    const int delivered = cfp_private(ref, 0x46994, dispatch, 3, &status);
    if (!cfp_private(ref, 0xb33f0, &event, 1, NULL) || !delivered)
        return 0;
    if ((int32_t)status != -9874)
        return result_word(call, status);
    uint32_t hook, object;
    if (!cfp_get(target + 0xc, &hook) || !cfp_get(target, &object))
        return 0;
    if (hook) {
        const uint32_t args[] = {target, object};
        uint32_t installed;
        if (!cfp_callback(hook, args, 2, NULL) || !cfp_get(target + 0x10, &installed) ||
            (!installed && !cfp_write(target + 0xc, 0)))
            return 0;
    }
    return result_word(call, noErr);
}

static int wait_next_event(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    uint32_t depth_address, initialized_address, delegate_address;
    if (!toc_pointer(ref, -0xeb8, &depth_address) || !toc_pointer(ref, -0xeb4, &initialized_address) ||
        !toc_pointer(ref, -0xeb0, &delegate_address))
        return 0;
    Byte* depth = ROMlib_carbon_guest_memory(depth_address, 4, 1);
    Byte* initialized = ROMlib_carbon_guest_memory(initialized_address, 1, 1);
    if (!depth || !initialized)
        return 0;

    store_word(depth, 0, load_word(depth, 0) + 1);
    uint32_t delegate, result;
    int ok;
    if (*initialized)
        ok = cfp_get(delegate_address, &delegate);
    else {
        uint32_t strings;
        ok = toc_pointer(ref, -0x1b0, &strings);
        if (ok) {
            const uint32_t names[] = {strings + 0x42, strings + 0x50};
            ok = cfp_private(ref, 0x55c5c, names, 2, &delegate) && cfp_write(delegate_address, delegate);
        }
        if (ok)
            *initialized = 1;
    }
    if (ok) {
        const uint32_t args[] = {(uint16_t)call->words[0], call->words[1], call->words[2], call->words[3]};
        ok = cfp_callback(delegate, args, 4, &result);
    }
    /* A callback can re-enter Carbon or fail while the wait is active. */
    store_word(depth, 0, load_word(depth, 0) - 1);
    return ok && result_word(call, result);
}

/* Use the manager's private property namespace, shared with the saved exports. */
static int read_modal_state(const ROMlib_carbon_reference* ref, uint32_t window, uint32_t* token, uint32_t* status)
{
    _Alignas(8) uint32_t encoded = 0;
    uint32_t valid;
    if (!cfp_private(ref, 0xb2c28, &window, 1, &valid))
        return 0;
    if (!valid) {
        *status = paramErr;
        return 1;
    }
    const uint32_t args[] = {window, modal_state_property, sizeof encoded, 0, US_TO_SYN68K(&encoded)};
    if (!cfp_private(ref, 0xb4530, args, 5, status))
        return 0;
    *token = CL(encoded);
    return 1;
}

static int begin_modal(const ROMlib_carbon_reference* ref, uint32_t window, uint32_t* status)
{
    uint32_t valid, visible, token;
    if (!cfp_private(ref, 0xb2c28, &window, 1, &valid))
        return 0;
    if (!valid) {
        *status = paramErr;
        return 1;
    }
    if (!cfp_private(ref, 0xb2dc0, &window, 1, &visible))
        return 0;
    if (!visible) {
        *status = window_not_visible;
        return 1;
    }
    if (!read_modal_state(ref, window, &token, status))
        return 0;
    if (*status != (uint32_t)window_property_not_found) {
        *status = *status == noErr ? window_already_modal : window_wrong_state;
        return 1;
    }
    _Alignas(8) uint32_t encoded = 0;
    const uint32_t create[] = {window, US_TO_SYN68K(&encoded)};
    if (!cfp_private(ref, 0xb4bf0, create, 2, status))
        return 0;
    if (*status)
        return 1;
    const uint32_t property[] = {window, modal_state_property, sizeof encoded, US_TO_SYN68K(&encoded)};
    if (!cfp_private(ref, 0xb4500, property, 4, status))
        return 0;
    token = CL(encoded);
    return !*status || cfp_private(ref, 0xb4c08, &token, 1, NULL);
}

static int end_or_quit_modal(const ROMlib_carbon_reference* ref, uint32_t window, int end)
{
    uint32_t token, status;
    if (!read_modal_state(ref, window, &token, &status))
        return 0;
    if (status || !token)
        return 1;
    if (end) {
        const uint32_t property[] = {window, modal_state_property};
        return cfp_private(ref, 0xb4518, property, 2, NULL) && cfp_private(ref, 0xb4c08, &token, 1, NULL);
    }
    Byte* running = ROMlib_carbon_guest_memory(token + 4, 1, 1);
    if (!running)
        return 0;
    *running = 0;
    return 1;
}

static int modal_pump(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call, uint32_t window,
                      uint32_t token)
{
    if (!token)
        return 1;
    Byte* running = ROMlib_carbon_guest_memory(token + 4, 1, 1);
    if (!running)
        return 0;
    *running = 1;

    for (;;) {
        uint32_t timers;
        if (!service_current_loop_timers(ref, &timers))
            return 0;

        /* A callback may quit the loop or destroy its modal record. Recheck
           the property before reading the running flag again. */
        uint32_t current, status, event;
        if (!read_modal_state(ref, window, &current, &status))
            return 0;
        if (status || current != token || !*running)
            return 1;

        /* A timer can end the loop while the Toolbox is waiting for input.
           A finite wait allows it to return even when no event was posted. */
        if (!wait_for_events(ref, call, 0.1, &status) || !take_next_event(ref, 0, 0, 1, &event))
            return 0;
        if (event) {
            const int dispatched = cfp_private(ref, 0x3dcfc, &event, 1, NULL);
            if (!cfp_private(ref, 0xb33f0, &event, 1, NULL) || !dispatched)
                return 0;
        }
    }
}

static int run_modal(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t window = call->words[0];
    uint32_t token, status;
    if (!read_modal_state(ref, window, &token, &status))
        return 0;
    if (!status)
        return modal_pump(ref, call, window, token) && result_word(call, noErr);
    if (status == (uint32_t)paramErr)
        return result_word(call, status);

    if (!cfp_private(ref, 0xb3a08, &window, 1, NULL))
        return 0;
    int ok = begin_modal(ref, window, &status);
    if (ok && !status) {
        ok = read_modal_state(ref, window, &token, &status);
        if (ok && !status)
            ok = modal_pump(ref, call, window, token);
        const int ended = end_or_quit_modal(ref, window, 1);
        ok = ok && ended;
    }
    const int released = cfp_private(ref, 0xb3a20, &window, 1, NULL);
    return ok && released && result_word(call, status);
}

static int window_modality(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call, int set)
{
    const uint32_t window = call->words[0], value = call->words[1], unavailable = call->words[2];
    uint32_t valid, status;
    if (!cfp_private(ref, 0xb2c28, &window, 1, &valid))
        return 0;
    if (!valid)
        return result_word(call, window_invalid);
    _Alignas(16) uint32_t mode[2] = {0, 0};
    const uint32_t address = US_TO_SYN68K(mode);
    const uint32_t read[] = {window, modality_property, sizeof mode, 0, address};
    if (!set) {
        if (!value)
            return result_word(call, paramErr);
        if (!cfp_private(ref, 0xb4530, read, 5, &status))
            return 0;
        return cfp_write(value, status ? modality_none : CL(mode[0])) &&
               (!unavailable || cfp_write(unavailable, status ? 0 : CL(mode[1]))) && result_word(call, noErr);
    }
    if (value > modality_window || (value == modality_window && !unavailable))
        return result_word(call, paramErr);
    /* The Toolbox tests this window-record flag when routing system-modal events. */
    Byte* flags = ROMlib_carbon_guest_memory(window + 0x71, 1, 1);
    if (!flags)
        return 0;
    if (value != modality_system) {
        if (!cfp_private(ref, 0xb4530, read, 5, &status))
            return 0;
        if (!status && CL(mode[0]) == modality_system)
            *flags &= 0x7f;
    }
    if (value == modality_none) {
        const uint32_t property[] = {window, modality_property};
        return cfp_private(ref, 0xb4518, property, 2, NULL) && result_word(call, noErr);
    }
    mode[0] = CL(value);
    mode[1] = CL(unavailable);
    const uint32_t property[] = {window, modality_property, sizeof mode, address};
    if (!cfp_private(ref, 0xb4500, property, 4, &status))
        return 0;
    if (!status && value == modality_system)
        *flags |= 0x80;
    return result_word(call, status);
}

int ROMlib_carbon_event_access(const char* name, uint32_t offset, enum carbon_event_operation operation, int field,
                               ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", name, &ref))
        return 0;
    if (ref.code_offset != offset || ref.code_size != 0xb8fb4 || !ref.toc || ref.toc_offset != 0x2f78 ||
        ref.toc_size != 0x10f0c)
        return ROMlib_carbon_invoke_frame(ref.function, call);

    uint32_t result;
    switch (operation) {
    case event_clock:
        return read_event_clock(&ref, call->floating);
    case event_wait_next:
        return wait_next_event(&ref, call);
    case event_in_classic_mask:
        return is_event_in_classic_mask(&ref, call);
    case event_convert_classic_record:
        return convert_classic_event(&ref, call);
    case event_flush_classic_queue:
        return flush_classic_queue(&ref, call);
    case event_classic_mouse_button:
        return classic_mouse_button(&ref, field, call);
    case event_classic_available:
    case event_classic_os_available:
    case event_classic_os_get:
        return classic_os_event(&ref, operation, field, call);
    case event_is_cancel_key:
        return is_cancel_key_event(&ref, call);
    case event_check_queue_for_cancel:
        return check_event_queue_for_cancel(&ref, call);
    case event_is_context_menu:
        return is_context_menu_event(&ref, call);
    case event_could_be_menu_key:
        return could_be_menu_key_event(&ref, call);
    case event_control_drag:
        return control_drag(&ref, field, call);
    case event_receive_next:
        return receive_next_event(&ref, call, 0);
    case event_run_current_loop:
        return run_current_event_loop(&ref, call);
    case event_run_application_loop:
        return run_application_event_loop(&ref, call);
    case event_quit_application_loop:
        return quit_application_event_loop(&ref, call);
    case event_track_mouse_location:
    case event_track_mouse_location_options:
        return track_mouse_location(&ref, call, operation == event_track_mouse_location_options);
    case event_track_mouse_region:
        return track_mouse_region(&ref, call);
    case event_quit_loop:
        return quit_event_loop(&ref, call);
    case event_begin_modal: {
        const int ok = begin_modal(&ref, call->words[0], &result);
        return ok && result_word(call, result);
    }
    case event_run_modal:
        return run_modal(&ref, call);
    case event_end_modal:
    case event_quit_modal:
        return end_or_quit_modal(&ref, call->words[0], operation == event_end_modal) && result_word(call, noErr);
    case event_get_modality:
    case event_set_modality:
        return window_modality(&ref, call, operation == event_set_modality);
    case event_global_time:
        return toc_pointer(&ref, field, &result) && read_double_bits(result, call->floating);
    case event_key_modifiers: {
        if (!cfp_private(&ref, 0xb5880, NULL, 0, &result) || !cfp_get(result + 4, &result))
            return 0;
        const uint16_t key_bits = (uint16_t)result;
        return result_word(call, (((uint16_t)(key_bits << 1) | (key_bits >> 15)) & 0xff) << 8);
    }
    case event_masked_global:
        return global_word(&ref, field, &result) && masked_ref(&ref, -0xdc8, result, &result) &&
               result_word(call, result);
    case event_current_loop:
        return current_manager_object(&ref, 1, &result) && masked_ref(&ref, -0xdc8, result, &result) &&
               result_word(call, result);
    case event_main_queue:
    case event_current_queue:
        return get_queue(&ref, call, operation == event_current_queue);
    case event_post_queue:
    case event_count_queue:
    case event_is_queued:
    case event_remove_queue:
        return queue_operation(&ref, operation, call);
    case event_flush_queue:
    case event_flush_matching_queue:
        return flush_event_queue(&ref, call, operation == event_flush_matching_queue);
    case event_find_in_queue:
    case event_flush_matching_callback:
        return compare_queued_events(&ref, call, operation == event_flush_matching_callback);
    case event_install_timer:
        return install_timer(&ref, call);
    case event_remove_timer:
    case event_reschedule_timer:
        return timer_operation(&ref, operation, call);
    case event_target:
        return get_global_target(&ref, field, call);
    case event_object_target:
        return get_object_target(&ref, field, call);
    case event_standard_handler:
        return install_standard_handler(&ref, call);
    case event_install_handler:
        return install_handler(&ref, call);
    case event_remove_handler:
        return remove_handler(&ref, call);
    case event_add_types:
    case event_remove_types:
        return edit_handler_types(&ref, call, operation == event_add_types);
    case event_dispatch:
    case event_dispatch_next:
        return dispatch_event(&ref, call, operation == event_dispatch_next);
    case event_create:
        return create_event(&ref, call);
    default:
        return event_record_access(&ref, operation, field, call);
    }
}
