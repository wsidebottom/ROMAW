/*+===================================================================
  File:      color_profiles.h

  Summary:   Read and edit live ColorSync profiles, descriptions, and ICC named colors.

  Classes:   None.

  Functions: Private frame entries for the ColorSync profile queries.

  Origin:    ColorSync 3.0.4 PowerPC profile workers from the Mac OS 9.2.1 Installer Tome
             and the Universal Interfaces 3.4.2 declarations.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_COLOR_PROFILES_H
#define ROMAW_CARBON_COLOR_PROFILES_H

#include "color_sync.h"
#include "ScriptMgr.h"

enum color_profile_operation {
    color_profile_retain,
    color_profile_ref_count,
    color_profile_modified,
    color_profile_header,
    color_profile_count,
    color_profile_exists,
    color_profile_info,
    color_profile_get,
    color_profile_get_index,
    color_profile_get_partial,
    color_profile_close,
    color_profile_location_legacy,
    color_profile_location_sized,
    color_profile_set_header,
    color_profile_remove,
    color_profile_alias,
    color_profile_write,
    color_profile_write_partial,
    color_profile_resize,
    color_profile_update,
    color_profile_copy,
    color_profile_flatten,
    color_profile_descriptions,
    color_profile_script_description,
    color_profile_set_descriptions,
    color_profile_named_info,
    color_profile_named_value,
    color_profile_named_indexed_value,
    color_profile_named_index,
    color_profile_named_name,
};

enum {
    color_profile_flags = 8,
    color_profile_tags = 12,
    color_profile_location = 0x10,
    color_profile_location_is_private = 0x112,
    color_profile_location_is_open = 0x113,
    color_profile_edit_location = 0x11c,
    color_profile_edit_owned = 0x21e,
    color_profile_edit_open = 0x21f,
    color_profile_edit_delete = 0x220,
    color_profile_memory_edit_limit = 0x5000,
    color_profile_owners = 0x228,
    color_profile_header_size = 0x22c,
    color_profile_header_data = 0x230,
    color_profile_dirty_header = 1,
    color_profile_dirty_directory = 2,
    color_profile_index_error = -4201,
    color_profile_missing_element = -4200,
    color_profile_invalid = -4204,
    color_profile_invalid_location = -4205,
    color_profile_named_not_found = -4216,
    color_profile_external_payload = 1,
    color_profile_description_tag = 0x64657363,
};

static void* color_profile_memory(uint32_t address, size_t size, int write_access)
{
    return address ? ROMlib_carbon_try_guest_memory(address, size, write_access) : NULL;
}

static uint32_t color_profile_indirect(uint32_t address)
{
    const unsigned char* bytes = color_profile_memory(address, 4, 0);
    return bytes ? color_read(bytes, 4) : 0;
}

static int color_profile_result(ROMlib_carbon_ppc_frame* call, int32_t result)
{
    call->words[0] = result;
    return 1;
}

static int color_profile_get_location(ROMlib_carbon_ppc_frame* call, const unsigned char* record, int sized)
{
    const uint32_t address = call->words[1];
    unsigned char* size = sized ? color_profile_memory(call->words[2], 4, 1) : NULL;
    if (sized && !size)
        return color_profile_result(call, paramErr);

    unsigned char location[258];
    static const uint16_t sizes[] = {2, 72, 6, 6, 10, sizeof location, 10};
    const unsigned char* source = record + color_profile_location;
    const unsigned kind = record[color_profile_location_is_open] && !record[color_profile_location_is_private]
                              ? color_read(source, 2)
                              : 0;
    if (!address) {
        if (kind >= sizeof sizes / sizeof sizes[0])
            return color_profile_result(call, color_profile_invalid);
        if (size)
            color_write(size, 4, sizes[kind]);
        return color_profile_result(call, noErr);
    }

    uint32_t capacity = size ? MIN(color_read(size, 4), sizeof location) : 72;
    unsigned char* output = color_profile_memory(address, capacity, 1);
    if (!output)
        return color_profile_result(call, paramErr);
    if (kind < sizeof sizes / sizeof sizes[0] && kind && capacity >= sizes[kind])
        memcpy(location, source, sizes[kind]);
    if (size)
        color_write(size, 4, capacity);
    memset(output, 0, capacity);
    if (capacity < 2)
        return color_profile_result(call, paramErr);
    if (kind >= sizeof sizes / sizeof sizes[0])
        return color_profile_result(call, color_profile_invalid);
    if (capacity < sizes[kind])
        return color_profile_result(call, color_profile_invalid_location);
    if (kind)
        memcpy(output, location, sizes[kind]);
    return color_profile_result(call, noErr);
}

static const unsigned char* color_profile_find_tag(const unsigned char* table, uint32_t count, uint32_t key)
{
    for (uint32_t i = 0; i < count; ++i) {
        const unsigned char* entry = table + 4 + (size_t)i * 16;
        if (color_read(entry, 4) == key)
            return entry;
    }
    return NULL;
}

static Size color_profile_handle_size(uint32_t handle)
{
    const uint32_t data = color_profile_indirect(handle);
    if (data < 12 || !color_profile_memory(data - 12, 12, 0))
        return -1;
    uint32_t error = ROMlib_carbon_LMGetMemErr();
    Size size = GetHandleSize(SYN68K_TO_US(handle));
    ROMlib_carbon_LMSetMemErr(error);
    return size;
}

/* Construct the same pair of movable handles as the saved ColorSync opener.
   The primary location remains borrowed. External locations retain PEF
   handling, including ColorSync's file and procedure ownership rules. */
static int color_profile_open_memory(ROMlib_carbon_ppc_frame* call, int creating)
{
    unsigned char* output = color_profile_memory(call->words[0], 4, 1);
    uint32_t location_address = call->words[1];
    const unsigned char* location = color_profile_memory(location_address, 2, 0);
    if (!output || (!location && (location_address || !creating)))
        return color_profile_result(call, paramErr);
    unsigned kind = location ? color_read(location, 2) : 0;
    unsigned char location_data[10] = {0};
    if (kind) {
        size_t location_size = kind == 2 ? 6 : 10;
        location = color_profile_memory(location_address, location_size, 0);
        if (!location)
            return color_profile_result(call, paramErr);
        memcpy(location_data, location, location_size);
    }
    color_write(output, 4, 0);
    uint32_t backing = color_read(location_data + 2, 4);
    uint32_t buffer_capacity = kind == 6 ? color_read(location_data + 6, 4) : 0;
    if ((kind && !backing) || (kind == 6 && !buffer_capacity))
        return color_profile_result(call, color_profile_invalid_location);

    enum { header_size = 128, directory_offset = 132, profile_size = color_profile_header_data + header_size };

    uint32_t count = 0;
    unsigned char* snapshot = NULL;
    if (!creating) {
        Size handle_size = kind == 2 ? color_profile_handle_size(backing) : 0;
        if (handle_size < 0)
            return color_profile_result(call, color_profile_invalid_location);
        const uint32_t source = kind == 2 ? color_profile_indirect(backing) : backing;
        const uint32_t capacity = kind == 2 ? (uint32_t)handle_size : buffer_capacity;
        if (capacity < directory_offset)
            return color_profile_result(call, eofErr);
        const unsigned char* input = color_profile_memory(source, directory_offset, 0);
        if (!input)
            return color_profile_result(call, color_profile_invalid_location);
        if (color_read(input + 36, 4) != UINT32_C(0x61637370) || input[8] != 2)
            return color_profile_result(call, -170);
        const uint32_t extent = color_read(input, 4);
        count = color_read(input + header_size, 4);
        const uint64_t directory_end = directory_offset + UINT64_C(12) * count;
        if (extent > capacity || directory_end > extent || count > (INT32_MAX - 4) / 16)
            return color_profile_result(call, color_profile_invalid);
        input = color_profile_memory(source, (size_t)directory_end, 0);
        if (!input)
            return color_profile_result(call, color_profile_invalid);
        for (uint32_t i = 0; i < count; ++i) {
            const unsigned char* entry = input + directory_offset + (size_t)i * 12;
            if ((uint64_t)color_read(entry + 4, 4) + color_read(entry + 8, 4) > extent)
                return color_profile_result(call, color_profile_invalid);
        }
        snapshot = malloc((size_t)directory_end);
        if (!snapshot)
            return color_profile_result(call, memFullErr);
        memcpy(snapshot, input, (size_t)directory_end);
    }
    Handle profile = NewHandleClear(profile_size);
    Handle directory = profile ? NewHandleClear(4 + count * 16) : NULL;
    if (!directory) {
        OSErr status = MemError();
        if (profile)
            DisposHandle(profile);
        free(snapshot);
        ROMlib_carbon_LMSetMemErr(status);
        return color_profile_result(call, status);
    }
    const uint32_t profile_address = US_TO_SYN68K(profile);
    const uint32_t directory_address = US_TO_SYN68K(directory);
    unsigned char* record = color_profile_memory(color_profile_indirect(profile_address), profile_size, 1);
    unsigned char* table = color_profile_memory(color_profile_indirect(directory_address), 4 + count * 16, 1);
    if (!record || !table) {
        DisposHandle(directory);
        DisposHandle(profile);
        free(snapshot);
        return color_profile_result(call, color_profile_invalid);
    }
    color_write(record, 4, UINT32_C(0x61636573));
    color_write(record + 4, 4, UINT32_C(0x61637370));
    color_write(record + color_profile_tags, 4, directory_address);
    memcpy(record + color_profile_location, location_data, sizeof location_data);
    record[color_profile_location_is_open] = !creating;
    color_write(record + color_profile_owners, 4, 1);
    color_write(record + color_profile_header_size, 4, header_size);
    color_write(table, 4, count);
    if (snapshot) {
        memcpy(record + color_profile_header_data, snapshot, header_size);
        for (uint32_t i = 0; i < count; ++i)
            memcpy(table + 4 + (size_t)i * 16, snapshot + directory_offset + (size_t)i * 12, 12);
    }
    else
        color_write(record + color_profile_header_data + 8, 4, UINT32_C(0x02000000));
    free(snapshot);
    color_write(output, 4, profile_address);
    return color_profile_result(call, noErr);
}

