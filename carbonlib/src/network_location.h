/*+===================================================================
  File:      network_location.h

  Summary:   Implements Network Services Location records and result-buffer iteration.

  Classes:   None.

  Functions: Private frame adapters for the Carbon export registry.

  Origin:    Universal Interfaces 3.4.2 NSLCore.h and System 9 NSLPPCLib.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_NETWORK_LOCATION_H
#define ROMAW_CARBON_NETWORK_LOCATION_H

#include "MemoryMgr.h"
#include "ResourceMgr.h"
#include "rsys/resource.h"
#include "rsys/system_resources.h"

enum {
    nsl_bad_parameter = -4176,
    nsl_bad_list = -4184,
    nsl_bad_data = -4193,
    nsl_bad_service = -4194,
    nsl_list_header = 8,
    nsl_record_tag = 0x4e534c5f,
    nsl_neighborhood_event = 7,
};

static void nsl_store_word(void* target, uint16 value)
{
    value = CW(value);
    memcpy(target, &value, sizeof value);
}

static void nsl_store_long(void* target, uint32 value)
{
    value = CL(value);
    memcpy(target, &value, sizeof value);
}

static uint32 nsl_read_long(const void* source)
{
    uint32 value;
    memcpy(&value, source, sizeof value);
    return CL(value);
}

static const char* nsl_string(uint32 address, size_t* length)
{
    *length = 0;
    for (size_t i = 0; address && i <= UINT16_MAX && i <= UINT32_MAX - address; ++i) {
        const char* character = ROMlib_carbon_try_guest_memory(address + i, 1, 0);
        if (!character)
            return NULL;
        if (!*character) {
            *length = i;
            return ROMlib_carbon_try_guest_memory(address, i + 1, 0);
        }
    }
    return NULL;
}

static int nsl_result(ROMlib_carbon_ppc_frame* call, uint32 result)
{
    call->words[0] = result;
    return 1;
}

/* Both list formats start with a count and a payload byte length. Services
   are Pascal strings. Protocols prefix each Pascal name with a record length
   and follow the name with an empty two-byte data field. */
static size_t nsl_list_encode(const char* list, size_t length, bool protocols, unsigned char* output)
{
    size_t used = 0, count = 0;
    for (size_t start = 0; start < length;) {
        size_t end = start;
        while (end < length && list[end] != ',')
            ++end;
        const size_t name_length = end - start;
        if (name_length > 127)
            return 0;
        if (name_length) {
            const size_t record_size = 1 + name_length + (protocols ? 4 : 0);
            if (record_size > UINT16_MAX - nsl_list_header - used)
                return 0;
            if (output) {
                unsigned char* entry = output + nsl_list_header + used;
                if (protocols) {
                    nsl_store_word(entry, record_size);
                    nsl_store_word(entry + record_size - 2, 0);
                    entry += 2;
                }
                *entry = name_length;
                memcpy(entry + 1, list + start, name_length);
            }
            used += record_size;
            ++count;
        }
        start = end + 1;
    }
    if (output) {
        nsl_store_long(output, count);
        nsl_store_long(output + 4, used);
    }
    return nsl_list_header + used;
}

static int carbon_NSLLibraryVersion(ROMlib_carbon_ppc_frame* call)
{
    return nsl_result(call, 0x10100);
}

static int carbon_NSLMakeNewServicesList(ROMlib_carbon_ppc_frame* call)
{
    size_t length;
    const char* list = nsl_string(call->words[0], &length);
    if (call->words[0] && !list)
        return nsl_result(call, 0);
    const size_t size = nsl_list_encode(list, length, false, NULL);
    Handle handle = size ? NewHandleClear(MAX(256, size)) : NULL;
    if (handle)
        nsl_list_encode(list, length, false, (unsigned char*)STARH(handle));
    return nsl_result(call, handle ? US_TO_SYN68K(handle) : 0);
}

