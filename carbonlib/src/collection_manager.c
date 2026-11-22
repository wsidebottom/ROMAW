/*+===================================================================
  File:      collection_manager.c

  Summary:   Implements the Classic Collection Manager over guest handle storage.

  Classes:   None.

  Functions: ROMlib_carbon_collection.

  Origin:    System 9.2.1 Collections PowerPC fragment and Universal Interfaces
             3.4.2 Collections.h. Records use the format read by the saved fragment.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "internal.h"
#include "collection_manager.h"

enum { item_locked = -5750, item_not_found = -5751, index_out_of_range = -5752, collection_version_error = -5753 };

#define COLLECTION_VERSION UINT32_C(0x00010000)
#define COLLECTION_MAJOR_MASK UINT32_C(0xffff0000)
#define COLLECTION_LOCK UINT32_C(0x80000000)
#define COLLECTION_PERSISTENT UINT32_C(0x40000000)
#define COLLECTION_ATTRIBUTE_MASK UINT32_C(0xc7ffffff)

typedef struct {
    uint32_t version, owners, defaults, exception;
    uint32_t add_notification, remove_notification;
    uint32_t cached_tag, cached_id, cached_index, cached_tag_order, cached_tag_index, transient_count;
    uint32_t payload, capacity, count, directory;
} CollectionRecord;

typedef struct {
    uint32_t tag, id, attributes, offset;
} CollectionItem;

_Static_assert(sizeof(CollectionRecord) == 64, "Classic collection header extent");
_Static_assert(offsetof(CollectionRecord, directory) == 60, "Classic collection directory handle");
_Static_assert(sizeof(CollectionItem) == 16, "Classic collection directory entry extent");

typedef struct {
    CollectionRecord* record;
    const CollectionItem* items;
    const unsigned char* bytes;
    uint32_t count, extent;
} CollectionView;

typedef struct {
    CollectionItem* items;
    unsigned char* bytes;
    uint32_t count, extent, transient_count;
} CollectionContents;

static int memory_vector(uint32_t vector, uint32_t info, uint16_t trap_word, uint32_t first, uint32_t second,
                         uint32_t* result)
{
    ROMlib_carbon_ppc_frame arguments = {0};
    arguments.words[0] = first;
    arguments.words[1] = second;
    return ROMlib_carbon_call_register_vector(vector, info, trap_word, 0, &arguments, result);
}

static int new_handle(uint32_t size, uint32_t* handle)
{
    return memory_vector(0x0488, 0x33932, 0xa322, size, 0, handle);
}

static int free_handle(uint32_t handle)
{
    uint32_t discarded;
    return !handle || memory_vector(0x048c, 0x133802, 0xa023, handle, 0, &discarded);
}

static int handle_size(uint32_t handle, uint32_t* size)
{
    return memory_vector(0x0494, 0x133832, 0xa025, handle, 0, size);
}

static int memory_error(void)
{
    const uint16_t* error = ROMlib_carbon_guest_memory(0x220, 2, 0);
    return error ? (int16_t)CW(*error) : memFullErr;
}

static int set_memory_error(int16_t error)
{
    void* word = ROMlib_carbon_guest_memory(0x220, 2, 1);
    if (!word)
        return 0;
    const uint16_t encoded = CW(error);
    memcpy(word, &encoded, 2);
    return 1;
}

static void* handle_data(uint32_t handle, size_t bytes, int write)
{
    if (!handle)
        return NULL;
    const uint32_t* master = ROMlib_carbon_guest_memory(handle, sizeof *master, 0);
    return master && *master ? ROMlib_carbon_guest_memory(CL(*master), bytes, write) : NULL;
}

static CollectionRecord* collection_record(uint32_t handle)
{
    CollectionRecord* record = handle_data(handle, sizeof *record, 1);
    return record && (CL(record->version) & COLLECTION_MAJOR_MASK) == COLLECTION_VERSION ? record : NULL;
}

static int set_word(uint32_t address, uint32_t value)
{
    if (!address)
        return 1;
    void* output = ROMlib_carbon_guest_memory(address, 4, 1);
    if (!output)
        return 0;
    const uint32_t encoded = CL(value);
    memcpy(output, &encoded, 4);
    return 1;
}

static int collection_result(ROMlib_carbon_ppc_frame* call, uint32_t collection, int error)
{
    const CollectionRecord* record = error ? collection_record(collection) : NULL;
    if (record && record->exception) {
        ROMlib_carbon_ppc_frame arguments = {0};
        arguments.words[0] = collection;
        arguments.words[1] = (uint32_t)error;
        return ROMlib_carbon_invoke_pascal(CL(record->exception), 0x2e0, &arguments, call->words);
    }
    call->words[0] = (uint32_t)error;
    return 1;
}

static int read_view(uint32_t collection, CollectionView* view)
{
    CollectionRecord* record = collection_record(collection);
    if (!record)
        return 0;
    const uint32_t count = CL(record->count), capacity = CL(record->capacity);
    if (count > capacity || capacity > INT32_MAX / sizeof(CollectionItem))
        return 0;
    const CollectionItem* items = NULL;
    uint32_t extent = 0;
    const unsigned char* bytes = NULL;
    if (count) {
        uint32_t table_extent;
        if (!handle_size(CL(record->directory), &table_extent) || (int32_t)table_extent < 0 ||
            table_extent < count * sizeof *items)
            return 0;
        if (!handle_size(CL(record->payload), &extent) || (int32_t)extent < 0)
            return 0;
        items = handle_data(CL(record->directory), count * sizeof *items, 0);
        bytes = extent ? handle_data(CL(record->payload), extent, 0) : NULL;
        if (!items || (extent && !bytes))
            return 0;
    }
    *view = (CollectionView){record, items, bytes, count, extent};
    return 1;
}

static int item_data(const CollectionView* view, uint32_t index, const unsigned char** bytes, uint32_t* size)
{
    uint32_t offset = CL(view->items[index].offset);
    *bytes = NULL;
    *size = 0;
    if (!offset)
        return 1;
    if (offset < 4 || offset > view->extent)
        return 0;
    uint32_t length;
    memcpy(&length, view->bytes + offset - 4, 4);
    length = CL(length);
    if (length > view->extent - offset)
        return 0;
    *bytes = view->bytes + offset;
    *size = length;
    return 1;
}

static uint32_t lower_bound(const CollectionView* view, uint32_t tag, int32_t id)
{
    uint32_t start = 0, end = view->count;
    while (start < end) {
        uint32_t middle = start + (end - start) / 2;
        const CollectionItem* item = &view->items[middle];
        if (CL(item->tag) < tag || (CL(item->tag) == tag && (int32_t)CL(item->id) < id))
            start = middle + 1;
        else
            end = middle;
    }
    return start;
}

static uint32_t tag_end(const CollectionView* view, uint32_t tag)
{
    return tag == UINT32_MAX ? view->count : lower_bound(view, tag + 1, INT32_MIN);
}

static uint32_t item_extent(uint32_t size)
{
    return size ? 4 + ((size + 1) & ~UINT32_C(1)) : 0;
}

/* Prepare complete guest-format contents on the host before any movable guest
   allocation. This also protects inputs that alias the original collection. */