static int color_profile_open(ROMlib_carbon_ppc_frame* call, int creating)
{
    const char* name = creating ? "CMNewProfile" : "CMOpenProfile";
    ROMlib_carbon_reference reference;
    if (!ROMlib_carbon_load_reference("ColorSync", name, &reference))
        return ROMlib_carbon_report_failure("Missing ColorSync profile opener");
    if (reference.code_offset == (creating ? 0x14efc : 0x14ecc) && reference.code_size == 0x366ec &&
        reference.toc_offset == 0x464 && reference.toc_size == 0x1670) {
        const unsigned char* location = color_profile_memory(call->words[1], 2, 0);
        unsigned kind = location ? color_read(location, 2) : 0;
        if (kind == 2 || kind == 6 || (creating && !kind))
            return color_profile_open_memory(call, creating);
        if (!call->words[0] || (!creating && !call->words[1]))
            return color_profile_result(call, paramErr);
    }
    return ROMlib_carbon_invoke_frame(reference.function, call);
}

static int carbon_CMOpenProfile(ROMlib_carbon_ppc_frame* call)
{
    return color_profile_open(call, 0);
}

static int carbon_CMNewProfile(ROMlib_carbon_ppc_frame* call)
{
    return color_profile_open(call, 1);
}

static int color_profile_close_memory(ROMlib_carbon_ppc_frame* call, unsigned char* record,
                                      const ROMlib_carbon_reference* reference)
{
    unsigned kind = color_read(record + color_profile_location, 2);
    unsigned char* edits = record + color_profile_edit_location;
    const uint32_t profile = call->words[0];
    const uint32_t directory = color_read(record + color_profile_tags, 4);
    const uint32_t backing = kind == 2 ? color_read(record + color_profile_location + 2, 4) : 0;
    const uint32_t edit_handle = record[color_profile_edit_open] ? color_read(edits + 2, 4) : 0;
    if ((kind && kind != 2 && kind != 6) || record[color_profile_location_is_private] ||
        color_read(record + color_profile_location + 0x106, 2) || color_read(edits + 0x106, 2) ||
        color_read(record + color_profile_owners, 4) != 1 || directory == profile || directory == backing ||
        color_profile_handle_size(directory) < 4 ||
        (record[color_profile_edit_owned] &&
         (!edit_handle || color_read(edits, 2) != 2 || edit_handle == profile || edit_handle == directory ||
          edit_handle == backing || color_profile_handle_size(edit_handle) < 0)) ||
        (record[color_profile_edit_open] && !record[color_profile_edit_owned]))
        return ROMlib_carbon_invoke_frame(reference->function, call);
    color_write(record, 4, UINT32_C(0x78787878));
    if (edit_handle)
        DisposHandle(SYN68K_TO_US(edit_handle));
    DisposHandle(SYN68K_TO_US(directory));
    DisposHandle(SYN68K_TO_US(profile));
    return color_profile_result(call, noErr);
}

static int color_profile_edit_directory(ROMlib_carbon_ppc_frame* call, enum color_profile_operation operation,
                                        uint32_t profile, uint32_t handle, uint32_t count, const unsigned char* table,
                                        const unsigned char* source)
{
    const uint32_t target_key = call->words[2];
    if (count > (INT32_MAX - 4) / 16 - 1)
        return color_profile_result(call, memFullErr);
    const uint32_t old_size = 4 + count * 16;
    Handle tags = SYN68K_TO_US(handle);
    if (color_profile_handle_size(handle) < (Size)old_size)
        return color_profile_result(call, color_profile_invalid);
    const unsigned char* target =
        operation == color_profile_alias ? color_profile_find_tag(table, count, target_key) : NULL;
    unsigned char item[16];
    if (operation == color_profile_alias) {
        memcpy(item, source, sizeof item);
        color_write(item, 4, target_key);
        if (target) {
            /* Replacing a pre-existing reference does not change the directory size. */
            memcpy((unsigned char*)target, item, sizeof item);
            return color_profile_result(call, noErr);
        }
    }
    const uint32_t new_count = operation == color_profile_remove ? count - 1 : count + 1;
    const uint32_t new_size = 4 + new_count * 16;
    unsigned char* contents = NULL;
    if (operation == color_profile_remove) {
        contents = malloc(new_size);
        if (!contents)
            return color_profile_result(call, memFullErr);
        size_t offset = source - table;
        memcpy(contents, table, offset);
        memcpy(contents + offset, source + 16, new_size - offset);
    }
    SetHandleSize(tags, new_size);
    OSErr status = MemError();
    if (!status) {
        /* Resizing can move both unlocked handles. */
        unsigned char* resized = color_profile_memory(color_profile_indirect(handle), new_size, 1);
        unsigned char* record = color_profile_memory(color_profile_indirect(profile), color_profile_header_data, 1);
        if (!resized || !record)
            status = color_profile_invalid;
        else {
            if (contents)
                memcpy(resized, contents, new_size);
            else
                memcpy(resized + old_size, item, sizeof item);
            color_write(resized, 4, new_count);
            color_write(record + color_profile_flags, 4,
                        color_read(record + color_profile_flags, 4) | color_profile_dirty_directory);
        }
    }
    free(contents);
    return color_profile_result(call, status);
}

/* ColorSync stores {signature, offset, size, source} in its live tag table.
   A source of one refers to the private edit backing store. */
static int color_profile_map_payload(const unsigned char* record, const unsigned char* tag, uint32_t offset,
                                     uint32_t amount, const unsigned char** input)
{
    if (tag[12] > 1)
        return color_profile_external_payload;
    const unsigned char* location = record + (tag[12] ? color_profile_edit_location : color_profile_location);
    const uint32_t kind = color_read(location, 2);
    uint32_t source = color_read(location + 2, 4);
    const uint64_t start = (uint64_t)color_read(tag + 4, 4) + offset;
    if (kind == 2) {
        Size size = color_profile_handle_size(source);
        if (size < 0 || start + amount > (uint32_t)size)
            return color_profile_invalid;
        source = color_profile_indirect(source);
    }
    else if (kind != 3 && kind != 6)
        return color_profile_external_payload;
    if (!source || source + start > UINT32_MAX || (kind == 6 && start + amount > color_read(location + 6, 4)))
        return color_profile_invalid;
    *input = color_profile_memory((uint32_t)(source + start), amount, 0);
    return *input ? noErr : paramErr;
}

/* The saved writer serializes the directory in tag order and shares payloads
   only when their source, offset and length match. */
