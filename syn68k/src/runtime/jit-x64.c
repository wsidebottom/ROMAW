/*+===================================================================
  File:      jit-x64.c

  Summary:   Implements jit x64 support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: syn68k_native_checkpoint, syn68k_native_unwind, syn68k_native_execution_count,
             syn68k_aot_hit_count, syn68k_native_compile_count, syn68k_x64_compile, syn68k_x64_release,
             syn68k_x64_execute_managed, syn68k_x64_flat_fpu, syn68k_x64_mapped_fpu, syn68k_x64_mapped_mmu, syn68k_x64_mapped_clear

  Origin:    Existing repository source in syn68k/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Win64 prefixes: RCX is CPUState*, RAX/RDX are volatile scratch. Control
   transfers may tail-call the checked decoder. Guest addresses stay 32 bits;
   native RAM accesses require bounds, alias and translated-page guards. */
#include "jit-x64.h"
#include "trap.h"
#include "native040.h"
#include "native040-flat.h"
#include "fpu-captured.h"
#include "native040-control.h"
#include "native040-ea.h"
#include "native040-registers.h"
#include <stddef.h>
#include <string.h>

#ifdef GENERATE_X64_CODE
extern __declspec(dllimport) void* __stdcall VirtualAlloc(void*, size_t, unsigned long, unsigned long);
extern __declspec(dllimport) int __stdcall VirtualProtect(void*, size_t, unsigned long, unsigned long*);
extern __declspec(dllimport) int __stdcall VirtualFree(void*, size_t, unsigned long);
extern __declspec(dllimport) void* __stdcall GetCurrentProcess(void);
extern __declspec(dllimport) int __stdcall FlushInstructionCache(void*, const void*, size_t);

int syn68k_x64_enabled;
/* Internal regression switch; normal launches use flag liveness. */
int syn68k_x64_optimize_flags = 1;
uint64 syn68k_x64_executions;
/* Only a terminal native instruction uses this private continuation. There
   are no guest callbacks between its address calculation and native exit. */
static uint32 indexed_next_pc;
#define DYNAMIC_PC UINT32_MAX

/* Stable symbol numbers are part of the persistent-code ABI. */
enum {
    RELOC_ADDRESS_MASK,
    RELOC_MEMORY_SIZE,
    RELOC_CODE_PAGES,
    RELOC_ALIAS_COUNT,
    RELOC_ALIAS_BASE,
    RELOC_ALIAS_SIZE,
    RELOC_MEMORY_OFFSET,
    RELOC_NEXT_PC,
    RELOC_TRAP,
    RELOC_INDEXED,
    RELOC_PAIR,
    RELOC_COMPLEX,
    RELOC_FPU_FRAME,
    RELOC_FPU_ROOTS,
    RELOC_NATIVE_FLAT,
    RELOC_COUNT
};

typedef struct {
    Syn68kNative040FPU arithmetic;
    uint32 command, operation, displacement;
    Syn68kNative040MMU memory;
    unsigned exit;
    Syn68kNative040Control control;
    Syn68kNative040EA ea;
    uint32 next, original_null, conversion;
    Syn68kFPUEntry entry;
    Syn68kNative040Registers registers;
    Syn68kFPUStep exception;
    unsigned frame_header;
    Syn68kNative040Restore restore;
    Syn68kNative040RTE rte;
} FPUInvocation;

enum {
    FPU_EXIT_UNFETCHED,
    FPU_EXIT_ACCESS,
    FPU_EXIT_COMMAND,
    FPU_EXIT_DONE,
    NATIVE_EXIT_PRIVILEGE,
    NATIVE_EXIT_ILLEGAL,
    FPU_EXIT_EA,
    FPU_EXIT_CAPTURED,
    FPU_EXIT_INCOMPLETE,
    FPU_EXIT_DESTINATION,
    FPU_EXIT_REGISTERS,
    FPU_EXIT_EXCEPTION,
    FPU_EXIT_FRAME,
    NATIVE_EXIT_RTE
};

/* Unmanaged register arithmetic cannot call guest code. Managed invocations
   preserve this packet across nested guest execution from memory callbacks. */
static FPUInvocation fpu_invocation;
static Syn68kNative040Entry fpu_roots[NATIVE040_COUNT];

typedef struct NativeInvocation {
    struct NativeInvocation* previous;
    void* code;
    unsigned retired;
    FPUInvocation saved_fpu;
    uint32 saved_next_pc;
} NativeInvocation;

void* syn68k_x64_managed_context;

static void leave_native(NativeInvocation* invocation)
{
    syn68k_x64_managed_context = invocation->previous;
    if (invocation->previous) {
        fpu_invocation = invocation->saved_fpu;
        indexed_next_pc = invocation->saved_next_pc;
    }
    if (invocation->retired)
        VirtualFree((unsigned char*)invocation->code - sizeof(Syn68kX64CodeHeader), 0, 0x8000);
}

static uint32 execute_managed_packet(void* code, FPUInvocation* packet)
{
    NativeInvocation invocation = {.previous = syn68k_x64_managed_context, .code = code};
    if (invocation.previous) {
        invocation.saved_fpu = fpu_invocation;
        invocation.saved_next_pc = indexed_next_pc;
    }
    syn68k_x64_managed_context = &invocation;
    if (packet)
        fpu_invocation = *packet;
    uint32 result = ((uint32(*)(CPUState*))code)(&cpu_state);
    if (packet)
        *packet = fpu_invocation;
    leave_native(&invocation);
    return result;
}

uint32 syn68k_x64_execute_managed(void* code)
{
    return execute_managed_packet(code, NULL);
}

static uint32 flat_native_run(uint32 pc, unsigned opcode);

static uintptr_t relocation_address(unsigned symbol)
{
    if (symbol == RELOC_FPU_ROOTS)
        for (unsigned i = 0; i < sizeof fpu_roots / sizeof *fpu_roots; ++i)
            if (!fpu_roots[i])
                fpu_roots[i] = syn68k_native040_entry(i);
    const uintptr_t addresses[] = {(uintptr_t)&syn68k_address_mask,
                                   (uintptr_t)&syn68k_direct_memory_size,
                                   (uintptr_t)syn68k_x64_code_pages,
                                   (uintptr_t)&syn68k_host_alias_count,
                                   (uintptr_t)&syn68k_host_alias_base,
                                   (uintptr_t)&syn68k_host_alias_size,
                                   (uintptr_t)&ROMlib_offset,
                                   (uintptr_t)&indexed_next_pc,
                                   (uintptr_t)trap_direct,
                                   (uintptr_t)syn68k_x64_indexed_address,
                                   (uintptr_t)syn68k_x64_prepare_pair,
                                   (uintptr_t)syn68k_x64_complex_run,
                                   (uintptr_t)&fpu_invocation,
                                   (uintptr_t)fpu_roots,
                                   (uintptr_t)flat_native_run};
    return symbol < RELOC_COUNT ? addresses[symbol] : 0;
}

typedef struct {
    uint32 offset, symbol;
} relocation;

typedef struct {
    unsigned char bytes[8192];
    /* The byte buffer bounds the number of eight-byte pointer operands. Keep
       arrays before size so instruction rollback only copies scalar state. */
    relocation relocations[8192 / sizeof(uintptr_t)];
    unsigned size;
    unsigned relocation_count;
    int cached_address_register;
    unsigned cached_size;
    int dynamic;
    unsigned managed;
    unsigned cc_mask;
    unsigned guards;
    int variable_ea;
    unsigned decoder_selector;
} emitter;

static void byte(emitter* out, unsigned value)
{
    out->bytes[out->size++] = value;
}

static void word32(emitter* out, uint32 value)
{
    memcpy(out->bytes + out->size, &value, 4);
    out->size += 4;
}

static unsigned reg_offset(unsigned reg)
{
    return offsetof(CPUState, regs) + reg * sizeof(M68kReg);
}

static void load(emitter* out, unsigned reg, int rhs)
{
    byte(out, 0x8B);
    byte(out, rhs ? 0x91 : 0x81); /* MOV EDX/EAX,[RCX+disp32] */
    word32(out, reg_offset(reg));
}

static void store(emitter* out, unsigned reg)
{
    byte(out, 0x89);
    byte(out, 0x81);
    word32(out, reg_offset(reg));
}

/* Narrow data-register writes retain the upper guest register bits. MOV
   preserves the arithmetic flags needed by the following SETcc sequence. */
static void store_sized(emitter* out, unsigned reg, unsigned size)
{
    if (size == 1)
        byte(out, 0x66);
    byte(out, size == 0 ? 0x88 : 0x89);
    byte(out, 0x81);
    word32(out, reg_offset(reg));
}

static void flag(emitter* out, unsigned condition, unsigned offset)
{
    unsigned mask = offset == offsetof(CPUState, ccc)    ? M68K_CCC
                    : offset == offsetof(CPUState, ccv)  ? M68K_CCV
                    : offset == offsetof(CPUState, ccnz) ? M68K_CCZ
                    : offset == offsetof(CPUState, ccn)  ? M68K_CCN
                                                         : M68K_CCX;
    if (!(out->cc_mask & mask))
        return;
    byte(out, 0x0F);
    byte(out, condition);
    byte(out, 0xC2); /* SETcc DL */
    byte(out, 0x0F);
    byte(out, 0xB6);
    byte(out, 0xD2); /* MOVZX EDX,DL; neither MOV alters flags. */
    byte(out, 0x89);
    byte(out, 0x91);
    word32(out, offset);
}

static void flags(emitter* out, int extend)
{
    flag(out, 0x95, offsetof(CPUState, ccnz));
    flag(out, 0x98, offsetof(CPUState, ccn));
    flag(out, 0x92, offsetof(CPUState, ccc));
    flag(out, 0x90, offsetof(CPUState, ccv));
    if (extend)
        flag(out, 0x92, offsetof(CPUState, ccx));
}

static void test(emitter* out, unsigned size)
{
    if (size == 1)
        byte(out, 0x66);
    byte(out, size == 0 ? 0x84 : 0x85);
    byte(out, 0xC0);
}

static unsigned jump(emitter* out, unsigned cc)
{
    byte(out, 0x0F);
    byte(out, cc);
    unsigned location = out->size;
    word32(out, 0);
    return location;
}

static void patch(emitter* out, unsigned location, unsigned target)
{
    int32 distance = (int32)target - (int32)(location + 4);
    memcpy(out->bytes + location, &distance, sizeof distance);
}

static void condition(emitter* out, unsigned cc);

static void extend_carry(emitter* out)
{
    /* CMP against zero clears CF for every raw CCR representation. */
    byte(out, 0x83);
    byte(out, 0xB9);
    word32(out, offsetof(CPUState, ccx));
    byte(out, 0);
    byte(out, 0x74);
    byte(out, 1);
    byte(out, 0xF9); /* STC only when X is nonzero. */
}

static void shift(emitter* out, unsigned op, unsigned size)
{
    unsigned kind = (op >> 3) & 3, left = !!(op & 0x100);
    if (op & 0x20) {
        byte(out, 0x44);
        byte(out, 0x8B);
        byte(out, 0x81);
        word32(out, reg_offset((op >> 9) & 7));
        byte(out, 0x41);
        byte(out, 0x83);
        byte(out, 0xE0);
        byte(out, 63); /* R8D = captured count, including source/dest alias. */
    }
    else {
        unsigned count = (op >> 9) & 7;
        byte(out, 0x41);
        byte(out, 0xB8);
        word32(out, count ? count : 8);
    }
    load(out, op & 7, 0);
    byte(out, 0x45);
    byte(out, 0x31);
    byte(out, 0xC9); /* R9D accumulates ASL overflow across all steps. */
    if (kind == 2) {
        byte(out, 0x44);
        byte(out, 0x8B);
        byte(out, 0x91);
        word32(out, offsetof(CPUState, ccx));
        byte(out, 0x45);
        byte(out, 0x85);
        byte(out, 0xD2);
        byte(out, 0x41);
        byte(out, 0x0F);
        byte(out, 0x95);
        byte(out, 0xC2);
        byte(out, 0x45);
        byte(out, 0x0F);
        byte(out, 0xB6);
        byte(out, 0xD2); /* R10D = normalized X. */
        byte(out, 0x44);
        byte(out, 0x89);
        byte(out, 0xD2); /* Zero-count ROX copies X to C. */
    }
    else {
        byte(out, 0x31);
        byte(out, 0xD2);
    }
    byte(out, 0x45);
    byte(out, 0x85);
    byte(out, 0xC0);
    unsigned zero = jump(out, 0x84);
    unsigned loop = out->size;
    if (kind == 2) {
        byte(out, 0x41);
        byte(out, 0x0F);
        byte(out, 0xBA);
        byte(out, 0xE2);
        byte(out, 0); /* BT R10D,0 supplies the rotate's input carry. */
    }
    if (size == 1)
        byte(out, 0x66);
    byte(out, size == 0 ? 0xD0 : 0xD1);
    byte(out, kind == 0   ? (left ? 0xE0 : 0xF8)
              : kind == 1 ? (left ? 0xE0 : 0xE8)
              : kind == 2 ? (left ? 0xD0 : 0xD8)
                          : (left ? 0xC0 : 0xC8));
    byte(out, 0x0F);
    byte(out, 0x92);
    byte(out, 0xC2);
    byte(out, 0x0F);
    byte(out, 0xB6);
    byte(out, 0xD2); /* EDX = shifted-out bit. */
    if (kind == 0 && left) {
        byte(out, 0x41);
        byte(out, 0x0F);
        byte(out, 0x90);
        byte(out, 0xC3);
        byte(out, 0x45);
        byte(out, 0x0F);
        byte(out, 0xB6);
        byte(out, 0xDB);
        byte(out, 0x45);
        byte(out, 0x09);
        byte(out, 0xD9); /* OR R9D,R11D; preserve any intermediate sign change. */
    }
    if (kind == 2) {
        byte(out, 0x41);
        byte(out, 0x89);
        byte(out, 0xD2);
    }
    byte(out, 0x41);
    byte(out, 0xFF);
    byte(out, 0xC8);
    patch(out, jump(out, 0x85), loop);
    if (kind != 3) {
        byte(out, 0x89);
        byte(out, 0x91);
        word32(out, offsetof(CPUState, ccx));
    }
    patch(out, zero, out->size); /* Count zero must leave X untouched. */
    store_sized(out, op & 7, size);
    byte(out, 0x89);
    byte(out, 0x91);
    word32(out, offsetof(CPUState, ccc));
    byte(out, 0x44);
    byte(out, 0x89);
    byte(out, 0x89);
    word32(out, offsetof(CPUState, ccv));
    test(out, size);
    flag(out, 0x95, offsetof(CPUState, ccnz));
    flag(out, 0x98, offsetof(CPUState, ccn));
}

static void read_ccr(emitter* out)
{
    static const unsigned offsets[] = {offsetof(CPUState, ccc), offsetof(CPUState, ccv), offsetof(CPUState, ccnz),
                                       offsetof(CPUState, ccn), offsetof(CPUState, ccx)};
    byte(out, 0x31);
    byte(out, 0xC0); /* XOR EAX,EAX */
    for (unsigned bit = 0; bit < 5; ++bit) {
        byte(out, 0x83);
        byte(out, 0xB9);
        word32(out, offsets[bit]);
        byte(out, 0);
        byte(out, 0x0F);
        byte(out, bit == 2 ? 0x94 : 0x95);
        byte(out, 0xC2);
        byte(out, 0x0F);
        byte(out, 0xB6);
        byte(out, 0xD2);
        if (bit) {
            byte(out, 0xC1);
            byte(out, 0xE2);
            byte(out, bit);
        }
        byte(out, 0x09);
        byte(out, 0xD0); /* OR EAX,EDX */
    }
}

