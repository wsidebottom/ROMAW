/*+===================================================================
  File:      carbon_threads.h

  Summary:   Declares the cooperative Carbon Thread Manager and its ROMAW backend.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Original adapter following Apple's Thread Manager API contracts.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

#ifndef ROMAW_PPC_THREADS_H
#define ROMAW_PPC_THREADS_H
#include "synppc/ppc.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ROMlib_ppc_threads ROMlib_ppc_threads;

typedef struct {
    void* opaque;
    ROMlib_ppc_memory_fn memory;
    ROMlib_pef_allocate_fn allocate;
    ROMlib_pef_release_fn release;
    uint32_t (*resolve)(void*, uint32_t);
    int (*execute)(void*, uint32_t, uint32_t, uint32_t, uint32_t*);
    void (*switch_context)(void*, void (*)(void*), void*);
    int (*exiting)(void*);
    uint32_t (*stack_space)(void*, uint32_t);
    int (*invoke)(void*, uint32_t, uint32_t*, unsigned, uint32_t*);
} ROMlib_ppc_thread_environment;

int ROMlib_ppc_threads_handles(const char* name);
int ROMlib_ppc_threads_service(ROMlib_ppc_threads** scheduler, const ROMlib_ppc_thread_environment* environment,
                               const char* name, ROMlib_ppc_registers* registers, int save_stack);
void ROMlib_ppc_threads_destroy(ROMlib_ppc_threads* scheduler);
#ifdef __cplusplus
}
#endif
#endif
