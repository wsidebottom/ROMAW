/*+===================================================================
  File:      memory-manager.h

  Summary:   Native Memory Manager heap ownership and guest allocation interface.

  Classes:   ROMlib_native_memory and ROMlib_native_memory_backend.

  Functions: ROMlib_native_memory_handles, ROMlib_native_memory_service, ROMlib_native_memory_destroy.

  Origin:    ROMAW implementation of the Memory Manager import contract;
             signatures checked against the user's local System reference.

  Copyright and Legal notices: Applicable repository license: synppc/COPYING.
===================================================================+*/

#ifndef ROMLIB_NATIVE_MEMORY_MANAGER_H
#define ROMLIB_NATIVE_MEMORY_MANAGER_H

#include <stddef.h>
#include <stdint.h>

typedef struct ROMlib_native_memory ROMlib_native_memory;

typedef struct {
    void* opaque;
    void* (*allocate)(void* opaque, uint32_t size, unsigned kind, uint32_t* address);
    void (*release)(void* opaque, uint32_t address);
    void* (*memory)(void* opaque, uint32_t address, size_t size, int writing);
    void (*space)(void* opaque, unsigned kind, uint32_t* total, uint32_t* largest);
} ROMlib_native_memory_backend;

int ROMlib_native_memory_handles(const char* name);
int ROMlib_native_memory_service(ROMlib_native_memory** state, const ROMlib_native_memory_backend* backend,
                                 const char* name, const uint32_t* arguments, uint32_t* result);
void ROMlib_native_memory_destroy(ROMlib_native_memory* state);

#endif
