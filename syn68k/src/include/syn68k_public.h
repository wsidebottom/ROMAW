/*+===================================================================
  File:      syn68k_public.h

  Summary:   Declares syn68k public support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in syn68k/src/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#if !defined(_syn68k_public_h_)
#define _syn68k_public_h_

#if !defined(__x86_64__)
#error "syn68k requires an x86-64 host"
#endif

#include <stdint.h>
#include <setjmp.h>

/* Decide whether we are big or little endian here.  Add more machines as
 * they are supported.
 */
#if !defined(BIGENDIAN) && !defined(LITTLEENDIAN)
#if defined(__BIG_ENDIAN__)
#define BIGENDIAN /* ARDI naming convention, why rock the boat? */
#else
#define LITTLEENDIAN
#endif
#endif

/* Translated blocks are checksummed and interrupts are polled on every supported build. */

/* Portability typedefs for signed and unsigned n-bit numbers. */

/* TODO: just use the ones from stdint.h instead of our own */

typedef int8_t int8;
typedef uint8_t uint8;
typedef int16_t int16;
typedef uint16_t uint16;
typedef int32_t int32;
typedef uint32_t uint32;
typedef int64_t int64;
typedef uint64_t uint64;

/* Typedef for address in 68k space. */
typedef uint32 syn68k_addr_t;

/* Windows uses 32-bit long even when pointers are 64 bits. */
typedef uintptr_t ptr_sized_uint;

/* Because a 68k register can be referenced as a byte, word, or int32,
 * we represent them as a union of these types.  Although not guaranteed
 * to be portable, operations on appropriate byte- and word- union elements
 * should leave the other bytes unaffected (just as they do on the 68k).
 */
#if defined(LITTLEENDIAN)
typedef union {
    struct {
        uint32 n;
    } ul;

    struct {
        int32 n;
    } sl;

    struct {
        uint16 n, hi;
    } uw;

    struct {
        int16 n, hi;
    } sw;

    struct {
        uint8 n, b1, b2, b3;
    } ub;

    struct {
        int8 n, b1, b2, b3;
    } sb;
} M68kReg;
#else
typedef union {
    struct {
        uint32 n;
    } ul;

    struct {
        int32 n;
    } sl;

    struct {
        uint16 hi, n;
    } uw;

    struct {
        int16 hi, n;
    } sw;

    struct {
        uint8 b3, b2, b1, n;
    } ub;

    struct {
        int8 b3, b2, b1, n;
    } sb;
} M68kReg;
#endif

/* Function type that handles callbacks (and therefore traps). */
typedef syn68k_addr_t (*callback_handler_t)(syn68k_addr_t, void*);

typedef struct {
    syn68k_addr_t callback_address;
    callback_handler_t func;
    void* arg;
} TrapHandlerInfo;

typedef int32 CCRElement;

typedef struct {
    syn68k_addr_t tag;
    const uint16* code;
} jsr_stack_elt_t;

#define JSR_STACK_SIZE 8

typedef struct {
    uint16 exponent;
    uint64 significand;
} Syn68kFP80;

typedef struct {
    Syn68kFP80 regs[8];
    uint32 fpcr, fpsr, fpiar;
    unsigned null_state;
    /* Architectural FSAVE image, in guest byte order; zero size means idle. */
    uint8 frame[100];
    unsigned frame_size;
    unsigned pending_vector;
} Syn68kFPUState;

typedef struct {
    M68kReg regs[16];                    /* d0...d7 followed by a0...a7 */
    CCRElement ccnz, ccn, ccc, ccv, ccx; /* 0 or non-0, NOT 0 or 1! */
#if defined(MINIMAL_CPU_STATE)
    uint8 filler[3]; /* So we can copy small cpu states w/out boundary cruft. */
#else                /* !MINIMAL_CPU_STATE */
    char *amode_p, *reversed_amode_p;        /* Used in interpreter. */
    volatile int32 interrupt_status_changed; /* High bit set when interrupted. */
    jmp_buf* setjmp_buf;
    jsr_stack_elt_t jsr_stack[JSR_STACK_SIZE];
    unsigned jsr_stack_byte_index;       /* # of **BYTES** into jsr stack for top. */
    volatile uint16 sr;                  /* Status register, except cc bits (low 5 bits) == 0. */
    syn68k_addr_t vbr;                   /* Vector base register (in 68k address space). */
    uint32 cacr, caar;                   /* Cache Control Register/Cache Address Register. */
    uint32 usp, msp, isp;                /* Various stack pointers.  Whichever one is
			  * currently in use can only be found in a7;
			  * the value here will be outdated. */
    volatile uint8 interrupt_pending[8]; /* 1 if interrupt pending. */
    volatile TrapHandlerInfo trap_handler_info[64];
    Syn68kFPUState fpu;
    uint32 sfc, dfc; /* Appended to preserve all generated register offsets. */
#endif               /* !MINIMAL_CPU_STATE */
} CPUState;

