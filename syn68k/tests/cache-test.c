/*+===================================================================
  File:      cache-test.c

  Summary:   Implements cache test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Original 68040 CINV/CPUSH words executed through both real dispatch paths. */
#include "syn68k_public.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned trapped;
static int require_native_cache;
extern syn68k_addr_t __real_trap_direct(uint32 vector, syn68k_addr_t pc, uint32 argument);

syn68k_addr_t __wrap_trap_direct(uint32 vector, syn68k_addr_t pc, uint32 argument)
{
    assert(!require_native_cache || vector != 11 || pc != 0x1000);
    return __real_trap_direct(vector, pc, argument);
}

static syn68k_addr_t cache_trap(syn68k_addr_t pc, void* argument)
{
    trapped = (unsigned)(uintptr_t)argument;
    assert(pc == 0x1000 && READUL(EM_A7 + 2) == pc);
    assert(READUW(EM_A7 + 6) == trapped * 4);
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static void execute(uint16 opcode, int supervisor, unsigned expected)
{
    destroy_blocks(0x1000, 32);
    WRITEUW(0x1000, opcode);
    WRITEUW(0x1002, 0x40c0); /* move.w SR,D0: capture preserved CCR. */
    WRITEUW(0x1004, 0x4e75);
    cpu_state.sr = supervisor ? 0x2700 : 0;
    cpu_state.isp = 0xfe000;
    cpu_state.ccc = cpu_state.ccv = cpu_state.ccx = cpu_state.ccn = 1;
    cpu_state.ccnz = 0;
    EM_A7 = 0xff000;
    EM_D0 = 0x12340000;
    trapped = 0;
    CALL_EMULATOR(0x1000);
    assert(trapped == expected);
    if (!expected)
        assert(EM_D0 == 0x1234271f);
}

static void coherency(uint16 opcode)
{
    destroy_blocks(0x2000, 16);
    WRITEUW(0x2000, 0x7001);
    WRITEUW(0x2002, 0x4e75);
    cpu_state.sr = 0x2700;
    EM_A7 = 0xff000;
    CALL_EMULATOR(0x2000);
    assert(EM_D0 == 1);
    /* Do not explicitly invalidate the old target translation. The guest
     cache instruction must make this changed instruction visible. */
    WRITEUW(0x2000, 0x7002);
    destroy_blocks(0x1000, 32);
    WRITEUW(0x1000, opcode);
    WRITEUW(0x1002, 0x4eb9); /* jsr 0x2000 */
    WRITEUL(0x1004, 0x2000);
    WRITEUW(0x1008, 0x4e75);
    EM_A0 = 0x2007;
    EM_A7 = 0xff000;
    trapped = 0;
    CALL_EMULATOR(0x1000);
    assert(!trapped && EM_D0 == 2 && EM_A0 == 0x2007);
}

int main(int argc, char** argv)
{
    void* memory = VirtualAlloc(NULL, 1024 * 1024, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(1024 * 1024, 0, 0);
    uint32 vectors[64] = {0};
    int native = argc > 1 ? atoi(argv[1]) : 0;
    int aot_run = argc > 2 ? atoi(argv[2]) : 0;
    initialize_68k_emulator(NULL, native, vectors, 0);
    native = syn68k_native_code_enabled();
    require_native_cache = native;
    uint64 hits_before = syn68k_aot_hit_count(), emits_before = syn68k_native_compile_count();
    trap_install_handler(4, cache_trap, (void*)(uintptr_t)4);
    trap_install_handler(8, cache_trap, (void*)(uintptr_t)8);
    trap_install_handler(11, cache_trap, (void*)(uintptr_t)11);
    for (unsigned push = 0; push < 2; ++push)
        for (unsigned cache = 0; cache < 4; ++cache)
            for (unsigned scope = 0; scope < 4; ++scope)
                for (unsigned reg = 0; reg < 8; ++reg) {
                    uint16 opcode = 0xf400 | (push << 5) | (cache << 6) | (scope << 3) | reg;
                    execute(opcode, 1, scope ? 0 : 4);
                    if (scope)
                        execute(opcode, 0, 8);
                    if (scope && (cache & 2) && !reg)
                        coherency(opcode);
                }
    uint64 hits = syn68k_aot_hit_count() - hits_before;
    uint64 emits = syn68k_native_compile_count() - emits_before;
    if (aot_run == 1)
        assert(native && emits);
    else if (aot_run == 2)
        assert(native && hits && !emits);
    printf("68040 cache decoding, privilege, CCR and code coherency passed (%s; emitted=%llu hits=%llu).\n",
           native ? "native" : "interpreter", (unsigned long long)emits, (unsigned long long)hits);
    VirtualFree(memory, 0, MEM_RELEASE);
    return 0;
}
