/*+===================================================================
  File:      bridge.c

  Summary:   PowerPC application execution and native Classic services for the Windows SDL frontend.

  Classes:   None.

  Functions: ROMlib_carbon_canonical_cf_callbacks, ROMlib_carbon_call_cfm, ROMlib_carbon_call_romaw_service,
             ROMlib_carbon_call_original_toolbox_trap, ROMlib_carbon_call_register_vector, ROMlib_carbon_call_toolbox_vector,
             ROMlib_carbon_toolbox_vector_matches,
             ROMlib_carbon_dispose_descriptor, ROMlib_carbon_find_symbol, ROMlib_carbon_guest_memory, ROMlib_carbon_invoke_callback,
             ROMlib_carbon_native_ppc_callback, ROMlib_carbon_timezone_storage,
             ROMlib_carbon_invoke_frame, ROMlib_carbon_invoke_mixed_upp, ROMlib_carbon_invoke_pascal,
             ROMlib_carbon_invoke_transition_vector,
             ROMlib_carbon_is_cf_system_allocator_worker, ROMlib_carbon_load_reference, ROMlib_carbon_new_ppc_descriptor, ROMlib_carbon_probe_reference, ROMlib_carbon_reference_function,
             ROMlib_carbon_report_failure, ROMlib_carbon_try_guest_memory, ROMlib_cfm68k_shutdown,
             ROMlib_launch_cfm68k_fragment, ROMlib_launch_cfm68k_resource, ROMlib_launch_ppc_fragment,
             ROMlib_ppc_callback, ROMlib_ppc_callback_failed, ROMlib_ppc_code_fragment, ROMlib_ppc_completion,
             ROMlib_ppc_begin_launch, ROMlib_ppc_request_system,
             ROMlib_ppc_component_entry, ROMlib_ppc_display_dispatch, ROMlib_ppc_event_loop_entered, ROMlib_ppc_gestalt,
             ROMlib_ppc_mixed_entry,
             ROMlib_ppc_mixed_entry_error, ROMlib_ppc_set_aot, ROMlib_ppc_set_jit, ROMlib_ppc_set_system

  Origin:    Existing repository source in synppc/src/executor; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* Interpreted PowerPC application support for the Windows SDL frontend. */
#include "rsys/common.h"
#if defined(CYGWIN32) && defined(SDL) && defined(SYN68K)
#include "rsys/everything.h"
#include "Gestalt.h"
#include "BinaryDecimal.h"
#include "StdFilePkg.h"
#include "OSUtil.h"
#include "rsys/mman.h"
#include "synppc/ppc.h"
#include "synppc/bus750.h"
#include "rsys/file.h"
#include "rsys/hfs.h"
#include "winfs.h"
#include "rsys/osutil.h"
#include "rsys/cquick.h"
#include "rsys/pattern_origin.h"
#include "rsys/launch.h"
#include "rsys/romaw.h"
#include "rsys/ecf.h"
#include "rsys/version.h"
#include "rsys/segment.h"
#include "rsys/ctopflags.h"
#include "rsys/cfm.h"
#include "AliasMgr.h"
#include "Iconutil.h"
#include "Finder.h"
#include "NotifyMgr.h"
#include "ShutDown.h"
#include "CommTool.h"
#include <limits.h>
#include <float.h>
#include <math.h>
#include <unistd.h>
#include "rsys/ptocflags.h"
#include "rsys/pstuff.h"
#include "rsys/script_globals.h"
#include "rsys/mixed_mode.h"
#include "rsys/blockinterrupts.h"

#include "rsys/appearance.h"
#include "rsys/emustubs.h"
#include "rsys/wind.h"
#include "rsys/component.h"
#include "rsys/systemnotify.h"
#include "rsys/systemwindow.h"
#include "rsys/layers.h"
#include "rsys/process.h"
#include "rsys/prefs.h"
#include "rsys/resource.h"
#include "rsys/system_resources.h"
#include "rsys/speech.h"
#include "rsys/menu.h"
#include "HelpMgr.h"
#include "rsys/ctl.h"
#include "rsys/user_profile.h"
#include "carbonlib.h"
#include "carbon_threads.h"
#include "memory-manager.h"
#include "sound/sdl/sdl-sound.h"
_Static_assert(_Alignof(ROMlib_ppc_system750) == _Alignof(uint64_t), "PPC supervisor state requires native alignment");
_Static_assert(offsetof(ROMlib_ppc_system750, time_base) % _Alignof(uint64_t) == 0,
               "Macintosh packing must not alter PPC supervisor state");
_Static_assert(sizeof(ROMlib_ppc_cache750_line) == 40, "PPC cache lines require native four-byte alignment");
_Static_assert(_Alignof(ROMlib_ppc_cache750) == _Alignof(void*), "PPC cache transactions require native pointers");
_Static_assert(offsetof(ROMlib_ppc_cache750, transactions) == 41088, "PPC cache bank ABI must match C++ runtime");
extern int ROMlib_win_accessible_span(const void*, uint32, int);
extern void ROMlib_win_ppc_error(const char*);

static boolean_t ppc_jit_enabled = FALSE;
static boolean_t ppc_aot_enabled = FALSE;
static boolean_t ppc_system_enabled = FALSE;

PUBLIC void ROMlib_ppc_set_system(boolean_t enabled)
{
    ppc_system_enabled = enabled;
}

PUBLIC void ROMlib_ppc_set_aot(boolean_t enabled)
{
    ppc_aot_enabled = enabled;
}

PUBLIC void ROMlib_ppc_set_jit(boolean_t enabled)
{
    ppc_jit_enabled = enabled;
}

typedef struct ppc_allocation {
    struct ppc_allocation* next;
    Ptr base, aligned;
    uint32 size;
    THz zone;
    int libc;
} ppc_allocation;

typedef struct ppc_callback_stack {
    struct ppc_callback_stack* next;
    unsigned char* memory;
    uint32 address;
    int busy;
} ppc_callback_stack;

typedef struct {
    char* name;
    void* function;
    uint64 magic;
    uint32 stub, vector;
} ppc_import;
struct ppc_fragment;

typedef struct ppc_dependency {
    struct ppc_dependency* next;
    struct ppc_fragment* target;
} ppc_dependency;

typedef struct ppc_fragment {
    struct ppc_fragment *next, *owner;
    FSSpec spec;
    char name[64];
    uint32 offset, length, memory_address;
    ROMlib_pef_image* image;
    unsigned linking, initialized, visit, low, on_stack, native, startup_done;
    unsigned startup_inspected, has_startup_init, private_copy, closed;
    unsigned notified;
    ppc_dependency* dependencies;
    uint32 connection, references;
    uint32 initialization_closure;
} ppc_fragment;

typedef struct ppc_closure {
    struct ppc_closure* next;
    ppc_fragment* root;
    ppc_fragment** members;
    unsigned count, state, launch;
    uint32 id;
} ppc_closure;

typedef struct ppc_notifier {
    struct ppc_notifier* next;
    ppc_fragment* owner;
    uint32 address, vector, serial;
    int active;
} ppc_notifier;

typedef struct ppc_folder_notification {
    struct ppc_folder_notification* next;
    ROMlib_pef_image* owner;
    uint32 procedure, refcon;
    boolean_t removed;
} ppc_folder_notification;

typedef struct ppc_display_notification {
    struct ppc_display_notification* next;
    ROMlib_pef_image* owner;
    ProcessSerialNumber process;
    uint32 procedure, refcon;
    boolean_t extended;
} ppc_display_notification;

typedef struct ppc_shutdown_callback {
    struct ppc_shutdown_callback* next;
    uint32 procedure, code;
    uint16 flags;
} ppc_shutdown_callback;

/* Appearance drawing states are opaque guest tokens with per-launch ownership.
   They save color/pen state, not a port, clip region or font selection. */
typedef struct ppc_theme_state {
    struct ppc_theme_state* next;
    uint32 token;
    PenState pen;
    RGBColor foreground, background;
    LONGINT foreground_index, background_index;
    Pattern background_pattern;
    PixPatHandle background_pixpat;
    Point origin;
    INTEGER text_mode;
    boolean_t color, background_valid;
} ppc_theme_state;

typedef struct {
    uint32 architecture;
    ROMlib_pef_image* application;
    ROMlib_ppc_translation_cache* translations;
    ROMlib_ppc_system750 system;
    ROMlib_ppc_l2cache750* system_l2;
    uint64 system_tick;
    int power_int_asserted;
    int system_initialized;
    char translation_path[4096];
    ppc_allocation* allocations;
    ppc_callback_stack* callback_stacks;
    ppc_shutdown_callback* shutdown_callbacks;
    int shutting_down;
    unsigned callback_depth, allocating_callback_stack;
    const ROMlib_ppc_registers* native_caller;
    ppc_import* imports;
    unsigned n_imports;
    ppc_fragment *fragments, *requester;
    ppc_dependency* application_dependencies;
    ppc_closure *closures, *launch_closure, *initializing_closure;
    int linking_application;
    Handle* segments;
    unsigned n_segments;
    int segmented;
    FSSpec startup_spec;
    ppc_fragment* init_order[512];
    unsigned n_initialized, graph_depth, graph_index;
    uint32 saved_desktop_device_loop_return;
    ppc_fragment* graph_stack[512];
    uint32 loader_vectors;
    uint32 closure, ctype_data, iob_data, carbon_data, flt_max, dbl_max;
    uint32 som_allocator_data;
    uint32 carbon_cf_callback_vectors[ROMLIB_CARBON_CF_CALLBACK_COUNT];
    uint32 carbon_system_allocator, carbon_system_workers[ROMLIB_CARBON_CF_SYSTEM_WORKER_COUNT];
    uint32 carbon_timezone_storage;
    boolean_t carbon_cf_shared_data;
    FILE* streams[40];
    uint32 os_vectors[256], tool_vectors[1024];
    uint32 halt;
    int exited, retry_system, application_started, application_running;
    uint32 required_system;
    ppc_notifier* notifiers;
    ppc_folder_notification* folder_notifications;
    unsigned folder_notification_depth;
    ppc_theme_state* theme_states;
    ppc_fragment* notifying_owner;
    uint32 notifier_serial;
    int floating_windows_inactive;
    boolean_t display_initialized;
    ppc_display_notification* display_notifications;

    struct {
        GDHandle device;
        uint32 id;
    } displays[32];

    unsigned display_count;
    uint32 next_display_id;

    struct {
        uint32 procedure;
        ROMlib_pef_image* owner;
    } desktop_draw, display_patches[4];

    uint32 desktop_draw_refcon;
    ROMlib_ppc_threads* threads;
    ROMlib_native_memory* memory_manager;
    Point iconographer_mouse;
    boolean_t iconographer_mouse_valid;
    char error[512];
} ppc_context;

PRIVATE ppc_context* ppc_active_context;
PRIVATE ppc_context* cfm68k_classic_context;
static boolean_t classic_application_running;

PRIVATE uint32 cfm_architecture(ppc_context* ctx)
{
    return ctx->architecture ? ctx->architecture : kPowerPCArch;
}

PRIVATE unsigned cfm_native_isa(ppc_context* ctx)
{
    return cfm_architecture(ctx) == kMotorola68KArch ? kM68kISA | kCFM68kRTA : kPowerPCISA;
}

PUBLIC boolean_t ROMlib_ppc_callback_failed(void)
{
    return ppc_active_context && ppc_active_context->error[0];
}

PUBLIC void ROMlib_ppc_event_loop_entered(void)
{
    if (ppc_active_context && ppc_active_context->application_started)
        ppc_active_context->application_running = 1;
    else if (!ppc_active_context)
        classic_application_running = TRUE;
}

void ROMlib_ppc_begin_launch(void)
{
    classic_application_running = FALSE;
}

int ROMlib_ppc_request_system(uint32 version)
{
    ppc_context* ctx = ppc_active_context;
    if (version <= system_version || version < 0x600 || version > 0x922)
        return 0;
    if (ctx ? ctx->application_running : classic_application_running) {
        fprintf(stderr, "trace: system-version request left to running application after event processing\n");
        return 0;
    }
    if (ctx) {
        ctx->required_system = version;
        snprintf(ctx->error, sizeof ctx->error, "Application startup requested System %x", (unsigned)version);
        return 1;
    }
    FSSpec application;
    if (C_FSMakeFSSpec(ROMlib_exevrefnum, 0, ROMlib_exefname, &application) == noErr &&
        ROMlib_ecf_require_system(&application, version) > 0) {
        ROMlib_exit = 1;
        C_ExitToShell();
    }
    return 0;
}

PRIVATE void* ppc_memory(void* opaque, uint32 address, size_t length, int write_p)
{
    ppc_context* ctx = opaque;
    uint64 end = (uint64)address + length;
    /* Classic clients also read the Toolbox trap table through low memory.
     ROMAW keeps its 68K vectors in a separate host array. */
    if (address >= 0x400 && end <= 0x800)
        return (unsigned char*)ctx->os_vectors + address - 0x400;
    if (address >= 0xe00 && end <= 0x1e00)
        return (unsigned char*)ctx->tool_vectors + address - 0xe00;
    uint32 relative = address - syn68k_host_alias_base;
    unsigned slot = relative >> 17;
    void* p;
    if (end > UINT64_C(0x100000000))
        return NULL;
    if (slot < syn68k_host_alias_count) {
        if (length > 0x20000 - (relative & 0x1ffff))
            return NULL;
        p = SYN68K_TO_US(address);
        return ROMlib_win_accessible_span(p, length, write_p) ? p : NULL;
    }
    if (end > ROMlib_guest_memory_size)
        return NULL;
    return SYN68K_TO_US_RAW(address);
}

PRIVATE void* ppc_allocate(void* opaque, uint32 size, uint32 alignment, uint32* address)
{
    ppc_context* ctx = opaque;
    ppc_allocation* a;
    Ptr p;
    uint32 guest;
    if (!alignment || (alignment & (alignment - 1)) || size > INT32_MAX - alignment)
        return NULL;
    a = malloc(sizeof *a);
    if (!a)
        return NULL;
    p = NewPtr(size + alignment - 1);
    if (!p) {
        if (!ctx->application_started)
            ctx->retry_system = 1;
        free(a);
        return NULL;
    }
    guest = (US_TO_SYN68K(p) + alignment - 1) & ~(alignment - 1);
    a->base = p;
    a->size = size;
    a->zone = GetZone();
    a->libc = 0;
    a->aligned = (Ptr)SYN68K_TO_US(guest);
    a->next = ctx->allocations;
    ctx->allocations = a;
    *address = guest;
    return a->aligned;
}

static int ppc_allocation_has_address(const ppc_allocation* allocation, uintptr_t address)
{
    const uintptr_t base = (uintptr_t)allocation->aligned;
    return address >= base && address - base < allocation->size;
}

static int ppc_gestalt_handler_in_allocation(ProcPtr procedure, void* opaque)
{
    return ppc_allocation_has_address(opaque, (uintptr_t)procedure);
}

PRIVATE void ppc_release(void* opaque, void* p)
{
    ppc_context* ctx = opaque;
    ppc_allocation** link;
    for (link = &ctx->allocations; *link; link = &(*link)->next)
        if ((*link)->aligned == p) {
            ppc_allocation* a = *link;
            *link = a->next;
            ROMlib_remove_gestalt_handlers(ppc_gestalt_handler_in_allocation, a);
            for (ppc_shutdown_callback** callback = &ctx->shutdown_callbacks; *callback;) {
                ppc_shutdown_callback* entry = *callback;
                if (ppc_allocation_has_address(a, (uintptr_t)SYN68K_TO_US(entry->procedure)) ||
                    ppc_allocation_has_address(a, (uintptr_t)SYN68K_TO_US(entry->code))) {
                    *callback = entry->next;
                    free(entry);
                }
                else
                    callback = &entry->next;
            }
            DisposPtr(a->base);
            free(a);
            return;
        }
}

static void* ppc_mm_allocate(void* opaque, uint32 size, unsigned kind, uint32* address)
{
    THz saved = GetZone();
    /* ROMAW's temporary-memory partition is the System zone, as in
       TempNewHandle. Pointer blocks remain fixed until explicitly released. */
    SetZone(kind == 1 ? MR(ApplZone) : MR(SysZone));
    void* memory = ppc_allocate(opaque, size, 16, address);
    SetZone(saved);
    return memory;
}

static void ppc_mm_release(void* opaque, uint32 address)
{
    ppc_context* ctx = opaque;
    for (ppc_allocation* allocation = ctx->allocations; allocation; allocation = allocation->next)
        if (US_TO_SYN68K(allocation->aligned) == address) {
            ppc_release(ctx, allocation->aligned);
            return;
        }
}

static void ppc_mm_space(void* opaque, unsigned kind, uint32* total, uint32* largest)
{
    (void)opaque;
    THz saved = GetZone();
    SetZone(kind == 1 ? MR(ApplZone) : MR(SysZone));
    Size growth;
    *largest = MaxMem(&growth);
    *total = FreeMem();
    SetZone(saved);
}

static ROMlib_native_memory_backend ppc_mm_backend(ppc_context* ctx)
{
    ROMlib_native_memory_backend backend = {ctx, ppc_mm_allocate, ppc_mm_release, ppc_memory, ppc_mm_space};
    return backend;
}

PRIVATE int ppc_resolve(void*, const char*, const char*, unsigned, int, uint32*);
PRIVATE int ppc_cfm_service(ppc_context*, const char*, ROMlib_ppc_registers*);
static int ppc_fragment_notify(ppc_context*, ppc_fragment*, uint32, uint32, unsigned);
PRIVATE int ppc_mixed_service(ppc_context*, const char*, ROMlib_ppc_registers*, const ROMlib_carbon_ppc_frame*);
PRIVATE int ppc_service(void*, uint32, ROMlib_ppc_registers*);
static int ppc_initialize_desktop_appearance(ppc_context*);
static int ppc_shutdown_service(ppc_context*, const char*, ROMlib_ppc_registers*);

PRIVATE ROMlib_pef_image* cfm68k_prepare_resource(ppc_context* ctx, FSSpecPtr spec)
{
    INTEGER saved = CurResFile(), ref = C_FSpOpenResFile(spec, fsRdPerm);
    ROMlib_pef_image* image = NULL;
    if (ref != -1) {
        Handle header = Get1Resource(TICK("rseg"), 0);
        if (header && GetHandleSize(header) >= 56) {
            unsigned char* p = (unsigned char*)STARH(header);
            uint32* words = (uint32*)p;
            if (CL(words[0]) == TICK("Joy!") && CL(words[1]) == TICK("rseg") && CL(words[2]) == kMotorola68KArch &&
                CL(words[3]) == 1) {
                INTEGER resource = CW(*(INTEGER*)(p + 48));
                Handle data = Get1Resource(TICK("rseg"), resource);
                if (data) {
                    ROMlib_pef_environment env = {0};
                    env.opaque = ctx;
                    env.allocate = ppc_allocate;
                    env.release = ppc_release;
                    env.resolve = ppc_resolve;
                    HLock(data);
                    image = ROMlib_pef_prepare_arch(STARH(data), GetHandleSize(data), &env, kMotorola68KArch);
                    HUnlock(data);
                    if (!image)
                        snprintf(ctx->error, sizeof ctx->error, "%s", env.error);
                }
            }
        }
        CloseResFile(ref);
    }
    UseResFile(saved);
    if (!image && !ctx->error[0])
        snprintf(ctx->error, sizeof ctx->error, "Missing or malformed CFM-68K rseg resources");
    return image;
}

/* CFM-68K fixed arguments use C order, four-byte slots and callee cleanup.
   Toolbox scalar signatures can therefore share the PPC service adapter;
   classic Pascal trap wrappers cannot be called directly with this stack. */
PRIVATE syn68k_addr_t cfm68k_exit(syn68k_addr_t pc, void* opaque)
{
    ppc_context* ctx = opaque;
    (void)pc;
    ctx->exited = 1;
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

PRIVATE syn68k_addr_t cfm68k_import(syn68k_addr_t pc, void* opaque)
{
    ppc_context* ctx = ppc_active_context;
    unsigned id = (uintptr_t)opaque, count = 0;
    ROMlib_ppc_registers r = {0};
    uint32 ret = POPADDR();
    int variadic = 0;
    (void)pc;
    if (!ctx || !id || id > ctx->n_imports)
        return ret;
    uint64 magic = ctx->imports[id - 1].magic >> 3;
    while (magic) {
        ++count;
        magic >>= 3;
    }
    if (!strcmp(strrchr(ctx->imports[id - 1].name, ':') + 1, "CallUniversalProc")) {
        if (!ppc_memory(ctx, EM_A7, 8, FALSE))
            return MAGIC_EXIT_EMULATOR_ADDRESS;
        uint32 info = READUL(EM_A7 + 4);
        unsigned convention = info & 15;
        uint32 fields = info >> (convention == kRegisterBased ? 11 : 6);
        count = 2;
        for (; fields; fields >>= convention == kRegisterBased ? 5 : 2)
            ++count;
        variadic = 1;
    }
    if (count > (variadic ? 13 : 11))
        return ret;
    if (!ppc_memory(ctx, EM_A7, count * 4, FALSE)) {
        snprintf(ctx->error, sizeof ctx->error, "Invalid CFM-68K argument stack");
        return MAGIC_EXIT_EMULATOR_ADDRESS;
    }
    r.gpr[1] = EM_A7 - 24;
    for (unsigned i = 0; i < count && i < 8; ++i)
        r.gpr[3 + i] = READUL(EM_A7 + i * 4);
    if (!ppc_service(ctx, id, &r)) {
        if (!ctx->error[0])
            snprintf(ctx->error, sizeof ctx->error, "CFM-68K import failed: %s", ctx->imports[id - 1].name);
        EM_D0 = paramErr;
    }
    else
        EM_D0 = r.gpr[3];
    if (!variadic)
        EM_A7 += count * 4;
    return ctx->error[0] || ctx->exited ? MAGIC_EXIT_EMULATOR_ADDRESS : ret;
}

PRIVATE ROMlib_pef_image* ppc_prepare_file_status(ppc_context* ctx, FSSpecPtr spec, uint32 offset, uint32 length,
                                                  OSErr* file_error)
{
    ROMlib_pef_environment env;
    ROMlib_pef_image* image = NULL;
    INTEGER ref = 0;
    LONGINT eof;
    OSErr err;
    unsigned char* bytes = NULL;
    Ptr read_buffer = NULL;
    if (file_error)
        *file_error = noErr;
    memset(&env, 0, sizeof env);
    env.opaque = ctx;
    env.allocate = ppc_allocate;
    env.release = ppc_release;
    env.resolve = ppc_resolve;
    err = C_FSpOpenDF(spec, fsRdPerm, &ref);
    ref = CW(ref);
    if (err != noErr)
        goto done;
    err = GetEOF(ref, &eof);
    if (err == noErr && (eof < 0 || offset > (uint32)eof))
        err = paramErr;
    if (err == noErr) {
        if (!length)
            length = eof - offset;
        if (length > (uint32)eof - offset || length > INT32_MAX)
            err = paramErr;
    }
    if (err == noErr && !(bytes = malloc(length ? length : 1)))
        err = memFullErr;
    if (err == noErr)
        err = SetFPos(ref, fsFromStart, offset);
    /* File Manager buffers use guest addresses. Large host allocations can
       exhaust the host aliases and can span multiple alias pages. */
    const unsigned read_size = MIN(length, 64 * 1024U);
    if (err == noErr && read_size && !(read_buffer = NewPtr(read_size)))
        err = memFullErr;
    for (unsigned copied = 0; err == noErr && copied < length;) {
        LONGINT amount = MIN(read_size, length - copied);
        err = FSReadAll(ref, &amount, read_buffer);
        if (err == noErr) {
            memcpy(bytes + copied, read_buffer, amount);
            copied += amount;
        }
    }
    if (read_buffer)
        DisposPtr(read_buffer);
    FSClose(ref);
    if (err == noErr) {
        image = ROMlib_pef_prepare_arch(bytes, length, &env, cfm_architecture(ctx));
        if (!image)
            snprintf(ctx->error, sizeof ctx->error, "%s", env.error);
    }
done:
    if (err != noErr) {
        if (file_error)
            *file_error = err;
        else
            snprintf(ctx->error, sizeof ctx->error, "Unable to read PowerPC fragment (file error %d)", err);
    }
    free(bytes);
    return image;
}

PRIVATE ROMlib_pef_image* ppc_prepare_file(ppc_context* ctx, FSSpecPtr spec, uint32 offset, uint32 length)
{
    return ppc_prepare_file_status(ctx, spec, offset, length, NULL);
}

PRIVATE int ppc_same_spec(const FSSpec* first, const FSSpec* second)
{
    unsigned length = first->name[0];
    return length < sizeof first->name && length == second->name[0] && first->vRefNum == second->vRefNum &&
           first->parID == second->parID && !memcmp(first->name, second->name, length + 1);
}

/* Register all named flat fragments in an extension, preserving its resource
   chain. Files without cfrg resources are ordinary extensions, not libraries. */
PRIVATE OSErr ppc_register_resource(ppc_context* ctx, FSSpecPtr spec, INTEGER resource_id, const char* member_name)
{
    INTEGER saved = CurResFile(), ref = C_FSpOpenResFile(spec, fsRdPerm);
    if (ref == -1)
        return ResError();

    Handle h = Get1Resource(T('c', 'f', 'r', 'g'), resource_id);
    Size size;
    unsigned char *p, *end;
    uint32 count;
    OSErr error = noErr;
    if (!h || (size = GetHandleSize(h)) < 32) {
        error = resNotFound;
        goto done;
    }
    HLock(h);
    p = (unsigned char*)STARH(h);
    end = p + size;
    count = CL(((cfrg_resource_t*)p)->n_descripts);
    p += 32;
    while (count) {
        if (end - p < 43) {
            error = paramErr;
            break;
        }
        --count;
        cfir_t* member = (cfir_t*)p;
        unsigned length = CFIR_LENGTH(member), n;
        ppc_fragment *fragment, *same;
        if (length < 43 || length > (size_t)(end - p) || p[42] > length - 43) {
            error = paramErr;
            break;
        }
        n = p[42];
        if (CFIR_ISA(member) == cfm_architecture(ctx) && CFIR_TYPE(member) == kImportLibraryCFrag &&
            CFIR_LOCATION(member) == kOnDiskFlat && n && n < 64 &&
            (!member_name || (strlen(member_name) == n && !memcmp(member_name, p + 43, n)))) {
            for (same = ctx->fragments; same; same = same->next)
                if (!same->private_copy && strlen(same->name) == n && !memcmp(same->name, p + 43, n))
                    break;
            if (!same) {
                fragment = calloc(1, sizeof *fragment);
                if (!fragment) {
                    error = memFullErr;
                    break;
                }
                fragment->spec = *spec;
                memcpy(fragment->name, p + 43, n);
                fragment->offset = CFIR_OFFSET_TO_FRAGMENT(member);
                fragment->length = CFIR_FRAGMENT_LENGTH(member);
                fragment->owner = fragment;
                for (same = ctx->fragments; same; same = same->next)
                    if (!same->private_copy && same->offset == fragment->offset && same->length == fragment->length &&
                        ppc_same_spec(&same->spec, spec)) {
                        fragment->owner = same->owner;
                        break;
                    }
                fragment->next = ctx->fragments;
                ctx->fragments = fragment;
            }
        }
        p += length;
    }
    HUnlock(h);
done:
    CloseResFile(ref);
    UseResFile(saved);
    return error;
}

PRIVATE OSErr ppc_register_file(ppc_context* ctx, FSSpecPtr spec)
{
    OSErr error = ppc_register_resource(ctx, spec, 0, NULL);
    if (error)
        return error;
    /* CarbonLib 1.4 puts DataBrowserLib in cfrg 1. Its DebugLib in cfrg 2
       requires classic startup state that ROMAW does not provide. */
    OSErr browser = ppc_register_resource(ctx, spec, 1, "DataBrowserLib");
    return browser == resNotFound ? noErr : browser;
}

/* Reload from the original container, including caller-owned memory images. */
PRIVATE ROMlib_pef_image* ppc_prepare_fragment(ppc_context* ctx, ppc_fragment* fragment, OSErr* file_error)
{
    if (!fragment->memory_address)
        return ppc_prepare_file_status(ctx, &fragment->spec, fragment->offset, fragment->length, file_error);
    if (file_error)
        *file_error = noErr;
    ROMlib_pef_environment env = {0};
    env.opaque = ctx;
    env.allocate = ppc_allocate;
    env.release = ppc_release;
    env.resolve = ppc_resolve;
    void* bytes = ppc_memory(ctx, fragment->memory_address, fragment->length, FALSE);
    ROMlib_pef_image* image =
        bytes ? ROMlib_pef_prepare_arch(bytes, fragment->length, &env, cfm_architecture(ctx)) : NULL;
    if (!image && !ctx->error[0])
        snprintf(ctx->error, sizeof ctx->error, "%s", bytes ? env.error : "Invalid memory fragment extent");
    return image;
}

PRIVATE int ppc_reopen_fragment(ppc_context* ctx, ppc_fragment* fragment)
{
    if (!fragment->closed)
        return 1;
    if (fragment->closed == 2) {
        snprintf(ctx->error, sizeof ctx->error, "CFM fragment is terminating: %s", fragment->name);
        return 0;
    }
    while (fragment->dependencies) {
        ppc_dependency* edge = fragment->dependencies;
        fragment->dependencies = edge->next;
        free(edge);
    }
    fragment->connection = fragment->initialization_closure = 0;
    fragment->closed = fragment->initialized = fragment->linking = fragment->notified = 0;
    fragment->visit = fragment->low = fragment->on_stack = 0;
    return 1;
}

PRIVATE void ppc_scan_directory(ppc_context* ctx, INTEGER vref, LONGINT directory)
{
    unsigned i;
    for (i = 1; i <= 4096; ++i) {
        CInfoPBRec pb;
        Str255 name;
        FSSpec spec;
        memset(&pb, 0, sizeof pb);
        PP_ASSIGN(pb.hFileInfo.ioNamePtr, ((Ptr)name));
        pb.hFileInfo.ioVRefNum = CW(vref);
        pb.hFileInfo.ioDirID = CL(directory);
        pb.hFileInfo.ioFDirIndex = CW(i);
        if (PBGetCatInfo(&pb, FALSE) != noErr)
            break;
        if (!(pb.hFileInfo.ioFlAttrib & 0x10) && name[0] <= 63) {
            memset(&spec, 0, sizeof spec);
            spec.vRefNum = CW(vref);
            spec.parID = CL(directory);
            memcpy(spec.name, name, name[0] + 1);
            ppc_register_file(ctx, &spec);
        }
    }
}

PRIVATE int ppc_record_dependency(ppc_context* ctx, ppc_fragment* requester, ppc_fragment* target)
{
    ppc_dependency **head, *p;
    if (requester == target)
        return 1;
    if (!requester && !ctx->linking_application)
        return 1;
    head = requester ? &requester->dependencies : &ctx->application_dependencies;
    for (p = *head; p; p = p->next)
        if (p->target == target)
            return 1;
    p = malloc(sizeof *p);
    if (!p)
        return 0;
    p->target = target;
    p->next = *head;
    *head = p;
    return 1;
}

PRIVATE int ppc_native_dependency(ppc_context* ctx, const char* name)
{
    ppc_fragment* fragment;
    for (fragment = ctx->fragments; fragment; fragment = fragment->next)
        if (!fragment->private_copy && !strcmp(fragment->name, name))
            break;
    if (!fragment) {
        if (strlen(name) > 63)
            return 0;
        fragment = calloc(1, sizeof *fragment);
        if (!fragment)
            return 0;
        strcpy(fragment->name, name);
        fragment->native = 1;
        fragment->owner = fragment;
        fragment->next = ctx->fragments;
        ctx->fragments = fragment;
    }
    fragment = fragment->owner;
    if (!ppc_reopen_fragment(ctx, fragment))
        return 0;
    if (!fragment->connection && !ppc_allocate(ctx, 4, 4, &fragment->connection))
        return 0;
    return ppc_record_dependency(ctx, ctx->requester, fragment);
}

PRIVATE int ppc_external_symbol(ppc_context* ctx, const char* library, const char* name, unsigned symbol_class,
                                uint32* address)
{
    ppc_fragment* fragment;
    unsigned actual_class;
    int found;
    ppc_fragment* requester = ctx->requester;
    for (fragment = ctx->fragments; fragment; fragment = fragment->next)
        if (!fragment->private_copy && !strcmp(library, fragment->name))
            break;
    if (!fragment)
        return 0;
    fragment = fragment->owner;
    if (fragment->native)
        return 0;
    if (!ppc_reopen_fragment(ctx, fragment))
        return -1;
    if (!fragment->image) {
        fragment->image = ppc_prepare_fragment(ctx, fragment, NULL);
        if (!fragment->image)
            return -1;
        fprintf(stderr, "trace: PPC prepared library %s\n", fragment->name);
    }
    ctx->requester = fragment;
    found = ROMlib_pef_find_symbol(fragment->image, name, address, &actual_class);
    ctx->requester = requester;
    if (!found) {
        if (ROMlib_pef_error(fragment->image)[0])
            snprintf(ctx->error, sizeof ctx->error, "%s:%s: %s", library, name, ROMlib_pef_error(fragment->image));
        return ROMlib_pef_error(fragment->image)[0] ? -1 : 0;
    }
    /* The System 9 font fragments declare selected shared globals as
       transition-vector imports. Their relocations use the data address. */
    const boolean_t font_data =
        requester && actual_class == 1 && symbol_class == 2 &&
        ((!strcmp(requester->name, "FontManager") &&
          ((!strcmp(library, "FMSharedCaches") && !strcmp(name, "gBackDefGlobals")) ||
           (!strcmp(library, "GlyphsCache") && name[0] == 'g'))) ||
         (!strcmp(requester->name, "FontObjects") && !strcmp(library, "FontObjectsGlobals") && name[0] == 'g') ||
         (!strcmp(requester->name, "ATSStreaming") &&
          ((!strcmp(library, "ATSSupport") && !strcmp(name, "identityMatrix")) ||
           (!strcmp(library, "GlyphsCache") && name[0] == 'g'))) ||
         /* ATSUnicodeLib at 0x6920, ATSGlue at 0x8f8 and 0xa64, and LayoutCore
            at 0x7bc and 0x551c read through the relocated pointers as data. */
         ((!strcmp(requester->name, "ATSUnicodeLib") || !strcmp(requester->name, "ATSGlue")) &&
          !strcmp(library, "GlyphsCache") && !strcmp(name, "gFracBmapAGPtr")) ||
         ((!strcmp(requester->name, "ATSGlue") || !strcmp(requester->name, "LayoutCore")) &&
          !strcmp(library, "ATSSupport") && !strcmp(name, "identityMatrix")) ||
         (!strcmp(requester->name, "LayoutCore") && !strcmp(library, "GlyphsCache") &&
          !strcmp(name, "gImagePathAGPtr")));
    /* These two System 9 libraries load the context pointer through their
       relocated TOC slots (WindowsLib +0x610, ControlsLib +0x1f8). Their
       import tables declare transition vectors, but ToolboxUtils exports data. */
    const boolean_t toolbox_context =
        requester && actual_class == 1 && symbol_class == 2 &&
        (!strcmp(requester->name, "WindowsLib") || !strcmp(requester->name, "ControlsLib")) &&
        !strcmp(library, "ToolboxUtils") && !strcmp(name, "gSystemToolboxContext");
    if (actual_class != symbol_class && !font_data && !toolbox_context) {
        snprintf(ctx->error, sizeof ctx->error, "PowerPC symbol class mismatch: %s:%s", library, name);
        return -1;
    }
    if (!ppc_record_dependency(ctx, requester, fragment))
        return -1;
    return 1;
}

static int ppc_install_import(ppc_context* ctx, ppc_import binding, const char* library, const char* name,
                              unsigned symbol_class, uint32* address)
{
    ppc_import* grown;
    uint32 stub_address;
    uint32* words;
    if (ctx->n_imports >= 0x03fffffe)
        return 0;
    binding.name = malloc(strlen(library) + strlen(name) + 2);
    if (binding.name)
        sprintf(binding.name, "%s:%s", library, name);
    if (!binding.name)
        return 0;
    words = ppc_allocate(ctx, 12, 4, &stub_address);
    if (!words) {
        free(binding.name);
        return 0;
    }
    /* NewPtr may invoke a guest grow-zone callback, which can register imports.
       Reserve the table slot only after that callback has returned. */
    grown = ctx->n_imports < 0x03fffffe ? realloc(ctx->imports, (ctx->n_imports + 1) * sizeof *grown) : NULL;
    if (!grown) {
        ppc_release(ctx, words);
        free(binding.name);
        return 0;
    }
    ctx->imports = grown;
    if (cfm_architecture(ctx) == kMotorola68KArch)
        stub_address = callback_install(cfm68k_import, (void*)(uintptr_t)(ctx->n_imports + 1));
    else
        words[0] = CL(0x18000000 | (ctx->n_imports + 1));
    words[1] = CL(stub_address);
    words[2] = 0;
    binding.stub = stub_address;
    binding.vector = US_TO_SYN68K(words + 1);
    ctx->imports[ctx->n_imports++] = binding;
    *address = symbol_class == 0 ? binding.stub : binding.vector;
    return 1;
}

void* ROMlib_carbon_timezone_storage(uint32_t size, uint32_t* address)
{
    ppc_context* ctx = ppc_active_context;
    if (!ctx)
        return NULL;
    if (!ctx->carbon_timezone_storage) {
        void* storage = ppc_allocate(ctx, size, 4, &ctx->carbon_timezone_storage);
        if (!storage)
            return NULL;
        memset(storage, 0, size);
    }
    *address = ctx->carbon_timezone_storage;
    return ppc_memory(ctx, *address, size, TRUE);
}

int ROMlib_carbon_native_ppc_callback(const char* name, int (*function)(ROMlib_carbon_ppc_frame*), uint32_t* vector)
{
    ppc_context* ctx = ppc_active_context;
    const ppc_import binding = {.function = function, .magic = ROMLIB_CARBON_PPC_FRAME};
    return ctx && cfm_architecture(ctx) == kPowerPCArch &&
           ppc_install_import(ctx, binding, "CarbonCFCallbacks", name, 2, vector);
}

int ROMlib_carbon_is_cf_system_allocator_worker(unsigned index, uint32_t address)
{
    return ppc_active_context && index < ROMLIB_CARBON_CF_SYSTEM_WORKER_COUNT && address &&
           ppc_active_context->carbon_system_workers[index] == address;
}

static void ppc_adopt_cf_system_allocator(ppc_context* ctx, ppc_fragment* fragment)
{
    if (!ctx->carbon_system_allocator || strcmp(fragment->name, "CoreFoundationLib") ||
        cfm_architecture(ctx) != kPowerPCArch)
        return;

    uint32 vector_address, code_base, code_size, data_base, data_size;
    unsigned type, kind;
    if (!ROMlib_pef_find_symbol(fragment->image, "CFAllocatorGetDefault", &vector_address, &type) || type != 2)
        return;
    const uint32* vector = ppc_memory(ctx, vector_address, 8, FALSE);
    if (!vector || !ROMlib_pef_address_section(fragment->image, CL(vector[0]), 4, &code_base, &code_size, &kind) ||
        kind != 0 || code_size != 0x5dee0 || CL(vector[0]) - code_base != 0x17344 ||
        !ROMlib_pef_address_section(fragment->image, CL(vector[1]), 1, &data_base, &data_size, &kind) ||
        (kind != 1 && kind != 2) || CL(vector[1]) != data_base || data_size != 0x72a8 ||
        ctx->carbon_system_allocator != data_base + 0x3164)
        return;

    const uint32* slot = ppc_memory(ctx, ctx->carbon_system_allocator, 4, FALSE);
    if (!slot || CL(*slot) != data_base + 0x30d0)
        return;
    const uint32* object = ppc_memory(ctx, CL(*slot), 48, FALSE);
    uint32* descriptors = ppc_memory(ctx, data_base + 0x125c, ROMLIB_CARBON_CF_SYSTEM_WORKER_COUNT * 8, TRUE);
    if (!object || !descriptors)
        return;

    const ROMlib_carbon_export* workers = ROMlib_carbon_cf_system_allocator_workers();
    for (unsigned i = 0; i < ROMLIB_CARBON_CF_SYSTEM_WORKER_COUNT; ++i) {
        if (CL(object[8 + i]) != data_base + 0x125c + i * 8 || CL(descriptors[2 * i + 1]) != data_base ||
            CL(descriptors[2 * i]) != code_base + workers[i].reference_offset)
            return;
    }
    for (unsigned i = 0; i < ROMLIB_CARBON_CF_SYSTEM_WORKER_COUNT; ++i) {
        if (!ctx->carbon_system_workers[i]) {
            const ppc_import binding = {.function = workers[i].function, .magic = workers[i].signature};
            if (!ppc_install_import(ctx, binding, "CarbonCFAllocator", workers[i].name, 0,
                                    &ctx->carbon_system_workers[i]))
                return;
        }
    }
    /* Keep the exported word, object, and descriptor addresses shared with
       the PPC runtime. Only the validated entry PCs change. */
    for (unsigned i = 0; i < ROMLIB_CARBON_CF_SYSTEM_WORKER_COUNT; ++i)
        descriptors[2 * i] = CL(ctx->carbon_system_workers[i]);
    if (ERROR_ENABLED_P(ERROR_TRACE_INFO))
        fprintf(stderr, "trace: PPC Carbon installed native system allocator workers\n");
}

static int ppc_carbon_callback_data(ppc_context* ctx, ROMlib_carbon_data_kind kind, uint32* record)
{
    const ROMlib_carbon_export* callbacks = ROMlib_carbon_cf_collection_callbacks();
    const unsigned count = kind == ROMLIB_CARBON_DATA_TYPE_CALLBACKS ? 4 : 5;
    for (unsigned i = 0; i < count; ++i) {
        unsigned index = i;
        if (i == ROMLIB_CARBON_CF_RETAIN && kind == ROMLIB_CARBON_DATA_STRING_HASH_CALLBACKS)
            index = ROMLIB_CARBON_CF_COPY_STRING;
        uint32* address = &ctx->carbon_cf_callback_vectors[index];
        if (!*address) {
            const ppc_import binding = {.function = callbacks[index].function, .magic = callbacks[index].signature};
            if (!ppc_install_import(ctx, binding, "CarbonCFCallbacks", callbacks[index].name, 2, address))
                return 0;
        }
        record[i + 1] = CL(*address);
    }
    return 1;
}

/* Replace complete CF 1.4 initializers before any guest code can call
   them. A non-linking branch preserves the original caller's LR and TOC. */
static void ppc_adopt_cf_initializers(ppc_context* ctx, ppc_fragment* fragment)
{
    if (fragment->private_copy || strcmp(fragment->name, "CoreFoundationLib") || cfm_architecture(ctx) != kPowerPCArch)
        return;

    uint32 code_base, code_size, data_base, data_size;
    unsigned kind;
    const uint32* init = ppc_memory(ctx, ROMlib_pef_init(fragment->image), 8, FALSE);
    if (!init || !ROMlib_pef_address_section(fragment->image, CL(init[0]), 4, &code_base, &code_size, &kind) ||
        kind != 0 || code_size != 0x5dee0 || CL(init[0]) - code_base != 0x17aa0 ||
        !ROMlib_pef_address_section(fragment->image, CL(init[1]), 4, &data_base, &data_size, &kind) ||
        (kind != 1 && kind != 2) || data_size != 0x72a8 || CL(init[1]) != data_base)
        return;

    static const struct {
        uint32 entry, call_site, call_opcode;
        const char* name;
        ROMlib_carbon_frame_entry function;
    } initializers[] = {
        {0x1f08, 0x5397c, 0x4bfae58d, "Preferences", ROMlib_carbon_cf_preference_keys},
        {0x3de8, 0x53974, 0x4bfb0475, "URLProperties", ROMlib_carbon_cf_url_keys},
        {0xd4b8, 0x53954, 0x4bfb9b65, "BundleProperties", ROMlib_carbon_cf_bundle_keys},
        {0x1534c, 0x5395c, 0x4bfc19f1, "PlugInProperties", ROMlib_carbon_cf_plugin_keys},
        {0x17280, 0x538c0, 0x4bfc39c1, "AllocatorConstants", ROMlib_carbon_cf_allocator_constants},
        {0x24598, 0x538fc, 0x4bfd0c9d, "BooleanConstants", ROMlib_carbon_cf_boolean_constants},
        {0x24c1c, 0x53914, 0x4bfd1309, "NumberConstants", ROMlib_carbon_cf_number_constants},
    };

    uint32* code = ppc_memory(ctx, code_base, code_size, TRUE);
    if (!code)
        return;
    /* The companion constructor also has to leave space for a later native
       CFTreeSetContext, including trees created by saved CF or XML code. */
    if (CL(code[0x22d7c / 4]) == 0x7c0802a6 && CL(code[0x22d98 / 4]) == 0x38a00010 &&
        CL(code[0x22e70 / 4]) == 0x38a0001c && CL(code[0x22e7c / 4]) == 0x48030389)
        code[0x22d98 / 4] = CL(0x38a0001c);
    if (!ctx->carbon_cf_shared_data)
        return;
    for (unsigned i = 0; i < NELEM(initializers); ++i)
        if (CL(code[initializers[i].entry / 4]) != 0x7c0802a6 ||
            CL(code[initializers[i].call_site / 4]) != initializers[i].call_opcode)
            return;
    uint32 branches[NELEM(initializers)];
    for (unsigned i = 0; i < NELEM(initializers); ++i) {
        uint32 stub;
        const ppc_import binding = {.function = initializers[i].function, .magic = ROMLIB_CARBON_PPC_FRAME};
        if (!ppc_install_import(ctx, binding, "CarbonCFInit", initializers[i].name, 0, &stub))
            return;
        int64 displacement = (int64)stub - (code_base + initializers[i].entry);
        if (displacement < -0x2000000 || displacement > 0x1fffffc || (displacement & 3))
            return;
        branches[i] = CL(0x48000000 | ((uint32)displacement & 0x03fffffc));
    }
    for (unsigned i = 0; i < NELEM(initializers); ++i)
        code[initializers[i].entry / 4] = branches[i];
}

enum { carbon_data_record_words = 6 };

uint32_t ROMlib_carbon_canonical_cf_callbacks(uint32_t record, const void* reference_toc)
{
    const ppc_context* ctx = ppc_active_context;
    if (!ctx || !ctx->carbon_data || record < ctx->carbon_data)
        return record;
    const uint32 offset = record - ctx->carbon_data, width = carbon_data_record_words * sizeof(uint32);
    size_t count;
    const ROMlib_carbon_data_export* data = ROMlib_carbon_data_exports(&count);
    const uint32 index = offset / width;
    if (offset % width || index >= count || !data[index].reference_data_offset)
        return record;
    return US_TO_SYN68K(reference_toc) + data[index].reference_data_offset;
}

/* Numeric constants and runtime-native callback records keep stable guest
   addresses for the launch. Explicit lookup on a real CFM connection still
   returns the real image's address. */
PRIVATE int ppc_carbon_data(ppc_context* ctx, const char* library, const char* name, uint32* address)
{

    size_t count;
    const ROMlib_carbon_data_export* data = ROMlib_carbon_data_exports(&count);
    for (size_t index = 0; index < count; ++index) {
        if (strcmp(name, data[index].name) ||
            (strcmp(library, "CarbonLib") && strcmp(library, data[index].reference_library)))
            continue;
        if (cfm_architecture(ctx) == kMotorola68KArch && strcmp(library, "MathLib"))
            return 0;
        if (data[index].kind == ROMLIB_CARBON_DATA_SYSTEM_ALLOCATOR) {
            if (ppc_external_symbol(ctx, data[index].reference_library, name, 1, address) != 1)
                return 0;
            ctx->carbon_system_allocator = *address;
            for (ppc_fragment* fragment = ctx->fragments; fragment; fragment = fragment->next)
                if (!fragment->private_copy && !strcmp(fragment->name, data[index].reference_library)) {
                    if (fragment->owner->initialized == 2)
                        ppc_adopt_cf_system_allocator(ctx, fragment->owner);
                    break;
                }
            return 1;
        }
        if (data[index].kind == ROMLIB_CARBON_DATA_CF_SHARED) {
            if (ppc_external_symbol(ctx, data[index].reference_library, name, 1, address) != 1)
                return 0;
            ctx->carbon_cf_shared_data = TRUE;
            return 1;
        }
        if (!ctx->carbon_data) {
            uint32* guest = ppc_allocate(ctx, count * carbon_data_record_words * sizeof(uint32), 8, &ctx->carbon_data);
            if (!guest)
                return 0;
            memset(guest, 0, count * carbon_data_record_words * sizeof(uint32));
            for (size_t i = 0; i < count; ++i) {
                guest[carbon_data_record_words * i] = CL(data[i].words[0]);
                guest[carbon_data_record_words * i + 1] = CL(data[i].words[1]);
            }
        }
        const uint32 location = ctx->carbon_data + index * carbon_data_record_words * sizeof(uint32);
        if (data[index].kind != ROMLIB_CARBON_DATA_WORDS) {
            uint32* record = ppc_memory(ctx, location, carbon_data_record_words * sizeof(uint32), TRUE);
            if (!record || !ppc_carbon_callback_data(ctx, data[index].kind, record))
                return 0;
        }
        *address = location;
        return 1;
    }
    return 0;
}

/* StdCLib's C-locale byte classes and IEEE constants are guest data, never
   host C runtime pointers. FILEs use the classic 24-byte unbuffered layout;
   actual host streams remain private in the launch context. */
PRIVATE int ppc_builtin_data(ppc_context* ctx, const char* library, const char* name, uint32* address)
{
    uint32* words;
    if (!strcmp(library, "SOM")) {
        static const char* const names[] = {"SOMMalloc", "SOMCalloc", "SOMRealloc", "SOMFree"};
        static const char* const workers[] = {"malloc", "calloc", "realloc", "free"};
        for (unsigned index = 0; index < NELEM(names); ++index)
            if (!strcmp(name, names[index])) {
                /* A supplied SOM image owns its allocator variables. Preserve
                   that shared state and its initializer rather than creating
                   a second set of cells beside a real runtime. */
                int original = ppc_external_symbol(ctx, library, name, 1, address);
                if (original)
                    return original > 0;
                if (!ctx->som_allocator_data) {
                    uint32 storage, vectors[4];
                    words = ppc_allocate(ctx, sizeof vectors, 4, &storage);
                    if (!words)
                        return 0;
                    for (unsigned worker = 0; worker < NELEM(workers); ++worker) {
                        ppc_import binding = {0};
                        if (!ppc_install_import(ctx, binding, "StdCLib", workers[worker], 2, &vectors[worker])) {
                            ppc_release(ctx, words);
                            return 0;
                        }
                    }
                    for (unsigned worker = 0; worker < NELEM(workers); ++worker)
                        words[worker] = CL(vectors[worker]);
                    ctx->som_allocator_data = storage;
                }
                /* SOM exports writable variables containing function pointers,
                   not function exports or immutable transition vectors. */
                *address = ctx->som_allocator_data + index * 4;
                return 1;
            }
        return 0;
    }
    if (ppc_carbon_data(ctx, library, name, address))
        return 1;
    if (strcmp(library, "StdCLib"))
        return 0;
    if (!strcmp(name, "__p_CType")) {
        if (!ctx->ctype_data) {
            unsigned char* bytes = ppc_allocate(ctx, 260, 4, &ctx->ctype_data);
            unsigned ch;
            if (!bytes)
                return 0;
            *(uint32*)bytes = CL(ctx->ctype_data + 4);
            for (ch = 0; ch < 256; ++ch) {
                unsigned flags = 0;
                if (ch < 32 || ch == 127)
                    flags |= 32;
                if ((ch >= 9 && ch <= 13) || ch == 32)
                    flags |= 8;
                if (ch == 32)
                    flags |= 64;
                if (ch >= '0' && ch <= '9')
                    flags |= 4;
                if (ch >= 'A' && ch <= 'Z')
                    flags |= 1;
                if (ch >= 'a' && ch <= 'z')
                    flags |= 2;
                if ((ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f'))
                    flags |= 128;
                if (ch > 32 && ch < 127 && !(flags & 7))
                    flags |= 16;
                bytes[4 + ch] = flags;
            }
        }
        *address = ctx->ctype_data;
        return 1;
    }
    if (!strcmp(name, "_DBL_MAX") || !strcmp(name, "_FLT_MAX")) {
        double value = !strcmp(name, "_DBL_MAX") ? DBL_MAX : (double)FLT_MAX;
        uint64 bits;
        uint32* slot = !strcmp(name, "_DBL_MAX") ? &ctx->dbl_max : &ctx->flt_max;
        if (!*slot) {
            memcpy(&bits, &value, sizeof bits);
            words = ppc_allocate(ctx, 8, 8, slot);
            if (!words)
                return 0;
            words[0] = CL(bits >> 32);
            words[1] = CL((uint32)bits);
        }
        *address = *slot;
        return 1;
    }
    if (!strcmp(name, "_iob")) {
        if (!ctx->iob_data) {
            unsigned i;
            unsigned char* bytes = ppc_allocate(ctx, 40 * 24, 4, &ctx->iob_data);
            FILE* standard[3] = {stdin, stdout, stderr};
            if (!bytes)
                return 0;
            memset(bytes, 0, 40 * 24);
            for (i = 0; i < 3; ++i) {
                int fd = dup(fileno(standard[i]));
                if (fd >= 0) {
                    ctx->streams[i] = fdopen(fd, i ? "wb" : "rb");
                    if (!ctx->streams[i])
                        close(fd);
                }
                /* A GUI launch may have no inherited stdin or console handle. */
                if (!ctx->streams[i])
                    ctx->streams[i] = fopen("NUL", i ? "wb" : "rb");
                if (!ctx->streams[i]) {
                    unsigned j;
                    for (j = 0; j < i; ++j) {
                        fclose(ctx->streams[j]);
                        ctx->streams[j] = NULL;
                    }
                    ppc_release(ctx, bytes);
                    ctx->iob_data = 0;
                    return 0;
                }
                *(uint16*)(bytes + i * 24 + 18) = CW((i ? 2 : 1) | 4);
                *(uint16*)(bytes + i * 24 + 20) = CW(i);
            }
        }
        *address = ctx->iob_data;
        return 1;
    }
    return 0;
}

PRIVATE Handle ppc_new_handle(Size size)
{
    return NewHandle(size);
}

PRIVATE Handle ppc_new_handle_clear(Size size)
{
    return NewHandleClear(size);
}

/* These register-trap implementations take a native Handle*, whereas CFM
   callers supply a four-byte big-endian guest Handle slot. */
PRIVATE OSErr ppc_ptr_to_hand(Ptr source, uint32 output, Size size)
{
    void* slot = output ? ppc_memory(ppc_active_context, output, 4, TRUE) : NULL;
    Handle handle = NULL;
    if (!slot)
        return paramErr;
    OSErr error = PtrToHand(source, &handle, size);
    if (!error) {
        uint32 encoded = CL(US_TO_SYN68K_CHECK0(handle));
        memcpy(slot, &encoded, sizeof encoded);
    }
    return error;
}

PRIVATE OSErr ppc_hand_to_hand(uint32 output)
{
    void* slot = output ? ppc_memory(ppc_active_context, output, 4, TRUE) : NULL;
    uint32 encoded;
    if (!slot)
        return paramErr;
    memcpy(&encoded, slot, sizeof encoded);
    uint32 address = CL(encoded);
    Handle handle = address ? ppc_memory(ppc_active_context, address, 4, FALSE) : NULL;
    if (address && !handle)
        return paramErr;
    OSErr error = HandToHand(&handle);
    if (!error) {
        encoded = CL(US_TO_SYN68K_CHECK0(handle));
        memcpy(slot, &encoded, sizeof encoded);
    }
    return error;
}

PRIVATE Ptr ppc_new_ptr(Size size)
{
    return NewPtr(size);
}

PRIVATE Ptr ppc_new_ptr_clear(Size size)
{
    return NewPtrClear(size);
}

PRIVATE Handle ppc_new_handle_sys(Size size)
{
    return NewHandleSys(size);
}

PRIVATE Handle ppc_new_handle_sys_clear(Size size)
{
    return NewHandleSysClear(size);
}

PRIVATE Ptr ppc_new_ptr_sys(Size size)
{
    return NewPtrSys(size);
}

PRIVATE Ptr ppc_new_ptr_sys_clear(Size size)
{
    return NewPtrSysClear(size);
}

PRIVATE Size ppc_max_mem(Size* grow)
{
    return MaxMem(grow);
}

PRIVATE Size ppc_compact_mem(Size size)
{
    return CompactMem(size);
}

/* Universal Interfaces 3.4.1 MacWindows.h defines these WindowsLib calls. */
PRIVATE BOOLEAN ppc_valid_window(uint32 address)
{
    WindowPtr window;
    if (!address || !ppc_active_context)
        return FALSE;
    window = ppc_memory(ppc_active_context, address, 1, FALSE);
    return window && ROMlib_valid_window(window);
}

PRIVATE LONGINT ppc_window_owner_count(uint32 address, uint32 output)
{
    uint32* count;
    if (!output || !ppc_valid_window(address))
        return paramErr;
    count = ppc_memory(ppc_active_context, output, 4, TRUE);
    if (!count)
        return paramErr;
    uint32 owners, status;
    if (!ROMlib_carbon_window_owners(address, 0, &owners, &status))
        return paramErr;
    if (status)
        return (int32)status;
    *count = CL(owners);
    return noErr;
}

PRIVATE void ppc_window_dispose(uint32 address)
{
    WindowPtr window =
        address && ppc_active_context ? ppc_memory(ppc_active_context, address, sizeof(WindowRecord), TRUE) : NULL;
    if (address && !window) {
        ROMlib_carbon_report_failure("Invalid WindowsLib window disposal address");
        return;
    }
    uint32 owners, status;
    if (!ROMlib_carbon_window_owners(address, -1, &owners, &status))
        return;
    if (!status && !owners) {
        C_DisposeWindow(window);
        if (address)
            ROMlib_carbon_drag_forget_window(address);
    }
    else if (status && (int32)status != -5600)
        ROMlib_carbon_report_failure("Could not release WindowsLib window owner");
}

PRIVATE BOOLEAN ppc_floating_windows_active(void)
{
    return ppc_active_context && !ppc_active_context->floating_windows_inactive;
}

PRIVATE void ppc_set_floating_windows_active(BOOLEAN active)
{
    ppc_context* ctx = ppc_active_context;
    if (!ctx || ppc_floating_windows_active() == active)
        return;
    ctx->floating_windows_inactive = !active;

    /* Carbon's floaters belong to the application window list. ROMAW's
       separate floating layer contains host-owned system windows. */
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_probe_reference("CarbonLib", "GetWindowClass", &ref))
        return;
    void* layer = ROMlib_layer_application();
    WindowPeek window = (WindowPeek)ROMlib_layer_children(layer);
    while (window && ROMlib_valid_window((WindowPtr)window)) {
        WindowPeek next = WINDOW_NEXT_WINDOW(window);
        if (WINDOW_VISIBLE_X(window)) {
            _Alignas(8) uint32 window_class = 0;
            uint32 status;
            const uint32 args[] = {US_TO_SYN68K(window), US_TO_SYN68K(&window_class)};
            if (!ROMlib_carbon_invoke_callback(ref.function, args, 2, &status))
                return;
            if (!status && CL(window_class) == 5)
                C_HiliteWindow((WindowPtr)window, active);
        }
        window = next;
    }
}

PRIVATE void ppc_activate_floating_windows(void)
{
    ppc_set_floating_windows_active(TRUE);
}

PRIVATE void ppc_deactivate_floating_windows(void)
{
    ppc_set_floating_windows_active(FALSE);
}

PRIVATE Handle ppc_recover_handle(Ptr p)
{
    return RecoverHandle(p);
}

PRIVATE LONGINT ppc_free_mem(void)
{
    return FreeMem();
}

PRIVATE void ppc_purge_space(Size* a, Size* b)
{
    Size total, contiguous;
    _PurgeSpace_flags(&total, &contiguous, FALSE);
    if (a)
        *a = CL(total);
    if (b)
        *b = CL(contiguous);
}

PRIVATE OSErr ppc_file_transfer(INTEGER reference, LONGINT* count, Ptr buffer, int writing)
{
    ParamBlockRec pb = {0};
    LONGINT length;
    OSErr error;
    if (!count || !ppc_memory(ppc_active_context, US_TO_SYN68K(count), 4, TRUE))
        return paramErr;
    length = CL(*count);
    if (length < 0 ||
        (length && (!buffer || !ppc_memory(ppc_active_context, US_TO_SYN68K(buffer), length, !writing)))) {
        *count = 0;
        return paramErr;
    }
    pb.ioParam.ioRefNum = CW(reference);
    PP_ASSIGN(pb.ioParam.ioBuffer, (buffer));
    pb.ioParam.ioReqCount = *count;
    pb.ioParam.ioPosMode = CWC(fsAtMark);
    error = writing ? PBWrite(&pb, FALSE) : PBRead(&pb, FALSE);
    *count = pb.ioParam.ioActCount;
    return error;
}

PRIVATE OSErr ppc_fsread(INTEGER reference, LONGINT* count, Ptr buffer)
{
    return ppc_file_transfer(reference, count, buffer, FALSE);
}

PRIVATE OSErr ppc_fswrite(INTEGER reference, LONGINT* count, Ptr buffer)
{
    return ppc_file_transfer(reference, count, buffer, TRUE);
}

PRIVATE OSErr ppc_fsopen(StringPtr name, INTEGER volume, INTEGER* reference)
{
    INTEGER result = 0;
    OSErr error;
    if (!reference)
        return paramErr;
    error = FSOpen(name, volume, &result);
    if (!error)
        *reference = CW(result);
    return error;
}

PRIVATE OSErr ppc_get_file_value(INTEGER reference, LONGINT* value, int position)
{
    ParamBlockRec pb = {0};
    OSErr error;
    if (!value)
        return paramErr;
    pb.ioParam.ioRefNum = CW(reference);
    error = position ? PBGetFPos(&pb, FALSE) : PBGetEOF(&pb, FALSE);
    if (!error)
        *value = position ? pb.ioParam.ioPosOffset : pb.ioParam.ioMisc;
    return error;
}

PRIVATE OSErr ppc_get_eof(INTEGER reference, LONGINT* value)
{
    return ppc_get_file_value(reference, value, FALSE);
}

PRIVATE OSErr ppc_get_fpos(INTEGER reference, LONGINT* value)
{
    return ppc_get_file_value(reference, value, TRUE);
}

PRIVATE LONGINT ppc_intl_spec(void)
{
    Ptr globals = ROMlib_script_globals();
    return globals ? US_TO_SYN68K(globals) : 0;
}

PRIVATE void ppc_set_intl_spec(LONGINT value)
{
    IntlSpec = CL(value);
}

PRIVATE void ppc_temp_insert_rom_map(BOOLEAN load)
{
    RomMapInsert = 0xff;
    TmpResLoad = load ? 0xff : 0;
}

PRIVATE void ppc_upper_text(Ptr text, INTEGER length)
{
    if (length > 0 && text && ppc_memory(ppc_active_context, US_TO_SYN68K(text), length, TRUE))
        C_UppercaseText(text, length, smSystemScript);
}

PRIVATE void ppc_lower_text(Ptr text, INTEGER length)
{
    if (length > 0 && text && ppc_memory(ppc_active_context, US_TO_SYN68K(text), length, TRUE))
        C_LowercaseText(text, length, smSystemScript);
}

PRIVATE void ppc_find_word(Ptr text, INTEGER length, INTEGER offset, BOOLEAN left_side, Ptr breaks, INTEGER* offsets)
{
    if (!offsets || !ppc_memory(ppc_active_context, US_TO_SYN68K(offsets), 6 * sizeof *offsets, TRUE))
        return;
    if (length > 0 && (!text || !ppc_memory(ppc_active_context, US_TO_SYN68K(text), length, FALSE)))
        return;
    C_FindWord(text, length, offset, left_side, breaks, offsets);
}

PRIVATE void ppc_long_divide(uint32 result, int32 numerator, int32 denominator)
{
    /* StdCLib passes an address for the two big-endian long results in r3. */
    void* output = ppc_memory(ppc_active_context, result, 2 * sizeof(uint32), TRUE);
    if (!output || !denominator)
        return;
    const int64 dividend = numerator;
    const uint32 words[] = {CL((uint32)(dividend / denominator)), CL((uint32)(dividend % denominator))};
    memcpy(output, words, sizeof words);
}

PRIVATE WindowPeek ppc_window_list(void)
{
    return MR(WindowList);
}

PRIVATE INTEGER ppc_real_script(void)
{
    return 0;
}

PRIVATE INTEGER ppc_font_family_from_name(StringPtr name)
{
    INTEGER family = 0;
    BOOLEAN saved_load = ResLoad;
    OSErr error;
    if (!name || !ppc_memory(ppc_active_context, US_TO_SYN68K(name), (size_t)name[0] + 1, FALSE))
        return -1; /* kInvalidFontFamily. */
    C_GetFNum(name, &family);
    error = ResError();
    SetResLoad(saved_load);
    return error == noErr ? CW(family) : -1;
}

PRIVATE int ppc_get_views_font(ROMlib_carbon_ppc_frame* call)
{
    /* GetViewsFont requires a Pascal font name and a big-endian size. The
       existing theme-font service owns script resolution and font settings. */
    if (!call->words[1] || !call->words[2]) {
        call->words[0] = (uint32_t)(int32_t)paramErr;
        return 1;
    }
    const ROMlib_carbon_export* service = ROMlib_carbon_find_export("GetThemeFont", 2);
    if (!service || !(service->signature & ROMLIB_CARBON_PPC_FRAME))
        return 0;
    ROMlib_carbon_ppc_frame arguments = *call;
    arguments.words[0] = 3; /* kThemeViewsFont. */
    arguments.words[1] = call->words[0];
    arguments.words[2] = call->words[1];
    arguments.words[3] = call->words[2];
    arguments.words[4] = 0;
    if (!((ROMlib_carbon_frame_entry)service->function)(&arguments))
        return 0;
    call->words[0] = arguments.words[0];
    return 1;
}

PRIVATE int ppc_composite_icon_ref(ROMlib_carbon_ppc_frame* call)
{
    void* output = call->words[2] ? ppc_memory(ppc_active_context, call->words[2], 4, TRUE) : NULL;
    if (!output) {
        call->words[0] = (uint32)(int32)paramErr;
        return 1;
    }
    memset(output, 0, 4);
    if (!call->words[0] || !call->words[1]) {
        call->words[0] = (uint32)(int32)-2580; /* invalidIconRefErr. */
        return 1;
    }
    ROMlib_carbon_reference saved;
    if (ROMlib_carbon_probe_reference("IconServicesLib", "CompositeIconRef", &saved)) {
        /* The audited System 9 forwarder needs the real installed IconDispatch
           vector, exactly like the existing Icon Services fallback adapters. */
        uint32* dispatch = ppc_memory(ppc_active_context, 0x17d4, 4, FALSE);
        if (saved.code_size != 0x16fd0 || (dispatch && CL(*dispatch)))
            return ROMlib_carbon_invoke_frame(saved.function, call);
    }
    call->words[0] = (uint32)(int32)-4; /* unimpErr: no compositor is available. */
    return 1;
}

PRIVATE int ppc_get_icon_ref_by_name(ROMlib_carbon_ppc_frame* call)
{
    /* Name lookup uses the same registered creator/type identity and ownership
       as the existing public Icon Services implementation. */
    const ROMlib_carbon_export* service = ROMlib_carbon_find_export("GetIconRef", 2);
    if (!service || !(service->signature & ROMLIB_CARBON_PPC_FRAME))
        return 0;
    ROMlib_carbon_ppc_frame arguments = *call;
    arguments.words[0] = (uint32)(int32)(int16)kOnSystemDisk;
    arguments.words[1] = call->words[0];
    arguments.words[2] = call->words[1];
    arguments.words[3] = call->words[2];
    if (!((ROMlib_carbon_frame_entry)service->function)(&arguments))
        return 0;
    call->words[0] = arguments.words[0];
    return 1;
}

PRIVATE int ppc_get_icon_ref_from_folder(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference saved;
    if (ROMlib_carbon_probe_reference("IconServicesLib", "GetIconRefFromFolder", &saved)) {
        uint32* dispatch = ppc_memory(ppc_active_context, 0x17d4, 4, FALSE);
        if (saved.code_size != 0x16fd0 || (dispatch && CL(*dispatch)))
            return ROMlib_carbon_invoke_frame(saved.function, call);
    }
    /* This entry explicitly describes a folder without a custom icon. Use
       the actual system family, retaining the normal IconRef ownership. */
    const ROMlib_carbon_export* service = ROMlib_carbon_find_export("GetIconRef", 2);
    if (!service || !(service->signature & ROMLIB_CARBON_PPC_FRAME))
        return 0;
    ROMlib_carbon_ppc_frame arguments = *call;
    arguments.words[1] = TICK("macs");
    arguments.words[2] = call->words[2] == 2 && call->words[1] == 1 ? TICK("hdsk") : TICK("fldr");
    arguments.words[3] = call->words[5];
    if (!((ROMlib_carbon_frame_entry)service->function)(&arguments))
        return 0;
    call->words[0] = arguments.words[0];
    return 1;
}

PRIVATE MenuHandle ppc_classic_menu_handle(ppc_context* ctx, uint32 address)
{
    uint32* master = address ? ppc_memory(ctx, address, 4, FALSE) : NULL;
    if (!master || !*master || !ppc_memory(ctx, CL(*master), offsetof(MenuInfo, menuData) + 2, FALSE))
        return NULL;
    return (MenuHandle)master;
}

PRIVATE int ppc_set_submenu_parent_choosable(ROMlib_carbon_ppc_frame* call)
{
    MenuHandle menu = ppc_classic_menu_handle(ppc_active_context, call->words[0]);
    call->words[0] = ROMlib_set_submenu_parent_choosable(menu, (INTEGER)call->words[1], (BOOLEAN)call->words[2]);
    return 1;
}

PRIVATE int ppc_get_submenu_parent_choosable(ROMlib_carbon_ppc_frame* call)
{
    BOOLEAN* output = call->words[2] ? ppc_memory(ppc_active_context, call->words[2], 1, TRUE) : NULL;
    MenuHandle menu = ppc_classic_menu_handle(ppc_active_context, call->words[0]);
    call->words[0] = ROMlib_get_submenu_parent_choosable(menu, (INTEGER)call->words[1], output);
    return 1;
}

PRIVATE int32 ppc_get_theme_drawing_state(uint32 output_address)
{
    ppc_context* ctx = ppc_active_context;
    void* output = output_address ? ppc_memory(ctx, output_address, 4, TRUE) : NULL;
    if (!output)
        return paramErr;
    memset(output, 0, 4);
    if (!thePort || !ROMlib_valid_qd_port(thePort))
        return paramErr;
    unsigned count = 0;
    for (ppc_theme_state* saved = ctx->theme_states; saved; saved = saved->next)
        ++count;
    if (count >= 512)
        return memFullErr;
    ppc_theme_state* state = calloc(1, sizeof *state);
    if (!state)
        return memFullErr;
    if (!ppc_allocate(ctx, 4, 4, &state->token)) {
        free(state);
        return memFullErr;
    }
    GetPenState(&state->pen);
    state->color = CGrafPort_p(thePort);
    state->foreground_index = PORT_FG_COLOR(thePort);
    state->background_index = PORT_BK_COLOR(thePort);
    state->text_mode = PORT_TX_MODE(thePort);
    state->origin = ROMlib_pattern_origin();
    if (state->color) {
        GetForeColor(&state->foreground);
        GetBackColor(&state->background);
        PixPatHandle background = CPORT_BK_PIXPAT(theCPort);
        if (background) {
            state->background_valid = TRUE;
            PATASSIGN(state->background_pattern, PIXPAT_1DATA(background));
            if (PIXPAT_TYPE_X(background) != CWC(pixpat_type_orig))
                state->background_pixpat = background;
        }
    }
    else {
        state->background_valid = TRUE;
        PATASSIGN(state->background_pattern, PORT_BK_PAT(thePort));
    }
    state->next = ctx->theme_states;
    ctx->theme_states = state;
    const uint32 token = CL(state->token);
    memcpy(output, &token, sizeof token);
    return noErr;
}

PRIVATE int32 ppc_dispose_theme_drawing_state(uint32 token)
{
    ppc_context* ctx = ppc_active_context;
    ppc_theme_state** link = &ctx->theme_states;
    while (*link && (*link)->token != token)
        link = &(*link)->next;
    if (!*link)
        return paramErr;
    ppc_theme_state* state = *link;
    *link = state->next;
    ppc_release(ctx, SYN68K_TO_US(state->token));
    free(state);
    return noErr;
}

PRIVATE int32 ppc_set_theme_drawing_state(uint32 token, BOOLEAN dispose)
{
    ppc_context* ctx = ppc_active_context;
    ppc_theme_state* state = ctx->theme_states;
    while (state && state->token != token)
        state = state->next;
    if (!state || !thePort || !ROMlib_valid_qd_port(thePort))
        return paramErr;
    THz zone = GetZone();
    if (thePort == MR(wmgr_port))
        SetZone(MR(SysZone));
    SetPenState(&state->pen);
    if (state->color && CGrafPort_p(thePort)) {
        RGBForeColor(&state->foreground);
        RGBBackColor(&state->background);
        if (state->background_valid) {
            if (state->background_pixpat)
                BackPixPat(state->background_pixpat);
            else
                BackPat(state->background_pattern);
        }
    }
    else {
        ForeColor(state->foreground_index);
        BackColor(state->background_index);
        if (state->background_valid)
            BackPat(state->background_pattern);
    }
    TextMode(state->text_mode);
    ROMlib_QDSetPatternOrigin(state->origin);
    SetZone(zone);
    return dispose ? ppc_dispose_theme_drawing_state(token) : noErr;
}

PRIVATE int32 ppc_normalize_theme_drawing_state(void)
{
    if (!thePort || !ROMlib_valid_qd_port(thePort))
        return paramErr;
    THz zone = GetZone();
    if (thePort == MR(wmgr_port))
        SetZone(MR(SysZone));
    ForeColor(blackColor);
    BackColor(whiteColor);
    PenNormal();
    BackPat(white);
    TextMode(srcOr);
    ROMlib_QDSetPatternOrigin((Point){0, 0});
    SetZone(zone);
    return noErr;
}

PRIVATE BOOLEAN ppc_wait_next_event(INTEGER mask, EventRecord* event, LONGINT sleep, RgnHandle mouse_region)
{
    ppc_context* ctx = ppc_active_context;
    RgnHandle hover_region = NULL;
    BOOLEAN result;

    /* Iconographer uses mouse-moved events to refresh the Colors palette,
     even when its event loop supplies an empty region. */
    if (ctx && ROMlib_creator == TICK("Mngl") && (mask & (1U << osEvt)) && mouse_region && EmptyRgn(mouse_region)) {
        Point position = ctx->iconographer_mouse_valid ? ctx->iconographer_mouse : MouseLocation;
        hover_region = NewRgn();
        if (hover_region) {
            INTEGER x = CW(position.h), y = CW(position.v);
            SetRectRgn(hover_region, x, y, x + 1, y + 1);
            mouse_region = hover_region;
        }
    }
    /* Honor guest trap patches, including Carbon's event-loop integration. */
    result = WaitNextEvent(mask, event, sleep, mouse_region);
    if (hover_region) {
        ctx->iconographer_mouse = event->where;
        ctx->iconographer_mouse_valid = TRUE;
        DisposeRgn(hover_region);
    }
    return result;
}

PRIVATE BOOLEAN ppc_get_next_event(INTEGER mask, EventRecord* event)
{
    return GetNextEvent(mask, event);
}

PRIVATE BOOLEAN ppc_event_avail(INTEGER mask, EventRecord* event)
{
    return EventAvail(mask, event);
}

PRIVATE OSErrRET ppc_post_event(INTEGER type, LONGINT message)
{
    /* The OS event mask is 16 bits. Avoid undefined host shifts for arbitrary
     guest event codes; these events cannot be enabled. */
    if ((unsigned)type >= 16)
        return evtNotEnb;
    return PostEvent(type, message);
}

PRIVATE LONGINT ppc_key_translate(Ptr map, uint16 code, LONGINT* state)
{
    LONGINT native_state, result;
    if (!map || !state)
        return 0;
    native_state = CL(*state);
    result = C_KeyTrans(map, code, &native_state);
    *state = CL(native_state);
    return result;
}

PRIVATE void ppc_init_zone(ProcPtr grow, INTEGER masters, Ptr limit, THz start)
{
    const uintptr_t first = (uintptr_t)start, last = (uintptr_t)limit;
    if (!start || (first & 1) || (last & 1) || last <= first || masters <= 0 ||
        last - first < sizeof(Zone) + 64 + (unsigned)masters * sizeof(uint32) ||
        !ROMlib_win_accessible_span(start, last - first, TRUE)) {
        MemErr = CWC(paramErr);
        return;
    }
    InitZone(grow, masters, limit, start);
}

PRIVATE THz ppc_application_zone(void)
{
    return MR(ApplZone);
}

PRIVATE INTEGER ppc_cur_ap_ref(void)
{
    return CW(CurApRefNum);
}

PRIVATE StringPtr ppc_cur_ap_name(void)
{
    return CurApName;
}

PRIVATE LONGINT ppc_cur_dir_store(void)
{
    return CL(CurDirStore);
}

static OSErr ppc_replace_gestalt(OSType selector, ProcPtr procedure, HIDDEN_ProcPtr* previous)
{
    if (!previous || !ROMlib_win_accessible_span(previous, sizeof *previous, TRUE))
        return paramErr;
    ProcPtr old_procedure;
    const OSErr result = ReplaceGestalt(selector, procedure, &old_procedure);
    if (!result)
        HIDDEN_RAW_SET(*previous, RM(old_procedure));
    return result;
}

PRIVATE void ppc_set_cur_dir_store(LONGINT directory)
{
    CurDirStore = CL(directory);
}

PRIVATE ProcPtr ppc_lm_get_mbar_hook(void)
{
    return HIDDEN_NATIVE(MBarHook_H);
}

PRIVATE void ppc_lm_set_mbar_hook(ProcPtr hook)
{
    HIDDEN_RAW_SET(MBarHook_H, RM(hook));
}

PRIVATE INTEGER ppc_lm_get_paint_white(void)
{
    return CW(PaintWhite);
}

PRIVATE void ppc_lm_set_paint_white(INTEGER value)
{
    PaintWhite = CW(value);
}

PRIVATE Byte ppc_lm_get_sd_volume(void)
{
    return SdVolume;
}

PUBLIC int ROMlib_ppc_gestalt(OSType selector, LONGINT* response)
{
    if (!ppc_active_context || cfm_architecture(ppc_active_context) == kMotorola68KArch) {
        if (selector == TICK("cfrg") || selector == TICK("mixd")) {
            /* CFM is built in. Mixed Mode supports CFM-68K, without claiming
             the separate dispatch-trap or save/restore-state extensions. */
            *response = CL(selector == TICK("cfrg") ? 1 : 2);
            return 1;
        }
    }
    if (!ppc_active_context)
        return 0;
    if (selector == TICK("drag") && cfm_architecture(ppc_active_context) == kPowerPCArch) {
        /* Drag Manager present, PPC DragLib present, translucent images supported. */
        *response = CL((1 << 0) | (1 << 2) | (1 << 3));
        return 1;
    }
    /* The saved initializer probes the registry before it creates the
       shared font collections and registers its own Appearance selectors. */
    if (selector == TICK("appr"))
        for (const ppc_fragment* fragment = ppc_active_context->fragments; fragment; fragment = fragment->next)
            if (fragment == fragment->owner && fragment->initialized == 1 && fragment->image &&
                !strcmp(fragment->name, "AppearanceLib"))
                return 0;

    /* The native Appearance bindings are available to PowerPC fragments. */
    if (selector == TICK("appr") || selector == TICK("apvr")) {
        *response = CL(selector == TICK("appr") ? 1 : 0x0100);
        return 1;
    }
    if (selector == gestaltSysArchitecture) {
        *response = CL(cfm_architecture(ppc_active_context) == kMotorola68KArch ? gestalt68k : gestaltPowerPC);
        return 1;
    }
    if (selector == gestaltNativeCPUtype) {
        *response = CL(cfm_architecture(ppc_active_context) == kMotorola68KArch ? gestaltCPU68040 : gestaltCPU750);
        return 1;
    }
    return 0;
}

PRIVATE OSErr ppc_gestalt(OSType selector, LONGINT* response)
{
    if (ppc_active_context && cfm_architecture(ppc_active_context) == kMotorola68KArch)
        return Gestalt(selector, response);
    if (selector == gestaltSysArchitecture) {
        *response = CL(gestaltPowerPC);
        return noErr;
    }
    if (selector == gestaltNativeCPUtype) {
        *response = CL(gestaltCPU750);
        return noErr;
    }
    OSErr error = Gestalt(selector, response);
    if (selector == TICK("thds") && error == gestaltUndefSelectorErr && response) {
        /* The native cooperative Thread Manager implements the library and
           exact stack-pool matching. Keep a guest-registered selector intact. */
        *response = CLC(7); /* Present, specific-match support, ThreadsLib present. */
        return noErr;
    }
    return error;
}

PRIVATE OSErrRET ppc_sys_environs(INTEGER version, uint32 address)
{
    if (version <= 0)
        return envBadVers;
    SysEnvRecPtr output = address ? ppc_memory(ppc_active_context, address, sizeof *output, TRUE) : NULL;
    return output ? SysEnvirons(version, output) : paramErr;
}

PRIVATE OSErr ppc_PBReadSync(void* pb)
{
    return PBRead(pb, FALSE);
}

PRIVATE OSErr ppc_PBReadAsync(void* pb)
{
    return PBRead(pb, TRUE);
}

PRIVATE OSErr ppc_PBWriteSync(void* pb)
{
    return PBWrite(pb, FALSE);
}

PRIVATE OSErr ppc_PBWriteAsync(void* pb)
{
    return PBWrite(pb, TRUE);
}

PRIVATE OSErr ppc_PBCloseSync(void* pb)
{
    return PBClose(pb, FALSE);
}

PRIVATE OSErr ppc_PBCloseAsync(void* pb)
{
    return PBClose(pb, TRUE);
}

PRIVATE OSErr ppc_PBGetEOFSync(void* pb)
{
    return PBGetEOF(pb, FALSE);
}

PRIVATE OSErr ppc_PBGetEOFAsync(void* pb)
{
    return PBGetEOF(pb, TRUE);
}

PRIVATE OSErr ppc_PBSetEOFSync(void* pb)
{
    return PBSetEOF(pb, FALSE);
}

PRIVATE OSErr ppc_PBSetEOFAsync(void* pb)
{
    return PBSetEOF(pb, TRUE);
}

PRIVATE OSErr ppc_PBSetFPosSync(void* pb)
{
    return PBSetFPos(pb, FALSE);
}

PRIVATE OSErr ppc_PBSetFPosAsync(void* pb)
{
    return PBSetFPos(pb, TRUE);
}

PRIVATE OSErr ppc_PBGetCatInfoSync(void* pb)
{
    return PBGetCatInfo(pb, FALSE);
}

PRIVATE OSErr ppc_PBGetCatInfoAsync(void* pb)
{
    return PBGetCatInfo(pb, TRUE);
}

PRIVATE OSErr ppc_PBSetCatInfoSync(void* pb)
{
    return PBSetCatInfo(pb, FALSE);
}

PRIVATE OSErr ppc_PBSetCatInfoAsync(void* pb)
{
    return PBSetCatInfo(pb, TRUE);
}

PRIVATE OSErr ppc_PBGetFCBInfoSync(void* pb)
{
    return PBGetFCBInfo(pb, FALSE);
}

PRIVATE OSErr ppc_PBGetFCBInfoAsync(void* pb)
{
    return PBGetFCBInfo(pb, TRUE);
}

PRIVATE OSErr ppc_PBHGetVInfoSync(void* pb)
{
    return PBHGetVInfo(pb, FALSE);
}

PRIVATE OSErr ppc_PBHGetVInfoAsync(void* pb)
{
    return PBHGetVInfo(pb, TRUE);
}

PRIVATE OSErr ppc_PBHGetVolParmsSync(HParmBlkPtr pb)
{
    return PBHGetVolParms(pb, FALSE);
}

PRIVATE OSErr ppc_PBDTGetIconSync(DTPBPtr pb)
{
    return PBDTGetIcon(pb, FALSE);
}

PRIVATE OSErr ppc_PBDTGetAPPLSync(DTPBPtr pb)
{
    return pb ? PBDTGetAPPL(pb, FALSE) : paramErr;
}

PRIVATE OSErr ppc_PBDTGetAPPLAsync(DTPBPtr pb)
{
    return pb ? PBDTGetAPPL(pb, TRUE) : paramErr;
}

PRIVATE OSErr ppc_PBDTGetCommentSync(DTPBPtr pb)
{
    return PBDTGetComment(pb, FALSE);
}

PRIVATE OSErr ppc_PBDTGetCommentAsync(DTPBPtr pb)
{
    return PBDTGetComment(pb, TRUE);
}

/* Universal Interfaces 3.4.2 Files.h: XVolumeParam has the HVolumeParam
   layout, with ioXVersion at 24 and two 64-bit byte counts at 122/130. */
PRIVATE OSErr ppc_PBXGetVolInfoSync(uint32 address)
{
    ppc_context* ctx = ppc_active_context;
    HParamBlockRec local;
    HVolumeParam* pb = &local.volumeParam;
    unsigned char* guest = address ? ppc_memory(ctx, address, 138, TRUE) : NULL;
    OSErr err = paramErr;
    uint64 total = 0, free_bytes = 0;
    if (!guest)
        return paramErr;
    memset(&local, 0, sizeof local);
    memcpy(pb, guest, sizeof *pb);
    if (!pb->pfiller2) {
        uint32 name = CL((uint32)(uintptr_t)PP_RAW(pb->ioNamePtr));
        unsigned char* text = name ? ppc_memory(ctx, name, 1, FALSE) : NULL;
        if (!name || (text && ppc_memory(ctx, name, MAX(28, text[0] + 1), TRUE))) {
            err = PBHGetVInfo(&local, FALSE);
            if (err == noErr) {
                VCBExtra* vcb = (VCBExtra*)ROMlib_vcbbyvrn(CW(pb->ioVRefNum));
                if (!vcb)
                    err = nsvErr;
                else if (!vcb->vcb.vcbCTRef) {
                    if (!ROMlib_win_volume_bytes(vcb->unixname, &total, &free_bytes))
                        err = ioErr;
                }
                else {
                    total = (uint64)CW(pb->ioVNmAlBlks) * (uint32)CL(pb->ioVAlBlkSiz);
                    free_bytes = (uint64)CW(pb->ioVFrBlk) * (uint32)CL(pb->ioVAlBlkSiz);
                }
            }
        }
    }
    if (err == noErr) {
        uint32 counts[4] = {CL((uint32)(total >> 32)), CL((uint32)total), CL((uint32)(free_bytes >> 32)),
                            CL((uint32)free_bytes)};
        memcpy(guest, pb, sizeof *pb);
        memcpy(guest + 122, counts, sizeof counts);
    }
    pb->ioResult = CW(err);
    memcpy(guest + 16, &pb->ioResult, sizeof pb->ioResult);
    return err;
}

PRIVATE OSErr ppc_PBHCreateSync(void* pb)
{
    return PBHCreate(pb, FALSE);
}

PRIVATE OSErr ppc_PBHCreateAsync(void* pb)
{
    return PBHCreate(pb, TRUE);
}

PRIVATE OSErr ppc_PBHDeleteSync(void* pb)
{
    return PBHDelete(pb, FALSE);
}

PRIVATE OSErr ppc_PBHDeleteAsync(void* pb)
{
    return PBHDelete(pb, TRUE);
}

PRIVATE OSErr ppc_PBHOpenDFSync(void* pb)
{
    return PBOpenDF(pb, FALSE);
}

PRIVATE OSErr ppc_PBHGetFInfoSync(void* pb)
{
    return PBHGetFInfo(pb, FALSE);
}

PRIVATE OSErr ppc_PBHSetFInfoSync(void* pb)
{
    return PBHSetFInfo(pb, FALSE);
}

PRIVATE OSErr ppc_PBHOpenSync(void* pb)
{
    return PBHOpen(pb, FALSE);
}

PRIVATE OSErr ppc_PBHOpenRFSync(void* pb)
{
    return PBHOpenRF(pb, FALSE);
}

PRIVATE OSErr ppc_PBHOpenDenySync(HParmBlkPtr pb)
{
    return pb ? OpenDeny(pb, FALSE) : paramErr;
}

PRIVATE OSErr ppc_launch_application(LaunchParamBlockRec* parameters)
{
    if (!parameters || !ROMlib_win_accessible_span(parameters, sizeof *parameters, FALSE) ||
        parameters->launchBlockID != CWC(extendedBlock) || CL(parameters->launchEPBLength) < extendedBlockLen)
        return paramErr;
    FSSpec* application = PPR(parameters->launchAppSpec);
    if (!application || !ROMlib_win_accessible_span(application, sizeof *application, FALSE))
        return paramErr;
    return NewLaunch(NULL, 0, parameters);
}

PRIVATE OSErr ppc_PBGetWDInfoSync(void* pb)
{
    return PBGetWDInfo(pb, FALSE);
}

PRIVATE OSErr ppc_PBResolveFileIDRefSync(void* pb)
{
    return PBResolveFileIDRef(pb, FALSE);
}

PRIVATE OSErr ppc_PBCreateFileIDRefSync(void* pb)
{
    return PBCreateFileIDRef(pb, FALSE);
}

PRIVATE OSErr ppc_hsetfinfo(INTEGER vref, LONGINT directory, StringPtr name, FInfo* info)
{
    HParamBlockRec pb;
    OSErr err;
    if (!info)
        return paramErr;
    memset(&pb, 0, sizeof pb);
    PP_ASSIGN(pb.fileParam.ioNamePtr, (name));
    pb.fileParam.ioVRefNum = CW(vref);
    pb.fileParam.ioDirID = CL(directory);
    err = PBHGetFInfo(&pb, FALSE);
    if (err == noErr) {
        pb.fileParam.ioFlFndrInfo = *info;
        err = PBHSetFInfo(&pb, FALSE);
    }
    return err;
}

PRIVATE OSErr ppc_hgetvol(StringPtr name, INTEGER* vref, LONGINT* directory)
{
    WDPBRec pb;
    OSErr err;
    memset(&pb, 0, sizeof pb);
    PP_ASSIGN(pb.ioNamePtr, (name));
    err = PBHGetVol(&pb, FALSE);
    if (err == noErr) {
        if (vref)
            *vref = pb.ioVRefNum;
        if (directory)
            *directory = pb.ioWDDirID;
    }
    return err;
}

PRIVATE OSErr ppc_get_front_process(ProcessSerialNumber* serial)
{
    return C_GetFrontProcess(serial, NULL);
}

/* PrivateInterfaceLib returns the full A5 QuickDraw record, whose
   current port slot is the final four bytes at offset 0xca. */
PRIVATE void* ppc_get_qd_globals(void)
{
    Byte* port_slot = STARH((HIDDEN_BytePtr*)SYN68K_TO_US(a5));
    return port_slot ? port_slot - (grafSize - sizeof(HIDDEN_GrafPtr)) : NULL;
}

PRIVATE void* ppc_key_map(void)
{
    return KeyMap;
}

static void ppc_lm_get_desk_pattern(Pattern* output)
{
    if (output && ROMlib_win_accessible_span(output, sizeof *output, TRUE))
        memmove(output, DeskPattern, sizeof *output);
}

static void ppc_lm_set_desk_pattern(const Pattern* input)
{
    if (input && ROMlib_win_accessible_span(input, sizeof *input, FALSE))
        memmove(DeskPattern, input, sizeof *input);
}

static void ppc_lm_get_drag_pattern(Pattern* output)
{
    if (output && ROMlib_win_accessible_span(output, sizeof *output, TRUE))
        memmove(output, DragPattern, sizeof *output);
}

static void ppc_lm_set_drag_pattern(const Pattern* input)
{
    if (input && ROMlib_win_accessible_span(input, sizeof *input, FALSE))
        memmove(DragPattern, input, sizeof *input);
}

PRIVATE GDHandle ppc_dm_first_screen_device(Boolean active_only)
{
    const uint16_t mask = (1 << screenDevice) | (active_only ? 1 << screenActive : 0);
    for (GDHandle device = MR(DeviceList); device; device = GD_NEXT_GD(device))
        if ((CW(GD_FLAGS_X(device)) & mask) == mask)
            return device;
    return NULL;
}

enum { dm_sw_not_initialized = -6227, dm_display_not_found = -6229, dm_first_display_id = 0x100 };

static unsigned ppc_dm_device_index(const ppc_context* ctx, GDHandle device)
{
    unsigned i = 0;
    while (i < ctx->display_count && ctx->displays[i].device != device)
        ++i;
    return i;
}

static OSErr ppc_dm_refresh_devices(ppc_context* ctx)
{
    const GDHandle first = ppc_dm_first_screen_device(FALSE);
    for (unsigned i = 0; i < ctx->display_count;) {
        GDHandle live = first;
        while (live && (live != ctx->displays[i].device || !(CW(GD_FLAGS_X(live)) & (1 << screenDevice))))
            live = GD_NEXT_GD(live);
        if (live)
            ++i;
        else
            ctx->displays[i] = ctx->displays[--ctx->display_count];
    }
    if (!first)
        return dm_sw_not_initialized;
    if (!ctx->next_display_id) {
        ctx->next_display_id = dm_first_display_id;
        const GDHandle main = MR(MainDevice);
        if (main && (CW(GD_FLAGS_X(main)) & (1 << screenDevice))) {
            ctx->displays[0].device = main;
            ctx->displays[0].id = ctx->next_display_id++;
            ctx->display_count = 1;
        }
    }
    for (GDHandle device = first; device; device = GD_NEXT_GD(device)) {
        if (!(CW(GD_FLAGS_X(device)) & (1 << screenDevice)))
            continue;
        const unsigned i = ppc_dm_device_index(ctx, device);
        if (i < ctx->display_count)
            continue;
        if (i == NELEM(ctx->displays) || ctx->next_display_id == UINT32_MAX)
            return memFullErr;
        ctx->displays[i].device = device;
        ctx->displays[i].id = ctx->next_display_id++;
        ++ctx->display_count;
    }
    return noErr;
}

PRIVATE OSErr ppc_dm_get_display_id(GDHandle requested, uint32* id, Boolean fall_back_to_main)
{
    if (!id)
        return paramErr;
    *id = 0;
    ppc_context* ctx = ppc_active_context;
    if (!ctx)
        return dm_sw_not_initialized;
    const OSErr status = ppc_dm_refresh_devices(ctx);
    if (status)
        return status;
    if (!requested && !fall_back_to_main)
        return paramErr;
    unsigned index = ppc_dm_device_index(ctx, requested);
    if (index == ctx->display_count && fall_back_to_main)
        index = ppc_dm_device_index(ctx, MR(MainDevice));
    if (index == ctx->display_count)
        return dm_display_not_found;
    *id = CL(ctx->displays[index].id);
    return noErr;
}

PRIVATE Handle ppc_top_resource_map(void)
{
    return (Handle)MR(TopMapHndl);
}

static Handle ppc_lm_get_da_strings(INTEGER index)
{
    return (unsigned)index < NELEM(DAStrings_H) ? HIDDEN_NATIVE(DAStrings_H[index]) : NULL;
}

static void ppc_lm_set_da_strings(Handle strings, INTEGER index)
{
    if ((unsigned)index < NELEM(DAStrings_H))
        HIDDEN_RAW_SET(DAStrings_H[index], RM(strings));
}

PRIVATE Handle ppc_get_resource_map(INTEGER reference)
{
    return (Handle)ROMlib_rntohandl(reference, NULL);
}

/* Resource map overrides are runs of maps with bit 1 of the file attributes.
   The first map in the run preceding the supplied map replaces that map. */
PRIVATE Handle ppc_get_override_map(Handle target)
{
    resmaphand map, override = NULL;
    if (!target) {
        ROMlib_setreserr(resFNotFound);
        return NULL;
    }
    for (map = (resmaphand)MR(TopMapHndl); map && (Handle)map != target; map = (resmaphand)HxP(map, nextmap))
        if (Hx(map, resfatr) & 2) {
            if (!override)
                override = map;
        }
        else
            override = NULL;
    ROMlib_setreserr(noErr);
    return override ? (Handle) override : target;
}

PRIVATE void ppc_lock_high(Handle handle)
{
    MoveHHi(handle);
    HLock(handle);
}

PRIVATE OSErr ppc_is_system_menu(INTEGER menu_id, Boolean* system)
{
    if (!system)
        return paramErr;
    /* MenuMgrPriv.h reserves the inclusive ID range 0xB000..0xBFFF. */
    *system = menu_id >= -20480 && menu_id <= -16385;
    return noErr;
}

PRIVATE Boolean ppc_menu_bar_visible(void)
{
    return CW(MBarHeight) > 0;
}

PRIVATE INTEGER ppc_menu_bar_height(void)
{
    return CW(MBarHeight);
}

PRIVATE OSErr ppc_get_aux_menu_item(MenuHandle menu, StringPtr item, void* output, Byte size)
{
    /* ROMAW has classic menu records, without auxiliary item records.
     Report the unavailable extension explicitly. MDEFs can retain the
     classic fields they already read; do not manufacture an aux record. */
    (void)menu;
    (void)item;
    (void)output;
    (void)size;
    return -4; /* unimpErr */
}

PRIVATE OSErr ppc_get_keyboard_focus(WindowPtr window, HIDDEN_ControlHandle* focus)
{
    if (!focus || !ROMlib_valid_window(window) || !ROMlib_win_accessible_span(focus, sizeof *focus, TRUE))
        return paramErr;
    return ROMlib_control_focus(window, NULL, 0, focus);
}

PRIVATE LONGINT ppc_clear_keyboard_focus(WindowPtr window)
{
    return ROMlib_control_focus(window, NULL, 0, NULL);
}

PRIVATE OSErr ppc_set_keyboard_focus(WindowPtr window, ControlHandle control, INTEGER part)
{
    return !control && part ? paramErr : ROMlib_control_focus(window, control, part, NULL);
}

PRIVATE LONGINT ppc_set_theme_window_background(WindowPtr window, INTEGER brush, BOOLEAN update)
{
    HIDDEN_GrafPtr saved;

    if (!ROMlib_valid_window(window))
        return paramErr;
    if (brush != 1 && brush != 2)
        return -4; /* unimpErr */
    GetPort(&saved);
    SetPort((GrafPtr)window);
    AuxWinHandle aux = HIDDEN_NATIVE(*lookup_aux_win(window));
    if (aux)
        HxX(aux, awFlags) = CL(Hx(aux, awFlags) | ROMlib_theme_window_background);
    RGBBackColor(&ROMlib_theme_background_rgb);
    if (update)
        InvalRect(&PORT_RECT((GrafPtr)window));
    SetPort(HIDDEN_NATIVE(saved));
    return noErr;
}

PRIVATE OSErr ppc_get_window_region(WindowPtr window, INTEGER code, RgnHandle output)
{
    if (!output || !ROMlib_valid_window(window))
        return paramErr;
    if (code != 32 && code != 33)
        return -30593; /* errWindowRegionCodeInvalid */
    /* Applications query the geometry of hidden windows before positioning
     them. The classic Window Manager normally defers their region calculation
     until ShowWindow. */
    if (!WINDOW_VISIBLE(window)) {
        THEPORT_SAVE_EXCURSION(MR(wmgr_port), { WINDCALL(window, wCalcRgns, 0); });
    }
    if (code == 32)
        CopyRgn(WINDOW_STRUCT_REGION(window), output);
    else
        CopyRgn(WINDOW_CONT_REGION(window), output);
    return noErr;
}

PRIVATE LONGINT ppc_get_dialog_item_as_control(DialogPtr dialog, INTEGER item, HIDDEN_ControlHandle* control)
{
    if (!control || !ROMlib_win_accessible_span(control, sizeof *control, TRUE))
        return paramErr;
    return ROMlib_get_dialog_control((WindowPtr)dialog, item, control);
}

PRIVATE LONGINT ppc_set_image_well_content(ControlHandle control, const Byte* content)
{
    if (!content || !ROMlib_win_accessible_span(content, 6, FALSE))
        return paramErr;
    return ROMlib_set_control_image_content(control, content, ROMlib_image_well_proc);
}

PRIVATE LONGINT ppc_get_image_well_content(ControlHandle control, Byte* content)
{
    if (!content || !ROMlib_win_accessible_span(content, 6, TRUE))
        return paramErr;
    return ROMlib_get_control_image_content(control, content, ROMlib_image_well_proc);
}

PRIVATE LONGINT ppc_set_bevel_content(ControlHandle control, const Byte* content)
{
    if (!content || !ROMlib_win_accessible_span(content, 6, FALSE))
        return paramErr;
    return ROMlib_set_control_image_content(control, content, ROMlib_bevel_button_proc);
}

PRIVATE LONGINT ppc_get_bevel_content(ControlHandle control, Byte* content)
{
    if (!content || !ROMlib_win_accessible_span(content, 6, TRUE))
        return paramErr;
    return ROMlib_get_control_image_content(control, content, ROMlib_bevel_button_proc);
}

PRIVATE OSErr ppc_get_bevel_menu_value(ControlHandle control, INTEGER* value)
{
    if (!value || !ROMlib_win_accessible_span(value, sizeof *value, TRUE))
        return paramErr;
    return ROMlib_get_bevel_menu_value(control, value);
}

/* Appearance's native controls share the classic tracking and Mixed Mode action
 * callback path. Modifier-aware custom CDEF tracking is not implemented here. */
PRIVATE INTEGER ppc_handle_control_click(ControlHandle control, Point where, INTEGER modifiers, ProcPtr action)
{
    WindowPeek window;
    ControlHandle candidate;
    (void)modifiers;
    if (!control)
        return 0;
    for (window = MR(WindowList); window; window = WINDOW_NEXT_WINDOW(window))
        for (candidate = WINDOW_CONTROL_LIST(window); candidate; candidate = CTL_NEXT_CONTROL(candidate))
            if (candidate == control) {
                /* Do not apply TrackControl's legacy global-coordinate fallback
                 * when the specified local point misses this control. */
                if (!C_TestControl(control, where))
                    return 0;
                return C_TrackControl(control, where, action);
            }
    return 0;
}

PRIVATE LONGINT ppc_set_control_data(ControlHandle control, INTEGER part, ResType tag, LONGINT size, const Byte* data)
{
    if (!control || size < 0 || (size && (!data || !ROMlib_win_accessible_span(data, size, FALSE))))
        return paramErr;
    LONGINT result = ROMlib_control_data(control, part, tag, size, (Byte*)data, NULL, ROMLIB_CONTROL_WRITE);
    return result == -30591 ? paramErr : result;
}

PRIVATE BOOLEAN ppc_is_control_visible(ControlHandle control)
{
    return control && CTL_VIS(control);
}

PRIVATE OSErr ppc_set_up_control_background(ControlHandle control, INTEGER depth, BOOLEAN color_device)
{
    return ROMlib_set_up_control_color(control, depth, color_device, FALSE);
}

PRIVATE LONGINT ppc_get_control_data(ControlHandle control, INTEGER part, ResType tag, Size size, Byte* data,
                                     Size* actual)
{
    if (size < 0 || (size && (!data || !ROMlib_win_accessible_span(data, size, TRUE))) ||
        (actual && !ROMlib_win_accessible_span(actual, sizeof *actual, TRUE)))
        return paramErr;
    return ROMlib_control_data(control, part, tag, size, data, actual, ROMLIB_CONTROL_READ);
}

PRIVATE BOOLEAN ppc_is_control_active(ControlHandle control)
{
    return control && CTL_HILITE(control) != 255;
}

PRIVATE OSErr ppc_activate_control(ControlHandle control)
{
    return ROMlib_control_activate(control, TRUE);
}

PRIVATE OSErr ppc_deactivate_control(ControlHandle control)
{
    return ROMlib_control_activate(control, FALSE);
}

PRIVATE OSErr ppc_set_control_visibility(ControlHandle control, BOOLEAN visible, BOOLEAN draw)
{
    if (!control)
        return paramErr;
    if (!draw)
        CTL_VIS(control) = visible ? 255 : 0;
    else if (visible)
        ShowControl(control);
    else
        HideControl(control);
    return noErr;
}

PRIVATE LONGINT ppc_ot_interface_unavailable(void* info, LONGINT index)
{
    if (!info)
        return paramErr;
    if (index < -1)
        return -3221; /* kEINVALErr */
    /* No Open Transport TCP/IP provider is active. Preserve the caller's
       record and report no interface, as an unloaded stack does on Mac OS. */
    return -3201; /* kOTNotFoundErr */
}

static OSErr ppc_window_proxy_unavailable(uint32 window, const FSSpec* spec)
{
    if (!ppc_valid_window(window))
        return -5600; /* errInvalidWindowPtr */
    if (!spec)
        return paramErr;
    /* Native title bars have no draggable file proxy. Report that optional
       service as unavailable so the application can retain its plain title. */
    return -4; /* unimpErr */
}

static OSErr ppc_toolbox_sync(uint32 address, uint16 selector)
{
    ppc_context* ctx = ppc_active_context;
    Byte* parameter_block = address ? ppc_memory(ctx, address, 18, TRUE) : NULL;
    if (!parameter_block)
        return paramErr;
    M68kReg saved[16];
    memcpy(saved, cpu_state.regs, sizeof saved);
    EM_A0 = address;
    EM_D0 = selector;
    CALL_EMULATOR(CL(ctx->os_vectors[0xdd]));
    const OSErr status = EM_D0;
    memcpy(cpu_state.regs, saved, sizeof saved);
    /* PPCParamBlockRec shares the IO header's ioResult field at offset 16.
       The native unpatched PPC Toolbox reports paramErr; it never invents a
       working interprocess port. A guest-installed provider remains callable. */
    const uint16 encoded = CW(status);
    memcpy(parameter_block + 16, &encoded, sizeof encoded);
    return status;
}

static OSErr ppc_ppc_open_sync(uint32 address)
{
    return ppc_toolbox_sync(address, 1);
}

static OSErr ppc_ppc_close_sync(uint32 address)
{
    return ppc_toolbox_sync(address, 9);
}

static OSErr ppc_window_proxy_icon_absent(uint32 window, uint32* icon)
{
    if (!ppc_valid_window(window))
        return -5600;
    if (!icon)
        return paramErr;
    /* WindowsLib's no-proxy path returns -5600 without touching the output. */
    return -5600;
}

static ULONGINT ppc_vm_get_holdable_bytes(void)
{
    M68kReg saved[16];
    memcpy(saved, cpu_state.regs, sizeof saved);
    EM_D0 = 0xfffffffd;
    CALL_EMULATOR(CL(ppc_active_context->os_vectors[0x5c]));
    const ULONGINT result = EM_D0;
    memcpy(cpu_state.regs, saved, sizeof saved);
    return result;
}

static LONGINT ppc_transition_window(uint32 window, uint32 effect, uint32 action, const Rect* origin)
{
    if (!ppc_valid_window(window))
        return -5600;
    if (origin && !ppc_memory(ppc_active_context, US_TO_SYN68K(origin), sizeof *origin, FALSE))
        return paramErr;
    if (effect != 1 || (action != 1 && action != 2))
        return paramErr;
    const ROMlib_carbon_export* zoom = ROMlib_carbon_find_export("ZoomRects", 2);
    if (!zoom || !(zoom->signature & ROMLIB_CARBON_PPC_FRAME))
        return -4;
    Rect* rectangles = (Rect*)NewPtrSysClear(2 * sizeof *rectangles);
    if (!rectangles)
        return memFullErr;
    WindowPtr native_window = SYN68K_TO_US(window);
    if (!WINDOW_VISIBLE(native_window))
        THEPORT_SAVE_EXCURSION(MR(wmgr_port), { WINDCALL(native_window, wCalcRgns, 0); });
    Rect bounds = HxX(WINDOW_STRUCT_REGION(native_window), rgnBBox);
    Rect center;
    SetRect(&center, (CW(bounds.left) + CW(bounds.right)) / 2, (CW(bounds.top) + CW(bounds.bottom)) / 2,
            (CW(bounds.left) + CW(bounds.right)) / 2, (CW(bounds.top) + CW(bounds.bottom)) / 2);
    const Rect small = origin ? *origin : center;
    rectangles[0] = action == 1 ? small : bounds;
    rectangles[1] = action == 1 ? bounds : small;
    ROMlib_carbon_ppc_frame animation = {.words = {US_TO_SYN68K(rectangles), US_TO_SYN68K(rectangles + 1), 6, 0}};
    /* Reuse the actual rectangle animator and Window Manager, retaining all
       update/visibility work. Theme sounds remain optional when no set exists. */
    int handled = ((ROMlib_carbon_frame_entry)zoom->function)(&animation);
    DisposPtr((Ptr)rectangles);
    if (!handled)
        return -4;
    if ((int32)animation.words[0])
        return animation.words[0];
    if (action == 1)
        ShowWindow(native_window);
    else
        HideWindow(native_window);
    return noErr;
}

static LONGINT ppc_theme_sound_unavailable(uint32 kind)
{
    /* No theme sound set is installed by the native Appearance provider.
       kThemeSoundNone requests silence; other requests are unavailable. */
    return kind ? -4 : noErr;
}

static LONGINT ppc_get_window_features(uint32 window, uint32 output)
{
    if (!ppc_valid_window(window))
        return -5600;
    uint32* features = output ? ppc_memory(ppc_active_context, output, 4, TRUE) : NULL;
    if (!features)
        return paramErr;
    *features = 0;
    /* Let the live window definition report its capabilities, including a
       guest WDEF. Legacy WDEFs leave the extended capability mask empty. */
    ROMlib_windcall(SYN68K_TO_US(window), 7, output);
    return noErr;
}

static BOOLEAN ppc_native_window_uncollapsed(uint32 window)
{
    /* Native classic windows have no WindowShade state. CollapseWindow below
       reports that feature as unavailable, so their content remains expanded. */
    (void)window;
    return FALSE;
}

static OSErr ppc_native_collapse_unavailable(uint32 window, BOOLEAN collapse)
{
    (void)collapse;
    return ppc_valid_window(window) ? -4 : -5600;
}

static OSErr ppc_set_window_proxy_icon_absent(uint32 window, uint32 icon)
{
    (void)window;
    /* WindowsLib rejects null icons before looking up the proxy record. */
    return icon ? -5600 : paramErr;
}

PRIVATE BOOLEAN ppc_wait_mouse_moved_without_drag(Point initial)
{
    EventRecord event;

    /* The native fallback cannot start a Drag Manager session. Let the
     application finish the click and leave the button event queued. */
    while (!OSEventAvail(mDownMask | mUpMask, &event) && !(event.modifiers & CWC(btnState)))
        Delay(1, NULL);
    return FALSE;
}

PRIVATE OSErr ppc_display_notification_unavailable(void)
{
    return -4; /* unimpErr */
}

PRIVATE ControlHandle ppc_find_control_under_mouse(Point where, WindowPtr window, INTEGER* part)
{
    ControlHandle control, found = NULL;
    INTEGER hit = 0;
    if (ROMlib_valid_window(window) && WINDOW_VISIBLE(window)) {
        HIDDEN_ControlHandle root;
        boolean_t embedded = ROMlib_get_root_control(window, &root) == noErr;
        /* Appearance controls paint from oldest to newest. Hit-test their
         foreground controls first; background panes must not intercept a
         button drawn over them. Retain classic ordering for older windows. */
        for (control = WINDOW_CONTROL_LIST(window); control; control = CTL_NEXT_CONTROL(control))
            if (ROMlib_control_visible_at(control, where)) {
                found = control;
                if (embedded)
                    break;
            }
        if (found) {
            found = ROMlib_control_hit_target(found);
            hit = TestControl(found, where);
        }
    }
    if (part)
        *part = CW(hit);
    return found;
}

PRIVATE INTEGER ppc_get_sys_direction(void)
{
    return CW(TESysJust);
}

PRIVATE void ppc_set_sys_direction(INTEGER direction)
{
    TESysJust = CW(direction);
}

PRIVATE OSErr ppc_tsm_init(void)
{
    return ROMlib_tsm_registration(TRUE);
}

PRIVATE OSErr ppc_tsm_close(void)
{
    return ROMlib_tsm_registration(FALSE);
}

PRIVATE OSErr ppc_tsm_activate(Handle id)
{
    return ROMlib_tsm_set_active(id, TRUE);
}

PRIVATE OSErr ppc_tsm_deactivate(Handle id)
{
    return ROMlib_tsm_set_active(id, FALSE);
}

PRIVATE BOOLEAN ppc_tsm_event(EventRecord* event)
{
    return FALSE;
}

PRIVATE LONGINT ppc_set_theme_cursor(uint32 cursor)
{
    /* The classic arrow is the only theme cursor supplied by this runtime.
     Do not report success for theme images that we cannot display. */
    if (cursor != 0)
        return -4; /* unimpErr */
    InitCursor();
    return noErr;
}

PRIVATE LONGINT ppc_draw_theme_menu_item(Rect* menu, Rect* item, INTEGER top, INTEGER bottom, uint16 state,
                                         uint16 item_type, ProcPtr draw, uint32 data)
{
    HIDDEN_GrafPtr port;
    RGBColor foreground, background;
    PenState pen;
    RgnHandle saved, clip;
    Rect visible;
    GDHandle device;
    if (!menu || !item || (state != 0 && state != 1 && state != 3))
        return paramErr;
    if ((item_type & 0x30fc) || (item_type & 3) > 1)
        return -4;
    if (!SectRect(menu, item, &visible))
        return noErr;
    if (CW(visible.top) < top)
        visible.top = CW(top);
    if (CW(visible.bottom) > bottom)
        visible.bottom = CW(bottom);
    if (EmptyRect(&visible))
        return noErr;
    saved = NewRgn();
    if (!saved)
        return memFullErr;
    clip = NewRgn();
    if (!clip) {
        DisposeRgn(saved);
        return memFullErr;
    }
    GetPort(&port);
    GetForeColor(&foreground);
    GetBackColor(&background);
    GetPenState(&pen);
    GetClip(saved);
    RectRgn(clip, &visible);
    SectRgn(saved, clip, clip);
    SetClip(clip);
    RGBForeColor(state == 1 ? &ROMlib_white_rgb_color : &ROMlib_black_rgb_color);
    RGBBackColor(state == 1 ? &ROMlib_black_rgb_color : &ROMlib_white_rgb_color);
    EraseRect(item);
    if (draw) {
        device = GetGDevice();
        /* CTOP: void (Rect *, SInt16, Boolean, SInt32), in argument order. */
        CToPascalCall(draw, 0x42a8, item, PIXMAP_PIXEL_SIZE(GD_PMAP(device)), !!(GD_FLAGS(device) & (1 << gdDevType)),
                      data);
    }
    SetPort(HIDDEN_NATIVE(port));
    if (item_type & 1) {
        INTEGER x = CW(item->right) - 6;
        INTEGER y = (CW(item->top) + CW(item->bottom)) / 2;
        INTEGER i;
        PenNormal();
        for (i = 0; i < 4; ++i) {
            MoveTo(x - i, y - i);
            LineTo(x - i, y + i);
        }
    }
    SetClip(saved);
    SetPenState(&pen);
    RGBForeColor(&foreground);
    RGBBackColor(&background);
    DisposeRgn(clip);
    DisposeRgn(saved);
    return noErr;
}

PRIVATE LONGINT ppc_draw_theme_menu_separator(Rect* item)
{
    PenState pen;
    RGBColor foreground;
    INTEGER middle;
    if (!item)
        return paramErr;
    if (EmptyRect(item))
        return noErr;
    GetPenState(&pen);
    GetForeColor(&foreground);
    PenNormal();
    RGBForeColor(&ROMlib_black_rgb_color);
    middle = (CW(item->top) + CW(item->bottom)) / 2;
    MoveTo(CW(item->left), middle);
    LineTo(CW(item->right) - 1, middle);
    SetPenState(&pen);
    RGBForeColor(&foreground);
    return noErr;
}

PRIVATE LONGINT ppc_draw_theme_frame(const Rect* rect, LONGINT state, int outset, BOOLEAN fill_center)
{
    static const RGBColor inactive = {CWC(0x8888), CWC(0x8888), CWC(0x8888)};
    if (!rect || state < 0 || state > 2)
        return paramErr;
    Rect bounds = *rect;
    draw_state_t saved;
    draw_state_save(&saved);
    PenNormal();
    if (fill_center)
        EraseRect(&bounds);
    if (outset)
        InsetRect(&bounds, -outset, -outset);
    RGBForeColor(state ? &ROMlib_black_rgb_color : &inactive);
    FrameRect(&bounds);
    draw_state_restore(&saved);
    return noErr;
}

PRIVATE LONGINT ppc_set_theme_text_color(INTEGER color, INTEGER depth, BOOLEAN color_device)
{
    static const RGBColor inactive = {CWC(0x7777), CWC(0x7777), CWC(0x7777)};
    if (color < 1 || color > 22)
        return -4;
    boolean_t disabled = color == 2 || color == 4 || color == 6 || color == 8 || color == 10 || color == 13 ||
                         color == 16 || color == 19;
    RGBForeColor(disabled && depth > 1 && color_device ? &inactive : &ROMlib_black_rgb_color);
    return noErr;
}

PRIVATE LONGINT ppc_draw_theme_placard(const Rect* rect, LONGINT state)
{
    if (!rect || state < 0 || state > 2)
        return paramErr;
    draw_state_t saved;
    draw_state_save(&saved);
    RGBBackColor(&ROMlib_theme_background_rgb);
    LONGINT result = ppc_draw_theme_frame(rect, state, 0, TRUE);
    Rect bevel = *rect;
    InsetRect(&bevel, 1, 1);
    if (RECT_WIDTH(&bevel) > 1 && RECT_HEIGHT(&bevel) > 1) {
        PenNormal();
        RGBForeColor(&ROMlib_white_rgb_color);
        MoveTo(CW(bevel.left), CW(bevel.bottom) - 1);
        LineTo(CW(bevel.left), CW(bevel.top));
        LineTo(CW(bevel.right) - 1, CW(bevel.top));
        static const RGBColor shadow = {CWC(0x8888), CWC(0x8888), CWC(0x8888)};
        RGBForeColor(&shadow);
        MoveTo(CW(bevel.right) - 1, CW(bevel.top) + 1);
        LineTo(CW(bevel.right) - 1, CW(bevel.bottom) - 1);
        LineTo(CW(bevel.left) + 1, CW(bevel.bottom) - 1);
    }
    draw_state_restore(&saved);
    return result;
}

PRIVATE LONGINT ppc_draw_theme_window_header(const Rect* rect, LONGINT state)
{
    if (!rect || !ROMlib_win_accessible_span(rect, sizeof *rect, FALSE) || state < 0 || state > 2)
        return paramErr;
    if (EmptyRect(rect))
        return noErr;
    draw_state_t saved;
    draw_state_save(&saved);
    PenNormal();
    RGBForeColor(&ROMlib_theme_background_rgb);
    PaintRect(rect);
    RGBForeColor(&ROMlib_white_rgb_color);
    MoveTo(CW(rect->left), CW(rect->top));
    LineTo(CW(rect->right) - 1, CW(rect->top));
    RGBForeColor(state ? &ROMlib_black_rgb_color : &ROMlib_gray_rgb_color);
    MoveTo(CW(rect->left), CW(rect->bottom) - 1);
    LineTo(CW(rect->right) - 1, CW(rect->bottom) - 1);
    draw_state_restore(&saved);
    return noErr;
}

PRIVATE QDErr ppc_new_gworld(HIDDEN_GWorldPtr* world, INTEGER depth, Rect* bounds, CTabHandle colors, GDHandle device,
                             GWorldFlags flags)
{
    return ROMlib_new_gworld(world, depth, bounds, colors, device, flags, TRUE);
}

PRIVATE PixPatHandle ppc_desktop_pattern(void)
{
    return HIDDEN_NATIVE(DeskCPat_H);
}

PRIVATE RgnHandle ppc_gray_region(void)
{
    return HIDDEN_NATIVE(GrayRgn_H);
}

PRIVATE Byte ppc_lm_get_fract_enable(void)
{
    return FractEnable;
}

PRIVATE void ppc_lm_get_hilite_rgb(RGBColor* color)
{
    if (color)
        *color = HiliteRGB;
}

PRIVATE LONGINT ppc_set_theme_background(INTEGER brush, INTEGER depth, BOOLEAN color_device)
{
    switch (brush) {
    case -1:
        RGBBackColor(&ROMlib_black_rgb_color);
        return noErr;
    case -2:
    case 10: /* kThemeBrushListViewBackground */
        RGBBackColor(&ROMlib_white_rgb_color);
        return noErr;
    case 1:
    case 2:
        RGBBackColor(color_device && depth > 1 ? &ROMlib_theme_background_rgb : &ROMlib_white_rgb_color);
        return noErr;
    default:
        return -4; /* unimpErr */
    }
}

PRIVATE LONGINT ppc_set_theme_pen(INTEGER brush, INTEGER depth, BOOLEAN color_device)
{
    switch (brush) {
    case -1:
        RGBForeColor(&ROMlib_black_rgb_color);
        return noErr;
    case -2:
        RGBForeColor(&ROMlib_white_rgb_color);
        return noErr;
    case 1:
    case 2:
        RGBForeColor(color_device && depth > 1 ? &ROMlib_theme_background_rgb : &ROMlib_white_rgb_color);
        return noErr;
    default:
        return -4;
    }
}

PRIVATE LONGINT ppc_draw_theme_edit_frame(const Rect* rect, LONGINT state)
{
    return state == 2 ? paramErr : ppc_draw_theme_frame(rect, state, 1, FALSE);
}

PRIVATE void ppc_draw_control_in_current_port(ControlHandle control)
{
    if (control && STARH(control) && CTL_VIS_X(control))
        CTLCALL(control, drawCntl, 0);
}

PRIVATE LONGINT ppc_draw_theme_focus_rect(const Rect* rect, BOOLEAN focused)
{
    if (!rect)
        return paramErr;
    if (focused)
        return ppc_draw_theme_frame(rect, 1, 3, FALSE);
    Rect bounds = *rect;
    draw_state_t saved;
    draw_state_save(&saved);
    PenNormal();
    RGBColor background;
    GetBackColor(&background);
    RGBForeColor(&background);
    InsetRect(&bounds, -3, -3);
    FrameRect(&bounds);
    draw_state_restore(&saved);
    return noErr;
}

PRIVATE LONGINT ppc_draw_theme_generic_well(const Rect* rect, LONGINT state, BOOLEAN fill_center)
{
    return state == 2 ? paramErr : ppc_draw_theme_frame(rect, state, 1, fill_center);
}

PRIVATE LONGINT ppc_theme_menu_separator_height(INTEGER* height)
{
    HIDDEN_GrafPtr saved;
    FontInfo info;
    if (!height)
        return paramErr;
    /* The classic MDEF allocates one menu-font line for separator items too. */
    GetPort(&saved);
    SetPort(MR(wmgr_port));
    GetFontInfo(&info);
    SetPort(HIDDEN_NATIVE(saved));
    *height = CW(CW(info.ascent) + CW(info.descent) + CW(info.leading));
    return noErr;
}

PRIVATE LONGINT ppc_theme_menu_item_extra(INTEGER type, INTEGER* height, INTEGER* width)
{
    if (!height || !width || (type & 0x30fc))
        return paramErr;
    /* The flat classic menu background has no extra frame or vertical inset.
     Hierarchical arrows occupy the existing command-key column. */
    *height = 0;
    *width = 0;
    return noErr;
}

PRIVATE OSErr ppc_hsetvol(StringPtr name, INTEGER vref, LONGINT directory)
{
    WDPBRec pb;
    memset(&pb, 0, sizeof pb);
    PP_ASSIGN(pb.ioNamePtr, (name));
    pb.ioVRefNum = CW(vref);
    pb.ioWDDirID = CL(directory);
    return PBHSetVol(&pb, FALSE);
}

#define M0(r) (r)
#define M1(r, a) ((r) | ((a) << 3))
#define M2(r, a, b) (M1(r, b) | ((a) << 6))
#define M3(r, a, b, c) (M2(r, b, c) | ((a) << 9))
#define M4(r, a, b, c, d) (M3(r, b, c, d) | ((a) << 12))
#define M5(r, a, b, c, d, e) (M4(r, b, c, d, e) | ((a) << 15))
#define M6(r, a, b, c, d, e, f) (M5(r, b, c, d, e, f) | ((a) << 18))
#define M7(r, a, b, c, d, e, f, g) (M6(r, b, c, d, e, f, g) | ((a) << 21))
#define M8(r, a, b, c, d, e, f, g, h) (M7(r, b, c, d, e, f, g, h) | ((a) << 24))

typedef struct {
    const char* name;
    void* function;
    uint64 magic;
} ppc_native_import;

#define CLASSIC_PRINT_CALL(name, selector, info, result_type)                                      \
    PRIVATE int ppc_##name(ROMlib_carbon_ppc_frame* call_frame)                                    \
    {                                                                                              \
        const uint32_t pr_glue = 0xe00 + 4 * (0xa8fd & 0x3ff);                                     \
        const uint32_t procedure = kStackDispatchedPascalStackBased | (3 << 6) | (info);           \
        uint32_t result;                                                                           \
        if (!ROMlib_carbon_call_toolbox_vector(pr_glue, procedure, selector, call_frame, &result)) \
            return 0;                                                                              \
        call_frame->words[0] = (result_type)result;                                                \
        return 1;                                                                                  \
    }

/* Printing.h places a four-byte selector last on the 68K Pascal stack. */
CLASSIC_PRINT_CALL(PrOpen, 0xc8000000, 0x0, uint32_t)
CLASSIC_PRINT_CALL(PrClose, 0xd0000000, 0x0, uint32_t)
CLASSIC_PRINT_CALL(PrError, 0xba000000, 0x20, int16_t)
CLASSIC_PRINT_CALL(PrSetError, 0xc0000200, 0x200, uint32_t)
CLASSIC_PRINT_CALL(PrintDefault, 0x20040480, 0x300, uint32_t)
CLASSIC_PRINT_CALL(PrValidate, 0x52040498, 0x310, uint8_t)
CLASSIC_PRINT_CALL(PrGeneral, 0x70070480, 0x300, uint32_t)
CLASSIC_PRINT_CALL(PrOpenDoc, 0x04000c00, 0x3f30, uint32_t)
CLASSIC_PRINT_CALL(PrCloseDoc, 0x08000484, 0x300, uint32_t)
CLASSIC_PRINT_CALL(PrOpenPage, 0x10000808, 0xf00, uint32_t)
CLASSIC_PRINT_CALL(PrClosePage, 0x1800040c, 0x300, uint32_t)
CLASSIC_PRINT_CALL(PrStlDialog, 0x2a040484, 0x310, uint8_t)
CLASSIC_PRINT_CALL(PrJobDialog, 0x32040488, 0x310, uint8_t)
CLASSIC_PRINT_CALL(PrStlInit, 0x3c04040c, 0x330, uint32_t)
CLASSIC_PRINT_CALL(PrJobInit, 0x44040410, 0x330, uint32_t)
CLASSIC_PRINT_CALL(PrDlgMain, 0x4a040894, 0xf10, uint8_t)
CLASSIC_PRINT_CALL(PrJobMerge, 0x5804089c, 0xf00, uint32_t)
#undef CLASSIC_PRINT_CALL

PRIVATE const ppc_native_import printing_imports[] = {
    {"PrOpen", ppc_PrOpen, ROMLIB_CARBON_PPC_FRAME},
    {"PrClose", ppc_PrClose, ROMLIB_CARBON_PPC_FRAME},
    {"PrError", ppc_PrError, ROMLIB_CARBON_PPC_FRAME},
    {"PrSetError", ppc_PrSetError, ROMLIB_CARBON_PPC_FRAME},
    {"PrintDefault", ppc_PrintDefault, ROMLIB_CARBON_PPC_FRAME},
    {"PrValidate", ppc_PrValidate, ROMLIB_CARBON_PPC_FRAME},
    {"PrGeneral", ppc_PrGeneral, ROMLIB_CARBON_PPC_FRAME},
    {"PrOpenDoc", ppc_PrOpenDoc, ROMLIB_CARBON_PPC_FRAME},
    {"PrCloseDoc", ppc_PrCloseDoc, ROMLIB_CARBON_PPC_FRAME},
    {"PrOpenPage", ppc_PrOpenPage, ROMLIB_CARBON_PPC_FRAME},
    {"PrClosePage", ppc_PrClosePage, ROMLIB_CARBON_PPC_FRAME},
    {"PrStlDialog", ppc_PrStlDialog, ROMLIB_CARBON_PPC_FRAME},
    {"PrJobDialog", ppc_PrJobDialog, ROMLIB_CARBON_PPC_FRAME},
    {"PrStlInit", ppc_PrStlInit, ROMLIB_CARBON_PPC_FRAME},
    {"PrJobInit", ppc_PrJobInit, ROMLIB_CARBON_PPC_FRAME},
    {"PrDlgMain", ppc_PrDlgMain, ROMLIB_CARBON_PPC_FRAME},
    {"PrJobMerge", ppc_PrJobMerge, ROMLIB_CARBON_PPC_FRAME},
};

PRIVATE uint32 ppc_get_tool_trap(uint16 trap_no)
{
    uint32 d0 = trap_no, result;
    ROMlib_GetTrapAddress_helper(&d0, 0xa746, &result);
    return result;
}

PRIVATE uint32 ppc_get_os_trap(uint16 trap_no)
{
    uint32 d0 = trap_no, result;
    ROMlib_GetTrapAddress_helper(&d0, 0xa346, &result);
    return result;
}

PRIVATE void ppc_set_tool_trap(uint32 address, uint16 trap_no)
{
    tooltraptable[trap_no & 0x3ff] = (void*)address;
    if (ppc_active_context)
        ppc_active_context->tool_vectors[trap_no & 0x3ff] = CL(address);
}

PRIVATE void ppc_set_os_trap(uint32 address, uint16 trap_no)
{
    ostraptable[trap_no & 0xff] = (void*)address;
    if (ppc_active_context)
        ppc_active_context->os_vectors[trap_no & 0xff] = CL(address);
}

PRIVATE uint32 ppc_n_get_trap(uint16 trap_no, uint16 type)
{
    return type ? ppc_get_tool_trap(trap_no) : ppc_get_os_trap(trap_no);
}

PRIVATE void ppc_n_set_trap(uint32 address, uint16 trap_no, uint16 type)
{
    if (type)
        ppc_set_tool_trap(address, trap_no);
    else
        ppc_set_os_trap(address, trap_no);
}

static int ppc_script_char_byte(ROMlib_carbon_ppc_frame* call)
{
    /* Classic ScriptUtil selector 0x10 takes two arguments. */
    uint32 result;
    if (!ROMlib_carbon_call_toolbox_vector(0x10d4, 0xbee, 0x82060010, call, &result))
        return 0;
    call->words[0] = (int16)result;
    return 1;
}

static OSErr ppc_osdispatch_two_words(uint32 first, uint32 second, uint32 selector)
{
    const ROMlib_carbon_ppc_frame args = {.words = {first, second}};
    uint32 result;
    /* Use the live OSDispatch vector, including guest patches. ROMAW's
       unpatched dispatcher explicitly reports registration as unavailable,
       allowing Finder to retain its volume polling fallback. */
    if (!ROMlib_carbon_call_toolbox_vector(0x103c, 0xfae, selector, &args, &result))
        return paramErr;
    return (int16)result;
}

static OSErr ppc_request_volume_notification(uint32 procedure, uint32 refcon)
{
    return ppc_osdispatch_two_words(procedure, refcon, 0x42);
}

static OSErr ppc_decline_volume_notification(uint32 procedure, uint32 refcon)
{
    return ppc_osdispatch_two_words(procedure, refcon, 0x43);
}

static OSErr ppc_sys_zone_float_sizes(uint32 high, uint32 low)
{
    return ppc_osdispatch_two_words(high, low, 0x47);
}

static void ppc_set_icon_device(uint32 device)
{
    const ROMlib_carbon_ppc_frame args = {.words = {device}};
    uint32 ignored;
    ROMlib_carbon_call_toolbox_vector(0x1d24, 0x388, 0x218, &args, &ignored);
}

static void ppc_get_icon_device(uint32 output)
{
    const ROMlib_carbon_ppc_frame args = {.words = {output}};
    uint32 ignored;
    ROMlib_carbon_call_toolbox_vector(0x1d24, 0x388, 0x220, &args, &ignored);
}

PRIVATE const ppc_native_import native_imports[] = {
    {"AEObjectInit", ROMlib_ae_object_init, M0(2)},
    {"AESetObjectCallbacks", C_AESetObjectCallbacks, PTOC_AESetObjectCallbacks},
    {"AEInstallObjectAccessor", C_AEInstallObjectAccessor, PTOC_AEInstallObjectAccessor},
    {"AEGetObjectAccessor", C_AEGetObjectAccessor, PTOC_AEGetObjectAccessor},
    {"AERemoveObjectAccessor", C_AERemoveObjectAccessor, PTOC_AERemoveObjectAccessor},
    {"AECallObjectAccessor", C_AECallObjectAccessor, PTOC_AECallObjectAccessor},
    {"AEResolve", C_AEREesolve, PTOC_AEREesolve},
    {"AEDisposeToken", C_AEDisposeToken, PTOC_AEDisposeToken},
    {"PPCOpenSync", ppc_ppc_open_sync, M1(2, 4)},
    {"PPCCloseSync", ppc_ppc_close_sync, M1(2, 4)},
    {"SetIconDevice", ppc_set_icon_device, M1(0, 4)},
    {"GetIconDevice", ppc_get_icon_device, M1(0, 4)},
    {"NewDesktopLayerOwner", ROMlib_new_desktop_layer_owner, M1(2, 5)},
    {"RequestVolumeNotification", ppc_request_volume_notification, M2(2, 4, 4)},
    {"DeclineVolumeNotification", ppc_decline_volume_notification, M2(2, 4, 4)},
    {"SysZoneFloatSizes", ppc_sys_zone_float_sizes, M2(2, 4, 4)},
    {"VM_GetHoldableBytes", ppc_vm_get_holdable_bytes, M0(4)},
    {"CharByte", ppc_script_char_byte, ROMLIB_CARBON_PPC_FRAME},
    /* Saved TextEdit dispatch code uses this InterfaceLib entry as its base. */
    {"TEFeatureFlag", C_TEFeatureFlag, PTOC_TEFeatureFlag},
    {"WaitNextEvent", ppc_wait_next_event, M4(1, 2, 5, 4, 5)},
    {"GetNextEvent", ppc_get_next_event, M2(1, 2, 5)},
    {"EventAvail", ppc_event_avail, M2(1, 2, 5)},
    {"GetOSEvent", GetOSEvent, M2(1, 2, 5)},
    {"OSEventAvail", OSEventAvail, M2(1, 2, 5)},
    {"PostEvent", ppc_post_event, M2(2, 2, 4)},
    {"LMGetWindowList", ppc_window_list, M0(5)},
    {"GetEvQHdr", GetEvQHdr, M0(5)},
    {"LMGetEventQueue", GetEvQHdr, M0(5)},
    {"InitCRM", InitCRM, M0(2)},
    {"CRMGetCRMVersion", CRMGetCRMVersion, M0(2)},
    {"CRMGetHeader", CRMGetHeader, M0(5)},
    {"CRMInstall", CRMInstall, M1(0, 5)},
    {"CRMRemove", CRMRemove, M1(2, 5)},
    {"CRMSearch", CRMSearch, M1(5, 5)},
    {"GetVCBQHdr", GetVCBQHdr, M0(5)},
    {"GetDrvQHdr", GetDrvQHdr, M0(5)},
    {"NGetTrapAddress", ppc_n_get_trap, M2(4, 2, 2)},
    {"NSetTrapAddress", ppc_n_set_trap, M3(0, 4, 2, 2)},
    {"SetEventMask", SetEventMask, M1(0, 2)},
    {"FlushEvents", FlushEvents, M2(0, 2, 2)},
    {"SetGrowZone", SetGrowZone, M1(0, 5)},
    {"MaxApplZone", MaxApplZone, M0(0)},
    {"MoreMasters", MoreMasters, M0(0)},
    {"SetDialogDefaultItem", C_SetDialogDefaultItem, M2(2, 5, 2)},
    {"SetDialogCancelItem", C_SetDialogCancelItem, M2(2, 5, 2)},
    {"StdFilterProc", C_ROMlib_myfilt, PTOC_ROMlib_myfilt},
    {"AppendDITL", AppendDITL, M3(0, 5, 5, 2)},
    {"CountDITL", CountDITL, M1(2, 5)},
    {"ShortenDITL", ShortenDITL, M2(0, 5, 2)},
    {"NewAlias", C_NewAlias, PTOC_NewAlias},
    {"ResolveAlias", C_ResolveAlias, PTOC_ResolveAlias},
    {"MatchAlias", C_MatchAlias, PTOC_MatchAlias},
    {"ResolveAliasFile", C_ResolveAliasFile, PTOC_ResolveAliasFile},
    {"ResolveAliasFileWithMountFlags", ROMlib_resolve_alias_file_with_mount_flags, M5(2, 5, 1, 5, 5, 4)},
    {"IsAliasFile", ROMlib_is_alias_file, M3(2, 5, 5, 5)},
    {"NewAliasMinimal", C_NewAliasMinimal, PTOC_NewAliasMinimal},
    {"NewAliasMinimalFromFullPath", C_NewAliasMinimalFromFullPath, PTOC_NewAliasMinimalFromFullPath},
    {"StandardAlert", ROMlib_standard_alert, M5(2, 2, 5, 5, 5, 5)},
    {"NewIconSuite", C_NewIconSuite, PTOC_NewIconSuite},
    {"AddIconToSuite", C_AddIconToSuite, PTOC_AddIconToSuite},
    {"GetIconFromSuite", C_GetIconFromSuite, PTOC_GetIconFromSuite},
    {"DisposeIconSuite", C_DisposeIconSuite, PTOC_DisposeIconSuite},
    {"GetIconSuite", C_GetIconSuite, PTOC_GetIconSuite},
    {"PlotIconSuite", C_PlotIconSuite, PTOC_PlotIconSuite},
    {"PlotIconID", C_PlotIconID, PTOC_PlotIconID},
    {"PlotIconMethod", C_PlotIconMethod, PTOC_PlotIconMethod},
    {"GetLabel", C_GetLabel, PTOC_GetLabel},
    {"ForEachIconDo", C_ForEachIconDo, PTOC_ForEachIconDo},
    {"GetSuiteLabel", C_GetSuiteLabel, PTOC_GetSuiteLabel},
    {"SetSuiteLabel", C_SetSuiteLabel, PTOC_SetSuiteLabel},
    {"IconSuiteToRgn", C_IconSuiteToRgn, PTOC_IconSuiteToRgn},
    {"IconIDToRgn", C_IconIDToRgn, PTOC_IconIDToRgn},
    {"IconMethodToRgn", C_IconMethodToRgn, PTOC_IconMethodToRgn},
    {"PtInIconSuite", C_PtInIconSuite, PTOC_PtInIconSuite},
    {"PtInIconID", C_PtInIconID, PTOC_PtInIconID},
    {"PtInIconMethod", C_PtInIconMethod, PTOC_PtInIconMethod},
    {"RectInIconSuite", C_RectInIconSuite, PTOC_RectInIconSuite},
    {"RectInIconID", C_RectInIconID, PTOC_RectInIconID},
    {"RectInIconMethod", C_RectInIconMethod, PTOC_RectInIconMethod},
    {"MakeIconCache", C_MakeIconCache, PTOC_MakeIconCache},
    {"LoadIconCache", C_LoadIconCache, PTOC_LoadIconCache},
    {"GetIconCacheData", C_GetIconCacheData, PTOC_GetIconCacheData},
    {"SetIconCacheData", C_SetIconCacheData, PTOC_SetIconCacheData},
    {"GetIconCacheProc", C_GetIconCacheProc, PTOC_GetIconCacheProc},
    {"SetIconCacheProc", C_SetIconCacheProc, PTOC_SetIconCacheProc},
    {"TENumStyles", C_TENumStyles, PTOC_TENumStyles},
    {"TESetStyle", C_TESetStyle, PTOC_TESetStyle},
    {"TEGetStyle", C_TEGetStyle, PTOC_TEGetStyle},
    {"GetStylScrap", C_GetStylScrap, PTOC_GetStylScrap},
    {"SetStylScrap", C_SetStylScrap, PTOC_SetStylScrap},
    {"GetMenuHandle", C_GetMHandle, M1(5, 2)},
    {"GetToolTrapAddress", ppc_get_tool_trap, M1(4, 2)},
    {"GetOSTrapAddress", ppc_get_os_trap, M1(4, 2)},
    {"SetToolTrapAddress", ppc_set_tool_trap, M2(0, 4, 2)},
    {"SetOSTrapAddress", ppc_set_os_trap, M2(0, 4, 2)},
    {"NewHandle", ppc_new_handle, M1(5, 4)},
    {"NewHandleClear", ppc_new_handle_clear, M1(5, 4)},
    {"NewPtr", ppc_new_ptr, M1(5, 4)},
    {"NewPtrClear", ppc_new_ptr_clear, M1(5, 4)},
    {"NewHandleSys", ppc_new_handle_sys, M1(5, 4)},
    {"NewHandleSysClear", ppc_new_handle_sys_clear, M1(5, 4)},
    {"NewPtrSys", ppc_new_ptr_sys, M1(5, 4)},
    {"NewPtrSysClear", ppc_new_ptr_sys_clear, M1(5, 4)},
    {"DisposeHandle", DisposHandle, M1(0, 5)},
    {"DisposePtr", DisposPtr, M1(0, 5)},
    {"SetHandleSize", SetHandleSize, M2(0, 5, 4)},
    {"GetHandleSize", GetHandleSize, M1(4, 5)},
    {"InlineGetHandleSize", GetHandleSize, M1(4, 5)},
    {"InlineGetPtrSize", GetPtrSize, M1(4, 5)},
    {"HandleZone", HandleZone, M1(5, 5)},
    {"IUMagString", C_IUMagString, PTOC_IUMagString},
    {"IUMagPString", C_IUMagPString, PTOC_IUMagPString},
    {"IUMagIDPString", C_IUMagIDPString, PTOC_IUMagIDPString},
    {"CharacterType", C_CharacterType, PTOC_CharacterType},
    {"FindWord", ppc_find_word, PTOC_FindWord},
    {"UpperString", UprString, M2(0, 5, 1)},
    {"UpperText", ppc_upper_text, M2(0, 5, 2)},
    {"LowerText", ppc_lower_text, M2(0, 5, 2)},
    {"FillParseTable", C_FillParseTable, PTOC_FillParseTable},
    {"FontScript", C_FontScript, PTOC_FontScript},
    {"InitTSMAwareApplication", ppc_tsm_init, M0(2)},
    {"CloseTSMAwareApplication", ppc_tsm_close, M0(2)},
    {"NewTSMDocument", ROMlib_tsm_new_document, M4(2, 2, 5, 5, 4)},
    {"DeleteTSMDocument", ROMlib_tsm_delete_document, M1(2, 5)},
    {"ActivateTSMDocument", ppc_tsm_activate, M1(2, 5)},
    {"DeactivateTSMDocument", ppc_tsm_deactivate, M1(2, 5)},
    {"FixTSMDocument", ROMlib_tsm_fix_document, M1(2, 5)},
    {"TSMGetActiveDocument", ROMlib_tsm_active_document, M0(5)},
    {"UseInputWindow", ROMlib_tsm_set_input_window, M2(2, 5, 1)},
    {"IsTSMDocumentUsingInputWindow", ROMlib_tsm_document_input_window, M1(1, 5)},
    {"TSMEvent", ppc_tsm_event, M1(1, 5)},
    {"GetSysDirection", ppc_get_sys_direction, M0(2)},
    {"SetSysDirection", ppc_set_sys_direction, M1(0, 2)},
    {"DrawJustified", C_DrawJustified, PTOC_DrawJustified},
    {"VisibleLength", C_VisibleLength, PTOC_VisibleLength},
    {"Pixel2Char", C_Pixel2Char, PTOC_Pixel2Char},
    {"Char2Pixel", C_Char2Pixel, PTOC_Char2Pixel},
    {"PixelToChar", C_PixelToChar, PTOC_PixelToChar},
    {"CharToPixel", C_CharToPixel, PTOC_CharToPixel},
    {"PortionLine", C_PortionLine, PTOC_PortionLine},
    {"StyledLineBreak", C_StyledLineBreak, PTOC_StyledLineBreak},
    {"RealScript", ppc_real_script, M0(2)},
    {"Font2RealScript", C_Font2Script, M1(2, 2)},
    {"LowercaseText", C_LowercaseText, M3(0, 5, 2, 2)},
    {"CharacterByteType", C_CharacterByteType, M3(2, 5, 2, 2)},
    {"KeyTranslate", ppc_key_translate, M3(4, 5, 2, 5)},
    {"HLock", HLock, M1(0, 5)},
    {"HLockHi", ppc_lock_high, M1(0, 5)},
    {"IsMenuBarVisible", ppc_menu_bar_visible, M0(1)},
    {"GetMBarHeight", ppc_menu_bar_height, M0(2)},
    {"LMGetDeskCPat", ppc_desktop_pattern, M0(5)},
    {"GetGrayRgn", ppc_gray_region, M0(5)},
    {"HMGetHelpMenuHandle", C_HMGetHelpMenuHandle, M1(2, 5)},
    {"HMGetBalloons", C_HMGetBalloons, M0(1)},
    {"HMIsBalloon", C_HMIsBalloon, M0(1)},
    {"HMRemoveBalloon", C_HMRemoveBalloon, M0(2)},
    {"HideMenuBar", ROMlib_menu_hide_bar, M0(0)},
    {"ShowMenuBar", ROMlib_menu_show_bar, M0(0)},
    {"GetMenuItemIconHandle", ROMlib_get_menu_item_icon, M4(2, 5, 2, 5, 5)},
    {"SetMenuItemIconHandle", ROMlib_set_menu_item_icon, M4(2, 5, 2, 1, 5)},
    {"GetMenuItemModifiers", ROMlib_get_menu_item_modifiers, M3(4, 5, 2, 5)},
    {"SetMenuItemModifiers", ROMlib_set_menu_item_modifiers, M3(4, 5, 2, 1)},
    {"GetMenuItemKeyGlyph", ROMlib_get_menu_item_key_glyph, M3(4, 5, 2, 5)},
    {"SetMenuItemKeyGlyph", ROMlib_set_menu_item_key_glyph, M3(4, 5, 2, 2)},
    {"SetMenuItemRefCon", ROMlib_set_menu_item_refcon, M3(2, 5, 2, 4)},
    {"GetMenuItemRefCon", ROMlib_get_menu_item_refcon, M3(2, 5, 2, 5)},
    {"IsSystemMenu", ppc_is_system_menu, M2(2, 2, 5)},
    {"MenuEvent", ROMlib_menu_event, M1(4, 5)},
    {"GetMenuItemFontID", ROMlib_get_menu_item_font, M3(2, 5, 2, 5)},
    {"SetMenuItemFontID", ROMlib_set_menu_item_font, M3(2, 5, 2, 2)},
    {"GetMenuItemCommandID", ROMlib_get_menu_item_command, M3(4, 5, 2, 5)},
    {"GetMenuItemTextEncoding", ROMlib_get_menu_item_encoding, M3(2, 5, 2, 5)},
    {"SetMenuItemTextEncoding", ROMlib_set_menu_item_encoding, M3(2, 5, 2, 4)},
    {"FMGetFontFamilyFromName", ppc_font_family_from_name, M1(2, 5)},
    {"HMSetMenuResID", C_HMSetMenuResID, M2(2, 2, 2)},
    {"GetAppFont", GetAppFont, M0(2)},
    {"GetSysFont", GetSysFont, M0(2)},
    {"SetThemeCursor", ppc_set_theme_cursor, M1(4, 4)},
    {"GetKeyboardFocus", ppc_get_keyboard_focus, M2(2, 5, 5)},
    {"SetKeyboardFocus", ppc_set_keyboard_focus, M3(2, 5, 5, 2)},
    {"ClearKeyboardFocus", ppc_clear_keyboard_focus, M1(2, 5)},
    {"SetThemeWindowBackground", ppc_set_theme_window_background, M3(4, 5, 2, 1)},
    {"GetWindowRegion", ppc_get_window_region, M3(2, 5, 2, 5)},
    {"GetDialogItemAsControl", ppc_get_dialog_item_as_control, M3(4, 5, 2, 5)},
    {"SetControlFontStyle", ROMlib_set_control_font_style, M2(4, 5, 5)},
    {"SetControlData", ppc_set_control_data, M5(4, 5, 2, 4, 4, 5)},
    {"GetControlFeatures", ROMlib_get_control_features, M2(2, 5, 5)},
    {"HandleControlClick", ppc_handle_control_click, M4(2, 5, 3, 2, 5)},
    {"SetImageWellContentInfo", ppc_set_image_well_content, M2(2, 5, 5)},
    {"GetImageWellContentInfo", ppc_get_image_well_content, M2(2, 5, 5)},
    {"SetBevelButtonContentInfo", ppc_set_bevel_content, M2(2, 5, 5)},
    {"GetBevelButtonContentInfo", ppc_get_bevel_content, M2(2, 5, 5)},
    {"SetBevelButtonMenuValue", ROMlib_set_bevel_menu_value, M2(2, 5, 2)},
    {"GetBevelButtonMenuValue", ppc_get_bevel_menu_value, M2(2, 5, 5)},
    {"SetBevelButtonGraphicAlignment", ROMlib_set_bevel_graphic_alignment, M4(2, 5, 2, 2, 2)},
    {"SetBevelButtonTextAlignment", ROMlib_set_bevel_text_alignment, M3(2, 5, 2, 2)},
    {"SetBevelButtonTextPlacement", ROMlib_set_bevel_text_placement, M2(2, 5, 2)},
    {"GetControlData", ppc_get_control_data, M6(4, 5, 2, 4, 4, 5, 5)},
    {"CreateRootControl", ROMlib_create_root_control, M2(2, 5, 5)},
    {"GetRootControl", ROMlib_get_root_control, M2(2, 5, 5)},
    {"EmbedControl", ROMlib_embed_control, M2(2, 5, 5)},
    {"AutoEmbedControl", ROMlib_auto_embed_control, M2(2, 5, 5)},
    {"IsControlVisible", ppc_is_control_visible, M1(1, 5)},
    {"IsControlActive", ppc_is_control_active, M1(1, 5)},
    {"ActivateControl", ppc_activate_control, M1(2, 5)},
    {"DeactivateControl", ppc_deactivate_control, M1(2, 5)},
    {"SetControlVisibility", ppc_set_control_visibility, M3(2, 5, 1, 1)},
    {"SetUpControlBackground", ppc_set_up_control_background, M3(2, 5, 2, 1)},
    {"FindControlUnderMouse", ppc_find_control_under_mouse, M3(5, 3, 5, 5)},
    {"DrawThemeMenuItem", ppc_draw_theme_menu_item, M8(4, 5, 5, 2, 2, 2, 2, 5, 4)},
    {"DrawThemeMenuSeparator", ppc_draw_theme_menu_separator, M1(4, 5)},
    {"DrawThemePlacard", ppc_draw_theme_placard, M2(4, 5, 4)},
    {"DrawThemeWindowHeader", ppc_draw_theme_window_header, M2(4, 5, 4)},
    {"DrawThemeWindowListViewHeader", ppc_draw_theme_placard, M2(4, 5, 4)},
    {"SetThemeTextColor", ppc_set_theme_text_color, M3(4, 2, 2, 1)},
    {"DrawThemeEditTextFrame", ppc_draw_theme_edit_frame, M2(4, 5, 4)},
    {"DrawThemeListBoxFrame", ppc_draw_theme_edit_frame, M2(4, 5, 4)},
    {"DrawControlInCurrentPort", ppc_draw_control_in_current_port, M1(0, 5)},
    {"DrawThemeFocusRect", ppc_draw_theme_focus_rect, M2(4, 5, 1)},
    {"DrawThemeModelessDialogFrame", ppc_draw_theme_edit_frame, M2(4, 5, 4)},
    {"LMGetHiliteRGB", ppc_lm_get_hilite_rgb, M1(0, 5)},
    {"LMGetFractEnable", ppc_lm_get_fract_enable, M0(1)},
    {"SetPreserveGlyph", C_SetPreserveGlyph, PTOC_SetPreserveGlyph},
    {"GetPreserveGlyph", C_GetPreserveGlyph, PTOC_GetPreserveGlyph},
    {"SetThemeBackground", ppc_set_theme_background, M3(4, 2, 2, 1)},
    {"SetThemePen", ppc_set_theme_pen, M3(4, 2, 2, 1)},
    {"DrawThemeGenericWell", ppc_draw_theme_generic_well, M3(4, 5, 4, 1)},
    {"GetThemeMenuSeparatorHeight", ppc_theme_menu_separator_height, M1(4, 5)},
    {"GetThemeMenuItemExtra", ppc_theme_menu_item_extra, M3(4, 2, 5, 5)},
    {"HUnlock", HUnlock, M1(0, 5)},
    {"HNoPurge", HNoPurge, M1(0, 5)},
    {"HPurge", HPurge, M1(0, 5)},
    {"HGetState", HGetState, M1(1, 5)},
    {"HSetState", HSetState, M2(0, 5, 1)},
    {"RecoverHandle", ppc_recover_handle, M1(5, 5)},
    {"FreeMem", ppc_free_mem, M0(4)},
    {"PurgeSpace", ppc_purge_space, M2(0, 5, 5)},
    {"MemError", MemError, M0(2)},
    {"FSOpen", ppc_fsopen, M3(2, 5, 2, 5)},
    {"FSClose", FSClose, M1(2, 2)},
    {"FSRead", ppc_fsread, M3(2, 2, 5, 5)},
    {"FSWrite", ppc_fswrite, M3(2, 2, 5, 5)},
    {"GetEOF", ppc_get_eof, M2(2, 2, 5)},
    {"SetEOF", SetEOF, M2(2, 2, 4)},
    {"GetFPos", ppc_get_fpos, M2(2, 2, 5)},
    {"SetFPos", SetFPos, M3(2, 2, 2, 4)},
    {"FindFolder", C_FindFolder, PTOC_FindFolder},
    {"GetResource", C_GetResource, M2(5, 4, 2)},
    {"GetIndString", GetIndString, M3(0, 5, 2, 2)},
    {"GetIndPattern", GetIndPattern, M3(0, 5, 2, 2)},
    {"NumToString", NumToString, M2(0, 4, 5)},
    {"StringToNum", StringToNum, M2(0, 5, 5)},
    {"BlockMove", BlockMove, M3(0, 5, 5, 4)},
    {"BlockMoveData", BlockMove, M3(0, 5, 5, 4)},
    {"HandToHand", ppc_hand_to_hand, M1(2, 4)},
    {"PtrToHand", ppc_ptr_to_hand, M3(2, 5, 4, 4)},
    {"PtrAndHand", PtrAndHand, M3(2, 5, 5, 4)},
    {"HandAndHand", HandAndHand, M2(2, 5, 5)},
    {"MoveHHi", MoveHHi, M1(0, 5)},
    {"GetDateTime", GetDateTime, M1(0, 5)},
    {"SysEnvirons", ppc_sys_environs, M2(4, 2, 4)},
    {"Gestalt", ppc_gestalt, M2(2, 4, 5)},
    {"NewGestalt", NewGestalt, M2(2, 4, 5)},
    {"ReplaceGestalt", ppc_replace_gestalt, M3(2, 4, 5, 5)},
    {"GetDblTime", GetDblTime, M0(4)},
    {"GetCaretTime", GetCaretTime, M0(4)},
    {"GetPort", C_GetPort, PTOC_GetPort},
    {"NewGWorld", ppc_new_gworld, M6(2, 5, 2, 5, 5, 5, 4)},
    {"SetGWorld", C_SetGWorld, PTOC_SetGWorld},
    {"GetGWorld", C_GetGWorld, PTOC_GetGWorld},
    {"DisposeGWorld", C_DisposeGWorld, PTOC_DisposeGWorld},
    {"DisposeGWorld", C_DisposeGWorld, M1(0, 5)},
    {"GetGWorldPixMap", C_GetGWorldPixMap, M1(5, 5)},
    {"GetPixBaseAddr", C_GetPixBaseAddr, M1(5, 5)},
    {"CopyDeepMask", C_IMVI_CopyDeepMask, PTOC_IMVI_CopyDeepMask},
    {"RGB2HSL", C_RGB2HSL, PTOC_RGB2HSL},
    {"HSV2RGB", C_HSV2RGB, PTOC_HSV2RGB},
    {"RGB2HSV", C_RGB2HSV, PTOC_RGB2HSV},
    {"LockPixels", C_LockPixels, M1(1, 5)},
    {"AllowPurgePixels", C_AllowPurgePixels, PTOC_AllowPurgePixels},
    {"NoPurgePixels", C_NoPurgePixels, PTOC_NoPurgePixels},
    {"UnlockPixels", C_UnlockPixels, M1(0, 5)},
    {"LMGetIntlSpec", ppc_intl_spec, M0(4)},
    {"LMSetIntlSpec", ppc_set_intl_spec, M1(0, 4)},
    {"TempInsertROMMap", ppc_temp_insert_rom_map, M1(0, 1)},
    {"PBReadSync", ppc_PBReadSync, M1(2, 5)},
    {"PBReadAsync", ppc_PBReadAsync, M1(2, 5)},
    {"PBWriteSync", ppc_PBWriteSync, M1(2, 5)},
    {"PBWriteAsync", ppc_PBWriteAsync, M1(2, 5)},
    {"PBCloseSync", ppc_PBCloseSync, M1(2, 5)},
    {"PBCloseAsync", ppc_PBCloseAsync, M1(2, 5)},
    {"PBGetEOFSync", ppc_PBGetEOFSync, M1(2, 5)},
    {"PBGetEOFAsync", ppc_PBGetEOFAsync, M1(2, 5)},
    {"PBSetEOFSync", ppc_PBSetEOFSync, M1(2, 5)},
    {"PBSetEOFAsync", ppc_PBSetEOFAsync, M1(2, 5)},
    {"PBSetFPosSync", ppc_PBSetFPosSync, M1(2, 5)},
    {"PBSetFPosAsync", ppc_PBSetFPosAsync, M1(2, 5)},
    {"PBGetCatInfoSync", ppc_PBGetCatInfoSync, M1(2, 5)},
    {"PBGetCatInfoAsync", ppc_PBGetCatInfoAsync, M1(2, 5)},
    {"PBSetCatInfoSync", ppc_PBSetCatInfoSync, M1(2, 5)},
    {"PBSetCatInfoAsync", ppc_PBSetCatInfoAsync, M1(2, 5)},
    {"PBGetFCBInfoSync", ppc_PBGetFCBInfoSync, M1(2, 5)},
    {"PBGetFCBInfoAsync", ppc_PBGetFCBInfoAsync, M1(2, 5)},
    {"PBHGetVolParmsSync", ppc_PBHGetVolParmsSync, M1(2, 5)},
    {"PBDTGetPath", PBDTGetPath, M1(2, 5)},
    {"PBDTOpenInform", PBDTOpenInform, M1(2, 5)},
    {"PBDTGetIconSync", ppc_PBDTGetIconSync, M1(2, 5)},
    {"PBDTGetAPPLSync", ppc_PBDTGetAPPLSync, M1(2, 5)},
    {"PBDTGetAPPLAsync", ppc_PBDTGetAPPLAsync, M1(2, 5)},
    {"PBDTGetCommentSync", ppc_PBDTGetCommentSync, M1(2, 5)},
    {"PBDTGetCommentAsync", ppc_PBDTGetCommentAsync, M1(2, 5)},
    {"PBHGetVInfoSync", ppc_PBHGetVInfoSync, M1(2, 5)},
    {"PBHGetVInfoAsync", ppc_PBHGetVInfoAsync, M1(2, 5)},
    {"PBHCreateSync", ppc_PBHCreateSync, M1(2, 5)},
    {"PBHCreateAsync", ppc_PBHCreateAsync, M1(2, 5)},
    {"PBHDeleteSync", ppc_PBHDeleteSync, M1(2, 5)},
    {"PBHDeleteAsync", ppc_PBHDeleteAsync, M1(2, 5)},
    {"PBHGetFInfoSync", ppc_PBHGetFInfoSync, M1(2, 5)},
    {"PBHSetFInfoSync", ppc_PBHSetFInfoSync, M1(2, 5)},
    {"PBHOpenSync", ppc_PBHOpenSync, M1(2, 5)},
    {"PBHOpenRFSync", ppc_PBHOpenRFSync, M1(2, 5)},
    {"PBHOpenDenySync", ppc_PBHOpenDenySync, M1(2, 5)},
    {"LaunchApplication", ppc_launch_application, M1(2, 5)},
    {"PBGetWDInfoSync", ppc_PBGetWDInfoSync, M1(2, 5)},
    {"PBResolveFileIDRefSync", ppc_PBResolveFileIDRefSync, M1(2, 5)},
    {"PBCreateFileIDRefSync", ppc_PBCreateFileIDRefSync, M1(2, 5)},
    {"PBHOpenDFSync", ppc_PBHOpenDFSync, M1(2, 5)},
    {"FSpOpenResFile", C_FSpOpenResFile, PTOC_FSpOpenResFile},
    {"FSpCreateResFile", C_FSpCreateResFile, PTOC_FSpCreateResFile},
    {"StandardGetFile", C_StandardGetFile, PTOC_StandardGetFile},
    {"CustomGetFile", C_CustomGetFile,
     ((uint64)5 << 33) | ((uint64)2 << 30) | ((uint64)5 << 27) | M8(0, 5, 2, 3, 5, 5, 5, 5, 5)},
    {"StandardPutFile", C_StandardPutFile, PTOC_StandardPutFile},
    {"FSpOpenDF", C_FSpOpenDF, PTOC_FSpOpenDF},
    {"FSpOpenRF", C_FSpOpenRF, PTOC_FSpOpenRF},
    {"FSpCreate", C_FSpCreate, PTOC_FSpCreate},
    {"FSpDirCreate", C_FSpDirCreate, PTOC_FSpDirCreate},
    {"FSpDelete", C_FSpDelete, PTOC_FSpDelete},
    {"FSpGetFInfo", C_FSpGetFInfo, PTOC_FSpGetFInfo},
    {"GetResourceSizeOnDisk", C_SizeResource, PTOC_SizeResource},
    {"ApplicationZone", ppc_application_zone, M0(5)},
    {"InitZone", ppc_init_zone, M4(0, 5, 2, 5, 5)},
    {"DMGetFirstScreenDevice", ppc_dm_first_screen_device, M1(5, 1)},
    {"DMGetDisplayIDByGDevice", ppc_dm_get_display_id, M3(2, 5, 5, 1)},
    {"LMGetDeskPattern", ppc_lm_get_desk_pattern, M1(0, 5)},
    {"LMSetDeskPattern", ppc_lm_set_desk_pattern, M1(0, 5)},
    {"LMGetDragPattern", ppc_lm_get_drag_pattern, M1(0, 5)},
    {"LMSetDragPattern", ppc_lm_set_drag_pattern, M1(0, 5)},
    {"LMGetTopMapHndl", ppc_top_resource_map, M0(5)},
    {"LMGetDAStrings", ppc_lm_get_da_strings, M1(5, 2)},
    {"LMSetDAStrings", ppc_lm_set_da_strings, M2(0, 5, 2)},
    {"LMGetCurApRefNum", ppc_cur_ap_ref, M0(2)},
    {"LMGetCurApName", ppc_cur_ap_name, M0(5)},
    {"LMGetCurDirStore", ppc_cur_dir_store, M0(4)},
    {"LMSetCurDirStore", ppc_set_cur_dir_store, M1(0, 4)},
    {"LMGetMBarHook", ppc_lm_get_mbar_hook, M0(5)},
    {"LMSetMBarHook", ppc_lm_set_mbar_hook, M1(0, 5)},
    {"LMGetPaintWhite", ppc_lm_get_paint_white, M0(2)},
    {"LMSetPaintWhite", ppc_lm_set_paint_white, M1(0, 2)},
    {"LMGetSdVolume", ppc_lm_get_sd_volume, M0(1)},
    {"memcpy", memcpy, M3(5, 5, 5, 4)},
    {"ldiv", ppc_long_divide, M3(0, 4, 4, 4)},
    {"memmove", memmove, M3(5, 5, 5, 4)},
    {"memset", memset, M3(5, 5, 4, 4)},
    {"memcmp", memcmp, M3(4, 5, 5, 4)},
    {"strlen", strlen, M1(4, 5)},
    {"strcmp", strcmp, M2(4, 5, 5)},
    {"strncmp", strncmp, M3(4, 4, 4, 4)},
    {"strcat", strcat, M2(5, 5, 5)},
    {"strncat", strncat, M3(5, 5, 5, 4)},
    {"GetPtrSize", GetPtrSize, M1(4, 5)},
    {"SetPtrSize", SetPtrSize, M2(0, 5, 4)},
    {"MaxMem", ppc_max_mem, M1(4, 5)},
    {"CompactMem", ppc_compact_mem, M1(4, 4)},
    {"PtrZone", PtrZone, M1(5, 5)},
    {"GetZone", GetZone, M0(5)},
    {"SetZone", SetZone, M1(0, 5)},
    {"SystemZone", SystemZone, M0(5)},
    {"GetCurrentProcess", C_GetCurrentProcess, PTOC_GetCurrentProcess},
    {"AEInstallEventHandler", C_AEInstallEventHandler, PTOC_AEInstallEventHandler},
    {"AEGetEventHandler", C_AEGetEventHandler, PTOC_AEGetEventHandler},
    {"AERemoveEventHandler", C_AERemoveEventHandler, PTOC_AERemoveEventHandler},
    {"AEProcessAppleEvent", C_AEProcessAppleEvent, PTOC_AEProcessAppleEvent},
    {"AEGetAttributePtr", C_AEGetAttributePtr, PTOC_AEGetAttributePtr},
    {"AECreateDesc", C_AECreateDesc, PTOC_AECreateDesc},
    {"AEDisposeDesc", C_AEDisposeDesc, PTOC_AEDisposeDesc},
    {"AECreateList", C_AECreateList, PTOC_AECreateList},
    {"AEPutKeyDesc", C_AEPutKeyDesc, PTOC_AEPutKeyDesc},
    {"AEPutKeyPtr", C_AEPutKeyPtr, PTOC_AEPutKeyPtr},
    {"AEPutDesc", C_AEPutDesc, PTOC_AEPutDesc},
    {"AEPutPtr", C_AEPutPtr, PTOC_AEPutPtr},
    {"AESizeOfKeyDesc", C_AESizeOfKeyDesc, PTOC_AESizeOfKeyDesc},
    {"AESizeOfNthItem", C_AESizeOfNthItem, PTOC_AESizeOfNthItem},
    {"AECreateAppleEvent", C_AECreateAppleEvent, PTOC_AECreateAppleEvent},
    {"AESend", C_AESend, PTOC_AESend},
    {"AEGetParamPtr", C_AEGetKeyPtr, PTOC_AEGetKeyPtr},
    {"AEGetParamDesc", C_AEGetKeyDesc, PTOC_AEGetKeyDesc},
    {"AEPutParamDesc", C_AEPutKeyDesc, PTOC_AEPutKeyDesc},
    {"AECoerceDesc", C_AECoerceDesc, PTOC_AECoerceDesc},
    {"AECountItems", C_AECountItems, PTOC_AECountItems},
    {"AEGetNthPtr", C_AEGetNthPtr, PTOC_AEGetNthPtr},
    {"AEInteractWithUser", C_AEInteractWithUser, PTOC_AEInteractWithUser},
    {"GetFrontProcess", ppc_get_front_process, M1(2, 5)},
    {"WakeUpProcess", C_WakeUpProcess, PTOC_WakeUpProcess},
    {"GetNextProcess", C_GetNextProcess, PTOC_GetNextProcess},
    {"GetProcessInformation", C_GetProcessInformation, PTOC_GetProcessInformation},
    {"SameProcess", C_SameProcess, PTOC_SameProcess},
    {"RemoveResource", C_RmveResource, PTOC_RmveResource},
    {"RegisterAppearanceClient", ROMlib_RegisterAppearanceClient, M0(4)},
    {"UnregisterAppearanceClient", ROMlib_UnregisterAppearanceClient, M0(4)},
    {"EqualString", EqualString, M4(1, 5, 5, 1, 1)},
    {"TempNewHandle", C_TempNewHandle, PTOC_TempNewHandle},
    {"TempFreeMem", C_TempFreeMem, PTOC_TempFreeMem},
    {"TempMaxMem", C_TempMaxMem, PTOC_TempMaxMem},
    {"TempHLock", C_TempHLock, PTOC_TempHLock},
    {"TempHUnlock", C_TempHUnlock, PTOC_TempHUnlock},
    {"TempDisposeHandle", C_TempDisposeHandle, PTOC_TempDisposeHandle},
    {"FSMakeFSSpec", C_FSMakeFSSpec, PTOC_FSMakeFSSpec},
    {"HGetFInfo", HGetFInfo, M4(2, 2, 4, 5, 5)},
    {"HSetFInfo", ppc_hsetfinfo, M4(2, 2, 4, 5, 5)},
    {"FlushVol", FlushVol, M2(2, 5, 2)},
    {"GetVol", GetVol, M2(2, 5, 5)},
    {"SetVol", SetVol, M2(2, 5, 2)},
    {"GetWDInfo", GetWDInfo, M4(2, 2, 5, 5, 5)},
    {"HGetVol", ppc_hgetvol, M3(2, 5, 5, 5)},
    {"HSetVol", ppc_hsetvol, M3(2, 5, 2, 4)},
    {"NMInstall", NMInstall, M1(2, 5)},
    {"NMRemove", NMRemove, M1(2, 5)},
    {"SetScriptManagerVariable", C_SetEnvirons, PTOC_SetEnvirons},
    {"GetScriptManagerVariable", C_GetEnvirons, PTOC_GetEnvirons},
    {"GetScriptVariable", C_GetScript, PTOC_GetScript},
    {"SndSoundManagerVersion", C_SndSoundManagerVersion, PTOC_SndSoundManagerVersion},
};

PRIVATE const struct {
    const char* name;
    unsigned index;
} toolbox_imports[] = {

#include "generated/ppc-toolbox.inc"
};

/* The private Mixed Mode worker uses the saved nanokernel register order:
   D0-D3, A0-A3, D4-D7, A4-A6, then the condition-code selectors. */
PRIVATE uint32 ppc_set_emulator_register(uint32 selector, uint32 value)
{
    static const uint8 register_index[] = {0, 1, 2, 3, 8, 9, 10, 11, 4, 5, 6, 7, 12, 13, 14};
    if (selector < NELEM(register_index)) {
        M68kReg* reg = &cpu_state.regs[register_index[selector]];
        const uint32 previous = reg->ul.n;
        reg->ul.n = value;
        cpu_state.ccc = cpu_state.ccv = 0;
        cpu_state.ccnz = value != 0;
        cpu_state.ccn = (int32)value < 0;
        return previous;
    }
    if (selector == 15)
        return 15;
    if (selector > 20)
        return 16;

    const unsigned shift = selector - 16;
    const uint32 previous = (!!cpu_state.ccc) | ((!!cpu_state.ccv) << 1) | ((!cpu_state.ccnz) << 2) |
                            ((!!cpu_state.ccn) << 3) | ((!!cpu_state.ccx) << 4);
    const uint32 updated = previous | (value << shift);
    cpu_state.ccc = updated & 1;
    cpu_state.ccv = updated & 2;
    cpu_state.ccnz = !(updated & 4);
    cpu_state.ccn = updated & 8;
    cpu_state.ccx = updated & 16;
    return (previous >> shift) & 1;
}

PRIVATE const ppc_native_import private_imports[] = {
    {"SetEmulatorRegister", ppc_set_emulator_register, M2(4, 4, 4)},
    {"PBXGetVolInfoSync", ppc_PBXGetVolInfoSync, M1(2, 4)},
    {"HasDepth", C_HasDepth, PTOC_HasDepth},
    {"SndManagerStatus", C_SndManagerStatus, PTOC_SndManagerStatus},
    {"SndChannelStatus", C_SndChannelStatus, PTOC_SndChannelStatus},
    {"SndStartFilePlay", C_SndStartFilePlay, PTOC_SndStartFilePlay},
    {"SndPauseFilePlay", C_SndPauseFilePlay, PTOC_SndPauseFilePlay},
    {"SndStopFilePlay", C_SndStopFilePlay, PTOC_SndStopFilePlay},
    {"SndGetSysBeepState", C_SndGetSysBeepState, PTOC_SndGetSysBeepState},
    {"SndSetSysBeepState", C_SndSetSysBeepState, PTOC_SndSetSysBeepState},
    /* Without a separate pageable heap, classic MemoryMgr returns SysZone. */
    {"GetPageableSystemZone", SystemZone, M0(5)},
    {"RealScript", ppc_real_script, M0(2)},
    {"Font2RealScript", C_Font2Script, M1(2, 2)},
    {"GetAuxMenuItem", ppc_get_aux_menu_item, M4(2, 5, 5, 5, 1)},
    {"GetMap", ppc_get_resource_map, M1(5, 2)},
    {"GetOverrideMap", ppc_get_override_map, M1(5, 5)},
    {"LMGetKeyMap", ppc_key_map, M0(5)},
    {"GetQDGlobals", ppc_get_qd_globals, M0(5)},
    {"GetSystemClientProcess", ROMlib_get_system_client_process, M1(2, 5)},
    {"BeginSystemMode", ROMlib_begin_system_mode, M0(2)},
    {"EndSystemMode", ROMlib_end_system_mode, M0(2)},
    {"MoveHLow", ROMlib_MoveHLow, M1(0, 5)},
    {"NewFloatLayer", ROMlib_new_float_layer, M1(2, 5)},
    {"DisposeFloatLayer", ROMlib_dispose_float_layer, M1(2, 5)},
    {"GetCurLayer", ROMlib_layer_current, M0(5)},
    {"GetRootLayer", ROMlib_layer_root, M0(5)},
    {"SetCurLayer", ROMlib_layer_set, M1(0, 5)},
    {"SwapCurLayer", ROMlib_layer_swap, M1(5, 5)},
    {"GetSubWindows", ROMlib_layer_children, M1(5, 5)},
    {"FrontWindowIn", ROMlib_layer_front, M1(5, 5)},
    {"GetParent", ROMlib_layer_parent, M1(5, 5)},
    {"IsLayer", ROMlib_layer_p, M1(1, 5)},
};

PRIVATE const ppc_native_import windows_imports[] = {
    {"NewWindow", C_NewWindow, PTOC_NewWindow},
    {"NewCWindow", C_NewCWindow, PTOC_NewCWindow},
    {"DisposeWindow", ppc_window_dispose, M1(0, 4)},
    {"CloseWindow", C_CloseWindow, PTOC_CloseWindow},
    {"IsValidWindowPtr", ppc_valid_window, M1(1, 4)},
    {"GetWindowOwnerCount", ppc_window_owner_count, M2(4, 4, 4)},
    {"AreFloatingWindowsActive", ppc_floating_windows_active, M0(1)},
    {"ActivateFloatingWindows", ppc_activate_floating_windows, M0(0)},
    {"DeactivateFloatingWindows", ppc_deactivate_floating_windows, M0(0)},
};

PRIVATE const ppc_native_import ppc_windows_imports[] = {
    {"SetWindowBounds", ROMlib_carbon_windows_set_bounds, ROMLIB_CARBON_PPC_FRAME},
    {"MoveWindowStructure", ROMlib_carbon_windows_move_structure, ROMLIB_CARBON_PPC_FRAME},
    {"SetWindowContentColor", ROMlib_carbon_windows_set_content_color, ROMLIB_CARBON_PPC_FRAME},
    {"GetWindowContentColor", ROMlib_carbon_windows_get_content_color, ROMLIB_CARBON_PPC_FRAME},
};

PRIVATE const ppc_native_import movie_imports[] = {
    {"EnterMovies", C_EnterMovies, PTOC_EnterMovies},
    {"ExitMovies", C_ExitMovies, PTOC_ExitMovies},
    {"GetMoviesError", C_GetMoviesError, PTOC_GetMoviesError},
    {"GetMoviesStickyError", C_GetMoviesStickyError, PTOC_GetMoviesStickyError},
    {"ClearMoviesStickyError", C_ClearMoviesStickyError, PTOC_ClearMoviesStickyError},
    {"MoviesTask", C_MoviesTask, PTOC_MoviesTask},
    {"PrerollMovie", C_PrerollMovie, PTOC_PrerollMovie},
    {"SetMovieActive", C_SetMovieActive, PTOC_SetMovieActive},
    {"StartMovie", C_StartMovie, PTOC_StartMovie},
    {"StopMovie", C_StopMovie, PTOC_StopMovie},
    {"GoToBeginningOfMovie", C_GoToBeginningOfMovie, PTOC_GoToBeginningOfMovie},
    {"SetMovieGWorld", C_SetMovieGWorld, PTOC_SetMovieGWorld},
    {"UpdateMovie", C_UpdateMovie, PTOC_UpdateMovie},
    {"DisposeMovie", C_DisposeMovie, PTOC_DisposeMovie},
    {"GetMovieVolume", C_GetMovieVolume, PTOC_GetMovieVolume},
    {"CloseMovieFile", C_CloseMovieFile, PTOC_CloseMovieFile},
    {"IsMovieDone", C_IsMovieDone, PTOC_IsMovieDone},
    {"NewMovieFromFile", C_NewMovieFromFile, PTOC_NewMovieFromFile},
    {"GetMoviePreferredRate", C_GetMoviePreferredRate, PTOC_GetMoviePreferredRate},
    {"GetMovieBox", C_GetMovieBox, PTOC_GetMovieBox},
    {"SetMovieBox", C_SetMovieBox, PTOC_SetMovieBox},
    {"OpenMovieFile", C_OpenMovieFile, PTOC_OpenMovieFile},
};

static const ppc_native_import* ppc_find_native_import(const char* name)
{
    for (unsigned i = 0; i < NELEM(native_imports); ++i)
        if (!strcmp(name, native_imports[i].name))
            return &native_imports[i];
    return NULL;
}

PRIVATE int ppc_special_function(const char* library, const char* name, int ppc)
{
    if (ppc && !strcmp(library, "Memory Manager") && ROMlib_native_memory_handles(name))
        return 1;
    if (ppc && !strcmp(library, "InterfaceLib") &&
        (!strcmp(name, "ShutDwnInstall") || !strcmp(name, "ShutDwnRemove") || !strcmp(name, "ShutDwnPower") ||
         !strcmp(name, "ShutDwnStart")))
        return 1;
    static const char* math[] = {
        "acos",       "asin",          "atan",          "cos",         "sin",      "tan",          "cosh",
        "sinh",       "tanh",          "exp",           "log",         "log10",    "sqrt",         "fabs",
        "floor",      "ceil",          "round",         "trunc",       "pow",      "atan2",        "fmod",
        "ldexp",      "frexp",         "modf",          "fegetenv",    "fesetenv", "feholdexcept", "fegetround",
        "fesetround", "feraiseexcept", "feclearexcept", "fetestexcept"};
    static const char* toolbox[] = {"GetSharedLibrary",
                                    "GetDiskFragment",
                                    "GetMemFragment",
                                    "FindSymbol",
                                    "CloseConnection",
                                    "CountSymbols",
                                    "GetIndSymbol",
                                    "CallUniversalProc",
                                    "UpTime",
                                    "AbsoluteToNanoseconds",
                                    "Microseconds",
                                    "MakeDataExecutable",
                                    "DebugStr",
                                    "CallOSTrapUniversalProc",
                                    "NewRoutineDescriptor",
                                    "DisposeRoutineDescriptor",
                                    "FragFindOwnerOfPC",
                                    "FragGetContextInfo",
                                    "FragGetConnectionInfo",
                                    "FragGetClosureInfo",
                                    "FragAddNotifyProc",
                                    "FragRemoveNotifyProc",
                                    "FragRelease",
                                    "ROMAWGetImportInfo",
                                    "ROMAWGetImportLibrary"};
    unsigned i;
    if ((!strcmp(library, "SpeechLib") || !strcmp(library, "InterfaceLib") || !strcmp(library, "CarbonLib")) &&
        ROMlib_speech_signature(name, NULL))
        return 1;
    if (!strcmp(library, "StdCLib"))
        return !strcmp(name, "sprintf") || !strcmp(name, "snprintf") || !strcmp(name, "malloc") ||
               !strcmp(name, "calloc") || !strcmp(name, "realloc") || !strcmp(name, "free") ||
               !strcmp(name, "__setjmp") || !strcmp(name, "longjmp");
    if (ppc && (!strcmp(library, "ThreadsLib") || !strcmp(library, "CarbonLib") || !strcmp(library, "InterfaceLib")))
        if (ROMlib_ppc_threads_handles(name))
            return 1;
    if (!strcmp(library, "MathLib")) {
        for (i = 0; i < NELEM(math); ++i)
            if (!strcmp(name, math[i]))
                return 1;
    }
    else if (!strcmp(library, "InterfaceLib") || !strcmp(library, "CarbonLib") ||
             !strcmp(library, "PrivateInterfaceLib") || !strcmp(library, "ROMAWCFM")) {
        if (ROMlib_component_selector(name) >= 0)
            return 1;
        for (i = 0; i < NELEM(toolbox); ++i)
            if (!strcmp(name, toolbox[i]))
                return 1;
    }
    return 0;
}

PRIVATE int ppc_resolve(void* opaque, const char* library, const char* name, unsigned symbol_class, int weak,
                        uint32* address)
{
    ppc_context* ctx = opaque;
    ppc_import binding;
    unsigned i;
    int carbon_classic_reference = 1;
    memset(&binding, 0, sizeof binding);
    if (!strcmp(library, "WindowsLib") && !strcmp(name, "TransitionWindow")) {
        int found = ppc_external_symbol(ctx, library, name, symbol_class, address);
        if (found)
            return found > 0;
        binding.function = ppc_transition_window;
        binding.magic = M4(4, 4, 4, 4, 5);
    }
    if (!strcmp(library, "AppearanceLib") && !strcmp(name, "PlayThemeSound")) {
        int found = ppc_external_symbol(ctx, library, name, symbol_class, address);
        if (found)
            return found > 0;
        binding.function = ppc_theme_sound_unavailable;
        binding.magic = M1(4, 4);
    }
    if ((!strcmp(library, "AppearanceLib") || !strcmp(library, "WindowsLib")) &&
        (!strcmp(name, "IsWindowCollapsed") || !strcmp(name, "CollapseWindow") || !strcmp(name, "GetWindowFeatures"))) {
        int found = ppc_external_symbol(ctx, library, name, symbol_class, address);
        if (found)
            return found > 0;
        if (!strcmp(name, "GetWindowFeatures")) {
            binding.function = ppc_get_window_features;
            binding.magic = M2(4, 4, 4);
        }
        else if (!strcmp(name, "IsWindowCollapsed")) {
            binding.function = ppc_native_window_uncollapsed;
            binding.magic = M1(1, 4);
        }
        else {
            binding.function = ppc_native_collapse_unavailable;
            binding.magic = M2(2, 4, 1);
        }
    }
    if (!strcmp(library, "WindowsLib") &&
        (!strcmp(name, "SetWindowProxyFSSpec") || !strcmp(name, "GetWindowProxyIcon") ||
         !strcmp(name, "SetWindowProxyIcon"))) {
        int found = ppc_external_symbol(ctx, library, name, symbol_class, address);
        if (found != 0)
            return found > 0;
        binding.function = !strcmp(name, "SetWindowProxyFSSpec") ? (void*)ppc_window_proxy_unavailable
                                                                 : (void*)ppc_window_proxy_icon_absent;
        binding.magic = M2(2, 4, 5);
        if (!strcmp(name, "SetWindowProxyIcon")) {
            binding.function = ppc_set_window_proxy_icon_absent;
            binding.magic = M2(2, 4, 4);
        }
    }
    if (!strcmp(library, "MacDialogsLib") &&
        (!strcmp(name, "SetModalDialogEventMask") || !strcmp(name, "GetModalDialogEventMask"))) {
        int found = ppc_external_symbol(ctx, library, name, symbol_class, address);
        if (found)
            return found > 0;
        binding.function = !strcmp(name, "SetModalDialogEventMask") ? (void*)ROMlib_SetModalDialogEventMask
                                                                    : (void*)ROMlib_GetModalDialogEventMask;
        binding.magic = !strcmp(name, "SetModalDialogEventMask") ? M2(4, 5, 2) : M2(4, 5, 5);
    }
    if (!strcmp(library, "OTInetClientLib") && !strcmp(name, "OTInetGetInterfaceInfo")) {
        int found = ppc_external_symbol(ctx, library, name, symbol_class, address);
        if (found != 0)
            return found > 0;
        binding.function = ppc_ot_interface_unavailable;
        binding.magic = M2(4, 5, 4);
    }
    if (!strcmp(library, "StdCLib") && !strcmp(name, "qsort") && cfm_architecture(ctx) == kPowerPCArch) {
        int found = ppc_external_symbol(ctx, library, name, symbol_class, address);
        if (found)
            return found > 0;
        binding.function = ROMlib_carbon_stdlib_qsort;
        binding.magic = ROMLIB_CARBON_PPC_FRAME;
    }
    if (!strcmp(library, "StdCLib") && (!strcmp(name, "__setjmp") || !strcmp(name, "longjmp")) &&
        cfm_architecture(ctx) == kPowerPCArch) {
        int found = ppc_external_symbol(ctx, library, name, symbol_class, address);
        if (found)
            return found > 0;
    }
    /* These weak imports are called directly by classic applications. Report
     unavailable notification support through the Display Manager API. */
    if (!strcmp(library, "DisplayLib") && cfm_architecture(ctx) == kMotorola68KArch &&
        (!strcmp(name, "DMRegisterExtendedNotifyProc") || !strcmp(name, "DMRemoveExtendedNotifyProc"))) {
        binding.function = ppc_display_notification_unavailable;
        binding.magic = 2;
    }
    if (!strcmp(library, "QuickTimeLib") || !strcmp(library, "InterfaceLib"))
        for (i = 0; i < NELEM(movie_imports); ++i)
            if (!strcmp(name, movie_imports[i].name)) {
                binding.function = movie_imports[i].function;
                binding.magic = movie_imports[i].magic;
                break;
            }
    if (!strcmp(library, "NQD") || !strcmp(library, "InterfaceLib") || !strcmp(library, "CarbonLib")) {
        if (!strcmp(name, "QDGetPatternOrigin")) {
            binding.function = ROMlib_QDGetPatternOrigin;
            binding.magic = M1(0, 5);
        }
        else if (!strcmp(name, "QDSetPatternOrigin")) {
            binding.function = ROMlib_QDSetPatternOrigin;
            binding.magic = M1(0, 3);
        }
    }
    /* The Carbon wrappers use shared state from the saved fragment. Classic
       launches without that fragment continue to use the bridge adapters. */
    if (!strcmp(library, "CarbonLib")) {
        const int wait = !strcmp(name, "WaitNextEvent");
        const int ppc = cfm_architecture(ctx) == kPowerPCArch;
        if (wait || (ppc && !strcmp(name, "Gestalt"))) {
            int found = ppc_external_symbol(ctx, library, name, symbol_class, address);
            if (found < 0 || (found && !ppc))
                return found > 0;
            carbon_classic_reference = found;
        }
    }
    if (!strcmp(library, "PrivateInterfaceLib") || !strcmp(library, "InterfaceLib"))
        for (i = 0; i < NELEM(private_imports); ++i)
            if (!strcmp(name, private_imports[i].name)) {
                binding.function = private_imports[i].function;
                binding.magic = private_imports[i].magic;
                break;
            }
    if (!strcmp(library, "MenusLib") && cfm_architecture(ctx) == kPowerPCArch) {
        if (!strcmp(name, "SetSubmenuParentChoosable")) {
            binding.function = ppc_set_submenu_parent_choosable;
            binding.magic = ROMLIB_CARBON_PPC_FRAME;
        }
        else if (!strcmp(name, "GetSubmenuParentChoosable")) {
            binding.function = ppc_get_submenu_parent_choosable;
            binding.magic = ROMLIB_CARBON_PPC_FRAME;
        }
    }
    if (!strcmp(library, "IconServicesLib") && cfm_architecture(ctx) == kPowerPCArch) {
        if (!strcmp(name, "GetIconRefFromFolder")) {
            binding.function = ppc_get_icon_ref_from_folder;
            binding.magic = ROMLIB_CARBON_PPC_FRAME;
        }
        else if (!strcmp(name, "GetIconRefByName")) {
            binding.function = ppc_get_icon_ref_by_name;
            binding.magic = ROMLIB_CARBON_PPC_FRAME;
        }
        else if (!strcmp(name, "CompositeIconRef")) {
            binding.function = ppc_composite_icon_ref;
            binding.magic = ROMLIB_CARBON_PPC_FRAME;
        }
    }
    if (!strcmp(library, "WindowsLib"))
        for (i = 0; i < NELEM(windows_imports); ++i)
            if (!strcmp(name, windows_imports[i].name)) {
                binding.function = windows_imports[i].function;
                binding.magic = windows_imports[i].magic;
                break;
            }
    if (!strcmp(library, "InterfaceLib") && cfm_architecture(ctx) == kPowerPCArch)
        for (i = 0; i < NELEM(printing_imports); ++i)
            if (!strcmp(name, printing_imports[i].name)) {
                binding.function = printing_imports[i].function;
                binding.magic = printing_imports[i].magic;
                break;
            }
    if (!strcmp(library, "WindowsLib") && cfm_architecture(ctx) == kPowerPCArch)
        for (i = 0; i < NELEM(ppc_windows_imports); ++i)
            if (!strcmp(name, ppc_windows_imports[i].name)) {
                binding.function = ppc_windows_imports[i].function;
                binding.magic = ppc_windows_imports[i].magic;
                break;
            }
    if (!strcmp(library, "InterfaceLib") || !strcmp(library, "CarbonLib") || !strcmp(library, "MathLib") ||
        !strcmp(library, "StdCLib") || !strcmp(library, "AppearanceLib") || !strcmp(library, "ObjectSupportLib") ||
        (!strcmp(library, "PrivateInterfaceLib") &&
         (!strcmp(name, "SndSoundManagerVersion") || !strcmp(name, "IsSystemMenu") ||
          !strcmp(name, "RequestVolumeNotification") || !strcmp(name, "DeclineVolumeNotification") ||
          !strcmp(name, "NewDesktopLayerOwner") || !strcmp(name, "SetIconDevice") || !strcmp(name, "GetIconDevice") ||
          !strcmp(name, "SysZoneFloatSizes") || !strcmp(name, "VM_GetHoldableBytes")))) {
        const ppc_native_import* service = ppc_find_native_import(name);
        if (service) {
            binding.function = service->function;
            binding.magic = service->magic;
        }
    }
    /* AppearanceLib 0x502 writes the same command ID that Carbon's getter
       reads. Selectors 0x511 and 0x512 access Carbon's property collection slot. */
    if (!strcmp(library, "AppearanceLib")) {
        if (!strcmp(name, "GetViewsFont") && cfm_architecture(ctx) == kPowerPCArch) {
            binding.function = ppc_get_views_font;
            binding.magic = ROMLIB_CARBON_PPC_FRAME;
        }
        else if (!strcmp(name, "GetThemeDrawingState") && cfm_architecture(ctx) == kPowerPCArch) {
            binding.function = ppc_get_theme_drawing_state;
            binding.magic = M1(4, 4);
        }
        else if (!strcmp(name, "SetThemeDrawingState") && cfm_architecture(ctx) == kPowerPCArch) {
            binding.function = ppc_set_theme_drawing_state;
            binding.magic = M2(4, 4, 1);
        }
        else if (!strcmp(name, "DisposeThemeDrawingState") && cfm_architecture(ctx) == kPowerPCArch) {
            binding.function = ppc_dispose_theme_drawing_state;
            binding.magic = M1(4, 4);
        }
        else if (!strcmp(name, "NormalizeThemeDrawingState") && cfm_architecture(ctx) == kPowerPCArch) {
            binding.function = ppc_normalize_theme_drawing_state;
            binding.magic = M0(4);
        }
        else if (!strcmp(name, "SetMenuItemRefCon2")) {
            binding.function = ROMlib_set_menu_item_refcon2;
            binding.magic = M3(2, 5, 2, 4);
        }
        else if (!strcmp(name, "GetMenuItemRefCon2")) {
            binding.function = ROMlib_get_menu_item_refcon2;
            binding.magic = M3(2, 5, 2, 5);
        }
        else if (!strcmp(name, "SetMenuItemCommandID")) {
            binding.function = ROMlib_set_menu_item_command;
            binding.magic = M3(2, 5, 2, 4);
        }
    }
    if (!binding.function && (!strcmp(library, "InterfaceLib") || !strcmp(library, "CarbonLib"))) {
        static const struct {
            const char *modern, *classic;
        } aliases[] = {
            {"SetControlTitle", "SetCTitle"},
            {"GetControlTitle", "GetCTitle"},
            {"SetControlReference", "SetCRefCon"},
            {"GetControlReference", "GetCRefCon"},
            {"SetControlValue", "SetCtlValue"},
            {"GetControlValue", "GetCtlValue"},
            {"SetControlMinimum", "SetCtlMin"},
            {"GetControlMinimum", "GetCtlMin"},
            {"SetControlMaximum", "SetCtlMax"},
            {"GetControlMaximum", "GetCtlMax"},
            {"SetControlAction", "SetCtlAction"},
            {"GetControlAction", "GetCtlAction"},
            {"GetAuxiliaryControlRecord", "GetAuxCtl"},
            {"UpdateControls", "UpdtControl"},
            {"AppendResMenu", "AddResMenu"},
            {"GetMenuItemText", "GetItem"},
            {"SetMenuItemText", "SetItem"},
            {"CountMenuItems", "CountMItems"},
            {"InsertMenuItem", "InsMenuItem"},
            {"DeleteMenuItem", "DelMenuItem"},
            {"GetDialogItem", "GetDItem"},
            {"DisposeDialog", "DisposDialog"},
            {"DisposePixMap", "DisposPixMap"},
            {"DisposePixPat", "DisposPixPat"},
            {"DisposeCTable", "DisposCTable"},
            {"DisposeCCursor", "DisposCCursor"},
            {"DisposeGDevice", "DisposGDevice"},
            {"SetDialogItem", "SetDItem"},
            {"GetDialogItemText", "GetIText"},
            {"SetDialogItemText", "SetIText"},
            {"SelectDialogItemText", "SelIText"},
            {"HideDialogItem", "HideDItem"},
            {"ShowDialogItem", "ShowDItem"},
            {"FindDialogItem", "FindDItem"},
        };

        const char* toolbox_name = name;
        for (i = 0; i < NELEM(aliases); ++i)
            if (!strcmp(name, aliases[i].modern)) {
                toolbox_name = aliases[i].classic;
                break;
            }
        for (i = 0; i < NELEM(toolbox_imports); ++i)
            if (!strcmp(toolbox_name, toolbox_imports[i].name)) {
                ptocblock_t* entry = &toolstuff[toolbox_imports[i].index].ptoc;
                if (entry->magic != (uint64)-1 && (entry->magic < 6 || entry->magic > 7)) {
                    binding.function = entry->wheretogo;
                    binding.magic = entry->magic;
                }
                break;
            }
    }
    if (symbol_class == 1 && ppc_builtin_data(ctx, library, name, address))
        return ppc_native_dependency(ctx, library);
    /* Carbon WindowRefs expose CGrafPorts through GetWindowPort and its color
     accessors. The classic monochrome NewWindow entry violates that contract. */
    if (!strcmp(library, "CarbonLib") && binding.function == C_NewWindow)
        binding.function = C_NewCWindow;
    if (!strcmp(library, "InterfaceLib") && binding.function == C_DisposeWindow) {
        binding.function = ppc_window_dispose;
        binding.magic = M1(0, 4);
    }
    {
        const ROMlib_carbon_export* native = ROMlib_carbon_find_library_export(library, name, symbol_class);
        if (!native && !strcmp(library, "ControlsLib") && !strcmp(name, "IsValidControlHandle"))
            native = ROMlib_carbon_find_export(name, symbol_class);
        if (!native && !strcmp(library, "MenusLib")) {
            static const char* const menu_reexports[] = {
                "ShowMenuBar",
                "HideMenuBar",
                "DisableMenuItem",
                "IsMenuItemEnabled",
            };
            for (i = 0; i < NELEM(menu_reexports); ++i)
                if (!strcmp(name, menu_reexports[i])) {
                    native = ROMlib_carbon_find_export(name, symbol_class);
                    break;
                }
        }
        if (!native && !strcmp(library, "InterfaceLib")) {
            static const char* const carbon_reexports[] = {
                "BlockZero",
                "DeleteGestaltValue",
                "DMDrawDesktopRect",
                "DMDrawDesktopRegion",
                "GetMaxResourceSize",
                "LMGetSysResName",
                "LMSetHiliteRGB",
                "MaxBlock",
                "NewGestaltValue",
                "ReadPartialResource",
                "ReallocateHandle",
                "ReplaceGestaltValue",
                "SetGestaltValue",
                "PtrToXHand",
                "Enqueue",
                "Dequeue",
                "DMRegisterNotifyProc",
                "DMRemoveNotifyProc",
                "DMRegisterExtendedNotifyProc",
                "DMRemoveExtendedNotifyProc",
                "FolderManagerRegisterNotificationProc",
                "FolderManagerUnregisterNotificationProc",
                "FolderManagerRegisterCallNotificationProcs",
            };
            for (i = 0; i < NELEM(carbon_reexports); ++i)
                if (!strcmp(name, carbon_reexports[i])) {
                    native = ROMlib_carbon_find_export(name, symbol_class);
                    break;
                }
        }
        if (!native && !strcmp(library, "DisplayLib") &&
            (!strcmp(name, "DMRegisterExtendedNotifyProc") || !strcmp(name, "DMRemoveExtendedNotifyProc") ||
             !strcmp(name, "DMRegisterNotifyProc") || !strcmp(name, "DMRemoveNotifyProc")))
            native = ROMlib_carbon_find_export(name, symbol_class);
        if (!carbon_classic_reference)
            native = NULL;
        if (native && cfm_architecture(ctx) == kMotorola68KArch &&
            ((native->signature & ROMLIB_CARBON_PPC_FRAME) || (native->signature & 7) == ROMLIB_CARBON_DOUBLE_RESULT ||
             (native->flags & ROMLIB_CARBON_PPC_ONLY)))
            native = NULL;
        if (native) {
            if (native->flags & ROMLIB_CARBON_EXTENSION) {
                int found = ppc_external_symbol(ctx, library, name, symbol_class, address);
                if (found)
                    return found > 0;
            }
            /* TEC and its companion fragments share initialized registry and table state.
               The Data Browser constructor needs the initialized CDEF.
               Appearance uses saved color-resource paths and initialized manager state. */
            if (native->reference_library &&
                ((native->flags & ROMLIB_CARBON_PRELOAD_REFERENCE) ||
                 !strcmp(native->reference_library, "TextEncodingConverter") ||
                 !strcmp(native->reference_library, "TextCommon") ||
                 !strcmp(native->reference_library, "UnicodeConverter") ||
                 (!strcmp(native->reference_library, "DataBrowserLib") && !strcmp(name, "CreateDataBrowserControl")) ||
                 (!strcmp(native->reference_library, "AppearanceLib") &&
                  (!strcmp(name, "SetThemeCursor") || !strcmp(name, "SetAnimatedThemeCursor") ||
                   !strcmp(name, "SetThemeCursorWithOptions"))))) {
                uint32 original;
                if (ppc_external_symbol(ctx, native->reference_library, name, symbol_class, &original) < 0)
                    return 0;
            }
            binding.function = native->function;
            binding.magic = native->signature;
        }
        else if ((!strcmp(library, "DragLib") || !strcmp(library, "CarbonLib")) && !strcmp(name, "WaitMouseMoved")) {
            int found = ppc_external_symbol(ctx, library, name, symbol_class, address);
            if (found != 0)
                return found > 0;
            binding.function = ppc_wait_mouse_moved_without_drag;
            binding.magic = M1(1, 3);
        }
    }
    const int special = ppc_special_function(library, name, cfm_architecture(ctx) == kPowerPCArch);
    if (!binding.function && !special) {
        char previous_error[sizeof ctx->error];
        int found;
        memcpy(previous_error, ctx->error, sizeof previous_error);
        /* The System-file CollectionsLib contains only trap forwarding stubs
         and has no initializer. Its matching Collections fragment contains
         the actual PPC implementation of the same public exports. ROMAW
         has no boot-installed Collection Manager dispatch vector, so resolve
         those forwarding exports to that real implementation. */
        found = ppc_external_symbol(ctx, !strcmp(library, "CollectionsLib") ? "Collections" : library, name,
                                    symbol_class, address);
        if (found > 0)
            return 1;
        if (!found && cfm_architecture(ctx) == kPowerPCArch && !strcmp(library, "ControlsLib")) {
            /* The six private system-property exports have the same ABI in
               CarbonLib 1.4 and the System 9 ControlsLib. Keep real symbols
               first and use the Carbon property store when they are absent. */
            static const char* const properties[] = {
                "GetControlSystemProperty",           "GetControlSystemPropertySize",
                "SetControlSystemProperty",           "RemoveControlSystemProperty",
                "GetControlSystemPropertyAttributes", "ChangeControlSystemPropertyAttributes",
            };
            for (i = 0; i < NELEM(properties); ++i)
                if (!strcmp(name, properties[i])) {
                    const ROMlib_carbon_export* entry = ROMlib_carbon_find_export(name, symbol_class);
                    if (entry) {
                        binding.function = entry->function;
                        binding.magic = entry->signature;
                        return ppc_native_dependency(ctx, "CarbonLib") &&
                               ppc_install_import(ctx, binding, library, name, symbol_class, address);
                    }
                    break;
                }
        }
        if (weak) {
            memcpy(ctx->error, previous_error, sizeof ctx->error);
            return 0;
        }
        if (found < 0)
            return 0;
    }
    if ((binding.function || special) && !ppc_native_dependency(ctx, library))
        return 0;
    /* CoreFoundation data objects need their actual runtime and callbacks;
     do not substitute null pointers for unsupported allocator/constants. */
    if (symbol_class != 0 && symbol_class != 2)
        return 0;
    /* Special services need explicit signatures before they can consume a
     CFM-68K stack. Do not generate a zero-argument thunk for an unknown ABI. */
    if (cfm_architecture(ctx) == kMotorola68KArch && !binding.function) {
        static const struct {
            const char* name;
            unsigned count, result;
        } signatures[] = {{"GetSharedLibrary", 6, 2},
                          {"GetDiskFragment", 8, 2},
                          {"GetMemFragment", 7, 2},
                          {"CloseConnection", 1, 2},
                          {"FindSymbol", 4, 2},
                          {"CountSymbols", 2, 2},
                          {"GetIndSymbol", 5, 2},
                          {"NewRoutineDescriptor", 3, 5},
                          {"DisposeRoutineDescriptor", 1, 0},
                          {"CallUniversalProc", 2, 4},
                          {"malloc", 1, 5},
                          {"calloc", 2, 5},
                          {"realloc", 2, 5},
                          {"free", 1, 0},
                          {"ExitToShell", 0, 0}};

        unsigned index;
        for (index = 0; index < NELEM(signatures); ++index)
            if (!strcmp(name, signatures[index].name)) {
                binding.magic = signatures[index].result;
                for (unsigned n = 0; n < signatures[index].count; ++n)
                    binding.magic |= UINT64_C(4) << (3 * (n + 1));
                break;
            }
        if (index == NELEM(signatures))
            return 0;
    }
    return ppc_install_import(ctx, binding, library, name, symbol_class, address);
}

PRIVATE uint32 ppc_callback_vector(ppc_context*, uint32);
PRIVATE int ppc_invoke_callback_stack(ppc_context*, uint32, uint32*, unsigned, uint32*, const ROMlib_ppc_registers*);
PRIVATE int ppc_execute(ppc_context*, ROMlib_ppc_registers*);

PRIVATE uint32 ppc_thread_resolve(void* opaque, uint32 address)
{
    return ppc_callback_vector(opaque, address);
}

PRIVATE int ppc_thread_execute(void* opaque, uint32 vector, uint32 argument, uint32 stack, uint32* result)
{
    ppc_context* ctx = opaque;
    uint32* entry = ppc_memory(ctx, vector, 8, FALSE);
    ROMlib_ppc_registers r;
    if (!entry)
        return 0;
    memset(&r, 0, sizeof r);
    r.pc = CL(entry[0]);
    r.gpr[2] = CL(entry[1]);
    r.gpr[1] = stack;
    r.gpr[3] = argument;
    r.lr = ctx->halt;
    int ok = ppc_execute(ctx, &r);
    *result = r.gpr[3];
    return ok;
}

PRIVATE void ppc_thread_switch(void* opaque, void (*switch_host)(void*), void* target)
{
    ppc_context* ctx = opaque;
    __typeof__(cpu_state) saved;
    ppc_fragment* requester = ctx->requester;
    ppc_closure* initializing = ctx->initializing_closure;
    /* Registers and FP state belong to the suspended thread. Interrupt
       flags, trap vectors and translator continuation/cache state remain
       live and shared; copying them back could lose a timer interrupt or
       resurrect a completed 68K interpreter frame. */
    size_t registers = offsetof(__typeof__(cpu_state), interrupt_status_changed);
    memcpy(&saved, &cpu_state, registers);
    saved.sr = cpu_state.sr;
    saved.usp = cpu_state.usp;
    saved.msp = cpu_state.msp;
    saved.isp = cpu_state.isp;
    saved.fpu = cpu_state.fpu;
    ROMlib_ppc_switch_host(switch_host, target);
    memcpy(&cpu_state, &saved, registers);
    cpu_state.sr = saved.sr;
    cpu_state.usp = saved.usp;
    cpu_state.msp = saved.msp;
    cpu_state.isp = saved.isp;
    cpu_state.fpu = saved.fpu;
    ctx->requester = requester;
    ctx->initializing_closure = initializing;
}

PRIVATE int ppc_thread_exiting(void* opaque)
{
    ppc_context* ctx = opaque;
    return ctx->exited || ctx->error[0];
}

PRIVATE uint32 ppc_thread_stack_space(void* opaque, uint32 sp)
{
    ppc_context* ctx = opaque;
    for (const ppc_allocation* allocation = ctx->allocations; allocation; allocation = allocation->next) {
        const uint32 base = US_TO_SYN68K(allocation->aligned);
        if (sp >= base && sp - base <= allocation->size)
            return sp - base;
    }
    return 0;
}

PRIVATE int ppc_thread_invoke(void* opaque, uint32 procedure, uint32* args, unsigned count, uint32* result)
{
    ppc_context* ctx = opaque;
    const uint32 vector = ppc_callback_vector(ctx, procedure);
    if (!vector) {
        if (!ctx->error[0])
            snprintf(ctx->error, sizeof ctx->error, "Invalid Thread Manager callback 0x%08lx",
                     (unsigned long)procedure);
        return 0;
    }
    return ppc_invoke_callback_stack(ctx, vector, args, count, result, NULL);
}

PRIVATE int ppc_thread_service(ppc_context* ctx, const char* name, ROMlib_ppc_registers* r)
{
    if (!ROMlib_ppc_threads_handles(name))
        return 0;
    /* Switching an active 68K callback would also require continuation of
       its translator/native bridge frames. Keep that unsupported boundary
       explicit rather than switching partially saved CPU state. */
    if (cfm_architecture(ctx) != kPowerPCArch && !strcmp(name, "NewThread")) {
        uint32* made = r->gpr[9] ? ppc_memory(ctx, r->gpr[9], 4, TRUE) : NULL;
        if (made)
            *made = 0;
        r->gpr[3] = (uint32)(int32)-4;
        return 1;
    }
    if (ctx->callback_depth &&
        (!strcmp(name, "YieldToThread") || !strcmp(name, "YieldToAnyThread") || !strcmp(name, "SetThreadState") ||
         !strcmp(name, "SetThreadStateEndCritical") || !strcmp(name, "DisposeThread"))) {
        r->gpr[3] = (uint32)(int32)-619;
        return 1;
    }
    ROMlib_ppc_thread_environment env = {ctx,
                                         ppc_memory,
                                         ppc_allocate,
                                         ppc_release,
                                         ppc_thread_resolve,
                                         ppc_thread_execute,
                                         ppc_thread_switch,
                                         ppc_thread_exiting,
                                         ppc_thread_stack_space,
                                         ppc_thread_invoke};
    int result = ROMlib_ppc_threads_service(&ctx->threads, &env, name, r, !ctx->callback_depth);
    if (result == 2 || ctx->exited)
        r->lr = ctx->halt;
    return ctx->error[0] ? -1 : result;
}

PRIVATE int ppc_time_service(ppc_context* ctx, const char* name, ROMlib_ppc_registers* r)
{
    uint64 value;
    uint32* out;
    if (!strcmp(name, "UpTime"))
        value = ROMlib_ppc_clock_ticks();
    else if (!strcmp(name, "Microseconds"))
        value = ROMlib_ppc_clock_ticks() / 25;
    else if (!strcmp(name, "AbsoluteToNanoseconds"))
        value = (((uint64)r->gpr[4] << 32) | r->gpr[5]) * 40;
    else
        return 0;
    /* Classic PPC returns UnsignedWide structures through the hidden first
     argument. Microseconds uses the same layout via its explicit output. */
    out = r->gpr[3] ? ppc_memory(ctx, r->gpr[3], 8, TRUE) : NULL;
    if (!out)
        return -1;
    out[0] = CL((uint32)(value >> 32));
    out[1] = CL((uint32)value);
    return 1;
}

PRIVATE int ppc_math_service(ppc_context* ctx, const char* binding_name, ROMlib_ppc_registers* r)
{
    const char* name;
    unsigned i;

    static const struct {
        const char* name;
        double (*fn)(double);
    } unary[] = {{"acos", acos}, {"asin", asin}, {"atan", atan},   {"cos", cos},   {"sin", sin},     {"tan", tan},
                 {"cosh", cosh}, {"sinh", sinh}, {"tanh", tanh},   {"exp", exp},   {"log", log},     {"log10", log10},
                 {"sqrt", sqrt}, {"fabs", fabs}, {"floor", floor}, {"ceil", ceil}, {"round", round}, {"trunc", trunc}};

    if (strncmp(binding_name, "MathLib:", 8))
        return 0;
    name = binding_name + 8;
    for (i = 0; i < NELEM(unary); ++i)
        if (!strcmp(name, unary[i].name)) {
            r->fpr[1] = unary[i].fn(r->fpr[1]);
            return 1;
        }
    if (!strcmp(name, "pow"))
        r->fpr[1] = pow(r->fpr[1], r->fpr[2]);
    else if (!strcmp(name, "atan2"))
        r->fpr[1] = atan2(r->fpr[1], r->fpr[2]);
    else if (!strcmp(name, "fmod"))
        r->fpr[1] = fmod(r->fpr[1], r->fpr[2]);
    else if (!strcmp(name, "ldexp"))
        r->fpr[1] = ldexp(r->fpr[1], (int32)r->gpr[5]);
    else if (!strcmp(name, "frexp")) {
        int exponent;
        uint32* p = ppc_memory(ctx, r->gpr[5], 4, TRUE);
        if (!p)
            return -1;
        r->fpr[1] = frexp(r->fpr[1], &exponent);
        *p = CL(exponent);
    }
    else if (!strcmp(name, "modf")) {
        double integral;
        uint64 bits;
        uint32* p = ppc_memory(ctx, r->gpr[5], 8, TRUE);
        if (!p)
            return -1;
        r->fpr[1] = modf(r->fpr[1], &integral);
        memcpy(&bits, &integral, 8);
        p[0] = CL(bits >> 32);
        p[1] = CL((uint32)bits);
    }
    else if (!strcmp(name, "fegetenv") || !strcmp(name, "fesetenv") || !strcmp(name, "feholdexcept")) {
        int write_p = strcmp(name, "fesetenv") != 0;
        uint32* p = ppc_memory(ctx, r->gpr[3], 4, write_p);
        if (!p)
            return -1;
        if (write_p)
            *p = CL(r->fpscr);
        else
            r->fpscr = CL(*p);
        if (!strcmp(name, "feholdexcept"))
            r->fpscr &= ~0xfff807f8U;
        r->gpr[3] = 0;
    }
    else if (!strcmp(name, "fegetround"))
        r->gpr[3] = r->fpscr & 3;
    else if (!strcmp(name, "fesetround")) {
        uint32 mode = r->gpr[3];
        r->gpr[3] = mode > 3 ? -1 : 0;
        if (mode <= 3)
            r->fpscr = (r->fpscr & ~3U) | mode;
    }
    else if (!strcmp(name, "feraiseexcept") || !strcmp(name, "feclearexcept") || !strcmp(name, "fetestexcept")) {
        const uint32 exceptions = 0x3e000000;
        uint32 mask = r->gpr[3] & exceptions;
        if (!strcmp(name, "fetestexcept"))
            r->gpr[3] = r->fpscr & mask;
        else {
            if (mask) {
                if (!strcmp(name, "feclearexcept")) {
                    r->fpscr &= ~mask;
                    if (mask & 0x20000000)
                        r->fpscr &= ~0x01f80700U; /* All invalid-operation causes. */
                }
                else {
                    r->fpscr |= mask;
                    if (mask & 0x20000000)
                        r->fpscr |= 0x00000400; /* VXSOFT. */
                }
                r->fpscr = (r->fpscr & ~0xc0000000U) | ((r->fpscr & exceptions) ? 0x80000000U : 0) |
                           ((r->fpscr & ((r->fpscr & 0xf8) << 22)) ? 0x40000000U : 0);
            }
            r->gpr[3] = 0;
        }
    }
    else
        return 0;
    return 1;
}

/* InterfaceLib's obsolete one-argument conversions return the original guest
   pointer, not a translated host pointer. Validate before moving any bytes. */
PRIVATE int ppc_inplace_string(ppc_context* ctx, const char* binding, ROMlib_ppc_registers* r)
{
    int pascal_to_c = !strcmp(binding, "InterfaceLib:p2cstr");
    if (!pascal_to_c && strcmp(binding, "InterfaceLib:c2pstr"))
        return 0;
    uint32 address = r->gpr[3];
    if (!address)
        return 1;
    unsigned length = 0;
    unsigned char* bytes;
    if (pascal_to_c) {
        bytes = ppc_memory(ctx, address, 1, FALSE);
        if (!bytes)
            goto invalid;
        length = bytes[0];
    }
    else {
        while (length < 255) {
            uint64 next = (uint64)address + length;
            if (next > UINT32_MAX || !(bytes = ppc_memory(ctx, next, 1, FALSE)))
                goto invalid;
            if (!*bytes)
                break;
            ++length;
        }
    }
    bytes = ppc_memory(ctx, address, length + 1, TRUE);
    if (!bytes)
        goto invalid;
    if (pascal_to_c) {
        memmove(bytes, bytes + 1, length);
        bytes[length] = 0;
    }
    else {
        memmove(bytes + 1, bytes, length);
        bytes[0] = length;
    }
    return 1;
invalid:
    snprintf(ctx->error, sizeof ctx->error, "Invalid InterfaceLib string buffer for %s", binding + 13);
    return -1;
}

/* C allocation results are guest heap pointers. Metadata is private to the
   host and distinguishes C ownership from images, descriptors and CFM state. */
PRIVATE int ppc_stdlib_memory(ppc_context* ctx, const char* binding, ROMlib_ppc_registers* r)
{
    const char* name;
    ppc_allocation* old = NULL;
    uint64 size;
    uint32 address;
    void* p;
    THz saved_zone;
    if (strncmp(binding, "StdCLib:", 8))
        return 0;
    name = binding + 8;
    if (strcmp(name, "malloc") && strcmp(name, "calloc") && strcmp(name, "realloc") && strcmp(name, "free"))
        return 0;
    size = r->gpr[3];
    if (!strcmp(name, "free") || !strcmp(name, "realloc")) {
        if (r->gpr[3]) {
            for (old = ctx->allocations; old; old = old->next)
                if (old->libc && US_TO_SYN68K(old->aligned) == r->gpr[3])
                    break;
            if (!old) {
                snprintf(ctx->error, sizeof ctx->error, "Invalid StdCLib allocation reference 0x%08lx",
                         (unsigned long)r->gpr[3]);
                return -1;
            }
        }
        size = !strcmp(name, "free") ? 0 : r->gpr[4];
        if (!size) {
            if (old)
                ppc_release(ctx, old->aligned);
            r->gpr[3] = 0;
            return 1;
        }
    }
    else if (!strcmp(name, "calloc"))
        size *= (uint64)r->gpr[4];
    if (size > INT32_MAX - 16) {
        r->gpr[3] = 0;
        return 1;
    }
    saved_zone = GetZone();
    if (old)
        SetZone(old->zone);
    p = ppc_allocate(ctx, size ? (uint32)size : 1, 16, &address);
    SetZone(saved_zone);
    if (!p) {
        r->gpr[3] = 0;
        return 1;
    }
    ctx->allocations->libc = 1;
    ctx->allocations->size = size;
    if (!strcmp(name, "calloc"))
        memset(p, 0, size);
    if (old) {
        memcpy(p, old->aligned, MIN(old->size, size));
        ppc_release(ctx, old->aligned);
    }
    r->gpr[3] = address;
    return 1;
}

PRIVATE void ppc_debug_string(const unsigned char* bytes, unsigned length)
{
    fprintf(stderr, "debugstr: ");
    fwrite(bytes, 1, length, stderr);
    fputc('\n', stderr);
}

/* Classic StdCLib's scalar PowerPC jump buffer. The vector slot at 0x10 is
   reserved. Store raw FPR bits, and retain the guest linkage and ABI slots. */
PRIVATE int ppc_stdclib_jump(ppc_context* ctx, ROMlib_ppc_registers* r, BOOLEAN saving)
{
    uint32 saved[0xf8 / 4];
    if (!r->gpr[3] || (r->gpr[3] & 3))
        return 0;
    void* guest = ppc_memory(ctx, r->gpr[3], sizeof saved, saving);
    if (!guest)
        return 0;
    memcpy(saved, guest, sizeof saved);
    if (saving) {
        saved[0] = CL(r->lr);
        saved[1] = CL(r->cr);
        saved[2] = CL(r->gpr[1]);
        saved[3] = CL(r->gpr[2]);
    }
    else {
        const uint32 target = CL(saved[0]), stack = CL(saved[2]);
        if (!target || (target & 3) || !stack || (stack & 15) || !ppc_memory(ctx, target, 4, FALSE) ||
            !ppc_memory(ctx, stack, 4, FALSE))
            return 0;
        r->lr = target;
        r->cr = CL(saved[1]);
        r->gpr[1] = stack;
        r->gpr[2] = CL(saved[3]);
    }
    for (unsigned reg = 13; reg < 32; ++reg) {
        if (saving)
            saved[reg - 8] = CL(r->gpr[reg]);
        else
            r->gpr[reg] = CL(saved[reg - 8]);
    }
    for (unsigned reg = 14; reg < 32; ++reg) {
        const unsigned index = 0x60 / 4 + 2 * (reg - 14);
        uint64 bits;
        if (saving) {
            memcpy(&bits, &r->fpr[reg], sizeof bits);
            saved[index] = CL((uint32)(bits >> 32));
            saved[index + 1] = CL((uint32)bits);
        }
        else {
            bits = ((uint64)CL(saved[index]) << 32) | CL(saved[index + 1]);
            memcpy(&r->fpr[reg], &bits, sizeof bits);
        }
    }
    const unsigned fpscr_index = 0xf0 / 4;
    if (saving) {
        saved[fpscr_index] = 0;
        saved[fpscr_index + 1] = CL(r->fpscr);
        memcpy(&r->fpr[0], &(uint64){r->fpscr}, sizeof r->fpr[0]);
        memcpy(guest, saved, sizeof saved);
        r->gpr[3] = 0;
    }
    else {
        uint64 fp = ((uint64)CL(saved[fpscr_index]) << 32) | CL(saved[fpscr_index + 1]);
        memcpy(&r->fpr[0], &fp, sizeof fp);
        r->fpscr = (uint32)fp;
        /* The saved implementation uses or. before changing a zero result to 1. */
        const uint32 result = r->gpr[4];
        const unsigned condition = (int32)result < 0 ? 8 : result ? 4 : 2;
        r->cr = (r->cr & 0x0fffffff) | condition << 28 | ((r->xer & 0x80000000) >> 3);
        r->gpr[3] = result ? result : 1;
    }
    return 1;
}

PRIVATE void ppc_publish_code(ppc_context* ctx, uint32 address, uint32 length)
{
    /* OPS19 requires publication of constructed PPC instructions. The host
       boundary already wrote back enabled D-cache. ICBI is virtual-index
       only on the 750; cap a large range at all 128 sets (MPC750UM 3.4.2.6). */
    uint64 first = length >= 4096 ? 0 : (uint64)address & ~UINT64_C(31);
    uint64 end = length >= 4096 ? 4096 : (uint64)address + length;
    for (uint64 line = first; line < end; line += 32)
        ROMlib_ppc_cache750_invalidate_set(&ctx->system.cache[0], (uint32)line);
}

PRIVATE unsigned ppc_collect_published_words(ppc_context* ctx, ROMlib_ppc_published_word* words, unsigned capacity,
                                             uint32 logical, uint32 physical, size_t length)
{
    int instruction_alias = FALSE;
    if (logical != physical && (ctx->system.mmu.msr & 0x20)) {
        // Look only at current BAT or ITLB mappings, without a page-table
        // walk or a change to the live translation replacement state.
        ROMlib_ppc_mmu750 mapping_state = ctx->system.mmu;
        ROMlib_ppc_mmu_result mapping;
        instruction_alias = ROMlib_ppc_mmu_translate(&mapping_state, NULL, logical,
                                                     ROMLIB_PPC_MMU_INSTRUCTION | ROMLIB_PPC_MMU_PROBE, &mapping) &&
                            mapping.physical == physical;
    }
    unsigned count = 0;
    unsigned lane = (ctx->system.mmu.msr & 1) ? 4 : 0;
    uint64 first = ((uint64)physical + 3) & ~UINT64_C(3);
    uint64 end = MIN((uint64)physical + length, first + (uint64)capacity * 4);
    for (uint64 cursor = first; cursor + 4 <= end && count < capacity; cursor += 4) {
        uint32 address = (uint32)cursor, opcode;
        const void* bytes = ppc_memory(ctx, address ^ lane, sizeof opcode, FALSE);
        if (!bytes)
            continue;
        memcpy(&opcode, bytes, sizeof opcode);
        opcode = CL(opcode);
        words[count++] = (ROMlib_ppc_published_word){address, opcode};
        if (instruction_alias && count < capacity)
            words[count++] = (ROMlib_ppc_published_word){logical + (address - physical), opcode};
    }
    return count;
}

PRIVATE int ppc_make_code_executable(ppc_context* ctx, uint32 address, uint32 length,
                                     const ROMlib_ppc_physical_bus* bus, ROMlib_ppc_bus_result* fault)
{
    if (!length)
        return 1;
    uint64 end = (uint64)address + length;
    if (end > UINT64_C(0x100000000) || (!bus && !ppc_memory(ctx, address, length, FALSE)))
        return 0;
    if (!ppc_system_enabled)
        return 1;

    unsigned capacity = MIN(ROMLIB_PPC_PUBLISHED_750_MAX_WORDS, length / 4 * (bus ? 2u : 1u));
    ROMlib_ppc_published_word* words =
        capacity && ppc_aot_enabled && ppc_jit_enabled && ctx->translations ? malloc(capacity * sizeof *words) : NULL;
    unsigned count = 0;
    // Validate the entire data range before invalidating instruction lines
    // or admitting any candidate. Optional source reads touch host RAM only.
    for (uint64 cursor = address; cursor < end;) {
        size_t amount = bus ? MIN(end - cursor, 0x1000 - (cursor & 0xFFF)) : length;
        uint32 logical = (uint32)cursor, physical = logical;
        if (bus) {
            if (!ROMlib_ppc_750_translate(&ctx->system, bus, logical, 0, fault) ||
                !ppc_memory(ctx, fault->mapping.physical, amount, FALSE)) {
                free(words);
                return 0;
            }
            physical = fault->mapping.physical;
        }
        if (words && count < capacity)
            count += ppc_collect_published_words(ctx, words + count, capacity - count, logical, physical, amount);
        cursor += amount;
    }
    ppc_publish_code(ctx, address, length);
    unsigned prepared = count ? ROMlib_ppc_translate_published_750(ctx->translations, words, count) : 0;
    if (prepared && ERROR_ENABLED_P(ERROR_TRACE_INFO))
        fprintf(stderr, "trace: PPC AOT prepared %u published instructions\n", prepared);
    free(words);
    return 1;
}

static int ppc_call_typed_native(ppc_context* ctx, const ppc_native_import* binding, ROMlib_ppc_registers* r)
{
    if (binding->function == C_SndSoundManagerVersion) {
        /* NumVersion is a four-byte structure in the Classic PPC ABI. The
           68K/native implementation uses a scalar, but PPC callers supply a
           hidden result pointer in r3, as for the wide clock results. */
        uint32* version = r->gpr[3] ? ppc_memory(ctx, r->gpr[3], sizeof *version, TRUE) : NULL;
        if (!version)
            return 0;
        *version = CL(C_SndSoundManagerVersion());
        return 1;
    }
    intptr_t args[11] = {0}, result;
    uint32 types[11], value;
    uint64 magic;
    unsigned count = 0, i, type;
    typedef intptr_t (*native_call)(intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t,
                                    intptr_t, intptr_t, intptr_t);
    magic = binding->magic >> 3;
    while (magic) {
        if (count == NELEM(types))
            return 0;
        types[count++] = magic & 7;
        magic >>= 3;
    }
    for (i = 0; i < count; ++i) {
        type = types[count - 1 - i];
        if (i < 8)
            value = r->gpr[3 + i];
        else {
            uint64 address = (uint64)r->gpr[1] + 24 + i * 4;
            uint32* p;
            if (address > UINT32_MAX)
                return 0;
            p = ppc_memory(ctx, address, 4, FALSE);
            if (!p)
                return 0;
            value = CL(*p);
        }
        switch (type) {
        case 1:
            args[i] = (int8)value;
            break;
        case 2:
            args[i] = (int16)value;
            break;
        case 3:
            args[i] = (value << 16) | (value >> 16);
            break;
        case 4:
            args[i] = value;
            break;
        case 5:
            if (!value || value == UINT32_MAX)
                args[i] = value ? -1 : 0;
            else {
                void* p = ppc_memory(ctx, value, 1, FALSE);
                if (!p)
                    return 0;
                args[i] = (intptr_t)p;
            }
            break;
        default:
            return 0;
        }
    }
    if (binding->function == C_LowercaseText || binding->function == C_CharacterByteType) {
        int16 length = r->gpr[4], script = r->gpr[5];
        if (script != 0 && script != smSystemScript) {
            snprintf(ctx->error, sizeof ctx->error, "Unsupported script %d for %s", script, binding->name);
            return 0;
        }
        if (binding->function == C_LowercaseText && length > 0) {
            void* p = ppc_memory(ctx, r->gpr[3], length, TRUE);
            if (!p)
                return 0;
            args[0] = (intptr_t)p;
        }
    }
    if (binding->function == memcpy || binding->function == memmove || binding->function == memcmp) {
        void* a = ppc_memory(ctx, r->gpr[3], r->gpr[5], binding->function != memcmp);
        void* b = ppc_memory(ctx, r->gpr[4], r->gpr[5], FALSE);
        if (!a || !b)
            return 0;
        args[0] = (intptr_t)a;
        args[1] = (intptr_t)b;
    }
    else if (binding->function == memset) {
        void* a = ppc_memory(ctx, r->gpr[3], r->gpr[5], TRUE);
        if (!a)
            return 0;
        args[0] = (intptr_t)a;
    }
    else if (binding->function == strcat || binding->function == strncat) {
        const uint32_t destination = r->gpr[3], source = r->gpr[4];
        const uint64_t limit = binding->function == strncat ? r->gpr[5] : UINT64_MAX;
        uint64_t suffix = destination, length = 0;
        if (!destination || !source)
            return 0;
        for (; suffix <= UINT32_MAX; ++suffix) {
            const unsigned char* byte = ppc_memory(ctx, suffix, 1, FALSE);
            if (!byte)
                return 0;
            if (!*byte)
                break;
        }
        for (; length < limit && source + length <= UINT32_MAX; ++length) {
            const unsigned char* byte = ppc_memory(ctx, source + length, 1, FALSE);
            if (!byte)
                return 0;
            if (!*byte)
                break;
        }
        if (suffix > UINT32_MAX || (length < limit && source + length > UINT32_MAX))
            return 0;
        unsigned char* to = ppc_memory(ctx, suffix, length + 1, TRUE);
        if (!to)
            return 0;
        if (length) {
            const void* from = ppc_memory(ctx, source, length, FALSE);
            if (!from)
                return 0;
            memmove(to, from, length);
        }
        to[length] = 0;
        return 1;
    }
    else if (binding->function == strlen || binding->function == strcmp || binding->function == strncmp) {
        const int compare = binding->function != strlen;
        const uint64 limit = binding->function == strncmp ? r->gpr[5] : UINT64_C(1) << 32;
        uint64 n;
        for (n = 0; n < limit; ++n) {
            unsigned char *a, *b = NULL;
            if ((uint64)r->gpr[3] + n > UINT32_MAX)
                return 0;
            a = ppc_memory(ctx, r->gpr[3] + n, 1, FALSE);
            if (!a)
                return 0;
            if (compare) {
                if ((uint64)r->gpr[4] + n > UINT32_MAX)
                    return 0;
                b = ppc_memory(ctx, r->gpr[4] + n, 1, FALSE);
                if (!b)
                    return 0;
                if (*a != *b) {
                    r->gpr[3] = (int)*a - (int)*b;
                    return 1;
                }
            }
            if (!*a) {
                r->gpr[3] = b ? 0 : n;
                return 1;
            }
        }
        if (binding->function != strncmp)
            return 0;
        r->gpr[3] = 0;
        return 1;
    }
    const ROMlib_ppc_registers* saved_caller = ctx->native_caller;
    ctx->native_caller = r;
    if ((binding->magic & 7) == ROMLIB_CARBON_DOUBLE_RESULT) {
        typedef double (*native_double_call)(intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t,
                                             intptr_t, intptr_t, intptr_t, intptr_t);
        r->fpr[1] = ((native_double_call)binding->function)(args[0], args[1], args[2], args[3], args[4], args[5],
                                                            args[6], args[7], args[8], args[9], args[10]);
        ctx->native_caller = saved_caller;
        return !ctx->error[0];
    }
    result = ((native_call)binding->function)(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7],
                                              args[8], args[9], args[10]);
    ctx->native_caller = saved_caller;
    if (ctx->error[0])
        return 0;
    switch (binding->magic & 7) {
    case 0:
        result = 0;
        break;
    case 1:
        result = (int8)result;
        break;
    case 2:
        result = (int16)result;
        break;
    case 3:
        result = ((uint32)result << 16) | ((uint32)result >> 16);
        break;
    case 5:
        if (result && result != -1)
            result = US_TO_SYN68K((void*)result);
        break;
    }
    r->gpr[3] = result;
    /* A native Toolbox routine may have invoked a failing PPC callback. Do
     not resume its caller as though that callback had completed normally. */
    return !ctx->error[0];
}

PRIVATE int ppc_layer_dispatch(ppc_context* ctx, const char* import, ROMlib_ppc_registers* r)
{
    static const struct {
        const char* name;
        uint32 selector;
        uint32 proc_info;
    } operations[] = {
        {"NewLayer", 1, 0x3d7a8},     {"IsLayer", 2, 0x398},      {"GetCurLayer", 3, 0xb8},
        {"SetCurLayer", 4, 0x388},    {"SwapCurLayer", 5, 0x3b8}, {"GetSubWindows", 6, 0x3b8},
        {"GetParent", 0xfff9, 0x3b8},
    };

    static const char library[] = "PrivateInterfaceLib:";
    if (strncmp(import, library, sizeof library - 1))
        return 0;
    const uint32 vector = CL(ctx->tool_vectors[0x29]);
    if (!vector || vector == (uint32)(uintptr_t)toolstuff[0x29].orig)
        return 0;

    /* Keep the saved Window Manager's layer records within its live trap
       chain. GetParent's MOVEQ #$f9 passes -7 to the word dispatcher. */
    const char* name = import + sizeof library - 1;
    for (unsigned i = 0; i < NELEM(operations); ++i) {
        if (strcmp(name, operations[i].name))
            continue;
        ROMlib_carbon_ppc_frame call_frame = {.stack = r->gpr[1]};
        memcpy(call_frame.words, r->gpr + 3, sizeof call_frame.words);
        ROMlib_ppc_registers registers = *r;
        registers.gpr[3] = vector;
        registers.gpr[4] = operations[i].proc_info;
        registers.gpr[5] = operations[i].selector;
        if (ppc_mixed_service(ctx, "CallUniversalProc", &registers, &call_frame) != 1 || ctx->error[0])
            return -1;
        r->gpr[3] = registers.gpr[3];
        return 1;
    }
    return 0;
}

PRIVATE int ppc_service(void* opaque, uint32 id, ROMlib_ppc_registers* r)
{
    ppc_context* ctx = opaque;
    ppc_import snapshot, *binding;
    if (!id || id > ctx->n_imports)
        return 0;
    snapshot = ctx->imports[id - 1];
    binding = &snapshot;
    {
        uint32 selector, answer, arguments[4] = {0};
        const char* signature = ROMlib_speech_signature(strrchr(binding->name, ':') + 1, &selector);
        if (signature) {
            for (unsigned index = 1; signature[index]; ++index)
                arguments[index - 1] = r->gpr[index + 2];
            if (!ROMlib_speech_service(selector, arguments, &answer))
                return 0;
            r->gpr[3] = signature[0] == '4' ? answer : (uint32)(int32)(int16)answer;
            return 1;
        }
    }
    if (ERROR_ENABLED_P(ERROR_TRACE_INFO))
        fprintf(stderr, "trace: PPC service %s r3=%08lx r4=%08lx\n", binding->name, (unsigned long)r->gpr[3],
                (unsigned long)r->gpr[4]);
    if (ctx->saved_desktop_device_loop_return && r->lr == ctx->saved_desktop_device_loop_return &&
        !strcmp(binding->name, "InterfaceLib:DeviceLoop") && !ppc_initialize_desktop_appearance(ctx))
        return 0;
    if (!strncmp(binding->name, "InterfaceLib:ShutDwn", 20)) {
        int handled = ppc_shutdown_service(ctx, binding->name + 13, r);
        if (handled)
            return handled > 0;
    }
    const int layer_handled = ppc_layer_dispatch(ctx, binding->name, r);
    if (layer_handled)
        return layer_handled > 0;
    if (!strncmp(binding->name, "Memory Manager:", 15)) {
        ROMlib_native_memory_backend backend = ppc_mm_backend(ctx);
        uint32 result;
        int handled =
            ROMlib_native_memory_service(&ctx->memory_manager, &backend, binding->name + 15, r->gpr + 3, &result);
        if (handled < 0)
            snprintf(ctx->error, sizeof ctx->error, "Invalid Memory Manager reference or output: %s", binding->name);
        if (handled) {
            r->gpr[3] = result;
            return handled > 0;
        }
    }
    if (!strcmp(strrchr(binding->name, ':') + 1, "DebugStr")) {
        unsigned char* text = ppc_memory(ctx, r->gpr[3], 1, FALSE);
        if (!text || !ppc_memory(ctx, r->gpr[3], (size_t)text[0] + 1, FALSE))
            return 0;
        ppc_debug_string(text + 1, text[0]);
        return 1;
    }
    if (!strcmp(binding->name, "InterfaceLib:MakeDataExecutable") ||
        !strcmp(binding->name, "CarbonLib:MakeDataExecutable"))
        return ppc_make_code_executable(ctx, r->gpr[3], r->gpr[4], NULL, NULL);
    {
        int selector = ROMlib_component_selector(strrchr(binding->name, ':') + 1);
        if (selector >= 0 && !(binding->magic & ROMLIB_CARBON_PPC_FRAME)) {
            const char* signature = ROMlib_component_signature(selector);
            uint32 arguments[6] = {0}, answer;
            unsigned i;
            for (i = 1; signature[i]; ++i)
                arguments[i - 1] = signature[i] == '2' ? r->gpr[i + 2] & 0xffff : r->gpr[i + 2];
            ROMlib_component_service(selector, arguments, &answer);
            r->gpr[3] = *signature == '2' ? (int16)answer : *signature == '4' ? answer : 0;
            return 1;
        }
    }
    if (!strcmp(binding->name, "StdCLib:sprintf") || !strcmp(binding->name, "StdCLib:snprintf"))
        return ROMlib_ppc_format(r, ppc_memory, ctx, !strcmp(binding->name, "StdCLib:snprintf"), ctx->error,
                                 sizeof ctx->error);
    if (!strcmp(binding->name, "StdCLib:__setjmp"))
        return ppc_stdclib_jump(ctx, r, TRUE);
    if (!strcmp(binding->name, "StdCLib:longjmp"))
        return ppc_stdclib_jump(ctx, r, FALSE);
    {
        /* MPLibraryIsLoaded must be safe even when no Multiprocessing runtime
           is available. Its documented availability probe returns Boolean. */
        if (!strcmp(binding->name, "MPLibrary:_MPIsFullyInitialized")) {
            r->gpr[3] = FALSE;
            return 1;
        }
        int handled = ppc_inplace_string(ctx, binding->name, r);
        if (handled)
            return handled > 0;
        handled = ppc_stdlib_memory(ctx, binding->name, r);
        if (handled)
            return handled > 0;
        handled = (binding->magic & ROMLIB_CARBON_PPC_THREAD) || !binding->function
                      ? ppc_thread_service(ctx, strrchr(binding->name, ':') + 1, r)
                      : 0;
        if (handled)
            return handled > 0;
        if (!(binding->magic & ROMLIB_CARBON_PPC_FRAME)) {
            handled = ppc_time_service(ctx, strrchr(binding->name, ':') + 1, r);
            if (handled)
                return handled > 0;
        }
        handled = ppc_math_service(ctx, binding->name, r);
        if (handled)
            return handled > 0;
        handled = ppc_mixed_service(ctx, strrchr(binding->name, ':') + 1, r, NULL);
        if (handled)
            return handled > 0;
        handled =
            binding->magic & ROMLIB_CARBON_PPC_FRAME ? 0 : ppc_cfm_service(ctx, strrchr(binding->name, ':') + 1, r);
        if (handled)
            return handled > 0;
    }
    if (!(binding->magic & ROMLIB_CARBON_PPC_FRAME) && !strcmp(strrchr(binding->name, ':') + 1, "ExitToShell")) {
        ctx->exited = TRUE;
        r->lr = ctx->halt;
        return 1;
    }
    if (!binding->function) {
        snprintf(ctx->error, sizeof ctx->error, "PowerPC application requires an unsupported API: %s (PC 0x%08lx)",
                 binding->name, (unsigned long)r->pc);
        return 0;
    }
    if (binding->magic & ROMLIB_CARBON_PPC_FRAME) {
        const ROMlib_ppc_registers* saved_caller = ctx->native_caller;
        const int already_exiting = ctx->exited;
        ctx->native_caller = r;
        int ok = ROMlib_carbon_dispatch_frame((ROMlib_carbon_frame_entry)binding->function, r->gpr + 3, r->fpr + 1,
                                              r->gpr[1], &r->fpscr);
        ctx->native_caller = saved_caller;
        if (!already_exiting && ctx->exited)
            r->lr = ctx->halt;
        if (!ok && !ctx->error[0])
            snprintf(ctx->error, sizeof ctx->error, "Native Carbon PPC frame call failed: %s", binding->name);
        return ok && !ctx->error[0];
    }
    const ppc_native_import native = {binding->name, binding->function, binding->magic};
    if (native.function == C_InitWindows && CL(ctx->tool_vectors[0x112]) != (uint32)(uintptr_t)toolstuff[0x112].orig) {
        /* The patched Window Manager allocates its update scratch region at
           system startup. A classic InitWindows import clears the low-memory
           slot, but saved BeginUpdate still uses that same region. */
        const HIDDEN_RgnHandle saved_visibility = SaveVisRgn_H;
        const int ok = ppc_call_typed_native(ctx, &native, r);
        SaveVisRgn_H = saved_visibility;
        return ok;
    }
    return ppc_call_typed_native(ctx, &native, r);
}

int ROMlib_carbon_call_romaw_service(const char* name, ROMlib_carbon_ppc_frame* call_frame)
{
    ppc_context* ctx = ppc_active_context;
    if (!ctx || !ctx->native_caller || call_frame->guest_words)
        return 0;
    if (!strcmp(name, "MakeDataExecutable"))
        return ppc_make_code_executable(ctx, call_frame->words[0], call_frame->words[1], NULL, NULL);
    const ppc_native_import* native = ppc_find_native_import(name);
    if (!native || (native->magic & ROMLIB_CARBON_PPC_FRAME))
        return 0;
    ROMlib_ppc_registers registers = *ctx->native_caller;
    registers.gpr[1] = call_frame->stack;
    memcpy(registers.gpr + 3, call_frame->words, sizeof call_frame->words);
    if (!ppc_call_typed_native(ctx, native, &registers))
        return 0;
    call_frame->words[0] = registers.gpr[3];
    return 1;
}

PRIVATE void ppc_advance_system_clock(ppc_context* ctx, uint64 now)
{
    uint64 elapsed = now - ctx->system_tick;
    ROMlib_ppc_750_advance(&ctx->system, elapsed);
    /* Functional 250-MHz core input alongside the 25-MHz TB source.
       Split multiplication to preserve every cycle for long suspensions. */
    while (elapsed > UINT64_MAX / 10) {
        ROMlib_ppc_750_advance_core(&ctx->system, (UINT64_MAX / 10) * 10);
        elapsed -= UINT64_MAX / 10;
    }
    ROMlib_ppc_750_advance_core(&ctx->system, elapsed * 10);
    ctx->system_tick = now;
}

PRIVATE void ppc_wait_forward(void* opaque, uint64_t since, unsigned interval)
{
    ppc_context* ctx = opaque;
    do {
        ppc_advance_system_clock(ctx, ROMlib_ppc_clock_ticks());
    } while (ctx->system.core_cycles - since < interval);
}

PRIVATE int ppc_power_wait(void* opaque, int waiting)
{
    ppc_context* ctx = opaque;
    /* The CFM board has no external bus masters. A pending host interrupt
       wakes the guest through INT. Drop this board-owned input on return. */
    if (!waiting) {
        ROMlib_ppc_750_set_qack(&ctx->system, 0);
        if (ctx->power_int_asserted)
            ROMlib_ppc_750_set_inputs(&ctx->system, ctx->system.inputs & ~ROMLIB_PPC_750_INT);
        ctx->power_int_asserted = 0;
        return 1;
    }
    ppc_advance_system_clock(ctx, ROMlib_ppc_clock_ticks());
    ROMlib_ppc_750_set_qack(&ctx->system, ROMlib_ppc_750_qreq(&ctx->system));
    usleep(1000);
    ppc_advance_system_clock(ctx, ROMlib_ppc_clock_ticks());
    for (unsigned i = 0; i < NELEM(cpu_state.interrupt_pending); ++i)
        if (cpu_state.interrupt_pending[i] && !(ctx->system.inputs & ROMLIB_PPC_750_INT)) {
            ctx->power_int_asserted = 1;
            ROMlib_ppc_750_set_inputs(&ctx->system, ctx->system.inputs | ROMLIB_PPC_750_INT);
            break;
        }
    return 1;
}

PRIVATE void ppc_poll(void* opaque)
{
    ppc_context* ctx = opaque;
    if (ppc_system_enabled) {
        uint64 now = ROMlib_ppc_clock_ticks();
        ppc_advance_system_clock(ctx, now);
    }
    check_virtual_interrupt();
    if (!ctx->callback_depth && !ctx->exited && !ctx->error[0])
        ROMlib_carbon_sound_input_poll();
}

PRIVATE int ppc_physical_transfer(void* opaque, uint32 address, void* bytes, unsigned size, unsigned access,
                                  unsigned wimg, uint32* cause)
{
    /* ROMAW has no external-control resource devices attached. */
    if (access & ROMLIB_PPC_PHYSICAL_EXTERNAL)
        return 0;
    /* No external cache masters: address-only broadcasts complete synchronously. */
    if (access & ROMLIB_PPC_PHYSICAL_CONTROL)
        return 1;
    void* memory = ppc_memory(opaque, address, size, (access & ROMLIB_PPC_MMU_WRITE) != 0);
    if (!memory)
        return 0;
    if (access & ROMLIB_PPC_MMU_WRITE)
        memcpy(memory, bytes, size);
    else
        memcpy(bytes, memory, size);
    return 1;
}

PRIVATE int ppc_physical_history(void* opaque, uint32 address, uint32 mask, uint32* cause)
{
    uint32 encoded;
    void* memory;
    if ((uint64)address + 8 > UINT64_C(0x100000000))
        return 0;
    memory = ppc_memory(opaque, address + 4, 4, TRUE);
    if (!memory)
        return 0;
    /* All PPC execution and callbacks are serialized on ROMAW's CPU thread. */
    memcpy(&encoded, memory, sizeof encoded);
    encoded = CL(CL(encoded) | mask);
    memcpy(memory, &encoded, sizeof encoded);
    return 1;
}

typedef struct {
    ROMlib_ppc_system750* system;
    const ROMlib_ppc_physical_bus* bus;
    ROMlib_ppc_bus_result* fault;
} ppc_mapped_memory;

PRIVATE int ppc_mapped_write_preflight(ppc_mapped_memory* access, uint32 address, size_t size)
{
    /* Check each page before storing an in-place update. A repaired page
       fault can retry the original service with its input unchanged. */
    for (size_t offset = 0; offset < size;) {
        uint32 cursor = address + (uint32)offset;
        if (!ROMlib_ppc_750_translate(access->system, access->bus, cursor,
                                      ROMLIB_PPC_MMU_WRITE | ROMLIB_PPC_MMU_PREFLIGHT, access->fault))
            return 0;
        offset += 0x1000 - (cursor & 0xFFF);
    }
    return 1;
}

PRIVATE int ppc_mapped_words(void* opaque, uint32_t address, void* record, size_t size, int writing)
{
    ppc_mapped_memory* access = opaque;
    unsigned char* bytes = record;
    if (size % 2)
        return 0;
    if (writing && !ppc_mapped_write_preflight(access, address, size))
        return 0;
    for (size_t offset = 0; offset < size; offset += 2) {
        uint32_t cursor = address + (uint32_t)offset;
        uint16_t word;
        if (writing) {
            memcpy(&word, bytes + offset, sizeof word);
            if (!ROMlib_ppc_750_write(access->system, access->bus, cursor, 2, (uint16_t)CW(word), access->fault))
                return 0;
        }
        else {
            uint64_t value;
            if (!ROMlib_ppc_750_read(access->system, access->bus, cursor, 2, 0, &value, access->fault))
                return 0;
            word = CW((uint16_t)value);
            memcpy(bytes + offset, &word, sizeof word);
        }
    }
    return 1;
}

PRIVATE int ppc_mapped_bytes(ppc_mapped_memory* access, uint32 address, unsigned char* bytes, uint32 size, int writing)
{
    return ROMlib_ppc_750_transfer_bytes(access->system, access->bus, address, bytes, size,
                                         writing ? ROMLIB_PPC_MMU_WRITE : 0, access->fault);
}

PRIVATE int ppc_mapped_copy(ppc_context* ctx, ppc_mapped_memory* access, const char* name, uint32 destination,
                            uint32 source, uint32 size)
{
    if (!size)
        return 1;
    if ((uint64)destination + size > UINT64_C(0x100000000) || (uint64)source + size > UINT64_C(0x100000000)) {
        snprintf(ctx->error, sizeof ctx->error, "Translated %s range exceeds guest address space", name);
        return 0;
    }
    if (!ppc_mapped_write_preflight(access, destination, size))
        return 0;
    /* Effective ranges can be disjoint while their physical bytes overlap.
       Snapshot before writing so neither traversal direction destroys input. */
    unsigned char local[256];
    unsigned char* snapshot = local;
    size_t loaded = 0, capacity = sizeof local;
    while (loaded < size) {
        if (loaded == capacity) {
            size_t next = capacity + MIN(capacity, size - capacity);
            unsigned char* grown = snapshot == local ? malloc(next) : realloc(snapshot, next);
            if (!grown) {
                snprintf(ctx->error, sizeof ctx->error, "Could not allocate translated %s source buffer", name);
                break;
            }
            if (snapshot == local)
                memcpy(grown, local, loaded);
            snapshot = grown;
            capacity = next;
        }
        size_t amount = MIN(size - loaded, capacity - loaded);
        if (!ppc_mapped_bytes(access, source + (uint32)loaded, snapshot + loaded, amount, 0))
            break;
        loaded += amount;
    }
    int ok = loaded == size && ppc_mapped_bytes(access, destination, snapshot, size, 1);
    if (snapshot != local)
        free(snapshot);
    return ok;
}

/* Returns zero for other imports, positive on completion and negative for a fault. */
PRIVATE int ppc_mapped_stdclib(ppc_context* ctx, ppc_mapped_memory* access, const ppc_import* binding,
                               ROMlib_ppc_registers* r)
{
    if (strncmp(binding->name, "StdCLib:", sizeof "StdCLib:" - 1))
        return 0;
    const void* function = binding->function;
    uint32 first = r->gpr[3], second = r->gpr[4], size = r->gpr[5];
    const uint64 address_space = UINT64_C(1) << 32;
    if (function == memcmp || function == strcmp || function == strncmp || function == strlen) {
        int string = function != memcmp, compare = function != strlen;
        const int bounded = !string || function == strncmp;
        uint64 limit = address_space - first;
        if (bounded)
            limit = MIN(limit, size);
        if (compare)
            limit = MIN(limit, address_space - second);
        for (uint64 offset = 0; offset < limit; ++offset) {
            uint64 left, right;
            if (!ROMlib_ppc_750_read(access->system, access->bus, first + (uint32)offset, 1, 0, &left, access->fault))
                return -1;
            if (compare) {
                if (!ROMlib_ppc_750_read(access->system, access->bus, second + (uint32)offset, 1, 0, &right,
                                         access->fault))
                    return -1;
                if (left != right) {
                    r->gpr[3] = (int)left - (int)right;
                    return 1;
                }
            }
            if (string && !left) {
                r->gpr[3] = compare ? 0 : (uint32)offset;
                return 1;
            }
        }
        if (bounded && limit == size) {
            r->gpr[3] = 0;
            return 1;
        }
        snprintf(ctx->error, sizeof ctx->error, "Translated %s range exceeds guest address space", binding->name);
        return -1;
    }
    if (function == memcpy || function == memmove)
        return ppc_mapped_copy(ctx, access, binding->name, first, second, size) ? 1 : -1;
    if (function != memset)
        return 0;
    if ((uint64)first + size > address_space) {
        snprintf(ctx->error, sizeof ctx->error, "Translated %s range exceeds guest address space", binding->name);
        return -1;
    }
    if (!ppc_mapped_write_preflight(access, first, size))
        return -1;
    unsigned char bytes[256];
    memset(bytes, (unsigned char)second, sizeof bytes);
    for (uint32 offset = 0; offset < size;) {
        size_t amount = MIN(sizeof bytes, size - offset);
        if (!ppc_mapped_bytes(access, first + offset, bytes, amount, 1))
            return -1;
        offset += amount;
    }
    return 1;
}

typedef struct {
    ppc_context* ctx;
    uint32 start;
    uint32 size;
} ppc_block_move_range;

PRIVATE void ppc_block_move_invalidate(ppc_block_move_range* range)
{
    if (range->size)
        ROMlib_destroy_blocks(range->start, range->size, TRUE);
}

PRIVATE int ppc_block_move_transfer(void* opaque, uint32 address, void* bytes, unsigned size, unsigned access,
                                    unsigned wimg, uint32* cause)
{
    ppc_block_move_range* range = opaque;
    if (!ppc_physical_transfer(range->ctx, address, bytes, size, access, wimg, cause))
        return 0;
    if ((access & ROMLIB_PPC_MMU_WRITE) && !(access & (ROMLIB_PPC_PHYSICAL_TABLE | ROMLIB_PPC_PHYSICAL_CONTROL))) {
        uint64 end = (uint64)address + size, range_end = (uint64)range->start + range->size;
        if (range->size && address <= range_end && end >= range->start) {
            range->start = MIN(range->start, address);
            range->size = MAX(end, range_end) - range->start;
        }
        else {
            ppc_block_move_invalidate(range);
            range->start = address;
            range->size = size;
        }
    }
    return 1;
}

PRIVATE int ppc_block_move_history(void* opaque, uint32 address, uint32 mask, uint32* cause)
{
    ppc_block_move_range* range = opaque;
    return ppc_physical_history(range->ctx, address, mask, cause);
}

PRIVATE int ppc_mapped_block_move(ppc_context* ctx, ppc_mapped_memory access, const char* name, int flush,
                                  ROMlib_ppc_registers* r)
{
    /* Both exports use the same OS trap and differ in the trap's cache flag. */
    if (CL(ctx->os_vectors[0x2e]) != (uint32)(uintptr_t)osstuff[0x2e].orig) {
        snprintf(ctx->error, sizeof ctx->error, "Translated arguments are unsupported for patched %s", name);
        return 0;
    }
    int32 size = (int32)r->gpr[5];
    if (size > 0) {
        ppc_block_move_range range = {ctx, 0, 0};
        ROMlib_ppc_physical_bus bus = {&range, ppc_block_move_transfer, ppc_block_move_history};
        if (flush)
            access.bus = &bus;
        int copied = ppc_mapped_copy(ctx, &access, name, r->gpr[4], r->gpr[3], size);
        if (copied || flush) {
            /* Write back before changing MemErr or checking affected 68K blocks. */
            ROMlib_ppc_bus_result sync_fault;
            int synced = ROMlib_ppc_750_host_sync(access.system, access.bus, &sync_fault);
            if (flush)
                ppc_block_move_invalidate(&range);
            if (copied && !synced) {
                *access.fault = sync_fault;
                return 0;
            }
        }
        if (!copied)
            return 0;
    }
    MemErr = CWC(noErr);
    r->gpr[3] = 0;
    return 1;
}

PRIVATE int ppc_mapped_geometry(const ppc_import* binding)
{
    static const char* const entries[] = {
        "AddPt",    "SubPt",     "SetPt",     "EqualPt",  "DeltaPoint", "SetRect", "OffsetRect", "InsetRect", "Pt2Rect",
        "PtInRect", "EmptyRect", "EqualRect", "SectRect", "UnionRect",  "MapPt",   "MapRect",    "ScalePt",
    };
    if (!(binding->magic & ROMLIB_CARBON_PPC_FRAME) ||
        (strncmp(binding->name, "InterfaceLib:", sizeof "InterfaceLib:" - 1) &&
         strncmp(binding->name, "CarbonLib:", sizeof "CarbonLib:" - 1)))
        return 0;
    const char* name = strchr(binding->name, ':') + 1;
    for (unsigned i = 0; i < NELEM(entries); ++i)
        if (!strcmp(entries[i], name))
            return 1;
    return 0;
}

PRIVATE int ppc_mapped_service(void* opaque, uint32 id, ROMlib_ppc_registers* r, ROMlib_ppc_bus_result* fault)
{
    ppc_context* ctx = opaque;
    if (!id || id > ctx->n_imports)
        return 0;
    const ppc_import* binding = &ctx->imports[id - 1];
    ROMlib_ppc_physical_bus bus = {ctx, ppc_physical_transfer, ppc_physical_history};
    ppc_mapped_memory access = {&ctx->system, &bus, fault};
    if (!strcmp(binding->name, "InterfaceLib:MakeDataExecutable") ||
        !strcmp(binding->name, "CarbonLib:MakeDataExecutable"))
        return ppc_make_code_executable(ctx, r->gpr[3], r->gpr[4], &bus, fault);
    if (!strcmp(binding->name, "InterfaceLib:BlockMove") || !strcmp(binding->name, "CarbonLib:BlockMove"))
        return ppc_mapped_block_move(ctx, access, binding->name, TRUE, r);
    if (!strcmp(binding->name, "InterfaceLib:BlockMoveData") || !strcmp(binding->name, "CarbonLib:BlockMoveData"))
        return ppc_mapped_block_move(ctx, access, binding->name, FALSE, r);
    int handled = ppc_mapped_stdclib(ctx, &access, binding, r);
    if (handled)
        return handled > 0;
    if (ppc_mapped_geometry(binding)) {
        return ROMlib_carbon_dispatch_mapped_frame((ROMlib_carbon_frame_entry)binding->function, r->gpr + 3, r->fpr + 1,
                                                   r->gpr[1], &r->fpscr, ppc_mapped_words, &access);
    }
    if (!strcmp(binding->name, "InterfaceLib:GetDateTime")) {
        LONGINT seconds;
        GetDateTime(&seconds);
        return ROMlib_ppc_750_write(&ctx->system, &bus, r->gpr[3], 4, (uint32)CL(seconds), fault);
    }
    if (!strcmp(binding->name, "InterfaceLib:DebugStr")) {
        unsigned char bytes[255];
        uint64_t length, value;
        if (!ROMlib_ppc_750_read(&ctx->system, &bus, r->gpr[3], 1, 0, &length, fault))
            return 0;
        for (unsigned i = 0; i < length; ++i) {
            if (!ROMlib_ppc_750_read(&ctx->system, &bus, r->gpr[3] + 1 + i, 1, 0, &value, fault))
                return 0;
            bytes[i] = value;
        }
        ppc_debug_string(bytes, length);
        return 1;
    }
    snprintf(ctx->error, sizeof ctx->error, "Translated PowerPC arguments are unsupported for %s", binding->name);
    return 0;
}

PRIVATE int ppc_system_service_gate(void* opaque, uint32 physical, uint32 opcode)
{
    ppc_context* ctx = opaque;
    uint32 id = opcode & 0x03FFFFFF;
    if (!id)
        return physical == ctx->halt;
    if (id > ctx->n_imports || physical != ctx->imports[id - 1].stub)
        return 0;
    return 1;
}

PRIVATE int ppc_execute(ppc_context* ctx, ROMlib_ppc_registers* r)
{
    ROMlib_ppc_environment env;
    ROMlib_ppc_physical_bus physical = {ctx, ppc_physical_transfer, ppc_physical_history};
    ppc_context* previous = ppc_active_context;
    const ROMlib_ppc_registers* previous_caller = ctx->native_caller;
    int ok;
    memset(&env, 0, sizeof env);
    env.opaque = ctx;
    env.memory = ppc_memory;
    env.service = ppc_service;
    env.poll = ppc_poll;
    env.execution_flags = ppc_jit_enabled ? ROMLIB_PPC_EXEC_JIT : 0;
    env.translation_cache = ctx->translations;
    if (ppc_system_enabled) {
        if (!ctx->system_initialized) {
            /* CFM enters an application context, rather than the reset vector.
               FP is available, translation and maskable interrupts start off. */
            memset(&ctx->system, 0, sizeof ctx->system);
            ctx->system_l2 = calloc(1, sizeof *ctx->system_l2);
            if (!ctx->system_l2 || !ROMlib_ppc_750_set_l2(&ctx->system, ctx->system_l2)) {
                free(ctx->system_l2);
                ctx->system_l2 = NULL;
                snprintf(ctx->error, sizeof ctx->error, "Cannot allocate PowerPC 750 L2 storage");
                return 0;
            }
            ctx->system.mmu.msr = 0x2000;
            ROMlib_ppc_750_set_temperature(&ctx->system, 25);
            ctx->system_tick = ctx->system.time_base = ROMlib_ppc_clock_ticks();
            ctx->system_initialized = 1;
            fprintf(stderr, "trace: PPC 750 architectural core enabled\n");
        }
        uint64 now = ROMlib_ppc_clock_ticks();
        ppc_advance_system_clock(ctx, now);
        env.system = &ctx->system;
        env.physical_bus = &physical;
        env.physical_service_gate = ppc_system_service_gate;
        env.mapped_service = ppc_mapped_service;
        env.power_wait = ppc_power_wait;
        env.wait_forward = ppc_wait_forward;
    }
    /* The reserved alias window and low-memory vector overlays retain their
     checked mapping. Only ordinary, permanently allocated guest RAM is direct. */
    uint64 linear_start = MAX(UINT64_C(0x2000), (uint64)syn68k_host_alias_base + syn68k_host_alias_size);
    if (linear_start < ROMlib_guest_memory_size) {
        env.linear_address = linear_start;
        env.linear_size = ROMlib_guest_memory_size - linear_start;
        env.linear_memory = SYN68K_TO_US_RAW(env.linear_address);
    }
    physical.linear_memory = env.linear_memory;
    physical.linear_address = env.linear_address;
    physical.linear_size = env.linear_size;
    ppc_active_context = ctx;
    /* A native caller belongs only to its suspended native service. Polls
       during guest execution must not reuse that service's old stack. */
    ctx->native_caller = NULL;
    ok = ROMlib_ppc_execute(r, &env);
    ctx->native_caller = previous_caller;
    if (env.native_blocks)
        fprintf(stderr, "trace: PPC JIT executed %llu instructions in %llu blocks (%llu compilations, %llu cached)\n",
                (unsigned long long)env.native_instructions, (unsigned long long)env.native_blocks,
                (unsigned long long)env.native_compilations, (unsigned long long)env.native_cache_hits);
    ppc_active_context = previous;
    if (!ok && !ctx->error[0]) {
        const char* entry = NULL;
        for (unsigned i = 0; i < ctx->n_imports; ++i)
            if (ctx->imports[i].stub == r->pc) {
                entry = ctx->imports[i].name;
                break;
            }
        uint32 encoded = 0;
        const void* instruction = ppc_memory(ctx, r->pc, sizeof encoded, FALSE);
        if (instruction)
            memcpy(&encoded, instruction, sizeof encoded);
        uint32 opcode = CL(encoded);
        if (entry)
            snprintf(ctx->error, sizeof ctx->error, "%s at %s (PC 0x%08lx)", env.error, entry, (unsigned long)r->pc);
        else if (instruction && (opcode & 0xfc000000) == 0x18000000)
            snprintf(ctx->error, sizeof ctx->error, "%s (service %lu/%u, PC 0x%08lx)", env.error,
                     (unsigned long)(opcode & 0x03ffffff), ctx->n_imports, (unsigned long)r->pc);
        else
            snprintf(ctx->error, sizeof ctx->error, "%s (PC 0x%08lx)", env.error, (unsigned long)r->pc);
    }
    return ok;
}

PRIVATE int ppc_code_address(ppc_context* ctx, uint32 address)
{
    ppc_fragment* fragment;
    unsigned i;
    if (ROMlib_pef_is_code(ctx->application, address))
        return 1;
    for (fragment = ctx->fragments; fragment; fragment = fragment->next)
        if (ROMlib_pef_is_code(fragment->image, address))
            return 1;
    for (i = 0; i < ctx->n_imports; ++i)
        if (ctx->imports[i].stub == address)
            return 1;
    for (i = 0; i < ctx->n_segments; ++i) {
        Handle h = ctx->segments[i];
        uint32 base = US_TO_SYN68K(STARH(h));
        if (!(address & 1) && address >= base + 40 && (uint64)address + 2 <= (uint64)base + GetHandleSize(h))
            return 1;
    }
    return 0;
}

/* Validate descriptors and transition vectors before treating a Toolbox
   callback as PPC; ordinary 68K procedures continue through the old bridge. */
PRIVATE uint32 ppc_prepare_routine_resource(ppc_context*, uint32, uint32);

PRIVATE uint32 ppc_callback_vector_depth(ppc_context* ctx, uint32 address, unsigned depth)
{
    if (depth == 16)
        return 0;
    uint16* op = ppc_memory(ctx, address, 2, FALSE);
    uint32* tv;
    if (!op)
        return 0;
    if (*op == (uint16)CWC(MIXED_MODE_TRAP)) {
        RoutineDescriptor* rd = ppc_memory(ctx, address, sizeof *rd, FALSE);
        unsigned i, count;
        if (!rd || rd->version != kRoutineDescriptorVersion)
            return 0;
        count = CW(rd->routineCount) + 1;
        if (count > 16 || !ppc_memory(ctx, address, 12 + count * sizeof(RoutineRecord), FALSE))
            return 0;
        for (i = 0; i < count; ++i)
            if (rd->routineRecords[i].ISA == cfm_native_isa(ctx)) {
                RoutineRecord* record = &rd->routineRecords[i];
                unsigned flags = CW(record->routineFlags);
                int64 entry = CL((uint32)PP_RAW(record->procDescriptor));
                if (flags & ~7)
                    return 0;
                if (flags & 1)
                    entry = (int64)address + (int32)entry;
                if (entry < 0 || entry > UINT32_MAX)
                    return 0;
                if (flags & 2) {
                    uint32 vector;
                    if (!(flags & 1) || !(vector = ppc_prepare_routine_resource(ctx, address, (uint32)entry)))
                        return 0;
                    FIELD_ASSIGN_RAW(record->procDescriptor, CL(vector));
                    record->routineFlags = CW(flags & ~3);
                    entry = vector;
                }
                address = entry;
                break;
            }
        if (i == count) {
            /* A classic NewRoutineDescriptor wrapper can contain a universal
               procedure pointer rather than raw 68K instructions. Follow only
               another validated descriptor, with bounded nesting for cycles. */
            RoutineRecord* record = &rd->routineRecords[0];
            unsigned flags = CW(record->routineFlags);
            int64 entry = CL((uint32)PP_RAW(record->procDescriptor));
            if (count != 1 || rd->routineDescriptorFlags || rd->selectorInfo || record->ISA != kM68kISA || (flags & ~1))
                return 0;
            if (flags & 1)
                entry = (int64)address + (int32)entry;
            if (entry < 0 || entry > UINT32_MAX)
                return 0;
            op = ppc_memory(ctx, entry, 2, FALSE);
            if (!op || *op != (uint16)CWC(MIXED_MODE_TRAP))
                return 0;
            return ppc_callback_vector_depth(ctx, entry, depth + 1);
        }
    }
    tv = ppc_memory(ctx, address, 8, FALSE);
    return tv && ppc_code_address(ctx, CL(tv[0])) ? address : 0;
}

PRIVATE uint32 ppc_callback_vector(ppc_context* ctx, uint32 address)
{
    return ppc_callback_vector_depth(ctx, address, 0);
}

/* Keep interrupt/native callback stacks outside the application partition.
   A cached stack remains usable when a client has filled its own heap. */
PRIVATE ppc_callback_stack* ppc_acquire_callback_stack(ppc_context* ctx)
{
    ppc_callback_stack* stack;
    for (stack = ctx->callback_stacks; stack; stack = stack->next)
        if (!stack->busy) {
            stack->busy = 1;
            return stack;
        }
    if (ctx->allocating_callback_stack) {
        snprintf(ctx->error, sizeof ctx->error, "Reentrant PowerPC callback stack allocation");
        return NULL;
    }
    stack = calloc(1, sizeof *stack);
    if (!stack)
        return NULL;
    ctx->allocating_callback_stack = 1;
    ZONE_SAVE_EXCURSION(SysZone, { stack->memory = ppc_allocate(ctx, 256 * 1024, 16, &stack->address); });
    ctx->allocating_callback_stack = 0;
    if (!stack->memory) {
        free(stack);
        return NULL;
    }
    stack->busy = 1;
    stack->next = ctx->callback_stacks;
    ctx->callback_stacks = stack;
    return stack;
}

PRIVATE int ppc_invoke_callback_stack(ppc_context* ctx, uint32 vector, uint32* args, unsigned count, uint32* result,
                                      const ROMlib_ppc_registers* caller)
{
    ROMlib_ppc_registers r;
    uint32 stack_address, *tv = ppc_memory(ctx, vector, 8, FALSE);
    uint32 caller_stack = caller ? caller->gpr[1] : 0;
    int m68k = cfm_architecture(ctx) == kMotorola68KArch;
    unsigned stack_size = caller && !m68k ? 256 : 256 * 1024;
    ppc_callback_stack* reserved = NULL;
    unsigned char* stack;
    unsigned i;
    int ok;
    if (ctx->callback_depth == 64) {
        snprintf(ctx->error, sizeof ctx->error, "PowerPC callback nesting limit exceeded");
        return 0;
    }
    /* UniversalProc is an ordinary call boundary. Its dispatch frame belongs
     below the caller's stack, rather than in a fresh application-heap block.
     Native/interrupt callbacks still require an independent stack. */
    if (caller && !m68k) {
        if (caller_stack <= stack_size || (caller_stack & 15))
            stack = NULL;
        else
            stack = ppc_memory(ctx, caller_stack - stack_size, stack_size, TRUE);
        stack_address = caller_stack - stack_size;
    }
    else {
        reserved = ppc_acquire_callback_stack(ctx);
        stack = reserved ? reserved->memory : NULL;
        stack_address = reserved ? reserved->address : 0;
    }
    if (!tv || !stack) {
        if (!ctx->error[0])
            snprintf(ctx->error, sizeof ctx->error,
                     !tv      ? "Invalid PowerPC callback vector 0x%08lx"
                     : caller ? "Invalid PowerPC callback stack 0x%08lx"
                              : "Unable to allocate PowerPC callback stack (%lu bytes)",
                     (unsigned long)(!tv      ? vector
                                     : caller ? caller_stack
                                              : stack_size));
        if (reserved)
            reserved->busy = 0;
        return 0;
    }
    if (m68k) {
        if (count > 11 || !ppc_code_address(ctx, CL(tv[0]))) {
            if (reserved)
                reserved->busy = 0;
            return 0;
        }
        M68kReg saved[16];
        ppc_context* previous = ppc_active_context;
        memcpy(saved, cpu_state.regs, sizeof saved);
        EM_A7 = stack_address + stack_size - 256;
        EM_A5 = CL(tv[1]);
        EM_A1 = vector + 4;
        for (i = count; i; --i)
            PUSHUL(args[i - 1]);
        ppc_active_context = ctx;
        ++ctx->callback_depth;
        CALL_EMULATOR(CL(tv[0]));
        --ctx->callback_depth;
        if (result)
            *result = EM_D0;
        memcpy(cpu_state.regs, saved, sizeof saved);
        ppc_active_context = previous;
        if (reserved)
            reserved->busy = 0;
        return !ctx->error[0];
    }
    memset(stack, 0, stack_size);
    if (caller)
        *(uint32*)stack = CL(caller_stack);
    memset(&r, 0, sizeof r);
    r.pc = CL(tv[0]);
    r.gpr[2] = CL(tv[1]);
    r.gpr[1] = stack_address + stack_size - 256;
    r.lr = ctx->halt;
    for (i = 0; i < count; ++i) {
        if (i < 8)
            r.gpr[i + 3] = args[i];
        else
            *(uint32*)(stack + stack_size - 256 + 24 + i * 4) = CL(args[i]);
    }
    ++ctx->callback_depth;
    ok = ppc_execute(ctx, &r);
    --ctx->callback_depth;
    if (result)
        *result = r.gpr[3];
    if (reserved)
        reserved->busy = 0;
    return ok;
}

PRIVATE int ppc_invoke_callback(ppc_context* ctx, uint32 vector, uint32* args, unsigned count, uint32* result)
{
    /* Synchronous Toolbox callbacks are ordinary calls on the application's
       stack. Only callbacks without a suspended PPC caller need a reserve. */
    return ppc_invoke_callback_stack(ctx, vector, args, count, result, ctx->native_caller);
}

static ROMlib_pef_image* ppc_callback_owner(ppc_context* ctx, uint32 vector)
{
    const uint32* entry = ppc_memory(ctx, vector, sizeof *entry, FALSE);
    if (!entry)
        return NULL;
    const uint32 code = CL(*entry);
    for (ppc_fragment* fragment = ctx->fragments; fragment; fragment = fragment->next)
        if (fragment->image && ROMlib_pef_is_code(fragment->image, code))
            return fragment->image;
    return ctx->application && ROMlib_pef_is_code(ctx->application, code) ? ctx->application : NULL;
}

static void ppc_remove_display_notifications(ppc_context* ctx, ROMlib_pef_image* owner)
{
    for (ppc_display_notification** link = &ctx->display_notifications; *link;) {
        ppc_display_notification* entry = *link;
        if (!owner || entry->owner == owner) {
            *link = entry->next;
            free(entry);
        }
        else
            link = &entry->next;
    }
}

static OSErr ppc_display_notify_lifecycle(ppc_context* ctx, uint32 procedure, uint32 refcon, int16 message)
{
    const uint32 vector = ppc_callback_vector(ctx, procedure);
    if (vector) {
        uint32 args[] = {refcon, message, 0};
        return ppc_invoke_callback(ctx, vector, args, NELEM(args), NULL) ? noErr : paramErr;
    }
    const ROMlib_carbon_ppc_frame callback = {.words = {refcon, message, 0}};
    uint32 ignored;
    return ROMlib_carbon_invoke_pascal(procedure, 0xec0, &callback, &ignored) ? noErr : paramErr;
}

static OSErr ppc_update_display_notifications(ppc_context* ctx, uint16 selector, const uint32 args[4])
{
    enum { register_basic = 0x414, register_extended = 0x7ef, remove_extended = 0x726 };

    enum { notify_installed = 1, notify_removed = 3 };

    const boolean_t extended = selector == register_extended || selector == remove_extended;
    const boolean_t install = selector == register_basic || selector == register_extended;
    const uint32 procedure = args[0], refcon = extended ? args[1] : 0;
    const uint32 process_address = args[extended ? (install ? 3 : 2) : 1];
    if (!procedure || !ppc_memory(ctx, procedure, 2, FALSE))
        return paramErr;

    ProcessSerialNumber process = {0};
    if (process_address) {
        const void* input = ppc_memory(ctx, process_address, sizeof process, FALSE);
        if (!input)
            return paramErr;
        memcpy(&process, input, sizeof process);
        if (!process.highLongOfPSN && CL(process.lowLongOfPSN) == kCurrentProcess && C_GetCurrentProcess(&process))
            return paramErr;
    }
    ppc_display_notification** link = &ctx->display_notifications;
    for (; *link; link = &(*link)->next) {
        ppc_display_notification* entry = *link;
        if (entry->procedure != procedure || entry->refcon != refcon || entry->extended != extended ||
            !PSN_EQ_P(entry->process, process))
            continue;
        if (install)
            return noErr;
        *link = entry->next;
        free(entry);
        return extended ? ppc_display_notify_lifecycle(ctx, procedure, refcon, notify_removed) : noErr;
    }
    if (!install)
        return paramErr;

    ppc_display_notification* entry = calloc(1, sizeof *entry);
    if (!entry)
        return memFullErr;
    const uint32 vector = ppc_callback_vector(ctx, procedure);
    entry->owner = vector ? ppc_callback_owner(ctx, vector) : ctx->application;
    entry->procedure = procedure;
    entry->refcon = refcon;
    entry->process = process;
    entry->extended = extended;
    *link = entry;
    return extended ? ppc_display_notify_lifecycle(ctx, procedure, refcon, notify_installed) : noErr;
}

static OSErr ppc_display_draw_desktop(ppc_context* ctx, uint32 shape, boolean_t rectangular)
{
    const GrafPtr desktop = (GrafPtr)MR(WMgrCPort);
    if (!desktop || (uint32)(uintptr_t)HIDDEN_RAW(WMgrCPort_H) == UINT32_MAX)
        return dm_sw_not_initialized;
    void* bounds = shape ? ppc_memory(ctx, shape, rectangular ? sizeof(Rect) : sizeof(HIDDEN_RgnPtr), FALSE) : NULL;
    if (!bounds)
        return paramErr;
    RgnHandle region = bounds;
    if (rectangular) {
        region = C_NewRgn();
        if (!region)
            return memFullErr;
        C_RectRgn(region, bounds);
    }
    const GrafPtr previous = thePort;
    C_SetPort(desktop);
    OSErr result = noErr;
    if (ctx->desktop_draw.procedure) {
        uint32 arguments[] = {ctx->desktop_draw_refcon, 0, US_TO_SYN68K(region)};
        const uint32 vector = ppc_callback_vector(ctx, ctx->desktop_draw.procedure);
        if (!vector || !ppc_invoke_callback(ctx, vector, arguments, NELEM(arguments), NULL))
            result = paramErr;
    }
    else if ((USE_DESKCPAT_VAR & USE_DESKCPAT_BIT) && MR(DeskCPat))
        C_FillCRgn(region, MR(DeskCPat));
    else
        C_FillRgn(region, DeskPattern);
    C_SetPort(previous);
    if (rectangular)
        C_DisposeRgn(region);
    return result;
}

/* DisplayLib calls the live ABEB vector with the selector in D0. */
PUBLIC OSErr ROMlib_ppc_display_dispatch(uint16 selector, const uint32 args[4])
{
    ppc_context* ctx = ppc_active_context;
    if (selector == 0xffff || selector == 0xfffe) {
        if (!ctx)
            return ppc_dm_first_screen_device(FALSE) ? noErr : dm_sw_not_initialized;
        const OSErr result = ppc_dm_refresh_devices(ctx);
        ctx->display_initialized = result == noErr;
        return result;
    }
    if (!ctx)
        return dm_sw_not_initialized;
    if (selector == 0x414 || selector == 0x415 || selector == 0x7ef || selector == 0x726)
        return ppc_update_display_notifications(ctx, selector, args);
    if (selector == 0x51f) {
        uint32* output = args[1] ? ppc_memory(ctx, args[1], sizeof *output, TRUE) : NULL;
        /* The Pascal trap has already decoded the stack words to host order. */
        return ppc_dm_get_display_id((GDHandle)SYN68K_TO_US_CHECK0(args[0]), output, (Boolean)args[2]);
    }
    if (selector == 0x520) {
        HIDDEN_GDHandle* output = args[1] ? ppc_memory(ctx, args[1], sizeof *output, TRUE) : NULL;
        if (!output)
            return paramErr;
        HIDDEN_RAW_SET(*output, NULL);
        const OSErr status = ppc_dm_refresh_devices(ctx);
        if (status)
            return status;
        GDHandle device = NULL;
        for (unsigned i = 0; i < ctx->display_count; ++i)
            if (ctx->displays[i].id == args[0]) {
                device = ctx->displays[i].device;
                break;
            }
        if (!device && (Boolean)args[2])
            device = MR(MainDevice);
        HIDDEN_RAW_SET(*output, RM(device));
        return device ? noErr : dm_display_not_found;
    }
    if (!ctx->display_initialized)
        return dm_sw_not_initialized;
    if (selector == 2 || selector == 3)
        return ppc_display_draw_desktop(ctx, args[0], selector == 2);
    if (selector != 0x4e2 && selector != 0x8e4)
        return -4;

    /* The saved System registers the transition vectors with operation 6
       and removes them with operation 4. */
    if (selector == 0x8e4 && ((args[0] != 4 && args[0] != 6) || args[2] >= NELEM(ctx->display_patches)))
        return paramErr;
    const uint32 procedure = selector == 0x8e4 ? (args[0] == 6 ? args[3] : 0) : args[0];
    const uint32 vector = procedure ? ppc_callback_vector(ctx, procedure) : 0;
    ROMlib_pef_image* owner = vector ? ppc_callback_owner(ctx, vector) : NULL;
    if (procedure && !owner)
        return paramErr;
    if (selector == 0x8e4) {
        ctx->display_patches[args[2]].procedure = procedure;
        ctx->display_patches[args[2]].owner = owner;
    }
    else {
        ctx->desktop_draw.procedure = procedure;
        ctx->desktop_draw.owner = owner;
        ctx->desktop_draw_refcon = args[1];
    }
    return noErr;
}

PUBLIC void* ROMlib_carbon_try_guest_memory(uint32_t address, size_t length, int write_access)
{
    ppc_context* ctx = ppc_active_context;
    if (!ctx || ctx->error[0] || length > UINT32_MAX || (uint64)address + length > UINT64_C(0x100000000))
        return NULL;
    return ppc_memory(ctx, address, length, write_access != 0);
}

PUBLIC void* ROMlib_carbon_guest_memory(uint32_t address, size_t length, int write_access)
{
    ppc_context* ctx = ppc_active_context;
    void* memory = ROMlib_carbon_try_guest_memory(address, length, write_access);
    if (!memory && ctx && !ctx->error[0])
        snprintf(ctx->error, sizeof ctx->error, "Invalid native Carbon guest memory: %s 0x%08lx (%llu bytes)",
                 write_access ? "write" : "read", (unsigned long)address, (unsigned long long)length);
    return memory;
}

PUBLIC int ROMlib_carbon_report_failure(const char* message)
{
    ppc_context* ctx = ppc_active_context;
    if (ctx && !ctx->error[0])
        snprintf(ctx->error, sizeof ctx->error, "%s", message ? message : "Native Carbon operation failed");
    return 0;
}

PUBLIC int ROMlib_carbon_invoke_callback(void* procedure, const uint32_t* arguments, unsigned count, uint32_t* result)
{
    ppc_context* ctx = ppc_active_context;
    uint32 vector, copied[32];
    if (!ctx || ctx->error[0])
        return 0;
    if (!procedure || procedure == (void*)-1 || count > NELEM(copied) || (count && !arguments) ||
        cfm_architecture(ctx) == kMotorola68KArch) {
        snprintf(ctx->error, sizeof ctx->error, "Invalid native Carbon PowerPC callback");
        return 0;
    }
    vector = ppc_callback_vector(ctx, US_TO_SYN68K(procedure));
    if (!vector) {
        if (!ctx->error[0])
            snprintf(ctx->error, sizeof ctx->error, "Invalid native Carbon PowerPC callback");
        return 0;
    }
    if (count)
        memcpy(copied, arguments, count * sizeof *copied);
    return ppc_invoke_callback(ctx, vector, copied, count, result) && !ctx->error[0];
}

PRIVATE int ppc_invoke_ppc_frame(ppc_context* ctx, uint32_t vector, ROMlib_carbon_ppc_frame* call_frame)
{
    const ROMlib_ppc_registers* caller = ctx->native_caller;
    if (!vector || !call_frame || !caller || cfm_architecture(ctx) == kMotorola68KArch ||
        call_frame->stack != caller->gpr[1] || (call_frame->stack & 15) || call_frame->stack < 16) {
        return ROMlib_carbon_report_failure("Invalid native Carbon PPC reference frame");
    }
    if (ctx->callback_depth >= 64)
        return ROMlib_carbon_report_failure("PowerPC callback nesting limit exceeded");
    const uint32* tv = ppc_memory(ctx, vector, 8, FALSE);
    if (!tv || !ppc_code_address(ctx, CL(tv[0])))
        return ROMlib_carbon_report_failure("Invalid native Carbon PPC frame callback");
    /* This is the suspended service's ordinary call boundary. Reuse its actual
       caller parameter area, including words beyond r10 and the varargs homes.
       The copied execution state keeps caller nonvolatile registers/FP status;
       the real callee owns its normal linkage/frame space below this r1. */
    ROMlib_ppc_registers registers = *caller;
    memcpy(registers.gpr + 3, call_frame->words, sizeof call_frame->words);
    memcpy(registers.fpr + 1, call_frame->floating, sizeof call_frame->floating);
    registers.fpscr = call_frame->fpscr;
    registers.pc = CL(tv[0]);
    registers.gpr[2] = CL(tv[1]);
    registers.gpr[12] = vector;
    registers.lr = ctx->halt;
    ++ctx->callback_depth;
    int ok = ppc_execute(ctx, &registers);
    --ctx->callback_depth;
    if (!ok || ctx->error[0])
        return 0;
    memcpy(call_frame->words, registers.gpr + 3, sizeof call_frame->words);
    memcpy(call_frame->floating, registers.fpr + 1, sizeof call_frame->floating);
    call_frame->fpscr = registers.fpscr;
    return 1;
}

PUBLIC int ROMlib_carbon_invoke_frame(void* procedure, ROMlib_carbon_ppc_frame* call_frame)
{
    ppc_context* ctx = ppc_active_context;
    if (!ctx || ctx->error[0])
        return 0;
    if (!procedure || procedure == (void*)-1)
        return ROMlib_carbon_report_failure("Invalid native Carbon PPC reference frame");
    return ppc_invoke_ppc_frame(ctx, ppc_callback_vector(ctx, US_TO_SYN68K(procedure)), call_frame);
}

PUBLIC int ROMlib_carbon_invoke_transition_vector(uint32_t vector, ROMlib_carbon_ppc_frame* call_frame)
{
    ppc_context* ctx = ppc_active_context;
    if (!ctx || ctx->error[0])
        return 0;
    return ppc_invoke_ppc_frame(ctx, vector, call_frame);
}

PRIVATE int ppc_reference_section(ppc_context* ctx, uint32 address, uint32 length, uint32* base, uint32* size,
                                  unsigned* kind)
{
    if (ROMlib_pef_address_section(ctx->application, address, length, base, size, kind))
        return 1;
    for (ppc_fragment* fragment = ctx->fragments; fragment; fragment = fragment->next)
        if (ROMlib_pef_address_section(fragment->image, address, length, base, size, kind))
            return 1;
    return 0;
}

PUBLIC int ROMlib_carbon_probe_reference(const char* library, const char* name, ROMlib_carbon_reference* reference)
{
    ppc_context* ctx = ppc_active_context;
    ppc_fragment* fragment;
    uint32 address, *vector, base, size;
    unsigned type, kind;
    if (reference)
        memset(reference, 0, sizeof *reference);
    if (!ctx || ctx->error[0])
        return 0;
    if (!library || !name || !reference || cfm_architecture(ctx) != kPowerPCArch)
        return 0;
    for (fragment = ctx->fragments; fragment; fragment = fragment->next)
        if (!fragment->private_copy && !strcmp(fragment->name, library))
            break;
    if (!fragment || fragment->owner->native || !fragment->owner->image)
        return 0;
    fragment = fragment->owner;
    if (!ROMlib_pef_find_symbol(fragment->image, name, &address, &type) || type != 2 || (address & 3))
        return 0;
    vector = ppc_memory(ctx, address, 8, FALSE);
    if (!vector)
        return 0;
    if ((CL(vector[0]) & 3) || !ppc_reference_section(ctx, CL(vector[0]), 4, &base, &size, &kind) || kind != 0)
        return 0;
    reference->function = vector;
    reference->code_offset = CL(vector[0]) - base;
    reference->code_size = size;
    if (ppc_reference_section(ctx, CL(vector[1]), 1, &base, &size, &kind) && (kind == 1 || kind == 2)) {
        reference->toc_offset = CL(vector[1]) - base;
        reference->toc_size = size - reference->toc_offset;
        reference->toc = ppc_memory(ctx, CL(vector[1]), reference->toc_size, FALSE);
        if (!reference->toc)
            return 0;
    }
    return 1;
}

PUBLIC int ROMlib_carbon_reference_function(const char* library, const char* name, ROMlib_carbon_reference* reference)
{
    if (ROMlib_carbon_probe_reference(library, name, reference))
        return 1;
    ppc_context* ctx = ppc_active_context;
    if (ctx && !ctx->error[0])
        snprintf(ctx->error, sizeof ctx->error, "Native Carbon requires a real reference function: %s:%s",
                 library ? library : "?", name ? name : "?");
    return 0;
}

static int ppc_matches_reference_vector(ppc_context* ctx, uint32 vector, const ROMlib_carbon_reference* reference,
                                        uint32 code_offset)
{
    if (!reference || !reference->function || code_offset >= reference->code_size)
        return 0;
    const uint32* target = vector ? ppc_memory(ctx, vector, 8, FALSE) : NULL;
    const uint32* anchor = reference->function;
    const uint32 code = CL(anchor[0]) - reference->code_offset + code_offset;
    return target && CL(target[0]) == code && target[1] == anchor[1];
}

PUBLIC int ROMlib_ppc_completion(void* proc, void* pb)
{
    ppc_context* ctx = ppc_active_context;
    uint32 vector, argument;
    if (!ctx || !(vector = ppc_callback_vector(ctx, US_TO_SYN68K(proc))))
        return 0;
    argument = US_TO_SYN68K(pb);
    ppc_invoke_callback(ctx, vector, &argument, 1, NULL);
    return 1;
}

PUBLIC int ROMlib_ppc_callback(void* proc, uint64 magic, va_list ap, intptr_t* result)
{
    ppc_context* ctx = ppc_active_context;
    uint32 vector, args[11], value, answer = 0;
    uint64 types = magic >> 3;
    unsigned count = 0, type;
    if (!ctx || !(vector = ppc_callback_vector(ctx, US_TO_SYN68K(proc))))
        return 0;
    while (types) {
        type = types & 7;
        types >>= 3;
        if (count == NELEM(args)) {
            *result = 0;
            return 1;
        }
        uintptr_t host_value = type == 5 ? (uintptr_t)va_arg(ap, void*) : va_arg(ap, uint32);
        value = (uint32)host_value;
        switch (type) {
        case 1:
            value = (int8)value;
            break;
        case 2:
            value = (int16)value;
            break;
        case 3:
            value = (value << 16) | (value >> 16);
            break;
        case 4:
            break;
        case 5:
            if (host_value && host_value != UINTPTR_MAX)
                value = US_TO_SYN68K((void*)host_value);
            break;
        default:
            *result = 0;
            return 1;
        }
        args[count++] = value;
    }
    if (!ppc_invoke_callback(ctx, vector, args, count, &answer)) {
        *result = 0;
        return 1;
    }
    switch (magic & 7) {
    case 0:
        answer = 0;
        break;
    case 1:
        answer = (int8)answer;
        break;
    case 2:
        answer = (int16)answer;
        break;
    case 3:
        answer = (answer << 16) | (answer >> 16);
        break;
    case 5:
        *result = !answer ? 0 : answer == UINT32_MAX ? -1 : (intptr_t)ppc_memory(ctx, answer, 1, FALSE);
        return 1;
    }
    *result = (int32)answer;
    return 1;
}

PRIVATE int ppc_call_vector(ppc_context* ctx, ROMlib_pef_image* image, uint32 vector, uint32 stack, uint32 argument,
                            uint32* result)
{
    ROMlib_ppc_registers r;
    uint32* tv;
    if (!vector)
        return 1;
    tv = ppc_memory(ctx, vector, 8, FALSE);
    if (!tv)
        return 0;
    if (cfm_architecture(ctx) == kMotorola68KArch) {
        uint32 entry = CL(tv[0]);
        int valid = ROMlib_pef_is_code(image, entry);
        for (unsigned i = 0; !valid && i < ctx->n_segments; ++i) {
            Handle segment = ctx->segments[i];
            uint32 base = US_TO_SYN68K(STARH(segment));
            valid = !(entry & 1) && entry >= base + 40 && (uint64)entry + 2 <= (uint64)base + GetHandleSize(segment);
        }
        if (!valid) {
            snprintf(ctx->error, sizeof ctx->error, "CFM-68K entry is outside executable sections");
            return 0;
        }
        M68kReg saved[16];
        ppc_context* previous = ppc_active_context;
        memcpy(saved, cpu_state.regs, sizeof saved);
        ppc_active_context = ctx;
        ctx->exited = 0;
        EM_A7 = stack;
        EM_A5 = CL(tv[1]);
        EM_A1 = vector + 4;
        PUSHUL(argument);
        CALL_EMULATOR(entry);
        if (result)
            *result = EM_D0;
        memcpy(cpu_state.regs, saved, sizeof saved);
        ppc_active_context = previous;
        return !ctx->error[0];
    }
    memset(&r, 0, sizeof r);
    r.pc = CL(tv[0]);
    r.gpr[2] = CL(tv[1]);
    r.gpr[1] = stack;
    r.gpr[3] = argument;
    r.lr = ctx->halt;
    fprintf(stderr, "trace: PPC call pc=%08lx toc=%08lx argument=%08lx\n", (unsigned long)r.pc, (unsigned long)r.gpr[2],
            (unsigned long)argument);
    if (!ppc_execute(ctx, &r)) {
        fprintf(stderr, "trace: PPC fault lr=%08lx ctr=%08lx r0=%08lx r1=%08lx r2=%08lx r3=%08lx r12=%08lx\n",
                (unsigned long)r.lr, (unsigned long)r.ctr, (unsigned long)r.gpr[0], (unsigned long)r.gpr[1],
                (unsigned long)r.gpr[2], (unsigned long)r.gpr[3], (unsigned long)r.gpr[12]);
        return 0;
    }
    if (result)
        *result = r.gpr[3];
    return 1;
}

/* System 7 CFragInitBlock, using the guest 32-bit layout from Apple's
   CodeFragments.h (Mac OS X 10.1.5 SDK, CarbonCore framework). */
PRIVATE int ppc_initialize(ppc_context* ctx, ROMlib_pef_image* image, const FSSpec* spec, const char* name,
                           uint32 offset, uint32 length, uint32 connection, uint32 stack, uint32 memory_address)
{
    uint32 address, result, *block;
    unsigned n = strlen(name);
    unsigned char* data;
    if (!ROMlib_pef_init(image))
        return 1;
    if (n > 63)
        return 0;
    data = ppc_allocate(ctx, 36 + sizeof *spec + n + 1, 4, &address);
    if (!data)
        return 0;
    memset(data, 0, 36 + sizeof *spec + n + 1);
    block = (uint32*)data;
    block[1] = CL(ctx->initializing_closure ? ctx->initializing_closure->id : ctx->closure);
    block[2] = CL(connection);
    block[3] = CL(memory_address ? 0 : 1); /* memory or data-fork locator */
    block[4] = CL(address + 36);
    block[5] = CL(offset);
    block[6] = CL(length);
    if (ctx->segmented && image == ctx->application) {
        block[3] = CLC(2);
        block[5] = CL(TICK("rseg"));
        block[6] = 0;
    }
    if (memory_address) {
        block[4] = CL(memory_address);
        block[5] = CL(length);
        block[6] = 0;
    }
    block[7] = CL(address + 36 + sizeof *spec);
    memcpy(data + 36, spec, sizeof *spec);
    data[36 + sizeof *spec] = n;
    memcpy(data + 37 + sizeof *spec, name, n);
    if (!ppc_call_vector(ctx, image, ROMlib_pef_init(image), stack, address, &result)) {
        if (!strcmp(name, "CarbonLib"))
            ctx->retry_system = 1;
        return 0;
    }
    if ((int16)result) {
        if (!strcmp(name, "CarbonLib")) {
            ctx->retry_system = 1;
            snprintf(ctx->error, sizeof ctx->error,
                     "CarbonLib initialization failed (error %d) under emulated System "
                     "%u.%u.%u. Check this application's system-version and memory "
                     "settings.",
                     (int16)result, (unsigned)(system_version >> 8), (unsigned)((system_version >> 4) & 15),
                     (unsigned)(system_version & 15));
        }
        else
            snprintf(ctx->error, sizeof ctx->error, "PowerPC library %s initialization failed (%d)", name,
                     (int16)result);
        return 0;
    }
    return 1;
}

/* WindowsLib from a saved System file expects that file's WDEFs and the
   localized strings in System Resources to be on the System resource chain. */
static void ppc_attach_window_system_resources(FSSpec* spec)
{
    if (ROMlib_resource_file_already_open(spec, NULL, NULL))
        return;
    const INTEGER previous = C_CurResFile();
    const INTEGER saved_error = ResErr;
    const THz saved_zone = GetZone();
    SetZone(MR(SysZone));

    const INTEGER system_file = C_FSpOpenResFile(spec, fsRdPerm);
    if (system_file != -1) {
        if (C_Count1Resources(TICK("WDEF")) && C_Count1Resources(TICK("CDEF"))) {
            Str255 name = "\020System Resources";
            FSSpec localized;
            if (!C_FSMakeFSSpec(CW(spec->vRefNum), CL(spec->parID), name, &localized) &&
                !ROMlib_resource_file_already_open(&localized, NULL, NULL)) {
                const INTEGER strings = C_FSpOpenResFile(&localized, fsRdPerm);
                if (strings != -1 && ROMlib_insert_resource_file(strings, 0))
                    C_CloseResFile(strings);
            }
            /* Leave ROMAW's built-in resources first in the System chain. */
            if (ROMlib_insert_resource_file(system_file, 0))
                C_CloseResFile(system_file);
        }
        else
            C_CloseResFile(system_file);
    }
    C_UseResFile(previous);
    ResErr = saved_error;
    SetZone(saved_zone);
}

static void ppc_share_window_manager_color_port(ppc_context* ctx, ROMlib_pef_image* image)
{
    /* The 9.2.2 initializer reuses an existing low-memory Window Manager
       port and skips the allocation that also sets its color-port slot. */
    const uint32 address = image ? ROMlib_pef_init(image) : 0;
    const uint32* init = address ? ppc_memory(ctx, address, 8, FALSE) : NULL;
    const uint32* instruction = init ? ppc_memory(ctx, CL(init[0]) + 0x800, 4, FALSE) : NULL;
    const uint32 color = (uint32)(uintptr_t)WMgrCPort, mono = (uint32)(uintptr_t)WMgrPort;
    if (!instruction || CL(*instruction) != 0x3bc22460 || !mono || mono == UINT32_MAX || !color || color == UINT32_MAX)
        return;
    const uint32 slot = CL(init[1]) + 0x2460;
    if (!ROMlib_pef_find_section(image, slot, sizeof color, NULL, NULL, NULL, NULL))
        return;
    uint32* color_port = ppc_memory(ctx, slot, sizeof *color_port, TRUE);
    if (color_port && !*color_port)
        *color_port = color;
}

static int ppc_initialize_saved_desktop(ppc_context* ctx, uint32 stack)
{
    const uint32* desktop = ppc_memory(ctx, 0x9e2, 4, FALSE);
    if (!desktop || (*desktop && CL(*desktop) != UINT32_MAX))
        return 1;
    const uint32 trap_address = CL(ctx->tool_vectors[0x112]); /* _InitWindows */
    if (trap_address == (uint32)(uintptr_t)toolstuff[0x112].orig)
        return 1;
    const uint32 vector = ppc_callback_vector(ctx, trap_address);
    const uint32* entry = vector ? ppc_memory(ctx, vector, 8, FALSE) : NULL;
    ROMlib_pef_image* image = vector ? ppc_callback_owner(ctx, vector) : NULL;
    const uint32 initializer = image ? ROMlib_pef_init(image) : 0;
    const uint32* init = initializer ? ppc_memory(ctx, initializer, 8, FALSE) : NULL;
    if (!entry || !init || CL(entry[0]) != CL(init[0]) + 0x1f8)
        return 1;
    const uint32* phase_check = ppc_memory(ctx, CL(entry[0]) + 0x10, 4, FALSE);
    if (!phase_check || CL(*phase_check) != 0x806002b6)
        return 1;

    /* The saved manager creates the desktop in its system phase. ROMAW's
       Process Manager is already active by the time the PEF is loaded. */
    AE_info_ptr expansion = (AE_info_ptr)MR(AE_info);
    if (!expansion)
        return ROMlib_carbon_report_failure("Missing Process Manager expansion memory for InitWindows");
    const INTEGER process_manager = expansion->process_manager_exists;
    const Byte application_windows = WWExist;
    expansion->process_manager_exists = 0;
    /* WindowsLib 9.2.2 creates the desktop before its first themed DeviceLoop.
       Appearance needs the new desktop, while the loop needs its theme. */
    ctx->saved_desktop_device_loop_return = CL(entry[0]) - (0x13e24 - 0x6ad8);
    fprintf(stderr, "trace: PPC initializing the saved Window Manager desktop\n");
    const int ok = ppc_call_vector(ctx, image, vector, stack, 0, NULL);
    ctx->saved_desktop_device_loop_return = 0;
    expansion->process_manager_exists = process_manager;
    WWExist = application_windows;
    return ok;
}

static int ppc_initialize_fragment(ppc_context* ctx, ppc_fragment* member, uint32 stack)
{
    if (ctx->n_initialized == NELEM(ctx->init_order))
        return 0;
    if (!member->connection && !ppc_allocate(ctx, 4, 4, &member->connection))
        return 0;
    fprintf(stderr, "trace: PPC initializing %s\n", member->name);
    /* Let an initializer resolve its own linked connection while it runs. */
    member->initialized = 1;
    member->initialization_closure = ctx->initializing_closure ? ctx->initializing_closure->id : ctx->closure;
    ppc_adopt_cf_initializers(ctx, member);
    const int saved_windows = !member->native && !strcmp(member->name, "WindowsLib");
    if (saved_windows) {
        ppc_attach_window_system_resources(&member->spec);
        ppc_share_window_manager_color_port(ctx, member->image);
    }
    if (!ppc_initialize(ctx, member->image, &member->spec, member->name, member->offset, member->length,
                        member->connection, stack, member->memory_address)) {
        member->initialized = 0;
        return 0;
    }
    member->initialized = 2;
    if (saved_windows) {
        /* ROMAW fills unused low memory with -1. The saved layer list uses
           null until the first layer is installed. */
        uint32* current_layer = ppc_memory(ctx, 0xa90, 4, TRUE);
        if (current_layer && CL(*current_layer) == UINT32_MAX)
            *current_layer = 0;
    }
    ctx->init_order[ctx->n_initialized++] = member;
    ppc_adopt_cf_system_allocator(ctx, member);
    return ppc_fragment_notify(ctx, member, member->connection, member->initialization_closure, 4);
}

static int ppc_initialize_desktop_appearance(ppc_context* ctx)
{
    const uint32* desktop = ppc_memory(ctx, 0x9e2, 4, FALSE);
    if (!desktop || !*desktop || CL(*desktop) == UINT32_MAX)
        return 1;
    for (ppc_fragment* fragment = ctx->fragments; fragment; fragment = fragment->next) {
        if (fragment->private_copy || strcmp(fragment->name, "AppearanceLib"))
            continue;
        ppc_fragment* owner = fragment->owner;
        if (owner->native || owner->initialized || !owner->on_stack)
            return 1;
        ppc_callback_stack* reserve = ppc_acquire_callback_stack(ctx);
        if (!reserve)
            return 0;
        const int ok = ppc_initialize_fragment(ctx, owner, reserve->address + 256 * 1024 - 256);
        reserve->busy = 0;
        return ok;
    }
    return 1;
}

static void ppc_order_window_manager_providers(ppc_fragment** members, unsigned count)
{
    const ppc_fragment* windows = NULL;
    ppc_fragment* providers[2] = {NULL, NULL};
    for (unsigned i = 0; i < count; ++i) {
        ppc_fragment* member = members[i];
        if (member->native)
            continue;
        if (!strcmp(member->name, "WindowsLib"))
            windows = member;
        else if (!strcmp(member->name, "ToolboxUtils"))
            providers[0] = member;
        else if (!strcmp(member->name, "PreferencesLib"))
            providers[1] = member;
    }
    if (!windows || !providers[0] || !providers[1] ||
        memcmp(&windows->spec, &providers[0]->spec, sizeof windows->spec) ||
        memcmp(&windows->spec, &providers[1]->spec, sizeof windows->spec))
        return;

    /* This saved System cohort is cyclic. WindowsLib reads the initialized
       PreferencesLib collection, which in turn uses ToolboxUtils state. */
    for (unsigned provider = 0; provider < NELEM(providers); ++provider) {
        unsigned index = 0;
        while (members[index] != providers[provider])
            ++index;
        --count;
        memmove(members + index, members + index + 1, (count - index) * sizeof *members);
        members[count] = providers[provider];
    }
}

/* Collapse cyclic fragment groups before running any member initializer.
   A simple DFS would initialize an SCC member before dependencies belonging
   to another member (CarbonLib/AppleHelpLib is one real example). */
PRIVATE int ppc_initialize_library(ppc_context* ctx, ppc_fragment* fragment, uint32 stack, unsigned depth)
{
    ppc_dependency* dependency;
    if (fragment->visit)
        return 1;
    if (depth >= 512 || ctx->graph_depth == 512)
        return 0;
    fragment->visit = fragment->low = ++ctx->graph_index;
    fragment->on_stack = TRUE;
    ctx->graph_stack[ctx->graph_depth++] = fragment;
    for (dependency = fragment->dependencies; dependency; dependency = dependency->next) {
        ppc_fragment* target = dependency->target;
        if (!target->visit) {
            if (!ppc_initialize_library(ctx, target, stack, depth + 1))
                return 0;
            if (target->on_stack && target->low < fragment->low)
                fragment->low = target->low;
        }
        else if (target->on_stack && target->visit < fragment->low)
            fragment->low = target->visit;
    }
    if (fragment->low == fragment->visit) {
        unsigned first = ctx->graph_depth - 1;
        while (ctx->graph_stack[first] != fragment)
            --first;
        ppc_order_window_manager_providers(ctx->graph_stack + first, ctx->graph_depth - first);
        while (ctx->graph_depth > first) {
            ppc_fragment* member = ctx->graph_stack[--ctx->graph_depth];
            member->on_stack = FALSE;
            if (member->initialized == 2)
                continue;
            if (!member->native && !strcmp(member->name, "MenusLib") && !ppc_initialize_saved_desktop(ctx, stack))
                return 0;
            if (!ppc_initialize_fragment(ctx, member, stack))
                return 0;
        }
    }
    return 1;
}

PRIVATE void ppc_load_aot(ppc_context* ctx, const FSSpec* spec, ROMlib_pef_image* const* images, unsigned count,
                          unsigned mode)
{
    char *path = NULL, leaf[64];
    uint64_t hash = UINT64_C(14695981039346656037);
    const unsigned char* identity = (const unsigned char*)spec;
    size_t size = offsetof(FSSpec, name) + 1 + MIN((size_t)spec->name[0], sizeof spec->name - 1);
    if (!ROMlib_filename_from_fsspec(&path, spec) && path) {
        identity = (const unsigned char*)path;
        size = strlen(path);
    }
    for (size_t n = 0; n < size; ++n)
        hash = (hash ^ identity[n]) * UINT64_C(1099511628211);
    free(path);
    if (!ROMlib_pef_translation_fingerprint(images, count, mode, &hash))
        return;
    snprintf(leaf, sizeof leaf, "ppc-aot-%016llx-v4.cache", (unsigned long long)hash);
    if (romaw_profile_path(leaf, ctx->translation_path, sizeof ctx->translation_path, NULL, 0))
        fprintf(stderr, "trace: PPC AOT loaded %u cached blocks\n",
                ROMlib_ppc_translation_cache_load(ctx->translations, ctx->translation_path));
    else
        ctx->translation_path[0] = 0;
}

PRIVATE void ppc_save_aot(ppc_context* ctx, const char* action)
{
    if (ppc_aot_enabled && ctx->translation_path[0])
        fprintf(stderr, "trace: PPC AOT %s: %d\n", action,
                ROMlib_ppc_translation_cache_save(ctx->translations, ctx->translation_path));
}

PRIVATE int ppc_extend_members(ppc_fragment** members, unsigned* count, unsigned capacity)
{
    unsigned n = *count;
    for (unsigned cursor = 0; cursor < n; ++cursor)
        for (ppc_dependency* edge = members[cursor]->dependencies; edge; edge = edge->next) {
            unsigned i;
            for (i = 0; i < n && members[i] != edge->target; ++i)
                ;
            if (i == n) {
                if (n == capacity)
                    return 0;
                members[n++] = edge->target;
            }
        }
    *count = n;
    return 1;
}

PRIVATE int ppc_link_pending(ppc_context* ctx, const FSSpec* application_spec)
{
    ppc_fragment* linked[NELEM(ctx->init_order)];
    unsigned nlinked = 0;
    int prepare = ppc_aot_enabled && ctx->translations;
    unsigned pass;
    ppc_fragment *fragment, *saved = ctx->requester;
    for (pass = 0; pass < 512; ++pass) {
        int pending = FALSE;
        for (fragment = ctx->fragments; fragment; fragment = fragment->next)
            if (fragment == fragment->owner && fragment->image && !fragment->linking) {
                pending = TRUE;
                fragment->linking = TRUE;
                ctx->requester = fragment;
                if (!ROMlib_pef_link(fragment->image)) {
                    snprintf(ctx->error, sizeof ctx->error, "%s: %s", fragment->name,
                             ROMlib_pef_error(fragment->image));
                    ctx->requester = saved;
                    return 0;
                }
                if (prepare && nlinked < NELEM(linked))
                    linked[nlinked++] = fragment;
            }
        if (!pending) {
            ctx->requester = saved;
            if (prepare && (application_spec || nlinked)) {
                ROMlib_pef_image* images[NELEM(linked) + 1];
                unsigned count = 0, members = nlinked;
                unsigned mode = ppc_system_enabled ? ROMLIB_PPC_TRANSLATE_750 : 0;
                if (application_spec)
                    images[count++] = ctx->application;
                else
                    /* Initializers can call already linked dependencies.
                       Include their code when choosing unused cache slots. */
                    ppc_extend_members(linked, &members, NELEM(linked));
                for (unsigned i = 0; i < members; ++i)
                    if (linked[i]->image)
                        images[count++] = linked[i]->image;
                if (application_spec)
                    ppc_load_aot(ctx, application_spec, images, count, mode);
                unsigned prepared = ROMlib_pef_translate_group(images, count, ctx->translations, mode);
                fprintf(stderr, "trace: PPC AOT prepared %u blocks in %u linked images\n", prepared,
                        nlinked + (application_spec != NULL));
                if (!application_spec && prepared)
                    ppc_save_aot(ctx, "published late cache");
            }
            return 1;
        }
    }
    ctx->requester = saved;
    snprintf(ctx->error, sizeof ctx->error, "Too many PowerPC library dependencies");
    return 0;
}

PRIVATE int ppc_pascal_name(ppc_context* ctx, uint32 address, char* name, unsigned capacity)
{
    unsigned char* p = ppc_memory(ctx, address, 1, FALSE);
    unsigned n;
    if (!p || (n = *p) >= capacity)
        return 0;
    p = ppc_memory(ctx, address, n + 1, FALSE);
    if (!p)
        return 0;
    memcpy(name, p + 1, n);
    name[n] = 0;
    return 1;
}

PRIVATE ppc_fragment* ppc_connection(ppc_context* ctx, uint32 id)
{
    ppc_fragment* fragment;
    if (!id)
        return NULL;
    for (fragment = ctx->fragments; fragment; fragment = fragment->next)
        if (fragment == fragment->owner && fragment->closed != 1 && fragment->connection == id)
            return fragment;
    return NULL;
}

PRIVATE ppc_closure* ppc_find_closure(ppc_context* ctx, uint32 id)
{
    for (ppc_closure* closure = ctx->closures; closure; closure = closure->next)
        if (closure->id == id && closure->state)
            return closure;
    return NULL;
}

PRIVATE ppc_fragment* ppc_connection_or_closure(ppc_context* ctx, uint32 id)
{
    ppc_fragment* fragment = ppc_connection(ctx, id);
    if (!fragment) {
        ppc_closure* closure = ppc_find_closure(ctx, id);
        if (closure && closure->root && closure->root->closed != 1)
            fragment = closure->root;
    }
    return fragment;
}

PRIVATE int ppc_pascal_argument(ppc_context* ctx, const ROMlib_ppc_registers* registers,
                                const ROMlib_carbon_ppc_frame* argument_frame, unsigned first, unsigned index,
                                uint32* value)
{
    if (argument_frame)
        return ROMlib_carbon_frame_word(argument_frame, index, value);
    if (index + first < 11) {
        *value = registers->gpr[index + first];
        return 1;
    }
    uint64 address = (uint64)registers->gpr[1] + 24 + (index + first - 3) * 4;
    if (address > UINT32_MAX - 3)
        return 0;
    const uint32* word = ppc_memory(ctx, (uint32)address, sizeof *word, FALSE);
    if (!word)
        return 0;
    *value = CL(*word);
    return 1;
}

/* Dispatched descriptors store full selector words, including sign-extended
   values, even when the original trap only inspects the low byte or word. */
PRIVATE uint32 ppc_selector_mask(unsigned width)
{
    return width == 1 ? UINT8_MAX : width == 2 ? UINT16_MAX : UINT32_MAX;
}

/* Select a prepared native entry without passing the dispatch selector as
   an ordinary target argument. RoutineRecord flags and relative entry points
   follow Apple's Mixed Mode Manager reference; unsupported layouts fail closed. */
PRIVATE int ppc_dispatch_descriptor(ppc_context* ctx, uint32 target, uint32 info, ROMlib_ppc_registers* r,
                                    const ROMlib_carbon_ppc_frame* argument_frame)
{
    enum { relative_entry = 1, use_native_isa = 4, omit_selector = 8, default_routine = 16 };

    unsigned convention = info & 15, count, i, width, n = 0;
    uint32 selector, sizes, vector, answer, args[11], record_info;
    uint16* op;
    RoutineDescriptor* rd;
    RoutineRecord *record = NULL, *fallback = NULL;
    int64 entry;
    unsigned flags;
    if (convention != kStackDispatchedPascalStackBased && convention != kD0DispatchedPascalStackBased &&
        convention != kD1DispatchedPascalStackBased)
        return 0;
    op = ppc_memory(ctx, target, 2, FALSE);
    if (!op || *op != (uint16)CWC(MIXED_MODE_TRAP))
        return 0;
    rd = ppc_memory(ctx, target, sizeof *rd, FALSE);
    if (!rd || rd->version != kRoutineDescriptorVersion)
        goto invalid;
    count = CW(rd->routineCount) + 1;
    /* This is a bounded linear search; indexable descriptor layouts are not
     implemented here. Ordinary nondispatched callbacks use their own path. */
    if (rd->routineDescriptorFlags || count > 256 ||
        !ppc_memory(ctx, target, 12 + count * sizeof(RoutineRecord), FALSE))
        goto invalid;
    width = (info >> 6) & 3;
    if (!width)
        goto invalid;
    if (rd->selectorInfo) {
        const unsigned selector_width = rd->selectorInfo >> 6;
        if ((rd->selectorInfo & 0x30) || (rd->selectorInfo & 15) != convention || !selector_width ||
            selector_width > width || (convention == kStackDispatchedPascalStackBased && selector_width != width))
            goto invalid;
        width = selector_width;
    }
    const uint32 selector_mask = ppc_selector_mask(width);
    const uint32 record_mask = rd->selectorInfo ? selector_mask : UINT32_MAX;
    selector = r->gpr[5] & selector_mask;
    for (i = 0; i < count; ++i) {
        RoutineRecord* candidate = &rd->routineRecords[i];
        if (candidate->ISA != cfm_native_isa(ctx))
            continue;
        flags = CW(candidate->routineFlags);
        if ((CL(candidate->selector) & record_mask) == selector) {
            record = candidate;
            break;
        }
        if (flags & default_routine)
            fallback = candidate;
    }
    if (!record)
        record = fallback;
    if (!record) {
        snprintf(ctx->error, sizeof ctx->error, "No PowerPC routine for Mixed Mode selector 0x%08lx",
                 (unsigned long)selector);
        return -1;
    }
    flags = CW(record->routineFlags);
    record_info = CL(record->procInfo);
    /* Prepared native records with kDontPassSelector describe the actual
     target signature, with the dispatch selector removed. Reject other
     layouts rather than silently calling the wrong routine or ABI. */
    if ((flags & ~(relative_entry | use_native_isa | omit_selector | default_routine)) || !(flags & omit_selector) ||
        (record_info & 15) != kPascalStackBased || (record_info & 0x30) != (info & 0x30) ||
        (record_info >> 6) != (info >> 8))
        goto invalid;
    entry = CL((uint32)PP_RAW(record->procDescriptor));
    if (flags & relative_entry)
        entry = (int64)target + (int32)entry;
    if (entry < 0 || entry > UINT32_MAX)
        goto invalid;
    vector = (uint32)entry;
    {
        uint32* tv = ppc_memory(ctx, vector, 8, FALSE);
        if (!tv || !ppc_code_address(ctx, CL(tv[0])))
            goto invalid;
    }
    for (sizes = info >> 8; sizes; sizes >>= 2) {
        uint32 value;
        width = sizes & 3;
        if (!width || n == NELEM(args))
            goto invalid;
        if (!ppc_pascal_argument(ctx, r, argument_frame, 6, n, &value))
            goto invalid;
        if (width == 1)
            value = (int8)value;
        if (width == 2)
            value = (int16)value;
        args[n++] = value;
    }
    if (!ppc_invoke_callback_stack(ctx, vector, args, n, &answer, r))
        return -1;
    width = (info >> 4) & 3;
    r->gpr[3] = width == 1 ? (int8)answer : width == 2 ? (int16)answer : width ? answer : 0;
    return 1;
invalid:
    if (!ctx->error[0])
        snprintf(ctx->error, sizeof ctx->error, "Unsupported or invalid dispatched PowerPC routine descriptor");
    return -1;
}

/* CFM window and control definitions put a 12-byte 68K trampoline before
   the routine descriptor. RecoverHandle needs the resource's address, while
   the entry offset is relative to the descriptor at AAFE. */
PRIVATE int ppc_definition_resource_prefix(ppc_context* ctx, uint32 descriptor)
{
    static const unsigned char trampoline[] = {0x60, 0x0a, 0, 0};
    const unsigned char* prefix = descriptor >= 12 ? ppc_memory(ctx, descriptor - 12, 12, FALSE) : NULL;
    return prefix && !memcmp(prefix, trampoline, sizeof trampoline) &&
           (!memcmp(prefix + 4, "WDEF", 4) || !memcmp(prefix + 4, "CDEF", 4));
}

static void ppc_sync_saved_menu_icon(ppc_context* ctx, uint32 vector, const uint32* args)
{
    ROMlib_carbon_reference menus;
    if (!ROMlib_carbon_probe_reference("MenusLib", "HideMenuBar", &menus) || menus.code_offset != 0x1c80 ||
        menus.code_size != 0x162a4 || !ppc_matches_reference_vector(ctx, vector, &menus, 0x3558))
        return;
    MenuHandle menu = ppc_classic_menu_handle(ctx, args[0]);
    if (!menu)
        return;
    const uint8 type = args[2];
    Handle icon = type && type <= 5 && args[3] ? ppc_memory(ctx, args[3], sizeof *icon, FALSE) : NULL;
    /* Stock MDEF drawing also needs the icon attached by the saved worker.
       Both managers borrow the original handle. */
    ROMlib_set_menu_item_icon(menu, (int16)args[1], icon ? type : 0, icon);
}

/* Entry from an actual 68K JSR or patched A-line vector to an AAFE routine
   descriptor. selectorInfo carries the dispatch convention and selector size;
   prepared records describe the selected target with that selector omitted. */
PUBLIC int ROMlib_ppc_mixed_entry(uint32 descriptor, uint32* return_address)
{
    ppc_context* ctx = ppc_active_context;
    RoutineDescriptor* rd;
    RoutineRecord *record = NULL, *fallback = NULL;
    M68kReg saved[16];
    uint32 info, sizes, args[11], widths[11], vector, answer = 0, selector = 0;
    uint32 cursor = EM_A7 + 8, argument_base, ret;
    unsigned count, i, n = 0, bytes = 0, flags, convention, dispatch = 0, selector_width = 0, result_width;
    boolean_t menu_bar_hook;
    int64 entry;
    if (!ctx || !return_address || EM_A7 > UINT32_MAX - 8)
        return 0;
    rd = ppc_memory(ctx, descriptor, 12, FALSE);
    if (!rd || rd->goMixedModeTrap != (uint16)CWC(MIXED_MODE_TRAP) || rd->version != kRoutineDescriptorVersion ||
        (rd->routineDescriptorFlags &&
         (rd->routineDescriptorFlags != 0x20 || !ppc_definition_resource_prefix(ctx, descriptor))))
        goto invalid;
    count = CW(rd->routineCount) + 1;
    if (count > 256 || !ppc_memory(ctx, descriptor, 12 + count * sizeof(RoutineRecord), FALSE))
        goto invalid;
    if (rd->selectorInfo) {
        dispatch = rd->selectorInfo & 15;
        selector_width = rd->selectorInfo >> 6;
        if ((rd->selectorInfo & 0x30) || !selector_width ||
            (dispatch != kD0DispatchedPascalStackBased && dispatch != kD1DispatchedPascalStackBased &&
             dispatch != kStackDispatchedPascalStackBased))
            goto invalid;
        if (dispatch == kStackDispatchedPascalStackBased) {
            unsigned char* p = ppc_memory(ctx, cursor, selector_width == 3 ? 4 : 2, FALSE);
            if (!p)
                goto invalid;
            selector = selector_width == 1   ? p[0]
                       : selector_width == 2 ? ((uint32)p[0] << 8) | p[1]
                                             : ((uint32)p[0] << 24) | ((uint32)p[1] << 16) | ((uint32)p[2] << 8) | p[3];
            cursor += selector_width == 3 ? 4 : 2;
        }
        else
            selector = dispatch == kD0DispatchedPascalStackBased ? EM_D0 : EM_D1;
        if (selector_width == 1)
            selector &= 0xff;
        if (selector_width == 2)
            selector &= 0xffff;
    }
    for (i = 0; i < count; ++i)
        if (rd->routineRecords[i].ISA == cfm_native_isa(ctx)) {
            if (!dispatch || (CL(rd->routineRecords[i].selector) & ppc_selector_mask(selector_width)) == selector) {
                record = &rd->routineRecords[i];
                break;
            }
            if (CW(rd->routineRecords[i].routineFlags) & 16)
                fallback = &rd->routineRecords[i];
        }
    if (!record)
        record = fallback;
    if (!record)
        goto invalid;
    flags = CW(record->routineFlags);
    info = CL(record->procInfo);
    convention = info & 15;
    /* Menus.h's uppMBarHookProcInfo is SPECIAL_CASE_PROCINFO(12): one
       stack Rect pointer, callee cleanup, and a signed short in D0. */
    menu_bar_hook = info == 0xcf;
    if ((flags & ~(1 | 2 | 4 | 8 | 16)) || (dispatch && !(flags & 8)) ||
        (convention != kPascalStackBased && convention != kCStackBased && convention != kRegisterBased &&
         !menu_bar_hook))
        goto invalid;
    entry = CL((uint32)PP_RAW(record->procDescriptor));
    if (flags & 1)
        entry = (int64)descriptor + (int32)entry;
    if (entry < 0 || entry > UINT32_MAX)
        goto invalid;
    vector = (uint32)entry;
    if (flags & 2) {
        if (!(flags & 1) || !(vector = ppc_prepare_routine_resource(ctx, descriptor, vector)))
            goto invalid;
        FIELD_ASSIGN_RAW(record->procDescriptor, CL(vector));
        record->routineFlags = CW(flags & ~3);
    }
    {
        uint32* tv = ppc_memory(ctx, vector, 8, FALSE);
        if (!tv || !ppc_code_address(ctx, CL(tv[0])))
            goto invalid;
    }
    result_width = (info >> 4) & 3;
    argument_base = cursor;
    if (menu_bar_hook) {
        const uint32* rectangle = ppc_memory(ctx, cursor, sizeof *rectangle, FALSE);
        if (!rectangle || !ppc_memory(ctx, CL(*rectangle), sizeof(Rect), FALSE))
            goto invalid;
        args[n++] = CL(*rectangle);
        cursor += sizeof *rectangle;
    }
    else if (convention == kRegisterBased) {
        static const unsigned map[] = {0, 1, 2, 3, 8, 9, 10, 11};
        unsigned field, reg;
        if (((info >> 6) & 31) >= NELEM(map))
            goto invalid;
        for (sizes = info >> 11; sizes; sizes >>= 5) {
            field = sizes & 31;
            reg = field >> 2;
            if (!(field & 3) || reg >= NELEM(map) || n == NELEM(args))
                goto invalid;
            args[n] = cpu_state.regs[map[reg]].ul.n;
            if ((field & 3) == 1)
                args[n] = (int8)args[n];
            if ((field & 3) == 2)
                args[n] = (int16)args[n];
            ++n;
        }
    }
    else {
        for (sizes = info >> 6; sizes; sizes >>= 2) {
            unsigned width = sizes & 3;
            if (!width || n == NELEM(args))
                goto invalid;
            widths[n++] = width;
            bytes += width == 3 ? 4 : 2;
        }
        if (!ppc_memory(ctx, argument_base,
                        bytes + (convention == kPascalStackBased ? (result_width == 3 ? 4
                                                                    : result_width    ? 2
                                                                                      : 0)
                                                                 : 0),
                        FALSE))
            goto invalid;
        for (i = 0; i < n; ++i) {
            unsigned at = convention == kPascalStackBased ? n - 1 - i : i;
            unsigned char* p = ppc_memory(ctx, cursor, widths[at] == 3 ? 4 : 2, FALSE);
            uint32 value = widths[at] == 1   ? p[0]
                           : widths[at] == 2 ? ((uint32)p[0] << 8) | p[1]
                                             : ((uint32)p[0] << 24) | ((uint32)p[1] << 16) | ((uint32)p[2] << 8) | p[3];
            args[at] = widths[at] == 1 ? (int8)value : widths[at] == 2 ? (int16)value : value;
            cursor += widths[at] == 3 ? 4 : 2;
        }
    }
    {
        uint32* return_slot = ppc_memory(ctx, EM_A7 + 4, 4, FALSE);
        if (!return_slot)
            goto invalid;
        ret = CL(*return_slot);
    }
    memcpy(saved, cpu_state.regs, sizeof saved);
    if (!ppc_invoke_callback(ctx, vector, args, n, &answer))
        return 0;
    if (!answer && n == 4 && info == 0x36e0 && dispatch == kD0DispatchedPascalStackBased && selector == 6)
        ppc_sync_saved_menu_icon(ctx, vector, args);
    memcpy(cpu_state.regs, saved, sizeof saved);
    if (menu_bar_hook) {
        EM_A7 = cursor;
        EM_D0 = (int16)answer;
    }
    else if (convention == kPascalStackBased) {
        EM_A7 = cursor;
        if (result_width == 1)
            WRITEUB(EM_A7, answer);
        else if (result_width == 2)
            WRITEUW(EM_A7, answer);
        else if (result_width == 3)
            WRITEUL(EM_A7, answer);
    }
    else {
        static const unsigned map[] = {0, 1, 2, 3, 8, 9, 10, 11};
        unsigned reg = convention == kRegisterBased ? map[(info >> 6) & 31] : 0;
        EM_A7 = argument_base;
        if (result_width)
            cpu_state.regs[reg].ul.n = result_width == 1 ? (int8)answer : result_width == 2 ? (int16)answer : answer;
        if (convention == kCStackBased && result_width == 3)
            EM_A0 = answer;
    }
    *return_address = ret;
    return 1;
invalid:
    if (!ctx->error[0])
        snprintf(ctx->error, sizeof ctx->error, "Unsupported or invalid 68K-to-PowerPC routine descriptor at 0x%08lx",
                 (unsigned long)descriptor);
    return 0;
}

PUBLIC void ROMlib_ppc_mixed_entry_error(void)
{
    ppc_context* ctx = ppc_active_context;
    ROMlib_win_ppc_error(ctx && ctx->error[0] ? ctx->error : "No active PowerPC context for a routine descriptor");
}

PRIVATE uint32 ppc_new_routine_descriptor(ppc_context* ctx, uint32 procedure, uint32 info, uint32 isa)
{
    if (!procedure || (isa != kM68kISA && isa != cfm_native_isa(ctx)))
        return 0;
    uint32 address;
    RoutineDescriptor* descriptor = ppc_allocate(ctx, sizeof *descriptor, 4, &address);
    if (!descriptor)
        return 0;
    memset(descriptor, 0, sizeof *descriptor);
    descriptor->goMixedModeTrap = CWC(MIXED_MODE_TRAP);
    descriptor->version = kRoutineDescriptorVersion;
    descriptor->routineRecords[0].procInfo = CL(info);
    descriptor->routineRecords[0].ISA = isa;
    FIELD_ASSIGN_RAW(descriptor->routineRecords[0].procDescriptor, CL(procedure));
    return address;
}

PRIVATE void ppc_dispose_routine_descriptor(ppc_context* ctx, uint32 address)
{
    ppc_release(ctx, ppc_memory(ctx, address, sizeof(RoutineDescriptor), FALSE));
}

PUBLIC uint32_t ROMlib_carbon_new_ppc_descriptor(uint32_t procedure, uint32_t info)
{
    ppc_context* ctx = ppc_active_context;
    return ctx && !ctx->error[0] ? ppc_new_routine_descriptor(ctx, procedure, info, kPowerPCISA) : 0;
}

PUBLIC void ROMlib_carbon_dispose_descriptor(uint32_t address)
{
    ppc_context* ctx = ppc_active_context;
    if (ctx && !ctx->error[0])
        ppc_dispose_routine_descriptor(ctx, address);
}

/* TextEdit's special ProcInfo values describe fixed register layouts, including
   output registers that the PowerPC interface exposes as pointer arguments. */
PRIVATE int ppc_textedit_hook(ppc_context* ctx, uint32 target, uint32 info, ROMlib_ppc_registers* r,
                              const ROMlib_carbon_ppc_frame* argument_frame)
{
    enum { highlight, end_of_line, width, width_with_slop, draw, hit_test, find_word, recalc, do_text };

    unsigned hook, count, first_output = 0;
    uint8_t output_widths[3] = {0};
    switch (info) {
    case 0x0f:
        hook = highlight;
        count = 2;
        break;
    case 0x1f:
        hook = end_of_line;
        count = 3;
        break;
    case 0x2f:
        hook = width;
        count = 5;
        break;
    case 0x3f:
        hook = width_with_slop;
        count = 8;
        break;
    case 0x4f:
        hook = draw;
        count = 5;
        break;
    case 0x5f:
        hook = hit_test;
        count = 9;
        first_output = 6;
        output_widths[0] = output_widths[1] = 2;
        output_widths[2] = 1;
        break;
    case 0x6f:
        hook = find_word;
        count = 6;
        first_output = 4;
        output_widths[0] = output_widths[1] = 2;
        break;
    case 0x9f:
        hook = recalc;
        count = 5;
        first_output = 2;
        output_widths[0] = output_widths[1] = output_widths[2] = 2;
        break;
    case 0xaf:
        hook = do_text;
        count = 6;
        first_output = 4;
        output_widths[0] = 4;
        output_widths[1] = 2;
        break;
    default:
        return 0;
    }

    uint32 args[9];
    for (unsigned i = 0; i < count; ++i)
        if (!ppc_pascal_argument(ctx, r, argument_frame, 5, i, &args[i]))
            return -1;
    /* These are the register extensions in the CarbonLib 1.4 public wrappers. */
    if (hook == end_of_line)
        args[0] = (uint8)args[0];
    else if (hook >= width && hook <= find_word) {
        args[0] = hook == width_with_slop ? (uint32)(int16)args[0] : (uint16)args[0];
        args[1] = hook == find_word ? (uint32)(int16)args[1] : (uint16)args[1];
        if (hook == width_with_slop || hook == hit_test)
            args[2] = (uint16)args[2];
        if (hook == width_with_slop)
            args[3] = (uint16)args[3];
    }
    else if (hook >= recalc) {
        args[1] = (uint16)args[1];
        if (hook == do_text) {
            args[2] = (uint16)args[2];
            args[3] = (uint16)args[3];
        }
    }

    uint32 answer = 0;
    uint32 vector = target ? ppc_callback_vector(ctx, target) : 0;
    if (vector) {
        if (!ppc_invoke_callback_stack(ctx, vector, args, count, &answer, r))
            return -1;
    }
    else {
        const uint16* instruction = target && !(target & 1) ? ppc_memory(ctx, target, 2, FALSE) : NULL;
        if (!instruction)
            return -1;
        if (*instruction == (uint16)CWC(MIXED_MODE_TRAP)) {
            const RoutineDescriptor* descriptor = ppc_memory(ctx, target, sizeof *descriptor, FALSE);
            if (!descriptor || descriptor->version != kRoutineDescriptorVersion || descriptor->routineCount ||
                descriptor->routineDescriptorFlags || descriptor->selectorInfo)
                return -1;
            const RoutineRecord* record = &descriptor->routineRecords[0];
            const unsigned flags = CW(record->routineFlags);
            const uint32 offset = CL((uint32)PP_RAW(record->procDescriptor));
            const int64 entry = (flags & 1) ? (int64)target + (int32)offset : offset;
            if (record->ISA != kM68kISA || flags & ~1u || CL(record->procInfo) != info || entry <= 0 ||
                entry > UINT32_MAX || (entry & 1))
                return -1;
            target = (uint32)entry;
            instruction = ppc_memory(ctx, target, 2, FALSE);
            if (!instruction || *instruction == (uint16)CWC(MIXED_MODE_TRAP))
                return -1;
        }

        unsigned char* outputs[3] = {NULL};
        for (unsigned i = 0; i < NELEM(outputs) && output_widths[i]; ++i) {
            const uint32 address = args[first_output + i];
            if (address)
                outputs[i] = ppc_memory(ctx, address, output_widths[i], TRUE);
            if (!outputs[i])
                return -1;
        }
        if (hook == highlight && (EM_A7 < 4 || !ppc_memory(ctx, EM_A7 - 4, 4, TRUE)))
            return -1;

        M68kReg saved[16];
        memcpy(saved, cpu_state.regs, sizeof saved);
        const CCRElement ccr[] = {cpu_state.ccnz, cpu_state.ccn, cpu_state.ccc, cpu_state.ccv, cpu_state.ccx};
        switch (hook) {
        case highlight:
            PUSHUL(args[0]);
            EM_A3 = args[1];
            break;
        case end_of_line:
            cpu_state.regs[0].ub.n = args[0];
            EM_A3 = args[1];
            EM_A4 = args[2];
            break;
        case width:
        case width_with_slop:
        case draw:
        case hit_test: {
            const unsigned text = hook == width_with_slop ? 4 : hook == hit_test ? 3 : 2;
            cpu_state.regs[0].uw.n = args[0];
            cpu_state.regs[1].uw.n = args[1];
            EM_A0 = args[text];
            if (hook == width_with_slop) {
                EM_D2 = args[2] | (args[3] << 16);
                EM_A2 = args[5];
            }
            else if (hook == hit_test)
                cpu_state.regs[2].uw.n = args[2];
            EM_A3 = args[text + 1 + (hook == width_with_slop)];
            EM_A4 = args[text + 2 + (hook == width_with_slop)];
            break;
        }
        case find_word:
            cpu_state.regs[0].uw.n = args[0];
            cpu_state.regs[2].uw.n = args[1];
            EM_A3 = args[2];
            EM_A4 = args[3];
            break;
        case recalc:
            EM_A3 = args[0];
            cpu_state.regs[7].uw.n = args[1];
            break;
        case do_text:
            EM_A3 = args[0];
            cpu_state.regs[3].uw.n = args[1];
            cpu_state.regs[4].uw.n = args[2];
            cpu_state.regs[7].uw.n = args[3];
            break;
        }
        CALL_EMULATOR(target);
        uint32 output_values[3] = {0};
        if (hook == end_of_line)
            answer = cpu_state.ccnz == 0;
        else if (hook == width || hook == width_with_slop)
            answer = cpu_state.regs[1].uw.n;
        else if (hook == hit_test || hook == find_word) {
            output_values[0] = EM_D0;
            output_values[1] = EM_D1;
            output_values[2] = EM_D2;
            if (hook == hit_test)
                answer = (uint16)(EM_D0 >> 16) != 0;
        }
        else if (hook == recalc) {
            output_values[0] = EM_D2;
            output_values[1] = EM_D3;
            output_values[2] = EM_D4;
        }
        else if (hook == do_text) {
            output_values[0] = EM_A0;
            output_values[1] = EM_D0;
        }
        memcpy(cpu_state.regs, saved, sizeof saved);
        cpu_state.ccnz = ccr[0];
        cpu_state.ccn = ccr[1];
        cpu_state.ccc = ccr[2];
        cpu_state.ccv = ccr[3];
        cpu_state.ccx = ccr[4];
        for (unsigned i = 0; i < NELEM(outputs) && outputs[i]; ++i)
            for (unsigned j = 0; j < output_widths[i]; ++j)
                outputs[i][j] = output_values[i] >> (8 * (output_widths[i] - j - 1));
    }
    r->gpr[3] = hook == end_of_line || hook == hit_test    ? (uint8)answer
                : hook == width || hook == width_with_slop ? (uint16)answer
                                                           : 0;
    return 1;
}

/* Bridge stack-based Universal Procedure calls into the existing 68K engine.
   ProcInfo encodes sizes, not pointer types: all parameters remain guest values. */
/* A single 68K Pascal record can be passed to CallUniversalProc by a
   PPC caller. Window Manager WDEF calls also install A3 before entering it. */
PRIVATE void* ppc_mixed_68k_pascal_target(ppc_context* ctx, uint32 descriptor, uint32 info)
{
    const RoutineDescriptor* rd = ppc_memory(ctx, descriptor, sizeof *rd, FALSE);
    if (!rd || rd->version != kRoutineDescriptorVersion || rd->routineDescriptorFlags || rd->selectorInfo ||
        rd->routineCount)
        return NULL;
    const RoutineRecord* record = rd->routineRecords;
    const unsigned flags = CW(record->routineFlags);
    if (record->ISA != kM68kISA || CL(record->procInfo) != info || (flags & ~5))
        return NULL;
    int64 address = CL((uint32)PP_RAW(record->procDescriptor));
    if (flags & 1)
        address = (int64)descriptor + (int32)address;
    if (address <= 0 || address > UINT32_MAX || (address & 1))
        return NULL;
    uint16* code = ppc_memory(ctx, (uint32)address, 2, FALSE);
    return code && *code != (uint16)CWC(MIXED_MODE_TRAP) ? code : NULL;
}

PRIVATE int ppc_mixed_service(ppc_context* ctx, const char* name, ROMlib_ppc_registers* r,
                              const ROMlib_carbon_ppc_frame* argument_frame)
{
    uint32 info, sizes, args[11] = {0}, target, value;
    uint64 magic;
    unsigned n = 0, size, convention, first = 5;
    uint32 saved_d0 = EM_D0, saved_d1 = EM_D1, stack_selector = 0;
    uint32 dispatched_selector = 0;
    unsigned stack_selector_size = 0;
    void* proc;
    if (!strcmp(name, "NewRoutineDescriptor")) {
        r->gpr[3] = ppc_new_routine_descriptor(ctx, r->gpr[3], r->gpr[4], r->gpr[5]);
        return 1;
    }
    if (!strcmp(name, "DisposeRoutineDescriptor")) {
        ppc_dispose_routine_descriptor(ctx, r->gpr[3]);
        return 1;
    }
    if (strcmp(name, "CallUniversalProc") && strcmp(name, "CallOSTrapUniversalProc"))
        return 0;
    target = r->gpr[3];
    info = r->gpr[4];
    /* Carbon calls the live _ExitToShell vector. Reaching the original
       trap ends the guest first so PPC fragment terminators can run. */
    if (!info && target && target == (uint32)(uintptr_t)toolstuff[0x1f4].orig &&
        cfm_architecture(ctx) == kPowerPCArch) {
        ctx->exited = TRUE;
        r->gpr[3] = 0;
        r->lr = ctx->halt;
        return 1;
    }
    convention = info & 15;
    if (convention == kSpecialCase)
        return ppc_textedit_hook(ctx, target, info, r, argument_frame);
    {
        int handled = ppc_dispatch_descriptor(ctx, target, info, r, argument_frame);
        if (handled)
            return handled;
    }
    if (convention == kRegisterBased) {
        static const unsigned map[] = {0, 1, 2, 3, 8, 9, 10, 11};
        unsigned destinations[NELEM(args)], widths[NELEM(args)];
        const uint16* instruction = ppc_memory(ctx, target, 2, FALSE);
        if (!instruction)
            return -1;
        for (sizes = info >> 11; sizes; sizes >>= 5) {
            size = sizes & 3;
            if (!size || n == NELEM(args) || !ppc_pascal_argument(ctx, r, argument_frame, 5, n, &args[n]))
                return -1;
            destinations[n] = map[(sizes >> 2) & 7];
            widths[n] = size;
            if (size == 1)
                args[n] = (int8)args[n];
            if (size == 2)
                args[n] = (int16)args[n];
            ++n;
        }
        unsigned reg = (info >> 6) & 31;
        if (reg > 14)
            return -1;
        uint32 answer, vector = ppc_callback_vector(ctx, target);
        if (vector) {
            if (!ppc_invoke_callback_stack(ctx, vector, args, n, &answer, r))
                return -1;
        }
        else {
            if (*instruction == (uint16)CWC(MIXED_MODE_TRAP))
                return -1;
            M68kReg saved[16];
            memcpy(saved, cpu_state.regs, sizeof saved);
            for (unsigned i = 0; i < n; ++i)
                cpu_state.regs[destinations[i]].ul.n = widths[i] == 1   ? (uint8)args[i]
                                                       : widths[i] == 2 ? (uint16)args[i]
                                                                        : args[i];
            CALL_EMULATOR(target);
            reg = reg < 4 ? reg : reg < 8 ? reg + 4 : reg < 12 ? reg - 4 : reg;
            answer = cpu_state.regs[reg].ul.n;
            memcpy(cpu_state.regs, saved, sizeof saved);
        }
        size = (info >> 4) & 3;
        r->gpr[3] = size == 1 ? (int8)answer : size == 2 ? (int16)answer : size ? answer : 0;
        return 1;
    }
    if (convention == kCStackBased || convention == kThinkCStackBased) {
        M68kReg saved[16];
        unsigned widths[11], i;
        uint32 vector = ppc_callback_vector(ctx, target), answer;
        for (sizes = info >> 6; sizes; sizes >>= 2) {
            size = sizes & 3;
            if (!size || n == NELEM(args) || !ppc_pascal_argument(ctx, r, argument_frame, 5, n, &args[n]))
                return -1;
            widths[n] = size;
            ++n;
        }
        if (vector) {
            if (!ppc_invoke_callback_stack(ctx, vector, args, n, &answer, r))
                return -1;
        }
        else {
            if (!ppc_memory(ctx, target, 2, FALSE))
                return -1;
            memcpy(saved, cpu_state.regs, sizeof saved);
            for (i = n; i; --i)
                switch (widths[i - 1]) {
                case 1:
                    PUSHUB(args[i - 1]);
                    break;
                case 2:
                    PUSHUW(args[i - 1]);
                    break;
                case 3:
                    PUSHUL(args[i - 1]);
                    break;
                }
            CALL_EMULATOR(target);
            answer = EM_D0;
            memcpy(cpu_state.regs, saved, sizeof saved);
        }
        size = (info >> 4) & 3;
        r->gpr[3] = size == 1 ? (int8)answer : size == 2 ? (int16)answer : size ? answer : 0;
        return 1;
    }
    if (convention != kPascalStackBased && convention != kD0DispatchedPascalStackBased &&
        convention != kD1DispatchedPascalStackBased && convention != kStackDispatchedPascalStackBased) {
        snprintf(ctx->error, sizeof ctx->error, "Unsupported Mixed Mode convention %u", convention);
        return -1;
    }
    proc = ppc_memory(ctx, target, 2, FALSE);
    if (!proc)
        return -1;
    {
        uint32 vector = ppc_callback_vector(ctx, target);
        if (vector) {
            sizes = info >> 6;
            if (argument_frame && convention != kPascalStackBased) {
                uint32 selector = r->gpr[5];
                size = sizes & 3;
                args[n++] = size == 1 ? (int8)selector : size == 2 ? (int16)selector : selector;
                sizes >>= 2;
            }
            for (unsigned index = 0; sizes; sizes >>= 2, ++index) {
                size = sizes & 3;
                if (!size || n == 11)
                    return -1;
                if (!ppc_pascal_argument(ctx, r, argument_frame, 5, index, &args[n]))
                    return -1;
                if (size == 1)
                    args[n] = (int8)args[n];
                if (size == 2)
                    args[n] = (int16)args[n];
                ++n;
            }
            if (!ppc_invoke_callback_stack(ctx, vector, args, n, &value, r))
                return -1;
            size = (info >> 4) & 3;
            r->gpr[3] = size == 1 ? (int8)value : size == 2 ? (int16)value : size ? value : 0;
            return 1;
        }
        if (*(uint16*)proc == (uint16)CWC(MIXED_MODE_TRAP)) {
            if (convention != kPascalStackBased || !(proc = ppc_mixed_68k_pascal_target(ctx, target, info)))
                return -1;
        }
    }
    size = (info >> 4) & 3;
    magic = size == 3 ? 4 : size;
    sizes = info >> 6;
    if (convention != kPascalStackBased) {
        unsigned selector_size = sizes & 3;
        uint32 selector = r->gpr[first++];
        if (!selector_size)
            return -1;
        if (selector_size == 1)
            selector &= 0xff;
        if (selector_size == 2)
            selector &= 0xffff;
        if (convention != kStackDispatchedPascalStackBased)
            dispatched_selector = selector;
        else {
            stack_selector = selector;
            stack_selector_size = selector_size;
        }
        sizes >>= 2;
    }
    for (; sizes; sizes >>= 2) {
        size = sizes & 3;
        if (!size || n == 11)
            return -1;
        magic |= (uint64)(size == 3 ? 4 : size) << (3 * (n + 1));
        if (!ppc_pascal_argument(ctx, r, argument_frame, first, n, &args[n]))
            return -1;
        ++n;
    }
    if (convention == kD0DispatchedPascalStackBased && target == (uint32)tooltraptable[0x3eb] &&
        (int8)dispatched_selector == -10) {
        /* Display Manager's DMDeskClick offers the event to desktop-animation
           components. This board installs none, so Finder must handle it.
           Compare the native trap address so a guest patch still runs. */
        if (info != 0x3d8 || n != 1)
            return -1;
        r->gpr[3] = FALSE;
        EM_D0 = saved_d0;
        EM_D1 = saved_d1;
        return 1;
    }
    if (stack_selector_size && target == (uint32)tooltraptable[0x8f] && !ROMlib_osdispatch_available(stack_selector)) {
        unsigned i;
        fprintf(stderr, "trace: unsupported OSDispatch selector=%04lx ProcInfo=%08lx caller=%08lx arguments=%u",
                (unsigned long)stack_selector, (unsigned long)info, (unsigned long)r->lr, n);
        for (i = 0; i < n; ++i)
            fprintf(stderr, " %08lx", (unsigned long)args[i]);
        fputc('\n', stderr);
        snprintf(
            ctx->error, sizeof ctx->error,
            "PowerPC application requires unsupported OSDispatch selector 0x%04lx (ProcInfo 0x%08lx, caller 0x%08lx)",
            (unsigned long)stack_selector, (unsigned long)info, (unsigned long)r->lr);
        EM_D0 = saved_d0;
        EM_D1 = saved_d1;
        return -1;
    }
    if ((convention == kD0DispatchedPascalStackBased || convention == kD1DispatchedPascalStackBased) &&
        target == (uint32)tooltraptable[0x254]) {
        uint32 selector = dispatched_selector;
        if (!ROMlib_tsm_dispatch_available(selector) || (selector == 8 && n != 4) || (selector == 0x1d && n != 1) ||
            (selector == 0x20 && (n != 3 || args[0] || !args[1] || !args[2]))) {
            snprintf(
                ctx->error, sizeof ctx->error,
                "PowerPC application requires unsupported TextServicesDispatch selector 0x%04lx (ProcInfo 0x%08lx, caller 0x%08lx)",
                (unsigned long)selector, (unsigned long)info, (unsigned long)r->lr);
            EM_D0 = saved_d0;
            EM_D1 = saved_d1;
            return -1;
        }
    }
    if (stack_selector_size) {
        if (n == NELEM(args))
            return -1;
        args[n] = stack_selector;
        magic |= (uint64)(stack_selector_size == 3 ? 4 : stack_selector_size) << (3 * (++n));
    }
    if (convention == kD0DispatchedPascalStackBased)
        EM_D0 = dispatched_selector;
    else if (convention == kD1DispatchedPascalStackBased)
        EM_D1 = dispatched_selector;
    r->gpr[3] = ROMlib_CToPascalCall64(proc, magic, args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                                       args[7], args[8], args[9], args[10]);
    EM_D0 = saved_d0;
    EM_D1 = saved_d1;
    return 1;
}

PRIVATE int ppc_carbon_pascal_call(ppc_context* ctx, uint32_t procedure, uint32_t info, uint32_t selector,
                                   const ROMlib_carbon_ppc_frame* call_frame, uint32_t* result)
{
    unsigned convention = info & 15;
    int dispatched = convention == kD0DispatchedPascalStackBased || convention == kD1DispatchedPascalStackBased ||
                     convention == kStackDispatchedPascalStackBased;
    if (!ctx->native_caller || !call_frame || !result || (!dispatched && convention != kPascalStackBased) ||
        (dispatched && !(info & 0xc0)) || (info >> 28))
        return ROMlib_carbon_report_failure("Invalid Carbon Pascal call frame");
    for (uint32_t widths = info >> 6; widths; widths >>= 2)
        if (!(widths & 3))
            return ROMlib_carbon_report_failure("Invalid Carbon Pascal parameter width");

    ROMlib_ppc_registers registers = *ctx->native_caller;
    registers.gpr[3] = procedure;
    registers.gpr[4] = info;
    if (dispatched)
        registers.gpr[5] = selector;
    if (ppc_mixed_service(ctx, "CallUniversalProc", &registers, call_frame) != 1 || ctx->error[0])
        return ROMlib_carbon_report_failure("Carbon Pascal invocation failed");
    *result = registers.gpr[3];
    return 1;
}

static int ppc_shutdown_service(ppc_context* ctx, const char* name, ROMlib_ppc_registers* r)
{
    enum { boot_volume_unmount = 16 };

    const int install = !strcmp(name, "ShutDwnInstall");
    const int remove = !strcmp(name, "ShutDwnRemove");
    const int power = !strcmp(name, "ShutDwnPower");
    if (!install && !remove && !power && strcmp(name, "ShutDwnStart"))
        return 0;

    const unsigned trap_index = 0xa895 & 0x3ff;
    const uint32 procedure = CL(ctx->tool_vectors[trap_index]);
    if (procedure != (uint32)(uintptr_t)toolstuff[trap_index].orig) {
        const uint32 selector = install ? 3 : remove ? 4 : power ? 1 : 2;
        const uint32 info = kStackDispatchedPascalStackBased | (2 << 6) |
                            (install  ? (3 << 8) | (2 << 10)
                             : remove ? (3 << 8)
                                      : 0);
        ROMlib_carbon_ppc_frame call = {.words = {r->gpr[3], r->gpr[4]}};
        const ROMlib_ppc_registers* saved_caller = ctx->native_caller;
        ctx->native_caller = r;
        uint32 ignored;
        const int ok = ppc_carbon_pascal_call(ctx, procedure, info, selector, &call, &ignored);
        ctx->native_caller = saved_caller;
        if (ctx->exited)
            r->lr = ctx->halt;
        return ok ? 1 : -1;
    }

    if (install || remove) {
        const uint32 address = r->gpr[3];
        uint32 code = address;
        if (install) {
            if (ctx->shutting_down || !address)
                return 1;
            if (!ppc_memory(ctx, address, 2, FALSE))
                return -1;
            const uint32 vector = ppc_callback_vector(ctx, address);
            const uint32* transition = vector ? ppc_memory(ctx, vector, 8, FALSE) : NULL;
            if (transition)
                code = CL(transition[0]);
        }
        ppc_shutdown_callback** link = &ctx->shutdown_callbacks;
        while (*link && (*link)->procedure != address)
            link = &(*link)->next;
        if (remove && *link) {
            ppc_shutdown_callback* entry = *link;
            *link = entry->next;
            free(entry);
        }
        if (remove)
            return 1;
        if (!*link) {
            *link = calloc(1, sizeof **link);
            if (!*link)
                return -1;
        }
        **link = (ppc_shutdown_callback){.procedure = address, .code = code, .flags = r->gpr[4], .next = (*link)->next};
        return 1;
    }

    if (ctx->shutting_down)
        return 1;
    ctx->shutting_down = TRUE;
    const unsigned kind = power ? sdOnPowerOff : sdOnRestart;
    const unsigned stages[] = {sdOnDrivers, sdOnUnmount, boot_volume_unmount, kind};
    const unsigned stage_mask = sdOnDrivers | sdOnUnmount | boot_volume_unmount;
    for (unsigned i = 0; i < NELEM(stages); ++i) {
        const unsigned stage = stages[i];
        ppc_shutdown_callback** link = &ctx->shutdown_callbacks;
        while (*link) {
            ppc_shutdown_callback* entry = *link;
            const unsigned when = entry->flags & stage_mask;
            const unsigned kinds = entry->flags & sdOnRestartOrPower;
            if (!(when ? (when & stage) != 0 : stage == kind) || (kinds && !(kinds & kind))) {
                link = &entry->next;
                continue;
            }
            const uint32 callback = entry->procedure;
            *link = entry->next;
            free(entry);
            /* CallUniversalProc marshals the register-based D0 argument for
               68K handlers and the corresponding PPC argument for UPPs. */
            ROMlib_ppc_registers registers = *r;
            registers.gpr[3] = callback;
            registers.gpr[4] = 0x1002;
            registers.gpr[5] = stage;
            if (ppc_mixed_service(ctx, "CallUniversalProc", &registers, NULL) != 1 || ctx->error[0])
                return -1;
            /* Callback code may remove other entries. Rescan the live list. */
            link = &ctx->shutdown_callbacks;
        }
    }
    ctx->exited = TRUE;
    r->lr = ctx->halt;
    return 1;
}

static void ppc_sweep_folder_notifications(ppc_context* ctx)
{
    if (ctx->folder_notification_depth)
        return;
    for (ppc_folder_notification** link = &ctx->folder_notifications; *link;) {
        ppc_folder_notification* entry = *link;
        if (entry->removed) {
            *link = entry->next;
            free(entry);
        }
        else
            link = &entry->next;
    }
}

static void ppc_remove_folder_notifications(ppc_context* ctx, ROMlib_pef_image* owner)
{
    for (ppc_folder_notification* entry = ctx->folder_notifications; entry; entry = entry->next)
        if (!owner || entry->owner == owner)
            entry->removed = TRUE;
    ppc_sweep_folder_notifications(ctx);
}

static int ppc_call_folder_notifications(ppc_context* ctx, const uint32 args[3], uint32* result)
{
    ppc_folder_notification* last = ctx->folder_notifications;
    while (last && last->next)
        last = last->next;

    ++ctx->folder_notification_depth;
    *result = noErr;
    int ok = 1;
    for (ppc_folder_notification* entry = ctx->folder_notifications; entry; entry = entry->next) {
        if (!entry->removed) {
            ROMlib_carbon_ppc_frame callback = {.words = {args[0], args[1], entry->refcon}};
            uint32 status;
            ok = ROMlib_carbon_invoke_pascal(entry->procedure, 0xff0, &callback, &status);
            if (!ok)
                break;
            if (status && (args[2] & UINT32_C(0x80000000))) {
                *result = status;
                break;
            }
        }
        if (entry == last)
            break;
    }
    --ctx->folder_notification_depth;
    /* Entries removed while a callback ran stay linked until dispatch ends. */
    ppc_sweep_folder_notifications(ctx);
    return ok;
}

static OSErr ppc_update_folder_notifications(ppc_context* ctx, uint32 procedure, uint32 refcon, boolean_t install)
{
    if (!procedure || !ppc_memory(ctx, procedure, 2, FALSE))
        return paramErr;
    ppc_folder_notification** tail = &ctx->folder_notifications;
    for (; *tail; tail = &(*tail)->next) {
        ppc_folder_notification* entry = *tail;
        if (!entry->removed && entry->procedure == procedure && entry->refcon == refcon) {
            if (!install) {
                if (ctx->folder_notification_depth)
                    entry->removed = TRUE;
                else {
                    *tail = entry->next;
                    free(entry);
                }
            }
            return noErr;
        }
    }
    if (!install)
        return paramErr;
    ppc_folder_notification* entry = calloc(1, sizeof *entry);
    if (!entry)
        return memFullErr;
    const uint32 vector = ppc_callback_vector(ctx, procedure);
    entry->owner = vector ? ppc_callback_owner(ctx, vector) : ctx->application;
    entry->procedure = procedure;
    entry->refcon = refcon;
    *tail = entry;
    return noErr;
}

PUBLIC int ROMlib_carbon_toolbox_vector_matches(uint32_t address, const ROMlib_carbon_reference* reference,
                                               uint32_t code_offset)
{
    ppc_context* ctx = ppc_active_context;
    if (!ctx || ctx->error[0] || (address & 3) || address < 0xe00 || address >= 0x1e00)
        return 0;
    const uint32* slot = ppc_memory(ctx, address, sizeof *slot, FALSE);
    const uint32 vector = slot ? ppc_callback_vector(ctx, CL(*slot)) : 0;
    return ppc_matches_reference_vector(ctx, vector, reference, code_offset);
}

PUBLIC int ROMlib_carbon_call_toolbox_vector(uint32_t address, uint32_t info, uint32_t selector,
                                             const ROMlib_carbon_ppc_frame* call_frame, uint32_t* result)
{
    ppc_context* ctx = ppc_active_context;
    if (!ctx || ctx->error[0])
        return 0;
    if ((address & 3) || address < 0xe00 || address >= 0x1e00)
        return ROMlib_carbon_report_failure("Invalid Carbon Toolbox vector address");
    const uint32* slot = ppc_memory(ctx, address, sizeof *slot, FALSE);
    if (!slot)
        return ROMlib_carbon_report_failure("Invalid Carbon Toolbox vector address");
    if (address == 0xe8c && CL(*slot) == (uint32)(uintptr_t)toolstuff[0x23].orig && call_frame && result) {
        const uint32* args = call_frame->words;
        if (selector == 0x631)
            return ppc_call_folder_notifications(ctx, args, result);
        if (selector == 0x62f || selector == 0x430) {
            *result = ppc_update_folder_notifications(ctx, args[0], args[1], selector == 0x62f);
            return 1;
        }
    }
    return ppc_carbon_pascal_call(ctx, CL(*slot), info, selector, call_frame, result);
}

PUBLIC int ROMlib_carbon_call_original_toolbox_trap(uint16_t trap_index, uint32_t info, uint32_t selector,
                                                    const ROMlib_carbon_ppc_frame* call_frame, uint32_t* result)
{
    ppc_context* ctx = ppc_active_context;
    if (!ctx || ctx->error[0])
        return 0;
    if (trap_index >= NELEM(toolstuff) || !toolstuff[trap_index].orig)
        return ROMlib_carbon_report_failure("Invalid original Carbon Toolbox trap");
    return ppc_carbon_pascal_call(ctx, (uint32_t)(uintptr_t)toolstuff[trap_index].orig, info, selector, call_frame,
                                  result);
}

PUBLIC int ROMlib_carbon_call_register_vector(uint32_t address, uint32_t info, uint32_t trap_word, unsigned trap_slot,
                                              const ROMlib_carbon_ppc_frame* call_frame, uint32_t* result)
{
    ppc_context* ctx = ppc_active_context;
    if (!ctx || ctx->error[0])
        return 0;
    if (!ctx->native_caller || !call_frame || !result || (address & 3) || (info & 15) != kRegisterBased ||
        (trap_word ? ((trap_word & ~0x7ffu) != 0xa000 || address != 0x400 + 4 * (trap_word & 0xff) || trap_slot >= 6)
                   : (address < 0xe00 || address >= 0x1e00)))
        return ROMlib_carbon_report_failure("Invalid Carbon register trap call");
    const uint32* slot = ppc_memory(ctx, address, sizeof *slot, FALSE);
    if (!slot)
        return ROMlib_carbon_report_failure("Invalid Carbon register trap vector");

    ROMlib_ppc_registers registers = *ctx->native_caller;
    registers.gpr[3] = CL(*slot);
    registers.gpr[4] = info;
    uint32* arguments = registers.gpr + 5;
    if (trap_word) {
        memcpy(arguments, call_frame->words, trap_slot * sizeof(uint32_t));
        arguments[trap_slot] = trap_word;
        memcpy(arguments + trap_slot + 1, call_frame->words + trap_slot, (5 - trap_slot) * sizeof(uint32_t));
    }
    else
        memcpy(arguments, call_frame->words, 6 * sizeof(uint32_t));
    if (ppc_mixed_service(ctx, trap_word ? "CallOSTrapUniversalProc" : "CallUniversalProc", &registers, NULL) != 1 ||
        ctx->error[0])
        return ROMlib_carbon_report_failure("Carbon register trap invocation failed");
    *result = registers.gpr[3];
    return 1;
}

PUBLIC int ROMlib_carbon_invoke_pascal(uint32_t procedure, uint32_t info, const ROMlib_carbon_ppc_frame* call_frame,
                                       uint32_t* result)
{
    ppc_context* ctx = ppc_active_context;
    if (!ctx || ctx->error[0])
        return 0;
    if ((info & 15) != kPascalStackBased)
        return ROMlib_carbon_report_failure("Invalid Pascal UPP convention");
    return ppc_carbon_pascal_call(ctx, procedure, info, 0, call_frame, result);
}

PUBLIC int ROMlib_carbon_invoke_mixed_upp(uint32_t procedure, uint32_t info, const ROMlib_carbon_ppc_frame* call_frame,
                                          uint32_t* result)
{
    ppc_context* ctx = ppc_active_context;
    if (!ctx || ctx->error[0])
        return 0;
    unsigned convention = info & 15;
    if (!ctx->native_caller || !call_frame || !result ||
        (convention != kRegisterBased && convention != kCStackBased && convention != kSpecialCase))
        return ROMlib_carbon_report_failure("Invalid Carbon Mixed Mode callback frame");
    ROMlib_ppc_registers registers = *ctx->native_caller;
    registers.gpr[3] = procedure;
    registers.gpr[4] = info;
    if (ppc_mixed_service(ctx, "CallUniversalProc", &registers, call_frame) != 1 || ctx->error[0])
        return ROMlib_carbon_report_failure("Carbon Mixed Mode callback invocation failed");
    *result = registers.gpr[3];
    return 1;
}

PRIVATE ppc_fragment* ppc_register_memory(ppc_context* ctx, uint32 address, uint32 length, const char* name)
{
    ppc_fragment* fragment;
    ROMlib_pef_environment env;
    void* bytes = ppc_memory(ctx, address, length, FALSE);
    if (!bytes || !length || strlen(name) >= sizeof fragment->name) {
        snprintf(ctx->error, sizeof ctx->error, "Invalid memory fragment extent or name");
        return NULL;
    }
    fragment = calloc(1, sizeof *fragment);
    if (!fragment)
        return NULL;
    memset(&env, 0, sizeof env);
    env.opaque = ctx;
    env.allocate = ppc_allocate;
    env.release = ppc_release;
    env.resolve = ppc_resolve;
    fragment->image = ROMlib_pef_prepare_arch(bytes, length, &env, cfm_architecture(ctx));
    if (!fragment->image) {
        snprintf(ctx->error, sizeof ctx->error, "%s", env.error);
        free(fragment);
        return NULL;
    }
    fragment->owner = fragment;
    fragment->memory_address = address;
    fragment->length = length;
    fragment->spec = ctx->startup_spec;
    fragment->startup_done = 1;
    strcpy(fragment->name, name);
    fragment->next = ctx->fragments;
    ctx->fragments = fragment;
    return fragment;
}

/* A zero-length in-memory container has no file EOF to consult. Bound every
   header/table read by guest memory, then derive its extent from the packed
   section spans; the PEF loader still validates the complete container. */
PRIVATE uint32 ppc_memory_container_length(ppc_context* ctx, uint32 address)
{
    const unsigned char* header = ppc_memory(ctx, address, 40, FALSE);
    if (!header || memcmp(header, "Joy!peff", 8))
        return 0;
    unsigned count = header[32] * 256u + header[33];
    uint32 length = 40 + count * 28u;
    const unsigned char* table = ppc_memory(ctx, address, length, FALSE);
    if (!table)
        return 0;
    for (unsigned i = 0; i < count; ++i) {
        uint32 packed, offset;
        memcpy(&packed, table + 40 + i * 28 + 16, 4);
        memcpy(&offset, table + 40 + i * 28 + 20, 4);
        uint64 end = (uint64)CL(offset) + CL(packed);
        if (end > UINT32_MAX)
            return 0;
        if (end > length)
            length = end;
    }
    return ppc_memory(ctx, address, length, FALSE) ? length : 0;
}

PRIVATE ppc_closure* ppc_create_closure(ppc_context* ctx, ppc_fragment* root, int launch);
PRIVATE void ppc_cancel_closure(ppc_context* ctx, ppc_closure* closure);

/* Resource routines and components remain live until their launch context ends.
   Their imports need ownership just like externally returned CFM closures. */
PRIVATE int ppc_initialize_owned_library(ppc_context* ctx, ppc_fragment* fragment, uint32 stack)
{
    ppc_closure* closure = ppc_create_closure(ctx, fragment, 0);
    if (!closure)
        return 0;
    ppc_closure* previous = ctx->initializing_closure;
    ctx->initializing_closure = closure;
    int ok = ppc_initialize_library(ctx, fragment, stack, 0);
    ctx->initializing_closure = previous;
    if (!ok) {
        ppc_cancel_closure(ctx, closure);
        return 0;
    }
    closure->state = 2;
    return 1;
}

PRIVATE uint32 ppc_prepare_routine_resource(ppc_context* ctx, uint32 descriptor, uint32 address)
{
    Handle resource;
    Size length;
    ppc_fragment* fragment;
    uint32 stack_address, vector = 0;
    void* stack;
    SignedByte state;
    char name[64];
    if (descriptor < 12 || !ppc_memory(ctx, descriptor - 12, 44, FALSE))
        return 0;
    const uint32 resource_start = ppc_definition_resource_prefix(ctx, descriptor) ? descriptor - 12 : descriptor;
    resource = RecoverHandle((Ptr)SYN68K_TO_US(resource_start));
    if (!resource)
        return 0;
    length = GetHandleSize(resource);
    if (length < 0 || address < descriptor || address - resource_start >= (uint32)length)
        return 0;
    state = HGetState(resource);
    HLock(resource);
    snprintf(name, sizeof name, "Routine resource %08lx", (unsigned long)address);
    fragment = ppc_register_memory(ctx, address, length - (address - resource_start), name);
    if (fragment && ppc_link_pending(ctx, NULL)) {
        stack = ppc_allocate(ctx, 256 * 1024, 16, &stack_address);
        if (stack) {
            memset(stack, 0, 256 * 1024);
            if (ppc_initialize_owned_library(ctx, fragment, stack_address + 256 * 1024 - 256))
                vector = ROMlib_pef_main(fragment->image);
            ppc_release(ctx, stack);
        }
    }
    HSetState(resource, state);
    return vector;
}

PUBLIC int ROMlib_ppc_component_entry(Handle code, const FSSpec* spec, ProcPtr* entry)
{
    ppc_context* ctx = ppc_active_context;
    ppc_fragment* fragment;
    FSSpec saved;
    uint32 stack_address, vector;
    void* stack;
    char name[64];
    int ok;
    if (!ctx || !code || !spec || !entry || GetHandleSize(code) < 40)
        return 0;
    snprintf(name, sizeof name, "Component %08lx", (unsigned long)US_TO_SYN68K(code));
    saved = ctx->startup_spec;
    ctx->startup_spec = *spec;
    fragment = ppc_register_memory(ctx, US_TO_SYN68K(STARH(code)), GetHandleSize(code), name);
    ctx->startup_spec = saved;
    if (!fragment || !ppc_link_pending(ctx, NULL))
        return 0;
    stack = ppc_allocate(ctx, 256 * 1024, 16, &stack_address);
    if (!stack)
        return 0;
    memset(stack, 0, 256 * 1024);
    ok = ppc_initialize_owned_library(ctx, fragment, stack_address + 256 * 1024 - 256);
    ppc_release(ctx, stack);
    vector = ROMlib_pef_main(fragment->image);
    if (!ok || !vector)
        return 0;
    *entry = ppc_memory(ctx, vector, 8, FALSE);
    return *entry != NULL;
}

PRIVATE int ppc_closure_members(ppc_fragment* root, ppc_dependency* dependencies, ppc_fragment** members,
                                unsigned* count)
{
    unsigned n = 0;
    ppc_dependency* edge;
    if (root)
        members[n++] = root;
    else
        for (edge = dependencies; edge; edge = edge->next) {
            unsigned i;
            for (i = 0; i < n && members[i] != edge->target; ++i)
                ;
            if (i == n) {
                if (n == 512)
                    return 0;
                members[n++] = edge->target;
            }
        }
    if (!ppc_extend_members(members, &n, 512))
        return 0;
    *count = n;
    return 1;
}

PRIVATE OSErr ppc_find_connection_symbol(ppc_context* ctx, uint32 connection, const char* name, uint32* value,
                                         unsigned* type)
{
    ppc_fragment* fragment = ppc_connection_or_closure(ctx, connection);
    *value = 0;
    fprintf(stderr, "trace: PPC FindSymbol %s:%s\n", fragment ? fragment->name : "invalid", name);
    if (!fragment)
        return fragConnectionIDNotFound;
    if (fragment->native) {
        const char* library = !strcmp(fragment->name, "PeerNativeInterfaceLib") ? "InterfaceLib" : fragment->name;
        *type = 1;
        if (!ppc_builtin_data(ctx, library, name, value)) {
            *type = 2;
            if (!ppc_resolve(ctx, library, name, *type, TRUE, value))
                return fragSymbolNotFound;
        }
    }
    else if (!ROMlib_pef_find_symbol(fragment->image, name, value, type))
        return fragSymbolNotFound;
    return noErr;
}

PUBLIC int32_t ROMlib_carbon_find_symbol(uint32_t connection, const unsigned char* name, uint32_t* address,
                                         unsigned* symbol_class)
{
    ppc_context* ctx = ppc_active_context;
    char text[256];
    if (!ctx || ctx->error[0] || !name || !address || !symbol_class)
        return paramErr;
    memcpy(text, name + 1, name[0]);
    text[name[0]] = 0;
    return ppc_find_connection_symbol(ctx, connection, text, address, symbol_class);
}

/* A prepare call owns a closure; find calls only borrow a connection. Keep
   snapshots so closing one handle cannot release another client's imports. */
PRIVATE ppc_closure* ppc_create_closure(ppc_context* ctx, ppc_fragment* root, int launch)
{
    ppc_fragment* members[512];
    unsigned count;
    unsigned char assigned[512] = {0};
    if (!ppc_closure_members(root, root ? NULL : ctx->application_dependencies, members, &count)) {
        snprintf(ctx->error, sizeof ctx->error, "Too many CFM closure connections");
        return NULL;
    }
    ppc_closure* closure = calloc(1, sizeof *closure);
    if (!closure)
        return NULL;
    if (count) {
        closure->members = malloc(count * sizeof *closure->members);
        if (!closure->members) {
            free(closure);
            return NULL;
        }
        memcpy(closure->members, members, count * sizeof *members);
    }
    closure->root = root;
    closure->count = count;
    closure->launch = launch;
    closure->id = launch ? ctx->closure : 0;
    if (!launch && !ppc_allocate(ctx, 4, 4, &closure->id))
        goto fail;
    for (unsigned i = 0; i < count; ++i) {
        if (members[i]->references == UINT32_MAX)
            goto fail;
        if (!members[i]->connection) {
            if (!ppc_allocate(ctx, 4, 4, &members[i]->connection))
                goto fail;
            assigned[i] = 1;
        }
    }
    for (unsigned i = 0; i < count; ++i) {
        ++members[i]->references;
        if (members[i]->native && !members[i]->initialization_closure)
            members[i]->initialization_closure = closure->id;
    }
    closure->state = launch ? 2 : 1;
    closure->next = ctx->closures;
    ctx->closures = closure;
    for (unsigned i = 0; i < count; ++i) {
        ppc_fragment* member = members[i];
        if (!member->notified) {
            member->notified = 1;
            if (!ppc_fragment_notify(ctx, member, member->connection, 0, 0))
                return NULL;
        }
        if (!ppc_fragment_notify(ctx, member, member->connection, closure->id, 1))
            return NULL;
    }
    return closure;
fail:
    for (unsigned i = 0; i < count; ++i)
        if (assigned[i]) {
            ppc_release(ctx, SYN68K_TO_US(members[i]->connection));
            members[i]->connection = 0;
        }
    if (!launch && closure->id)
        ppc_release(ctx, SYN68K_TO_US(closure->id));
    free(closure->members);
    free(closure);
    return NULL;
}

PRIVATE int ppc_pin_launch_closure(ppc_context* ctx)
{
    if (!ctx->launch_closure) {
        ctx->launch_closure = ppc_create_closure(ctx, NULL, 1);
        return ctx->launch_closure != NULL;
    }
    /* Bootstrap INITs can add imports. Pin the completed launch graph too. */
    ppc_fragment* members[512];
    unsigned count;
    if (!ppc_closure_members(NULL, ctx->application_dependencies, members, &count))
        return 0;
    ppc_closure* closure = ctx->launch_closure;
    ppc_fragment** expanded = realloc(closure->members, (closure->count + count) * sizeof *expanded);
    if (!expanded && (closure->count || count))
        return 0;
    closure->members = expanded;
    for (unsigned i = 0; i < count; ++i) {
        unsigned j;
        for (j = 0; j < closure->count && closure->members[j] != members[i]; ++j)
            ;
        if (j == closure->count) {
            if (!members[i]->connection && !ppc_allocate(ctx, 4, 4, &members[i]->connection))
                return 0;
            if (members[i]->references == UINT32_MAX)
                return 0;
            ++members[i]->references;
            if (members[i]->native && !members[i]->initialization_closure)
                members[i]->initialization_closure = closure->id;
            closure->members[closure->count++] = members[i];
            if (!members[i]->notified) {
                members[i]->notified = 1;
                if (!ppc_fragment_notify(ctx, members[i], members[i]->connection, 0, 0))
                    return 0;
            }
            if (!ppc_fragment_notify(ctx, members[i], members[i]->connection, closure->id, 1))
                return 0;
        }
    }
    return 1;
}

PRIVATE void ppc_cancel_closure(ppc_context* ctx, ppc_closure* closure)
{
    for (unsigned i = 0; i < closure->count; ++i)
        --closure->members[i]->references;
    ppc_closure** link = &ctx->closures;
    while (*link && *link != closure)
        link = &(*link)->next;
    if (*link)
        *link = closure->next;
    ppc_release(ctx, SYN68K_TO_US(closure->id));
    free(closure->members);
    free(closure);
}

PRIVATE void ppc_free_closures(ppc_context* ctx)
{
    ppc_remove_folder_notifications(ctx, NULL);
    ppc_remove_display_notifications(ctx, NULL);
    while (ctx->shutdown_callbacks) {
        ppc_shutdown_callback* entry = ctx->shutdown_callbacks;
        ctx->shutdown_callbacks = entry->next;
        free(entry);
    }
    while (ctx->theme_states) {
        ppc_theme_state* state = ctx->theme_states;
        ctx->theme_states = state->next;
        free(state); /* Guest tokens are released by the allocation teardown. */
    }
    while (ctx->notifiers) {
        ppc_notifier* notifier = ctx->notifiers;
        ctx->notifiers = notifier->next;
        free(notifier);
    }
    while (ctx->closures) {
        ppc_closure* closure = ctx->closures;
        ctx->closures = closure->next;
        free(closure->members);
        free(closure);
    }
}

/* Private connections are deliberately excluded from import/name lookup.
   Their imports use ordinary shared connections (Apple Runtime Architectures,
   https://dev.os9.ca/techpubs/mac/runtimehtml/RTArch-19.html). */
PRIVATE int ppc_finish_connection(ppc_context* ctx, ppc_fragment* fragment, uint32 flags, uint32* result,
                                  uint32* main_address, ROMlib_ppc_registers* r)
{
    if (!fragment) {
        r->gpr[3] = fragLibNotFound;
        return 1;
    }
    fragment = fragment->owner;
    if (flags == kFindLib &&
        (!fragment->references || fragment->closed || (!fragment->native && !fragment->initialized))) {
        r->gpr[3] = fragLibNotFound;
        return 1;
    }
    if (flags != kFindLib && !ppc_reopen_fragment(ctx, fragment))
        return -1;
    if (!fragment->native && !fragment->image) {
        OSErr file_error;
        fragment->image = ppc_prepare_fragment(ctx, fragment, &file_error);
        if (file_error != noErr) {
            /* A failed private probe has no connection or dependencies to cache. */
            if (fragment->private_copy) {
                ppc_fragment** link = &ctx->fragments;
                while (*link && *link != fragment)
                    link = &(*link)->next;
                if (*link) {
                    *link = fragment->next;
                    free(fragment);
                }
            }
            r->gpr[3] = file_error;
            return 1;
        }
    }
    if (!fragment->native && fragment->image && !fragment->linking)
        for (ppc_fragment* peer = ctx->fragments; peer; peer = peer->next)
            if (peer != fragment && peer == peer->owner && peer->image && peer->offset == fragment->offset &&
                peer->length == fragment->length && peer->memory_address == fragment->memory_address &&
                ppc_same_spec(&peer->spec, &fragment->spec))
                ROMlib_pef_share_code(fragment->image, peer->image);
    if (!fragment->native && (!fragment->image || !ppc_link_pending(ctx, NULL)))
        return -1;
    if (flags == kFindLib) {
        if (!fragment->connection && !ppc_allocate(ctx, 4, 4, &fragment->connection))
            return -1;
        *result = CL(fragment->connection);
        if (main_address)
            *main_address = fragment->native ? 0 : CL(ROMlib_pef_main(fragment->image));
        r->gpr[3] = noErr;
        return 1;
    }
    ppc_closure* closure = ppc_create_closure(ctx, fragment, 0);
    if (!closure) {
        if (ctx->error[0])
            return -1;
        r->gpr[3] = memFullErr;
        return 1;
    }
    if (!fragment->native && !fragment->initialized) {
        uint32 stack_address;
        void* stack = ppc_allocate(ctx, 256 * 1024, 16, &stack_address);
        if (!stack) {
            ppc_cancel_closure(ctx, closure);
            r->gpr[3] = memFullErr;
            return 1;
        }
        memset(stack, 0, 256 * 1024);
        ppc_closure* previous = ctx->initializing_closure;
        ctx->initializing_closure = closure;
        int ok = ppc_initialize_library(ctx, fragment, stack_address + 256 * 1024 - 256, 0);
        ctx->initializing_closure = previous;
        ppc_release(ctx, stack);
        if (!ok) {
            ppc_cancel_closure(ctx, closure);
            return -1;
        }
    }
    closure->state = 2;
    *result = CL(closure->id);
    if (main_address)
        *main_address = fragment->native ? 0 : CL(ROMlib_pef_main(fragment->image));
    r->gpr[3] = noErr;
    return 1;
}

PRIVATE int ppc_close_closure(ppc_context* ctx, ppc_closure* closure, boolean_t normal_term)
{
    /* A callback cannot remove the code to which its active invocation must
       return. Reject that close before changing any reference counts. */
    for (unsigned i = 0; i < closure->count; ++i)
        if (ctx->notifying_owner && closure->members[i] == ctx->notifying_owner &&
            closure->members[i]->references == 1) {
            snprintf(ctx->error, sizeof ctx->error, "Cannot unload an active CFM notification callback");
            return 0;
        }
    uint32 stack_address = 0;
    void* stack = NULL;
    for (unsigned i = 0; i < closure->count; ++i)
        if (normal_term && closure->members[i]->references == 1 && ROMlib_pef_term(closure->members[i]->image)) {
            stack = ppc_allocate(ctx, 256 * 1024, 16, &stack_address);
            if (!stack)
                return 0;
            break;
        }
    closure->state = 3;
    for (unsigned i = 0; i < closure->count; ++i)
        if (!ppc_fragment_notify(ctx, closure->members[i], closure->members[i]->connection, closure->id, 2)) {
            if (stack)
                ppc_release(ctx, stack);
            return 0;
        }
    for (unsigned i = 0; i < closure->count; ++i)
        --closure->members[i]->references;
    for (;;) {
        ppc_fragment* fragment = NULL;
        /* Reverse actual initialization order, including cyclic groups. */
        for (unsigned at = ctx->n_initialized; at > 0 && !fragment; --at)
            for (unsigned i = 0; i < closure->count; ++i)
                if (ctx->init_order[at - 1] == closure->members[i] && !closure->members[i]->references &&
                    !closure->members[i]->closed) {
                    fragment = closure->members[i];
                    break;
                }
        if (!fragment)
            break;
        fragment->closed = 2;
        if (normal_term && ROMlib_pef_term(fragment->image) &&
            !ppc_call_vector(ctx, fragment->image, ROMlib_pef_term(fragment->image), stack_address + 256 * 1024 - 256,
                             0, NULL)) {
            ppc_release(ctx, stack);
            return 0;
        }
        for (unsigned i = 0; i < ctx->n_initialized; ++i)
            if (ctx->init_order[i] == fragment) {
                memmove(ctx->init_order + i, ctx->init_order + i + 1,
                        (ctx->n_initialized - i - 1) * sizeof *ctx->init_order);
                --ctx->n_initialized;
                break;
            }
        if (!ppc_fragment_notify(ctx, fragment, fragment->connection, 0, 3)) {
            if (stack)
                ppc_release(ctx, stack);
            return 0;
        }
        if (ctx->desktop_draw.owner == fragment->image) {
            ctx->desktop_draw.procedure = 0;
            ctx->desktop_draw.owner = NULL;
            ctx->desktop_draw_refcon = 0;
        }
        ppc_remove_folder_notifications(ctx, fragment->image);
        ppc_remove_display_notifications(ctx, fragment->image);
        for (unsigned i = 0; i < NELEM(ctx->display_patches); ++i) {
            if (ctx->display_patches[i].owner == fragment->image) {
                ctx->display_patches[i].procedure = 0;
                ctx->display_patches[i].owner = NULL;
            }
        }
        fragment->closed = 1;
        fragment->initialized = 0;
        ROMlib_pef_unload(fragment->image);
        fragment->image = NULL;
    }
    for (unsigned i = 0; i < closure->count; ++i)
        if (!closure->members[i]->references && closure->members[i]->native && closure->members[i]->closed != 1) {
            if (!ppc_fragment_notify(ctx, closure->members[i], closure->members[i]->connection, 0, 3)) {
                if (stack)
                    ppc_release(ctx, stack);
                return 0;
            }
            closure->members[i]->closed = 1;
        }
    if (stack)
        ppc_release(ctx, stack);
    closure->state = 0;
    return 1;
}

PRIVATE int ppc_prepare_loader_vectors(ppc_context* ctx)
{
    if (ctx->loader_vectors)
        return 1;
    uint32 address, import_info, import_library;
    uint32* vectors = ppc_allocate(ctx, 56, 4, &address);
    if (!vectors)
        return 0;
    if (!ppc_resolve(ctx, "ROMAWCFM", "ROMAWGetImportInfo", 2, FALSE, &import_info) ||
        !ppc_resolve(ctx, "ROMAWCFM", "ROMAWGetImportLibrary", 2, FALSE, &import_library)) {
        ppc_release(ctx, address);
        return 0;
    }
    memset(vectors, 0, 56);
    vectors[12] = CL(import_info);
    vectors[13] = CL(import_library);
    ctx->loader_vectors = address;
    return 1;
}

/* System 9's FragFindOwnerOfPC writes six guest longwords: the image ID,
   its loader identity, the connection identity, section index, base and size. */
PRIVATE int ppc_find_owner_of_pc(ppc_context* ctx, ROMlib_ppc_registers* r)
{
    if (!r->gpr[4]) {
        r->gpr[3] = paramErr;
        return 1;
    }
    const uint32 pc = r->gpr[3];
    uint32 connection = 0, base, size;
    unsigned index;
    if (ROMlib_pef_find_section(ctx->application, pc, 1, &base, &size, NULL, &index))
        connection = ctx->closure;
    else {
        for (ppc_fragment* fragment = ctx->fragments; fragment; fragment = fragment->next)
            if (!fragment->closed && fragment->connection &&
                ROMlib_pef_find_section(fragment->image, pc, 1, &base, &size, NULL, &index)) {
                connection = fragment->connection;
                break;
            }
    }
    if (!connection) {
        r->gpr[3] = fnfErr;
        return 1;
    }
    uint32* out = ppc_memory(ctx, r->gpr[4], 24, TRUE);
    if (!out || !ppc_prepare_loader_vectors(ctx))
        return -1;
    out[0] = CL(connection);
    out[1] = CL(ctx->loader_vectors);
    out[2] = CL(connection);
    out[3] = CL(index);
    out[4] = CL(base);
    out[5] = CL(size);
    r->gpr[3] = noErr;
    return 1;
}

PUBLIC int ROMlib_carbon_load_reference(const char* library, const char* name, ROMlib_carbon_reference* reference)
{
    ppc_context* ctx = ppc_active_context;
    if (!ctx || ctx->error[0] || cfm_architecture(ctx) != kPowerPCArch)
        return 0;
    for (ppc_fragment* fragment = ctx->fragments; fragment; fragment = fragment->next) {
        if (fragment->private_copy || strcmp(fragment->name, library) || fragment->owner->native)
            continue;
        if (!fragment->owner->initialized) {
            ROMlib_ppc_registers result = {0};
            uint32 connection;
            if (ppc_finish_connection(ctx, fragment, kLoadLib, &connection, NULL, &result) <= 0 || result.gpr[3])
                return 0;
        }
        return ROMlib_carbon_probe_reference(library, name, reference);
    }
    return 0;
}

static int ppc_fragment_notify(ppc_context* ctx, ppc_fragment* fragment, uint32 connection, uint32 closure,
                               unsigned event)
{
    /* The private CFM ABI passes connection, closure and event (0 create,
       1 attach, 2 detach, 3 release, 4 initialized). Registration takes only
       the callback pointer; there is no caller-supplied refCon argument. */
    uint32 fence = ctx->notifier_serial;
    for (ppc_notifier* notifier = ctx->notifiers; notifier; notifier = notifier->next) {
        if (!notifier->active || notifier->serial > fence)
            continue;
        if (notifier->owner && notifier->owner->closed == 1) {
            notifier->active = 0;
            continue;
        }
        uint32 arguments[] = {connection, closure, event}, ignored;
        ppc_fragment* previous = ctx->notifying_owner;
        ctx->notifying_owner = notifier->owner;
        int ok = ppc_invoke_callback_stack(ctx, notifier->vector, arguments, 3, &ignored, NULL);
        ctx->notifying_owner = previous;
        if (!ok)
            return 0;
    }
    if (event == 3)
        for (ppc_notifier* notifier = ctx->notifiers; notifier; notifier = notifier->next)
            if (notifier->owner == fragment)
                notifier->active = 0;
    return !ctx->error[0];
}

static int ppc_notifier_service(ppc_context* ctx, const char* name, ROMlib_ppc_registers* r)
{
    int adding = !strcmp(name, "FragAddNotifyProc");
    if (!adding && strcmp(name, "FragRemoveNotifyProc"))
        return 0;
    uint32 address = r->gpr[3];
    if (!address) {
        r->gpr[3] = (uint32)(int32)paramErr;
        return 1;
    }
    if (!adding) {
        for (ppc_notifier* notifier = ctx->notifiers; notifier; notifier = notifier->next)
            if (notifier->active && notifier->address == address) {
                notifier->active = 0;
                r->gpr[3] = noErr;
                return 1;
            }
        r->gpr[3] = (uint32)(int32)-2804;
        return 1;
    }
    uint32 vector = ppc_callback_vector(ctx, address);
    if (!vector) {
        r->gpr[3] = (uint32)(int32)paramErr;
        return 1;
    }
    ppc_notifier* available = NULL;
    unsigned count = 0;
    for (ppc_notifier* notifier = ctx->notifiers; notifier; notifier = notifier->next) {
        ++count;
        if (!notifier->active && !available)
            available = notifier;
    }
    if (ctx->notifier_serial == UINT32_MAX || (!available && count >= 512)) {
        r->gpr[3] = (uint32)(int32)-2809;
        return 1;
    }
    if (!available) {
        available = calloc(1, sizeof *available);
        if (!available) {
            r->gpr[3] = (uint32)(int32)-2809;
            return 1;
        }
        available->next = ctx->notifiers;
        ctx->notifiers = available;
    }
    uint32* words = ppc_memory(ctx, vector, 8, FALSE);
    available->owner = NULL;
    ppc_fragment* candidate = NULL;
    unsigned candidates = 0;
    for (ppc_fragment* fragment = ctx->fragments; fragment; fragment = fragment->next)
        if (fragment == fragment->owner && ROMlib_pef_is_code(fragment->image, CL(words[0]))) {
            candidate = fragment;
            ++candidates;
            /* Private instances can share PIC text. Their transition vectors
               and TOCs still identify the data-owning instance. */
            if (ROMlib_pef_contains(fragment->image, vector, 8) ||
                ROMlib_pef_contains(fragment->image, CL(words[1]), 0)) {
                available->owner = fragment;
                candidates = 0;
                break;
            }
        }
    if (candidates > 1) {
        r->gpr[3] = (uint32)(int32)paramErr;
        return 1;
    }
    if (candidates == 1)
        available->owner = candidate;
    available->address = address;
    available->vector = vector;
    available->serial = ++ctx->notifier_serial;
    available->active = 1;
    r->gpr[3] = noErr;
    return 1;
}

PRIVATE int ppc_cfm_service(ppc_context* ctx, const char* name, ROMlib_ppc_registers* r)
{
    if (ppc_notifier_service(ctx, name, r))
        return 1;
    ppc_fragment* fragment;
    char fragment_name[256];
    uint32 *result, *main_address, value;
    unsigned type;
    int disk = !strcmp(name, "GetDiskFragment");
    if (!strcmp(name, "ROMAWGetImportInfo") || !strcmp(name, "ROMAWGetImportLibrary")) {
        ROMlib_pef_image* image;
        fragment = ppc_connection_or_closure(ctx, r->gpr[3]);
        image = r->gpr[3] == ctx->closure ? ctx->application : fragment ? fragment->image : NULL;
        if (!image) {
            r->gpr[3] = fragConnectionIDNotFound;
            return 1;
        }
        if (!strcmp(name, "ROMAWGetImportInfo")) {
            uint32 *libraries = ppc_memory(ctx, r->gpr[4], 4, TRUE), *symbols = ppc_memory(ctx, r->gpr[5], 4, TRUE);
            if (!libraries || !symbols)
                return -1;
            *libraries = CL(ROMlib_pef_library_count(image));
            *symbols = CL(ROMlib_pef_import_count(image));
        }
        else {
            uint32 info[4], address;
            const char* library = ROMlib_pef_library_info(image, r->gpr[4], info);
            uint32* out = ppc_memory(ctx, r->gpr[5], 24, TRUE);
            char* title;
            if (!out)
                return -1;
            if (!library) {
                r->gpr[3] = paramErr;
                return 1;
            }
            title = ppc_allocate(ctx, strlen(library) + 1, 1, &address);
            if (!title)
                return -1;
            strcpy(title, library);
            out[0] = CL(info[0]);
            out[1] = CL(info[1]);
            out[2] = CL(strlen(library));
            out[3] = CL(address);
            out[4] = CL(info[2]);
            out[5] = CL(info[3]);
        }
        r->gpr[3] = noErr;
        return 1;
    }
    if (!strcmp(name, "FragGetClosureInfo")) {
        uint32 id = r->gpr[4] ? r->gpr[4] : ctx->closure, *out, *connections = NULL;
        ppc_closure* closure = ppc_find_closure(ctx, id);
        unsigned start = r->gpr[7], requested = r->gpr[8], available;
        if (r->gpr[3]) {
            r->gpr[3] = -2800;
            return 1;
        }
        if (!r->gpr[6] || (int32)start < 0 || (int32)requested < 0 || (requested && !r->gpr[9])) {
            r->gpr[3] = paramErr;
            return 1;
        }
        if (r->gpr[5] || !closure) {
            r->gpr[3] = fragConnectionIDNotFound;
            return 1;
        }
        available = closure->count + closure->launch;
        if (start > available) {
            r->gpr[3] = paramErr;
            return 1;
        }
        requested = MIN(requested, available - start);
        out = ppc_memory(ctx, r->gpr[6], 12, TRUE);
        if (requested)
            connections = ppc_memory(ctx, r->gpr[9], (size_t)requested * 4, TRUE);
        if (!out || (requested && !connections))
            return -1;
        out[0] = CL(id);
        out[1] = CL(closure->launch ? ctx->closure : closure->root->connection);
        out[2] = CL(available);
        for (unsigned i = 0; i < requested; ++i) {
            unsigned at = start + i;
            connections[i] =
                CL(closure->launch && !at ? ctx->closure : closure->members[at - closure->launch]->connection);
        }
        r->gpr[3] = noErr;
        return 1;
    }
    if (!strcmp(name, "FragFindOwnerOfPC"))
        return ppc_find_owner_of_pc(ctx, r);
    if (!strcmp(name, "FragGetContextInfo")) {
        uint32* out = ppc_memory(ctx, r->gpr[5], 20, TRUE);
        if (!out)
            return -1;
        if (r->gpr[3] || r->gpr[4]) {
            r->gpr[3] = -2800;
            return 1;
        }
        out[0] = 0;
        out[1] = CL(US_TO_SYN68K(MR(ApplZone)));
        unsigned count = 0;
        uint32 newest = ctx->closure;
        for (ppc_closure* closure = ctx->closures; closure; closure = closure->next)
            if (closure->state) {
                if (!count)
                    newest = closure->id;
                ++count;
            }
        out[2] = CL(count);
        out[3] = CL(ctx->closure);
        out[4] = CL(newest);
        r->gpr[3] = noErr;
        return 1;
    }
    if (!strcmp(name, "FragGetConnectionInfo")) {
        uint32 id = r->gpr[4], *out = ppc_memory(ctx, r->gpr[6], 68, TRUE), title_address;
        const char* title;
        unsigned char* title_out;
        if (!out)
            return -1;
        fragment = ppc_connection_or_closure(ctx, id);
        if (id != ctx->closure && !fragment) {
            r->gpr[3] = fragConnectionIDNotFound;
            return 1;
        }
        if (r->gpr[5]) {
            r->gpr[3] = fragConnectionIDNotFound;
            return 1;
        }
        title = fragment ? fragment->name : "Application";
        title_address = CL(out[6]);
        title_out = ppc_memory(ctx, title_address, strlen(title) + 1, TRUE);
        if (!title_out)
            return -1;
        title_out[0] = strlen(title);
        memcpy(title_out + 1, title, strlen(title));
        if (!ppc_prepare_loader_vectors(ctx))
            return -1;
        /* CFM's connection inspection record preserves the caller's name buffer.
         Its loader procedure table exposes real PEF import metadata. */
        memset(out, 0, 68);
        out[0] = CL(fragment ? fragment->connection : id);
        out[3] = CL(fragment ? fragment->initialization_closure : ctx->closure);
        out[6] = CL(title_address);
        out[15] = CL(ctx->loader_vectors);
        out[16] = CL(fragment ? fragment->connection : id);
        r->gpr[3] = noErr;
        return 1;
    }

    if (!strcmp(name, "CountSymbols") || !strcmp(name, "GetIndSymbol")) {
        fragment = ppc_connection_or_closure(ctx, r->gpr[3]);
        if (!fragment) {
            r->gpr[3] = fragConnectionIDNotFound;
            return 1;
        }
        if (fragment->native) {
            r->gpr[3] = (uint32)(int32)-4; /* unimpErr */
            return 1;
        }
        if (!strcmp(name, "CountSymbols")) {
            result = ppc_memory(ctx, r->gpr[4], 4, TRUE);
            if (!result)
                return -1;
            *result = CL(ROMlib_pef_export_count(fragment->image));
        }
        else {
            unsigned char* title = ppc_memory(ctx, r->gpr[5], 256, TRUE);
            unsigned char* class_out = ppc_memory(ctx, r->gpr[7], 1, TRUE);
            result = ppc_memory(ctx, r->gpr[6], 4, TRUE);
            if (!title || !class_out || !result)
                return -1;
            const char* export_name = ROMlib_pef_export_info(fragment->image, r->gpr[4], &value, &type);
            if (!export_name || strlen(export_name) > 255) {
                r->gpr[3] = fragSymbolNotFound;
                return 1;
            }
            title[0] = strlen(export_name);
            memcpy(title + 1, export_name, title[0]);
            *result = CL(value);
            *class_out = type;
        }
        r->gpr[3] = noErr;
        return 1;
    }
    if (!strcmp(name, "FindSymbol")) {
        unsigned char* class_out;
        result = ppc_memory(ctx, r->gpr[5], 4, TRUE);
        class_out = r->gpr[6] ? ppc_memory(ctx, r->gpr[6], 1, TRUE) : NULL;
        if (!result || (r->gpr[6] && !class_out) ||
            !ppc_pascal_name(ctx, r->gpr[4], fragment_name, sizeof fragment_name))
            return -1;
        *result = 0;
        r->gpr[3] = ppc_find_connection_symbol(ctx, r->gpr[3], fragment_name, &value, &type);
        if (!r->gpr[3]) {
            *result = CL(value);
            if (class_out)
                *class_out = type;
        }
        return 1;
    }
    if (!strcmp(name, "CloseConnection") || !strcmp(name, "FragRelease")) {
        const boolean_t private_release = !strcmp(name, "FragRelease");
        if (private_release && r->gpr[3]) {
            r->gpr[3] = -2800; /* Unknown context; zero selects this application. */
            return 1;
        }
        result = private_release ? NULL : ppc_memory(ctx, r->gpr[3], 4, TRUE);
        if (!private_release && !result)
            return -1;
        ppc_closure* closure = ppc_find_closure(ctx, private_release ? r->gpr[4] : CL(*result));
        if (!closure || closure->state != 2 || closure->launch)
            r->gpr[3] = fragConnectionIDNotFound;
        else {
            if (!ppc_close_closure(ctx, closure, !private_release || (uint8)r->gpr[5])) {
                if (ctx->error[0])
                    return -1;
                r->gpr[3] = memFullErr;
                return 1;
            }
            if (result)
                *result = 0;
            r->gpr[3] = noErr;
        }
        return 1;
    }
    if (!strcmp(name, "GetMemFragment")) {
        uint32 flags = r->gpr[6];
        const uint32 main_output = r->gpr[8];
        result = ppc_memory(ctx, r->gpr[7], 4, TRUE);
        main_address = main_output ? ppc_memory(ctx, main_output, 4, TRUE) : NULL;
        fragment_name[0] = 0;
        if (!result || (main_output && !main_address) ||
            (r->gpr[5] && !ppc_pascal_name(ctx, r->gpr[5], fragment_name, 64)))
            return -1;
        *result = 0;
        if (main_address)
            *main_address = 0;
        if (!r->gpr[4])
            r->gpr[4] = ppc_memory_container_length(ctx, r->gpr[3]);
        if (flags != kLoadLib && flags != kFindLib && flags != kLoadNewCopy) {
            r->gpr[3] = paramErr;
            return 1;
        }
        if (r->gpr[9]) {
            unsigned char* error_name = ppc_memory(ctx, r->gpr[9], 1, TRUE);
            if (!error_name)
                return -1;
            *error_name = 0;
        }
        for (fragment = ctx->fragments; fragment; fragment = fragment->next)
            if (!fragment->private_copy && fragment->memory_address == r->gpr[3] && fragment->length == r->gpr[4] &&
                (!fragment_name[0] || !strcmp(fragment->name, fragment_name)))
                break;
        if (flags == kLoadNewCopy || (!fragment && flags != kFindLib)) {
            fragment =
                ppc_register_memory(ctx, r->gpr[3], r->gpr[4], fragment_name[0] ? fragment_name : "Memory fragment");
            if (!fragment)
                return -1;
            fragment->private_copy = flags == kLoadNewCopy;
        }
        return ppc_finish_connection(ctx, fragment, flags, result, main_address, r);
    }
    if (!disk && strcmp(name, "GetSharedLibrary"))
        return 0;
    const uint32 main_output = r->gpr[disk ? 9 : 7];
    result = ppc_memory(ctx, r->gpr[disk ? 8 : 6], 4, TRUE);
    main_address = main_output ? ppc_memory(ctx, main_output, 4, TRUE) : NULL;
    const uint32 name_address = r->gpr[disk ? 6 : 3];
    fragment_name[0] = 0;
    if (!result || (main_output && !main_address) ||
        ((!disk || name_address) && !ppc_pascal_name(ctx, name_address, fragment_name, 64)))
        return -1;
    FSSpec spec;
    if (disk) {
        const FSSpec* input = ppc_memory(ctx, r->gpr[3], sizeof spec, FALSE);
        if (!input)
            return -1;
        spec = *input;
    }
    *result = 0;
    if (main_address)
        *main_address = 0;
    uint32 flags = r->gpr[disk ? 7 : 5];
    /* CarbonLib passes the combined find/reference flags to open its lazy
       companion fragments. This acquires a normal shared reference. */
    if (!disk && flags == (kReferenceCFrag | kFindLib))
        flags = kReferenceCFrag;
    if (flags != kLoadLib && flags != kFindLib && flags != kLoadNewCopy) {
        r->gpr[3] = paramErr;
        return 1;
    }
    if (r->gpr[disk ? 10 : 8]) {
        unsigned char* error_name = ppc_memory(ctx, r->gpr[disk ? 10 : 8], 1, TRUE);
        if (!error_name)
            return -1;
        *error_name = 0;
    }
    if (disk) {
        ppc_register_file(ctx, &spec);
        for (fragment = ctx->fragments; fragment; fragment = fragment->next)
            if (!fragment->private_copy && ppc_same_spec(&fragment->spec, &spec) && fragment->offset == r->gpr[4] &&
                (!fragment_name[0] || !strcmp(fragment->name, fragment_name)))
                break;
        if (!fragment && flags != kFindLib) {
            fragment = calloc(1, sizeof *fragment);
            if (!fragment) {
                r->gpr[3] = memFullErr;
                return 1;
            }
            fragment->spec = spec;
            fragment->private_copy = flags == kLoadNewCopy;
            fragment->startup_done = fragment->private_copy;
            fragment->offset = r->gpr[4];
            fragment->length = r->gpr[5];
            strcpy(fragment->name, fragment_name[0] ? fragment_name : "Disk fragment");
            fragment->owner = fragment;
            fragment->next = ctx->fragments;
            ctx->fragments = fragment;
        }
    }
    else {
        if (r->gpr[4] != cfm_architecture(ctx)) {
            r->gpr[3] = fragLibNotFound;
            return 1;
        }
        fprintf(stderr, "trace: PPC GetSharedLibrary %s flags=%lu\n", fragment_name, (unsigned long)r->gpr[5]);
        for (fragment = ctx->fragments; fragment; fragment = fragment->next)
            if (!fragment->private_copy && !strcmp(fragment->name, fragment_name))
                break;
        if (!fragment && flags != kFindLib &&
            (!strcmp(fragment_name, "InterfaceLib") || !strcmp(fragment_name, "MathLib") ||
             !strcmp(fragment_name, "StdCLib") || !strcmp(fragment_name, "PeerNativeInterfaceLib") ||
             !strcmp(fragment_name, "PrivateInterfaceLib") || !strcmp(fragment_name, "WindowsLib") ||
             !strcmp(fragment_name, "SpeechLib") || !strcmp(fragment_name, "QuickTimeLib"))) {
            if (flags == kLoadNewCopy) {
                r->gpr[3] = (uint32)(int32)-4;
                return 1;
            }
            fragment = calloc(1, sizeof *fragment);
            if (!fragment) {
                r->gpr[3] = memFullErr;
                return 1;
            }
            strcpy(fragment->name, fragment_name);
            fragment->native = 1;
            fragment->owner = fragment;
            fragment->next = ctx->fragments;
            ctx->fragments = fragment;
        }
    }
    if (fragment && flags == kLoadNewCopy && !fragment->private_copy) {
        ppc_fragment* source = fragment->owner;
        if (source->native) {
            r->gpr[3] = (uint32)(int32)-4; /* No PEF instance exists for native-only libraries. */
            return 1;
        }
        fragment = calloc(1, sizeof *fragment);
        if (!fragment) {
            r->gpr[3] = memFullErr;
            return 1;
        }
        fragment->spec = source->spec;
        fragment->offset = source->offset;
        fragment->length = disk ? r->gpr[5] : source->length;
        fragment->memory_address = source->memory_address;
        strcpy(fragment->name, fragment_name[0] ? fragment_name : source->name);
        fragment->private_copy = fragment->startup_done = 1;
        fragment->owner = fragment;
        fragment->next = ctx->fragments;
        ctx->fragments = fragment;
    }
    return ppc_finish_connection(ctx, fragment, flags, result, main_address, r);
}

PUBLIC int ROMlib_carbon_call_cfm(const char* name, ROMlib_carbon_ppc_frame* call)
{
    ppc_context* ctx = ppc_active_context;
    if (!ctx || ctx->error[0])
        return 0;
    ROMlib_ppc_registers registers = {0};
    memcpy(registers.gpr + 3, call->words, sizeof call->words);
    if (ppc_cfm_service(ctx, name, &registers) != 1)
        return 0;
    call->words[0] = registers.gpr[3];
    return 1;
}

/* System 7 public stack selectors plus the resource/file registration calls
   used by CarbonLib's own startup INIT. Unknown private requests are rejected. */
PRIVATE ppc_context* cfm68k_classic_connection_context(uint32 architecture)
{
    if (cfm68k_classic_context)
        return cfm68k_classic_context;
    ppc_context* ctx = calloc(1, sizeof *ctx);
    FSSpec spec;
    INTEGER vref;
    LONGINT directory;
    if (!ctx)
        return NULL;
    ctx->architecture = architecture;
    ctx->application_started = 1;
    if (FSMakeFSSpec(ROMlib_exevrefnum, 0, ROMlib_exefname, &spec) != noErr ||
        !ppc_allocate(ctx, 4, 4, &ctx->closure)) {
        free(ctx->system_l2);
        free(ctx);
        return NULL;
    }
    ctx->startup_spec = spec;
    if (architecture == kPowerPCArch) {
        uint32* halt = ppc_allocate(ctx, 4, 4, &ctx->halt);
        if (!halt) {
            while (ctx->allocations)
                ppc_release(ctx, ctx->allocations->aligned);
            free(ctx->system_l2);
            free(ctx);
            return NULL;
        }
        *halt = CLC(0x18000000);
    }
    for (unsigned i = 0; i < NELEM(ctx->os_vectors); ++i)
        ctx->os_vectors[i] = CL((uint32)(uintptr_t)ostraptable[i]);
    for (unsigned i = 0; i < NELEM(ctx->tool_vectors); ++i)
        ctx->tool_vectors[i] = CL((uint32)(uintptr_t)tooltraptable[i]);
    if (!ppc_pin_launch_closure(ctx)) {
        ppc_free_closures(ctx);
        while (ctx->allocations)
            ppc_release(ctx, ctx->allocations->aligned);
        free(ctx->system_l2);
        free(ctx);
        return NULL;
    }
    ppc_scan_directory(ctx, CW(spec.vRefNum), CL(spec.parID));
    if (FindFolder((int16)0x8000, kExtensionFolderType, FALSE, &vref, &directory) == noErr)
        ppc_scan_directory(ctx, CW(vref), CL(directory));
    cfm68k_classic_context = ctx;
    ppc_active_context = ctx;
    return ctx;
}

PRIVATE uint32 cfm_classic_code_fragment(void)
{
    ppc_context* ctx = ppc_active_context;
    uint32 ret = POPADDR(), selector = POPUW();
    ROMlib_ppc_registers r;
    const char* name = NULL;
    unsigned count = 0, i;
    int handled;
    memset(&r, 0, sizeof r);
    fprintf(stderr, "trace: 68K CFM selector=%04lx\n", (unsigned long)selector);
    switch (selector) {
    case 1:
        name = "GetSharedLibrary";
        count = 6;
        break;
    case 2:
        name = "GetDiskFragment";
        count = 8;
        break;
    case 3:
        name = "GetMemFragment";
        count = 7;
        break;
    case 4:
        name = "CloseConnection";
        count = 1;
        break;
    case 5:
        name = "FindSymbol";
        count = 4;
        break;
    case 6:
        name = "CountSymbols";
        count = 2;
        break;
    case 7:
        name = "GetIndSymbol";
        count = 5;
        break;
    case 0xfffc: {
        unsigned unregister = POPUB();
        uint32 library = POPUL(), level = POPUL(), location = POPUL();
        uint32* loc = ctx ? ppc_memory(ctx, location, 16, FALSE) : NULL;
        char title[64];
        OSErr err = paramErr;
        (void)level;
        if (ctx && !unregister && loc && !CL(loc[0]) && ppc_pascal_name(ctx, library, title, sizeof title))
            err = ppc_register_memory(ctx, CL(loc[1]), CL(loc[2]), title) ? noErr : fragLibNotFound;
        WRITEUW(EM_A7, err);
        return ret;
    }
    case 0xffea: {
        unsigned unregister = POPUB();
        INTEGER resource_id = POPSW();
        uint32 address = POPUL();
        FSSpec* spec = ctx ? ppc_memory(ctx, address, sizeof *spec, FALSE) : NULL;
        WRITEUW(EM_A7, spec && !unregister ? ppc_register_resource(ctx, spec, resource_id, NULL) : paramErr);
        return ret;
    }
    case 0xffec: {
        unsigned unregister = POPUB();
        uint32 address = POPUL();
        FSSpec* spec = ctx ? ppc_memory(ctx, address, sizeof *spec, FALSE) : NULL;
        WRITEUW(EM_A7, spec && !unregister ? ppc_register_file(ctx, spec) : paramErr);
        return ret;
    }
    default:
        if (ctx)
            snprintf(ctx->error, sizeof ctx->error, "Unsupported 68K CFM selector 0x%04lx", (unsigned long)selector);
        WRITEUW(EM_A7, paramErr);
        return ret;
    }
    for (i = count; i; --i)
        r.gpr[i + 2] = POPUL();
    if (!ctx && (selector == 2 || selector == 3 ||
                 (selector == 1 && (r.gpr[4] == kMotorola68KArch || r.gpr[4] == kPowerPCArch)))) {
        uint32 architecture = selector == 1 ? r.gpr[4] : kMotorola68KArch;
        if (selector == 3) {
            const unsigned char* header = r.gpr[3] >= 0x1e00 && (uint64)r.gpr[3] + 12 <= ROMlib_guest_memory_size
                                              ? SYN68K_TO_US_RAW(r.gpr[3])
                                              : NULL;
            if (header && !memcmp(header, "Joy!peffpwpc", 12))
                architecture = kPowerPCArch;
        }
        ctx = cfm68k_classic_connection_context(architecture);
    }
    if (!ctx) {
        WRITEUW(EM_A7, fragLibNotFound);
        return ret;
    }
    fprintf(stderr, "trace: 68K CFM %s\n", name);
    handled = ppc_cfm_service(ctx, name, &r);
    WRITEUW(EM_A7, handled > 0 ? r.gpr[3] : paramErr);
    return ret;
}

PUBLIC uint32 ROMlib_ppc_code_fragment(void)
{
    char saved_error[sizeof((ppc_context*)0)->error] = {0};
    if (cfm68k_classic_context)
        memcpy(saved_error, cfm68k_classic_context->error, sizeof saved_error);
    uint32 ret = cfm_classic_code_fragment();
    /* A classic CODE application's CFM requests return their errors through
       the trap. A rejected optional fragment must not leave a callback failure
       behind: event loops would then stop before consuming their next event.
       Keep an actual PPC application's execution failure unchanged. */
    if (cfm68k_classic_context && ppc_active_context == cfm68k_classic_context) {
        if (cfm68k_classic_context->error[0] && strcmp(saved_error, cfm68k_classic_context->error))
            fprintf(stderr, "trace: classic CFM request failed: %s\n", cfm68k_classic_context->error);
        memcpy(cfm68k_classic_context->error, saved_error, sizeof saved_error);
    }
    return ret;
}

/* A System-file CFM library may provide the context used by an extension's
   INIT. Initialize independent CFM prerequisites first, while preserving the
   rule that each resource-bearing container's INIT precedes its own CFM code.
   Reachability, rather than library names, excludes every dependency closure
   that still contains an unexecuted INIT. */
PRIVATE int ppc_bootstrap_prerequisites(ppc_context* ctx, uint32 stack)
{
    ppc_fragment *fragment, *same;
    INTEGER saved_ref = CurResFile();
    THz saved_zone = GetZone();
    ppc_context* previous = ppc_active_context;
    int ok = 1;
    ppc_active_context = ctx;
    SetZone(MR(SysZone));
    for (fragment = ctx->fragments; fragment; fragment = fragment->next)
        if (fragment->image && !fragment->native && !fragment->startup_inspected) {
            INTEGER ref = C_FSpOpenResFile(&fragment->spec, fsRdPerm);
            int has_init = 0;
            if (ref != -1) {
                has_init = Count1Resources(T('I', 'N', 'I', 'T')) > 0;
                CloseResFile(ref);
            }
            UseResFile(saved_ref);
            for (same = ctx->fragments; same; same = same->next)
                if (!memcmp(&same->spec, &fragment->spec, sizeof same->spec)) {
                    same->startup_inspected = 1;
                    same->has_startup_init = has_init;
                }
        }
    for (fragment = ctx->fragments; fragment && ok; fragment = fragment->next)
        if (fragment == fragment->owner && fragment->image && fragment->initialized != 2) {
            ppc_fragment* members[512];
            unsigned count, i;
            int blocked = 0;
            if (!ppc_closure_members(fragment, NULL, members, &count)) {
                ok = 0;
                break;
            }
            for (i = 0; i < count; ++i)
                if (members[i]->has_startup_init && !members[i]->startup_done)
                    blocked = 1;
            if (!blocked) {
                fprintf(stderr, "trace: PPC bootstrap prerequisites %s\n", fragment->name);
                ok = ppc_initialize_library(ctx, fragment, stack, 0);
            }
        }
    SetZone(saved_zone);
    UseResFile(saved_ref);
    ppc_active_context = previous;
    return ok;
}

/* Execute the actual 68K INIT from each used extension before its CFM
   initializers. Copies live with the launch so installed patches stay resident.
   Extensions retain their own OS/version checks; do not synthesize boot state. */
PRIVATE int ppc_startup_extensions(ppc_context* ctx)
{
    ppc_fragment *fragment, *same;
    INTEGER saved_ref = CurResFile();
    THz saved_zone = GetZone();
    ppc_context* previous = ppc_active_context;
    int ok = 1;
    ppc_active_context = ctx;
    SetZone(MR(SysZone));
    for (fragment = ctx->fragments; fragment && ok; fragment = fragment->next)
        if (fragment->image && !fragment->startup_done && !fragment->native) {
            INTEGER ref;
            INTEGER count, i;
            for (same = ctx->fragments; same; same = same->next)
                if (!memcmp(&same->spec, &fragment->spec, sizeof same->spec))
                    same->startup_done = 1;
            ctx->startup_spec = fragment->spec;
            ref = C_FSpOpenResFile(&fragment->spec, fsRdPerm);
            if (ref == -1)
                continue;
            count = Count1Resources(T('I', 'N', 'I', 'T'));
            for (i = 1; i <= count && ok; ++i) {
                Handle init = Get1IndResource(T('I', 'N', 'I', 'T'), i);
                Size size;
                uint32 address;
                void* code;
                if (!init || (size = GetHandleSize(init)) < 2) {
                    ok = 0;
                    break;
                }
                HLock(init);
                code = ppc_allocate(ctx, size, 2, &address);
                if (!code) {
                    HUnlock(init);
                    ok = 0;
                    break;
                }
                memcpy(code, STARH(init), size);
                HUnlock(init);
                fprintf(stderr, "trace: PPC extension INIT %s index=%d address=%08lx\n", fragment->name, i,
                        (unsigned long)address);
                CToPascalCall(code, 0);
                fprintf(stderr, "trace: PPC extension INIT returned D0=%08lx\n", (unsigned long)EM_D0);
                if (ctx->error[0])
                    ok = 0;
            }
            CloseResFile(ref);
            UseResFile(saved_ref);
        }
    SetZone(saved_zone);
    UseResFile(saved_ref);
    ppc_active_context = previous;
    if (!ok && !ctx->error[0])
        snprintf(ctx->error, sizeof ctx->error, "Unable to initialize PowerPC startup extension");
    return ok;
}

/* Keep segmented application code resident. Each segment's published header
   describes exactly which jump-table and twelve-byte transition-vector entries
   belong to it. Preparing these eagerly avoids executing the boot-dependent
   segment-loader patch while preserving the application's original code. */
PRIVATE int cfm68k_prepare_segments(ppc_context* ctx, ROMlib_pef_image* image)
{
    uint32* main = ppc_memory(ctx, ROMlib_pef_main(image), 8, FALSE);
    if (!main)
        return 0;
    uint32 world = CL(main[1]);
    unsigned count = Count1Resources(TICK("CODE"));
    ctx->segments = calloc(count ? count : 1, sizeof *ctx->segments);
    if (!ctx->segments)
        return 0;
    for (unsigned i = 1; i <= count; ++i) {
        Handle h = Get1IndResource(TICK("CODE"), i);
        if (!h || GetHandleSize(h) < 42)
            continue;
        HLock(h);
        uint32* header = (uint32*)STARH(h);
        if (CL(header[0]) != 0xfffd0000) {
            HUnlock(h);
            continue;
        }
        ctx->segments[ctx->n_segments++] = h;
        uint32 base = US_TO_SYN68K(STARH(h));
        for (unsigned table = 0; table < 2; ++table) {
            unsigned width = table ? 12 : 8;
            uint32 offset = CL(header[table ? 3 : 1]);
            uint32 n = CL(header[table ? 4 : 2]);
            int64 address = (int64)world + (int32)offset;
            if (address < 0 || address > UINT32_MAX || n > UINT32_MAX / width ||
                !ROMlib_pef_contains(image, address, n * width))
                return 0;
            unsigned char* entries = ppc_memory(ctx, address, n * width, TRUE);
            if (!entries)
                return 0;
            for (unsigned index = 0; index < n; ++index) {
                unsigned char* entry = entries + index * width;
                uint32 code_offset = CL(*(uint32*)(entry + (table ? 8 : 4)));
                if (table)
                    code_offset = (code_offset & 0xfffff) * 2;
                if (code_offset > (uint32)GetHandleSize(h) - 42)
                    return 0;
                if (!table)
                    *(uint16*)(entry + 2) = CWC(0x4ef9);
                *(uint32*)(entry + (table ? 0 : 4)) = CL(base + 40 + code_offset);
            }
        }
    }
    return ctx->n_segments != 0;
}

PRIVATE int cfm_launch_fragment(FSSpecPtr spec, uint32 offset, uint32 length, uint32 architecture, int segmented)
{
    ppc_context ctx;
    ROMlib_pef_image* image = NULL;
    ppc_fragment* fragment;
    INTEGER folder_vref;
    LONGINT folder_id;
    uint32 stack_address, halt_address;
    uint32 previous_system_version = system_version;
    uint32 saved_current_a5 = CL((LONGINT)CurrentA5);
    boolean_t raised_system_version = FALSE;
    void *saved_os[0x100], *saved_tools[0x400];
    uint32* halt;
    int success = 0;
    unsigned i;
    memset(&ctx, 0, sizeof ctx);
    ctx.architecture = architecture;
    if (architecture == kPowerPCArch)
        ROMlib_carbon_sound_input_reset(ROMlib_sdl_audio_test_mode());
    ctx.segmented = segmented;
    if (architecture == kPowerPCArch && ppc_jit_enabled)
        ctx.translations = ROMlib_ppc_translation_cache_create();
    memcpy(saved_os, ostraptable, sizeof saved_os);
    memcpy(saved_tools, tooltraptable, sizeof saved_tools);
    for (i = 0; i < NELEM(ctx.os_vectors); ++i)
        ctx.os_vectors[i] = CL((uint32)ostraptable[i]);
    for (i = 0; i < NELEM(ctx.tool_vectors); ++i)
        ctx.tool_vectors[i] = CL((uint32)tooltraptable[i]);
    ppc_scan_directory(&ctx, CW(spec->vRefNum), CL(spec->parID));
    if (FindFolder((int16)0x8000, kExtensionFolderType, FALSE, &folder_vref, &folder_id) == noErr)
        ppc_scan_directory(&ctx, CW(folder_vref), CL(folder_id));
    if (FindFolder((int16)0x8000, kTextEncodingsFolderType, FALSE, &folder_vref, &folder_id) == noErr)
        ppc_scan_directory(&ctx, CW(folder_vref), CL(folder_id));
    image = segmented ? cfm68k_prepare_resource(&ctx, spec) : ppc_prepare_file(&ctx, spec, offset, length);
    if (!image)
        goto done;
    /* A weak AppearanceLib import still signals that the app can use the
     native Appearance Manager. Select the compatibility version before
     running any fragment initializers or guest application code. */
    if (system_version < 0x810)
        for (i = 0; i < ROMlib_pef_library_count(image); ++i) {
            uint32 info[4];
            const char* name = ROMlib_pef_library_info(image, i, info);
            if (info[3] && !strcmp(name, "AppearanceLib")) {
                ROMlib_set_system_version(0x810);
                raised_system_version = TRUE;
                break;
            }
        }
    ctx.application = image;
    ctx.linking_application = 1;
    if (!ROMlib_pef_link(image)) {
        if (!ctx.error[0])
            snprintf(ctx.error, sizeof ctx.error, "%s", ROMlib_pef_error(image));
        goto done;
    }
    ctx.linking_application = 0;
    if (!ppc_link_pending(&ctx, spec))
        goto done;
    if (segmented && !cfm68k_prepare_segments(&ctx, image)) {
        snprintf(ctx.error, sizeof ctx.error, "Invalid CFM-68K segment headers or table extents");
        goto done;
    }
    /* Retain eager work even if guest startup or an eventual host shutdown
     prevents the normal termination path from publishing warmed code. */
    ppc_save_aot(&ctx, "published startup cache");
    {
        void* stack = ppc_allocate(&ctx, 256 * 1024, 16, &stack_address);
        if (!stack)
            goto done;
        memset(stack, 0, 256 * 1024);
    }
    halt = ppc_allocate(&ctx, 4, 4, &halt_address);
    if (!halt)
        goto done;
    *halt = CLC(0x18000000);
    ctx.halt = halt_address;
    if (architecture == kMotorola68KArch) {
        ctx.halt = callback_install(cfm68k_exit, &ctx);
        tooltraptable[0x1f4] = (void*)(uintptr_t)ctx.halt;
    }
    stack_address += 256 * 1024 - 256;
    if (!ppc_allocate(&ctx, 4, 4, &ctx.closure))
        goto done;
    if (!ppc_pin_launch_closure(&ctx))
        goto done;
    if (!ppc_bootstrap_prerequisites(&ctx, stack_address) || !ppc_startup_extensions(&ctx) ||
        !ppc_link_pending(&ctx, NULL))
        goto done;
    for (i = 0; i < NELEM(ctx.os_vectors); ++i)
        ctx.os_vectors[i] = CL((uint32)ostraptable[i]);
    for (i = 0; i < NELEM(ctx.tool_vectors); ++i)
        ctx.tool_vectors[i] = CL((uint32)tooltraptable[i]);
    for (fragment = ctx.fragments; fragment; fragment = fragment->next)
        if (fragment == fragment->owner && fragment->image && !ppc_initialize_library(&ctx, fragment, stack_address, 0))
            goto done;
    if (!ppc_pin_launch_closure(&ctx))
        goto done;
    ctx.application_started = 1;
    ctx.retry_system = 0;
    if (architecture == kMotorola68KArch) {
        uint32* main_vector = ppc_memory(&ctx, ROMlib_pef_main(image), 8, FALSE);
        if (!ROMlib_pef_main(image) || !main_vector) {
            snprintf(ctx.error, sizeof ctx.error, "CFM-68K application has no valid main transition vector");
            goto done;
        }
        HIDDEN_RAW_SET(CurrentA5_H, (Ptr)(uintptr_t)main_vector[1]);
    }
    success = ppc_initialize(&ctx, image, spec, "Application", offset, length, ctx.closure, stack_address, 0) &&
              ppc_call_vector(&ctx, image, ROMlib_pef_main(image), stack_address, 0, NULL);
    ROMlib_ppc_threads_destroy(ctx.threads);
    ctx.threads = NULL;
    if (success) {
        ctx.application_running = 1;
        success = ppc_call_vector(&ctx, image, ROMlib_pef_term(image), stack_address, 0, NULL);
    }
    {
        ppc_context* previous = ppc_active_context;
        ppc_active_context = &ctx;
        ROMlib_system_window_reset();
        ROMlib_layers_reset();
        ROMlib_component_reset();
        ROMlib_system_notify_reset();
        ppc_active_context = previous;
    }
    if (success)
        while (ctx.n_initialized) {
            fragment = ctx.init_order[--ctx.n_initialized];
            if (!ppc_call_vector(&ctx, fragment->image, ROMlib_pef_term(fragment->image), stack_address, 0, NULL)) {
                success = 0;
                break;
            }
        }

done: {
    if (architecture == kPowerPCArch)
        ROMlib_carbon_sound_input_reset(0);
    ROMlib_carbon_url_access_reset();
    ROMlib_carbon_icon_services_reset();
    ROMlib_carbon_drag_reset();
    ROMlib_tsm_dispose_documents();
    ROMlib_ppc_threads_destroy(ctx.threads);
    ctx.threads = NULL;
    ppc_context* previous = ppc_active_context;
    ppc_active_context = &ctx;
    ROMlib_carbon_navigation_reset();
    ROMlib_system_window_reset();
    ROMlib_layers_reset();
    ROMlib_component_reset();
    ROMlib_system_notify_reset();
    ppc_active_context = previous;
}
    if (success)
        fprintf(stderr, "trace: %s fragment completed\n", architecture == kMotorola68KArch ? "CFM-68K" : "PowerPC");
    if (!success)
        fprintf(stderr, "%s launch failed: %s\n", architecture == kMotorola68KArch ? "CFM-68K" : "PowerPC",
                ctx.error[0] ? ctx.error : "unable to prepare application");
    memcpy(ostraptable, saved_os, sizeof saved_os);
    memcpy(tooltraptable, saved_tools, sizeof saved_tools);
    if (architecture == kMotorola68KArch && ctx.halt)
        callback_remove(ctx.halt);
    ROMlib_menu_restore_bar(FALSE);
    if (ctx.translations) {
        ppc_save_aot(&ctx, "saved cache");
        ROMlib_ppc_translation_cache_destroy(ctx.translations);
    }
    ROMlib_pef_unload(image);
    for (i = 0; i < ctx.n_segments; ++i)
        HUnlock(ctx.segments[i]);
    free(ctx.segments);
    ppc_free_closures(&ctx);
    while (ctx.fragments) {
        fragment = ctx.fragments;
        ctx.fragments = fragment->next;
        if (fragment == fragment->owner)
            ROMlib_pef_unload(fragment->image);
        while (fragment->dependencies) {
            ppc_dependency* dependency = fragment->dependencies;
            fragment->dependencies = dependency->next;
            free(dependency);
        }
        free(fragment);
    }
    while (ctx.application_dependencies) {
        ppc_dependency* edge = ctx.application_dependencies;
        ctx.application_dependencies = edge->next;
        free(edge);
    }
    ROMlib_native_memory_destroy(ctx.memory_manager);
    while (ctx.allocations)
        ppc_release(&ctx, ctx.allocations->aligned);
    while (ctx.callback_stacks) {
        ppc_callback_stack* stack = ctx.callback_stacks;
        ctx.callback_stacks = stack->next;
        free(stack);
    }
    for (i = 0; i < ctx.n_imports; ++i) {
        if (architecture == kMotorola68KArch)
            callback_remove(ctx.imports[i].stub);
        free(ctx.imports[i].name);
    }
    for (i = 0; i < NELEM(ctx.streams); ++i)
        if (ctx.streams[i])
            fclose(ctx.streams[i]);
    free(ctx.imports);
    free(ctx.system_l2);
    /* Normal application return/ExitToShell and successful library termination
     establish a working run; merely passing CarbonLib's initializer does not. */
    ROMlib_ecf_complete_startup(success);
    if (!success) {
        if (ctx.required_system && !ctx.application_running &&
            ROMlib_ecf_require_system(spec, ctx.required_system) > 0) {
            ROMlib_exit = 1;
            C_ExitToShell();
        }
        /* Retry only known startup compatibility/memory failures before normal
         event processing, after cleanup, in a fresh address space. Ordinary
         application faults and failures in running apps are not replayed. */
        if (ctx.retry_system && !ctx.application_running && ROMlib_ecf_restart_system(spec, system_version) > 0) {
            ROMlib_exit = 1;
            C_ExitToShell();
        }
        char message[640];
        snprintf(message, sizeof message, "This %s application could not start.\n\n%s",
                 architecture == kMotorola68KArch ? "CFM-68K" : "PowerPC",
                 ctx.error[0] ? ctx.error : "Unable to read or prepare the application.");
        ROMlib_win_ppc_error(message);
    }
    if (raised_system_version)
        ROMlib_set_system_version(previous_system_version);
    if (architecture == kMotorola68KArch)
        HIDDEN_RAW_SET(CurrentA5_H, (Ptr)(uintptr_t)CL(saved_current_a5));
    return success;
}

PUBLIC int ROMlib_launch_ppc_fragment(FSSpecPtr spec, uint32 offset, uint32 length)
{
    return cfm_launch_fragment(spec, offset, length, kPowerPCArch, FALSE);
}

PUBLIC int ROMlib_launch_cfm68k_fragment(FSSpecPtr spec, uint32 offset, uint32 length)
{
    return cfm_launch_fragment(spec, offset, length, kMotorola68KArch, FALSE);
}

PUBLIC int ROMlib_launch_cfm68k_resource(FSSpecPtr spec)
{
    return cfm_launch_fragment(spec, 0, 0, kMotorola68KArch, TRUE);
}

/* Classic CODE applications may open CFM-68K plug-ins without having a PEF
   application fragment. Their connection closure lives until ExitToShell. */
PUBLIC void ROMlib_cfm68k_shutdown(void)
{
    ppc_context* ctx = cfm68k_classic_context;
    if (!ctx)
        return;
    cfm68k_classic_context = NULL;
    ppc_callback_stack* stack = ppc_acquire_callback_stack(ctx);
    if (stack)
        while (ctx->n_initialized) {
            ppc_fragment* fragment = ctx->init_order[--ctx->n_initialized];
            if (!ppc_call_vector(ctx, fragment->image, ROMlib_pef_term(fragment->image),
                                 stack->address + 256 * 1024 - 256, 0, NULL))
                fprintf(stderr, "CFM-68K plug-in termination failed: %s\n", ctx->error);
        }
    ppc_active_context = NULL;
    ppc_free_closures(ctx);
    while (ctx->fragments) {
        ppc_fragment* fragment = ctx->fragments;
        ctx->fragments = fragment->next;
        if (fragment == fragment->owner)
            ROMlib_pef_unload(fragment->image);
        while (fragment->dependencies) {
            ppc_dependency* edge = fragment->dependencies;
            fragment->dependencies = edge->next;
            free(edge);
        }
        free(fragment);
    }
    ROMlib_native_memory_destroy(ctx->memory_manager);
    while (ctx->allocations)
        ppc_release(ctx, ctx->allocations->aligned);
    for (unsigned i = 0; i < ctx->n_imports; ++i) {
        callback_remove(ctx->imports[i].stub);
        free(ctx->imports[i].name);
    }
    free(ctx->imports);
    while (ctx->callback_stacks) {
        stack = ctx->callback_stacks;
        ctx->callback_stacks = stack->next;
        free(stack);
    }
    for (unsigned i = 0; i < NELEM(ctx->streams); ++i)
        if (ctx->streams[i])
            fclose(ctx->streams[i]);
    free(ctx->system_l2);
    free(ctx);
    fprintf(stderr, "trace: CFM-68K classic connection closure released\n");
}
#endif