static void write_item(CollectionContents* contents, CollectionItem item, const unsigned char* bytes, uint32_t length,
                       uint32_t* offset)
{
    item.offset = 0;
    if (length) {
        uint32_t encoded = CL(length);
        memcpy(contents->bytes + *offset, &encoded, 4);
        memcpy(contents->bytes + *offset + 4, bytes, length);
        item.offset = CL(*offset + 4);
        if (length & 1)
            contents->bytes[*offset + 4 + length] = 0;
        *offset += item_extent(length);
    }
    contents->transient_count += !(CL(item.attributes) & COLLECTION_PERSISTENT);
    contents->items[contents->count++] = item;
}

static int publish_contents(uint32_t collection, const CollectionContents* contents, int* error)
{
    uint32_t payload = 0, directory = 0;
    int ok = new_handle(contents->extent, &payload);
    if (ok && payload)
        ok = new_handle(contents->count * sizeof(CollectionItem), &directory);
    if (!ok || !payload || !directory) {
        *error = memFullErr;
        if (ok)
            ok = free_handle(payload) && free_handle(directory);
        return ok;
    }
    CollectionRecord* record = collection_record(collection);
    void* bytes = contents->extent ? handle_data(payload, contents->extent, 1) : NULL;
    void* items = contents->count ? handle_data(directory, contents->count * sizeof(CollectionItem), 1) : NULL;
    if (!record || (contents->extent && !bytes) || (contents->count && !items)) {
        *error = collection_version_error;
        return free_handle(payload) && free_handle(directory);
    }
    if (contents->extent)
        memcpy(bytes, contents->bytes, contents->extent);
    if (contents->count)
        memcpy(items, contents->items, contents->count * sizeof(CollectionItem));
    const uint32_t old_payload = CL(record->payload), old_directory = CL(record->directory);
    record->payload = CL(payload);
    record->directory = CL(directory);
    record->count = record->capacity = CL(contents->count);
    record->transient_count = CL(contents->transient_count);
    record->cached_tag = contents->count ? contents->items[0].tag : 0;
    record->cached_id = contents->count ? contents->items[0].id : 0;
    record->cached_index = record->cached_tag_order = record->cached_tag_index = 0;
    *error = noErr;
    return free_handle(old_payload) && free_handle(old_directory);
}

