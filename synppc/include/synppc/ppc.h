/*+===================================================================
  File:      ppc.h

  Summary:   PowerPC execution interface for x86-64 hosts. Guest values are host-endian here.

  Classes:   Interface structures, including ROMlib_ppc_published_word.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in synppc/include/synppc; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* Portable PowerPC execution interface. Guest values are host-endian here. */
#ifndef SYN_PPC_H
#define SYN_PPC_H
#if !defined(__x86_64__)
#error "synppc requires an x86-64 host"
#endif
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t gpr[32];
    double fpr[32];
    uint32_t pc, lr, ctr, cr, xer, fpscr;
} ROMlib_ppc_registers;

typedef void* (*ROMlib_ppc_memory_fn)(void*, uint32_t, size_t, int);
typedef int (*ROMlib_ppc_service_fn)(void*, uint32_t, ROMlib_ppc_registers*);
typedef void (*ROMlib_ppc_poll_fn)(void*);
typedef struct ROMlib_ppc_translation_cache ROMlib_ppc_translation_cache;
typedef struct ROMlib_pef_image ROMlib_pef_image;
struct ROMlib_ppc_system750;
struct ROMlib_ppc_physical_bus;
struct ROMlib_ppc_bus_result;

typedef struct {
    void* opaque;
    ROMlib_ppc_memory_fn memory;
    /* Architectural mode fences enabled D-cache around service/poll callbacks
       and nested guest returns. Host RAM uses physical addresses; I-cache code
       publication still requires explicit maintenance. Instruction-limit CPU
       checkpoints preserve dirty data and are not implicit host boundaries. */
    ROMlib_ppc_service_fn service;
    ROMlib_ppc_poll_fn poll;
    /* Optional stable, writable RAM span. Other addresses use memory().
       Do not include overlays, protected pages or host-pointer aliases. */
    unsigned char* linear_memory;
    uint32_t linear_address, linear_size;
    uint64_t instruction_limit; /* zero permits an unrestricted application loop */
    uint64_t instructions;
    char error[256];
    /* Optional native blocks with checked, ahead-compiled semantic tails.
       Zero keeps interpretation only. Physical CPU/MMU support is separate. */
    uint32_t execution_flags;
    uint64_t native_blocks, native_instructions;
    /* Optional caller-owned, thread-confined cache. Retains translations across
       invocations and nested executions, with private invocation scratch.
       The caller must keep it alive until all executions have returned. */
    ROMlib_ppc_translation_cache* translation_cache;
    uint64_t native_compilations, native_cache_hits;
    /* Native semantic tails use ahead-compiled checked instruction kernels. */
    uint64_t native_kernel_instructions;
    /* Optional architectural 750 mode. State persists across invocations;
       physical_bus supplies RAM/device transfers and atomic PTE history.
       The checked instruction path owns MMU translation in this mode. */
    struct ROMlib_ppc_system750* system;
    const struct ROMlib_ppc_physical_bus* physical_bus;
    /* Optional embedding extension for opcode 6 at trusted service stubs only.
       Return 1 to dispatch, 0 for architectural illegal instruction, or -1
       for an embedding error. Called in the host floating environment. */
    int (*system_service_gate)(void*, uint32_t pc, uint32_t opcode);
    /* When supplied, check the physical word address from the successful
       instruction fetch (after translation and endian lane selection).
       Takes precedence over the effective-PC gate above. */
    int (*physical_service_gate)(void*, uint32_t physical, uint32_t opcode);
    /* Optional native service marshaller for DR-enabled guest arguments.
       On failure, return zero and supply the failed architectural bus result.
       A zero vector reports an embedding error. */
    int (*mapped_service)(void*, uint32_t, ROMlib_ppc_registers*, struct ROMlib_ppc_bus_result*);
    /* Supply board clocks/inputs during power waits. Return nonzero to poll
       again, or zero to yield the suspended register state to the caller.
       On interrupt wake, waiting=0 lets the board release its input pins. */
    int (*power_wait)(void*, int waiting);
    int waiting_for_power;
    /* A retired TLBSYNC yields before the next instruction if the board
       holds TLBISYNC. Negate the input and resume with the saved registers. */
    int waiting_for_tlb;
    /* Optional clocked board: advance clocks until at least interval core
       cycles have elapsed since the previous instruction group. */
    void (*wait_forward)(void*, uint64_t since, unsigned interval);
} ROMlib_ppc_environment;

enum { ROMLIB_PPC_EXEC_JIT = 1 };

int ROMlib_ppc_jit_available(void);
ROMlib_ppc_translation_cache* ROMlib_ppc_translation_cache_create(void);
void ROMlib_ppc_translation_cache_destroy(ROMlib_ppc_translation_cache*);
/* Translate supported runs before execution and return the number published.
   At capacity, a rewritten start can reuse its slot. A publication failure
   preserves the old translation. Invalid extents, busy caches and unsupported
   hosts return zero. */
unsigned ROMlib_ppc_translate_range(ROMlib_ppc_translation_cache*, const void*, uint32_t, uint32_t);
unsigned ROMlib_pef_translate(ROMlib_pef_image*, ROMlib_ppc_translation_cache*);

/* Distinct singleton 750 kernels retain checked architectural operand helpers.
   The effective address and the opcode from the live fetch select a record.
   Separate views at the same effective address share the cache's slot limit. */