static void write_ccr(emitter* out)
{
    static const unsigned offsets[] = {offsetof(CPUState, ccc), offsetof(CPUState, ccv), offsetof(CPUState, ccnz),
                                       offsetof(CPUState, ccn), offsetof(CPUState, ccx)};
    for (unsigned bit = 0; bit < 5; ++bit) {
        byte(out, 0x0F);
        byte(out, 0xBA);
        byte(out, 0xE0); /* BT EAX,bit */
        byte(out, bit);
        flag(out, bit == 2 ? 0x93 : 0x92, offsets[bit]);
    }
}

static void pointer_value(emitter* out, unsigned symbol)
{
    relocation* entry = &out->relocations[out->relocation_count++];
    entry->offset = out->size;
    entry->symbol = symbol;
    uintptr_t value = relocation_address(symbol);
    memcpy(out->bytes + out->size, &value, sizeof value);
    out->size += sizeof value;
}

static void pointer_r8(emitter* out, unsigned symbol)
{
    byte(out, 0x49);
    byte(out, 0xB8);
    pointer_value(out, symbol);
}

static void instruction_end(emitter* out, uint32 pc, unsigned length)
{
    if (length == DYNAMIC_PC) {
        byte(out, 0x48);
        byte(out, 0xB8);
        pointer_value(out, RELOC_NEXT_PC);
        byte(out, 0x8B);
        byte(out, 0x00);
    }
    else {
        byte(out, 0xB8);
        word32(out, pc + length);
    }
}

static void exception_tail(emitter* out, uint32 pc, uint32 address, unsigned selector)
{
    ++out->guards;
    byte(out, 0xB9);
    word32(out, selector);
    byte(out, 0xBA);
    word32(out, pc);
    byte(out, 0x41);
    byte(out, 0xB8);
    word32(out, address);
    byte(out, 0x48);
    byte(out, 0xB8);
    pointer_value(out, RELOC_TRAP);
    byte(out, 0xFF);
    byte(out, 0xE0);
}

static void decoder_tail(emitter* out, uint32 pc, unsigned selector)
{
    exception_tail(out, pc, 0, selector == 4 && out->decoder_selector ? out->decoder_selector : selector);
}

/* Bookkeeping tail only: the inner native body consumes command/EA/data. */
static void flat_native_tail(emitter* out, uint32 pc, unsigned opcode)
{
    ++out->guards;
    out->managed = out->dynamic = 1;
    byte(out, 0xb9);
    word32(out, pc);
    byte(out, 0xba);
    word32(out, opcode);
    byte(out, 0x48);
    byte(out, 0xb8);
    pointer_value(out, RELOC_NATIVE_FLAT);
    byte(out, 0xff);
    byte(out, 0xe0);
}

/* This frame exists only across a non-trapping native EA helper. Restore it
   before any checked decoder tail, which can invoke callbacks or free code. */
static void indexed_address(emitter* out, unsigned code, uint32 pc, uint32 extension_pc)
{
    byte(out, 0x48);
    byte(out, 0x83);
    byte(out, 0xEC);
    byte(out, 56); /* Aligned call, 32-byte shadow space, result and saved inputs. */
    byte(out, 0x48);
    byte(out, 0x89);
    byte(out, 0x4C);
    byte(out, 0x24);
    byte(out, 40);
    byte(out, 0x89);
    byte(out, 0x44);
    byte(out, 0x24);
    byte(out, 48);
    byte(out, 0xB9);
    word32(out, code);
    byte(out, 0xBA);
    word32(out, extension_pc);
    byte(out, 0x4C);
    byte(out, 0x8D);
    byte(out, 0x44);
    byte(out, 0x24);
    byte(out, 32);
    byte(out, 0x48);
    byte(out, 0xB8);
    pointer_value(out, RELOC_INDEXED);
    byte(out, 0xFF);
    byte(out, 0xD0);
    byte(out, 0x85);
    byte(out, 0xC0);
    byte(out, 0x48);
    byte(out, 0x8B);
    byte(out, 0x4C);
    byte(out, 0x24);
    byte(out, 40);
    byte(out, 0x8B);
    byte(out, 0x44);
    byte(out, 0x24);
    byte(out, 48);
    unsigned valid = jump(out, 0x85);
    byte(out, 0x48);
    byte(out, 0x83);
    byte(out, 0xC4);
    byte(out, 56);
    decoder_tail(out, pc, 4);
    patch(out, valid, out->size);
    byte(out, 0x8B);
    byte(out, 0x54);
    byte(out, 0x24);
    byte(out, 32);
    byte(out, 0x44);
    byte(out, 0x8B);
    byte(out, 0x4C);
    byte(out, 0x24);
    byte(out, 36);
    byte(out, 0x48);
    byte(out, 0x83);
    byte(out, 0xC4);
    byte(out, 56);
    pointer_r8(out, RELOC_NEXT_PC);
    byte(out, 0x45);
    byte(out, 0x89);
    byte(out, 0x08);
    out->cached_address_register = -1;
}

static void memory_load(emitter* out, unsigned width, int rhs);

/* EDX holds the live brief extension. Leave the scaled index plus signed
   displacement in EDX, preserving EAX for memory Scc and other callers. */
static void brief_index(emitter* out, uint32 pc)
{
    byte(out, 0xF7);
    byte(out, 0xC2);
    word32(out, 0x100);
    unsigned brief = jump(out, 0x84);
    /* A changed extension can select a different full-format length.
     Tail-call before any data access or register side effect. */
    decoder_tail(out, pc, 4);
    patch(out, brief, out->size);
    byte(out, 0x41);
    byte(out, 0x89);
    byte(out, 0xD3); /* R11D = extension. */
    byte(out, 0x44);
    byte(out, 0x0F);
    byte(out, 0xBE);
    byte(out, 0xD2); /* R10D = signed byte displacement. */
    byte(out, 0x41);
    byte(out, 0x89);
    byte(out, 0xD1);
    byte(out, 0x41);
    byte(out, 0xC1);
    byte(out, 0xE9);
    byte(out, 12);
    byte(out, 0x41);
    byte(out, 0x83);
    byte(out, 0xE1);
    byte(out, 15);
    byte(out, 0x42);
    byte(out, 0x8B);
    byte(out, 0x94);
    byte(out, 0x89); /* MOV EDX,[RCX+R9*4+regs] */
    word32(out, reg_offset(0));
    byte(out, 0x41);
    byte(out, 0xF7);
    byte(out, 0xC3);
    word32(out, 0x800);
    unsigned long_index = jump(out, 0x85);
    byte(out, 0x0F);
    byte(out, 0xBF);
    byte(out, 0xD2);
    patch(out, long_index, out->size);
    for (unsigned bit = 0; bit < 2; ++bit) {
        byte(out, 0x41);
        byte(out, 0xF7);
        byte(out, 0xC3);
        word32(out, 0x200U << bit);
        unsigned unscaled = jump(out, 0x84);
        byte(out, 0xC1);
        byte(out, 0xE2);
        byte(out, 1U << bit);
        patch(out, unscaled, out->size);
    }
    byte(out, 0x44);
    byte(out, 0x01);
    byte(out, 0xD2); /* ADD EDX,R10D */
}

/* EDX supplies the guest address. Guards preserve EAX/R10/R11 and
   publish R8 only after validating the complete flat span. */
static void memory_guard(emitter* out, unsigned size, uint32 pc, int instruction_fetch)
{
    unsigned failures[8], count = 0;
    pointer_r8(out, RELOC_ADDRESS_MASK);
    byte(out, 0x41);
    byte(out, 0x23);
    byte(out, 0x10); /* AND EDX,[R8] */
    byte(out, 0x41);
    byte(out, 0x89);
    byte(out, 0xD1); /* MOV R9D,EDX */
    byte(out, 0x41);
    byte(out, 0x83);
    byte(out, 0xC1);
    byte(out, size);
    failures[count++] = jump(out, 0x82); /* Address overflow. */
    pointer_r8(out, RELOC_MEMORY_SIZE);
    byte(out, 0x45);
    byte(out, 0x3B);
    byte(out, 0x08); /* CMP R9D,[R8] */
    failures[count++] = jump(out, 0x87);
    byte(out, 0x41);
    byte(out, 0x89);
    byte(out, 0xD1);
    byte(out, 0x41);
    byte(out, 0x81);
    byte(out, 0xE1);
    word32(out, 4095);
    byte(out, 0x41);
    byte(out, 0x81);
    byte(out, 0xF9);
    word32(out, 4096 - size);
    failures[count++] = jump(out, 0x87); /* Cross-page transfers use EA. */
    if (!instruction_fetch) {
        byte(out, 0x41);
        byte(out, 0x89);
        byte(out, 0xD1);
        byte(out, 0x41);
        byte(out, 0xC1);
        byte(out, 0xE9);
        byte(out, SYN68K_X64_PAGE_SHIFT);
        pointer_r8(out, RELOC_CODE_PAGES);
        byte(out, 0x43);
        byte(out, 0x83);
        byte(out, 0x3C);
        byte(out, 0x88);
        byte(out, 0); /* CMP DWORD [R8+R9*4],0 */
        failures[count++] = jump(out, 0x85);
    }
    pointer_r8(out, RELOC_ALIAS_COUNT);
    byte(out, 0x41);
    byte(out, 0x83);
    byte(out, 0x38);
    byte(out, 0);
    unsigned no_aliases = jump(out, 0x84);
    for (unsigned end = 0; end < 2; ++end) {
        byte(out, 0x41);
        byte(out, 0x89);
        byte(out, 0xD1);
        if (end) {
            byte(out, 0x41);
            byte(out, 0x83);
            byte(out, 0xC1);
            byte(out, size - 1);
        }
        pointer_r8(out, RELOC_ALIAS_BASE);
        byte(out, 0x45);
        byte(out, 0x2B);
        byte(out, 0x08);
        pointer_r8(out, RELOC_ALIAS_SIZE);
        byte(out, 0x45);
        byte(out, 0x3B);
        byte(out, 0x08);
        failures[count++] = jump(out, 0x82);
    }
    /* Multi-register spans can enclose an entire alias. Endpoint checks alone
     cannot reject that overlap. Subtraction also handles disjoint low spans. */
    pointer_r8(out, RELOC_ALIAS_BASE);
    byte(out, 0x45);
    byte(out, 0x8B);
    byte(out, 0x08);
    byte(out, 0x41);
    byte(out, 0x29);
    byte(out, 0xD1);
    byte(out, 0x41);
    byte(out, 0x83);
    byte(out, 0xF9);
    byte(out, size);
    failures[count++] = jump(out, 0x82);
    patch(out, no_aliases, out->size);
    pointer_r8(out, RELOC_MEMORY_OFFSET);
    byte(out, 0x4D);
    byte(out, 0x8B);
    byte(out, 0x00);
    byte(out, 0x41);
    byte(out, 0x89);
    byte(out, 0xD1);
    byte(out, 0x4D);
    byte(out, 0x01);
    byte(out, 0xC8); /* R8 = flat RAM base + masked guest address. */
    byte(out, 0xE9);
    unsigned valid = out->size;
    word32(out, 0);
    for (unsigned i = 0; i < count; ++i)
        patch(out, failures[i], out->size);
    decoder_tail(out, pc, 4);
    patch(out, valid, out->size);
    out->dynamic = 1;
}

static unsigned address_value(emitter* out, unsigned code, uint32 pc, unsigned extension_offset);

static void memory_pointer(emitter* out, unsigned code, unsigned size, uint32 pc)
{
    unsigned reg = code & 7, mode = code >> 3;
    unsigned step = size == 1 && reg == 7 ? 2 : size;
    if (mode != 2 || out->cached_address_register != (int)reg || out->cached_size < size) {
        if (mode == 5 || mode == 6 || mode == 7) {
            if (!address_value(out, code, pc, 2)) {
                decoder_tail(out, pc, 4);
                return;
            }
        }
        else if (mode == 8) {
            byte(out, 0xBA);
            word32(out, pc + 2); /* Live control-transfer extension. */
        }
        else
            load(out, reg + 8, 1);
        if (mode == 4) {
            byte(out, 0x83);
            byte(out, 0xEA); /* SUB EDX,step, without committing An yet. */
            byte(out, step);
        }
        memory_guard(out, size, pc, mode == 8);
        out->cached_address_register = mode == 2 ? (int)reg : -1;
        out->cached_size = size;
    }
    if (mode == 3 || mode == 4) {
        /* Commit once the guards succeed. A failed guard tail-calls the decoder,
         which performs the architectural update and fault ordering itself. */
        byte(out, 0x83);
        byte(out, mode == 3 ? 0x81 : 0xA9);
        word32(out, reg_offset(reg + 8));
        byte(out, step);
    }
    out->dynamic = 1;
}

static void memory_load(emitter* out, unsigned width, int rhs)
{
    byte(out, 0x41);
    if (width < 2) {
        byte(out, 0x0F);
        byte(out, width == 0 ? 0xB6 : 0xB7);
    }
    else
        byte(out, 0x8B);
    byte(out, rhs ? 0x10 : 0x00);
    if (width == 1) {
        byte(out, 0x66);
        byte(out, 0xC1);
        byte(out, rhs ? 0xC2 : 0xC0);
        byte(out, 8);
    }
    else if (width == 2) {
        byte(out, 0x0F);
        byte(out, rhs ? 0xCA : 0xC8);
    }
}

static void memory_store(emitter* out, unsigned width)
{
    if (width == 1) {
        byte(out, 0x66);
        byte(out, 0xC1);
        byte(out, 0xC0);
        byte(out, 8);
    }
    else if (width == 2) {
        byte(out, 0x0F);
        byte(out, 0xC8);
    }
    if (width == 1)
        byte(out, 0x66);
    byte(out, 0x41);
    byte(out, width == 0 ? 0x88 : 0x89);
    byte(out, 0x00);
}