static int new_collection(uint32_t* collection)
{
    uint32_t handle = 0, payload = 0, directory = 0;
    *collection = 0;
    if (!new_handle(0, &payload) || (payload && !new_handle(0, &directory)) ||
        (directory && !new_handle(sizeof(CollectionRecord), &handle)))
        return 0;
    if (!handle) {
        const int error = memory_error();
        return free_handle(payload) && free_handle(directory) && set_memory_error(error ? error : memFullErr);
    }
    CollectionRecord* record = handle_data(handle, sizeof *record, 1);
    if (!record)
        return 0;
    record->version = CL(COLLECTION_VERSION);
    record->owners = CLC(1);
    record->defaults = CL(COLLECTION_PERSISTENT);
    record->payload = CL(payload);
    record->directory = CL(directory);
    *collection = handle;
    return 1;
}

static int dispose_collection(uint32_t handle, CollectionRecord* record)
{
    uint32_t owners = CL(record->owners);
    if (owners > 1) {
        record->owners = CL(owners - 1);
        return 1;
    }
    const uint32_t payload = CL(record->payload), directory = CL(record->directory);
    return free_handle(directory) && free_handle(payload) && free_handle(handle);
}

enum item_change { copy_items, insert_item, replace_item, remove_item, filter_tag, filter_attributes, remove_all };

static int keep_item(enum item_change change, const CollectionItem* item, uint32_t index, uint32_t target,
                     uint32_t mask, uint32_t match)
{
    if (change == remove_item || change == replace_item)
        return index != target;
    if (change == filter_tag)
        return CL(item->tag) != match;
    if (change == filter_attributes)
        return (CL(item->attributes) & mask) != (match & mask);
    return change != remove_all;
}

static int change_contents(uint32_t handle, const CollectionView* view, enum item_change change, uint32_t index,
                           CollectionItem replacement, const unsigned char* data, uint32_t size)
{
    uint64_t extent = item_extent(size);
    uint32_t count = (change == insert_item || change == replace_item);
    for (uint32_t i = 0; i < view->count; ++i) {
        if (!keep_item(change, &view->items[i], i, index, CL(replacement.attributes), CL(replacement.tag)))
            continue;
        uint32_t length;
        const unsigned char* bytes;
        if (!item_data(view, i, &bytes, &length))
            return collection_version_error;
        extent += item_extent(length);
        ++count;
    }
    if (extent > INT32_MAX || count > INT32_MAX / sizeof(CollectionItem))
        return memFullErr;
    CollectionContents contents = {0};
    contents.extent = (uint32_t)extent;
    contents.items = count ? malloc(count * sizeof(CollectionItem)) : NULL;
    contents.bytes = extent ? malloc((size_t)extent) : NULL;
    int error = memFullErr;
    if ((!count || contents.items) && (!extent || contents.bytes)) {
        uint32_t offset = 0;
        for (uint32_t i = 0; i <= view->count; ++i) {
            if (i == index && (change == insert_item || change == replace_item))
                write_item(&contents, replacement, data, size, &offset);
            if (i == view->count ||
                !keep_item(change, &view->items[i], i, index, CL(replacement.attributes), CL(replacement.tag)))
                continue;
            uint32_t length;
            const unsigned char* bytes;
            if (item_data(view, i, &bytes, &length))
                write_item(&contents, view->items[i], bytes, length, &offset);
        }
        if (!publish_contents(handle, &contents, &error))
            error = memFullErr;
    }
    free(contents.bytes);
    free(contents.items);
    return error;
}

static int get_item(const CollectionView* view, uint32_t index, uint32_t size_address, uint32_t destination)
{
    const unsigned char* bytes;
    uint32_t size;
    if (!item_data(view, index, &bytes, &size))
        return collection_version_error;
    uint32_t amount = size;
    if (size_address) {
        const void* limit = ROMlib_carbon_guest_memory(size_address, 4, 0);
        if (!limit)
            return paramErr;
        uint32_t encoded;
        memcpy(&encoded, limit, 4);
        amount = MIN(CL(encoded), size);
        if (!set_word(size_address, size))
            return paramErr;
    }
    if (destination && amount) {
        void* output = ROMlib_carbon_guest_memory(destination, amount, 1);
        if (!output)
            return paramErr;
        memmove(output, bytes, amount);
    }
    return noErr;
}

