/*+===================================================================
  File:      carbonlib.h

  Summary:   Native Carbon export registry and checked ROMAW/PPC backend interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in packages/carbonlib/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef ROMAW_CARBONLIB_H
#define ROMAW_CARBONLIB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Signatures use ROMAW's three-bit Toolbox marshalling encoding. Pointers
   describe translated host addresses, never raw PPC or 68K guest addresses.
   Result code 6 extends this encoding for a PPC double result in f1, with
   ordinary integer/pointer arguments. It is not a 68K Toolbox signature;
   68K imports of these functions must retain their real-library fallback. */
/* Full-frame entries use int entry(ROMlib_carbon_ppc_frame*), returning success.
   They bypass the three-bit word marshaller and always retain 68K fallback.
   No ordinary signature uses bit 63 (the word marshaller accepts 11 arguments). */
#define ROMLIB_CARBON_PPC_FRAME (UINT64_C(1) << 63)
/* The scheduler consumes the CPU registers and may switch guest threads. */
#define ROMLIB_CARBON_PPC_THREAD (UINT64_C(1) << 62)
#define ROMLIB_CARBON_DOUBLE_RESULT 6
#define ROMLIB_CARBON_PPC_ONLY 1
#define ROMLIB_CARBON_COMPANION_ONLY 2
#define ROMLIB_CARBON_EXTENSION 4
#define ROMLIB_CARBON_PRELOAD_REFERENCE 8

typedef struct ROMlib_carbon_export {
    const char* name;
    void* function;
    uint64_t signature;
    /* Code offset in CarbonLib, or imported-symbol index when reference_library != NULL.
       COMPANION_ONLY entries instead use the code offset in reference_library.
       EXTENSION entries have no reference offset and store 0xffffffff. */
    uint32_t reference_offset;
    const char* reference_library;
    uint32_t flags;
} ROMlib_carbon_export;

const ROMlib_carbon_export* ROMlib_carbon_find_export(const char* name, unsigned symbol_class);
const ROMlib_carbon_export* ROMlib_carbon_find_library_export(const char* library, const char* name,
                                                              unsigned symbol_class);
const ROMlib_carbon_export* ROMlib_carbon_exports(size_t* count);
int32_t ROMlib_carbon_transition_window(uint32_t window, uint32_t effect, uint32_t action, const void* origin);

typedef enum ROMlib_carbon_data_kind {
    ROMLIB_CARBON_DATA_WORDS,
    ROMLIB_CARBON_DATA_TYPE_CALLBACKS,
    ROMLIB_CARBON_DATA_TYPE_HASH_CALLBACKS,
    ROMLIB_CARBON_DATA_STRING_HASH_CALLBACKS,
    ROMLIB_CARBON_DATA_SYSTEM_ALLOCATOR,
    ROMLIB_CARBON_DATA_CF_SHARED,
} ROMlib_carbon_data_kind;

/* Immutable re-exports have per-launch guest addresses, independent of host pointers. */
typedef struct ROMlib_carbon_data_export {
    const char* name;
    const char* reference_library;
    uint32_t reference_index;
    uint32_t words[2];
    ROMlib_carbon_data_kind kind;
    uint32_t reference_data_offset;
} ROMlib_carbon_data_export;

const ROMlib_carbon_data_export* ROMlib_carbon_data_exports(size_t* count);

/* In guest record order, with the string-copy retain alternative last. */
enum {
    ROMLIB_CARBON_CF_RETAIN,
    ROMLIB_CARBON_CF_RELEASE,
    ROMLIB_CARBON_CF_DESCRIBE,
    ROMLIB_CARBON_CF_EQUAL,
    ROMLIB_CARBON_CF_HASH,
    ROMLIB_CARBON_CF_COPY_STRING,
    ROMLIB_CARBON_CF_CALLBACK_COUNT,
};

const ROMlib_carbon_export* ROMlib_carbon_cf_collection_callbacks(void);
/* Match callback identities only after verifying the CoreFoundation 1.4 layout. */
uint32_t ROMlib_carbon_canonical_cf_callbacks(uint32_t record, const void* reference_toc);

enum { ROMLIB_CARBON_CF_SYSTEM_WORKER_COUNT = 4 };

const ROMlib_carbon_export* ROMlib_carbon_cf_system_allocator_workers(void);
int ROMlib_carbon_is_cf_system_allocator_worker(unsigned index, uint32_t address);

/* Host-endian register words and raw IEEE bits, never translated host pointers.
   stack is the suspended caller's immutable r1. Word indices include the ABI
   positions occupied by floating/aggregate arguments; this is not a type parser.
   Registers r3..r10 and f1..f13 also carry results/volatile outputs. Explicit
   packing keeps this interface identical with and without Macintosh headers. */
