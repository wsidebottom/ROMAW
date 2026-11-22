/*+===================================================================
  File:      threaded-dispatch-test.c

  Summary:   Implements threaded dispatch test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Reentrant dispatch, full-width labels and translated-block invalidation. */
#include "syn68k_public.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned callbacks;
static int token;

static syn68k_addr_t nested_callback(syn68k_addr_t pc, void* argument)
{
    assert(argument == &token);
    uint32 resume = READUL(EM_A7);
    uint32 stack = EM_A7;
    char* amode = cpu_state.amode_p;
    char* reversed = cpu_state.reversed_amode_p;
    jmp_buf* saved_jump = cpu_state.setjmp_buf;
    WRITEUL(stack - 4, MAGIC_EXIT_EMULATOR_ADDRESS);
    EM_A7 = stack - 4;
    interpret_code(hash_lookup_code_and_create_if_needed(0x2000));
    assert(EM_A7 == stack && EM_D1 == 23);
    assert(cpu_state.amode_p == amode);
    assert(cpu_state.reversed_amode_p == reversed);
    assert(cpu_state.setjmp_buf == saved_jump);
    EM_A7 += 4;
    ++callbacks;
    return resume;
}

int main(void)
{
    void* memory = calloc(1, 0x10000);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(0x10000, 0, 0);
    uint32 vectors[64] = {0};
    initialize_68k_emulator(NULL, 1, vectors, 0);
    assert(syn68k_threaded_dispatch_enabled() == EXPECT_THREADED);
    if (sizeof(void*) == 8 && syn68k_threaded_dispatch_enabled())
        assert(*(const uintptr_t*)hash_lookup_code_and_create_if_needed(MAGIC_EXIT_EMULATOR_ADDRESS) > UINT32_MAX);
    syn68k_addr_t callback = callback_install(nested_callback, &token);
    WRITEUW(0x2000, 0x7217); /* MOVEQ #23,D1 */
    WRITEUW(0x2002, 0x4E75); /* RTS */
    WRITEUW(0x1002, 0x4EB9); /* JSR callback */
    WRITEUL(0x1004, callback);
    WRITEUW(0x1008, 0x5280); /* ADDQ.L #1,D0 */
    WRITEUW(0x100A, 0x4E75);
    for (unsigned i = 0; i < 1000; ++i) {
        destroy_blocks(0x1000, 12);
        WRITEUW(0x1000, 0x7000 | (i & 127));
        EM_A7 = 0xF000;
        WRITEUL(EM_A7, MAGIC_EXIT_EMULATOR_ADDRESS);
        cpu_state.amode_p = (char*)memory + 0x3000;
        cpu_state.reversed_amode_p = (char*)memory + 0x4000;
        jmp_buf jump;
        cpu_state.setjmp_buf = &jump;
        interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
        assert(EM_D0 == (i & 127) + 1 && EM_A7 == 0xF004);
        assert(callbacks == i + 1 && cpu_state.setjmp_buf == &jump);
        assert(cpu_state.amode_p == (char*)memory + 0x3000);
        assert(cpu_state.reversed_amode_p == (char*)memory + 0x4000);
    }
    puts("1000 nested dispatch, callback and block invalidation cases passed.");
    return 0;
}