static int get_item_handle(const CollectionView* view, uint32_t index, uint32_t destination)
{
    const unsigned char* bytes;
    uint32_t size;
    if (!item_data(view, index, &bytes, &size) || !destination)
        return paramErr;
    unsigned char* snapshot = size ? malloc(size) : NULL;
    if (size && !snapshot)
        return memFullErr;
    if (size)
        memcpy(snapshot, bytes, size);
    uint32_t discarded;
    int error = memory_vector(0x0490, 0x733802, 0xa024, destination, size, &discarded) ? memory_error() : memFullErr;
    if (!error && size) {
        void* output = handle_data(destination, size, 1);
        if (output)
            memcpy(output, snapshot, size);
        else
            error = paramErr;
    }
    free(snapshot);
    return error;
}

static int item_attributes(const CollectionView* view, uint32_t index, uint32_t mask, uint32_t value)
{
    CollectionItem* items = handle_data(CL(view->record->directory), view->count * sizeof *items, 1);
    if (!items)
        return collection_version_error;
    mask &= COLLECTION_ATTRIBUTE_MASK;
    const uint32_t old = CL(items[index].attributes), next = (old & ~mask) | (value & mask);
    items[index].attributes = CL(next);
    if ((old ^ next) & COLLECTION_PERSISTENT) {
        uint32_t count = CL(view->record->transient_count);
        view->record->transient_count = CL(next & COLLECTION_PERSISTENT ? count - 1 : count + 1);
    }
    return noErr;
}

typedef struct {
    uint32_t procedure, reference;
    unsigned char* buffer;
    uint32_t remaining;
    int reading;
} CollectionStream;

/* The callback sees guest storage, pinned across any Toolbox calls it makes. */
static int transfer_chunk(CollectionStream* stream, void* bytes, uint32_t size)
{
    if (!stream->procedure) {
        if (size > stream->remaining)
            return collection_version_error;
        if (size) {
            if (stream->reading)
                memcpy(bytes, stream->buffer, size);
            else
                memcpy(stream->buffer, bytes, size);
            stream->buffer += size;
            stream->remaining -= size;
        }
        return noErr;
    }

    uint32_t handle = 0, unused;
    if (!new_handle(size ? size : 1, &handle) || !handle)
        return memFullErr;
    int error = paramErr;
    if (memory_vector(0x04a4, 0x133802, 0xa029, handle, 0, &unused) && !memory_error()) {
        const uint32_t* master = ROMlib_carbon_guest_memory(handle, sizeof *master, 0);
        const uint32_t address = master ? CL(*master) : 0;
        void* guest = address ? ROMlib_carbon_guest_memory(address, size ? size : 1, 1) : NULL;
        if (guest) {
            if (size && !stream->reading)
                memcpy(guest, bytes, size);
            ROMlib_carbon_ppc_frame args = {0};
            args.words[0] = size;
            args.words[1] = address;
            args.words[2] = stream->reference;
            uint32_t result;
            if (ROMlib_carbon_invoke_pascal(stream->procedure, 0xfe0, &args, &result)) {
                error = (int16_t)result;
                if (!error && stream->reading && size)
                    memcpy(bytes, guest, size);
            }
        }
    }
    return free_handle(handle) ? error : memFullErr;
}

static int stored_item(const CollectionView* view, uint32_t index, const unsigned char** bytes, uint32_t* extent)
{
    uint32_t size;
    if (!item_data(view, index, bytes, &size))
        return 0;
    *extent = 0;
    if (*bytes) {
        *bytes -= 4;
        *extent = 4 + size + (size & 1);
        if (*extent > view->extent - (CL(view->items[index].offset) - 4))
            return 0;
    }
    return 1;
}

static int snapshot_collection(const CollectionView* view, uint32_t mask, uint32_t match, CollectionContents* contents)
{
    uint32_t count = view->count;
    uint64_t extent = view->extent;
    if (mask) {
        count = 0;
        extent = 0;
        for (uint32_t i = 0; i < view->count; ++i) {
            if ((CL(view->items[i].attributes) & mask) != match)
                continue;
            uint32_t size;
            const unsigned char* data;
            if (!stored_item(view, i, &data, &size))
                return collection_version_error;
            extent += size;
            ++count;
        }
    }
    if (extent > INT32_MAX)
        return memFullErr;
    contents->items = count ? malloc(count * sizeof(CollectionItem)) : NULL;
    contents->bytes = extent ? malloc((size_t)extent) : NULL;
    if ((count && !contents->items) || (extent && !contents->bytes))
        return memFullErr;
    contents->extent = (uint32_t)extent;
    if (!mask) {
        contents->count = count;
        if (count)
            memcpy(contents->items, view->items, count * sizeof(CollectionItem));
        if (extent)
            memcpy(contents->bytes, view->bytes, (size_t)extent);
        return noErr;
    }
    uint32_t offset = 0;
    for (uint32_t i = 0; i < view->count; ++i) {
        if ((CL(view->items[i].attributes) & mask) != match)
            continue;
        uint32_t size;
        const unsigned char* data;
        if (!stored_item(view, i, &data, &size))
            return collection_version_error;
        CollectionItem item = view->items[i];
        if (size) {
            memcpy(contents->bytes + offset, data, size);
            item.offset = CL(offset + 4);
            offset += size;
        }
        contents->items[contents->count++] = item;
    }
    return noErr;
}