static int carbon_NSLDisposeServicesList(ROMlib_carbon_ppc_frame* call)
{
    Handle list = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], sizeof(uint32), 1) : NULL;
    if (list)
        DisposHandle(list);
    return 1;
}

static bool nsl_list_payload(const unsigned char* list, size_t size, uint32* length)
{
    if (!list || size < nsl_list_header)
        return false;
    *length = nsl_read_long(list + 4);
    return *length <= size - nsl_list_header;
}

static int carbon_NSLAddServiceToServicesList(ROMlib_carbon_ppc_frame* call)
{
    /* Classic PPC returns NSLError in caller-provided storage. */
    uint32_t* result = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], 8, 1) : NULL;
    if (!result)
        return 0;
    const uint32 address = call->words[1];
    uint32 extent = 0, used;
    const unsigned char* list = cfp_handle_data(address, &extent, 0);
    size_t length;
    const char* name = nsl_string(call->words[2], &length);
    int status = noErr;
    if (!nsl_list_payload(list, extent, &used))
        status = nsl_bad_list;
    else if (!name || length > 127)
        status = nsl_bad_service;
    else if (length) {
        unsigned char entry[128] = {(unsigned char)length};
        memcpy(entry + 1, name, length);
        Handle handle = ROMlib_carbon_try_guest_memory(address, sizeof(uint32), 1);
        const uint32 needed = nsl_list_header + used + length + 1;
        if (needed > UINT16_MAX)
            status = memFullErr;
        else if (needed > extent) {
            SetHandleSize(handle, MAX(needed, extent + 256));
            status = MemError();
        }
        if (!status) {
            unsigned char* updated = (unsigned char*)STARH(handle);
            memcpy(updated + nsl_list_header + used, entry, length + 1);
            nsl_store_long(updated, nsl_read_long(updated) + 1);
            nsl_store_long(updated + 4, used + length + 1);
        }
    }
    nsl_store_long(result, status);
    nsl_store_long((unsigned char*)result + 4, 0);
    return 1;
}

static int carbon_NSLServiceIsInServiceList(ROMlib_carbon_ppc_frame* call)
{
    uint32 extent = 0, used;
    const unsigned char* list = cfp_handle_data(call->words[0], &extent, 0);
    size_t length;
    const char* name = nsl_string(call->words[1], &length);
    if (!name || !nsl_list_payload(list, extent, &used))
        return nsl_result(call, 0);
    uint32 count = nsl_read_long(list);
    if (!count)
        return nsl_result(call, 1);
    size_t offset = 0;
    for (const unsigned char* names = list + nsl_list_header; count && offset < used; --count) {
        const size_t entry = names[offset++];
        if (entry > used - offset)
            break;
        if (entry == length && !memcmp(names + offset, name, entry))
            return nsl_result(call, 1);
        offset += entry;
    }
    return nsl_result(call, 0);
}

static int carbon_NSLMakeNewNeighborhood(ROMlib_carbon_ppc_frame* call)
{
    size_t name_length, protocols_length;
    const char* name = nsl_string(call->words[0], &name_length);
    const char* protocols = nsl_string(call->words[1], &protocols_length);
    if ((call->words[0] && !name) || (call->words[1] && !protocols) || name_length > 127)
        return nsl_result(call, 0);
    const size_t payload = nsl_list_encode(protocols, protocols_length, true, NULL);
    unsigned char* neighborhood = payload ? (unsigned char*)NewPtrClear(1 + name_length + payload) : NULL;
    if (!neighborhood)
        return nsl_result(call, 0);
    neighborhood[0] = name_length;
    if (name_length)
        memcpy(neighborhood + 1, name, name_length);
    nsl_list_encode(protocols, protocols_length, true, neighborhood + 1 + name_length);
    return nsl_result(call, US_TO_SYN68K(neighborhood));
}

