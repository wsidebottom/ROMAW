/*+===================================================================
  File:      encoding_converter.c

  Summary:   Implements Text Encoding Converter catalogue and streaming contexts.

  Classes:   None.

  Functions: ROMlib_carbon_tec.

  Origin:    Mac OS 9.2.1 TextEncodingConverter fragment and Universal Interfaces
             3.4.2 TextEncodingConverter.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "encoding_converter.h"
#include "cf_batch.h"

enum {
    tec_signature = 0x656e6376,
    tec_sniffer_signature = 0x736e6966,
    tec_serial_path = 0x6f746f6f,
    tec_multi_path = 0x6f746f6d,
    tec_bad_context = -8748,
    tec_no_path = -8749,
    tec_invalid_encoding = 0xfff,
    tec_missing_encoding = -8738,
    tec_missing_annotation = -8771,
    tec_stages = 0x2c,
    tec_first_stage = 0x30,
    tec_stage_size = 112,
};

static int tec_result(ROMlib_carbon_ppc_frame* call, uint32_t status)
{
    call->words[0] = status;
    return 1;
}

static int read_short(uint32_t address, uint16_t* value)
{
    const void* data = ROMlib_carbon_guest_memory(address, 2, 0);
    if (!data)
        return 0;
    uint16_t encoded;
    memcpy(&encoded, data, sizeof encoded);
    *value = CW(encoded);
    return 1;
}

static int read_record(uint32_t handle, uint32_t* record, uint32_t* status)
{
    uint32_t tag = 0;
    *record = 0;
    if (handle && (!cfp_get(handle, record) || (*record && !cfp_get(*record, &tag))))
        return 0;
    *status = tag == tec_signature ? noErr : tec_bad_context;
    return 1;
}

static int lock_converter(const ROMlib_carbon_reference* ref, uint32_t handle)
{
    return cfp_private(ref, 0x59bc, &handle, 1, NULL);
}

static int unlock_converter(const ROMlib_carbon_reference* ref, uint32_t handle)
{
    return cfp_private(ref, 0x59d4, &handle, 1, NULL);
}

static int dispose_handle(const ROMlib_carbon_reference* ref, uint32_t handle)
{
    return cfp_private(ref, 0x59a4, &handle, 1, NULL);
}

static int plugin_method(uint32_t plugin, unsigned method, uint32_t* vector)
{
    uint32_t methods;
    return plugin && cfp_get(plugin, &methods) && methods && cfp_get(methods + method, vector) && *vector;
}

static int change_stages(const ROMlib_carbon_reference* ref, uint32_t handle, uint32_t record, int disposing,
                         uint32_t* status)
{
    uint32_t count;
    if (!cfp_get(record + tec_stages, &count) || count > UINT16_MAX || !lock_converter(ref, handle))
        return 0;
    if (!cfp_get(handle, &record))
        return 0;

    int ok = 1;
    for (uint32_t i = 0; i < count && ok && (disposing || !*status); ++i) {
        const uint32_t stage = record + tec_first_stage + i * tec_stage_size;
        uint32_t plugin, vector, answer;
        const uint32_t args[] = {handle, stage};
        ok = cfp_get(stage, &plugin);
        if (ok && plugin) {
            ok = plugin_method(plugin, disposing ? 0x1c : 0x10, &vector) && cfp_callback(vector, args, 2, &answer);
            if (ok && !disposing)
                *status = answer;
        }
        if (ok && disposing) {
            const Byte* active = ROMlib_carbon_guest_memory(stage + 0x60, 1, 0);
            ok = active != NULL;
            if (active && *active) {
                uint32_t intermediate;
                ok = cfp_get(stage + 0x64, &intermediate) && dispose_handle(ref, intermediate) &&
                     cfp_clear(stage + 0x60, 1);
                *status = -8784;
            }
        }
    }
    if (ok && disposing) {
        uint32_t encodings;
        ok = cfp_get(record + 0x18, &encodings) && cfp_write(record, 0) &&
             (!encodings || dispose_handle(ref, encodings));
    }
    const int unlocked = unlock_converter(ref, handle);
    return ok && unlocked && (!disposing || dispose_handle(ref, handle));
}

static int start_stage(uint32_t handle_word, uint32_t converter, uint32_t stage, uint32_t plugin, uint32_t source,
                       uint32_t destination, uint32_t* status)
{
    uint32_t method;
    const uint32_t args[] = {handle_word, stage, source, destination};
    if (!cfp_write(stage, plugin) || !plugin_method(plugin, 0x0c, &method) || !cfp_callback(method, args, 4, status))
        return 0;
    if (!*status) {
        uint32_t count;
        return cfp_get(converter + tec_stages, &count) && cfp_write(converter + tec_stages, count + 1);
    }
    return 1;
}

static int begin_stage(uint32_t handle_word, uint32_t converter, uint32_t stage, uint32_t plugin, uint32_t source,
                       uint32_t destination, uint32_t* status)
{
    return cfp_write(stage + 4, source) && cfp_write(stage + 8, destination) &&
           start_stage(handle_word, converter, stage, plugin, source, destination, status);
}

typedef struct {
    uint32_t address;
    uint32_t count;
} tec_destinations;

static int install_destinations(const ROMlib_carbon_reference* ref, uint32_t record, const tec_destinations* targets,
                                uint32_t* status)
{
    if (!targets)
        return cfp_write(record + 4, tec_serial_path);
    const uint32_t bytes = targets->count * 4;
    uint32_t list, destination;
    if (!cfp_write(record + 4, tec_multi_path) || !cfp_write(record + 0x14, targets->count) ||
        !cfp_private(ref, 0x5a4c, &bytes, 1, &list) || !cfp_write(record + 0x18, list) ||
        !cfp_private(ref, 0x4394, NULL, 0, status))
        return 0;
    if (*status)
        return 1;
    if (!list) {
        *status = memFullErr;
        return 1;
    }
    if (!lock_converter(ref, list))
        return 0;
    const int copied = cfp_get(list, &destination) && cfp_move(destination, targets->address, bytes);
    return unlock_converter(ref, list) && copied;
}

static int create_from_path(const ROMlib_carbon_reference* ref, uint32_t output, uint32_t path, uint32_t length,
                            const tec_destinations* targets, uint32_t* status)
{
    if (!output) {
        *status = paramErr;
        return 1;
    }
    if (!cfp_write(output, 0))
        return 0;
    if (!path || length < 2 || length > UINT16_MAX) {
        *status = paramErr;
        return 1;
    }
    if (!ROMlib_carbon_guest_memory(path, length * 4u, 0))
        return 0;
    uint32_t source;
    if (!cfp_get(path, &source) || !cfp_private(ref, 0x59ec, &source, 1, &source))
        return 0;

    _Alignas(8) uint32_t allocated = 0;
    const uint32_t handle_word = US_TO_SYN68K(&allocated), allocate_args[] = {handle_word, length - 1};
    if (!cfp_private(ref, 0x39b8, allocate_args, 2, status))
        return 0;
    uint32_t handle = CL(allocated), converter;
    if (*status)
        return !handle || cfp_private(ref, 0x57c4, &handle, 1, NULL);
    if (!handle || !lock_converter(ref, handle) || !cfp_get(handle, &converter))
        return 0;
    int ok = install_destinations(ref, converter, targets, status);
    for (uint32_t i = 1; i < length && ok && !*status; ++i) {
        uint32_t destination;
        _Alignas(8) uint32_t plug = 0;
        ok = cfp_get(path + i * 4, &destination) && cfp_private(ref, 0x59ec, &destination, 1, &destination);
        if (!ok)
            break;
        const uint32_t locate[] = {source, destination, US_TO_SYN68K(&plug)};
        ok = cfp_private(ref, 0x3b5c, locate, 3, status);
        if (ok && !*status)
            ok = begin_stage(handle_word, converter, converter + tec_first_stage + (i - 1) * tec_stage_size, CL(plug),
                             source, destination, status);
        source = destination;
    }
    handle = CL(allocated);
    const int unlocked = unlock_converter(ref, handle);
    if (!ok || !unlocked)
        return 0;
    if (*status)
        return cfp_private(ref, 0x57c4, &handle, 1, NULL);
    return cfp_write(output, handle) && cfp_private(ref, 0x57f4, &handle, 1, NULL);
}

static int lookup_path(const ROMlib_carbon_reference* ref, uint32_t output, uint32_t source, uint32_t destination,
                       const tec_destinations* targets, uint32_t* status)
{
    const uint32_t allocate[] = {4, 20};
    uint32_t list, path = 0;
    if (!cfp_private(ref, 0x43bc, allocate, 2, &list))
        return 0;
    if (!list) {
        *status = memFullErr;
        return 1;
    }
    const uint32_t args[] = {source, destination, list};
    int ok = cfp_private(ref, 0x41cc, args, 3, status);
    if (ok && !*status)
        ok = cfp_private(ref, 0x4274, &list, 1, &path);
    if (ok && !*status) {
        if (!path)
            *status = tec_no_path;
        else {
            uint32_t record;
            uint16_t count;
            if (!lock_converter(ref, path))
                ok = 0;
            else {
                ok = cfp_get(path, &record) && read_short(record, &count) &&
                     create_from_path(ref, output, record + 8, count, targets, status);
                const int unlocked = unlock_converter(ref, path);
                ok = ok && unlocked;
            }
        }
    }
    return cfp_private(ref, 0x4308, &list, 1, NULL) && ok;
}

static int create_converter(const ROMlib_carbon_reference* ref, int multiple, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t output = call->words[0], input = call->words[1];
    const uint32_t target = multiple ? tec_invalid_encoding : call->words[2];
    const tec_destinations encodings = {call->words[3], call->words[2]};
    const tec_destinations* targets = multiple ? &encodings : NULL;
    if (!output || (!multiple && (input == tec_invalid_encoding || target == tec_invalid_encoding)) ||
        (multiple && (!encodings.count || encodings.count > INT32_MAX / 4 || !encodings.address)))
        return tec_result(call, paramErr);
    if (multiple && !ROMlib_carbon_guest_memory(encodings.address, encodings.count * 4, 0))
        return 0;
    uint32_t source, destination = target, status;
    if (!cfp_write(output, 0) || !cfp_private(ref, 0x59ec, &input, 1, &source) ||
        (!multiple && !cfp_private(ref, 0x59ec, &target, 1, &destination)))
        return 0;

    _Alignas(8) uint32_t plug = 0, allocated = 0;
    const uint32_t lookup[] = {source, destination, US_TO_SYN68K(&plug)};
    if (!cfp_private(ref, 0x3b5c, lookup, 3, &status))
        return 0;
    if (!plug)
        return lookup_path(ref, output, source, destination, targets, &status) && tec_result(call, status);

    const uint32_t handle_word = US_TO_SYN68K(&allocated), args[] = {handle_word, 1};
    if (!cfp_private(ref, 0x39b8, args, 2, &status))
        return 0;
    uint32_t handle = CL(allocated), converter;
    if (status)
        return (!handle || dispose_handle(ref, handle)) && tec_result(call, status);
    if (!handle || !lock_converter(ref, handle) || !cfp_get(handle, &converter))
        return 0;
    const uint32_t stage = converter + tec_first_stage;
    int initialized = install_destinations(ref, converter, targets, &status);
    if (initialized && !status)
        initialized = multiple ? start_stage(handle_word, converter, stage, CL(plug), source, destination, &status)
                               : begin_stage(handle_word, converter, stage, CL(plug), source, destination, &status);
    handle = CL(allocated);
    const int ok = initialized && (status || (cfp_write(output, handle) && cfp_private(ref, 0x57f4, &handle, 1, NULL)));
    if (multiple && (status || !ok)) {
        const int unlocked = unlock_converter(ref, handle);
        return cfp_private(ref, 0x57c4, &handle, 1, NULL) && unlocked && ok && tec_result(call, status);
    }
    return unlock_converter(ref, handle) && (ok && (!status || dispose_handle(ref, handle))) &&
           tec_result(call, status);
}

typedef struct {
    uint32_t bytes;
    uint32_t capacity;
    uint32_t used;
    uint32_t reserved;
} tec_buffer;

static int convert_buffers(const ROMlib_carbon_reference* ref, uint32_t handle, uint32_t record, const uint32_t* args,
                           int multiple, uint32_t* status)
{
    const uint32_t source = args[1], source_size = args[2], destination = args[4], destination_size = args[5];
    if (!destination || !destination_size) {
        *status = paramErr;
        return 1;
    }
    if ((source_size && (!source || !ROMlib_carbon_guest_memory(source, source_size, 0))) ||
        !ROMlib_carbon_guest_memory(destination, destination_size, 1))
        return 0;
    if (multiple) {
        uint32_t signature;
        if (!cfp_get(record + 4, &signature))
            return 0;
        if (signature != tec_multi_path) {
            *status = paramErr;
            return 1;
        }
        if (args[8] > UINT32_MAX / 8 || (args[8] && !ROMlib_carbon_guest_memory(args[7], args[8] * 8, 1)))
            return 0;
    }
    _Alignas(16) tec_buffer input = {CL(source), CL(source_size), 0, 0};
    _Alignas(16) tec_buffer output = {CL(destination), CL(destination_size), 0, 0};
    _Alignas(16) tec_buffer runs = {0};
    if (multiple) {
        runs.bytes = CL(args[7]);
        runs.capacity = CL(args[8]);
    }
    if (!lock_converter(ref, handle))
        return 0;
    int ok = cfp_get(handle, &record) && cfp_write(record + 0x1c, US_TO_SYN68K(&input)) &&
             cfp_write(record + 0x20, US_TO_SYN68K(&output)) &&
             (!multiple || cfp_write(record + 0x28, US_TO_SYN68K(&runs))) &&
             cfp_private(ref, 0x4cc0, &handle, 1, status);
    if (ok)
        ok = cfp_write(args[3], CL(input.used)) && cfp_write(args[6], CL(output.used)) &&
             (!multiple || cfp_write(args[9], CL(runs.used) / 8));
    const int unlocked = unlock_converter(ref, handle);
    return ok && unlocked;
}

static int flush_buffers(const ROMlib_carbon_reference* ref, uint32_t record, const uint32_t* args, int multiple,
                         uint32_t* status)
{
    uint32_t flags;
    if (!cfp_get(record + 8, &flags) || !cfp_write(record + 8, flags | 1))
        return 0;
    _Alignas(8) uint32_t consumed = 0;
    uint32_t worker[10] = {args[0], 0, 0, US_TO_SYN68K(&consumed), args[1], args[2], args[3]};
    if (multiple) {
        worker[7] = args[4];
        worker[8] = args[5];
        worker[9] = args[6];
    }
    const int flushed = cfp_write(args[3], 0) && (!multiple || cfp_write(args[6], 0)) &&
                        convert_buffers(ref, args[0], record, worker, multiple, status);
    return cfp_get(args[0], &record) && cfp_get(record + 8, &flags) && cfp_write(record + 8, flags & ~1u) && flushed;
}

static uint32_t encoding_key(uint32_t encoding)
{
    return ((encoding & 0xffff) << 14) | ((encoding >> 12) & 0x3ff0) | ((encoding >> 26) & 0xf);
}

static int plugin_list(const ROMlib_carbon_reference* ref, uint32_t* globals, uint16_t* count)
{
    return cfp_private(ref, 0x1fa4, NULL, 0, globals) && *globals && read_short(*globals, count);
}

static int plugin_at(const ROMlib_carbon_reference* ref, uint32_t globals, uint16_t index, uint32_t* plugin)
{
    const uint32_t find[] = {globals, index};
    return cfp_private(ref, 0x3aa4, find, 2, plugin);
}

static int normalize_encoding(const ROMlib_carbon_reference* ref, uint32_t encoding, uint32_t* normalized)
{
    _Alignas(8) uint32_t value = 0;
    const uint32_t args[] = {encoding, US_TO_SYN68K(&value)};
    if (!cfp_private(ref, 0x245c, args, 2, NULL))
        return 0;
    *normalized = CL(value);
    return 1;
}

static int available_entries(const ROMlib_carbon_reference* ref, int operation, const uint32_t* args, uint32_t* status)
{
    const int only_count = operation <= tec_count_sniffers;
    const int direct = operation == tec_count_direct || operation == tec_get_direct;
    const int sniffers = operation == tec_count_sniffers || operation == tec_get_sniffers;
    const uint32_t output = only_count ? 0 : args[0], capacity = only_count ? 0 : args[1];
    const uint32_t written = args[only_count ? 0 : 2];
    const uint32_t entry_bytes = direct ? 12 : 4;
    if (!written) {
        *status = paramErr;
        return 1;
    }
    if (!cfp_write(written, 0))
        return 0;
    if (!only_count && (!capacity || !output || capacity > UINT32_MAX / entry_bytes)) {
        *status = paramErr;
        return 1;
    }
    if (capacity && !ROMlib_carbon_guest_memory(output, capacity * entry_bytes, 1))
        return 0;

    uint32_t globals;
    uint16_t total;
    if (!plugin_list(ref, &globals, &total))
        return 0;
    uint32_t offset = 0;
    *status = noErr;
    for (uint16_t i = 0; i < total && !*status && (only_count || offset < capacity); ++i) {
        uint32_t plugin;
        if (!plugin_at(ref, globals, i, &plugin))
            return 0;
        _Alignas(8) uint32_t found = 0;
        const uint32_t query[] = {plugin, output ? output + offset * entry_bytes : 0,
                                  capacity - (only_count ? 0 : offset), US_TO_SYN68K(&found)};
        if (!cfp_private(ref, direct ? 0 : (sniffers ? 0xc78 : 0x188), query, 4, status))
            return 0;
        const uint32_t count = CL(found);
        if (count > UINT32_MAX - offset)
            return 0;
        offset += count;
        if (!only_count && offset > capacity)
            offset = capacity;
    }
    if (!only_count && !direct && (offset || !*status)) {
        const uint32_t sort[] = {output, offset};
        if (!cfp_private(ref, 0x45f0, sort, 2, NULL) || !cfp_private(ref, 0x46d4, sort, 2, &offset))
            return 0;
    }
    if (!only_count && direct && offset > 1) {
        /* The saved direct query sorts by destination encoding and moves the
           whole entry, including source encoding and plugin metadata. */
        Byte* entries = ROMlib_carbon_guest_memory(output, offset * entry_bytes, 1);
        if (!entries)
            return 0;
        for (uint32_t gap = offset / 2; gap; gap /= 2)
            for (uint32_t i = gap; i < offset; ++i)
                for (uint32_t j = i; j >= gap; j -= gap) {
                    uint32_t left, right;
                    memcpy(&left, entries + (j - gap) * 12 + 4, 4);
                    memcpy(&right, entries + j * 12 + 4, 4);
                    if (encoding_key(CL(left)) <= encoding_key(CL(right)))
                        break;
                    Byte snapshot[12];
                    memcpy(snapshot, entries + j * 12, 12);
                    memcpy(entries + j * 12, entries + (j - gap) * 12, 12);
                    memcpy(entries + (j - gap) * 12, snapshot, 12);
                }
    }
    return cfp_write(written, offset);
}