static uint64_t color_profile_layout(const unsigned char* table, uint32_t count, uint32_t* offsets)
{
    uint64_t size = 132 + UINT64_C(12) * count;
    for (uint32_t i = 0; i < count && size <= INT32_MAX; ++i) {
        const unsigned char* tag = table + 4 + (size_t)i * 16;
        uint32_t previous = 0;
        while (previous < i && memcmp(tag + 4, table + 8 + (size_t)previous * 16, 9) != 0)
            ++previous;
        if (offsets)
            offsets[i] = previous == i ? (uint32_t)size : offsets[previous];
        if (previous == i)
            size += (color_read(tag + 8, 4) + UINT64_C(3)) & ~UINT64_C(3);
    }
    return size;
}

/* Build the image before changing the backing store, which may also contain
   the input tags. */
static int color_profile_pack(const unsigned char* record, const unsigned char* table, uint32_t count,
                              unsigned char** packed, uint32_t* packed_size)
{
    uint32_t* offsets = count ? malloc((size_t)count * sizeof *offsets) : NULL;
    if (count && !offsets)
        return memFullErr;
    const uint64_t size = color_profile_layout(table, count, offsets);
    unsigned char* image = size <= INT32_MAX ? calloc(1, (size_t)size) : NULL;
    if (!image) {
        free(offsets);
        return memFullErr;
    }
    memcpy(image, record + color_profile_header_data, 128);
    color_write(image, 4, (uint32_t)size);
    color_write(image + 128, 4, count);
    int status = noErr;
    uint32_t next_payload = 132 + count * 12;
    for (uint32_t i = 0; i < count; ++i) {
        const unsigned char* tag = table + 4 + (size_t)i * 16;
        unsigned char* entry = image + 132 + (size_t)i * 12;
        const uint32_t length = color_read(tag + 8, 4);
        memcpy(entry, tag, 12);
        color_write(entry + 4, 4, offsets[i]);
        if (length && offsets[i] == next_payload) {
            const unsigned char* source;
            status = color_profile_map_payload(record, tag, 0, length, &source);
            if (status)
                break;
            memcpy(image + next_payload, source, length);
            next_payload += (length + 3) & ~UINT32_C(3);
        }
    }
    free(offsets);
    if (status) {
        free(image);
        return status;
    }
    *packed = image;
    *packed_size = (uint32_t)size;
    return noErr;
}

static void color_profile_note_serialized_size(uint32_t profile, uint32_t size)
{
    unsigned char* record = color_profile_memory(color_profile_indirect(profile), color_profile_header_data + 4, 1);
    if (record && color_read(record + color_profile_header_data, 4) != size) {
        color_write(record + color_profile_header_data, 4, size);
        color_write(record + color_profile_flags, 4,
                    color_read(record + color_profile_flags, 4) | color_profile_dirty_header);
    }
}

enum {
    color_spool_chunk = 4096,
    color_spool_open_read = 1,
    color_spool_open_write = 2,
    color_spool_read = 3,
    color_spool_write = 4,
    color_spool_close = 5,
};

static int color_profile_spool(ROMlib_carbon_ppc_frame* call, const unsigned char* image, uint32_t size)
{
    unsigned char* block = (unsigned char*)NewPtr(color_spool_chunk + 4);
    if (!block)
        return color_profile_result(call, MemError());
    ROMlib_carbon_ppc_frame spool = *call;
    spool.words[0] = color_spool_open_write;
    spool.words[1] = US_TO_SYN68K(block);
    spool.words[2] = 0;
    spool.words[4] = call->words[2];
    color_write(block, 4, 0);
    int ok = carbon_InvokeCMFlattenUPP(&spool);
    int16_t status = (int16_t)spool.words[0];
    for (uint32_t offset = 0; ok && !status && offset < size;) {
        uint32_t amount = MIN(size - offset, color_spool_chunk);
        color_write(block, 4, amount);
        memcpy(block + 4, image + offset, amount);
        spool.words[0] = color_spool_write;
        spool.words[2] = US_TO_SYN68K(block + 4);
        ok = carbon_InvokeCMFlattenUPP(&spool);
        status = (int16_t)spool.words[0];
        const uint32_t written = color_read(block, 4);
        if (ok && !status && (!written || written > amount))
            status = paramErr;
        offset += written <= amount ? written : 0;
    }
    if (ok && !status) {
        spool.words[0] = color_spool_close;
        spool.words[2] = 0;
        ok = carbon_InvokeCMFlattenUPP(&spool);
        status = (int16_t)spool.words[0];
    }
    DisposPtr((Ptr)block);
    return ok && color_profile_result(call, status);
}

static int color_profile_read_spool(ROMlib_carbon_ppc_frame* call, uint32_t capacity, unsigned char** image,
                                    uint32_t* size, OSErr* status)
{
    unsigned char* block = (unsigned char*)NewPtr(color_spool_chunk + 4);
    if (!block) {
        *status = MemError();
        return 1;
    }
    ROMlib_carbon_ppc_frame spool = *call;
    spool.words[0] = color_spool_open_read;
    spool.words[1] = US_TO_SYN68K(block);
    spool.words[2] = 0;
    spool.words[3] = call->words[2];
    spool.words[4] = call->words[1];
    color_write(block, 4, 0);
    int ok = carbon_InvokeCMFlattenUPP(&spool);
    *status = (int16_t)spool.words[0];
    if (!ok || *status) {
        DisposPtr((Ptr)block);
        return ok;
    }

    /* The ICC size is the first word. Buffer the stream before modifying the destination. */
    unsigned char prefix[4];
    uint32_t offset = 0;
    while (ok && !*status && (!*image || offset < *size)) {
        uint32_t requested = *image ? MIN(*size - offset, color_spool_chunk) : color_spool_chunk;
        color_write(block, 4, requested);
        spool.words[0] = color_spool_read;
        spool.words[2] = US_TO_SYN68K(block + 4);
        ok = carbon_InvokeCMFlattenUPP(&spool);
        OSErr result = (int16_t)spool.words[0];
        if (!ok)
            break;
        if (result && result != eofErr) {
            *status = result;
            break;
        }
        uint32_t received = color_read(block, 4);
        if (!received || received > requested) {
            *status = received ? paramErr : eofErr;
            break;
        }
        uint32_t consumed = 0;
        if (!*image) {
            consumed = MIN(received, sizeof prefix - offset);
            memcpy(prefix + offset, block + 4, consumed);
            offset += consumed;
            if (offset == sizeof prefix) {
                *size = color_read(prefix, 4);
                if (*size < sizeof prefix)
                    *status = color_profile_invalid;
                else if (*size > capacity || !(*image = malloc(*size)))
                    *status = memFullErr;
                else
                    memcpy(*image, prefix, sizeof prefix);
            }
        }
        if (*image) {
            uint32_t amount = MIN(received - consumed, *size - offset);
            memcpy(*image + offset, block + 4 + consumed, amount);
            offset += amount;
        }
        if (!*status && result == eofErr && (!*image || offset < *size))
            *status = eofErr;
    }
    if (ok) {
        spool.words[0] = color_spool_close;
        spool.words[2] = 0;
        color_write(block, 4, 0);
        ok = carbon_InvokeCMFlattenUPP(&spool);
        if (!*status)
            *status = (int16_t)spool.words[0];
    }
    DisposPtr((Ptr)block);
    return ok;
}

static int carbon_NCMUnflattenProfile(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference reference;
    if (!ROMlib_carbon_load_reference("ColorSync", "NCMUnflattenProfile", &reference))
        return ROMlib_carbon_report_failure("Missing ColorSync profile spool reader");
    if (reference.code_offset != 0x10a50 || reference.code_size != 0x366ec || reference.toc_offset != 0x464 ||
        reference.toc_size != 0x1670)
        return ROMlib_carbon_invoke_frame(reference.function, call);
    const unsigned char* location = color_profile_memory(call->words[0], 2, 0);
    unsigned char* preferred_missing = color_profile_memory(call->words[3], 1, 1);
    if (!location || !call->words[1] || (call->words[3] && !preferred_missing))
        return color_profile_result(call, paramErr);
    unsigned kind = color_read(location, 2);
    if (kind != 2 && kind != 6)
        return ROMlib_carbon_invoke_frame(reference.function, call);
    location = color_profile_memory(call->words[0], kind == 2 ? 6 : 10, 0);
    if (!location)
        return color_profile_result(call, paramErr);
    if (preferred_missing)
        *preferred_missing = 0;
    const uint32_t backing = color_read(location + 2, 4);
    const uint32_t capacity = kind == 2 ? INT32_MAX : MIN(color_read(location + 6, 4), (uint32_t)INT32_MAX);
    if (!backing || !capacity || (kind == 2 && color_profile_handle_size(backing) < 0))
        return color_profile_result(call, color_profile_invalid_location);
    unsigned char* image = NULL;
    uint32_t size = 0;
    OSErr status = noErr;
    const int ok = color_profile_read_spool(call, capacity, &image, &size, &status);
    if (ok && !status && kind == 2) {
        const Size handle_size = color_profile_handle_size(backing);
        if (handle_size < 0)
            status = color_profile_invalid_location;
        else if (handle_size != (Size)size) {
            SetHandleSize((Handle)SYN68K_TO_US(backing), size);
            status = MemError();
        }
    }
    if (ok && !status) {
        unsigned char* output = color_profile_memory(kind == 2 ? color_profile_indirect(backing) : backing, size, 1);
        if (!output)
            status = color_profile_invalid_location;
        else
            memcpy(output, image, size);
    }
    free(image);
    return ok && color_profile_result(call, status);
}