static uint32 nsl_neighborhood_size(uint32 address, uint32 available)
{
    const unsigned char* name_length = address ? ROMlib_carbon_try_guest_memory(address, 1, 0) : NULL;
    if (!name_length)
        return 0;
    const uint32 prefix = 1 + *name_length + nsl_list_header;
    if (prefix > available || address > UINT32_MAX - prefix)
        return 0;
    const unsigned char* record = ROMlib_carbon_try_guest_memory(address, prefix, 0);
    if (!record)
        return 0;
    const uint32 payload = nsl_read_long(record + prefix - 4);
    return payload <= available - prefix && payload <= UINT32_MAX - address - prefix &&
                   ROMlib_carbon_try_guest_memory(address, prefix + payload, 0)
               ? prefix + payload
               : 0;
}

static int carbon_NSLCopyNeighborhood(ROMlib_carbon_ppc_frame* call)
{
    const uint32 address = call->words[0];
    const uint32 size = nsl_neighborhood_size(address, UINT16_MAX);
    Ptr copy = size ? NewPtr(size) : NULL;
    if (copy)
        memcpy(copy, ROMlib_carbon_try_guest_memory(address, size, 0), size);
    return nsl_result(call, copy ? US_TO_SYN68K(copy) : 0);
}

static int carbon_NSLGetNeighborhoodLength(ROMlib_carbon_ppc_frame* call)
{
    return nsl_result(call, nsl_neighborhood_size(call->words[0], UINT16_MAX));
}

static int carbon_NSLGetNameFromNeighborhood(ROMlib_carbon_ppc_frame* call)
{
    const uint32 address = call->words[0];
    const unsigned char* length = address ? ROMlib_carbon_try_guest_memory(address, 1, 0) : NULL;
    return cfp_write(call->words[1], length ? address + 1 : 0) && cfp_write(call->words[2], length ? *length : 0);
}

static int nsl_dispose_pointer(ROMlib_carbon_ppc_frame* call)
{
    Ptr pointer = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], 1, 1) : NULL;
    if (pointer)
        DisposPtr(pointer);
    return nsl_result(call, 0);
}

static int carbon_NSLFreeTypedDataPtr(ROMlib_carbon_ppc_frame* call)
{
    return nsl_dispose_pointer(call);
}

static int carbon_NSLFreeNeighborhood(ROMlib_carbon_ppc_frame* call)
{
    return nsl_dispose_pointer(call);
}

static int carbon_NSLFreeURL(ROMlib_carbon_ppc_frame* call)
{
    return nsl_dispose_pointer(call);
}

static int carbon_NSLMakeServicesRequestPB(ROMlib_carbon_ppc_frame* call)
{
    const uint32 output = call->words[1];
    if (!output || !ROMlib_carbon_try_guest_memory(output, 4, 1))
        return nsl_result(call, paramErr);
    uint32 extent = 0, used = 0;
    const uint32 handle = call->words[0];
    const unsigned char* list = handle ? cfp_handle_data(handle, &extent, 0) : NULL;
    if (handle && !nsl_list_payload(list, extent, &used))
        return nsl_result(call, nsl_bad_list);
    if (used > UINT16_MAX - nsl_list_header)
        return nsl_result(call, nsl_bad_list);
    const uint32 payload = nsl_list_header + used;
    unsigned char* packet = (unsigned char*)NewPtrClear(8 + payload);
    if (packet) {
        nsl_store_long(packet, nsl_record_tag);
        nsl_store_long(packet + 4, payload + 1);
        if (handle) {
            list = cfp_handle_data(handle, &extent, 0);
            if (!list) {
                DisposPtr((Ptr)packet);
                return nsl_result(call, nsl_bad_list);
            }
            memcpy(packet + 8, list, payload);
        }
    }
    cfp_write(output, packet ? US_TO_SYN68K(packet) : 0);
    return nsl_result(call, packet ? noErr : memFullErr);
}

static int nsl_record_outputs(ROMlib_carbon_ppc_frame* call, unsigned index, uint32 pointer, uint16 length)
{
    void* size = call->words[index + 1] ? ROMlib_carbon_try_guest_memory(call->words[index + 1], 2, 1) : NULL;
    if (!size || !call->words[index] || !cfp_write(call->words[index], pointer))
        return nsl_result(call, paramErr);
    nsl_store_word(size, length);
    return nsl_result(call, noErr);
}