static int internet_name(const ROMlib_carbon_reference* ref, const uint32_t* args, int from_name, uint32_t* status)
{
    const uint32_t name = args[1];
    if (!name || (from_name && !args[0])) {
        *status = paramErr;
        return 1;
    }
    uint32_t globals;
    uint16_t total;
    uint32_t encoding = 0;
    if ((!from_name && (!cfp_clear(name, 1) || !normalize_encoding(ref, args[0], &encoding))) ||
        !plugin_list(ref, &globals, &total))
        return 0;
    *status = from_name ? tec_missing_encoding : noErr;
    for (uint16_t i = 0; i < total; ++i) {
        uint32_t plugin;
        if (!plugin_at(ref, globals, i, &plugin))
            return 0;
        const uint32_t query[] = {plugin, from_name ? args[0] : encoding, name};
        if (!cfp_private(ref, from_name ? 0x740 : 0x5f4, query, 3, status))
            return 0;
        if (from_name ? *status == noErr : (*status && (int32_t)*status != tec_missing_encoding))
            break;
        if (!from_name) {
            const Byte* length = ROMlib_carbon_guest_memory(name, 1, 0);
            if (!length)
                return 0;
            if (*length)
                break;
        }
    }
    return 1;
}

static int labelled_encodings(const ROMlib_carbon_reference* ref, unsigned query, int count_only, const uint32_t* args,
                              uint32_t* status)
{
    const uint32_t output = count_only ? 0 : args[1], capacity = count_only ? 0 : args[2];
    const uint32_t written = args[count_only ? 1 : 3];
    if (!written) {
        *status = paramErr;
        return 1;
    }
    if (!cfp_write(written, 0))
        return 0;
    if (!count_only && (!output || !capacity || capacity > UINT32_MAX / 4)) {
        *status = paramErr;
        return 1;
    }
    if (capacity && !ROMlib_carbon_guest_memory(output, capacity * 4, 1))
        return 0;

    const int subencoding = query == 0x3f8;
    uint32_t key = (uint32_t)(int32_t)(int16_t)args[0], globals;
    uint16_t total;
    if ((subencoding && !normalize_encoding(ref, args[0], &key)) || !plugin_list(ref, &globals, &total))
        return 0;

    *status = noErr;
    for (uint16_t i = 0; i < total; ++i) {
        uint32_t plugin;
        if (!plugin_at(ref, globals, i, &plugin))
            return 0;
        _Alignas(8) uint32_t found = 0;
        const uint32_t arguments[] = {plugin, key, output, capacity, subencoding ? written : US_TO_SYN68K(&found)};
        if (!cfp_private(ref, query, arguments, 5, status))
            return 0;
        if (subencoding) {
            if (!*status)
                return 1;
        }
        else {
            if ((int32_t)*status == tec_missing_annotation)
                *status = noErr;
            if (found)
                return cfp_write(written, CL(found));
            if (*status)
                return 1;
        }
    }
    if (!subencoding)
        *status = tec_missing_annotation;
    else if (!count_only)
        *status = tec_missing_encoding;
    return 1;
}