void syn68k_fpu_reset(void);
/* The caller owns a word-aligned guest allocation of syn68k_fpsp_size()
   bytes, kept alive while the installed vectors may execute. Failed installs
   leave the allocation and vector table unchanged. */
uint32 syn68k_fpsp_size(void);
int syn68k_fpsp_install(syn68k_addr_t base);
int syn68k_fpu_execute(syn68k_addr_t pc, syn68k_addr_t* next);
int syn68k_memory_span(syn68k_addr_t address, uint32 length);
/* Optional data-bus reads outside RAM/host aliases. Return one only when the
   entire byte/word/long transfer was handled. Values use host byte order.
   This does not map executable memory or permit writes. Install before
   translating guest code; changing the handler requires invalidating blocks. */
extern int (*syn68k_bus_read)(uint32 address, unsigned size, uint32* value);

/* This lets us put syn68k.c's global register variable before the
 * inline functions in this file, since that is an error.
 */
#if defined(GLOBAL_REGISTER_DECLS)
GLOBAL_REGISTER_DECLS
#endif

#define EM_DREG(X) (cpu_state.regs[X].ul.n)
#define EM_AREG(X) (cpu_state.regs[8 + (X)].ul.n)
#define EM_D0 EM_DREG(0)
#define EM_D1 EM_DREG(1)
#define EM_D2 EM_DREG(2)
#define EM_D3 EM_DREG(3)
#define EM_D4 EM_DREG(4)
#define EM_D5 EM_DREG(5)
#define EM_D6 EM_DREG(6)
#define EM_D7 EM_DREG(7)
#define EM_A0 EM_AREG(0)
#define EM_A1 EM_AREG(1)
#define EM_A2 EM_AREG(2)
#define EM_A3 EM_AREG(3)
#define EM_A4 EM_AREG(4)
#define EM_A5 EM_AREG(5)
#define EM_A6 EM_AREG(6)
#define EM_A7 EM_AREG(7)
#define EM_FP EM_A6
#define EM_SP EM_A7

/* We need a range of "magic" addresses where we know legitimate 68k
 * code will be stored.  When code at those addresses is "executed" by
 * the 68k, we will instead take special actions.  We want those
 * addresses to be dereferenceable so we'll overlay them over a big
 * block of legitimate memory that can never contain m68k code.
 * We'll start a few bytes into the array in case anyone examines
 * memory before the array.
 */
extern uint16 callback_dummy_address_space[];
extern uint16* syn68k_callback_address_space;
#define CALLBACK_SLOP 16
#define MAGIC_ADDRESS_BASE ((syn68k_addr_t)US_TO_SYN68K(&syn68k_callback_address_space[CALLBACK_SLOP]))
#define MAGIC_EXIT_EMULATOR_ADDRESS (MAGIC_ADDRESS_BASE + 0)
#define MAGIC_RTE_ADDRESS (MAGIC_ADDRESS_BASE + 2)

/* Address bits that have meaning, for the CLEAN() macro, below. */
#define LEGAL_ADDRESS_BITS 0xFFFFFFFFUL

/* Here is a macro to "clean" an address; if you make it mask out the
 * nasty bits, you risk a noticeable performance downgrade.
 */
#if 1
#define CLEAN(addr) ((ptr_sized_uint)(addr))
#else
#define CLEAN(addr) (((ptr_sized_uint)(addr)) & LEGAL_ADDRESS_BITS)
#endif

/* TODO: see if get rid of need for SIZEOF_CHAR_P, since that's something
         that comes from config.h and we shouldn't require users of
         syn68k_public.h to require config.h */

#if !defined(SIZEOF_CHAR_P)

#define SIZEOF_CHAR_P 8

#endif

#if SIZEOF_CHAR_P == 4

extern uint32 ROMlib_offset;
#define SYN68K_TO_US_RAW(addr) ((uint16*)((uint32)(addr) + ROMlib_offset))
#define US_TO_SYN68K_RAW(addr) ((uint32)(ptr_sized_uint)(addr) - ROMlib_offset)

