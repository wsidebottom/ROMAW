/*+===================================================================
  File:      native040-publication-test.c

  Summary:   Check native arithmetic/MMU image fixups and rollback after Windows publication failures.

  Classes:   None.

  Functions: main, test_alloc, test_protect, test_flush, test_process, test_free; imports the publisher fixture.

  Origin:    Original ARDI Executor native 68040 publication regression.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#undef __declspec
#define __declspec(x)
#define VirtualAlloc test_alloc
#define VirtualProtect test_protect
#define VirtualFree test_free
#define FlushInstructionCache test_flush
#define GetCurrentProcess test_process
#include "../src/runtime/native040-code.c"

static unsigned failure, stage, freed;
static void* allocation;

void* __stdcall test_alloc(void* address, size_t size, unsigned long type, unsigned long protection)
{
    assert(!address && size == sizeof native040_code && type == 0x3000 && protection == 4 && ++stage == 1);
    return failure == 1 ? NULL : (allocation = malloc(size));
}

int __stdcall test_protect(void* address, size_t size, unsigned long protection, unsigned long* previous)
{
    assert(address == allocation && size == sizeof native040_code && protection == 0x20 && ++stage == 2);
    for (unsigned i = 0; i < native040_relocation_count; ++i) {
        uintptr_t pointer;
        memcpy(&pointer, (unsigned char*)address + native040_relocations[i].offset, sizeof pointer);
        assert(pointer == (uintptr_t)address + native040_relocations[i].target);
    }
    *previous = 4;
    return failure != 2;
}

void* __stdcall test_process(void)
{
    return &stage;
}

int __stdcall test_flush(void* process, const void* address, size_t size)
{
    assert(process == &stage && address == allocation && size == sizeof native040_code && ++stage == 3);
    return failure != 3;
}

int __stdcall test_free(void* address, size_t size, unsigned long type)
{
    assert(address == allocation && !size && type == 0x8000);
    ++freed;
    free(address);
    allocation = NULL;
    return 1;
}

int main(void)
{
    for (failure = 0; failure < 4; ++failure) {
        stage = freed = 0;
        Syn68kNative040Entry entry = syn68k_native040_entry(NATIVE040_FADD);
        assert(!!entry == !failure && stage == (failure == 1 ? 1U : failure == 2 ? 2U : 3U));
        assert(freed == (failure == 2 || failure == 3));
        if (!failure) {
            assert((unsigned char*)entry == (unsigned char*)native_code + native040_fadd_offset);
            for (unsigned root = 0; root < NATIVE040_COUNT; ++root)
                assert(syn68k_native040_entry(root));
            assert(!syn68k_native040_entry(NATIVE040_COUNT) && !syn68k_native040_entry(-1) && stage == 3);
            test_free(native_code, 0, 0x8000);
            native_code = NULL;
        }
        else {
            assert(!native_code);
            Syn68kMMU040 mmu = {0};
            Syn68kMMU040Memory memory = {.mmu = &mmu}, before = memory;
            Syn68kMMU040Native entries = {0}, saved = entries;
            stage = freed = 0;
            assert(!syn68k_native040_memory(&memory, &entries, (Syn68kNative040RAM){0}));
            assert(!memcmp(&before, &memory, sizeof memory) && !memcmp(&entries, &saved, sizeof entries));
            assert(stage == (failure == 1 ? 1U : failure == 2 ? 2U : 3U));
            assert(freed == (failure == 2 || failure == 3));
        }
    }
    puts("Native 68040 image fixups, all root entries and allocation/protection/flush rollback passed");
    return 0;
}