static int flatten_chunks(CollectionStream* stream, uint32_t header[3], const CollectionContents* contents,
                          int selective)
{
    int error = transfer_chunk(stream, header, 12);
    if (error || !contents->count)
        return error;
    if (selective && stream->procedure) {
        for (uint32_t i = 0; !error && i < contents->count; ++i)
            error = transfer_chunk(stream, &contents->items[i], sizeof(CollectionItem));
    }
    else
        error = transfer_chunk(stream, contents->items, contents->count * sizeof(CollectionItem));
    uint32_t extent = CL(contents->extent);
    if (!error)
        error = transfer_chunk(stream, &extent, 4);
    if (error || !contents->extent)
        return error;
    if (!selective || !stream->procedure)
        return transfer_chunk(stream, contents->bytes, contents->extent);

    const CollectionView view = {NULL, contents->items, contents->bytes, contents->count, contents->extent};
    for (uint32_t i = 0; !error && i < view.count; ++i) {
        uint32_t size;
        const unsigned char* data;
        if (!stored_item(&view, i, &data, &size))
            return collection_version_error;
        error = transfer_chunk(stream, size ? contents->bytes + (data - contents->bytes) : NULL, size);
    }
    return error;
}

static int flatten_collection(const CollectionView* view, uint32_t mask, uint32_t match, uint32_t callback,
                              uint32_t reference, uint32_t destination)
{
    if (!callback && !destination)
        return paramErr;
    if (mask || CL(view->record->transient_count)) {
        mask |= COLLECTION_PERSISTENT;
        match = (match & mask) | COLLECTION_PERSISTENT;
    }
    uint32_t header[] = {view->record->version, view->record->defaults, 0};
    CollectionContents contents = {0};
    int error = snapshot_collection(view, mask, match, &contents);
    header[2] = CL(contents.count);
    CollectionStream stream = {callback, reference, NULL, 0, 0};
    if (!error && destination) {
        const uint64_t size = 12 + (contents.count ? 4 + (uint64_t)contents.count * 16 + contents.extent : 0);
        uint32_t unused;
        if (size > INT32_MAX || !memory_vector(0x0490, 0x733802, 0xa024, destination, (uint32_t)size, &unused))
            error = memFullErr;
        else if (!(error = memory_error())) {
            stream.buffer = handle_data(destination, (size_t)size, 1);
            stream.remaining = (uint32_t)size;
            if (!stream.buffer)
                error = paramErr;
        }
    }
    if (!error)
        error = flatten_chunks(&stream, header, &contents, mask != 0);
    free(contents.bytes);
    free(contents.items);
    return error;
}

static int read_contents(CollectionStream* stream, CollectionContents* contents, uint32_t header[3])
{
    int error = transfer_chunk(stream, header, 12);
    if (error)
        return error;
    contents->count = CL(header[2]);
    if ((CL(header[0]) & COLLECTION_MAJOR_MASK) != COLLECTION_VERSION ||
        contents->count > INT32_MAX / sizeof(CollectionItem))
        return collection_version_error;
    if (!contents->count)
        return noErr;

    const uint32_t directory_size = contents->count * sizeof(CollectionItem);
    if (!stream->procedure && directory_size > stream->remaining)
        return collection_version_error;
    contents->items = malloc(directory_size);
    if (!contents->items)
        return memFullErr;
    error = transfer_chunk(stream, contents->items, directory_size);
    if (error)
        return error;
    uint32_t extent;
    error = transfer_chunk(stream, &extent, 4);
    if (error)
        return error;
    contents->extent = CL(extent);
    if (contents->extent > INT32_MAX || (!stream->procedure && contents->extent > stream->remaining))
        return collection_version_error;
    if (contents->extent) {
        contents->bytes = malloc(contents->extent);
        if (!contents->bytes)
            return memFullErr;
        error = transfer_chunk(stream, contents->bytes, contents->extent);
        if (error)
            return error;
    }
    const CollectionView view = {NULL, contents->items, contents->bytes, contents->count, contents->extent};
    for (uint32_t i = 0; i < contents->count; ++i) {
        const CollectionItem* item = &contents->items[i];
        const uint32_t offset = CL(item->offset);
        uint32_t size;
        const unsigned char* data;
        if ((offset & 1) || !stored_item(&view, i, &data, &size))
            return collection_version_error;
        if (i) {
            const CollectionItem* previous = item - 1;
            if (CL(previous->tag) > CL(item->tag) ||
                (previous->tag == item->tag && (int32_t)CL(previous->id) >= (int32_t)CL(item->id)))
                return collection_version_error;
        }
        contents->transient_count += !(CL(item->attributes) & COLLECTION_PERSISTENT);
    }
    return noErr;
}