#elif SIZEOF_CHAR_P == 8

extern uint64 ROMlib_offset;
#define SYN68K_TO_US_RAW(addr) ((uint16*)((uint64)(uint32)(addr) + ROMlib_offset))
#define US_TO_SYN68K_RAW(addr) ((uint32)((ptr_sized_uint)(addr) - ROMlib_offset))

#else
#error "SIZEOF_CHAR_P unknown"
#endif

extern uint32 syn68k_address_mask;
extern unsigned syn68k_host_alias_count;
extern uint32 syn68k_host_alias_base, syn68k_host_alias_size;
extern void* syn68k_guest_to_host(uint32 address);
extern uint32 syn68k_host_to_guest(ptr_sized_uint address);
/* Return an existing mapping without reserving space for a new host pointer. */
extern int syn68k_lookup_guest_address(ptr_sized_uint address, uint32* guest);
extern void syn68k_set_memory_window(uint32 size, uint32 alias_base, uint32 alias_size);
extern void syn68k_set_addressing_mode(int mode32);
/* Report the active backend after initialization or an addressing-mode change. */
int syn68k_native_code_enabled(void);

static inline uint16* syn68k_to_us(uint32 address)
{
    address &= syn68k_address_mask;
    if (syn68k_host_alias_count && address - syn68k_host_alias_base < syn68k_host_alias_size)
        return (uint16*)syn68k_guest_to_host(address);
    return SYN68K_TO_US_RAW(address);
}

#define SYN68K_TO_US(addr) syn68k_to_us((uint32)(ptr_sized_uint)(addr))
#define US_TO_SYN68K(addr)                                                                     \
    ((syn68k_address_mask == 0xFFFFFFFFUL && !syn68k_host_alias_size) ? US_TO_SYN68K_RAW(addr) \
                                                                      : syn68k_host_to_guest((ptr_sized_uint)(addr)))
/* Effective addresses retain tag bits for LEA; mask only on memory access. */
#define SYN68K_AMODE_US(ptr) SYN68K_TO_US(US_TO_SYN68K_RAW(ptr))

/* These macros should not be used within Syn68k, but are needed in
	ROMlib for translations of addresses where it's important to preserve
	the address 0 as 0 */

#define SYN68K_TO_US_CHECK0(addr)                        \
    ({                                                   \
        uint32 t = (uint32)(addr) & syn68k_address_mask; \
        t ? SYN68K_TO_US(t) : (uint16*)0;                \
    })
#define US_TO_SYN68K_CHECK0(addr)       \
    ({                                  \
        typeof(addr) t;                 \
        t = addr;                       \
        t ? US_TO_SYN68K(t) : (int32)0; \
    })

/* Macros for byte swapping + specifying signedness.  On a big endian
 * machine, these macros are dummies and don't actually swap bytes.
 * You may wish to replace these with the most efficient inline assembly
 * you can get, as these macros are performance-critical.
 * The "_IFLE" suffix stands for "if little endian".
 */

/* Byte swapping macros. */
#define SWAPUB(val) ((uint8)(val)) /* Here for symmetry. */
#define SWAPSB(val) ((int8)(val))

#if defined(CWV) && defined(CLV)
#define ROMlib_SwapShort(n) CWV((uint16)(n))
#define ROMlib_SwapLong(n) CLV((uint32)(n))
#define SWAPUW(n) CWV((uint16)(n))
#define SWAPUL(n) CLV((uint32)(n))
#elif defined(LITTLEENDIAN)

#if defined(__GNUC__)
#define SWAPUW(val)                      \
    ({                                   \
        uint16 _v = (val);               \
        (uint16)((_v >> 8) | (_v << 8)); \
    })
#define SWAPUL(val)                                                                      \
    ({                                                                                   \
        uint32 _v = (val);                                                               \
        (uint32)((_v >> 24) | (_v << 24) | ((_v >> 8) & 0xFF00) | ((_v & 0xFF00) << 8)); \
    })
#else

static uint16 _swapuw(uint16 v)
{
    return (v >> 8) | (v << 8);
}

#define SWAPUW(val) _swapuw(val)

static uint32 _swapul(uint32 v)
{
    return (v >> 24) | (v << 24) | ((v >> 8) & 0xFF00) | ((v & 0xFF00) << 8);
}

#define SWAPUL(val) _swapul(val)

#endif

#define ROMlib_SwapShort(val) SWAPUW(val)
#define ROMlib_SwapLong(val) SWAPUL(val)

