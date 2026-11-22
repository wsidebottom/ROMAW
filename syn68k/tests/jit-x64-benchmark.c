/*+===================================================================
  File:      jit-x64-benchmark.c

  Summary:   Implements jit x64 benchmark support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Optional steady-state Win64 register/indirect loop timing, not a speed gate. */
#include "syn68k_public.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static void run_loop(int memory)
{
    EM_D0 = 0;
    EM_D7 = 65535;
    EM_A0 = 0x4000;
    WRITEUL(0x4000, 0);
    interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
    assert((memory ? READUL(0x4000) : EM_D0) == 32 * 65536);
}

int main(int argc, char** argv)
{
    if (argc != 3 || (argv[1][0] != '0' && argv[1][0] != '1') || argv[1][1] ||
        (argv[2][0] != '0' && argv[2][0] != '1') || argv[2][1]) {
        fprintf(stderr, "Usage: jit-x64-benchmark.exe native(0|1) memory(0|1)\n");
        return 2;
    }
    int native = argv[1][0] == '1', memory = argv[2][0] == '1';
    void* ram = calloc(1, 0x10000);
    assert(ram);
    ROMlib_offset = (uintptr_t)ram;
    syn68k_set_memory_window(0x10000, 0, 0);
    uint32 vectors[64] = {0};
    initialize_68k_emulator(NULL, native, vectors, 0);
    for (unsigned i = 0; i < 32; ++i)
        WRITEUW(0x1000 + i * 2, memory ? 0x5290 : 0x5280);
    WRITEUW(0x1040, 0x51CF); /* DBF D7,loop */
    WRITEUW(0x1042, (uint16)-66);
    WRITEUW(0x1044, 0x4EF9);
    WRITEUL(0x1046, MAGIC_EXIT_EMULATOR_ADDRESS);
    run_loop(memory); /* Exclude translation and initial page allocation. */
    LARGE_INTEGER frequency, start, end;
    assert(QueryPerformanceFrequency(&frequency));
    assert(QueryPerformanceCounter(&start));
    uint64 entries = syn68k_native_execution_count();
    for (unsigned i = 0; i < 8; ++i)
        run_loop(memory);
    assert(QueryPerformanceCounter(&end));
    entries = syn68k_native_execution_count() - entries;
    assert(native ? entries != 0 : entries == 0);
    printf("%s %s: 16777216 arithmetic operations in %.6f s; "
           "%llu native entries\n",
           native ? "x64 JIT" : "synthetic", memory ? "indirect" : "register",
           (double)(end.QuadPart - start.QuadPart) / frequency.QuadPart, (unsigned long long)entries);
    destroy_blocks(0, UINT32_MAX);
    free(ram);
    return 0;
}