static int unflatten_collection(uint32_t collection, uint32_t callback, uint32_t reference, uint32_t source)
{
    if (!callback && !source)
        return paramErr;
    CollectionStream stream = {callback, reference, NULL, 0, 1};
    if (source) {
        if (!handle_size(source, &stream.remaining) || stream.remaining > INT32_MAX)
            return paramErr;
        stream.buffer = handle_data(source, stream.remaining, 0);
        if (!stream.buffer && stream.remaining)
            return paramErr;
    }
    CollectionContents contents = {0};
    uint32_t header[3];
    /* Handle input may alias storage owned by the destination collection. */
    int error = source ? read_contents(&stream, &contents, header) : noErr;
    const CollectionContents empty = {0};
    int clear_error;
    if (!publish_contents(collection, &empty, &clear_error))
        clear_error = memFullErr;
    if (clear_error)
        error = clear_error;
    if (!error && !source)
        error = read_contents(&stream, &contents, header);
    if (!error && contents.count && !publish_contents(collection, &contents, &error))
        error = memFullErr;
    if (!error) {
        CollectionRecord* record = collection_record(collection);
        if (!record)
            error = collection_version_error;
        else {
            record->version = header[0];
            record->defaults = header[1];
        }
    }
    free(contents.bytes);
    free(contents.items);
    return error;
}

static int get_new_collection(int16_t id, uint32_t* result)
{
    ROMlib_carbon_ppc_frame args = {0};
    args.words[0] = UINT32_C(0x636c746e); /* 'cltn' */
    args.words[1] = (uint32_t)id;
    uint32_t resource, size;
    *result = 0;
    if (!ROMlib_carbon_call_toolbox_vector(0x1480, 0x2f0, 0, &args, &resource))
        return 0;
    if (!resource || !handle_size(resource, &size) || size < 4 || size > INT32_MAX)
        return 1;
    const unsigned char* bytes = handle_data(resource, size, 0);
    if (!bytes)
        return 1;
    unsigned char* copy = malloc(size);
    if (!copy)
        return set_memory_error(memFullErr);
    memcpy(copy, bytes, size);

    uint32_t encoded_count, collection = 0;
    memcpy(&encoded_count, copy, 4);
    uint32_t count = CL(encoded_count), offset = 4;
    int error = noErr;
    int ok = 1;
    if (count <= (size - offset) / 14)
        ok = new_collection(&collection);
    if (ok && collection) {
        for (uint32_t i = 0; !error && i < count; ++i) {
            if (size - offset < 14) {
                error = collection_version_error;
                break;
            }
            CollectionItem item = {0};
            uint16_t encoded_size;
            memcpy(&item, copy + offset, 12);
            memcpy(&encoded_size, copy + offset + 12, 2);
            const uint32_t length = CW(encoded_size), padded = length + (length & 1);
            offset += 14;
            CollectionView view;
            if (length > INT16_MAX || padded > size - offset || !read_view(collection, &view)) {
                error = collection_version_error;
                break;
            }
            const uint32_t index = lower_bound(&view, CL(item.tag), (int32_t)CL(item.id));
            const int found =
                index < view.count && view.items[index].tag == item.tag && view.items[index].id == item.id;
            if (found && (CL(view.items[index].attributes) & COLLECTION_LOCK)) {
                error = item_locked;
                break;
            }
            if (found && item.attributes == view.record->defaults)
                item.attributes = view.items[index].attributes;
            else
                item.attributes = CL((CL(view.record->defaults) & ~COLLECTION_ATTRIBUTE_MASK) |
                                     (CL(item.attributes) & COLLECTION_ATTRIBUTE_MASK));
            error = change_contents(collection, &view, found ? replace_item : insert_item, index, item, copy + offset,
                                    length);
            offset += padded;
        }
        if (error) {
            CollectionRecord* record = collection_record(collection);
            ok = record && dispose_collection(collection, record);
            collection = 0;
        }
    }
    free(copy);
    *result = collection;
    return ok;
}