static void memory_integer(emitter* out, unsigned op, uint32 pc)
{
    unsigned family = op >> 12, code = op & 63, reg = (op >> 9) & 7;
    unsigned width = (op >> 6) & 3;
    if (width == 3 && (family == 9 || family == 11 || family == 13)) {
        width = op & 256 ? 2 : 1;
        memory_pointer(out, code, 1U << width, pc);
        memory_load(out, width, 1);
        if (width == 1) {
            byte(out, 0x0F);
            byte(out, 0xBF);
            byte(out, 0xD2);
        }
        /* Source EA updates precede reading the destination An, including when
         postincrement/predecrement uses that same address register. */
        load(out, reg + 8, 0);
        byte(out, family == 13 ? 0x01 : family == 9 ? 0x29 : 0x39);
        byte(out, 0xD0);
        if (family == 11)
            flags(out, 0);
        else
            store(out, reg + 8);
        out->cached_address_register = -1;
        return;
    }
    if (family >= 1 && family <= 3) {
        width = family == 1 ? 0 : family == 3 ? 1 : 2;
        unsigned dest = ((op >> 3) & 56) | reg;
        int read = code >= 16;
        memory_pointer(out, read ? code : dest, 1U << width, pc);
        if (read)
            memory_load(out, width, 0);
        else
            load(out, code, 0);
        if (read && dest >= 8) {
            if (width == 1) {
                byte(out, 0x0F);
                byte(out, 0xBF);
                byte(out, 0xC0);
            }
            store(out, dest);
            out->cached_address_register = -1;
            return;
        }
        test(out, width);
        flags(out, 0);
        if (read)
            store_sized(out, dest, width);
        else
            memory_store(out, width);
        return;
    }
    if ((op & 0xF0C0) == 0x50C0) {
        condition(out, (op >> 8) & 15);
        byte(out, 0xF6);
        byte(out, 0xD8);
        out->cached_address_register = -1;
        memory_pointer(out, code, 1, pc);
        memory_store(out, 0);
        return;
    }
    if ((op & 0xF100) == 0x0100) {
        memory_pointer(out, code, 1, pc);
        memory_load(out, 0, 0);
        load(out, reg, 1);
        byte(out, 0x83);
        byte(out, 0xE2);
        byte(out, 7);
        unsigned kind = (op >> 6) & 3;
        byte(out, 0x0F);
        byte(out, kind == 0 ? 0xA3 : kind == 1 ? 0xBB : kind == 2 ? 0xB3 : 0xAB);
        byte(out, 0xD0);
        flag(out, 0x92, offsetof(CPUState, ccnz));
        if (kind)
            memory_store(out, 0);
        return;
    }
    if (family == 4) {
        unsigned unary = op & 0xFF00;
        if ((op & 0xFFC0) == 0x4AC0)
            width = 0;
        memory_pointer(out, code, 1U << width, pc);
        if (unary == 0x4200) {
            byte(out, 0x31);
            byte(out, 0xC0);
        }
        else
            memory_load(out, width, 0);
        if (unary == 0x4400 || unary == 0x4600) {
            if (width == 1)
                byte(out, 0x66);
            byte(out, width == 0 ? 0xF6 : 0xF7);
            byte(out, unary == 0x4400 ? 0xD8 : 0xD0);
        }
        if (unary != 0x4400)
            test(out, width);
        flags(out, unary == 0x4400);
        if ((op & 0xFFC0) == 0x4AC0) {
            byte(out, 0x0C);
            byte(out, 128);
        }
        if (unary != 0x4A00 || (op & 0xFFC0) == 0x4AC0)
            memory_store(out, width);
        return;
    }
    memory_pointer(out, code, 1U << width, pc);
    if (family != 5) {
        int reverse = !!(op & 256), compare = family == 11 && !reverse;
        memory_load(out, width, !reverse);
        load(out, reg, reverse);
        unsigned alu = family == 8    ? 0x08
                       : family == 9  ? 0x28
                       : family == 12 ? 0x20
                       : family == 13 ? 0
                       : compare      ? 0x38
                                      : 0x30;
        if (width == 1)
            byte(out, 0x66);
        byte(out, alu + (width != 0));
        byte(out, 0xD0);
        flags(out, !compare && (family == 9 || family == 13));
        if (!compare) {
            if (reverse)
                memory_store(out, width);
            else
                store_sized(out, reg, width);
        }
        return;
    }
    memory_load(out, width, 0);
    if (width == 1)
        byte(out, 0x66);
    byte(out, width == 0 ? 0x80 : 0x83);
    byte(out, op & 256 ? 0xE8 : 0xC0);
    unsigned quick = (op >> 9) & 7;
    byte(out, quick ? quick : 8);
    flags(out, 1);
    memory_store(out, width);
}

static int instruction(emitter* out, const uint16* source, unsigned length, uint32 pc)
{
    unsigned op = SWAPUW_IFLE(source[0]);
    unsigned src = op & 7, dst = (op >> 9) & 7;
    unsigned size = (op >> 6) & 3;
    unsigned alu = 0, write_result = 1, extend = 0;
    if (length == (unsigned)syn68k_x64_fast_memory_opcode(op, pc)) {
        memory_integer(out, op, pc);
        return 1;
    }
    out->cached_address_register = -1;
    if (op == 0x4E71 && length == 1)
        return 1;
    if ((op & 0xF1F8) == 0x41D0 && length == 1) {
        load(out, src + 8, 0);
        store(out, dst + 8);
        return 1;
    }
    if ((op & 0xFFF8) == 0x4AC0 && length == 1) {
        load(out, src, 0);
        test(out, 0);
        flags(out, 0);
        byte(out, 0x0C);
        byte(out, 128); /* OR AL,128, after recording the original byte flags. */
        store_sized(out, src, 0);
        return 1;
    }
    if ((op & 0xFFF8) == 0x42C0 && length == 1) {
        read_ccr(out);
        store_sized(out, src, 1);
        return 1;
    }
    if ((op & 0xFFF8) == 0x44C0 && length == 1) {
        load(out, src, 0);
        write_ccr(out);
        return 1;
    }
    if ((op == 0x003C || op == 0x023C || op == 0x0A3C) && length == 2) {
        read_ccr(out);
        byte(out, op == 0x003C ? 0x0D : op == 0x023C ? 0x25 : 0x35);
        word32(out, SWAPUW_IFLE(source[1]) & 31);
        write_ccr(out);
        return 1;
    }
    if ((op & 0xF000) == 0xE000 && size < 3 && length == 1) {
        shift(out, op, size);
        return 1;
    }
    if ((op & 0xFCF8) == 0xE4C0 && length == 1) {
        unsigned kind = (op >> 9) & 3, left = !!(op & 0x100);
        load(out, src, 0);
        if (kind == 2)
            extend_carry(out);
        byte(out, 0x66);
        byte(out, 0xD1);
        byte(out, kind == 2 ? (left ? 0xD0 : 0xD8) : (left ? 0xC0 : 0xC8));
        flag(out, 0x92, offsetof(CPUState, ccc));
        if (kind == 2)
            flag(out, 0x92, offsetof(CPUState, ccx));
        byte(out, 0xC7);
        byte(out, 0x81);
        word32(out, offsetof(CPUState, ccv));
        word32(out, 0);
        store_sized(out, src, 1);
        test(out, 1);
        flag(out, 0x95, offsetof(CPUState, ccnz));
        flag(out, 0x98, offsetof(CPUState, ccn));
        return 1;
    }
    if ((op & 0xF1F8) == 0xB008 && length == 1) {
        load(out, dst, 0);
        load(out, src + 8, 1);
        byte(out, 0x38);
        byte(out, 0xD0);
        flags(out, 0);
        return 1;
    }
    if ((op & 0xF0F8) == 0x50C0 && length == 1) {
        condition(out, (op >> 8) & 15);
        byte(out, 0xF6);
        byte(out, 0xD8); /* NEG AL: false=0, true=255; guest CCR is unchanged. */
        store_sized(out, src, 0);
        return 1;
    }
    if (((op & 0xF138) == 0x0100 && length == 1) || ((op & 0xFF38) == 0x0800 && length == 2)) {
        if (length == 1)
            load(out, dst, 1);
        else {
            byte(out, 0xBA);
            word32(out, SWAPUW_IFLE(source[1]));
        }
        byte(out, 0x83);
        byte(out, 0xE2);
        byte(out, 31);
        load(out, src, 0);
        unsigned kind = (op >> 6) & 3;
        byte(out, 0x0F);
        byte(out, kind == 0 ? 0xA3 : kind == 1 ? 0xBB : kind == 2 ? 0xB3 : 0xAB);
        byte(out, 0xD0);
        if (kind)
            store(out, src);
        flag(out, 0x92, offsetof(CPUState, ccnz));
        return 1;
    }
    if (size < 3 && length == 1 && ((op & 0xF138) == 0xD100 || (op & 0xF138) == 0x9100 || (op & 0xFF38) == 0x4000)) {
        int negate = (op & 0xFF38) == 0x4000;
        load(out, src, 1);
        if (negate) {
            dst = src;
            byte(out, 0x31);
            byte(out, 0xC0);
        }
        else
            load(out, dst, 0);
        byte(out, 0x44);
        byte(out, 0x8B);
        byte(out, 0x81);
        word32(out, offsetof(CPUState, ccnz)); /* R8D preserves sticky Z. */
        extend_carry(out);
        if (size == 1)
            byte(out, 0x66);
        unsigned alu = negate || (op & 0xF000) == 0x9000 ? 0x19 : 0x11;
        byte(out, size == 0 ? alu - 1 : alu);
        byte(out, 0xD0);
        store_sized(out, dst, size);
        flags(out, 1);
        byte(out, 0x44);
        byte(out, 0x09);
        byte(out, 0x81);
        word32(out, offsetof(CPUState, ccnz));
        return 1;
    }
    if ((op & 0xF100) == 0x7000 && length == 1) {
        byte(out, 0xB8);
        word32(out, (int32)(int8)op);
        store(out, dst);
        byte(out, 0x85);
        byte(out, 0xC0); /* TEST EAX,EAX */
        flags(out, 0);
        return 1;
    }
    unsigned move = op >> 12;
    if (move >= 1 && move <= 3 && ((op & 0x1C0) == 0 || (op & 0x1C0) == 0x40)) {
        unsigned width = move == 1 ? 0 : move == 3 ? 1 : 2;
        int address = !!(op & 0x40);
        if (width == 0 && (address || (op & 0x38) == 8))
            return 0;
        if ((op & 0x30) == 0 && length == 1)
            load(out, op & 15, 0);
        else if ((op & 0x3F) == 0x3C && length == (width == 2 ? 3 : 2)) {
            byte(out, 0xB8);
            word32(out, width == 2 ? ((uint32)SWAPUW_IFLE(source[1]) << 16) | SWAPUW_IFLE(source[2])
                                   : SWAPUW_IFLE(source[1]));
        }
        else
            return 0;
        if (address) {
            if (width == 1) {
                byte(out, 0x0F);
                byte(out, 0xBF);
                byte(out, 0xC0); /* MOVSX EAX,AX */
            }
            store(out, dst + 8);
        }
        else {
            store_sized(out, dst, width);
            test(out, width);
            flags(out, 0);
        }
        return 1;
    }
    if (length == 1 &&
        ((op & 0xFFF8) == 0x4840 || (op & 0xFFF8) == 0x4880 || (op & 0xFFF8) == 0x48C0 || (op & 0xFFF8) == 0x49C0)) {
        load(out, src, 0);
        unsigned width = (op & 0xFFF8) == 0x4880 ? 1 : 2;
        if ((op & 0xFFF8) == 0x4840) {
            byte(out, 0xC1);
            byte(out, 0xC8);
            byte(out, 16); /* ROR EAX,16 */
        }
        else {
            if (width == 1)
                byte(out, 0x66);
            byte(out, 0x0F);
            byte(out, (op & 0xFFF8) == 0x48C0 ? 0xBF : 0xBE);
            byte(out, 0xC0); /* MOVSX AX,AL / EAX,AX / EAX,AL */
        }
        store_sized(out, src, width);
        test(out, width);
        flags(out, 0);
        return 1;
    }
    if (length == 1 && ((op & 0xF1F8) == 0xC140 || (op & 0xF1F8) == 0xC148 || (op & 0xF1F8) == 0xC188)) {
        unsigned kind = op & 0xF1F8;
        if (kind != 0xC140)
            src += 8;
        if (kind == 0xC148)
            dst += 8;
        load(out, dst, 0);
        load(out, src, 1);
        store(out, src);
        byte(out, 0x89);
        byte(out, 0x91);
        word32(out, reg_offset(dst)); /* MOV [RCX+dst],EDX */
        return 1;
    }
    unsigned address_alu = op & 0xF0C0;
    if (address_alu == 0xD0C0 || address_alu == 0x90C0 || address_alu == 0xB0C0) {
        unsigned width = op & 0x100 ? 2 : 1;
        if ((op & 0x30) == 0 && length == 1)
            load(out, op & 15, 1);
        else if ((op & 0x3F) == 0x3C && length == (width == 2 ? 3 : 2)) {
            byte(out, 0xBA);
            word32(out, width == 2 ? ((uint32)SWAPUW_IFLE(source[1]) << 16) | SWAPUW_IFLE(source[2])
                                   : SWAPUW_IFLE(source[1]));
        }
        else
            return 0;
        if (width == 1) {
            byte(out, 0x0F);
            byte(out, 0xBF);
            byte(out, 0xD2); /* MOVSX EDX,DX */
        }
        load(out, dst + 8, 0);
        byte(out, address_alu == 0xD0C0 ? 0x01 : address_alu == 0x90C0 ? 0x29 : 0x39);
        byte(out, 0xD0);
        if (address_alu == 0xB0C0)
            flags(out, 0);
        else
            store(out, dst + 8);
        return 1;
    }
    unsigned unary = op & 0xFF38;
    int testing = unary == 0x4A00 || (unary == 0x4A08 && size != 0);
    if (size < 3 && length == 1 && (unary == 0x4200 || unary == 0x4400 || unary == 0x4600 || testing)) {
        if (unary == 0x4200) {
            byte(out, 0x31);
            byte(out, 0xC0);
        }
        else {
            load(out, src + (unary == 0x4A08 ? 8 : 0), 0);
            if (!testing) {
                if (size == 1)
                    byte(out, 0x66);
                byte(out, size == 0 ? 0xF6 : 0xF7);
                byte(out, unary == 0x4400 ? 0xD8 : 0xD0); /* NEG / NOT */
            }
        }
        if (!testing)
            store_sized(out, src, size);
        if (unary != 0x4400)
            test(out, size);
        flags(out, unary == 0x4400);
        return 1;
    }
    if ((op & 0xF038) == 0x5000 && size < 3 && length == 1) {
        load(out, src, 0);
        if (size == 1)
            byte(out, 0x66);
        byte(out, size == 0 ? 0x80 : 0x83);
        byte(out, (op & 0x100) ? 0xE8 : 0xC0);
        byte(out, dst ? dst : 8);
        store_sized(out, src, size);
        flags(out, 1);
        return 1;
    }
    if ((op & 0xF038) == 0x5008 && length == 1 && (((op >> 6) & 3) == 1 || ((op >> 6) & 3) == 2)) {
        load(out, src + 8, 0);
        byte(out, 0x83);
        byte(out, (op & 0x100) ? 0xE8 : 0xC0);
        byte(out, dst ? dst : 8);
        store(out, src + 8);
        return 1;
    }
    if (size == 3)
        return 0;
    if (length == 1) {
        switch (op & 0xF138) {
        case 0xD000:
            alu = 0x01;
            extend = 1;
            break;
        case 0x9000:
            alu = 0x29;
            extend = 1;
            break;
        case 0xB000:
            alu = 0x39;
            write_result = 0;
            break;
        case 0xC000:
            alu = 0x21;
            break;
        case 0x8000:
            alu = 0x09;
            break;
        case 0xB100:
            alu = 0x31;
            src = dst;
            dst = op & 7;
            break;
        default:
            return 0;
        }
        load(out, dst, 0);
        load(out, src, 1);
    }
    else if (length == (size == 2 ? 3 : 2)) {
        switch (op & 0xFF38) {
        case 0x0600:
            alu = 0x01;
            extend = 1;
            break;
        case 0x0400:
            alu = 0x29;
            extend = 1;
            break;
        case 0x0C00:
            alu = 0x39;
            write_result = 0;
            break;
        case 0x0200:
            alu = 0x21;
            break;
        case 0x0000:
            alu = 0x09;
            break;
        case 0x0A00:
            alu = 0x31;
            break;
        default:
            return 0;
        }
        dst = src;
        load(out, dst, 0);
        byte(out, 0xBA);
        word32(out,
               size == 2 ? ((uint32)SWAPUW_IFLE(source[1]) << 16) | SWAPUW_IFLE(source[2]) : SWAPUW_IFLE(source[1]));
    }
    else
        return 0;
    if (size == 1)
        byte(out, 0x66);
    byte(out, size == 0 ? alu - 1 : alu);
    byte(out, 0xD0);
    if (write_result)
        store_sized(out, dst, size);
    flags(out, extend);
    return 1;
}