static int color_profile_export_image(ROMlib_carbon_ppc_frame* call, enum color_profile_operation operation,
                                      uint32_t profile, const unsigned char* record, uint32_t directory,
                                      const unsigned char* table, uint32_t count,
                                      const ROMlib_carbon_reference* reference)
{
    const int copying = operation == color_profile_copy;
    unsigned char* output = color_profile_memory(call->words[copying ? 0 : 4], copying ? 4 : 1, 1);
    if ((!output && (copying || call->words[4])) || (!copying && !call->words[2]))
        return color_profile_result(call, paramErr);
    const unsigned char* destination = NULL;
    unsigned kind = 0;
    uint32_t backing = 0, capacity = 0;
    Size old_size = 0;
    if (copying) {
        const unsigned char* location = color_profile_memory(call->words[1], 2, 0);
        kind = location ? color_read(location, 2) : 0;
        if (kind != 2 && kind != 6)
            return ROMlib_carbon_invoke_frame(reference->function, call);
        destination = color_profile_memory(call->words[1], kind == 2 ? 6 : 10, 0);
        if (!destination)
            return color_profile_result(call, paramErr);
        backing = color_read(destination + 2, 4);
        const unsigned char* source = record + color_profile_location;
        const uint32_t edit_handle =
            record[color_profile_edit_open] && color_read(record + color_profile_edit_location, 2) == 2
                ? color_read(record + color_profile_edit_location + 2, 4)
                : 0;
        color_write(output, 4, 0);
        if (!backing || (color_read(source, 2) == kind && color_read(source + 2, 4) == backing))
            return color_profile_result(call, color_profile_invalid_location);
        if (kind == 2 && (backing == profile || backing == directory || backing == edit_handle))
            return color_profile_result(call, color_profile_invalid_location);
        old_size = kind == 2 ? color_profile_handle_size(backing) : 0;
        capacity = kind == 6 ? color_read(destination + 6, 4) : INT32_MAX;
        if (old_size < 0)
            return color_profile_result(call, color_profile_invalid_location);
    }
    if (!table || count > (INT32_MAX - 4) / 16 || color_profile_handle_size(directory) < (Size)(4 + count * 16) ||
        color_read(record + color_profile_header_size, 4) != 128 ||
        !color_profile_memory(color_profile_indirect(profile), color_profile_header_data + 128, 0))
        return ROMlib_carbon_invoke_frame(reference->function, call);
    unsigned char* image;
    uint32_t size;
    int status = color_profile_pack(record, table, count, &image, &size);
    if (status == color_profile_external_payload)
        return ROMlib_carbon_invoke_frame(reference->function, call);
    if (status) {
        if (!copying && output)
            *output = 0;
        return color_profile_result(call, status);
    }
    if (!copying) {
        color_profile_note_serialized_size(profile, size);
        int ok = color_profile_spool(call, image, size);
        free(image);
        if (output)
            *output = 0;
        return ok;
    }
    if (size > capacity)
        status = memFullErr;
    else if (kind == 2 && size != (uint32_t)old_size) {
        SetHandleSize(SYN68K_TO_US(backing), size);
        status = MemError();
    }
    if (!status) {
        unsigned char* target = color_profile_memory(kind == 2 ? color_profile_indirect(backing) : backing, size, 1);
        if (!target)
            status = color_profile_invalid_location;
        else {
            memcpy(target, image, size);
            color_profile_note_serialized_size(profile, size);
        }
    }
    free(image);
    if (status)
        return color_profile_result(call, status);
    return carbon_CMOpenProfile(call);
}

static int color_profile_writeback(ROMlib_carbon_ppc_frame* call, uint32_t base, const unsigned char* record,
                                   uint32_t directory, const unsigned char* table, uint32_t count,
                                   const ROMlib_carbon_reference* reference)
{
    const unsigned char* location = record + color_profile_location;
    const unsigned char* edits = record + color_profile_edit_location;
    const unsigned kind = color_read(location, 2);
    const uint32_t backing = color_read(location + 2, 4);
    const int has_edits = record[color_profile_edit_open];
    const uint32_t edit_handle = has_edits ? color_read(edits + 2, 4) : 0;
    const uint32_t directory_size = 4 + count * 16;
    const size_t profile_size = color_profile_header_data + 128;
    const Size edit_size = has_edits ? color_profile_handle_size(edit_handle) : 0;
    unsigned char* live = color_profile_memory(base, profile_size, 1);
    if ((kind != 2 && kind != 6) || color_read(record + color_profile_header_size, 4) != 128 || !live ||
        color_read(live + color_profile_header_data + 36, 4) != UINT32_C(0x61637370) || !table ||
        backing == edit_handle || backing == directory || backing == call->words[0] ||
        (has_edits && (edit_handle == directory || edit_handle == call->words[0])) ||
        color_profile_handle_size(directory) < (Size)directory_size ||
        (has_edits && (color_read(edits, 2) != 2 || !record[color_profile_edit_owned] ||
                       !record[color_profile_edit_delete] || edit_size < 0)))
        return ROMlib_carbon_invoke_frame(reference->function, call);
    const Size old_size = kind == 2 ? color_profile_handle_size(backing) : 0;
    if (old_size < 0)
        return ROMlib_carbon_invoke_frame(reference->function, call);
    for (uint32_t i = 0; i < count; ++i) {
        const unsigned char* tag = table + 4 + (size_t)i * 16;
        uint64_t end = (uint64_t)color_read(tag + 4, 4) + color_read(tag + 8, 4);
        if ((tag[12] == 1 && (!has_edits || end > (uint32_t)edit_size)) || tag[12] > 1 ||
            (tag[12] == 0 && !record[color_profile_location_is_open]) ||
            (tag[12] == 0 && kind == 2 && end > (uint32_t)old_size))
            return ROMlib_carbon_invoke_frame(reference->function, call);
    }
    unsigned char* image;
    uint32_t size;
    int status = color_profile_pack(record, table, count, &image, &size);
    if (status == color_profile_external_payload)
        return ROMlib_carbon_invoke_frame(reference->function, call);
    if (status)
        return color_profile_result(call, status);
    if (kind == 6 && size > color_read(location + 6, 4)) {
        free(image);
        return ROMlib_carbon_invoke_frame(reference->function, call);
    }
    if (kind == 2 && size != (uint32_t)old_size) {
        SetHandleSize(SYN68K_TO_US(backing), size);
        status = MemError();
    }
    if (!status) {
        unsigned char* target = color_profile_memory(kind == 2 ? color_profile_indirect(backing) : backing, size, 1);
        unsigned char* entries = color_profile_memory(color_profile_indirect(directory), directory_size, 1);
        live = color_profile_memory(color_profile_indirect(call->words[0]), profile_size, 1);
        if (!target || !entries || !live)
            status = color_profile_invalid;
        else {
            memcpy(target, image, size);
            memcpy(live + color_profile_header_data, image, 128);
            for (uint32_t i = 0; i < count; ++i) {
                unsigned char* entry = entries + 4 + (size_t)i * 16;
                memcpy(entry, image + 132 + (size_t)i * 12, 12);
                color_write(entry + 12, 4, 0);
            }
            memset(live + color_profile_edit_location, 0, color_profile_owners - color_profile_edit_location);
            live[color_profile_location_is_open] = 1;
            color_write(live + color_profile_flags, 4, 0);
            if (edit_handle)
                DisposHandle(SYN68K_TO_US(edit_handle));
        }
    }
    free(image);
    return color_profile_result(call, status);
}