enum { ROMLIB_PPC_TRANSLATE_750 = 1 };

unsigned ROMlib_ppc_translate_range_mode(ROMlib_ppc_translation_cache*, const void*, uint32_t, uint32_t, unsigned);
unsigned ROMlib_pef_translate_mode(ROMlib_pef_image*, ROMlib_ppc_translation_cache*, unsigned);

typedef struct ROMlib_ppc_published_word {
    uint32_t address, opcode;
} ROMlib_ppc_published_word;

enum { ROMLIB_PPC_PUBLISHED_750_MAX_WORDS = 4096 };

/* Eagerly prepare a bounded list of published words in the owning host
   service. Addresses and opcodes are host-endian. The checked guest fetch
   still selects the live translation. Returns newly compiled records. */
unsigned ROMlib_ppc_translate_published_750(ROMlib_ppc_translation_cache*, const ROMlib_ppc_published_word*, size_t);
/* Prepare linked PowerPC images together, sharing the cache budget. */
unsigned ROMlib_pef_translate_group(ROMlib_pef_image* const*, size_t, ROMlib_ppc_translation_cache*, unsigned);
/* Extend a cache key with the group's linked code ranges, entry points and execution mode. */
int ROMlib_pef_translation_fingerprint(ROMlib_pef_image* const*, size_t, unsigned, uint64_t*);
/* Native artifacts are specific to a build and host ABI. Load verifies the
     bounded format and checks the code and metadata against fresh emission.
     Failure leaves the current cache intact. Save replaces atomically and
     permits a snapshot from the owning suspended host-service scope. */
unsigned ROMlib_ppc_translation_cache_load(ROMlib_ppc_translation_cache*, const char*);
int ROMlib_ppc_translation_cache_save(ROMlib_ppc_translation_cache*, const char*);
typedef void* (*ROMlib_pef_allocate_fn)(void*, uint32_t, uint32_t, uint32_t*);
typedef void (*ROMlib_pef_release_fn)(void*, void*);
typedef int (*ROMlib_pef_resolve_fn)(void*, const char*, const char*, unsigned, int, uint32_t*);

typedef struct {
    void* opaque;
    ROMlib_pef_allocate_fn allocate;
    ROMlib_pef_release_fn release;
    ROMlib_pef_resolve_fn resolve;
    char error[256];
} ROMlib_pef_environment;

enum { ROMLIB_PEF_POWERPC = 0x70777063, ROMLIB_PEF_M68K = 0x6d36386b };

/* Explicit architecture selection prevents accidentally linking fragments
     from another ISA. The original entry points remain PowerPC-only. */
ROMlib_pef_image* ROMlib_pef_prepare_arch(const void*, size_t, ROMlib_pef_environment*, uint32_t);
uint32_t ROMlib_pef_architecture(ROMlib_pef_image*);
ROMlib_pef_image* ROMlib_pef_load(const void*, size_t, ROMlib_pef_environment*);
ROMlib_pef_image* ROMlib_pef_prepare(const void*, size_t, ROMlib_pef_environment*);
unsigned ROMlib_pef_library_count(ROMlib_pef_image*);
unsigned ROMlib_pef_import_count(ROMlib_pef_image*);
const char* ROMlib_pef_library_info(ROMlib_pef_image*, unsigned, uint32_t*);
int ROMlib_pef_is_code(ROMlib_pef_image*, uint32_t);
int ROMlib_pef_contains(ROMlib_pef_image*, uint32_t, size_t);
int ROMlib_pef_address_section(ROMlib_pef_image*, uint32_t, size_t, uint32_t*, uint32_t*, unsigned*);
int ROMlib_pef_find_section(ROMlib_pef_image*, uint32_t, size_t, uint32_t*, uint32_t*, unsigned*, unsigned*);
int ROMlib_pef_link(ROMlib_pef_image*);
unsigned ROMlib_pef_share_code(ROMlib_pef_image*, ROMlib_pef_image*);
int ROMlib_pef_find_symbol(ROMlib_pef_image*, const char*, uint32_t*, unsigned*);
unsigned ROMlib_pef_export_count(ROMlib_pef_image*);
const char* ROMlib_pef_export_info(ROMlib_pef_image*, unsigned, uint32_t*, unsigned*);
const char* ROMlib_pef_error(ROMlib_pef_image*);
void ROMlib_pef_unload(ROMlib_pef_image*);
uint32_t ROMlib_pef_main(ROMlib_pef_image*);
uint32_t ROMlib_pef_init(ROMlib_pef_image*);
uint32_t ROMlib_pef_term(ROMlib_pef_image*);
/* Opcode 6, service 0, returns from the current emulated invocation.
   Nonzero services dispatch through the supplied callback and return via LR. */
int ROMlib_ppc_format(ROMlib_ppc_registers*, ROMlib_ppc_memory_fn, void*, int, char*, size_t);
int ROMlib_ppc_execute(ROMlib_ppc_registers*, ROMlib_ppc_environment*);
/* Suspend the host stack on the same OS thread, preserving the invocation's
   interpreter/JIT, reservation and floating-environment bookkeeping. */
void ROMlib_ppc_switch_host(void (*switch_host)(void*), void* target);
/* Same monotonic 25 MHz clock used by the emulated time-base registers. */
uint64_t ROMlib_ppc_clock_ticks(void);
#ifdef __cplusplus
}
#endif
#endif