#endif
#define SWAPSW(val) ((int16)SWAPUW(val))
#define SWAPSL(val) ((int32)SWAPUL(val))

#define SWAPUB_IFLE(n) ((uint8)(n)) /* Here for symmetry. */
#define SWAPSB_IFLE(n) ((int8)(n))

#if defined(BIGENDIAN)
#define SWAPUW_IFLE(n) ((uint16)(n))
#define SWAPSW_IFLE(n) ((int16)(n))
#define SWAPUL_IFLE(n) ((uint32)(n))
#define SWAPSL_IFLE(n) ((int32)(n))
#else /* LITTLEENDIAN */
#define SWAPUW_IFLE(n) SWAPUW(n)
#define SWAPSW_IFLE(n) SWAPSW(n)
#define SWAPUL_IFLE(n) SWAPUL(n)
#define SWAPSL_IFLE(n) SWAPSL(n)
#endif

/* The "slow" variants are here to facilitate compile-time reduction of
 * swapped constants.  The non-slow versions may use inline assembly, which
 * the C compiler won't be able to deal with, but the slow versions are
 * guaranteed to use standard C operators.  You would only want to use the
 * slow versions for swapping constants.
 */
#define SLOW_SWAPUB(val) ((uint8)(val)) /* Here for symmetry. */
#define SLOW_SWAPSB(val) ((int8)(val))

#if defined(__GNUC__)
#define SLOW_SWAPUW(val)                 \
    ({                                   \
        uint16 _v = (val);               \
        (uint16)((_v >> 8) | (_v << 8)); \
    })
#else
#define SLOW_SWAPUW(v) ((uint16)(((v) >> 8) | ((v) << 8)))
#endif

#define SLOW_SWAPSW(val) ((int16)SLOW_SWAPUW(val))

#if defined(__GNUC__)
#define SLOW_SWAPUL(val)                                                                 \
    ({                                                                                   \
        uint32 _v = (val);                                                               \
        (uint32)((_v >> 24) | (_v << 24) | ((_v >> 8) & 0xFF00) | ((_v & 0xFF00) << 8)); \
    })
#else
#define SLOW_SWAPUL(v) ((uint32)(((v) >> 24) | ((v) << 24) | (((v) >> 8) & 0xFF00) | (((v) & 0xFF00) << 8)))
#endif

#define SLOW_SWAPSL(val) ((int32)SLOW_SWAPUL(val))

#define SLOW_SWAPUB_IFLE(n) ((uint8)(n)) /* Here for symmetry. */
#define SLOW_SWAPSB_IFLE(n) ((int8)(n))

#if defined(BIGENDIAN)
#define SLOW_SWAPUW_IFLE(n) ((uint16)(n))
#define SLOW_SWAPSW_IFLE(n) ((int16)(n))
#define SLOW_SWAPUL_IFLE(n) ((uint32)(n))
#define SLOW_SWAPSL_IFLE(n) ((int32)(n))
#else /* LITTLEENDIAN */
#define SLOW_SWAPUW_IFLE(n) SLOW_SWAPUW(n)
#define SLOW_SWAPSW_IFLE(n) SLOW_SWAPSW(n)
#define SLOW_SWAPUL_IFLE(n) SLOW_SWAPUL(n)
#define SLOW_SWAPSL_IFLE(n) SLOW_SWAPSL(n)
#endif

/* Handy memory access macros.  These implicity refer to the 68k addr space
   except for the _US variants that refer to the native address space.*/

/* Memory reads. */
#define READUB(addr) (*(const uint8*)SYN68K_TO_US(CLEAN(addr)))
#define READUW(addr) SWAPUW_IFLE(*(const uint16*)SYN68K_TO_US(CLEAN(addr)))

#if defined(BIGENDIAN) && defined(QUADALIGN)
#define READUL(addr)                                  \
    ({                                                \
        const uint16* _p = SYN68K_TO_US(CLEAN(addr)); \
        (uint32)((_p[0] << 16) | _p[1]);              \
    })
#define READUL_US(addr)                  \
    ({                                   \
        const uint16* _p = CLEAN(addr);  \
        (uint32)((_p[0] << 16) | _p[1]); \
    })
#elif defined(BIGENDIAN) && !defined(QUADALIGN)
#define READUL(addr) (*(uint32*)SYN68K_TO_US(CLEAN(addr)))
#define READUL_US(addr) (*(uint32*)CLEAN(addr))
#elif !defined(BIGENDIAN) && defined(QUADALIGN)