#pragma pack(push, 8)

/* Transfer an even-sized Macintosh record as big-endian halfwords. */
typedef int (*ROMlib_carbon_guest_words_fn)(void* opaque, uint32_t address, void* record, size_t size, int writing);

typedef struct ROMlib_carbon_ppc_frame {
    uint32_t words[8];
    uint64_t floating[13];
    uint32_t stack;
    uint32_t fpscr;
    ROMlib_carbon_guest_words_fn guest_words;
    void* guest_opaque;
} ROMlib_carbon_ppc_frame;

int ROMlib_carbon_cf_preference_keys(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_cf_url_keys(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_cf_bundle_keys(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_cf_plugin_keys(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_cf_allocator_constants(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_cf_boolean_constants(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_cf_number_constants(ROMlib_carbon_ppc_frame* call);

#pragma pack(pop)

typedef int (*ROMlib_carbon_frame_entry)(ROMlib_carbon_ppc_frame* call_frame);

/* Dispatch without interpreting any argument types. Failed entries leave the
   caller's register outputs untouched; diagnostic ownership stays in ROMAW.
   floating points to 13 contiguous native double register slots. */
int ROMlib_carbon_dispatch_frame(ROMlib_carbon_frame_entry entry, uint32_t words[8], void* floating, uint32_t stack,
                                 uint32_t* fpscr);
/* Audited frame entries can transfer records through the caller's architectural bus. */
int ROMlib_carbon_dispatch_mapped_frame(ROMlib_carbon_frame_entry entry, uint32_t words[8], void* floating,
                                        uint32_t stack, uint32_t* fpscr, ROMlib_carbon_guest_words_fn guest_words,
                                        void* guest_opaque);

/* Read one argument word: r3..r10 first, then the caller's parameter area at
   r1+24+index*4. Only the requested word is accessed, with checked arithmetic. */
int ROMlib_carbon_frame_word(const ROMlib_carbon_ppc_frame* call_frame, unsigned index, uint32_t* result);

/* Invoke a saved PPC descriptor with the same register/stack argument frame.
   This synchronous operation requires a suspended native PPC caller. It does
   not fabricate a varargs home area or truncate arguments at eleven words.
   Failed calls preserve frame contents and the first active diagnostic. */
int ROMlib_carbon_invoke_frame(void* procedure, ROMlib_carbon_ppc_frame* call_frame);

/* Reuse ROMAW's typed native service ABI for a CarbonLib re-export. */
int ROMlib_carbon_call_romaw_service(const char* name, ROMlib_carbon_ppc_frame* call_frame);

/* Invoke a low-memory Toolbox vector using ordinary or selector-dispatched Pascal ProcInfo.
   The selector is separate from the function's PPC arguments. The live vector may select 68K code or a PPC callback. */
int ROMlib_carbon_call_toolbox_vector(uint32_t address, uint32_t info, uint32_t selector,
                                      const ROMlib_carbon_ppc_frame* call_frame, uint32_t* result);
int ROMlib_carbon_call_original_toolbox_trap(uint16_t trap_index, uint32_t info, uint32_t selector,
                                             const ROMlib_carbon_ppc_frame* call_frame, uint32_t* result);

/* Allocate or release a Mixed Mode routine descriptor owned by the active
   launch. Argument values and the returned address are guest words. */
uint32_t ROMlib_carbon_new_ppc_descriptor(uint32_t procedure, uint32_t info);
void ROMlib_carbon_dispose_descriptor(uint32_t address);

/* Invoke a Universal Procedure Pointer with up to eleven Pascal arguments. */
int ROMlib_carbon_invoke_pascal(uint32_t procedure, uint32_t info, const ROMlib_carbon_ppc_frame* call_frame,
                                uint32_t* result);

/* Invoke a C-stack, register-based, or special TextEdit Universal Procedure Pointer. */
int ROMlib_carbon_invoke_mixed_upp(uint32_t procedure, uint32_t info, const ROMlib_carbon_ppc_frame* call_frame,
                                   uint32_t* result);

/* Call a live register-based OS or Toolbox vector. A nonzero trap is
   inserted at the specified zero-based Mixed Mode argument slot. */
int ROMlib_carbon_call_register_vector(uint32_t address, uint32_t info, uint32_t trap, unsigned trap_slot,
                                       const ROMlib_carbon_ppc_frame* call_frame, uint32_t* result);

/* Call a borrowed, raw PowerPC transition vector, preserving the PPC frame. */
int ROMlib_carbon_invoke_transition_vector(uint32_t vector, ROMlib_carbon_ppc_frame* call_frame);

/* Date getter frame transport also preserves foreign-profile FP results. */
int ROMlib_carbon_cf_date_frame(ROMlib_carbon_ppc_frame* call_frame);

/* StdCLib sorting passes guest element addresses to a two-argument comparator. */
int ROMlib_carbon_stdlib_qsort(ROMlib_carbon_ppc_frame* call_frame);

void ROMlib_carbon_sound_input_poll(void);
void ROMlib_carbon_sound_input_reset(int testing);
void ROMlib_carbon_url_access_reset(void);
void ROMlib_carbon_navigation_reset(void);
void ROMlib_carbon_icon_services_reset(void);
void ROMlib_carbon_drag_reset(void);
void ROMlib_carbon_drag_forget_window(uint32_t window);

/* Supplied by the ROMAW backend. Arguments/results are opaque guest words;
   callback failures must retain the active launch's diagnostic. */
int ROMlib_carbon_invoke_callback(void* procedure, const uint32_t* arguments, unsigned count, uint32_t* result);

/* Validate and translate an entire raw guest span. Invalid memory retains the
   active launch diagnostic; subsequent accesses on that failed launch stop. */
void* ROMlib_carbon_guest_memory(uint32_t address, size_t length, int write_access);
typedef struct ROMlib_carbon_theme_state ROMlib_carbon_theme_state;
ROMlib_carbon_theme_state** ROMlib_carbon_theme_state_slot(void);
typedef struct ROMlib_carbon_debug_component ROMlib_carbon_debug_component;
ROMlib_carbon_debug_component** ROMlib_carbon_debug_component_slot(void);
uint32_t* ROMlib_carbon_debug_handler_slot(void);
void ROMlib_carbon_free_debug_components(ROMlib_carbon_debug_component** components);

void ROMlib_carbon_free_theme_states(ROMlib_carbon_theme_state** states);
enum ROMlib_carbon_theme_panel {
    appearance_window_header = 5,
    appearance_list_view_header = 6,
    appearance_placard = 7,
};
int32_t ROMlib_carbon_draw_theme_panel(const void* bounds, uint32_t state, enum ROMlib_carbon_theme_panel panel);
int32_t ROMlib_carbon_set_theme_window_background(void* window, int16_t brush, uint8_t update);
void* ROMlib_carbon_timezone_storage(uint32_t size, uint32_t* address);
int ROMlib_carbon_native_ppc_callback(const char* name, int (*function)(ROMlib_carbon_ppc_frame*), uint32_t* vector);

/* Validate a span for a Toolbox service that returns an error to the guest. */
void* ROMlib_carbon_try_guest_memory(uint32_t address, size_t length, int write_access);

/* Preserve an existing failure; otherwise fail the active native PPC operation. */
int ROMlib_carbon_report_failure(const char* message);

typedef struct ROMlib_carbon_reference {
    void* function;
    const void* toc;
    uint32_t code_offset, code_size, toc_offset, toc_size;
} ROMlib_carbon_reference;

/* Inspect an already linked real PEF image without changing initialization
   order or fabricating module globals. Failure sets the active diagnostic. */
int ROMlib_carbon_reference_function(const char* library, const char* name, ROMlib_carbon_reference* reference);

/* Probe an optional companion which the guest may load on its first call. */
int ROMlib_carbon_probe_reference(const char* library, const char* name, ROMlib_carbon_reference* reference);
/* Compare a live Toolbox vector with a code offset in an already linked PEF. */
int ROMlib_carbon_toolbox_vector_matches(uint32_t address, const ROMlib_carbon_reference* reference,
                                       uint32_t code_offset);
/* Read the shared Carbon ownership count or release one owner. An output of
   zero after release delegates final disposal to the caller. */
int ROMlib_carbon_window_owners(uint32_t window, int change, uint32_t* owners, uint32_t* status);
/* Release metadata held by an initialized WindowsLib after a native close. */
void ROMlib_carbon_window_closed(uint32_t window);
int ROMlib_carbon_windows_set_bounds(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_windows_move_structure(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_windows_get_content_color(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_windows_set_content_color(ROMlib_carbon_ppc_frame* call);
/* Load an optional original fragment through CFM and retain it for the launch. */
int ROMlib_carbon_load_reference(const char* library, const char* name, ROMlib_carbon_reference* reference);

/* Execute a validated CFM request against the active launch's loader. */
int ROMlib_carbon_call_cfm(const char* name, ROMlib_carbon_ppc_frame* call);

/* Resolve a symbol on the current launch's real CFM connection. Name is a
   host-accessible Pascal string; address/class outputs are native words. */
int32_t ROMlib_carbon_find_symbol(uint32_t connection, const unsigned char* name, uint32_t* address,
                                  unsigned* symbol_class);

#ifdef __cplusplus
}
#endif

#endif
