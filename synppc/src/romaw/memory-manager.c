/*+===================================================================
  File:      memory-manager.c

  Summary:   Native guest heaps, pointer allocation and Memory Manager ownership.

  Classes:   ROMlib_native_memory, native_heap and native_block.

  Functions: ROMlib_native_memory_handles, ROMlib_native_memory_service, ROMlib_native_memory_destroy.

  Origin:    ROMAW implementation; argument order and result contracts checked
             against the user's local System Memory Manager disassembly.

  Copyright and Legal notices: Applicable repository license: synppc/COPYING.
===================================================================+*/

#include "memory-manager.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct native_heap {
    struct native_heap* next;
    uint32_t address;
    unsigned kind;
} native_heap;

typedef struct native_block {
    struct native_block* next;
    native_heap* heap;
    uint32_t address, size;
    void* bytes;
} native_block;

struct ROMlib_native_memory {
    ROMlib_native_memory_backend backend;
    native_heap *heaps, *default_heap;
    native_block* blocks;
};

static native_heap* find_heap(ROMlib_native_memory* state, uint32_t address)
{
    if (!address)
        return state->default_heap;
    for (native_heap* heap = state->heaps; heap; heap = heap->next)
        if (heap->address == address)
            return heap;
    return NULL;
}

static native_block* find_block(ROMlib_native_memory* state, uint32_t address)
{
    for (native_block* block = state->blocks; block; block = block->next)
        if (block->address == address)
            return block;
    return NULL;
}

static native_heap* new_heap(ROMlib_native_memory* state, unsigned kind)
{
    if (kind > 2)
        return NULL;
    native_heap* heap = calloc(1, sizeof *heap);
    if (!heap)
        return NULL;
    /* Heap references are opaque guest allocations, never host pointers or
       invented boot-time Memory Manager structures. */
    void* token = state->backend.allocate(state->backend.opaque, 4, kind, &heap->address);
    if (!token) {
        free(heap);
        return NULL;
    }
    memset(token, 0, 4);
    heap->kind = kind;
    heap->next = state->heaps;
    state->heaps = heap;
    return heap;
}

static native_block* new_block(ROMlib_native_memory* state, native_heap* heap, uint32_t size, int clear)
{
    if (!heap || size > INT32_MAX - 16)
        return NULL;
    native_block* block = calloc(1, sizeof *block);
    if (!block)
        return NULL;
    block->bytes = state->backend.allocate(state->backend.opaque, size ? size : 1, heap->kind, &block->address);
    if (!block->bytes) {
        free(block);
        return NULL;
    }
    if (clear)
        memset(block->bytes, 0, size);
    block->heap = heap;
    block->size = size;
    block->next = state->blocks;
    state->blocks = block;
    return block;
}

static void free_block(ROMlib_native_memory* state, native_block* block)
{
    native_block** link = &state->blocks;
    while (*link && *link != block)
        link = &(*link)->next;
    if (!*link)
        return;
    *link = block->next;
    state->backend.release(state->backend.opaque, block->address);
    free(block);
}

static void dispose_heap(ROMlib_native_memory* state, native_heap* heap)
{
    native_block *block = state->blocks, *next;
    while (block) {
        next = block->next;
        if (block->heap == heap)
            free_block(state, block);
        block = next;
    }
    native_heap** link = &state->heaps;
    while (*link && *link != heap)
        link = &(*link)->next;
    if (!*link)
        return;
    *link = heap->next;
    if (state->default_heap == heap)
        state->default_heap = NULL;
    state->backend.release(state->backend.opaque, heap->address);
    free(heap);
}

int ROMlib_native_memory_handles(const char* name)
{
    static const char* const names[] = {
        "MMNewHeap",       "MMDisposeHeap",     "MMGetDefaultHeap", "MMSetDefaultHeap", "MMAllocate",  "MMAllocateIn",
        "MMAllocateClear", "MMAllocateClearIn", "MMFree",           "MMReallocate",     "MMBlockSize", "MMGetHeap",
        "MMGetFreeSpace",  "MMSystemFreeSpace", "MMConfiguration",
    };
    for (unsigned i = 0; i < sizeof names / sizeof *names; ++i)
        if (!strcmp(names[i], name))
            return 1;
    return 0;
}

static void store_u32(void* output, uint32_t value)
{
    unsigned char* bytes = output;
    bytes[0] = value >> 24;
    bytes[1] = value >> 16;
    bytes[2] = value >> 8;
    bytes[3] = value;
}