/* CCR elements may contain any nonzero value, so normalize before combining
   them. These host operations never write back to the guest CCR. */
static void boolean(emitter* out, unsigned offset, int rhs)
{
    byte(out, 0x8B);
    byte(out, rhs ? 0x91 : 0x81);
    word32(out, offset);
    byte(out, 0x85);
    byte(out, rhs ? 0xD2 : 0xC0);
    byte(out, 0x0F);
    byte(out, 0x95);
    byte(out, rhs ? 0xC2 : 0xC0);
    byte(out, 0x0F);
    byte(out, 0xB6);
    byte(out, rhs ? 0xD2 : 0xC0);
}

static void invert_boolean(emitter* out, int rhs)
{
    byte(out, 0x83);
    byte(out, rhs ? 0xF2 : 0xF0);
    byte(out, 1); /* XOR EDX/EAX,1 */
}

static void condition(emitter* out, unsigned cc)
{
    switch (cc) {
    case 0:
    case 1:
        byte(out, 0xB8);
        word32(out, cc == 0);
        break;
    case 2:
    case 3:
        boolean(out, offsetof(CPUState, ccc), 0);
        boolean(out, offsetof(CPUState, ccnz), 1);
        invert_boolean(out, cc == 3);
        byte(out, cc == 2 ? 0x21 : 0x09);
        byte(out, 0xD0);
        break;
    case 4:
    case 5:
        boolean(out, offsetof(CPUState, ccc), 0);
        if (cc == 4)
            invert_boolean(out, 0);
        break;
    case 6:
    case 7:
        boolean(out, offsetof(CPUState, ccnz), 0);
        if (cc == 7)
            invert_boolean(out, 0);
        break;
    case 8:
    case 9:
        boolean(out, offsetof(CPUState, ccv), 0);
        if (cc == 8)
            invert_boolean(out, 0);
        break;
    case 10:
    case 11:
        boolean(out, offsetof(CPUState, ccn), 0);
        if (cc == 10)
            invert_boolean(out, 0);
        break;
    default:
        boolean(out, offsetof(CPUState, ccn), 0);
        boolean(out, offsetof(CPUState, ccv), 1);
        byte(out, 0x31);
        byte(out, 0xD0); /* XOR EAX,EDX: N != V */
        if (cc == 12 || cc == 14)
            invert_boolean(out, 0);
        if (cc >= 14) {
            boolean(out, offsetof(CPUState, ccnz), 1);
            if (cc == 15)
                invert_boolean(out, 1);
            byte(out, cc == 14 ? 0x21 : 0x09);
            byte(out, 0xD0);
        }
        break;
    }
}

static void address_base(emitter* out, unsigned mode, unsigned reg, uint32 pc_base)
{
    if (mode == 5 || mode == 6) {
        byte(out, 0x03);
        byte(out, 0x91);
        word32(out, reg_offset(reg + 8));
    }
    else if (mode == 7 && reg >= 2) {
        byte(out, 0x81);
        byte(out, 0xC2);
        word32(out, pc_base);
    }
}

/* Leave a control EA in EDX, preserving EAX across live extension reads.
   The offset is four for instructions whose own extension precedes the EA. */
static unsigned address_value(emitter* out, unsigned code, uint32 pc, unsigned extension_offset)
{
    unsigned mode = code >> 3, reg = code & 7;
    if (mode == 2) {
        load(out, reg + 8, 1);
        return extension_offset;
    }
    if (mode < 5 || (mode == 7 && reg > 3))
        return 0;
    unsigned width = code == 57 ? 2 : 1;
    byte(out, 0xBA);
    word32(out, pc + extension_offset);
    memory_guard(out, 1U << width, pc, 1);
    memory_load(out, width, 1);
    if ((mode == 6 || code == 59) && out->variable_ea) {
        byte(out, 0xF7);
        byte(out, 0xC2);
        word32(out, 0x100);
        unsigned full = jump(out, 0x85);
        brief_index(out, pc);
        address_base(out, mode, reg, pc + extension_offset);
        pointer_r8(out, RELOC_NEXT_PC);
        byte(out, 0x41);
        byte(out, 0xC7);
        byte(out, 0x00);
        word32(out, pc + extension_offset + 2);
        byte(out, 0xE9);
        unsigned done = out->size;
        word32(out, 0);
        patch(out, full, out->size);
        indexed_address(out, code, pc, pc + extension_offset);
        patch(out, done, out->size);
        return DYNAMIC_PC;
    }
    if (mode == 6 || code == 59)
        brief_index(out, pc);
    else if (width == 1) {
        byte(out, 0x0F);
        byte(out, 0xBF);
        byte(out, 0xD2);
    }
    address_base(out, mode, reg, pc + extension_offset);
    return extension_offset + (1U << width);
}

static unsigned control_address(emitter* out, unsigned code, uint32 pc)
{
    unsigned length = address_value(out, code, pc, 2);
    if (length) {
        byte(out, 0x89);
        byte(out, 0xD0);
    }
    return length;
}

/* EAX holds the target before any change to A7 (including JSR (A7)).
   The synthetic return cache is optional; native returns dispatch by guest PC. */
static void push_return(emitter* out, uint32 pc, uint32 next_pc, int dynamic)
{
    byte(out, 0x41);
    byte(out, 0x89);
    byte(out, 0xC2);                /* R10D survives the simple stack guard. */
    memory_pointer(out, 39, 4, pc); /* -(A7), checked before committing A7. */
    instruction_end(out, next_pc, dynamic ? DYNAMIC_PC : 0);
    memory_store(out, 2);
    byte(out, 0x44);
    byte(out, 0x89);
    byte(out, 0xD0);
}

static int source_supported(unsigned code)
{
    return code < 8 || (code >= 16 && code <= 60);
}

/* Read word/long arithmetic sources into EDX, preserving EAX. Keeping the
   instruction extension in EAX avoids saving guest state across an EA guard. */
static unsigned data_source(emitter* out, unsigned code, unsigned width, uint32 pc, unsigned extension_offset)
{
    unsigned mode = code >> 3, reg = code & 7, size = 1U << width;
    if (mode == 0) {
        load(out, reg, 1);
        return extension_offset;
    }
    unsigned length = extension_offset;
    if (code == 60) {
        byte(out, 0xBA);
        word32(out, pc + extension_offset);
        memory_guard(out, size, pc, 1);
        length += size;
    }
    else {
        if (mode == 3 || mode == 4) {
            load(out, reg + 8, 1);
            if (mode == 4) {
                byte(out, 0x83);
                byte(out, 0xEA);
                byte(out, size);
            }
        }
        else
            length = address_value(out, code, pc, extension_offset);
        memory_guard(out, size, pc, 0);
    }
    memory_load(out, width, 1);
    if (mode == 3 || mode == 4) {
        byte(out, 0x83);
        byte(out, mode == 3 ? 0x81 : 0xA9);
        word32(out, reg_offset(reg + 8));
        byte(out, size);
    }
    return length;
}

static unsigned destination_pointer(emitter* out, unsigned code, unsigned size, uint32 pc, unsigned extension_offset)
{
    unsigned mode = code >> 3, reg = code & 7;
    unsigned step = size == 1 && reg == 7 ? 2 : size;
    unsigned length = extension_offset;
    if (mode == 3 || mode == 4) {
        load(out, reg + 8, 1);
        if (mode == 4) {
            byte(out, 0x83);
            byte(out, 0xEA);
            byte(out, step);
        }
    }
    else
        length = address_value(out, code, pc, extension_offset);
    memory_guard(out, size, pc, 0);
    if (mode == 3 || mode == 4) {
        byte(out, 0x83);
        byte(out, mode == 3 ? 0x81 : 0xA9);
        word32(out, reg_offset(reg + 8));
        byte(out, step);
    }
    return length;
}

static int immediate_memory(emitter* out, unsigned op, uint32 pc)
{
    unsigned code = op & 63, mode = code >> 3, kind = (op >> 6) & 3;
    unsigned operation = op & 0xFF00;
    int bit = operation == 0x0800;
    if ((!bit && (kind == 3 || (operation != 0 && operation != 0x0200 && operation != 0x0400 && operation != 0x0600 &&
                                operation != 0x0A00 && operation != 0x0C00))) ||
        mode < 2 || (mode == 7 && (code & 7) > ((bit && !kind) || operation == 0x0C00 ? 3 : 1)))
        return 0;
    unsigned width = bit ? 0 : kind;
    unsigned immediate_size = width == 2 ? 4 : 2;
    memory_pointer(out, 64, immediate_size, pc);
    memory_load(out, width == 2 ? 2 : 1, 0);
    if (bit) {
        byte(out, 0x83);
        byte(out, 0xE0);
        byte(out, 7);
    }
    unsigned length = destination_pointer(out, code, 1U << width, pc, 2 + immediate_size);
    memory_load(out, width, 1);
    if (bit) {
        byte(out, 0x0F);
        byte(out, kind == 0 ? 0xA3 : kind == 1 ? 0xBB : kind == 2 ? 0xB3 : 0xAB);
        byte(out, 0xC2); /* BT/BTC/BTR/BTS EDX,EAX. */
    }
    else {
        unsigned alu = operation == 0        ? 0x09
                       : operation == 0x0200 ? 0x21
                       : operation == 0x0400 ? 0x29
                       : operation == 0x0600 ? 0x01
                       : operation == 0x0A00 ? 0x31
                                             : 0x39;
        if (width == 1)
            byte(out, 0x66);
        byte(out, width == 0 ? alu - 1 : alu);
        byte(out, 0xC2); /* Memory value is the left operand, immediate the right. */
    }
    byte(out, 0x89);
    byte(out, 0xD0);
    if (bit)
        flag(out, 0x92, offsetof(CPUState, ccnz));
    else
        flags(out, operation == 0x0400 || operation == 0x0600);
    if (bit ? kind != 0 : operation != 0x0C00)
        memory_store(out, width);
    instruction_end(out, pc, length);
    return 1;
}

static int unary_memory(emitter* out, unsigned op, uint32 pc)
{
    unsigned code = op & 63, mode = code >> 3;
    int negate = (op & 0xFF00) == 0x4000 && (op & 0xC0) != 0xC0;
    int shifting = (op & 0xF8C0) == 0xE0C0;
    if ((!negate && !shifting) || mode < 2 || (mode == 7 && (code & 7) > 1))
        return 0;
    unsigned width = negate ? (op >> 6) & 3 : 1;
    unsigned length = destination_pointer(out, code, 1U << width, pc, 2);
    if (negate) {
        memory_load(out, width, 1);
        byte(out, 0x31);
        byte(out, 0xC0);
        byte(out, 0x44);
        byte(out, 0x8B);
        byte(out, 0x89);
        word32(out, offsetof(CPUState, ccnz));
        extend_carry(out);
        if (width == 1)
            byte(out, 0x66);
        byte(out, width == 0 ? 0x18 : 0x19);
        byte(out, 0xD0);
        flags(out, 1);
        byte(out, 0x44);
        byte(out, 0x09);
        byte(out, 0x89);
        word32(out, offsetof(CPUState, ccnz)); /* Sticky Z, without clobbering R8. */
    }
    else {
        unsigned kind = (op >> 9) & 3, left = !!(op & 0x100);
        memory_load(out, 1, 0);
        if (kind == 2)
            extend_carry(out);
        byte(out, 0x66);
        byte(out, 0xD1);
        byte(out, kind == 0   ? (left ? 0xE0 : 0xF8)
                  : kind == 1 ? (left ? 0xE0 : 0xE8)
                  : kind == 2 ? (left ? 0xD0 : 0xD8)
                              : (left ? 0xC0 : 0xC8));
        flag(out, 0x92, offsetof(CPUState, ccc));
        if (kind != 3)
            flag(out, 0x92, offsetof(CPUState, ccx));
        if (kind == 0 && left)
            flag(out, 0x90, offsetof(CPUState, ccv));
        else {
            byte(out, 0xC7);
            byte(out, 0x81);
            word32(out, offsetof(CPUState, ccv));
            word32(out, 0);
        }
        test(out, 1);
        flag(out, 0x95, offsetof(CPUState, ccnz));
        flag(out, 0x98, offsetof(CPUState, ccn));
    }
    memory_store(out, width);
    instruction_end(out, pc, length);
    return 1;
}

/* A semantic exception tail must re-execute the instruction's source read.
   Restore its sole native address-register update before entering the decoder. */
static void undo_source_update(emitter* out, unsigned code, unsigned size)
{
    unsigned mode = code >> 3;
    if (mode == 3 || mode == 4) {
        byte(out, 0x83);
        byte(out, mode == 3 ? 0xA9 : 0x81);
        word32(out, reg_offset((code & 7) + 8));
        byte(out, size);
    }
}

/* Snapshot long-arithmetic operands after checking the live extension.
   R11D holds the extension, R10D the low register and R9D the high register. */
static unsigned long_operands(emitter* out, unsigned op, uint32 pc)
{
    memory_pointer(out, 64, 2, pc);
    memory_load(out, 1, 0);
    byte(out, 0xF7);
    byte(out, 0xC0);
    word32(out, 0x83F8);
    unsigned valid = jump(out, 0x84);
    decoder_tail(out, pc, 4);
    patch(out, valid, out->size);
    unsigned length = data_source(out, op & 63, 2, pc, 4);
    byte(out, 0x41);
    byte(out, 0x89);
    byte(out, 0xC3);
    byte(out, 0x41);
    byte(out, 0x89);
    byte(out, 0xC2);
    byte(out, 0x41);
    byte(out, 0xC1);
    byte(out, 0xEA);
    byte(out, 12);
    byte(out, 0x41);
    byte(out, 0x89);
    byte(out, 0xC1);
    byte(out, 0x41);
    byte(out, 0x83);
    byte(out, 0xE1);
    byte(out, 7);
    byte(out, 0x42);
    byte(out, 0x8B);
    byte(out, 0x84);
    byte(out, 0x91);
    word32(out, reg_offset(0));
    return length;
}