static int carbon_NSLParseServicesRequestPB(ROMlib_carbon_ppc_frame* call)
{
    const uint32 address = call->words[0];
    const unsigned char* packet = address ? ROMlib_carbon_try_guest_memory(address, 16, 0) : NULL;
    if (!packet || nsl_read_long(packet) != nsl_record_tag)
        return nsl_result(call, nsl_bad_data);
    const uint32 used = nsl_read_long(packet + 12);
    if (used > UINT16_MAX - nsl_list_header || !ROMlib_carbon_try_guest_memory(address, 16 + used, 0))
        return nsl_result(call, nsl_bad_data);
    return nsl_record_outputs(call, 1, address + 8, nsl_list_header + used);
}

static int carbon_NSLParseServiceRegistrationPB(ROMlib_carbon_ppc_frame* call)
{
    const uint32 address = call->words[0];
    const unsigned char* packet = address ? ROMlib_carbon_try_guest_memory(address, 8, 0) : NULL;
    if (!packet || nsl_read_long(packet) != nsl_record_tag)
        return nsl_result(call, nsl_bad_data);
    const uint32 extent = nsl_read_long(packet + 4);
    if (extent <= 8 || extent > UINT16_MAX || !ROMlib_carbon_try_guest_memory(address, extent, 0))
        return nsl_result(call, nsl_bad_data);
    const uint32 neighborhood = nsl_neighborhood_size(address + 8, extent - 8);
    if (!neighborhood)
        return nsl_result(call, nsl_bad_data);
    if (!nsl_record_outputs(call, 1, address + 8, neighborhood) || call->words[0])
        return 1;
    return nsl_record_outputs(call, 3, address + 8 + neighborhood, extent - 8 - neighborhood);
}

static int carbon_NSLGetServiceFromURL(ROMlib_carbon_ppc_frame* call)
{
    size_t length;
    const char* text = nsl_string(call->words[0], &length);
    if (!text)
        return nsl_result(call, nsl_bad_data);
    const char* slash = memchr(text, '/', length);
    const char* separator = NULL;
    if (slash) {
        for (separator = slash; separator > text && *separator != ':'; --separator)
            ;
        if (*separator != ':')
            separator = NULL;
    }
    else
        separator = memchr(text, ':', length);
    return separator ? nsl_record_outputs(call, 1, call->words[0], separator - text) : nsl_result(call, nsl_bad_data);
}

static int nsl_hex_digit(unsigned char character)
{
    if (character >= '0' && character <= '9')
        return character - '0';
    character |= 0x20;
    return character >= 'a' && character <= 'f' ? character - 'a' + 10 : -1;
}

static bool nsl_hex_escape(unsigned char character)
{
    if (character >= '0' && character <= '9')
        return false;
    const unsigned char lower = character | 0x20;
    if (lower >= 'a' && lower <= 'z')
        return false;
    return !character || !strchr("!$'()*+-._", character);
}

