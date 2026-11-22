/*+===================================================================
  File:      fpu-oracle-runner.c

  Summary:   Implements fpu oracle runner support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include "syn68k_public.h"
#include <windows.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
static unsigned trapped;

static syn68k_addr_t exception_handler(syn68k_addr_t address, void* argument)
{
    (void)address;
    trapped = (unsigned)(uintptr_t)argument;
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

int main(int argc, char** argv)
{
    assert(argc == 4);
    void* memory = VirtualAlloc(NULL, 1024 * 1024, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(1024 * 1024, 0, 0);
    uint32 vectors[64] = {0};
    initialize_68k_emulator(NULL, atoi(argv[3]), vectors, 0);
    for (unsigned i = 2; i < 64; ++i)
        trap_install_handler(i, exception_handler, (void*)(uintptr_t)i);
    FILE* file = fopen(argv[1], "rb");
    assert(file);
    size_t size = fread(SYN68K_TO_US(0x1000), 1, 0x2000, file);
    assert(size > 0 && feof(file));
    fclose(file);
    EM_A7 = 0xff000;
    WRITEUL(EM_A7, 0x4000);
    CALL_EMULATOR(0x1000);
    if (trapped)
        fprintf(stderr, "Oracle fixture exception %u\n", trapped);
    assert(!trapped && EM_D0 <= 4096);
    file = fopen(argv[2], "wb");
    assert(file);
    assert(fwrite(SYN68K_TO_US(0x4000), 1, EM_D0, file) == EM_D0);
    assert(!fclose(file));
    VirtualFree(memory, 0, MEM_RELEASE);
    return 0;
}