static int multiple_registers(emitter* out, unsigned op, uint32 pc)
{
    unsigned mode = (op >> 3) & 7, address = (op & 7) + 8;
    int reading = !!(op & 0x400);
    if ((op & 0xFB80) != 0x4880 || mode < 2 || (reading ? mode == 4 : mode == 3) ||
        (mode == 7 && (op & 7) > (reading ? 3 : 1)))
        return 0;
    unsigned width = op & 0x40 ? 2 : 1, size = 1U << width;
    unsigned span = 16 * size;
    memory_pointer(out, 64, 2, pc);
    memory_load(out, 1, 0);
    unsigned length = 4;
    if (mode == 3 || mode == 4)
        load(out, address, 1);
    else
        length = address_value(out, op & 63, pc, 4);
    byte(out, 0x41);
    byte(out, 0x89);
    byte(out, 0xC3); /* R11D = live mask, preserved across EA calculation. */
    byte(out, 0x85);
    byte(out, 0xC0);
    unsigned empty = jump(out, 0x84);
    byte(out, 0x41);
    byte(out, 0x89);
    byte(out, 0xD2); /* R10D retains the unmasked guest address. */
    if (mode == 4) {
        byte(out, 0x83);
        byte(out, 0xEA);
        byte(out, span);
    }
    /* A conservative maximum span lets all selected transfers finish without
     callbacks or faults. Any uncertain span leaves before guest side effects. */
    memory_guard(out, span, pc, 0);
    if (mode == 4) {
        byte(out, 0x49);
        byte(out, 0x83);
        byte(out, 0xC0);
        byte(out, span);
        byte(out, 0x45);
        byte(out, 0x89);
        byte(out, 0xD1);
        byte(out, 0x41);
        byte(out, 0x83);
        byte(out, 0xE9);
        byte(out, size); /* R9D = initial An-size for a stored base register. */
    }
    for (unsigned bit = 0; bit < 16; ++bit) {
        unsigned selected = mode == 4 ? 15 - bit : bit;
        byte(out, 0x41);
        byte(out, 0xF7);
        byte(out, 0xC3);
        word32(out, 1U << bit);
        unsigned skip = jump(out, 0x84);
        if (mode == 4) {
            byte(out, 0x49);
            byte(out, 0x83);
            byte(out, 0xE8);
            byte(out, size);
            byte(out, 0x41);
            byte(out, 0x83);
            byte(out, 0xEA);
            byte(out, size);
            byte(out, 0x44);
            byte(out, 0x89);
            byte(out, 0x91);
            word32(out, reg_offset(address));
        }
        if (reading) {
            memory_load(out, width, 0);
            if (width == 1) {
                byte(out, 0x0F);
                byte(out, 0xBF);
                byte(out, 0xC0);
            }
            if (mode != 3 || selected != address)
                store(out, selected);
        }
        else {
            if (mode == 4 && selected == address) {
                byte(out, 0x44);
                byte(out, 0x89);
                byte(out, 0xC8);
            }
            else
                load(out, selected, 0);
            memory_store(out, width);
        }
        if (mode != 4) {
            byte(out, 0x49);
            byte(out, 0x83);
            byte(out, 0xC0);
            byte(out, size);
            byte(out, 0x41);
            byte(out, 0x83);
            byte(out, 0xC2);
            byte(out, size);
            if (mode == 3) {
                byte(out, 0x44);
                byte(out, 0x89);
                byte(out, 0x91);
                word32(out, reg_offset(address));
            }
        }
        patch(out, skip, out->size);
    }
    patch(out, empty, out->size);
    instruction_end(out, pc, length);
    out->cached_address_register = -1;
    return 1;
}

static int range_check(emitter* out, unsigned op, uint32 pc)
{
    unsigned mode = (op >> 3) & 7, code = op & 63;
    if ((op & 0xF9C0) != 0x00C0 || (op & 0x600) == 0x600 ||
        (mode != 2 && mode != 5 && mode != 6 && (mode != 7 || (op & 7) > 3)))
        return 0;
    unsigned width = (op >> 9) & 3, size = 1U << width;
    memory_pointer(out, 64, 2, pc);
    memory_load(out, 1, 0);
    byte(out, 0xA9);
    word32(out, 0x07FF);
    unsigned valid = jump(out, 0x84);
    decoder_tail(out, pc, 4);
    patch(out, valid, out->size);
    unsigned length = address_value(out, code, pc, 4);
    byte(out, 0x41);
    byte(out, 0x89);
    byte(out, 0xC3); /* Preserve the checked extension across the span guard. */
    memory_guard(out, 2 * size, pc, 0);
    memory_load(out, width, 1);
    byte(out, 0x41);
    byte(out, 0x89);
    byte(out, 0xD2);
    byte(out, 0x49);
    byte(out, 0x83);
    byte(out, 0xC0);
    byte(out, size);
    memory_load(out, width, 1);
    byte(out, 0x41);
    byte(out, 0x89);
    byte(out, 0xD1); /* R10D/R9D = lower/upper. */
    byte(out, 0x45);
    byte(out, 0x89);
    byte(out, 0xD8);
    byte(out, 0x41);
    byte(out, 0xC1);
    byte(out, 0xE8);
    byte(out, 12);
    byte(out, 0x42);
    byte(out, 0x8B);
    byte(out, 0x84);
    byte(out, 0x81);
    word32(out, reg_offset(0)); /* EAX = selected Dn/An. */
    if (width < 2) {
        byte(out, 0x41);
        byte(out, 0xF7);
        byte(out, 0xC3);
        word32(out, 0x8000);
        unsigned address_register = jump(out, 0x85);
        byte(out, 0x25);
        word32(out, width == 0 ? 0xFF : 0xFFFF);
        byte(out, 0xE9);
        unsigned normalized = out->size;
        word32(out, 0);
        patch(out, address_register, out->size);
        for (unsigned reg = 0; reg < 2; ++reg) {
            byte(out, 0x45);
            byte(out, 0x0F);
            byte(out, width == 0 ? 0xBE : 0xBF);
            byte(out, reg == 0 ? 0xD2 : 0xC9);
        }
        patch(out, normalized, out->size);
    }
    byte(out, 0x44);
    byte(out, 0x39);
    byte(out, 0xD0);
    byte(out, 0x0F);
    byte(out, 0x95);
    byte(out, 0xC2);
    byte(out, 0x0F);
    byte(out, 0xB6);
    byte(out, 0xD2);
    byte(out, 0x44);
    byte(out, 0x39);
    byte(out, 0xC8);
    byte(out, 0x41);
    byte(out, 0x0F);
    byte(out, 0x95);
    byte(out, 0xC0);
    byte(out, 0x45);
    byte(out, 0x0F);
    byte(out, 0xB6);
    byte(out, 0xC0);
    byte(out, 0x44);
    byte(out, 0x21);
    byte(out, 0xC2);
    byte(out, 0x89);
    byte(out, 0x91);
    word32(out, offsetof(CPUState, ccnz));
    byte(out, 0x45);
    byte(out, 0x29);
    byte(out, 0xD1);
    byte(out, 0x44);
    byte(out, 0x29);
    byte(out, 0xD0);
    if (width < 2) {
        byte(out, 0x41);
        byte(out, 0xF7);
        byte(out, 0xC3);
        word32(out, 0x8000);
        unsigned full_range = jump(out, 0x85);
        byte(out, 0x25);
        word32(out, width == 0 ? 0xFF : 0xFFFF);
        byte(out, 0x41);
        byte(out, 0x81);
        byte(out, 0xE1);
        word32(out, width == 0 ? 0xFF : 0xFFFF);
        patch(out, full_range, out->size);
    }
    byte(out, 0x44);
    byte(out, 0x39);
    byte(out, 0xC8);
    flag(out, 0x97, offsetof(CPUState, ccc));
    byte(out, 0x41);
    byte(out, 0xF7);
    byte(out, 0xC3);
    word32(out, 0x800);
    unsigned no_trap = jump(out, 0x84);
    byte(out, 0x44);
    byte(out, 0x39);
    byte(out, 0xC8);
    unsigned in_range = jump(out, 0x86);
    decoder_tail(out, pc, 4);
    patch(out, no_trap, out->size);
    patch(out, in_range, out->size);
    instruction_end(out, pc, length);
    return 1;
}

static int paired_operands(emitter* out, unsigned op, uint32 pc)
{
    unsigned family = op >> 12;
    int move = family >= 1 && family <= 3;
    int compare = (op & 0xF138) == 0xB108;
    if (!move && !compare && (op & 0xF138) != 0x9108 && (op & 0xF138) != 0xD108)
        return 0;
    if (!move && (op & 0xC0) == 0xC0)
        return 0;
    unsigned width = move ? (family == 1 ? 0 : family == 3 ? 1 : 2) : (op >> 6) & 3;
    /* The helper owns no callbacks, writes no guest data and rolls back all
     address changes on failure. Success makes the native body non-faulting. */
    byte(out, 0x48);
    byte(out, 0x83);
    byte(out, 0xEC);
    byte(out, 72);
    byte(out, 0x48);
    byte(out, 0x89);
    byte(out, 0x4C);
    byte(out, 0x24);
    byte(out, 56);
    byte(out, 0xB9);
    word32(out, op);
    byte(out, 0xBA);
    word32(out, pc);
    byte(out, 0x4C);
    byte(out, 0x8D);
    byte(out, 0x44);
    byte(out, 0x24);
    byte(out, 32);
    byte(out, 0x48);
    byte(out, 0xB8);
    pointer_value(out, RELOC_PAIR);
    byte(out, 0xFF);
    byte(out, 0xD0);
    byte(out, 0x85);
    byte(out, 0xC0);
    byte(out, 0x48);
    byte(out, 0x8B);
    byte(out, 0x4C);
    byte(out, 0x24);
    byte(out, 56);
    unsigned valid = jump(out, 0x85);
    byte(out, 0x48);
    byte(out, 0x83);
    byte(out, 0xC4);
    byte(out, 72);
    decoder_tail(out, pc, 4);
    patch(out, valid, out->size);
    byte(out, 0x8B);
    byte(out, 0x54);
    byte(out, 0x24);
    byte(out, 32 + offsetof(Syn68kX64Pair, source));
    byte(out, 0x8B);
    byte(out, 0x44);
    byte(out, 0x24);
    byte(out, 32 + offsetof(Syn68kX64Pair, destination));
    byte(out, 0x44);
    byte(out, 0x8B);
    byte(out, 0x4C);
    byte(out, 0x24);
    byte(out, 32 + offsetof(Syn68kX64Pair, next_pc));
    byte(out, 0x4C);
    byte(out, 0x8B);
    byte(out, 0x44);
    byte(out, 0x24);
    byte(out, 32 + offsetof(Syn68kX64Pair, host_destination));
    byte(out, 0x48);
    byte(out, 0x83);
    byte(out, 0xC4);
    byte(out, 72);
    byte(out, 0x49);
    byte(out, 0xBA);
    pointer_value(out, RELOC_NEXT_PC);
    byte(out, 0x45);
    byte(out, 0x89);
    byte(out, 0x0A);
    if (move) {
        unsigned destination = ((op >> 3) & 56) | ((op >> 9) & 7);
        byte(out, 0x89);
        byte(out, 0xD0);
        if (destination >= 8 && destination < 16) {
            if (width == 1) {
                byte(out, 0x0F);
                byte(out, 0xBF);
                byte(out, 0xC0);
            }
            store(out, destination);
        }
        else {
            test(out, width);
            flags(out, 0);
            if (destination < 8)
                store_sized(out, destination, width);
            else
                memory_store(out, width);
        }
    }
    else {
        if (!compare) {
            byte(out, 0x44);
            byte(out, 0x8B);
            byte(out, 0x89);
            word32(out, offsetof(CPUState, ccnz));
            extend_carry(out);
        }
        if (width == 1)
            byte(out, 0x66);
        unsigned alu = compare ? 0x39 : family == 9 ? 0x19 : 0x11;
        byte(out, width == 0 ? alu - 1 : alu);
        byte(out, 0xD0);
        flags(out, !compare);
        if (!compare) {
            byte(out, 0x44);
            byte(out, 0x09);
            byte(out, 0x89);
            word32(out, offsetof(CPUState, ccnz));
            memory_store(out, width);
        }
    }
    instruction_end(out, pc, DYNAMIC_PC);
    out->cached_address_register = -1;
    return 1;
}

static int condition_transfer(emitter* out, unsigned op, uint32 pc)
{
    unsigned operation = op & 0xFFC0, code = op & 63;
    if (operation == 0x42C0 && (code < 8 || (code >= 16 && code <= 57))) {
        read_ccr(out);
        unsigned length = 2;
        if (code < 8)
            store_sized(out, code, 1);
        else {
            length = destination_pointer(out, code, 2, pc, 2);
            memory_store(out, 1);
        }
        instruction_end(out, pc, length);
        return 1;
    }
    if (operation == 0x44C0 && source_supported(code)) {
        unsigned length = data_source(out, code, 1, pc, 2);
        byte(out, 0x89);
        byte(out, 0xD0);
        write_ccr(out);
        instruction_end(out, pc, length);
        return 1;
    }
    return 0;
}

static unsigned terminal_length(unsigned op, unsigned words)
{
    unsigned code = op & 63;
    if ((op >> 12) >= 1 && (op >> 12) <= 3 && code < 8)
        code = ((op >> 3) & 56) | ((op >> 9) & 7);
    return syn68k_x64_native_memory_words(op) && ((code >> 3) == 6 || code == 59) ? DYNAMIC_PC : 2 * words;
}

#include "jit-fpu.h"
#include "jit-mmu.h"