int ROMlib_native_memory_service(ROMlib_native_memory** storage, const ROMlib_native_memory_backend* backend,
                                 const char* name, const uint32_t* args, uint32_t* result)
{
    if (!ROMlib_native_memory_handles(name))
        return 0;
    *result = 0;
    if (!*storage) {
        ROMlib_native_memory* state = calloc(1, sizeof *state);
        if (!state)
            return 1;
        state->backend = *backend;
        *storage = state;
    }
    ROMlib_native_memory* state = *storage;
    if (!strcmp(name, "MMConfiguration"))
        return 1;
    if (!strcmp(name, "MMNewHeap")) {
        /* Minimum/maximum chunk sizes are allocation-policy hints. This
           backend owns individual Macintosh blocks instead of cached chunks. */
        native_heap* heap = new_heap(state, args[0] & 255);
        *result = heap ? heap->address : 0;
        return 1;
    }
    if (!state->default_heap &&
        (!strcmp(name, "MMGetDefaultHeap") || !strcmp(name, "MMAllocate") || !strcmp(name, "MMAllocateClear") ||
         (!strcmp(name, "MMReallocate") && !args[0]) || (!strcmp(name, "MMGetFreeSpace") && !args[0]))) {
        state->default_heap = new_heap(state, 2);
        if (!state->default_heap)
            return 1;
    }
    if (!strcmp(name, "MMGetDefaultHeap")) {
        *result = state->default_heap ? state->default_heap->address : 0;
        return 1;
    }
    if (!strcmp(name, "MMSetDefaultHeap")) {
        native_heap* heap = find_heap(state, args[0]);
        if (!heap)
            return -1;
        *result = state->default_heap ? state->default_heap->address : 0;
        state->default_heap = heap;
        return 1;
    }
    if (!strcmp(name, "MMDisposeHeap")) {
        if (!args[0])
            return 1;
        native_heap* heap = find_heap(state, args[0]);
        if (!heap)
            return -1;
        dispose_heap(state, heap);
        return 1;
    }
    if (!strcmp(name, "MMGetFreeSpace") || !strcmp(name, "MMSystemFreeSpace")) {
        native_heap* heap = find_heap(state, args[0]);
        unsigned kind = !strcmp(name, "MMSystemFreeSpace") ? args[0] & 255 : heap ? heap->kind : 3;
        void *total_out = NULL, *largest_out = NULL;
        if (kind > 2)
            return -1;
        if (args[1] && !(total_out = backend->memory(backend->opaque, args[1], 4, 1)))
            return -1;
        if (args[2] && !(largest_out = backend->memory(backend->opaque, args[2], 4, 1)))
            return -1;
        uint32_t total, largest;
        backend->space(backend->opaque, kind, &total, &largest);
        if (total_out)
            store_u32(total_out, total);
        if (largest_out)
            store_u32(largest_out, largest);
        *result = largest;
        return 1;
    }
    if (!strcmp(name, "MMAllocate") || !strcmp(name, "MMAllocateClear") || !strcmp(name, "MMAllocateIn") ||
        !strcmp(name, "MMAllocateClearIn")) {
        const int in_heap = !strcmp(name, "MMAllocateIn") || !strcmp(name, "MMAllocateClearIn");
        native_heap* heap = in_heap ? find_heap(state, args[1]) : state->default_heap;
        if (!heap)
            return -1;
        native_block* block = new_block(state, heap, args[0], strstr(name, "Clear") != NULL);
        *result = block ? block->address : 0;
        return 1;
    }
    native_block* block = find_block(state, args[0]);
    if (!strcmp(name, "MMFree")) {
        if (!args[0])
            return 1;
        if (!block)
            return -1;
        free_block(state, block);
        return 1;
    }
    if (!strcmp(name, "MMReallocate")) {
        if (args[0] && !block)
            return -1;
        if (!args[1]) {
            if (block)
                free_block(state, block);
            return 1;
        }
        native_block* replacement = new_block(state, block ? block->heap : state->default_heap, args[1], 0);
        if (replacement && block) {
            memcpy(replacement->bytes, block->bytes, block->size < args[1] ? block->size : args[1]);
            free_block(state, block);
        }
        *result = replacement ? replacement->address : 0;
        return 1;
    }
    if (!strcmp(name, "MMBlockSize")) {
        *result = block ? block->size : 0;
        return 1;
    }
    if (!strcmp(name, "MMGetHeap")) {
        *result = block ? block->heap->address : 0;
        return 1;
    }
    return 0;
}

void ROMlib_native_memory_destroy(ROMlib_native_memory* state)
{
    if (!state)
        return;
    while (state->heaps)
        dispose_heap(state, state->heaps);
    free(state);
}