#if defined(__GNUC__)
#define READUL(addr)                                                    \
    ({                                                                  \
        const uint8* _p = (const uint8*)SYN68K_TO_US(CLEAN(addr));      \
        (uint32)((_p[0] << 24) | (_p[1] << 16) | (_p[2] << 8) | _p[3]); \
    })
#define READUL_US(addr)                                                 \
    ({                                                                  \
        const uint8* _p = (const uint8*)CLEAN(addr);                    \
        (uint32)((_p[0] << 24) | (_p[1] << 16) | (_p[2] << 8) | _p[3]); \
    })
#else
static uint32 _readul(syn68k_addr_t addr)
{
    const uint8* p;

    p = (const uint8*)SYN68K_TO_US(CLEAN(addr));
    return (uint32)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
}

#define READUL(addr) _readul(addr)

static uint32 _readul_us(syn68k_addr_t addr)
{
    const uint8* p;

    p = (const uint8*)CLEAN(addr);
    return (uint32)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
}

#define READUL_US(addr) _readul_us(addr)
#endif

#else /* !defined (BIGENDIAN) && !defined (QUADALIGN) */
#define READUL(addr) SWAPUL(*(uint32*)SYN68K_TO_US(CLEAN(addr)))
#define READUL_US(addr) SWAPUL(*(uint32*)CLEAN(addr))
#endif
#define READSB(addr) ((int8)READUB(addr))
#define READSW(addr) ((int16)READUW(addr))
#define READSL(addr) ((int32)READUL(addr))

/* Memory writes. */
#define WRITEUB(addr, val) (*(uint8*)SYN68K_TO_US(CLEAN(addr)) = (val))

#if defined(BIGENDIAN)
#define WRITEUW(addr, val) (*(uint16*)SYN68K_TO_US(CLEAN(addr)) = (val))
#else
#define WRITEUW(addr, val) (*(uint16*)SYN68K_TO_US(CLEAN(addr)) = SWAPUW(val))
#endif

#if defined(BIGENDIAN) && defined(QUADALIGN)
#define WRITEUL(addr, val)                               \
    do {                                                 \
        uint32 _v = (val);                               \
        uint16* _p = (uint16*)SYN68K_TO_US(CLEAN(addr)); \
        _p[0] = _v >> 16;                                \
        _p[1] = _v;                                      \
    } while (0)
#elif defined(BIGENDIAN) && !defined(QUADALIGN)
#define WRITEUL(addr, val) (*(uint32*)(SYN68K_TO_US(CLEAN(addr))) = (val))
#elif !defined(BIGENDIAN) && defined(QUADALIGN)
#define WRITEUL(addr, val)                             \
    do {                                               \
        uint32 _v = (val);                             \
        uint8* _p = (uint8*)SYN68K_TO_US(CLEAN(addr)); \
        _p[0] = _v >> 24;                              \
        _p[1] = _v >> 16;                              \
        _p[2] = _v >> 8;                               \
        _p[3] = _v;                                    \
    } while (0)
#else /* !defined (BIGENDIAN) && !defined (QUADALIGN) */
#define WRITEUL(addr, val) (*(uint32*)SYN68K_TO_US(CLEAN(addr)) = SWAPUL(val))
#endif

#define WRITESB(addr, val) WRITEUB((addr), (uint8)(val))
#define WRITESW(addr, val) WRITEUW((addr), (uint16)(val))
#define WRITESL(addr, val) WRITEUL((addr), (uint32)(val))

/* Pop instructions. */
#define POPUB() (EM_A7 += 2, READUB(EM_A7 - 2))
#define POPUW() (EM_A7 += 2, READUW(EM_A7 - 2))
#define POPUL() (EM_A7 += 4, READUL(EM_A7 - 4))
#define POPSB() ((int8)POPUB())
#define POPSW() ((int16)POPUW())
#define POPSL() ((int32)POPUL())
#define POPADDR() POPUL()

/* Push instructions */
#define PUSHUB(val) WRITEUB(EM_A7 -= 2, (val))
#define PUSHUW(val) WRITEUW(EM_A7 -= 2, (val))
#define PUSHUL(val) WRITEUL(EM_A7 -= 4, (val))
#define PUSHSB(val) PUSHUB((uint8)(val))
#define PUSHSW(val) PUSHUW((uint16)(val))
#define PUSHSL(val) PUSHUL((uint32)(val))
#define PUSHADDR(val) PUSHUL(val)

