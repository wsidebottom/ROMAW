/*+===================================================================
  File:      native040-code.c

  Summary:   Publish verified closed 68040 arithmetic and MMU code with writable-to-executable protection.

  Classes:   None.

  Functions: syn68k_native040_entry, syn68k_native040_memory

  Origin:    Original ARDI Executor direct 68040 native-code publication.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "native040.h"
#include <stddef.h>
#include <string.h>

#if defined(__x86_64__) && (defined(_WIN32) || defined(__unix__))
typedef struct {
    unsigned offset, target;
} Syn68kNative040Relocation;

#include "generated/native040.h"
#if defined(_WIN32)
__declspec(dllimport) void* __stdcall VirtualAlloc(void*, size_t, unsigned long, unsigned long);
__declspec(dllimport) int __stdcall VirtualProtect(void*, size_t, unsigned long, unsigned long*);
__declspec(dllimport) int __stdcall VirtualFree(void*, size_t, unsigned long);
__declspec(dllimport) void* __stdcall GetCurrentProcess(void);
__declspec(dllimport) int __stdcall FlushInstructionCache(void*, const void*, size_t);
#else
#include <sys/mman.h>
#endif

static void* native_code;

static int publish(void)
{
    const size_t size = sizeof native040_code;
    for (unsigned i = 0; i < native040_relocation_count; ++i) {
        Syn68kNative040Relocation r = native040_relocations[i];
        if (r.offset > size - sizeof(uintptr_t) || r.target >= size ||
            (i && r.offset < native040_relocations[i - 1].offset + sizeof(uintptr_t)))
            return 0;
    }
#if defined(_WIN32)
    unsigned char* code = VirtualAlloc(NULL, size, 0x3000, 4);
    if (!code)
        return 0;
#else
    unsigned char* code = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED)
        return 0;
#endif
    memcpy(code, native040_code, size);
    for (unsigned i = 0; i < native040_relocation_count; ++i) {
        Syn68kNative040Relocation r = native040_relocations[i];
        uintptr_t value = (uintptr_t)code + r.target;
        memcpy(code + r.offset, &value, sizeof value);
    }
#if defined(_WIN32)
    unsigned long previous;
    if (!VirtualProtect(code, size, 0x20, &previous) || !FlushInstructionCache(GetCurrentProcess(), code, size)) {
        VirtualFree(code, 0, 0x8000);
        return 0;
    }
#else
    if (mprotect(code, size, PROT_READ | PROT_EXEC)) {
        munmap(code, size);
        return 0;
    }
    __builtin___clear_cache((char*)code, (char*)code + size);
#endif
    native_code = code;
    return 1;
}
#endif

Syn68kNative040Entry syn68k_native040_entry(enum Syn68kNative040Operation operation)
{
#if defined(__x86_64__) && (defined(_WIN32) || defined(__unix__))
    const unsigned offsets[] = {native040_fadd_offset,
                                native040_fsub_offset,
                                native040_fmul_offset,
                                native040_fdiv_offset,
                                native040_fsqrt_offset,
                                native040_fmove_offset,
                                native040_fabs_offset,
                                native040_fneg_offset,
                                native040_fsgldiv_offset,
                                native040_fsglmul_offset,
                                native040_fcmp_offset,
                                native040_ftst_offset,
                                native040_read_offset,
                                native040_write_offset,
                                native040_write_rmw_offset,
                                native040_translate_offset,
                                native040_ptest_offset,
                                native040_flush_offset,
                                native040_control_offset,
                                native040_reset_offset,
                                native040_ea_offset,
                                native040_from_i32_offset,
                                native040_from_f32_offset,
                                native040_from_f64_offset,
                                native040_to_i32_offset,
                                native040_to_i16_offset,
                                native040_to_i8_offset,
                                native040_to_f32_offset,
                                native040_to_f64_offset,
                                native040_bus_read_offset,
                                native040_bus_write_offset,
                                native040_bus_write_rmw_offset,
                                native040_movec_read_offset,
                                native040_movec_write_offset,
                                native040_load_fprs_offset,
                                native040_store_fprs_offset,
                                native040_flat_read_offset,
                                native040_flat_write_offset,
                                native040_flat_span_offset,
                                native040_span_offset,
                                native040_stop_offset,
                                native040_cache_access_offset,
                                native040_cache_maintain_offset,
                                native040_memory_cache_maintain_offset,
                                native040_bus_move16_offset,
                                native040_cache_snoop_offset,
                                native040_bus_snoop_offset,
                                native040_rte_offset,
                                  native040_restore_writeback_offset,
                                  native040_movem_offset};
    _Static_assert(sizeof offsets / sizeof *offsets == NATIVE040_COUNT, "native root table");
    if ((unsigned)operation >= NATIVE040_COUNT || offsets[operation] >= sizeof native040_code)
        return NULL;
    if (!native_code && !publish())
        return NULL;
    return (Syn68kNative040Entry)((unsigned char*)native_code + offsets[operation]);
#else
    (void)operation;
    return NULL;
#endif
}

int syn68k_native040_memory(Syn68kMMU040Memory* memory, Syn68kMMU040Native* storage, Syn68kNative040RAM ram)
{
    if (!memory || !memory->mmu || !storage)
        return 0;
    Syn68kNative040Entry read = syn68k_native040_entry(NATIVE040_BUS_READ);
    if (!read)
        return 0;
    Syn68kNative040Entry write = syn68k_native040_entry(NATIVE040_BUS_WRITE);
    Syn68kNative040Entry write_rmw = syn68k_native040_entry(NATIVE040_BUS_WRITE_RMW);
    Syn68kNative040Entry move16 = syn68k_native040_entry(NATIVE040_BUS_MOVE16);
    Syn68kNative040Entry snoop = syn68k_native040_entry(NATIVE040_BUS_SNOOP);
    if (!read || !write || !write_rmw || !move16 || !snoop)
        return 0;
    Syn68kMMU040Native entries = {
        .read =
            (Syn68kMMU040Access(*)(Syn68kMMU040Memory*, uint32_t, unsigned, unsigned, int, uint32_t*))(uintptr_t)read,
        .write = (Syn68kMMU040Access(*)(Syn68kMMU040Memory*, uint32_t, unsigned, unsigned, uint32_t))(uintptr_t)write,
        .write_rmw =
            (Syn68kMMU040Access(*)(Syn68kMMU040Memory*, uint32_t, unsigned, unsigned, uint32_t))(uintptr_t)write_rmw,
        .ram = ram,
        .move16 =
            (Syn68kMMU040Access(*)(Syn68kMMU040Memory*, uint32_t, unsigned, int, unsigned char[16]))(uintptr_t)move16,
        .snoop = (Syn68kCache040Snoop(*)(Syn68kMMU040Memory*, uint32_t, unsigned, unsigned, unsigned, int,
                                         unsigned char[16]))(uintptr_t)snoop};
    *storage = entries;
    memory->native = storage;
    return 1;
}