static int nsl_hex_text(ROMlib_carbon_ppc_frame* call, bool encode)
{
    enum { bad_escape = -4172, short_buffer = -4187 };

    static const char digits[] = "0123456789abcdef";
    const size_t length = (uint16)call->words[1];
    const uint32_t input_address = call->words[0], output_address = call->words[2];
    const uint32_t size_address = call->words[3], changed_address = call->words[4];
    const unsigned char* input =
        input_address ? ROMlib_carbon_try_guest_memory(input_address, length ? length : 1, 0) : NULL;
    void* size_output = size_address ? ROMlib_carbon_try_guest_memory(size_address, sizeof(uint16), 1) : NULL;
    unsigned char* changed_output = changed_address ? ROMlib_carbon_try_guest_memory(changed_address, 1, 1) : NULL;
    if (!size_output || !changed_output)
        return nsl_result(call, nsl_bad_parameter);

    uint16 capacity_be;
    memcpy(&capacity_be, size_output, sizeof capacity_be);
    const size_t capacity = CW(capacity_be);
    unsigned char* output =
        output_address ? ROMlib_carbon_try_guest_memory(output_address, capacity ? capacity : 1, 1) : NULL;
    int status = noErr;
    size_t written = 0;
    bool changed = false;
    /* Finish reading guest input before publishing bytes so overlapping buffers work. */
    unsigned char* buffer = NULL;
    if (!input || !output)
        status = nsl_bad_parameter;
    else if (length && capacity && !(buffer = malloc(MIN(capacity, length * (encode ? 3 : 1)))))
        status = memFullErr;

    for (size_t i = 0; !status && i < length; ++i) {
        const unsigned char character = input[i];
        if (encode && nsl_hex_escape(character)) {
            if (capacity - written < 3) {
                status = short_buffer;
                break;
            }
            buffer[written++] = '%';
            buffer[written++] = digits[character >> 4];
            buffer[written++] = digits[character & 15];
            changed = true;
            continue;
        }
        if (written == capacity) {
            status = short_buffer;
            break;
        }
        if (!encode && character == '%') {
            const int high = length - i >= 3 ? nsl_hex_digit(input[i + 1]) : -1;
            const int low = length - i >= 3 ? nsl_hex_digit(input[i + 2]) : -1;
            if (high < 0 || low < 0) {
                status = bad_escape;
                break;
            }
            buffer[written++] = (high << 4) | low;
            i += 2;
            changed = true;
        }
        else
            buffer[written++] = character;
    }
    if (!status && written)
        memcpy(output, buffer, written);
    free(buffer);
    nsl_store_word(size_output, status ? 0 : written);
    *changed_output = !status && changed;
    return nsl_result(call, status);
}

static int carbon_NSLHexEncodeText(ROMlib_carbon_ppc_frame* call)
{
    return nsl_hex_text(call, true);
}

static int carbon_NSLHexDecodeText(ROMlib_carbon_ppc_frame* call)
{
    return nsl_hex_text(call, false);
}

static int carbon_NSLGetDefaultDialogOptions(ROMlib_carbon_ppc_frame* call)
{
    enum { header = 6, fields = 4, field_size = 256 };

    unsigned char* options =
        call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], header + fields * field_size, 1) : NULL;
    if (!options)
        return nsl_result(call, nsl_bad_parameter);
    nsl_store_word(options, 0x0101);
    nsl_store_long(options + 2, 0);
    for (unsigned field = 0; field < fields; ++field)
        options[header + field * field_size] = 0;
    return nsl_result(call, noErr);
}

static OSErr nsl_error_strings(Handle resource, uint32 error, char* description, char* solution)
{
    /* NSLE has a word count followed by an OSStatus and two Pascal strings per entry. */
    const Size size = resource ? GetHandleSize(resource) : 0;
    if (size < 2)
        return resource ? nsl_bad_data : nsl_bad_parameter;
    const unsigned char* bytes = (const unsigned char*)STARH(resource);
    if (!bytes)
        return nsl_bad_data;
    const unsigned count = ((unsigned)bytes[0] << 8) | bytes[1];
    size_t offset = 2;
    for (unsigned entry = 0; entry < count; ++entry) {
        if ((size_t)size - offset < 6)
            return nsl_bad_data;
        const uint32 code = nsl_read_long(bytes + offset);
        offset += 4;
        const unsigned first = bytes[offset++];
        if ((size_t)size - offset <= first)
            return nsl_bad_data;
        const unsigned char* first_string = bytes + offset;
        offset += first;
        const unsigned second = bytes[offset++];
        if ((size_t)size - offset < second)
            return nsl_bad_data;
        if (code == error) {
            memcpy(description, first_string, first);
            description[first] = 0;
            memcpy(solution, bytes + offset, second);
            solution[second] = 0;
            return noErr;
        }
        offset += second;
    }
    return noErr;
}