static int branch(emitter* out, const uint16* source, unsigned length, uint32 pc)
{
    unsigned op = SWAPUW_IFLE(source[0]);
    unsigned cc = (op >> 8) & 15;
    out->variable_ea = 1;
    if ((op & ~255u) == 0xf200 || mapped_frame_opcode(op) || op == 0x4E73 || (op & 0xFFFE) == 0x4E7A) {
        /* Reserve the guard and the largest following inline FPU body before
           emitting either path, so a full buffer cannot leave a partial guard. */
        if (out->size + 4192 > sizeof out->bytes)
            return 0;
        /* Trace eligibility is live, including when an AOT image is reused.
           Enter the managed native body before consuming the command once. */
        fpu_cpu(out, 0xf7, 0, offsetof(CPUState, sr));
        word32(out, 0xc000);
        unsigned untraced = jump(out, 0x84);
        flat_native_tail(out, pc, op);
        patch(out, untraced, out->size);
    }
    int fpu = fpu_conditional(out, op, pc);
    if (!fpu)
        fpu = fpu_register(out, op, pc);
    if (fpu)
        return fpu > 0;
    if ((op & ~255u) == 0xf200 || mapped_frame_opcode(op) || op == 0x4E73 || (op & 0xFFFE) == 0x4E7A) {
        if (out->size + 64 > sizeof out->bytes)
            return 0;
        flat_native_tail(out, pc, op);
        return 1;
    }
    if (syn68k_x64_complex_opcode(op)) {
        ++out->guards;
        out->dynamic = 1;
        byte(out, 0xB9);
        word32(out, pc);
        byte(out, 0xBA);
        word32(out, op);
        byte(out, 0x48);
        byte(out, 0xB8);
        pointer_value(out, RELOC_COMPLEX);
        byte(out, 0xFF);
        byte(out, 0xE0);
        return 1;
    }
    if (condition_transfer(out, op, pc))
        return 1;
    if ((op & 0xFFF0) == 0x4E40) {
        exception_tail(out, pc + 2, 0, 32 + (op & 15));
        return 1;
    }
    if (op == 0x4AFC || (op & 0xFFF8) == 0x4848) {
        decoder_tail(out, pc, 4);
        return 1;
    }
    if (op == 0x4E76 || ((op & 0xF0FF) >= 0x50FA && (op & 0xF0FF) <= 0x50FC)) {
        unsigned bytes = op == 0x4E76 || (op & 255) == 0xFC ? 0 : (op & 255) == 0xFA ? 2 : 4;
        if (bytes)
            memory_pointer(out, 64, bytes, pc);
        condition(out, op == 0x4E76 ? 9 : cc);
        byte(out, 0x85);
        byte(out, 0xC0);
        unsigned no_trap = jump(out, 0x84);
        exception_tail(out, pc + 2 + bytes, pc, 7);
        patch(out, no_trap, out->size);
        instruction_end(out, pc, 2 + bytes);
        return 1;
    }
    if (length != 1) {
        if (!instruction(out, source, length, pc))
            return 0;
        instruction_end(out, pc, terminal_length(op, length));
        return 1;
    }
    if (immediate_memory(out, op, pc) || unary_memory(out, op, pc))
        return 1;
    if (multiple_registers(out, op, pc))
        return 1;
    if (range_check(out, op, pc))
        return 1;
    if ((op & 0xF138) == 0x0108) {
        unsigned bytes = op & 0x40 ? 4 : 2;
        unsigned reg = (op >> 9) & 7;
        /* Guard the complete strided span before touching any byte. Holes,
         page crossings, aliases and translated stores use the checked path. */
        memory_pointer(out, 40 + (op & 7), 2 * bytes - 1, pc);
        if (op & 0x80) {
            load(out, reg, 0);
            for (unsigned i = 0; i < bytes; ++i) {
                byte(out, 0x89);
                byte(out, 0xC2);
                if (i + 1 < bytes) {
                    byte(out, 0xC1);
                    byte(out, 0xEA);
                    byte(out, 8 * (bytes - i - 1));
                }
                byte(out, 0x41);
                byte(out, 0x88);
                byte(out, 0x50);
                byte(out, 2 * i);
            }
        }
        else {
            byte(out, 0x31);
            byte(out, 0xC0);
            for (unsigned i = 0; i < bytes; ++i) {
                if (i) {
                    byte(out, 0xC1);
                    byte(out, 0xE0);
                    byte(out, 8);
                }
                byte(out, 0x41);
                byte(out, 0x0F);
                byte(out, 0xB6);
                byte(out, 0x50);
                byte(out, 2 * i);
                byte(out, 0x09);
                byte(out, 0xD0);
            }
            store_sized(out, reg, bytes == 4 ? 2 : 1);
        }
        byte(out, 0xB8);
        word32(out, pc + 4);
        return 1;
    }
    if ((op & 0xFFF8) == 0x4E50 || (op & 0xFFF8) == 0x4808) {
        unsigned width = (op & 0xFFF8) == 0x4E50 ? 1 : 2;
        memory_pointer(out, 64, 1U << width, pc);
        memory_load(out, width, 0);
        if (width == 1) {
            byte(out, 0x0F);
            byte(out, 0xBF);
            byte(out, 0xC0);
        }
        byte(out, 0x41);
        byte(out, 0x89);
        byte(out, 0xC2); /* R10D retains the live displacement. */
        memory_pointer(out, 39, 4, pc);
        load(out, (op & 7) + 8, 0);
        if ((op & 7) == 7) {
            /* The implicit predecrement changed SP; store its original source. */
            byte(out, 0x83);
            byte(out, 0xC0);
            byte(out, 4);
        }
        memory_store(out, 2);
        load(out, 15, 0);
        store(out, (op & 7) + 8);
        byte(out, 0x44);
        byte(out, 0x01);
        byte(out, 0x91);
        word32(out, reg_offset(15));
        byte(out, 0xB8);
        word32(out, pc + 2 + (1U << width));
        return 1;
    }
    if ((op & 0xFFF8) == 0x4E58) {
        memory_pointer(out, 16 + (op & 7), 4, pc);
        load(out, (op & 7) + 8, 0);
        store(out, 15);
        memory_load(out, 2, 0);
        store(out, (op & 7) + 8);
        byte(out, 0x83);
        byte(out, 0x81);
        word32(out, reg_offset(15));
        byte(out, 4); /* UNLK A7 increments the restored register. */
        byte(out, 0xB8);
        word32(out, pc + 2);
        return 1;
    }
    if ((op & 0xF000) == 0x6000 && cc != 1 && (op & 255) != 0 && (op & 255) != 255) {
        uint32 target = pc + 2 + (uint32)(int32)(int8)op;
        if (cc) {
            condition(out, cc);
            byte(out, 0x85);
            byte(out, 0xC0);
            byte(out, 0xB8);
            word32(out, pc + 2);
            byte(out, 0x74);
            byte(out, 5); /* JZ past the taken target MOV */
        }
        byte(out, 0xB8);
        word32(out, target);
        return 1;
    }
    if ((op & 0xFF00) == 0x6100) {
        unsigned size = !(op & 255) ? 2 : (op & 255) == 255 ? 4 : 0;
        if (size) {
            memory_pointer(out, 64, size, pc);
            memory_load(out, size == 2 ? 1 : 2, 0);
            if (size == 2) {
                byte(out, 0x0F);
                byte(out, 0xBF);
                byte(out, 0xC0);
            }
            byte(out, 0x05);
            word32(out, pc + 2);
        }
        else {
            byte(out, 0xB8);
            word32(out, pc + 2 + (uint32)(int32)(int8)op);
        }
        push_return(out, pc, pc + 2 + size, 0);
        return 1;
    }
    if (op == 0x4E74 || op == 0x4E75 || op == 0x4E77) {
        if (op == 0x4E74) {
            memory_pointer(out, 64, 2, pc);
            memory_load(out, 1, 0);
            byte(out, 0x44);
            byte(out, 0x0F);
            byte(out, 0xBF);
            byte(out, 0xD0); /* R10D = live signed RTD displacement. */
        }
        memory_pointer(out, 31, op == 0x4E77 ? 6 : 4, pc);
        if (op == 0x4E77) {
            memory_load(out, 1, 0);
            write_ccr(out);
            byte(out, 0x49);
            byte(out, 0x83);
            byte(out, 0xC0);
            byte(out, 2); /* Saved PC follows the CCR word. */
        }
        memory_load(out, 2, 0);
        if (op == 0x4E74) {
            byte(out, 0x44);
            byte(out, 0x01);
            byte(out, 0x91);
            word32(out, reg_offset(15));
        }
        return 1;
    }
    if ((op & 0xFF80) == 0x4E80) {
        unsigned bytes = control_address(out, op & 63, pc);
        if (bytes) {
            if (!(op & 0x40))
                push_return(out, pc, pc + bytes, bytes == DYNAMIC_PC);
            return 1;
        }
    }
    if ((op & 0xF1C0) == 0x41C0 || ((op & 0xFFC0) == 0x4840 && (op & 0x30))) {
        unsigned bytes = control_address(out, op & 63, pc);
        if (bytes) {
            if ((op & 0xF1C0) == 0x41C0) {
                store(out, ((op >> 9) & 7) + 8);
                out->cached_address_register = -1;
            }
            else {
                memory_pointer(out, 39, 4, pc);
                memory_store(out, 2);
            }
            instruction_end(out, pc, bytes);
            return 1;
        }
    }
    if (((op & 0xF000) == 0x6000 && cc != 1) || (op & 0xF0F8) == 0x50C8) {
        int decrement = (op & 0xF0F8) == 0x50C8;
        unsigned width = decrement || !(op & 255) ? 1 : 2;
        memory_pointer(out, 64, 1U << width, pc);
        memory_load(out, width, 0);
        if (width == 1) {
            byte(out, 0x0F);
            byte(out, 0xBF);
            byte(out, 0xC0); /* MOVSX EAX,AX */
        }
        byte(out, 0x41);
        byte(out, 0x89);
        byte(out, 0xC2); /* R10D retains the live displacement. */
        condition(out, cc);
        byte(out, 0x85);
        byte(out, 0xC0);
        unsigned fallthrough = jump(out, decrement ? 0x85 : 0x84);
        unsigned expired = 0;
        if (decrement) {
            load(out, op & 7, 0);
            byte(out, 0x66);
            byte(out, 0x83);
            byte(out, 0xE8);
            byte(out, 1); /* SUB AX,1 leaves the upper word intact. */
            store_sized(out, op & 7, 1);
            byte(out, 0x66);
            byte(out, 0x83);
            byte(out, 0xF8);
            byte(out, 255);
            expired = jump(out, 0x84);
        }
        byte(out, 0x44);
        byte(out, 0x89);
        byte(out, 0xD0);
        byte(out, 0x05);
        word32(out, pc + 2);
        byte(out, 0xE9);
        unsigned done = out->size;
        word32(out, 0);
        patch(out, fallthrough, out->size);
        if (decrement)
            patch(out, expired, out->size);
        byte(out, 0xB8);
        word32(out, pc + 2 + (1U << width));
        patch(out, done, out->size);
        return 1;
    }
    if ((op & 0xFFC0) == 0x4C40 && source_supported(op & 63)) {
        unsigned length = long_operands(out, op, pc);
        byte(out, 0x41);
        byte(out, 0x89);
        byte(out, 0xD0); /* R8D = divisor. */
        byte(out, 0x85);
        byte(out, 0xD2);
        unsigned zero = jump(out, 0x84);
        byte(out, 0x41);
        byte(out, 0xF7);
        byte(out, 0xC3);
        word32(out, 0x400);
        unsigned narrow = jump(out, 0x84);
        byte(out, 0x42);
        byte(out, 0x8B);
        byte(out, 0x94);
        byte(out, 0x89);
        word32(out, reg_offset(0));
        byte(out, 0x48);
        byte(out, 0xC1);
        byte(out, 0xE2);
        byte(out, 32);
        byte(out, 0x48);
        byte(out, 0x09);
        byte(out, 0xD0); /* Wide dividend in RAX. */
        patch(out, narrow, out->size);
        byte(out, 0x41);
        byte(out, 0xF7);
        byte(out, 0xC3);
        word32(out, 0x800);
        unsigned unsigned_divide = jump(out, 0x84);
        byte(out, 0x4D);
        byte(out, 0x63);
        byte(out, 0xC0); /* Sign-extend divisor. */
        byte(out, 0x41);
        byte(out, 0xF7);
        byte(out, 0xC3);
        word32(out, 0x400);
        unsigned wide = jump(out, 0x85);
        byte(out, 0x48);
        byte(out, 0x63);
        byte(out, 0xC0);
        patch(out, wide, out->size);
        byte(out, 0x48);
        byte(out, 0xBA);
        word32(out, 0);
        word32(out, 0x80000000);
        byte(out, 0x48);
        byte(out, 0x39);
        byte(out, 0xD0);
        unsigned safe = jump(out, 0x85);
        byte(out, 0x49);
        byte(out, 0x83);
        byte(out, 0xF8);
        byte(out, 0xFF);
        unsigned host_overflow = jump(out, 0x84);
        patch(out, safe, out->size);
        byte(out, 0x48);
        byte(out, 0x99);
        byte(out, 0x49);
        byte(out, 0xF7);
        byte(out, 0xF8); /* IDIV R8; INT64_MIN/-1 was excluded. */
        byte(out, 0x4C);
        byte(out, 0x63);
        byte(out, 0xC0); /* Representable quotient must sign-extend EAX. */
        byte(out, 0xE9);
        unsigned signed_done = out->size;
        word32(out, 0);
        patch(out, unsigned_divide, out->size);
        byte(out, 0x31);
        byte(out, 0xD2);
        byte(out, 0x49);
        byte(out, 0xF7);
        byte(out, 0xF0);
        byte(out, 0x41);
        byte(out, 0x89);
        byte(out, 0xC0); /* Unsigned quotient must zero-extend EAX. */
        patch(out, signed_done, out->size);
        byte(out, 0x4C);
        byte(out, 0x39);
        byte(out, 0xC0);
        unsigned quotient_overflow = jump(out, 0x85);
        byte(out, 0x42);
        byte(out, 0x89);
        byte(out, 0x94);
        byte(out, 0x89);
        word32(out, reg_offset(0));
        byte(out, 0x42);
        byte(out, 0x89);
        byte(out, 0x84);
        byte(out, 0x91);
        word32(out, reg_offset(0)); /* Aliased Dr/Dq keeps quotient. */
        test(out, 2);
        flags(out, 0);
        instruction_end(out, pc, length);
        byte(out, 0xC3);
        patch(out, host_overflow, out->size);
        patch(out, quotient_overflow, out->size);
        byte(out, 0xC7);
        byte(out, 0x81);
        word32(out, offsetof(CPUState, ccc));
        word32(out, 0);
        byte(out, 0xC7);
        byte(out, 0x81);
        word32(out, offsetof(CPUState, ccv));
        word32(out, 1); /* Undefined N/Z and all registers stay unchanged. */
        instruction_end(out, pc, length);
        byte(out, 0xC3);
        patch(out, zero, out->size);
        undo_source_update(out, op & 63, 4);
        decoder_tail(out, pc, 4);
        return 1;
    }
    if ((op & 0xFFC0) == 0x4C00 && source_supported(op & 63)) {
        unsigned length = long_operands(out, op, pc);
        byte(out, 0x41);
        byte(out, 0xF7);
        byte(out, 0xC3);
        word32(out, 0x800);
        unsigned unsigned_product = jump(out, 0x84);
        byte(out, 0x48);
        byte(out, 0x63);
        byte(out, 0xC0);
        byte(out, 0x48);
        byte(out, 0x63);
        byte(out, 0xD2);
        patch(out, unsigned_product, out->size);
        byte(out, 0x48);
        byte(out, 0x0F);
        byte(out, 0xAF);
        byte(out, 0xC2); /* Complete 32-by-32 product in RAX. */
        byte(out, 0x42);
        byte(out, 0x89);
        byte(out, 0x84);
        byte(out, 0x91);
        word32(out, reg_offset(0));
        byte(out, 0x41);
        byte(out, 0xF7);
        byte(out, 0xC3);
        word32(out, 0x400);
        unsigned narrow = jump(out, 0x84);
        byte(out, 0x49);
        byte(out, 0x89);
        byte(out, 0xC0);
        byte(out, 0x49);
        byte(out, 0xC1);
        byte(out, 0xE8);
        byte(out, 32);
        byte(out, 0x46);
        byte(out, 0x89);
        byte(out, 0x84);
        byte(out, 0x89);
        word32(out, reg_offset(0)); /* Dh==Dl retains the high word. */
        byte(out, 0x48);
        test(out, 2);
        flags(out, 0);
        instruction_end(out, pc, length);
        byte(out, 0xC3);
        patch(out, narrow, out->size);
        byte(out, 0x89);
        byte(out, 0xC2); /* Unsigned expected product = zero-extended low. */
        byte(out, 0x41);
        byte(out, 0xF7);
        byte(out, 0xC3);
        word32(out, 0x800);
        unsigned unsigned_overflow = jump(out, 0x84);
        byte(out, 0x48);
        byte(out, 0x63);
        byte(out, 0xD0); /* Signed expected product = sign-extended low. */
        patch(out, unsigned_overflow, out->size);
        byte(out, 0x48);
        byte(out, 0x39);
        byte(out, 0xD0);
        flag(out, 0x95, offsetof(CPUState, ccv));
        test(out, 2);
        flag(out, 0x95, offsetof(CPUState, ccnz));
        flag(out, 0x98, offsetof(CPUState, ccn));
        flag(out, 0x92, offsetof(CPUState, ccc));
        instruction_end(out, pc, length);
        return 1;
    }
    if ((op & 0xF140) == 0x4100 && source_supported(op & 63)) {
        unsigned width = op & 0x80 ? 1 : 2;
        unsigned length = data_source(out, op & 63, width, pc, 2);
        load(out, (op >> 9) & 7, 0);
        if (width == 1) {
            byte(out, 0x0F);
            byte(out, 0xBF);
            byte(out, 0xC0);
            byte(out, 0x0F);
            byte(out, 0xBF);
            byte(out, 0xD2);
        }
        test(out, 2);
        unsigned negative = jump(out, 0x88);
        byte(out, 0x39);
        byte(out, 0xD0);
        unsigned above = jump(out, 0x8F);
        byte(out, 0xC7);
        byte(out, 0x81);
        word32(out, offsetof(CPUState, ccn));
        word32(out, 0);
        instruction_end(out, pc, length);
        byte(out, 0xC3);
        patch(out, negative, out->size);
        patch(out, above, out->size);
        undo_source_update(out, op & 63, 1U << width);
        decoder_tail(out, pc, 4);
        return 1;
    }
    if ((op & 0xF0C0) == 0x80C0 && source_supported(op & 63)) {
        /* Widen the complete division: INT32_MIN/-1 is a guest quotient
         overflow, not a host IDIV exception. Capture aliased inputs first. */
        int sign = !!(op & 0x100);
        unsigned length = data_source(out, op & 63, 1, pc, 2);
        byte(out, sign ? 0x4C : 0x44);
        byte(out, 0x0F);
        byte(out, sign ? 0xBF : 0xB7);
        byte(out, 0xC2); /* MOVSX R8,DX / MOVZX R8D,DX */
        byte(out, 0x45);
        byte(out, 0x85);
        byte(out, 0xC0);
        unsigned zero = jump(out, 0x84);
        load(out, (op >> 9) & 7, 0);
        if (sign) {
            byte(out, 0x48);
            byte(out, 0x63);
            byte(out, 0xC0);
            byte(out, 0x48);
            byte(out, 0x99); /* MOVSXD RAX,EAX; CQO */
        }
        else {
            byte(out, 0x31);
            byte(out, 0xD2);
        }
        byte(out, 0x49);
        byte(out, 0xF7);
        byte(out, sign ? 0xF8 : 0xF0); /* IDIV/DIV R8 */
        byte(out, 0x41);
        byte(out, 0x89);
        byte(out, 0xD1);
        byte(out, 0x41);
        byte(out, 0xC1);
        byte(out, 0xE1);
        byte(out, 16); /* R9D retains the packed remainder across SETcc. */
        byte(out, 0x48);
        byte(out, 0x3D);
        word32(out, sign ? 32767 : 65535);
        byte(out, 0x0F);
        byte(out, sign ? 0x9F : 0x97);
        byte(out, 0xC2);
        byte(out, 0x0F);
        byte(out, 0xB6);
        byte(out, 0xD2);
        byte(out, 0x41);
        byte(out, 0x89);
        byte(out, 0xD2); /* R10D = overflow. */
        if (sign) {
            byte(out, 0x48);
            byte(out, 0x3D);
            word32(out, (uint32)-32768);
            byte(out, 0x0F);
            byte(out, 0x9C);
            byte(out, 0xC2);
            byte(out, 0x0F);
            byte(out, 0xB6);
            byte(out, 0xD2);
            byte(out, 0x41);
            byte(out, 0x09);
            byte(out, 0xD2);
        }
        byte(out, 0x44);
        byte(out, 0x89);
        byte(out, 0x91);
        word32(out, offsetof(CPUState, ccv));
        byte(out, 0xC7);
        byte(out, 0x81);
        word32(out, offsetof(CPUState, ccc));
        word32(out, 0);
        test(out, 2);
        flag(out, 0x95, offsetof(CPUState, ccnz));
        if (!sign)
            test(out, 1);
        flag(out, 0x98, offsetof(CPUState, ccn));
        byte(out, 0x45);
        byte(out, 0x85);
        byte(out, 0xD2);
        unsigned overflow = jump(out, 0x85);
        byte(out, 0x25);
        word32(out, 65535);
        byte(out, 0x44);
        byte(out, 0x09);
        byte(out, 0xC8);
        store(out, (op >> 9) & 7);
        patch(out, overflow, out->size);
        instruction_end(out, pc, length);
        byte(out, 0xC3);
        patch(out, zero, out->size);
        undo_source_update(out, op & 63, 2);
        decoder_tail(out, pc, 4);
        return 1;
    }
    if ((op & 0xF0C0) == 0xC0C0 && source_supported(op & 63)) {
        /* A word multiply cannot overflow its 32-bit result. X is unchanged. */
        load(out, (op >> 9) & 7, 0);
        unsigned length = data_source(out, op & 63, 1, pc, 2);
        byte(out, 0x0F);
        byte(out, op & 256 ? 0xBF : 0xB7);
        byte(out, 0xC0); /* MOVSX/MOVZX EAX,AX */
        byte(out, 0x0F);
        byte(out, op & 256 ? 0xBF : 0xB7);
        byte(out, 0xD2); /* MOVSX/MOVZX EDX,DX */
        byte(out, 0x0F);
        byte(out, 0xAF);
        byte(out, 0xC2); /* IMUL EAX,EDX */
        store(out, (op >> 9) & 7);
        test(out, 2);
        flags(out, 0);
        instruction_end(out, pc, length);
        return 1;
    }
    unsigned words = syn68k_x64_native_memory_words(op);
    if (words) {
        memory_integer(out, op, pc);
        instruction_end(out, pc, terminal_length(op, words));
        return 1;
    }
    if (instruction(out, source, length, pc)) {
        instruction_end(out, pc, 2 * length);
        return 1;
    }
    if (paired_operands(out, op, pc))
        return 1;
    int checked = (op & 0xF000) == 0xF000 || (op & 0xF000) == 0xA000 || (op & 0xF0C0) == 0xC0C0 ||
                  (op & 0xF0C0) == 0x80C0 || (op & 0xFFC0) == 0x4C00 || (op & 0xFFC0) == 0x4C40 ||
                  (op & 0xF140) == 0x4100 || ((op & 0xF9C0) == 0x00C0 && (op & 0x600) != 0x600) ||
                  (op & 0xF1C0) == 0x41C0 || ((op & 0xFFC0) == 0x4840 && (op & 0x30)) ||
                  ((op & 0xFB80) == 0x4880 && (op & 0x38) >= 0x10) || (op & 0xF138) == 0x0108 ||
                  (op & 0xFFF8) == 0x4E50 || (op & 0xFFF8) == 0x4808 || (op & 0xFFF8) == 0x4E58 || op == 0x4E70 ||
                  ((op & 0xFF00) == 0x0E00 && (op & 0xC0) != 0xC0) || (op & 0xFFF0) == 0x4E60 ||
                  (op & 0xFFC0) == 0x40C0 || (op & 0xFFC0) == 0x42C0 || (op & 0xFFC0) == 0x44C0 ||
                  (op & 0xFFC0) == 0x46C0 || op == 0x007C || op == 0x027C || op == 0x0A7C;
    if ((op & 0xF000) != 0x6000 && (op & 0xF0F8) != 0x50C8 && (op & 0xFF80) != 0x4E80 && op != 0x4E75 && op != 0x4E74 &&
        op != 0x4E77 && !syn68k_x64_memory_opcode(op) && !checked)
        return 0;
    /* Extended branches and control EAs deliberately have one-word translation
     metadata. Their extensions and stack operands must be fetched live, with
     MMU/access faults preceding any counter/stack effects. Tail-call the same
     checked decoder as the interpreter, using the caller's Win64 shadow space.
     No generated frame remains if that decoder invokes a guest callback. */
    decoder_tail(out, pc, (op >> 12) == 15 ? 11 : (op >> 12) == 10 ? 10 : 4);
    return 1;
}