static int item_information(const CollectionView* view, uint32_t index, uint32_t tag, uint32_t id, uint32_t position,
                            uint32_t size, uint32_t attributes)
{
    const CollectionItem* item = &view->items[index];
    const unsigned char* bytes;
    uint32_t length;
    if (!item_data(view, index, &bytes, &length))
        return collection_version_error;
    return set_word(tag, CL(item->tag)) && set_word(id, CL(item->id)) && set_word(position, index + 1) &&
                   set_word(size, length) && set_word(attributes, CL(item->attributes))
               ? noErr
               : paramErr;
}

static int write_collection_item(const CollectionView* view, uint32_t collection, uint32_t index, int found,
                                 uint32_t tag, uint32_t id, uint32_t data, uint32_t size, int via_handle)
{
    if (found && (CL(view->items[index].attributes) & COLLECTION_LOCK))
        return item_locked;
    if (via_handle) {
        if (!data || !handle_size(data, &size) || (int32_t)size < 0)
            return paramErr;
    }
    else if (!data)
        size = 0;
    if (size > INT32_MAX - 5)
        return memFullErr;
    const unsigned char* bytes = NULL;
    if (size) {
        bytes = via_handle ? handle_data(data, size, 0) : ROMlib_carbon_guest_memory(data, size, 0);
        if (!bytes)
            return paramErr;
    }
    CollectionItem item = {CL(tag), CL(id), view->record->defaults, 0};
    if (found)
        item = view->items[index];
    return change_contents(collection, view, found ? replace_item : insert_item, index, item, bytes, size);
}

static int tag_query(enum carbon_collection_operation operation, const CollectionView* view, const uint32_t* args,
                     uint32_t* result)
{
    uint32_t tag_index = 0;
    for (uint32_t start = 0; start < view->count;) {
        uint32_t tag = CL(view->items[start].tag);
        if (++tag_index == args[1] && operation == collection_index_tag)
            return set_word(args[2], tag) ? noErr : paramErr;
        start = tag_end(view, tag);
    }
    if (operation == collection_index_tag)
        return index_out_of_range;
    *result = tag_index;
    return noErr;
}