static int collect_destinations(const ROMlib_carbon_reference* ref, uint32_t plugin, uint32_t source, uint32_t output,
                                uint32_t capacity, uint32_t* total, uint32_t* status)
{
    _Alignas(8) uint32_t count = 0;
    const uint32_t count_address = US_TO_SYN68K(&count);
    const uint32_t size_query[] = {plugin, 0, 0, count_address};
    if (!cfp_private(ref, 0, size_query, 4, status))
        return 0;
    if (*status)
        return 1;
    const uint32_t entries = CL(count);
    if (entries > INT32_MAX / 12) {
        *status = memFullErr;
        return 1;
    }
    const uint32_t bytes = entries * 12;
    uint32_t list;
    if (!cfp_private(ref, 0x5a1c, &bytes, 1, &list))
        return 0;
    int ok = cfp_private(ref, 0x4394, NULL, 0, status);
    if (ok && !*status) {
        if (!list)
            *status = memFullErr;
        else {
            const uint32_t list_query[] = {plugin, list, entries, count_address};
            ok = cfp_private(ref, 0, list_query, 4, status);
            if (ok && !*status && CL(count) > entries)
                ok = 0;
            for (uint32_t i = 0; ok && !*status && i < CL(count) && *total < capacity; ++i) {
                uint32_t from;
                ok = cfp_get(list + 12 * i, &from);
                if (ok && from == source) {
                    uint32_t to;
                    ok = !output || (cfp_get(list + 12 * i + 4, &to) && cfp_write(output + *total * 4, to));
                    ++*total;
                }
            }
        }
    }
    return (!list || cfp_private(ref, 0x5a34, &list, 1, NULL)) && ok;
}

