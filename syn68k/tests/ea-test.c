/*+===================================================================
  File:      ea-test.c

  Summary:   Implements ea test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include "ea.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>

int main(void)
{
    void* memory = VirtualAlloc(NULL, 16 * 1024 * 1024, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(1024 * 1024, 0, 0);
    Syn68kEAContext context = {.pc = 0};
    for (unsigned size = 1; size <= 4; size *= 2) {
        uint32 address = 0x100000 - size;
        syn68k_ea_write_memory(&context, address, size, 0x12345678);
        uint32 mask = size == 4 ? UINT32_MAX : (1u << (size * 8)) - 1;
        assert(!context.error);
        assert(syn68k_ea_read_memory(&context, address, size) == (0x12345678 & mask));
    }
    WRITEUW(0xffffe, 0x55aa);
    syn68k_ea_write_memory(&context, 0xffffe, 4, 0);
    assert(context.error == 2 && context.fault_address == 0x100000);
    assert(READUW(0xffffe) == 0x55aa);
    /* A failed context must never perform a subsequent read or write. */
    syn68k_ea_write_memory(&context, 0xffffe, 2, 0);
    assert(READUW(0xffffe) == 0x55aa);
    assert(!syn68k_ea_read_memory(&context, 0xffffffff, 4));
    assert(context.fault_address == 0x100000);
    EM_A0 = 0x2000;
    syn68k_effective_address(&context, 0x18, 2, 0, 1);
    assert(context.error == 2 && EM_A0 == 0x2000);
    /* Rejected immediate destinations must not consume extension bytes. */
    context = (Syn68kEAContext){.pc = 0xffffe};
    syn68k_effective_address(&context, 0x3c, 2, 1, 1);
    assert(context.error == 11 && context.pc == 0xffffe);
    const unsigned invalid[] = {0, 3, 5, UINT32_MAX};
    for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; ++i) {
        context = (Syn68kEAContext){.pc = 0};
        syn68k_ea_write_memory(&context, 0xffffe, invalid[i], 0);
        assert(context.error == 11 && READUW(0xffffe) == 0x55aa);
        context.error = 0;
        assert(!syn68k_ea_read_memory(&context, 0xffffe, invalid[i]));
        assert(context.error == 11);
    }
    /* Bad extension bits stop before a long displacement past guest RAM. */
    context = (Syn68kEAContext){.pc = 0xffffe};
    WRITEUW(0xffffe, 0x0138);
    EM_A0 = 0x100000;
    syn68k_effective_address(&context, 0x30, 4, 0, 1);
    assert(context.error == 4 && context.pc == 0x100000);
    /* Valid full extension, truncated long displacement: report first bad byte. */
    context = (Syn68kEAContext){.pc = 0xffffe};
    WRITEUW(0xffffe, 0x0130);
    syn68k_effective_address(&context, 0x30, 4, 0, 1);
    assert(context.error == 2 && context.fault_address == 0x100000);
    assert(EM_A0 == 0x100000);
    for (unsigned size = 1; size <= 4; size *= 2)
        for (unsigned alignment = 0; alignment < 8; ++alignment) {
            context = (Syn68kEAContext){.pc = 0};
            uint32 address = 0x2001 + alignment;
            syn68k_ea_write_memory(&context, address, size, 0x89ABCDEF);
            uint32 mask = size == 4 ? UINT32_MAX : (1u << (size * 8)) - 1;
            assert(!context.error && syn68k_ea_read_memory(&context, address, size) == (0x89ABCDEF & mask));
            assert(READUB(address) == ((0x89ABCDEF >> (size * 8 - 8)) & 255));
        }
    /* Flat copies must not bypass wrap or an alias inside the RAM window. */
    syn68k_set_memory_window(16 * 1024 * 1024, 0x400000, 0x400000);
    syn68k_address_mask = 0xFFFFFF;
    context = (Syn68kEAContext){.pc = 0};
    WRITEUB(0xFFFFFF, 0x12);
    WRITEUB(0, 0x34);
    WRITEUB(1, 0x56);
    WRITEUB(2, 0x78);
    assert(syn68k_ea_read_memory(&context, 0xAAFFFFFF, 4) == 0x12345678);
    syn68k_ea_write_memory(&context, 0xAAFFFFFF, 4, 0x9ABCDEF0);
    assert(!context.error && READUB(0xFFFFFF) == 0x9A && READUB(0) == 0xBC && READUB(1) == 0xDE && READUB(2) == 0xF0);
    void* alias_memory = VirtualAlloc(NULL, 0x20000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(alias_memory);
    uint32 alias = US_TO_SYN68K(alias_memory);
    syn68k_ea_write_memory(&context, alias + 37, 4, 0x12345678);
    assert(!context.error && ((uint8*)alias_memory)[37] == 0x12 && ((uint8*)alias_memory)[40] == 0x78);
    assert(syn68k_ea_read_memory(&context, alias + 37, 4) == 0x12345678);
    assert(!((uint8*)memory)[alias + 37]);
    VirtualFree(alias_memory, 0, MEM_RELEASE);
    puts("Shared EA byte/word/long bounds, atomic writes and error propagation passed");
    VirtualFree(memory, 0, MEM_RELEASE);
    return 0;
}