static int color_profile_shared_tag(const unsigned char* table, uint32_t count, const unsigned char* tag)
{
    uint32_t offset = color_read(tag + 4, 4);
    for (uint32_t i = 0; i < count; ++i) {
        const unsigned char* entry = table + 4 + (size_t)i * 16;
        if (entry != tag && entry[12] == tag[12] && color_read(entry + 4, 4) == offset)
            return 1;
    }
    return 0;
}

static int color_profile_write_tag(ROMlib_carbon_ppc_frame* call, enum color_profile_operation operation,
                                   const unsigned char* record, uint32_t directory, const unsigned char* table,
                                   uint32_t count, const unsigned char* tag, const ROMlib_carbon_reference* reference,
                                   const unsigned char* encoded, uint32_t encoded_size)
{
    const int description = operation == color_profile_set_descriptions;
    int partial = operation == color_profile_write_partial;
    uint32_t offset = partial ? call->words[2] : 0;
    uint32_t requested = description ? encoded_size : call->words[2 + partial];
    uint32_t input_address = description || operation == color_profile_resize ? 0 : call->words[3 + partial];
    if (!requested || (!description && operation != color_profile_resize && !input_address))
        return color_profile_result(call, paramErr);

    const unsigned char* location = record + color_profile_edit_location;
    int open = record[color_profile_edit_open];
    if (open && color_read(location, 2) != 2)
        return ROMlib_carbon_invoke_frame(reference->function, call);
    uint32_t edit_address = open ? color_read(location + 2, 4) : 0;
    Size used = open ? color_profile_handle_size(edit_address) : 0;
    if (used < 0 || count > (INT32_MAX - 20) / 16 || (tag && tag[12] > 1))
        return color_profile_result(call, color_profile_invalid);
    const uint32_t old_directory_size = 4 + count * 16;
    if (color_profile_handle_size(directory) < (Size)old_directory_size)
        return color_profile_result(call, color_profile_invalid);

    uint32_t length = tag ? color_read(tag + 8, 4) : 0;
    uint64_t final_length = (uint64_t)offset + requested;
    if (partial && final_length < length)
        final_length = length;
    if (final_length > INT32_MAX)
        return color_profile_result(call, paramErr);
    if (final_length > color_profile_memory_edit_limit)
        return ROMlib_carbon_invoke_frame(reference->function, call);
    uint32_t destination_offset = tag ? color_read(tag + 4, 4) : 0;
    int reuse = tag && tag[12] && open && final_length <= length && !color_profile_shared_tag(table, count, tag);
    if (!reuse) {
        if ((uint64_t)used + final_length > color_profile_memory_edit_limit)
            return ROMlib_carbon_invoke_frame(reference->function, call);
        destination_offset = used;
    }
    else if ((uint64_t)destination_offset + length > (uint64_t)used)
        return color_profile_result(call, color_profile_invalid);

    const unsigned char* replacement = description ? encoded : color_profile_memory(input_address, requested, 0);
    if (input_address && !replacement)
        return color_profile_result(call, paramErr);
    const unsigned char* previous = NULL;
    const size_t preserved = partial || operation == color_profile_resize ? MIN(final_length, length) : 0;
    if (preserved) {
        int status = color_profile_map_payload(record, tag, 0, preserved, &previous);
        if (status == color_profile_external_payload)
            return ROMlib_carbon_invoke_frame(reference->function, call);
        if (status)
            return color_profile_result(call, status);
    }
    /* Copy caller input before either movable handle can grow. */
    unsigned char* payload = calloc(final_length, 1);
    if (!payload)
        return color_profile_result(call, memFullErr);
    if (preserved)
        memcpy(payload, previous, preserved);
    if (replacement)
        memcpy(payload + offset, replacement, requested);

    uint32_t index = tag ? (uint32_t)(tag - table - 4) / 16 : count;
    Size edit_size = reuse ? used : used + (Size)final_length;
    Handle edits = open ? SYN68K_TO_US(edit_address) : NewHandle(edit_size);
    OSErr status = edits ? noErr : MemError();
    if (edits && open && !reuse) {
        /* The PEF editor can leave its private edit buffer locked. The
           payload is already copied, so the buffer can move during growth. */
        SignedByte state = HGetState(edits);
        status = MemError();
        if (!status) {
            if (state < 0)
                HUnlock(edits);
            SetHandleSize(edits, edit_size);
            status = MemError();
            if (state < 0) {
                HSetState(edits, state);
                ROMlib_carbon_LMSetMemErr(status);
            }
        }
    }
    uint32_t directory_size = old_directory_size + (index == count ? 16 : 0);
    int grew_directory = 0;
    if (!status && index == count) {
        SetHandleSize(SYN68K_TO_US(directory), directory_size);
        status = MemError();
        grew_directory = !status;
    }
    if (!status) {
        edit_address = US_TO_SYN68K(edits);
        unsigned char* bytes = color_profile_memory(color_profile_indirect(edit_address), edit_size, 1);
        unsigned char* entries = color_profile_memory(color_profile_indirect(directory), directory_size, 1);
        unsigned char* live =
            color_profile_memory(color_profile_indirect(call->words[0]), color_profile_header_data, 1);
        if (!bytes || !entries || !live)
            status = color_profile_invalid;
        else {
            memcpy(bytes + destination_offset, payload, final_length);
            unsigned char* entry = entries + 4 + (size_t)index * 16;
            color_write(entry, 4, description ? color_profile_description_tag : call->words[1]);
            color_write(entry + 4, 4, destination_offset);
            color_write(entry + 8, 4, final_length);
            if (index == count) {
                color_write(entry + 12, 4, 0);
                color_write(entries, 4, count + 1);
            }
            entry[12] = 1;
            if (!open) {
                color_write(live + color_profile_edit_location, 2, 2);
                color_write(live + color_profile_edit_location + 2, 4, edit_address);
                live[color_profile_edit_owned] = live[color_profile_edit_open] = live[color_profile_edit_delete] = 1;
            }
            color_write(live + color_profile_flags, 4,
                        color_read(live + color_profile_flags, 4) | color_profile_dirty_directory);
        }
    }
    if (status && edits) {
        if (grew_directory)
            SetHandleSize(SYN68K_TO_US(directory), old_directory_size);
        if (!open)
            DisposHandle(edits);
        else if (!reuse && color_profile_handle_size(edit_address) == edit_size)
            SetHandleSize(edits, used);
        ROMlib_carbon_LMSetMemErr(status);
    }
    free(payload);
    return color_profile_result(call, status);
}

/* The ICC v2 description tag has an ASCII string, optional UTF-16BE, and
   a classic Script Manager string. On disk all three counts include a NUL. */
