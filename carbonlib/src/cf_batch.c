/*+===================================================================
  File:      cf_batch.c

  Summary:   Native collection/value control flow shares the original initialized runtime. These helpers never fabricate
             classes, allocator contexts or cached globals.

  Classes:   None.

  Functions: cfp_call, cfp_callback, cfp_clear, cfp_failure, cfp_get, cfp_handle_data, cfp_move, cfp_private, cfp_read, cfp_reference,
             cfp_toc, cfp_write

  Origin:    Existing repository source in packages/carbonlib/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Native collection/value control flow shares the original initialized runtime.
   These helpers never fabricate classes, allocator contexts or cached globals. */
#include "cf_batch.h"
#include "rsys/mman_private.h"

int cfp_reference(const char* name, uint32 offset, const uint32* arguments, unsigned count,
                  ROMlib_carbon_reference* reference, uint32* result)
{
    if (!ROMlib_carbon_reference_function("CoreFoundationLib", name, reference))
        return 0;
    if (reference->code_offset == offset && reference->code_size == 0x5dee0 && !reference->toc_offset &&
        reference->toc && reference->toc_size == 0x72a8)
        return 1;
    uint32 value = 0;
    if (ROMlib_carbon_invoke_callback(reference->function, arguments, count, &value) && result)
        *result = value;
    return 0;
}

int cfp_call(const char* name, const uint32* arguments, unsigned count, uint32* result)
{
    ROMlib_carbon_reference reference;
    return ROMlib_carbon_reference_function("CoreFoundationLib", name, &reference) &&
           ROMlib_carbon_invoke_callback(reference.function, arguments, count, result);
}

int cfp_private(const ROMlib_carbon_reference* reference, uint32 offset, const uint32* arguments, unsigned count,
                uint32* result)
{
    uint32 entry;
    memcpy(&entry, reference->function, 4);
    _Alignas(16) uint32 vector[2] = {CL(CL(entry) - reference->code_offset + offset), CL(US_TO_SYN68K(reference->toc))};
    return ROMlib_carbon_invoke_callback(vector, arguments, count, result);
}

uint32 cfp_read(uint32 address)
{
    uint32 value = 0;
    cfp_get(address, &value);
    return value;
}

int cfp_get(uint32 address, uint32* value)
{
    const void* memory = ROMlib_carbon_guest_memory(address, 4, 0);
    if (!memory)
        return 0;
    uint32 encoded;
    memcpy(&encoded, memory, 4);
    *value = CL(encoded);
    return 1;
}

int cfp_write(uint32 address, uint32 value)
{
    void* memory = ROMlib_carbon_guest_memory(address, 4, 1);
    value = CL(value);
    if (!memory)
        return 0;
    memcpy(memory, &value, 4);
    return 1;
}

uint32 cfp_toc(const ROMlib_carbon_reference* reference, unsigned offset)
{
    return cfp_read(US_TO_SYN68K(reference->toc) + offset);
}

void* cfp_handle_data(uint32 handle, uint32* extent, int write_access)
{
    const void* master = handle ? ROMlib_carbon_try_guest_memory(handle, 4, 0) : NULL;
    if (!master)
        return NULL;
    uint32 encoded;
    memcpy(&encoded, master, sizeof encoded);
    const uint32 data = CL(encoded);
    const block_header_t* block =
        data >= HEADER_SIZE ? ROMlib_carbon_try_guest_memory(data - HEADER_SIZE, HEADER_SIZE, 0) : NULL;
    if (!block || USE(block) != REL || PSIZE(block) < HEADER_SIZE + SIZEC(block) || LSIZE(block) > INT32_MAX)
        return NULL;
    *extent = LSIZE(block);
    return ROMlib_carbon_try_guest_memory(data, *extent, write_access);
}

int cfp_move(uint32 destination, uint32 source, uint32 length)
{
    if (!length)
        return 1;
    const void* input = ROMlib_carbon_guest_memory(source, length, 0);
    void* output = ROMlib_carbon_guest_memory(destination, length, 1);
    if (!input || !output)
        return 0;
    memmove(output, input, length);
    return 1;
}

int cfp_clear(uint32 destination, uint32 length)
{
    if (!length)
        return 1;
    void* memory = ROMlib_carbon_guest_memory(destination, length, 1);
    if (!memory)
        return 0;
    memset(memory, 0, length);
    return 1;
}

int cfp_failure(const char* message)
{
    return ROMlib_carbon_report_failure(message);
}

int cfp_callback(uint32 procedure, const uint32* arguments, unsigned count, uint32* result)
{
    return ROMlib_carbon_invoke_callback(SYN68K_TO_US_CHECK0(procedure), arguments, count, result);
}