static int destination_encodings(const ROMlib_carbon_reference* ref, int count_only, const uint32_t* args,
                                 uint32_t* status)
{
    const uint32_t output = count_only ? 0 : args[1], capacity = count_only ? UINT32_MAX : args[2];
    const uint32_t written = args[count_only ? 1 : 3];
    if (!written) {
        *status = paramErr;
        return 1;
    }
    if (!cfp_write(written, 0))
        return 0;
    if (!count_only && (!capacity || !output || capacity > UINT32_MAX / 4)) {
        *status = paramErr;
        return 1;
    }
    if (output && !ROMlib_carbon_guest_memory(output, capacity * 4, 1))
        return 0;
    uint32_t source, globals, used = 0;
    uint16_t total;
    if (!normalize_encoding(ref, args[0], &source) || !plugin_list(ref, &globals, &total))
        return 0;
    *status = noErr;
    for (uint16_t i = 0; i < total && !*status; ++i) {
        uint32_t plugin;
        _Alignas(8) uint8_t supported = 0;
        if (!plugin_at(ref, globals, i, &plugin))
            return 0;
        const uint32_t query[] = {plugin, source, US_TO_SYN68K(&supported)};
        if (!cfp_private(ref, 0x2cc, query, 3, status) ||
            (!*status && supported && !collect_destinations(ref, plugin, source, output, capacity, &used, status)))
            return 0;
    }
    if (!*status) {
        if (!used)
            *status = tec_missing_encoding;
        else if (output) {
            const uint32_t sort[] = {output, used};
            if (!cfp_private(ref, 0x45f0, sort, 2, NULL) || !cfp_private(ref, 0x46d4, sort, 2, &used))
                return 0;
        }
    }
    return cfp_write(written, used);
}