static int color_profile_get_descriptions(ROMlib_carbon_ppc_frame* call, int script_only, const unsigned char* record,
                                          const unsigned char* tag, const ROMlib_carbon_reference* reference)
{
    const uint32_t ascii_address = script_only ? 0 : call->words[1];
    const uint32_t script_address = call->words[script_only ? 1 : 3];
    const uint32_t code_address = call->words[script_only ? 2 : 4];
    const uint32_t unicode_address = script_only ? 0 : call->words[5];
    unsigned char* ascii_size = script_only ? NULL : color_profile_memory(call->words[2], 4, 1);
    unsigned char* unicode_size = script_only ? NULL : color_profile_memory(call->words[6], 4, 1);
    unsigned char* code = color_profile_memory(code_address, 2, 1);
    if ((!script_only && ((call->words[2] && !ascii_size) || (call->words[6] && !unicode_size))) ||
        (ascii_address && !ascii_size) || (unicode_address && !unicode_size) || (code_address && !code))
        return color_profile_result(call, paramErr);
    const uint32_t ascii_capacity = ascii_size ? color_read(ascii_size, 4) : 0;
    const uint32_t unicode_capacity = unicode_size ? color_read(unicode_size, 4) : 0;
    const unsigned char* data = NULL;
    uint32_t ascii_count = 0, unicode_count = 0, script_count = 0, script_code = 0;
    size_t unicode_offset = 0, script_offset = 0;
    int append_zero = 0;
    int status = color_profile_missing_element;
    if (tag) {
        size_t length = color_read(tag + 8, 4);
        status = color_profile_map_payload(record, tag, 0, length, &data);
        if (status == color_profile_external_payload)
            return ROMlib_carbon_invoke_frame(reference->function, call);
        if (!status) {
            status = color_profile_invalid;
            if (length >= 23 && color_read(data, 4) == color_profile_description_tag) {
                ascii_count = color_read(data + 8, 4);
                if (ascii_count <= length - 23) {
                    unicode_offset = 20 + (size_t)ascii_count;
                    unicode_count = color_read(data + unicode_offset - 4, 4);
                    if (unicode_count <= (length - unicode_offset - 3) / 2) {
                        script_offset = unicode_offset + (size_t)unicode_count * 2;
                        script_code = color_read(data + script_offset, 2);
                        script_count = data[script_offset + 2];
                        if (script_count <= length - script_offset - 3)
                            status = noErr;
                    }
                }
            }
        }
    }
    const unsigned char* ascii = NULL;
    const unsigned char* unicode = NULL;
    const unsigned char* script = NULL;
    uint32_t script_length = 0, script_bytes = 0;
    if (status) {
        ascii_count = unicode_count = script_code = 0;
    }
    else {
        ascii = data + 12;
        unicode = data + unicode_offset;
        script = data + script_offset + 3;
        if (unicode_count) {
            append_zero = color_read(unicode + (size_t)(unicode_count - 1) * 2, 2) != 0;
            if (color_read(unicode, 2) == 0xfeff) {
                unicode += 2;
                --unicode_count;
            }
        }
        if (script_count && (!script_only || script_count > 1)) {
            script_bytes = script_count;
            script_length = script_count - 1;
        }
        else if (script_only) {
            script = ascii;
            script_bytes = script_length = ascii_count ? MIN(ascii_count - 1, 255) : 0;
            script_code = UINT16_MAX;
        }
    }
    const uint32_t ascii_copy = MIN(ascii_capacity, ascii_count);
    const uint32_t unicode_copy = MIN(unicode_capacity, unicode_count + append_zero);
    unsigned char* ascii_out = color_profile_memory(ascii_address, ascii_copy, 1);
    unsigned char* unicode_out = color_profile_memory(unicode_address, (uint64_t)unicode_copy * 2, 1);
    unsigned char* script_out = color_profile_memory(script_address, MAX(script_bytes, 1) + 1, 1);
    if ((ascii_address && !ascii_out) || (unicode_address && !unicode_out) || (script_address && !script_out))
        return color_profile_result(call, paramErr);
    if (ascii_out && ascii_copy)
        memmove(ascii_out, ascii, ascii_copy);
    if (unicode_out && unicode_copy) {
        memmove(unicode_out, unicode, (size_t)MIN(unicode_count, unicode_copy) * 2);
        if (append_zero)
            color_write(unicode_out + (size_t)(unicode_copy - 1) * 2, 2, 0);
    }
    else if (unicode_out && unicode_capacity && color_profile_memory(unicode_address, 2, 1))
        color_write(unicode_out, 2, 0);
    if (script_out) {
        if (script_bytes)
            memmove(script_out + 1, script, script_bytes);
        else
            script_out[1] = 0;
        script_out[0] = script_length;
    }
    if (ascii_size)
        color_write(ascii_size, 4, ascii_count);
    if (unicode_size)
        color_write(unicode_size, 4, unicode_count + append_zero);
    if (code)
        color_write(code, 2, script_code);
    return color_profile_result(call, status);
}

static int color_profile_set_descriptions_impl(ROMlib_carbon_ppc_frame* call, const unsigned char* record,
                                               uint32_t directory, const unsigned char* table, uint32_t count,
                                               const unsigned char* tag, const ROMlib_carbon_reference* reference)
{
    uint32_t ascii_count = call->words[2], unicode_count = call->words[6];
    if ((ascii_count && !call->words[1]) || (unicode_count && !call->words[5]))
        return color_profile_result(call, paramErr);
    ascii_count = ascii_count > 1 ? ascii_count : 0;
    unicode_count = unicode_count > 1 ? unicode_count : 0;
    if (unicode_count > INT32_MAX / 2)
        return color_profile_result(call, paramErr);
    const unsigned char* ascii = color_profile_memory(call->words[1], ascii_count, 0);
    const unsigned char* unicode = color_profile_memory(call->words[5], (uint64_t)unicode_count * 2, 0);
    const unsigned char* script = color_profile_memory(call->words[3], 1, 0);
    if ((ascii_count && !ascii) || (unicode_count && !unicode) || (call->words[3] && !script))
        return color_profile_result(call, paramErr);
    const unsigned script_count = script && script[0] ? MIN((unsigned)script[0] + 1, 255) : 0;
    if (script_count && !color_profile_memory(call->words[3], script_count, 0))
        return color_profile_result(call, paramErr);
    const uint64_t length = 23 + (uint64_t)ascii_count + (uint64_t)unicode_count * 2 + MAX(67, script_count);
    if (length > color_profile_memory_edit_limit)
        return ROMlib_carbon_invoke_frame(reference->function, call);
    int16_t script_code = (int16_t)call->words[4];
    if (script_code == -1 || script_code == -2) {
        ROMlib_carbon_ppc_frame arguments = *call;
        arguments.words[0] = script_code == -1 ? smSysScript : smKeyScript;
        if (!carbon_vector_GetScriptManagerVariable(&arguments))
            return 0;
        script_code = (int16_t)arguments.words[0];
    }
    unsigned char* data = calloc(1, (size_t)length);
    if (!data)
        return color_profile_result(call, memFullErr);
    const size_t unicode_offset = 20 + ascii_count;
    const size_t script_offset = unicode_offset + (size_t)unicode_count * 2;
    color_write(data, 4, color_profile_description_tag);
    color_write(data + 8, 4, ascii_count);
    if (ascii_count)
        memcpy(data + 12, ascii, ascii_count);
    color_write(data + unicode_offset - 4, 4, unicode_count);
    if (unicode_count)
        memcpy(data + unicode_offset, unicode, (size_t)unicode_count * 2);
    color_write(data + script_offset, 2, (uint16_t)script_code);
    data[script_offset + 2] = script_count;
    if (script_count)
        memcpy(data + script_offset + 3, script + 1, script_count - 1);
    int result = color_profile_write_tag(call, color_profile_set_descriptions, record, directory, table, count, tag,
                                         reference, data, (uint32_t)length);
    free(data);
    return result;
}

static int color_profile_read_tag(ROMlib_carbon_ppc_frame* call, const unsigned char* record, const unsigned char* tag,
                                  uint32_t offset, uint32_t size_pointer, uint32_t output_pointer,
                                  const ROMlib_carbon_reference* reference)
{
    const uint32_t length = color_read(tag + 8, 4);
    unsigned char* size = color_profile_memory(size_pointer, 4, 1);
    if (size_pointer && !size)
        return color_profile_result(call, paramErr);
    if (!output_pointer) {
        if (size)
            color_write(size, 4, length);
        return color_profile_result(call, noErr);
    }
    uint32_t amount = size ? color_read(size, 4) : length;
    if (!amount)
        return color_profile_result(call, noErr);
    if (offset >= length) {
        if (size)
            color_write(size, 4, 0);
        return color_profile_result(call, color_profile_index_error);
    }
    amount = MIN(amount, length - offset);

    const unsigned char* input;
    int status = color_profile_map_payload(record, tag, offset, amount, &input);
    if (status == color_profile_external_payload)
        return ROMlib_carbon_invoke_frame(reference->function, call);
    if (status)
        return color_profile_result(call, status);
    unsigned char* output = color_profile_memory(output_pointer, amount, 1);
    if (!output)
        return color_profile_result(call, paramErr);
    memmove(output, input, amount);
    if (size)
        color_write(size, 4, amount);
    return color_profile_result(call, noErr);
}

enum { color_named_field_size = 32, color_named_header_size = 84, color_named_color_size = 8 };

static size_t color_named_length(const unsigned char* name)
{
    const unsigned char* end = memchr(name, 0, color_named_field_size);
    return end ? (size_t)(end - name) : color_named_field_size;
}

static int color_named_store_string(uint32_t address, const unsigned char* name, size_t length)
{
    unsigned char* output = color_profile_memory(address, length + 1, 1);
    if (!output)
        return 0;
    memmove(output + 1, name, length);
    output[0] = length;
    return 1;
}

