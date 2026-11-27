/*+===================================================================
  File:      native040.h

  Summary:   Define invocation packets and entry points for closed native 68040 arithmetic, conversion and MMU code.

  Classes:   None.

  Functions: Interface declarations; see below.

  Origin:    Original ARDI Executor direct 68040 lowering; uses maintained MMU and floating arithmetic.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef SYN68K_NATIVE040_H
#define SYN68K_NATIVE040_H
#include "mmu040-memory.h"
#include "cache040.h"

typedef struct {
    uint16_t high;
    uint64_t low;
} Syn68kNative040Float;

typedef struct {
    Syn68kNative040Float source, target, result, condition, writeback;
    int8_t rounding, precision, flags, tininess;
    unsigned status, grs, sticky, exponent15, finite;
    uint64_t bits;
    unsigned unsupported, signaling, source_tag;
} Syn68kNative040FPU;

/* Optional permanent, nonwrapping ordinary physical RAM. Callback/device and
   alternate-function-code accesses remain checked at the physical boundary. */
typedef Syn68kMMU040RAM Syn68kNative040RAM;

typedef struct {
    Syn68kMMU040Memory* memory;
    uint32_t address, value;
    unsigned fc, size, writing, rmw, all, supervisor, global;
    enum Syn68kMMU040Control control;
    Syn68kMMU040Access access;
    Syn68kMMU040Result translation;
    int ok;
    Syn68kNative040RAM ram;
} Syn68kNative040MMU;

enum Syn68kNative040Operation {
    NATIVE040_FADD,
    NATIVE040_FSUB,
    NATIVE040_FMUL,
    NATIVE040_FDIV,
    NATIVE040_FSQRT,
    NATIVE040_FMOVE,
    NATIVE040_FABS,
    NATIVE040_FNEG,
    NATIVE040_FSGLDIV,
    NATIVE040_FSGLMUL,
    NATIVE040_FCMP,
    NATIVE040_FTST,
    NATIVE040_READ,
    NATIVE040_WRITE,
    NATIVE040_WRITE_RMW,
    NATIVE040_TRANSLATE,
    NATIVE040_PTEST,
    NATIVE040_FLUSH,
    NATIVE040_CONTROL,
    NATIVE040_RESET,
    NATIVE040_EA,
    NATIVE040_FROM_I32,
    NATIVE040_FROM_F32,
    NATIVE040_FROM_F64,
    NATIVE040_TO_I32,
    NATIVE040_TO_I16,
    NATIVE040_TO_I8,
    NATIVE040_TO_F32,
    NATIVE040_TO_F64,
    NATIVE040_BUS_READ,
    NATIVE040_BUS_WRITE,
    NATIVE040_BUS_WRITE_RMW,
    NATIVE040_MOVEC_READ,
    NATIVE040_MOVEC_WRITE,
    NATIVE040_LOAD_FPRS,
    NATIVE040_STORE_FPRS,
    NATIVE040_FLAT_READ,
    NATIVE040_FLAT_WRITE,
    NATIVE040_FLAT_SPAN,
    NATIVE040_SPAN,
    NATIVE040_STOP,
    NATIVE040_CACHE_ACCESS,
    NATIVE040_CACHE_MAINTAIN,
    NATIVE040_MEMORY_CACHE_MAINTAIN,
    NATIVE040_BUS_MOVE16,
    NATIVE040_CACHE_SNOOP,
    NATIVE040_BUS_SNOOP,
    NATIVE040_RTE,
    NATIVE040_RESTORE_WRITEBACK,
    NATIVE040_INTEGER_EA,
    NATIVE040_COUNT
};

/* Physical cache calls carry only live invocation pointers. Permanent RAM can
   bypass callbacks inside the closed kernel, including TM=0 victim pushes. */
typedef struct {
    Syn68kCache040* cache;
    const Syn68kCache040Bus* bus;
    Syn68kNative040RAM ram;
    uint32_t address, value, attributes, cacr;
    unsigned fc, size, writing, no_allocate;
    unsigned caches, scope, page_shift, push;
    Syn68kCache040Access access;
    enum Syn68kCache040Traffic traffic;
    unsigned char line[16];
    unsigned snoop_control, transfer_type;
    Syn68kCache040Snoop snoop;
} Syn68kNative040Cache;

typedef void (*Syn68kNative040Entry)(void*);
/* Lazily publish the build-time verified code image; NULL preserves fallback. */
Syn68kNative040Entry syn68k_native040_entry(enum Syn68kNative040Operation operation);
/* Install typed native bus entries in caller-owned storage. Failure preserves
   both objects; successful publication retains the storage through memory. */
int syn68k_native040_memory(Syn68kMMU040Memory* memory, Syn68kMMU040Native* storage, Syn68kNative040RAM ram);
#endif