static int create_sniffer(const ROMlib_carbon_reference* ref, const uint32_t* args, uint32_t* status)
{
    const uint32_t output = args[0], encodings = args[1], count = args[2];
    if (!output || !encodings || !count || count > UINT16_MAX) {
        *status = paramErr;
        return 1;
    }
    uint32_t initial, globals, handle = 0, record;
    if (!cfp_get(output, &initial) || !ROMlib_carbon_guest_memory(encodings, 4 * count, 0) ||
        !cfp_private(ref, 0x1fa4, NULL, 0, &globals))
        return 0;
    const uint32_t bytes = 0x50 + count * 64;
    if (!cfp_private(ref, 0x5a04, &bytes, 1, &handle))
        return 0;
    int ok = cfp_private(ref, 0x4394, NULL, 0, status);
    if (ok && !*status && !handle)
        *status = memFullErr;
    if (ok && !*status)
        ok = cfp_get(handle, &record) && cfp_write(record, tec_signature) &&
             cfp_write(record + 4, tec_sniffer_signature);
    for (uint32_t i = 0; ok && !*status && i < count; ++i) {
        uint32_t encoding, normalized;
        _Alignas(8) uint32_t found = 0;
        const uint32_t entry = record + 0x10 + i * 64;
        ok = cfp_get(encodings + 4 * i, &encoding) && normalize_encoding(ref, encoding, &normalized);
        if (!ok)
            break;
        const uint32_t lookup[] = {normalized, US_TO_SYN68K(&found)};
        ok = cfp_private(ref, 0xef0, lookup, 2, status);
        if (ok && (int32_t)*status == tec_missing_encoding)
            *status = noErr;
        if (ok && !*status) {
            const uint32_t plugin = CL(found), init[] = {initial, entry, normalized};
            uint32_t method;
            ok = cfp_write(entry, plugin) && cfp_write(entry + 4, normalized) &&
                 (!plugin || (plugin_method(plugin, 0x20, &method) && cfp_callback(method, init, 3, status)));
        }
    }
    if (ok && !*status)
        ok = cfp_write(record + 0xc, count);
    if (!ok || *status) {
        if (handle && !dispose_handle(ref, handle))
            return 0;
        handle = 0;
    }
    return ok && cfp_write(output, handle) && cfp_private(ref, 0x57dc, &handle, 1, NULL);
}