static int color_named_info(ROMlib_carbon_ppc_frame* call, const unsigned char* header, const unsigned char* palette,
                            uint32_t channels, uint32_t count, int status)
{
    unsigned char* outputs[4] = {0};
    const uint32_t result[] = {channels, header ? color_read(header + 16, 4) : 0,
                               header ? color_read(header + 20, 4) : 0, count};
    for (unsigned i = 0; i < 4; ++i) {
        if (!call->words[i + 1])
            continue;
        outputs[i] = color_profile_memory(call->words[i + 1], 4, 1);
        if (!outputs[i])
            return color_profile_result(call, paramErr);
    }
    if (!status) {
        unsigned char labels[2][color_named_field_size + 1];
        unsigned char* strings[2];
        for (unsigned i = 0; i < 2; ++i) {
            const unsigned char* field = palette + 20 + i * color_named_field_size;
            size_t length = color_named_length(field);
            strings[i] = color_profile_memory(call->words[5 + i], length + 1, 1);
            if (!strings[i])
                return color_profile_result(call, paramErr);
            labels[i][0] = length;
            memcpy(labels[i] + 1, field, length);
        }
        for (unsigned i = 0; i < 2; ++i)
            memcpy(strings[i], labels[i], labels[i][0] + 1);
    }
    for (unsigned i = 0; i < 4; ++i)
        if (outputs[i])
            color_write(outputs[i], 4, status ? 0 : result[i]);
    return color_profile_result(call, status);
}

static int color_named_operation(ROMlib_carbon_ppc_frame* call, enum color_profile_operation operation,
                                 const unsigned char* record, const unsigned char* header, const unsigned char* table,
                                 uint32_t tag_count, const ROMlib_carbon_reference* reference)
{
    const unsigned char* palette = NULL;
    int status = color_profile_invalid;
    uint32_t length = 0, count = 0, channels = 0;
    size_t stride = 0;
    const unsigned char* tag = header && color_read(header + 12, 4) == UINT32_C(0x6e6d636c)
                                   ? color_profile_find_tag(table, tag_count, UINT32_C(0x6e636c32))
                                   : NULL;
    if (tag) {
        length = color_read(tag + 8, 4);
        if (length >= color_named_header_size) {
            status = color_profile_map_payload(record, tag, 0, length, &palette);
            if (status == color_profile_external_payload)
                return ROMlib_carbon_invoke_frame(reference->function, call);
        }
    }
    if (!status) {
        count = color_read(palette + 12, 4);
        channels = color_read(palette + 16, 4);
        uint64_t record_size = 38 + UINT64_C(2) * channels;
        if (color_read(palette, 4) != UINT32_C(0x6e636c32) || record_size > SIZE_MAX ||
            count > (length - color_named_header_size) / record_size)
            status = color_profile_invalid;
        else
            stride = record_size;
    }
    if (operation == color_profile_named_info)
        return color_named_info(call, header, palette, channels, count, status);
    if (!status && channels != 0 && channels != 3 && channels != 4)
        status = color_profile_invalid;

    uint32_t index = 0;
    if (!status && (operation == color_profile_named_value || operation == color_profile_named_index)) {
        unsigned key = call->words[1];
        const unsigned char* length_byte = color_profile_memory(key, 1, 0);
        unsigned characters = length_byte ? *length_byte : 0;
        const unsigned char* name = color_profile_memory(key, characters + 1, 0);
        if (!length_byte || characters >= color_named_field_size || !name)
            status = paramErr;
        else {
            for (uint32_t i = 0; characters && i < count; ++i) {
                const unsigned char* entry = palette + color_named_header_size + i * stride;
                if (color_named_length(entry) == characters && !memcmp(entry, name + 1, characters)) {
                    index = i + 1;
                    break;
                }
            }
        }
    }
    else if (!status)
        index = call->words[1];
    if (!status && (!index || index > count))
        status = color_profile_named_not_found;

    if (operation == color_profile_named_index) {
        if (call->words[2]) {
            unsigned char* output = color_profile_memory(call->words[2], 4, 1);
            if (!output)
                return color_profile_result(call, paramErr);
            color_write(output, 4, status ? 0 : index);
        }
        return color_profile_result(call, status);
    }
    if (status)
        return color_profile_result(call, status);

    const unsigned char* entry = palette + color_named_header_size + (index - 1) * stride;
    if (operation == color_profile_named_name)
        return color_profile_result(
            call, color_named_store_string(call->words[2], entry, color_named_length(entry)) ? noErr : paramErr);
    unsigned char* device = color_profile_memory(call->words[2], color_named_color_size, 1);
    unsigned char* pcs = color_profile_memory(call->words[3], color_named_color_size, 1);
    if ((call->words[2] && !device) || (call->words[3] && !pcs))
        return color_profile_result(call, paramErr);
    unsigned char copy[14];
    memcpy(copy, entry + color_named_field_size, 6 + channels * 2);
    if (device && channels)
        memcpy(device, copy + 6, channels * 2);
    if (pcs)
        memcpy(pcs, copy, 6);
    return color_profile_result(call, noErr);
}