#include "jit-cache.h"

void* syn68k_x64_compile(const uint16* source, const int8* lengths, unsigned instructions, unsigned* translated,
                         uint32 pc, int* terminal)
{
    emitter out = {.cached_address_register = -1, .cc_mask = ALL_CCS};
    uint8 faults[64];
    const uint16* start_source = source;
    uint32 start_pc = pc;
    *translated = 0;
    *terminal = 0;
    cache_key key;
    char path[4096];
    int persistent = cache_path(&key, path, source, lengths, instructions, pc);
    if (persistent && cache_read(path, &key, source, &out, translated, terminal)) {
        void* code = publish_code(&out);
        if (!code) {
            *translated = 0;
            *terminal = 0;
        }
        else
            ++cache_hits;
        return code;
    }
    ++cache_emissions;
    /* Control transfers terminate native blocks. Live extension/stack accesses
     use the checked decoder; other unsupported instructions still fall back.
     Bound native execution between IRQ polls. */
    while (*translated < instructions && *translated < 64) {
        unsigned length = lengths[*translated];
        unsigned guards = out.guards;
        int last = *translated + 1 == instructions;
        if (!length || out.size + 2048 >= sizeof out.bytes)
            break;
        /* Some checked instructions can occur inside a synthetic block, but
         must finish the native prefix. Retry a failed linear emission as a
         terminal body. Roll back every emitter field, not just its bytes. */
        unsigned char checkpoint[sizeof(emitter) - offsetof(emitter, size)];
        memcpy(checkpoint, &out.size, sizeof checkpoint);
        int emitted = last ? branch(&out, source, length, pc) : instruction(&out, source, length, pc);
        if (!emitted && !last) {
            memcpy(&out.size, checkpoint, sizeof checkpoint);
            last = 1;
            emitted = branch(&out, source, length, pc);
        }
        if (!emitted) {
            memcpy(&out.size, checkpoint, sizeof checkpoint);
            break;
        }
        faults[*translated] = out.guards != guards;
        source += length;
        pc += length * 2;
        ++*translated;
        *terminal = last;
        if (last)
            break;
    }
    if (!*translated)
        return NULL;
    /* Determine the prefix with full flag stores before optimizing it. Its
     actual exit requires every CCR bit, including at bounded partial exits.
     A guard can leave early, so only instructions that actually emit a guard
     or checked tail demand all incoming flags. Reused RAM guards cannot fault. */
    uint8 masks[64];
    syn68k_x64_flag_liveness(start_source, lengths, *translated, faults, masks);
    emitter optimized = {.cached_address_register = -1, .cc_mask = ALL_CCS};
    source = start_source;
    uint32 optimized_pc = start_pc;
    int complete = 1;
    for (unsigned i = 0; i < *translated; ++i) {
        optimized.cc_mask = masks[i];
        int last = i + 1 == *translated && *terminal;
        if (!(last ? branch(&optimized, source, lengths[i], optimized_pc)
                   : instruction(&optimized, source, lengths[i], optimized_pc))) {
            complete = 0;
            break;
        }
        source += lengths[i];
        optimized_pc += lengths[i] * 2;
    }
    if (complete && syn68k_x64_optimize_flags)
        out = optimized;
    if (out.dynamic && !*terminal) {
        /* A guard may have returned a different next PC through the decoder.
         Use dynamic dispatch for both guard failures and successful prefixes. */
        byte(&out, 0xB8);
        word32(&out, pc);
        *terminal = 1;
    }
    byte(&out, 0xC3);
    void* code = publish_code(&out);
    if (!code) {
        *translated = 0;
        *terminal = 0;
        return NULL;
    }
    if (persistent)
        cache_write(path, &key, start_source, &out, *translated, *terminal);
    return code;
}

typedef struct {
    uint32 pc;
    unsigned opcode;
    void* code;
} MappedCode;

static MappedCode mapped_codes[16];

static void* compile_mapped(uint32 pc, unsigned opcode)
{
    emitter out = {.cached_address_register = -1, .cc_mask = ALL_CCS};
    const unsigned char bytes[] = {opcode >> 8, opcode};
    uint16 source;
    memcpy(&source, bytes, sizeof source);
    const int8 length = 1;
    cache_key key;
    char path[4096];
    unsigned translated = 0;
    int terminal = 0;
    int persistent = cache_path_mode(&key, path, &source, &length, 1, pc, 1);
    if (persistent && cache_read(path, &key, &source, &out, &translated, &terminal) && out.managed == 1 &&
        translated == 1 && terminal == 1) {
        void* code = publish_code(&out);
        if (code)
            ++cache_hits;
        return code;
    }
    out = (emitter){.cached_address_register = -1, .cc_mask = ALL_CCS};
    ++cache_emissions;
    int emitted = mapped_mmu_opcode(opcode)           ? mapped_mmu(&out, opcode, pc)
                  : mapped_frame_opcode(opcode)       ? fpu_mapped_frame(&out, opcode, pc)
                  : mapped_conditional_opcode(opcode) ? fpu_mapped_conditional(&out, opcode, pc)
                                                      : fpu_register_mode(&out, opcode, pc, 1);
    if (emitted != 1)
        return NULL;
    byte(&out, 0xc3);
    void* code = publish_code(&out);
    if (code && persistent)
        cache_write(path, &key, &source, &out, 1, 1);
    return code;
}

static void* mapped_code(uint32 pc, unsigned opcode)
{
    if (!fpu_roots[NATIVE040_READ])
        (void)relocation_address(RELOC_FPU_ROOTS);
    unsigned slot = ((pc >> 1) ^ opcode) % (sizeof mapped_codes / sizeof *mapped_codes);
    MappedCode* entry = &mapped_codes[slot];
    if (entry->code && (entry->pc != pc || entry->opcode != opcode)) {
        syn68k_x64_release(entry->code);
        entry->code = NULL;
    }
    if (!entry->code) {
        void* code = compile_mapped(pc, opcode);
        if (!code)
            return 0;
        *entry = (MappedCode){.pc = pc, .opcode = opcode, .code = code};
    }
    return entry->code;
}