static int sniffer_record(uint32_t handle, uint32_t* record, uint32_t* total, uint32_t* status)
{
    uint32_t kind;
    if (!read_record(handle, record, status))
        return 0;
    if (*status)
        return 1;
    if (!cfp_get(*record + 4, &kind) || !cfp_get(*record + 0xc, total))
        return 0;
    if (kind != tec_sniffer_signature || *total > UINT16_MAX)
        *status = paramErr;
    return 1;
}

static int reset_sniffer(const ROMlib_carbon_reference* ref, uint32_t handle, int disposing, uint32_t* status)
{
    *status = noErr;
    if (!handle) {
        *status = disposing ? noErr : paramErr;
        return 1;
    }
    uint32_t globals, record, total;
    if (!cfp_private(ref, 0x1fa4, NULL, 0, &globals) || !sniffer_record(handle, &record, &total, status))
        return 0;
    if (*status)
        return 1;
    for (uint32_t i = 0; i < total; ++i) {
        const uint32_t entry = record + 0x10 + i * 64;
        const uint32_t args[] = {handle, entry};
        uint32_t plugin, method;
        if (!cfp_get(entry, &plugin) || (plugin && (!plugin_method(plugin, disposing ? 0x2c : 0x24, &method) ||
                                                    !cfp_callback(method, args, 2, status))))
            return 0;
    }
    return !disposing || dispose_handle(ref, handle);
}