static int color_profile_operation(ROMlib_carbon_ppc_frame* call, enum color_profile_operation operation,
                                   const char* name, uint32_t reference_offset)
{
    const uint32_t address = call->words[operation == color_profile_copy ? 2 : 0];
    const int named = operation >= color_profile_named_info;
    if (operation == color_profile_script_description && (!address || !call->words[1] || !call->words[2]))
        return color_profile_result(call, paramErr);
    if (operation == color_profile_flatten && (!address || !call->words[2])) {
        unsigned char* preferred_missing = color_profile_memory(call->words[4], 1, 1);
        if (preferred_missing)
            *preferred_missing = 0;
        return color_profile_result(call, paramErr);
    }
    if (operation == color_profile_named_info && (!address || !call->words[5] || !call->words[6]))
        return color_named_info(call, NULL, NULL, 0, 0, paramErr);
    if (named && (!address || (operation != color_profile_named_info && !call->words[1]) ||
                  (operation == color_profile_named_name && !call->words[2])))
        return color_profile_result(call, paramErr);
    if (!address)
        return color_profile_result(call, color_profile_invalid);

    ROMlib_carbon_reference reference;
    if (!ROMlib_carbon_load_reference("ColorSync", name, &reference))
        return ROMlib_carbon_report_failure("Missing ColorSync profile implementation");
    if (reference.code_offset != reference_offset || reference.code_size != 0x366ec || reference.toc_offset != 0x464 ||
        reference.toc_size != 0x1670)
        return ROMlib_carbon_invoke_frame(reference.function, call);

    const uint32_t base = color_profile_indirect(address);
    const int writes_record = operation == color_profile_retain || operation == color_profile_close;
    unsigned char* record = color_profile_memory(base, color_profile_header_data, writes_record);
    if ((address & 1) || (base & 1) || !record || color_read(record, 4) != UINT32_C(0x61636573) ||
        color_read(record + 4, 4) != UINT32_C(0x61637370)) {
        if (named)
            return color_named_operation(call, operation, NULL, NULL, NULL, 0, &reference);
        if (operation == color_profile_flatten)
            return ROMlib_carbon_invoke_frame(reference.function, call);
        return color_profile_result(call, color_profile_invalid);
    }

    const uint32_t key = call->words[1];
    if (operation == color_profile_update && !color_read(record + color_profile_flags, 4))
        return color_profile_result(call, noErr);
    if (operation == color_profile_location_legacy || operation == color_profile_location_sized)
        return color_profile_get_location(call, record, operation == color_profile_location_sized);
    if (operation == color_profile_close) {
        uint32_t owners = color_read(record + color_profile_owners, 4);
        if (owners <= 1)
            return color_profile_close_memory(call, record, &reference);
        color_write(record + color_profile_owners, 4, owners - 1);
        return color_profile_result(call, noErr);
    }
    if (operation == color_profile_set_header) {
        const unsigned char* source = color_profile_memory(key, 128, 0);
        unsigned char* destination = color_profile_memory(base, color_profile_header_data + 128, 1);
        if (!source || !destination)
            return color_profile_result(call, paramErr);
        /* The header's size tracks serialization, independent of supplied header bytes. */
        uint32_t serialized_size = color_read(destination + color_profile_header_data, 4);
        memmove(destination + color_profile_header_data, source, 128);
        color_write(destination + color_profile_header_data, 4, serialized_size);
        color_write(destination + color_profile_flags, 4,
                    color_read(destination + color_profile_flags, 4) | color_profile_dirty_header);
        return color_profile_result(call, noErr);
    }
    if (operation <= color_profile_header) {
        uint32_t owners = color_read(record + color_profile_owners, 4);
        uint32_t flags = color_read(record + color_profile_flags, 4);
        if (operation == color_profile_retain) {
            color_write(record + color_profile_owners, 4, owners + 1);
            return color_profile_result(call, noErr);
        }
        uint32_t size = operation == color_profile_header     ? color_read(record + color_profile_header_size, 4)
                        : operation == color_profile_modified ? 1
                                                              : 4;
        if (size > 128)
            return color_profile_result(call, color_profile_invalid);
        unsigned char* out = color_profile_memory(key, size, 1);
        if (!out)
            return color_profile_result(call, paramErr);
        if (operation == color_profile_header) {
            const int dirty = (flags & color_profile_dirty_directory) != 0;
            unsigned char* live = color_profile_memory(base, color_profile_header_data + size, dirty);
            if (!live)
                return color_profile_result(call, color_profile_invalid);
            unsigned char* header = live + color_profile_header_data;
            if (dirty) {
                const uint32_t directory = color_read(record + color_profile_tags, 4);
                const uint32_t table_address = color_profile_indirect(directory);
                const uint32_t count = color_profile_indirect(table_address);
                const uint64_t table_size = 4 + UINT64_C(16) * count;
                if (size != 128 || table_size > INT32_MAX || color_profile_handle_size(directory) < (Size)table_size)
                    return ROMlib_carbon_invoke_frame(reference.function, call);
                const unsigned char* table = color_profile_memory(table_address, table_size, 0);
                if (!table)
                    return color_profile_result(call, color_profile_invalid);
                const uint64_t packed_size = color_profile_layout(table, count, NULL);
                if (packed_size > INT32_MAX)
                    return color_profile_result(call, memFullErr);
                if (packed_size != color_read(header, 4)) {
                    color_write(header, 4, (uint32_t)packed_size);
                    color_write(live + color_profile_flags, 4, flags | color_profile_dirty_header);
                }
            }
            memmove(out, header, size);
        }
        else
            color_write(out, size, operation == color_profile_modified ? flags != 0 : owners);
        return color_profile_result(call, noErr);
    }

    uint32_t handle = color_read(record + color_profile_tags, 4);
    uint32_t table_address = color_profile_indirect(handle);
    uint32_t count = color_profile_indirect(table_address);
    if (operation == color_profile_count) {
        unsigned char* out = color_profile_memory(key, 4, 1);
        if (!out)
            return color_profile_result(call, paramErr);
        color_write(out, 4, count);
        return color_profile_result(call, noErr);
    }
    const uint64_t table_size = 4 + UINT64_C(16) * count;
    const int editing = operation == color_profile_remove || operation == color_profile_alias;
    const unsigned char* table =
        table_size <= SIZE_MAX ? color_profile_memory(table_address, table_size, editing) : NULL;
    if (count && !table)
        return color_profile_result(call, color_profile_invalid);
    if (operation == color_profile_copy || operation == color_profile_flatten)
        return color_profile_export_image(call, operation, address, record, handle, table, count, &reference);
    if (operation == color_profile_update) {
        if (table_size > INT32_MAX)
            return color_profile_result(call, color_profile_invalid);
        return color_profile_writeback(call, base, record, handle, table, count, &reference);
    }
    if (operation >= color_profile_descriptions && operation <= color_profile_set_descriptions) {
        const unsigned char* description = color_profile_find_tag(table, count, color_profile_description_tag);
        if (operation == color_profile_set_descriptions)
            return color_profile_set_descriptions_impl(call, record, handle, table, count, description, &reference);
        return color_profile_get_descriptions(call, operation == color_profile_script_description, record, description,
                                              &reference);
    }
    if (named) {
        const unsigned char* header = color_profile_memory(base, color_profile_header_data + 24, 0);
        return color_named_operation(call, operation, record, header ? header + color_profile_header_data : NULL, table,
                                     count, &reference);
    }

    const unsigned char* tag = NULL;
    if (operation == color_profile_info || operation == color_profile_get_index) {
        if (!key || key > count)
            return color_profile_result(call, color_profile_index_error);
        tag = table + 4 + (size_t)(key - 1) * 16;
    }
    else {
        if (!key || (operation == color_profile_alias && !call->words[2]))
            return color_profile_result(call, paramErr);
        tag = color_profile_find_tag(table, count, key);
    }
    if (operation == color_profile_exists) {
        unsigned char* out = color_profile_memory(call->words[2], 1, 1);
        if (!out)
            return color_profile_result(call, paramErr);
        *out = tag != NULL;
        return color_profile_result(call, noErr);
    }
    if (operation == color_profile_write || operation == color_profile_write_partial ||
        operation == color_profile_resize)
        return color_profile_write_tag(call, operation, record, handle, table, count, tag, &reference, NULL, 0);
    if (!tag)
        return color_profile_result(call, color_profile_missing_element);
    if (editing)
        return color_profile_edit_directory(call, operation, address, handle, count, table, tag);

    if (operation == color_profile_info) {
        unsigned char* signature = color_profile_memory(call->words[2], 4, 1);
        unsigned char* size = color_profile_memory(call->words[3], 4, 1);
        unsigned char* shared = color_profile_memory(call->words[4], 1, 1);
        if ((call->words[2] && !signature) || (call->words[3] && !size) || (call->words[4] && !shared))
            return color_profile_result(call, paramErr);
        if (shared)
            *shared = color_profile_shared_tag(table, count, tag);
        if (signature)
            color_write(signature, 4, color_read(tag, 4));
        if (size)
            color_write(size, 4, color_read(tag + 8, 4));
        return color_profile_result(call, noErr);
    }
    unsigned partial = operation == color_profile_get_partial;
    return color_profile_read_tag(call, record, tag, partial ? call->words[2] : 0, call->words[2 + partial],
                                  call->words[3 + partial], &reference);
}

#define CARBON_COLOR_PROFILE(name, operation, offset)                                   \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call)                             \
    {                                                                                   \
        return color_profile_operation(call, color_profile_##operation, #name, offset); \
    }

CARBON_COLOR_PROFILE(CMCloneProfileRef, retain, 0x154d8)
CARBON_COLOR_PROFILE(CMGetProfileRefCount, ref_count, 0x15504)
CARBON_COLOR_PROFILE(CMProfileModified, modified, 0x154a8)
CARBON_COLOR_PROFILE(CMGetProfileHeader, header, 0x15534)
CARBON_COLOR_PROFILE(CMCountProfileElements, count, 0x1532c)
CARBON_COLOR_PROFILE(CMProfileElementExists, exists, 0x1524c)
CARBON_COLOR_PROFILE(CMGetIndProfileElementInfo, info, 0x1535c)
CARBON_COLOR_PROFILE(CMGetProfileElement, get, 0x15280)
CARBON_COLOR_PROFILE(CMGetIndProfileElement, get_index, 0x152bc)
CARBON_COLOR_PROFILE(CMGetPartialProfileElement, get_partial, 0x153fc)
CARBON_COLOR_PROFILE(CMCloseProfile, close, 0x15220)
CARBON_COLOR_PROFILE(CMGetProfileLocation, location_legacy, 0x151c4)
CARBON_COLOR_PROFILE(NCMGetProfileLocation, location_sized, 0x14f64)
CARBON_COLOR_PROFILE(CMSetProfileHeader, set_header, 0x15564)
CARBON_COLOR_PROFILE(CMRemoveProfileElement, remove, 0x15398)
CARBON_COLOR_PROFILE(CMSetProfileElementReference, alias, 0x153c8)
CARBON_COLOR_PROFILE(CMSetProfileElement, write, 0x152f4)
CARBON_COLOR_PROFILE(CMSetPartialProfileElement, write_partial, 0x1546c)
CARBON_COLOR_PROFILE(CMSetProfileElementSize, resize, 0x15438)
CARBON_COLOR_PROFILE(CMUpdateProfile, update, 0x151f4)
CARBON_COLOR_PROFILE(CMCopyProfile, copy, 0x14f2c)
CARBON_COLOR_PROFILE(CMFlattenProfile, flatten, 0x1054c)
CARBON_COLOR_PROFILE(CMGetProfileDescriptions, descriptions, 0x15594)
CARBON_COLOR_PROFILE(CMGetScriptProfileDescription, script_description, 0x15674)
CARBON_COLOR_PROFILE(CMSetProfileDescriptions, set_descriptions, 0x1576c)
CARBON_COLOR_PROFILE(CMGetNamedColorInfo, named_info, 0x12e98)
CARBON_COLOR_PROFILE(CMGetNamedColorValue, named_value, 0x130b8)
CARBON_COLOR_PROFILE(CMGetIndNamedColorValue, named_indexed_value, 0x13384)
CARBON_COLOR_PROFILE(CMGetNamedColorIndex, named_index, 0x135e0)
CARBON_COLOR_PROFILE(CMGetNamedColorName, named_name, 0x13818)

#undef CARBON_COLOR_PROFILE
#endif