static int run_fpu_packet(uint32 pc, unsigned opcode, Syn68kMMU040Memory* memory, Syn68kFPUStep* result)
{
    if ((pc & 1) || ((opcode & ~255u) != 0xf200 && !mapped_frame_opcode(opcode)) || !memory || !memory->mmu || !result)
        return 0;
    int flat = memory->native && memory->native->flat;
    Syn68kMMU040Memory* checked_memory = flat ? NULL : memory;
    void* code = mapped_code(pc, opcode);
    if (!code)
        return 0;
    FPUInvocation packet = {.memory = {.memory = memory},
                            .registers = {.cpu = &cpu_state, .memory = memory},
                            .ea = {.cpu = &cpu_state, .context = {.pc = pc + 4, .memory = memory}},
                            .next = pc + 4,
                            .original_null = cpu_state.fpu.null_state,
                            .restore = {.cpu = &cpu_state},
                            .entry = {cpu_state.fpu.fpcr, cpu_state.fpu.null_state, cpu_state.fpu.frame_size == 0}};
    if (memory->native)
        packet.registers.ram = packet.ea.ram = packet.memory.ram = memory->native->ram;
    ++syn68k_x64_executions;
    uint32 next = execute_managed_packet(code, &packet);
    unsigned trace_flow = mapped_frame_opcode(opcode) || (opcode & 0xfff8) == 0xf248 ||
                          ((opcode & ~63u) == 0xf200 && packet.command >> 13 >= 4) || packet.exception.trace_flow;
    if (packet.exit == FPU_EXIT_UNFETCHED)
        return syn68k_fpu_step_decoded(pc, opcode, checked_memory, result) ? 1 : -1;
    if (packet.exit == FPU_EXIT_COMMAND)
        return syn68k_fpu_finish_command_at(pc, opcode, packet.command, checked_memory, &packet.entry, result) ? 1 : -1;
    if (packet.exit == FPU_EXIT_FRAME) {
        Syn68kFPUFrame captured = {.pc = pc,
                                   .next = packet.next,
                                   .ea = packet.ea.result.address,
                                   .header = packet.restore.size ? packet.frame_header : packet.memory.value,
                                   .image = packet.restore.size ? packet.restore.image : NULL,
                                   .image_size = packet.restore.size,
                                   .opcode = opcode,
                                   .entry = packet.entry};
        return syn68k_fpu_finish_frame(&captured, checked_memory, result) ? 1 : -1;
    }
    if (packet.exit == FPU_EXIT_EXCEPTION) {
        *result = packet.exception;
        result->trace_flow = trace_flow;
        return 1;
    }
    if (packet.exit == FPU_EXIT_EA) {
        unsigned error = packet.ea.context.error;
        if (error != 2 && error != 4 && error != 11)
            return -1;
        if (error != 2)
            cpu_state.fpu.null_state = packet.original_null;
        *result = (Syn68kFPUStep){.next = pc,
                                  .exception_pc = pc,
                                  .vector = error,
                                  .format = error == 2 ? 7 : 0,
                                  .fault_address = packet.ea.context.fault_address,
                                  .trace_flow = trace_flow,
                                  .access = flat ? (Syn68kMMU040Access){0} : packet.ea.context.access};
        return 1;
    }
    if (packet.exit == FPU_EXIT_CAPTURED || packet.exit == FPU_EXIT_DESTINATION) {
        Syn68kFPUCaptured captured = {.pc = pc,
                                      .next = packet.next,
                                      .ea = packet.ea.result.instruction ? 0 : packet.ea.result.address,
                                      .opcode = opcode,
                                      .command = packet.command,
                                      .original_null = packet.original_null,
                                      .source = packet.arithmetic.source,
                                      .unsupported = packet.arithmetic.unsupported,
                                      .signaling = packet.arithmetic.signaling,
                                      .source_tag = packet.arithmetic.source_tag,
                                      .access = packet.memory.access,
                                      .has_entry = 1,
                                      .entry = packet.entry};
        if (packet.exit == FPU_EXIT_DESTINATION)
            return syn68k_fpu_finish_destination(&captured, checked_memory, result) ? 1 : -1;
        return syn68k_fpu_finish_captured(&captured, result) ? 1 : -1;
    }
    if (packet.exit == FPU_EXIT_REGISTERS) {
        Syn68kMMU040Access access = packet.registers.access;
        if (flat && access.translation.fault != MMU040_OK)
            return -1;
        if (!access.bus_error && access.translation.fault == MMU040_OK)
            return -1;
        *result = (Syn68kFPUStep){.next = pc,
                                  .exception_pc = pc,
                                  .vector = 2,
                                  .format = 7,
                                  .fault_address = access.logical,
                                  .trace_flow = trace_flow,
                                  .access = flat ? (Syn68kMMU040Access){0} : access};
        if (!flat && packet.ea.writing && packet.ea.result.reg < 0 && packet.ea.size >= 12 && access.size == 4 &&
            access.request == packet.ea.result.address + packet.ea.size - 4)
            syn68k_fpu_final_store_fault(result, packet.next);
        else if (!flat && packet.ea.writing && packet.ea.result.reg < 0 && packet.ea.size >= 12 && access.size == 4 &&
                 access.request == packet.ea.result.address + packet.ea.size - 8 &&
                 packet.registers.store_tail_valid == 1) {
            syn68k_fpu_final_store_fault(result, packet.next);
            if (result->store_state) {
                result->store_tail = packet.registers.store_tail;
                result->store_tail_valid = 1;
                result->store_tail_fc = (cpu_state.sr >> 11 & 4) | 1;
            }
        }
        if (!flat && packet.command >> 13 == 7 && packet.ea.writing && packet.ea.result.reg < 0) {
            syn68k_fpu_replay_store_fault(result);
            if (result->store_state == 2 && (opcode >> 3 & 7) == 4)
                result->store_ea_size = packet.ea.size;
        }
        return 1;
    }
    if (packet.exit == FPU_EXIT_ACCESS) {
        if (flat && packet.memory.access.translation.fault != MMU040_OK)
            return -1;
        if (!packet.memory.access.bus_error && packet.memory.access.translation.fault == MMU040_OK)
            return -1;
        *result = (Syn68kFPUStep){.next = pc,
                                  .exception_pc = pc,
                                  .vector = 2,
                                  .format = 7,
                                  .fault_address = packet.memory.access.logical,
                                  .trace_flow = trace_flow,
                                  .access = flat ? (Syn68kMMU040Access){0} : packet.memory.access};
        if (!flat && (opcode & ~63u) == 0xf240 && packet.ea.result.reg < 0 && packet.ea.size == 1 &&
            packet.memory.address == packet.ea.result.address)
            syn68k_fpu_final_store_fault(result, packet.next);
        if (!flat && (opcode & ~63u) == 0xf300 && packet.ea.result.reg < 0) {
            unsigned size = packet.operation ? packet.ea.size : 4;
            uint32 offset = packet.memory.address - packet.ea.result.address;
            if (offset == size - 4 || (packet.operation && offset == size - 8)) {
                syn68k_fpu_final_store_fault(result, packet.next);
                if (result->store_state && packet.operation) {
                    result->store_frame_clear = 1;
                    if (offset == size - 8) {
                        const uint8* tail = cpu_state.fpu.frame + packet.displacement + size - 4;
                        result->store_tail_valid = 1;
                        result->store_tail =
                            (uint32)tail[0] << 24 | (uint32)tail[1] << 16 | (uint32)tail[2] << 8 | tail[3];
                        result->store_tail_fc = (cpu_state.sr >> 11 & 4) | 1;
                    }
                }
            }
            if (packet.operation) {
                syn68k_fpu_replay_store_fault(result);
                if (result->store_state == 2 && (opcode >> 3 & 7) == 4)
                    result->store_ea_size = size;
            }
        }
        if (!flat && packet.command >> 13 == 5 && packet.operation < 3 && packet.ea.result.reg < 0 &&
            packet.memory.address == packet.ea.result.address) {
            unsigned mask = packet.command >> 10 & 7, selected = 4u >> packet.operation;
            unsigned remaining = mask & (selected - 1);
            if ((mask & selected) && !remaining)
                syn68k_fpu_final_store_fault(result, packet.next);
            else if ((mask & selected) && !(remaining & (remaining - 1))) {
                syn68k_fpu_final_store_fault(result, packet.next);
                if (result->store_state) {
                    result->store_tail_valid = 1;
                    result->store_tail = remaining & 2 ? cpu_state.fpu.fpsr : cpu_state.fpu.fpiar;
                    result->store_tail_fc = (cpu_state.sr >> 11 & 4) | 1;
                }
            }
            else if (mask == 7 && selected == 4) {
                syn68k_fpu_replay_store_fault(result);
                unsigned addressing = opcode >> 3 & 7;
                if (result->store_state == 2 && (addressing == 3 || addressing == 4))
                    result->store_ea_size = 12;
            }
        }
        if (!flat && packet.command >> 13 == 3 && packet.ea.result.reg < 0 && packet.ea.size >= packet.memory.size) {
            uint32 last = packet.ea.result.address + packet.ea.size - packet.memory.size;
            if (packet.memory.address == last)
                syn68k_fpu_final_store_fault(result, packet.next);
            else if ((packet.ea.size == 8 || packet.ea.size == 12) && packet.memory.size == 4 &&
                     packet.memory.address == last - 4) {
                syn68k_fpu_final_store_fault(result, packet.next);
                if (result->store_state) {
                    result->store_tail_valid = 1;
                    result->store_tail = packet.ea.size == 8 ? packet.arithmetic.bits : packet.arithmetic.source.low;
                    result->store_tail_fc = (cpu_state.sr >> 11 & 4) | 1;
                }
            }
            else if ((packet.command >> 10 & 7) == 2 && packet.ea.size == 12 && packet.memory.size == 2 &&
                     (uint32)(packet.memory.address - packet.ea.result.address) < 4) {
                syn68k_fpu_replay_store_fault(result);
                if (result->store_state == 2) {
                    unsigned mode = opcode >> 3 & 7;
                    result->store_state = 3;
                    result->store_ea_size = mode == 3 || mode == 4 ? 12 : 0;
                    result->replay_fpsr = packet.exception.replay_fpsr;
                }
            }
        }
        return 1;
    }
    if (packet.exit != FPU_EXIT_DONE)
        return -1;
    *result =
        (Syn68kFPUStep){.next = next, .trace_flow = trace_flow, .prefetch_address = packet.exception.prefetch_address};
    return 1;
}

int syn68k_x64_mapped_fpu(uint32 pc, unsigned opcode, Syn68kMMU040Memory* memory, Syn68kFPUStep* result)
{
    return run_fpu_packet(pc, opcode, memory, result);
}

static int flat_native_memory(Syn68kMMU040Memory* memory, Syn68kMMU040Native* native)
{
    if (!syn68k_native040_memory(memory, native, (Syn68kNative040RAM){0}))
        return 0;
    native->flat = syn68k_native040_flat_state();
    native->flat_code_pages = syn68k_x64_code_pages;
    native->flat_page_shift = SYN68K_X64_PAGE_SHIFT;
    native->flat_write = syn68k_invalidate_checked_write;
    return 1;
}

int syn68k_x64_flat_fpu(uint32 pc, unsigned opcode, Syn68kFPUStep* result)
{
    if ((pc & 1) || ((opcode & ~255u) != 0xf200 && !mapped_frame_opcode(opcode)) || !result ||
        !syn68k_memory_span(pc, 2))
        return 0;
    Syn68kMMU040 mmu = {0};
    Syn68kMMU040Memory memory = {.mmu = &mmu};
    Syn68kMMU040Native native = {0};
    if (!flat_native_memory(&memory, &native))
        return 0;
    return run_fpu_packet(pc, opcode, &memory, result);
}

static uint32 flat_control_run(uint32 pc, unsigned opcode)
{
    Syn68kMMU040 mmu = {0};
    Syn68kMMU040Memory memory = {.mmu = &mmu};
    Syn68kMMU040Native native = {0};
    Syn68kIntegerStep step;
    int handled = 0;
    if (syn68k_memory_span(pc, 2) && flat_native_memory(&memory, &native))
        handled = syn68k_x64_mapped_mmu(pc, opcode, &memory, &step);
    if (!handled)
        return trap_direct(4, pc, 0);
    if (handled < 0)
        return trap_exception(4, pc, 0, 0);
    return step.vector ? trap_exception(step.vector, step.exception_pc, step.fault_address, step.format) : step.next;
}

static uint32 flat_native_run(uint32 pc, unsigned opcode)
{
    if (opcode == 0x4e73 || (opcode & 0xfffe) == 0x4e7a)
        return flat_control_run(pc, opcode);
    uint16 entry_sr = cpu_state.sr;
    Syn68kFPUStep step;
    int handled = syn68k_x64_flat_fpu(pc, opcode, &step);
    if (!handled && !syn68k_fpu_step_decoded(pc, opcode, NULL, &step))
        return trap_direct(11, pc, 0);
    if (handled < 0)
        return trap_exception(4, pc, 0, 0); // Never replay a consumed native operation.
    syn68k_fpu_trace_result(&step, entry_sr, pc);
    return step.vector ? trap_exception(step.vector, step.exception_pc, step.fault_address, step.format) : step.next;
}

int syn68k_x64_mapped_mmu(uint32 pc, unsigned opcode, Syn68kMMU040Memory* memory, Syn68kIntegerStep* result)
{
    if ((pc & 1) || !mapped_mmu_opcode(opcode) || !memory || !memory->mmu || !result)
        return 0;
    if ((opcode & 0xff00) == 0xf400 && (opcode & 0x18) && (cpu_state.sr & SR_SUPERVISOR_BIT) && !memory->cache)
        return 0;
    void* code = mapped_code(pc, opcode);
    if (!code)
        return 0;
    int flat = memory->native && memory->native->flat;
    // Flat contexts can fetch commands but have no connected guest MMU registers.
    FPUInvocation packet = {.memory = {.memory = memory}, .control = {.cpu = &cpu_state, .memory = flat ? NULL : memory}};
    if (memory->native)
        packet.memory.ram = memory->native->ram;
    packet.rte = (Syn68kNative040RTE){.cpu = &cpu_state, .memory = &packet.memory, .pc = pc};
    ++syn68k_x64_executions;
    uint32 next = execute_managed_packet(code, &packet);
    if (packet.exit == NATIVE_EXIT_RTE) {
        *result = packet.rte.step;
        return 1;
    }
    if (packet.exit == FPU_EXIT_UNFETCHED) {
        if (flat)
            return 0;
        return syn68k_integer_step_decoded(pc, opcode, memory, result) ? 1 : -1;
    }
    if ((opcode & 0xff00) == 0xf400 && packet.memory.access.cache.interrupt_level) {
        *result = (Syn68kIntegerStep){
            .next = pc, .interrupt_level = packet.memory.access.cache.interrupt_level, .access = packet.memory.access};
        return 1;
    }
    if (packet.exit == FPU_EXIT_ACCESS) {
        *result = (Syn68kIntegerStep){.next = pc,
                                      .exception_pc = pc,
                                      .vector = 2,
                                      .format = 7,
                                      .fault_address = packet.memory.access.logical,
                                      .access = packet.memory.access};
        return 1;
    }
    if (packet.exit == NATIVE_EXIT_PRIVILEGE || packet.exit == NATIVE_EXIT_ILLEGAL) {
        *result = (Syn68kIntegerStep){.next = pc,
                                      .exception_pc = pc,
                                      .vector = packet.exit == NATIVE_EXIT_PRIVILEGE ? 8 : 4,
                                      .access = packet.memory.access};
        return 1;
    }
    if (packet.exit != FPU_EXIT_DONE)
        return -1; // The command was consumed: never replay it after an invalid native outcome.
    *result = (Syn68kIntegerStep){.next = next, .stopped = opcode == 0x4e72};
    return 1;
}

void syn68k_x64_mapped_clear(void)
{
    for (unsigned i = 0; i < sizeof mapped_codes / sizeof *mapped_codes; ++i) {
        syn68k_x64_release(mapped_codes[i].code);
        mapped_codes[i] = (MappedCode){0};
    }
}

void syn68k_x64_release(void* code)
{
    if (!code)
        return;
    /* Retire on the oldest active instance: recursive entry into the same
       allocation must keep every younger return address mapped as well. */
    NativeInvocation* oldest = NULL;
    for (NativeInvocation* current = syn68k_x64_managed_context; current; current = current->previous)
        if (current->code == code)
            oldest = current;
    if (oldest)
        oldest->retired = 1;
    else
        VirtualFree((unsigned char*)code - sizeof(Syn68kX64CodeHeader), 0, 0x8000);
}

#endif

void* syn68k_native_checkpoint(void)
{
#ifdef GENERATE_X64_CODE
    return syn68k_x64_managed_context;
#else
    return NULL;
#endif
}

int syn68k_native_unwind(void* checkpoint)
{
#ifdef GENERATE_X64_CODE
    NativeInvocation* current = syn68k_x64_managed_context;
    while (current && current != checkpoint)
        current = current->previous;
    if (current != checkpoint)
        return 0;
    while (syn68k_x64_managed_context != checkpoint)
        leave_native(syn68k_x64_managed_context);
    return 1;
#else
    return checkpoint == NULL;
#endif
}

uint64 syn68k_native_execution_count(void)
{
#ifdef GENERATE_X64_CODE
    return syn68k_x64_executions;
#else
    return 0;
#endif
}

uint64 syn68k_aot_hit_count(void)
{
#ifdef GENERATE_X64_CODE
    return cache_hits;
#else
    return 0;
#endif
}

uint64 syn68k_native_compile_count(void)
{
#ifdef GENERATE_X64_CODE
    return cache_emissions;
#else
    return 0;
#endif
}