static int sniff_sample(const ROMlib_carbon_reference* ref, const uint32_t* args, uint32_t max_features,
                        uint32_t* status)
{
    const uint32_t handle = args[0], input = args[1], length = args[2], capacity = args[4];
    uint32_t record, total;
    if (!length || !handle) {
        *status = paramErr;
        return 1;
    }
    if (!sniffer_record(handle, &record, &total, status))
        return 0;
    if (*status)
        return 1;
    if (capacity < total) {
        *status = paramErr;
        return 1;
    }
    if (!ROMlib_carbon_guest_memory(input, length, 0) ||
        (total &&
         (!ROMlib_carbon_guest_memory(args[3], 4 * total, 1) || !ROMlib_carbon_guest_memory(args[5], 4 * total, 1) ||
          !ROMlib_carbon_guest_memory(args[7], 4 * total, 1))))
        return 0;
    uint32_t globals;
    if (!cfp_private(ref, 0x1fa4, NULL, 0, &globals) || !lock_converter(ref, handle))
        return 0;
    int ok = cfp_get(handle, &record);
    for (uint32_t i = 0; ok && i < total; ++i) {
        const uint32_t entry = record + 0x10 + i * 64;
        uint32_t plugin, encoding, errors = UINT32_MAX, features = UINT32_MAX, method;
        const uint32_t sample[] = {handle, entry};
        ok = cfp_get(entry, &plugin) && cfp_write(entry + 8, args[6]) && cfp_write(entry + 12, max_features) &&
             cfp_write(entry + 0x10, input) && cfp_write(entry + 0x14, input + length);
        *status = noErr;
        if (ok && plugin)
            ok = plugin_method(plugin, 0x28, &method) && cfp_callback(method, sample, 2, status) &&
                 cfp_get(entry + 0x1c, &errors) && cfp_get(entry + 0x18, &features);
        if (ok)
            ok = cfp_get(entry + 4, &encoding) && cfp_write(args[3] + 4 * i, encoding) &&
                 cfp_write(args[5] + 4 * i, errors) && cfp_write(args[7] + 4 * i, features);
    }
    if (!unlock_converter(ref, handle) || !ok)
        return 0;
    uint32_t sorted;
    const uint32_t sort[] = {args[3], args[5], args[7], total};
    if (!cfp_private(ref, 0x4a44, sort, 4, &sorted))
        return 0;
    if (!*status)
        *status = sorted;
    return 1;
}

static int info_string(uint32_t destination, uint32_t source)
{
    if (!source)
        return cfp_clear(destination, 1);
    const Byte* length = ROMlib_carbon_guest_memory(source, 1, 0);
    if (!length)
        return 0;
    const uint8_t size = *length < 31 ? *length : 31;
    Byte* written = ROMlib_carbon_guest_memory(destination, 1, 1);
    if (!written || !cfp_move(destination + 1, source + 1, size))
        return 0;
    *written = size;
    return 1;
}

static int get_info(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t output = call->words[0], bytes = 0x54;
    if (!output)
        return tec_result(call, noErr);
    uint32_t handle, status;
    if (!cfp_write(output, 0) || !cfp_private(ref, 0x4ac0, &bytes, 1, &handle))
        return 0;
    if (!handle)
        return cfp_private(ref, 0x4ad8, NULL, 0, &status) && tec_result(call, status);
    if (!cfp_private(ref, 0x4af0, &handle, 1, NULL))
        return 0;

    uint32_t record, name, marker, version, version_resource, revisions = 0x0000ffff;
    int ok =
        cfp_get(handle, &record) && cfp_private(ref, 0x1b8, NULL, 0, &name) && (!name || cfp_get(name + 1, &marker));
    if (ok && name && marker == 0x3f3f3f23) {
        _Alignas(8) uint16_t volume = 0;
        _Alignas(8) uint32_t directory = 0;
        const uint32_t folder[] = {0xffff8000, 0x6578746e, 0, US_TO_SYN68K(&volume), US_TO_SYN68K(&directory)};
        ok = cfp_private(ref, 0x4b08, folder, 5, &status);
        if (ok && !status) {
            const uint32_t resolve[] = {name, (uint32_t)(int32_t)(int16_t)CW(volume)};
            ok = cfp_private(ref, 0x1c88, resolve, 2, NULL);
        }
    }
    if (ok)
        ok = info_string(record + 0x30, name) && cfp_private(ref, 0x17c, NULL, 0, &version) &&
             info_string(record + 0x10, version) && cfp_private(ref, 0x49d0, NULL, 0, &version_resource);
    if (ok && version_resource) {
        uint32_t data;
        ok = cfp_get(version_resource, &data) && cfp_get(data + 0x20, &revisions);
    }
    if (ok)
        ok = cfp_write(record, 0x00020160) && cfp_write(record + 8, 0xff) && cfp_write(record + 0x50, revisions);
    const int unlocked = cfp_private(ref, 0x4b20, &handle, 1, NULL);
    if (!ok || !unlocked) {
        cfp_private(ref, 0x4be0, &handle, 1, NULL);
        return 0;
    }
    return cfp_write(output, handle) && tec_result(call, noErr);
}