#if !defined(CALL_EMULATOR)
#define CALL_EMULATOR(addr)                                            \
    do {                                                               \
        PUSHADDR(MAGIC_EXIT_EMULATOR_ADDRESS);                         \
        (interpret_code(hash_lookup_code_and_create_if_needed(addr))); \
    } while (0)
#endif

/* Global struct describing the CPU state. */
extern CPUState cpu_state;

/* I hate to put this here, but we want our interrupt polling to
 * be extremely fast so we can check for interrupts anywhere in
 * romaw that might be time consuming.
 */

#define M68K_TIMER_PRIORITY 4
#define M68K_TIMER_VECTOR (24 + M68K_TIMER_PRIORITY)

#define INTERRUPT_STATUS_CHANGED (-1)
#define INTERRUPT_STATUS_UNCHANGED 0x7FFFFFFF

#define FETCH_INTERRUPT_STATUS() cpu_state.interrupt_status_changed
#define SET_INTERRUPT_STATUS(n) ((void)(cpu_state.interrupt_status_changed = (n)))

#define INTERRUPT_PENDING() (FETCH_INTERRUPT_STATUS() < 0)

/* Functions to be called from outside the emulator. */
extern void initialize_68k_emulator(void (*while_busy)(int), int native_p, uint32 trap_vector_storage[64],
                                    uint32 reserved_interrupt_flag_addr);

extern void interrupt_generate(unsigned priority);
extern void interrupt_note_if_present(void);
/* Preserve the exported symbol spelling used by existing library consumers. */
extern syn68k_addr_t interrupt_process_any_pending(syn68k_addr_t pc) asm("_interrupt_process_any_pending");

extern void interpret_code(const uint16* code);
/* Independent of native machine-code translation; reports interpreter dispatch. */
extern int syn68k_threaded_dispatch_enabled(void);
/* Count entries into generated x64 blocks, independently of fallback. */
extern uint64 syn68k_native_execution_count(void);
/* Process-lifetime counters: restored native prefixes and emitter invocations.
   Set SYN68K_AOT_CACHE to an existing trusted local directory to persist code. */
extern uint64 syn68k_aot_hit_count(void);
extern uint64 syn68k_native_compile_count(void);
/* Opaque native callback scope for a caller's nonlocal exit. Unwind to a
   checkpoint from the current active chain before longjmp; NULL unwinds all.
   Invalid/nonancestor checkpoints return zero without changing active scopes. */
extern void* syn68k_native_checkpoint(void);
extern int syn68k_native_unwind(void* checkpoint);
/* Generated frames have no Windows SEH tables. Pair this jump checkpoint with
   syn68k_native_unwind before longjmp instead of asking the CRT to walk them.
   This is MinGW's __USE_MINGW_SETJMP_NON_SEH expansion, scoped to native exits. */
#if defined(_WIN64) && defined(__MINGW32__)
#define SYN68K_NATIVE_SETJMP(buffer) _setjmp((buffer), (void*)0)
#else
#define SYN68K_NATIVE_SETJMP(buffer) setjmp(buffer)
#endif
/* Preserve the exported symbol spelling used by existing library consumers. */
extern const uint16*
hash_lookup_code_and_create_if_needed(syn68k_addr_t adr) asm("_hash_lookup_code_and_create_if_needed");

extern unsigned long destroy_blocks(syn68k_addr_t low_m68k_address, uint32 num_bytes);
extern syn68k_addr_t callback_install(callback_handler_t func, void* arbitrary_argument);
extern void callback_remove(syn68k_addr_t m68k_address);
extern void trap_install_handler(unsigned trap_number, callback_handler_t func, void* arbitrary_argument);
/* RESET asserts the external reset output; it does not reset the CPU itself.
   Install after initialization. NULL removes the device-bus callback. */
typedef void (*syn68k_reset_handler_t)(void* opaque);
extern void syn68k_set_reset_handler(syn68k_reset_handler_t handler, void* opaque);
extern void trap_remove_handler(unsigned trap_number);
extern void* callback_argument(syn68k_addr_t callback_address);
extern callback_handler_t callback_function(syn68k_addr_t callback_address);
extern void dump_profile(const char* file);

extern unsigned long destroy_blocks_with_checksum_mismatch(syn68k_addr_t low_m68k_address, uint32 num_bytes);

extern void m68kaddr(const uint16* pc);

#endif /* Not _syn68k_public_h_ */
