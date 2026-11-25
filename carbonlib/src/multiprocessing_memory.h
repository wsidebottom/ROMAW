/*+===================================================================
  File:      multiprocessing_memory.h

  Summary:   Allocates aligned guest memory for Multiprocessing Services.

  Classes:   None.

  Functions: ROMlib_carbon_mp_memory_reset and private PPC allocation adapters.

  Origin:    CarbonLib 1.4 and System 9.2.1 Multiprocessing Services ('ncod' 3).

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_MULTIPROCESSING_MEMORY_H
#define ROMAW_CARBON_MULTIPROCESSING_MEMORY_H

#include "cf_batch.h"

typedef struct mp_memory_block {
    struct mp_memory_block* next;
    const void* context;
    Ptr backing;
    uint32 address, size;
} mp_memory_block;

static mp_memory_block* mp_memory_blocks;

static void mp_memory_release(mp_memory_block** link)
{
    mp_memory_block* block = *link;
    *link = block->next;
    DisposPtr(block->backing);
    free(block);
}

void ROMlib_carbon_mp_memory_reset(const void* context)
{
    mp_memory_block** link = &mp_memory_blocks;
    while (*link) {
        if ((*link)->context == context)
            mp_memory_release(link);
        else
            link = &(*link)->next;
    }
}

static bool mp_memory_reference(const char* name, uint32 offset, ROMlib_carbon_reference* reference)
{
    if (!ROMlib_carbon_probe_reference("CarbonLib", name, reference))
        return false;
    if (reference->code_size != 0xb8fb4 || reference->code_offset != offset || !reference->toc ||
        reference->toc_offset != 0x2f78 || reference->toc_size != 0x10f0c)
        return true;
    const Byte* initialized = ROMlib_carbon_try_guest_memory(US_TO_SYN68K(reference->toc) + 0x6b94, 1, 0);
    return !initialized || *initialized;
}

static uint32 mp_memory_allocate(uint32 size, uint8 alignment, uint32 options)
{
    /* Pool-restricted allocation requires the real MP manager. */
    enum { clear = 1, global = 2, resident = 4 };

    const void* context = ROMlib_carbon_mp_context();
    if (!context || !size || size > INT32_MAX - 7 || (options & ~(clear | global | resident)))
        return 0;
    if (alignment == 254)
        alignment = 12;
    else if (alignment == 255)
        alignment = 5;
    else if (alignment < 3)
        alignment = 3;
    if (alignment > 16)
        return 0;

    const uint32 padded = (size + 7) & ~7u;
    const uint32 mask = (1u << alignment) - 1;
    if (padded > INT32_MAX - mask)
        return 0;
    mp_memory_block* block = malloc(sizeof *block);
    if (!block)
        return 0;
    block->backing = _NewPtr_flags(padded + mask, TRUE, (options & clear) != 0);
    if (!block->backing) {
        free(block);
        return 0;
    }
    block->address = (US_TO_SYN68K(block->backing) + mask) & ~mask;
    block->size = padded;
    block->context = context;
    block->next = mp_memory_blocks;
    mp_memory_blocks = block;
    return block->address;
}

static int mp_memory_new(ROMlib_carbon_ppc_frame* call, bool aligned)
{
    const char* name = aligned ? "MPAllocateAligned" : "MPAllocate";
    ROMlib_carbon_reference reference;
    if (mp_memory_reference(name, aligned ? 0x54a0 : 0x5c60, &reference))
        return ROMlib_carbon_invoke_frame(reference.function, call);
    call->words[0] = mp_memory_allocate(call->words[0], aligned ? call->words[1] : 255, aligned ? call->words[2] : 0);
    return 1;
}

static int carbon_MPAllocate(ROMlib_carbon_ppc_frame* call)
{
    return mp_memory_new(call, false);
}

static int carbon_MPAllocateAligned(ROMlib_carbon_ppc_frame* call)
{
    return mp_memory_new(call, true);
}

static int mp_memory_existing(ROMlib_carbon_ppc_frame* call, bool release)
{
    mp_memory_block** link = &mp_memory_blocks;
    while (*link && (*link)->address != call->words[0])
        link = &(*link)->next;
    if (*link) {
        call->words[0] = (*link)->size;
        if (release)
            mp_memory_release(link);
        return 1;
    }
    ROMlib_carbon_reference reference;
    if (call->words[0] &&
        mp_memory_reference(release ? "MPFree" : "MPGetAllocatedBlockSize", release ? 0x54d0 : 0x54fc, &reference))
        return ROMlib_carbon_invoke_frame(reference.function, call);
    call->words[0] = 0;
    return 1;
}

static int carbon_MPFree(ROMlib_carbon_ppc_frame* call)
{
    return mp_memory_existing(call, true);
}

static int carbon_MPGetAllocatedBlockSize(ROMlib_carbon_ppc_frame* call)
{
    return mp_memory_existing(call, false);
}

#endif