int ROMlib_carbon_collection(enum carbon_collection_operation operation, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t* args = call->words;
    const uint32_t handle = args[0];
    if (operation == collection_new)
        return new_collection(call->words);
    if (operation == collection_get_new)
        return get_new_collection((int16_t)handle, call->words);
    CollectionRecord* record = collection_record(handle);
    if (!record)
        return collection_result(call, 0, paramErr);
    uint32_t result = 0;
    switch (operation) {
    case collection_dispose:
    case collection_release:
        call->words[0] = 0;
        return dispose_collection(handle, record);
    case collection_retain:
    case collection_clone:
        if (CL(record->owners) == INT32_MAX)
            return collection_result(call, handle, memFullErr);
        record->owners = CL(CL(record->owners) + 1);
        call->words[0] = operation == collection_clone ? handle : noErr;
        return 1;
    case collection_owners:
        call->words[0] = CL(record->owners);
        return 1;
    case collection_count:
        call->words[0] = CL(record->count);
        return 1;
    case collection_default_attrs:
        call->words[0] = CL(record->defaults);
        return 1;
    case collection_set_default_attrs: {
        uint32_t mask = args[1] & COLLECTION_ATTRIBUTE_MASK;
        record->defaults = CL((CL(record->defaults) & ~mask) | (args[2] & mask));
        return 1;
    }
    case collection_exception:
        call->words[0] = CL(record->exception);
        return 1;
    case collection_set_exception:
        record->exception = CL(args[1]);
        return 1;
    default:
        break;
    }

    CollectionView view;
    if (!read_view(handle, &view))
        return collection_result(call, handle, collection_version_error);
    const uint32_t tag = args[1], index_arg = args[1] - 1;
    const int32_t id = args[2];
    int error = noErr;
    switch (operation) {
    case collection_flatten:
    case collection_flatten_partial:
        return collection_result(
            call, handle,
            flatten_collection(&view, operation == collection_flatten_partial ? args[3] & COLLECTION_ATTRIBUTE_MASK : 0,
                               operation == collection_flatten_partial ? args[4] : 0, args[1], args[2], 0));
    case collection_flatten_handle:
        return collection_result(call, handle, flatten_collection(&view, 0, 0, 0, 0, args[1]));
    case collection_unflatten:
        return collection_result(call, handle, unflatten_collection(handle, args[1], args[2], 0));
    case collection_unflatten_handle:
        return collection_result(call, handle, unflatten_collection(handle, 0, 0, args[1]));
    default:
        break;
    }
    if (operation == collection_copy) {
        uint32_t destination = args[1];
        if (destination == handle)
            return 1;
        const uint32_t defaults = record->defaults;
        if (!destination && !new_collection(&destination))
            return 0;
        if (!destination || !collection_record(destination))
            error = destination ? collection_version_error : memFullErr;
        else {
            /* Allocating a new collection can move the source handles. */
            if (!read_view(handle, &view))
                return 0;
            error = change_contents(destination, &view, copy_items, 0, (CollectionItem){0}, NULL, 0);
            CollectionRecord* target = collection_record(destination);
            if (!error && target)
                target->defaults = defaults;
            else if (!args[1] && target)
                dispose_collection(destination, target);
        }
        if (error) {
            if (!collection_result(call, handle, error) || !set_memory_error((int16_t)call->words[0]))
                return 0;
            call->words[0] = 0;
        }
        else
            call->words[0] = destination;
        return 1;
    }
    if (operation == collection_count_tags || operation == collection_index_tag) {
        error = tag_query(operation, &view, args, &result);
        if (error)
            return collection_result(call, handle, error);
        call->words[0] = result;
        return 1;
    }
    if (operation == collection_purge || operation == collection_purge_tag || operation == collection_empty) {
        enum item_change change = operation == collection_purge       ? filter_attributes
                                  : operation == collection_purge_tag ? filter_tag
                                                                      : remove_all;
        CollectionItem match = {CL(operation == collection_purge ? args[2] : tag), 0,
                                CL(args[1] & COLLECTION_ATTRIBUTE_MASK), 0};
        return collection_result(call, handle, change_contents(handle, &view, change, 0, match, NULL, 0));
    }

    if (operation == collection_tag_exists || operation == collection_count_tagged ||
        operation == collection_get_tagged || operation == collection_tagged_info) {
        const uint32_t start = lower_bound(&view, tag, INT32_MIN), end = tag_end(&view, tag);
        const uint32_t count = end - start;
        if (operation == collection_tag_exists || operation == collection_count_tagged) {
            call->words[0] = operation == collection_tag_exists ? count != 0 : count;
            return 1;
        }
        if (!args[2] || args[2] > count)
            return collection_result(call, handle, index_out_of_range);
        const uint32_t index = start + args[2] - 1;
        error = operation == collection_get_tagged
                    ? get_item(&view, index, args[3], args[4])
                    : item_information(&view, index, 0, args[3], args[4], args[5], args[6]);
        return collection_result(call, handle, error);
    }

    const int indexed = (operation >= collection_replace_index && operation <= collection_index_info) ||
                        operation == collection_replace_index_handle || operation == collection_get_index_handle;
    const uint32_t index = indexed ? index_arg : lower_bound(&view, tag, id);
    const int found = index < view.count &&
                      (indexed || (CL(view.items[index].tag) == tag && (int32_t)CL(view.items[index].id) == id));
    if (!found && operation != collection_add && operation != collection_add_handle)
        return collection_result(call, handle, indexed ? index_out_of_range : item_not_found);

    switch (operation) {
    case collection_add:
    case collection_add_handle:
        error = write_collection_item(&view, handle, index, found, tag, id, args[operation == collection_add ? 4 : 3],
                                      args[3], operation == collection_add_handle);
        break;
    case collection_replace_index:
    case collection_replace_index_handle:
        error =
            write_collection_item(&view, handle, index, 1, 0, 0, args[operation == collection_replace_index ? 3 : 2],
                                  args[2], operation == collection_replace_index_handle);
        break;
    case collection_get:
    case collection_get_index:
        error = get_item(&view, index, args[indexed ? 2 : 3], args[indexed ? 3 : 4]);
        break;
    case collection_get_handle:
    case collection_get_index_handle:
        error = get_item_handle(&view, index, args[indexed ? 2 : 3]);
        break;
    case collection_remove:
    case collection_remove_index:
        error = change_contents(handle, &view, remove_item, index, (CollectionItem){0}, NULL, 0);
        break;
    case collection_set_info:
    case collection_set_index_info:
        error = item_attributes(&view, index, args[indexed ? 2 : 3], args[indexed ? 3 : 4]);
        break;
    case collection_info:
        error = item_information(&view, index, 0, 0, args[3], args[4], args[5]);
        break;
    case collection_index_info:
        error = item_information(&view, index, args[2], args[3], 0, args[4], args[5]);
        break;
    default:
        error = paramErr;
        break;
    }
    return collection_result(call, handle, error);
}
