/*+===================================================================
  File:      ppc-native-memory-code.cpp

  Summary:   Relocate and publish closed native 750 memory kernels with writable/executable separation.

  Classes:   Uses ppc_native_memory_kernel.

  Functions: ppc_native_memory_kernel constructor, destructor and get.

  Origin:    Original ARDI Executor native PowerPC memory lowering.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ppc-native-memory-code.h"
#include <cstring>
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#include "generated/ppc-native-memory.h"

ppc_native_memory_kernel::ppc_native_memory_kernel() noexcept
{
    size = sizeof native_memory_code;
    // The generator verifies sections, pointer targets and entry points. Check
    // the publication boundary too, before allocating or copying any code.
    for (unsigned i = 0; i < native_memory_relocation_count; ++i) {
        const auto& fixup = native_memory_relocations[i];
        if (fixup.offset > size || sizeof(uintptr_t) > size - fixup.offset || fixup.target >= size)
            return;
    }
#if defined(_WIN32)
    code = VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    code = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED)
        code = nullptr;
#endif
    if (!code)
        return;
    memcpy(code, native_memory_code, size);
    for (unsigned i = 0; i < native_memory_relocation_count; ++i) {
        const auto& fixup = native_memory_relocations[i];
        uintptr_t pointer = reinterpret_cast<uintptr_t>(code) + fixup.target;
        memcpy(static_cast<unsigned char*>(code) + fixup.offset, &pointer, sizeof pointer);
    }
#if defined(_WIN32)
    DWORD previous;
    if (VirtualProtect(code, size, PAGE_EXECUTE_READ, &previous) &&
        FlushInstructionCache(GetCurrentProcess(), code, size))
        return;
    VirtualFree(code, 0, MEM_RELEASE);
#else
    if (!mprotect(code, size, PROT_READ | PROT_EXEC)) {
        __builtin___clear_cache(static_cast<char*>(code), static_cast<char*>(code) + size);
        return;
    }
    munmap(code, size);
#endif
    code = nullptr;
}

ppc_native_memory_kernel::~ppc_native_memory_kernel()
{
    if (!code)
        return;
#if defined(_WIN32)
    VirtualFree(code, 0, MEM_RELEASE);
#else
    munmap(code, size);
#endif
}

ppc_native_memory_kernel::entry ppc_native_memory_kernel::get(operation op) const noexcept
{
    static const unsigned offsets[] = {
        native_memory_read_offset,        native_memory_write_offset,    native_memory_reserve_offset,
        native_memory_conditional_offset, native_memory_external_offset, native_memory_translate_offset,
        native_memory_cache_offset,       native_memory_sync_offset,     native_memory_snoop_offset,
        native_memory_msr_offset,         native_memory_spr_offset,
    };
    static_assert(sizeof offsets / sizeof offsets[0] == operation_count, "native memory root count");
    if (!code || unsigned(op) >= operation_count || offsets[op] >= size)
        return nullptr;
    return reinterpret_cast<entry>(static_cast<unsigned char*>(code) + offsets[op]);
}
