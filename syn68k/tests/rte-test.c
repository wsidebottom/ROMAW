/*+===================================================================
  File:      rte-test.c

  Summary:   Implements rte test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Original words and frame layouts from MC68040UM section 8.4. */
#include "syn68k_public.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned trapped;
static int require_native_rte;
extern syn68k_addr_t __real_trap_direct(uint32 vector, syn68k_addr_t pc, uint32 argument);

syn68k_addr_t __wrap_trap_direct(uint32 vector, syn68k_addr_t pc, uint32 argument)
{
    assert(!require_native_rte || vector != 4 || pc != 0x1000);
    return __real_trap_direct(vector, pc, argument);
}

static syn68k_addr_t handler(syn68k_addr_t pc, void* argument)
{
    trapped = (unsigned)(uintptr_t)argument;
    assert(pc == 0x1000 && READUL(EM_A7 + 2) == pc);
    assert(READUW(EM_A7 + 6) == trapped * 4);
    assert(READUW(EM_A7) == (trapped == 8 ? 0x001f : 0x201f));
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static void reset(uint16 sr, uint32 stack)
{
    cpu_state.sr = sr;
    cpu_state.ccc = cpu_state.ccv = cpu_state.ccn = cpu_state.ccx = 1;
    cpu_state.ccnz = 0;
    cpu_state.usp = 0x7000;
    cpu_state.isp = 0x8000;
    cpu_state.msp = 0x9000;
    EM_A7 = stack;
    trapped = 0;
}

static void run(void)
{
    /* RTE consumes a prebuilt frame, so do not push CALL_EMULATOR's RTS word. */
    interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
}

static void frame(uint32 stack, unsigned format, uint16 sr)
{
    WRITEUW(stack, sr);
    WRITEUL(stack + 2, MAGIC_EXIT_EMULATOR_ADDRESS);
    WRITEUW(stack + 6, format << 12);
}

int main(int argc, char** argv)
{
    void* memory = VirtualAlloc(NULL, 1024 * 1024, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(1024 * 1024, 0, 0);
    uint32 vectors[64] = {0};
    int aot_run = argc > 2 ? atoi(argv[2]) : 0;
    int native = argc > 1 && atoi(argv[1]);
    initialize_68k_emulator(NULL, native, vectors, 0);
    native = syn68k_native_code_enabled();
    require_native_rte = native;
    uint64 hits_before = syn68k_aot_hit_count(), emits_before = syn68k_native_compile_count();
    for (unsigned i = 2; i < 64; ++i)
        trap_install_handler(i, handler, (void*)(uintptr_t)i);
    WRITEUW(0x1000, 0x4e73);
    /* A valid-looking and an unmapped user stack must both trap before reads. */
    const uint32 stacks[] = {0x7000, 0x100000};
    for (unsigned i = 0; i < 2; ++i) {
        uint32 stack = stacks[i];
        reset(0, stack);
        run();
        assert(trapped == 8 && EM_A7 == 0x7ff8);
        assert(cpu_state.usp == stack && cpu_state.msp == 0x9000);
    }
    for (unsigned format = 0; format < 16; ++format) {
        if (format == 1)
            continue;
        reset(0x2000, 0x8000);
        frame(0x8000, format, 0x201f);
        if (format == 7)
            WRITEUW(0x800c, 0x8000); /* Flat frames do not restore mapped continuations. */
        run();
        unsigned length = format == 0 ? 8 : format == 2 || format == 3 ? 12 : format == 7 ? 60 : 0;
        assert(trapped == (length ? 0 : 14));
        assert(EM_A7 == (length ? 0x8000 + length : 0x7ff8));
        assert(READUW(0x8006) == format << 12);
    }
    /* Restoring a frame cannot set reserved MC68040 SR bits. */
    reset(0x2000, 0x8000);
    frame(0x8000, 0, 0x2fff);
    run();
    assert(!trapped && cpu_state.sr == 0x2700);
    assert(cpu_state.ccc && cpu_state.ccv && !cpu_state.ccnz && cpu_state.ccn && cpu_state.ccx);
    /* A throwaway ISP frame selects the MSP, then the real frame selects ISP. */
    reset(0x2000, 0x8000);
    frame(0x8000, 1, 0x301f);
    frame(0x9000, 0, 0x201f);
    run();
    assert(!trapped && EM_A7 == 0x8008 && cpu_state.msp == 0x9008);
    /* A supervisor return can select the user stack without reading it. */
    reset(0x2000, 0x8000);
    frame(0x8000, 0, 0x001f);
    run();
    assert(!trapped && EM_A7 == 0x7000 && cpu_state.isp == 0x8008);
    assert(!(cpu_state.sr & 0x2000));
    uint64 hits = syn68k_aot_hit_count() - hits_before;
    uint64 emits = syn68k_native_compile_count() - emits_before;
    if (aot_run == 1)
        assert(native && hits == 0 && emits >= 2);
    else if (aot_run == 2)
        assert(native && hits >= 2 && emits == 0);
    printf("68040 RTE privilege, formats and stack banks passed (%s; emitted=%llu hits=%llu).\n",
           native ? "native" : "interpreter", (unsigned long long)emits, (unsigned long long)hits);
    VirtualFree(memory, 0, MEM_RELEASE);
    return 0;
}
