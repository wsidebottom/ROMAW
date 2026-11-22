/*+===================================================================
  File:      sysdeps.h

  Summary:   Define portable interpreter types, configuration and ROMAW runtime hooks.

  Classes:   Declared: romaw_ppc_arch_fault; Used: romaw_ppc_arch_fault, std::runtime_error

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in synppc/platform; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

#ifndef ROMAW_PPC_SYSDEPS_H
#define ROMAW_PPC_SYSDEPS_H
#include <stdint.h>
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <fenv.h>
#include <time.h>
#include <new>
#include <stdexcept>
#include "synppc/ppc.h"
typedef uint8_t uint8;
typedef int8_t int8;
typedef uint16_t uint16;
typedef int16_t int16;
typedef uint32_t uint32;
typedef int32_t int32;
typedef uint64_t uint64;
typedef int64_t int64;
typedef uintptr_t uintptr;
typedef int spinlock_t;
#define SPIN_LOCK_UNLOCKED 0

static inline void spin_lock(spinlock_t* p)
{
    while (__sync_lock_test_and_set(p, 1)) {
    }
}

static inline void spin_unlock(spinlock_t* p)
{
    __sync_lock_release(p);
}

#define ROMAW_PPC 1
#define SHEEPSHAVER 1
#define EMULATED_PPC 1
#define ENABLE_DYNGEN 0
#define ENABLE_MON 0
#define PPC_ENABLE_JIT 0
#define PPC_DECODE_CACHE 0
#define PPC_CHECK_INTERRUPTS 0
#define PPC_FLIGHT_RECORDER 0
#define KPX_MAX_CPUS 1
#define HAVE_FENV_H 1
#define HAVE_FENV 1
#define HAVE_ISNAN 1
#define HAVE_ISINF 1
#define HAVE_ISFINITE 1
#define HAVE_SIGNBIT 1
#define HAVE_LOG2 1
#define HAVE_EXP2 1
#define HAVE_ROUND 1
#define HAVE_TRUNC 1
#define HAVE_SQRTF 1
#define HAVE_LRINT 1
#define HAVE_LRINTF 1
#define HAVE_LLRINT 1
#define HAVE_LLRINTF 1
#define HAVE_NEARBYINT 1
#define HAVE_NEARBYINTF 1
#define HAVE_FMA 1
#define HAVE_FMAF 1
#define SIZEOF_VOID_P __SIZEOF_POINTER__
#define SIZEOF_LONG __SIZEOF_LONG__
#define SIZEOF_INT 4
#define SIZEOF_SHORT 2
#define UVAL64(v) UINT64_C(v)
#define VAL64(v) INT64_C(v)
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
extern bool romaw_ppc_forward(uint32 pc);
extern void romaw_ppc_power_wait(bool waiting);
extern void romaw_ppc_step(uint32 pc);
class powerpc_cpu;
extern bool romaw_ppc_native_step(powerpc_cpu* cpu, uint32* pc);
extern void romaw_ppc_system_step(powerpc_cpu* cpu);
[[noreturn]] void romaw_ppc_invalid_instruction(const char* reason);
extern uint64 romaw_ppc_fetch(uint32 address, uint32& physical);
extern bool romaw_ppc_system_service(uint32 pc, uint32 physical, uint32 opcode);
struct ROMlib_ppc_bus_result;
extern bool romaw_ppc_translate(uint32 address, unsigned access, ROMlib_ppc_bus_result*);
extern uint32 romaw_ppc_external(uint32 address, bool write, uint32 value);
extern void romaw_ppc_cache_control(uint32 address, unsigned opcode);
extern uint64 romaw_ppc_load(uint32 address, unsigned size);
extern void romaw_ppc_store(uint32 address, unsigned size, uint64 value);
extern uint32 romaw_ppc_load_reserved(powerpc_cpu*, uint32 address);
extern void romaw_ppc_store_conditional(powerpc_cpu*, uint32 address, uint32 value);
extern void romaw_ppc_transfer_multiple(powerpc_cpu*, uint32 address, unsigned reg, bool write);
extern void romaw_ppc_transfer_string(powerpc_cpu*, uint32 address, unsigned reg, unsigned bytes, bool write);

static inline unsigned romaw_ppc_string_load_limit(unsigned rd, unsigned ra, unsigned rb, bool indexed)
{
    unsigned distance = (ra - rd) & 31;
    if (indexed && ((rb - rd) & 31) < distance)
        distance = (rb - rd) & 31;
    return distance;
}

struct romaw_ppc_arch_fault {
    unsigned vector;
    uint32 cause, address;
};

extern int romaw_ppc_set_rounding(int mode);
extern int romaw_ppc_arithmetic_value(uint32_t opcode, uint64_t a, uint64_t b, uint64_t c, uint32_t* fpscr,
                                      uint64_t* output);
extern int romaw_ppc_fused_value(uint32_t opcode, uint64_t a, uint64_t b, uint64_t c, uint32_t* fpscr,
                                 uint64_t* output);
extern bool romaw_ppc_arithmetic_exceptions(uint32_t opcode, uint64_t a, uint64_t b, uint64_t c, uint32_t* fpscr);
extern bool romaw_ppc_integer_value(uint64_t value, unsigned rounding, uint32_t* fpscr, uint64_t* output);
extern unsigned romaw_ppc_compare_value(uint64_t a, uint64_t b, bool ordered, uint32_t* fpscr);
extern uint32_t romaw_ppc_record_fpscr(uint32_t status, uint32_t raised);
extern int romaw_ppc_estimate_value(uint32_t opcode, uint64_t operand, uint32_t* fpscr, uint64_t* output);
extern ROMlib_ppc_environment* romaw_ppc_environment;
extern uint8* romaw_ppc_memory_checked(uint32 address, size_t length, bool write);

inline uint8* romaw_ppc_memory(uint32 address, size_t length, bool write)
{
    ROMlib_ppc_environment* env = romaw_ppc_environment;
    if (env && env->system)
        throw std::runtime_error("raw host memory span requested in PowerPC 750 mode");
    if (env && env->linear_memory) {
        uint32 relative = address - env->linear_address;
        if (relative <= env->linear_size && length <= env->linear_size - relative)
            return env->linear_memory + relative;
    }
    return romaw_ppc_memory_checked(address, length, write);
}

extern int64 TimebaseSpeed;
extern uint64 GetTicks_usec(void);
#endif