int ROMlib_carbon_tec(const char* name, uint32_t offset, int operation, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    const int info = operation == tec_info;
    if (!ROMlib_carbon_reference_function(info ? "TextCommon" : "TextEncodingConverter", name, &ref))
        return 0;
    if (ref.code_offset != offset || ref.code_size != (info ? 0x4da4u : 0x5b98u) || !ref.toc ||
        ref.toc_offset != (info ? 0xf4u : 0xb8u) || ref.toc_size != (info ? 0x36cu : 0x138u))
        return ROMlib_carbon_invoke_frame(ref.function, call);

    if (info)
        return get_info(&ref, call);

    uint32_t args[10] = {0}, status = 0;
    memcpy(args, call->words, sizeof call->words);
    if (operation <= tec_get_sniffers)
        return available_entries(&ref, operation, args, &status) && tec_result(call, status);
    if (operation == tec_get_internet_name || operation == tec_from_internet_name)
        return internet_name(&ref, args, operation == tec_from_internet_name, &status) && tec_result(call, status);
    if (operation == tec_create || operation == tec_create_multiple)
        return create_converter(&ref, operation == tec_create_multiple, call);
    if (operation == tec_create_path)
        return create_from_path(&ref, args[0], args[1], args[2], NULL, &status) && tec_result(call, status);

    switch (operation) {
    case tec_count_sub:
    case tec_get_sub:
        return labelled_encodings(&ref, 0x3f8, operation == tec_count_sub, args, &status) && tec_result(call, status);
    case tec_count_mail:
    case tec_get_mail:
        return labelled_encodings(&ref, 0x888, operation == tec_count_mail, args, &status) && tec_result(call, status);
    case tec_count_web:
    case tec_get_web:
        return labelled_encodings(&ref, 0xa80, operation == tec_count_web, args, &status) && tec_result(call, status);
    case tec_count_destinations:
    case tec_get_destinations:
        return destination_encodings(&ref, operation == tec_count_destinations, args, &status) &&
               tec_result(call, status);
    case tec_create_sniffer:
        return create_sniffer(&ref, args, &status) && tec_result(call, status);
    case tec_clear_sniffer:
    case tec_dispose_sniffer:
        return reset_sniffer(&ref, args[0], operation == tec_dispose_sniffer, &status) && tec_result(call, status);
    case tec_sniff: {
        uint32_t max_features;
        return ROMlib_carbon_frame_word(call, 8, &max_features) && sniff_sample(&ref, args, max_features, &status) &&
               tec_result(call, status);
    }
    }

    const int multiple = operation == tec_convert_multiple;
    if (multiple && (!ROMlib_carbon_frame_word(call, 8, &args[8]) || !ROMlib_carbon_frame_word(call, 9, &args[9]) ||
                     !cfp_write(args[9], 0)))
        return 0;
    if ((operation == tec_convert || multiple) && (!cfp_write(args[3], 0) || !cfp_write(args[6], 0)))
        return 0;
    if (operation == tec_encoding_list && (!cfp_write(args[1], 0) || !cfp_write(args[2], 0)))
        return 0;

    uint32_t record;
    if (!read_record(args[0], &record, &status))
        return 0;
    if (status)
        return tec_result(call, status);
    switch (operation) {
    case tec_convert:
    case tec_convert_multiple:
        return convert_buffers(&ref, args[0], record, args, multiple, &status) && tec_result(call, status);
    case tec_flush:
    case tec_flush_multiple:
        return flush_buffers(&ref, record, args, operation == tec_flush_multiple, &status) && tec_result(call, status);
    case tec_clear:
    case tec_dispose:
        return change_stages(&ref, args[0], record, operation == tec_dispose, &status) && tec_result(call, status);
    case tec_encoding_list: {
        uint32_t count, list;
        return cfp_get(record + 0x14, &count) && cfp_get(record + 0x18, &list) && cfp_write(args[1], count) &&
               cfp_write(args[2], list) && tec_result(call, list ? noErr : paramErr);
    }
    default:
        return 0;
    }
}