static int carbon_NSLGetErrorStringsFromResource(ROMlib_carbon_ppc_frame* call)
{
    const FSSpec* input = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], sizeof(FSSpec), 0) : NULL;
    if (!input || !call->words[3] || !call->words[4])
        return nsl_result(call, nsl_bad_parameter);
    FSSpec spec = *input;
    const INTEGER original_map = C_CurResFile();
    const BOOLEAN original_loading = ResLoad;
    BOOLEAN in_chain;
    INTEGER opened;
    const bool borrowed = ROMlib_resource_file_already_open(&spec, &in_chain, &opened) && in_chain;
    if (borrowed)
        opened = CW(opened);
    else
        opened = C_FSpOpenResFile(&spec, fsRdPerm);
    if (opened == -1)
        return nsl_result(call, C_ResError());

    char description[256] = "", solution[256] = "";
    C_UseResFile(opened);
    OSErr status = C_ResError();
    if (!status) {
        C_SetResLoad(TRUE);
        Handle resource = C_Get1Resource(TICK("NSLE"), (INTEGER)call->words[2]);
        status = nsl_error_strings(resource, call->words[0], description, solution);
    }
    if (!borrowed)
        C_CloseResFile(opened);
    C_UseResFile(original_map);
    C_SetResLoad(original_loading);

    const size_t description_size = strlen(description) + 1, solution_size = strlen(solution) + 1;
    void* description_output = ROMlib_carbon_try_guest_memory(call->words[3], description_size, 1);
    void* solution_output = ROMlib_carbon_try_guest_memory(call->words[4], solution_size, 1);
    if (!description_output || !solution_output)
        return nsl_result(call, nsl_bad_parameter);
    memcpy(description_output, description, description_size);
    memcpy(solution_output, solution, solution_size);
    return nsl_result(call, status);
}

static int nsl_next_result(ROMlib_carbon_ppc_frame* call, bool neighborhood)
{
    const uint32 info_address = call->words[0], output = call->words[1], output_length = call->words[2];
    uint32 previous, item = 0, length = 0;
    if (!output || !output_length || !cfp_get(output, &previous))
        return nsl_result(call, 0);
    const unsigned char* info =
        info_address ? ROMlib_carbon_try_guest_memory(info_address, neighborhood ? 58 : 16, 0) : NULL;
    if (info && (!neighborhood || nsl_read_long(info + 54) == nsl_neighborhood_event)) {
        const uint32 address = nsl_read_long(info + 8), size = nsl_read_long(info + 12);
        const unsigned char* buffer = address && size <= INT32_MAX && size <= UINT32_MAX - address
                                          ? ROMlib_carbon_try_guest_memory(address, size, 0)
                                          : NULL;
        if (buffer && size && (!previous || (previous >= address && previous - address < size))) {
            uint32 offset = previous ? previous - address : 0;
            if (neighborhood) {
                if (previous) {
                    const uint32 prior_size = nsl_neighborhood_size(previous, size - offset);
                    offset = prior_size ? offset + prior_size : size;
                }
                if (offset < size)
                    length = nsl_neighborhood_size(address + offset, size - offset);
                if (length)
                    item = address + offset;
            }
            else {
                if (previous) {
                    const unsigned char* comma = memchr(buffer + offset, ',', size - offset);
                    offset = comma ? comma - buffer + 1 : size;
                }
                if (offset < size) {
                    const unsigned char* comma = memchr(buffer + offset, ',', size - offset);
                    length = comma ? comma - (buffer + offset) : size - offset;
                    item = address + offset;
                }
            }
        }
    }
    if (!cfp_write(output, item) || !cfp_write(output_length, length))
        return nsl_result(call, 0);
    return nsl_result(call, item != 0);
}

static int carbon_NSLGetNextUrl(ROMlib_carbon_ppc_frame* call)
{
    return nsl_next_result(call, false);
}

static int carbon_NSLGetNextNeighborhood(ROMlib_carbon_ppc_frame* call)
{
    return nsl_next_result(call, true);
}

#endif
