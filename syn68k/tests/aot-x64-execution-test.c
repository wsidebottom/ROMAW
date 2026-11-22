/*+===================================================================
  File:      aot-x64-execution-test.c

  Summary:   Verify cold and warm native execution across processes and block invalidation.

  Classes:   None.

  Functions: main, __wrap_trap_direct

  Origin:    ARDI Executor persistent 68K translation regression fixture.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include "jit-x64.h"
#include "trap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int warm, forbid_fpu_tail;
static void* retire_code;
static unsigned decoder_calls, compilations;
static clock_t translation_time;

syn68k_addr_t __real_trap_direct(uint32 selector, uint32 pc, uint32 address);

syn68k_addr_t __wrap_trap_direct(uint32 selector, uint32 pc, uint32 address)
{
    assert(!forbid_fpu_tail);
    if (selector != 11 || !retire_code)
        return __real_trap_direct(selector, pc, address);
    assert(selector == 11 && pc == 0x1000 && !address && retire_code);
    ++decoder_calls;
    syn68k_x64_release(retire_code);
    retire_code = NULL;
    return 0x1234;
}

static void* compile(unsigned words)
{
    int8 length = words;
    unsigned translated;
    int terminal;
    uint64 hits = syn68k_aot_hit_count(), emissions = syn68k_native_compile_count();
    clock_t before = clock();
    void* code = syn68k_x64_compile(SYN68K_TO_US(0x1000), &length, 1, &translated, 0x1000, &terminal);
    translation_time += clock() - before;
    assert(code && translated == 1 && terminal);
    assert(syn68k_aot_hit_count() == hits + warm);
    assert(syn68k_native_compile_count() == emissions + !warm);
    ++compilations;
    return code;
}

static void program(unsigned opcode)
{
    memset(&cpu_state, 0, sizeof cpu_state);
    WRITEUW(0x1000, opcode);
}

int main(int argc, char** argv)
{
    assert(argc == 2 && getenv("SYN68K_AOT_CACHE"));
    warm = atoi(argv[1]);
    void* memory = calloc(1, 0x10000);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(0x10000, 0, 0);
    uint32 vectors[64] = {0};
    initialize_68k_emulator(NULL, 1, vectors, 0);

    program(0x203C); /* MOVE.L #immediate,D0: cached immediate source bytes. */
    WRITEUL(0x1002, 0x12345678);
    void* code = compile(3);
    assert(syn68k_x64_execute(code) == 0x1006 && EM_D0 == 0x12345678);
    syn68k_x64_release(code);
    WRITEUL(0x1002, 0x87654321);
    code = compile(3); /* Different source gets its own entry. */
    assert(syn68k_x64_execute(code) == 0x1006 && EM_D0 == 0x87654321);
    syn68k_x64_release(code);

    program(0x2028); /* MOVE.L displacement(A0),D0: extensions stay live. */
    code = compile(1);
    EM_A0 = 0x4000;
    WRITEUW(0x1002, 4);
    WRITEUL(0x4004, 0x10203040);
    assert(syn68k_x64_execute(code) == 0x1004 && EM_D0 == 0x10203040);
    WRITEUW(0x1002, 8);
    WRITEUL(0x4008, 0x55667788);
    assert(syn68k_x64_execute(code) == 0x1004 && EM_D0 == 0x55667788);
    unsigned char aliased[4] = {1, 2, 3, 4};
    EM_A0 = US_TO_SYN68K(aliased);
    WRITEUW(0x1002, 0);
    assert(syn68k_x64_execute(code) == 0x1004 && EM_D0 == 0x01020304);
    syn68k_x64_release(code);

    program(0x2030); /* Full indexed helper and private continuation relocation. */
    code = compile(1);
    WRITEUW(0x1002, 0x01F0);
    WRITEUL(0x1004, 0x4801);
    WRITEUL(0x4801, 0x89ABCDEF);
    assert(syn68k_x64_execute(code) == 0x1008 && EM_D0 == 0x89ABCDEF);
    EM_A0 = 0x4000;
    EM_D1 = 4;
    WRITEUW(0x1002, 0x1004);
    assert(syn68k_x64_execute(code) == 0x1004 && EM_D0 == 0x55667788);
    syn68k_x64_release(code);

    program(0x22D8); /* Paired operand helper and host destination relocation. */
    EM_A0 = 0x4004;
    EM_A1 = 0x5000;
    code = compile(1);
    assert(syn68k_x64_execute(code) == 0x1002 && READUL(0x5000) == 0x10203040);
    assert(EM_A0 == 0x4008 && EM_A1 == 0x5004);
    syn68k_x64_release(code);

    program(0xC300); /* ABCD D0,D1: ahead-compiled decimal kernel relocation. */
    EM_D0 = 0x25;
    EM_D1 = 0x17;
    code = compile(1);
    assert(syn68k_x64_execute(code) == 0x1002 && EM_D1 == 0x42);
    syn68k_x64_release(code);

    program(0xF200); /* Direct arithmetic/control dispatch, with a live command word. */
    cpu_state.fpu.regs[0].exponent = 0x3FFF;
    cpu_state.fpu.regs[0].significand = UINT64_C(0x8000000000000000);
    cpu_state.fpu.regs[1].exponent = 0x4000;
    cpu_state.fpu.regs[1].significand = UINT64_C(0x8000000000000000);
    WRITEUW(0x1002, 0x0422); /* FADD FP1,FP0: 1 + 2 = 3. */
    code = compile(1);
    forbid_fpu_tail = 1;
    assert(syn68k_x64_execute(code) == 0x1004);
    assert(cpu_state.fpu.regs[0].exponent == 0x4000 &&
           cpu_state.fpu.regs[0].significand == UINT64_C(0xC000000000000000));
    WRITEUW(0x1002, 0x0428); /* FSUB FP1,FP0: 3 - 2 = 1. */
    assert(syn68k_x64_execute(code) == 0x1004);
    assert(cpu_state.fpu.regs[0].exponent == 0x3FFF &&
           cpu_state.fpu.regs[0].significand == UINT64_C(0x8000000000000000));
    EM_D0 = 0xfedcba98;
    WRITEUW(0x1002, 0x9000); // A changed command now loads FPCR from D0.
    assert(syn68k_x64_execute(code) == 0x1004 && cpu_state.fpu.fpcr == 0xba90);
    cpu_state.fpu.fpiar = 0x76543210;
    WRITEUW(0x1002, 0xa400); // FPIAR to D0, preserving every address bit.
    assert(syn68k_x64_execute(code) == 0x1004 && EM_D0 == 0x76543210);
    cpu_state.fpu.fpcr = 0;
    EM_D0 = 0x3fc00000;
    WRITEUW(0x1002, 0x4400); // The same cached body now converts IEEE single D0.
    assert(syn68k_x64_execute(code) == 0x1004);
    assert(cpu_state.fpu.regs[0].exponent == 0x3fff &&
           cpu_state.fpu.regs[0].significand == UINT64_C(0xc000000000000000));
    forbid_fpu_tail = 0;
    syn68k_x64_release(code);

    program(0xF207); // Signed byte input from D7 keeps its upper bytes untouched.
    EM_D7 = 0x12345680;
    WRITEUW(0x1002, 0x5800);
    code = compile(1);
    forbid_fpu_tail = 1;
    assert(syn68k_x64_execute(code) == 0x1004 && EM_D7 == 0x12345680);
    assert(cpu_state.fpu.regs[0].exponent == 0xc006 &&
           cpu_state.fpu.regs[0].significand == UINT64_C(0x8000000000000000));
    cpu_state.fpu.regs[0] = (Syn68kFP80){0x3fff, UINT64_C(0xc000000000000000)};
    WRITEUW(0x1002, 0x7800); // A live direction change rounds 1.5 into the low byte.
    assert(syn68k_x64_execute(code) == 0x1004 && EM_D7 == 0x12345602);
    assert((cpu_state.fpu.fpsr & 0xff000000) == 0x08000000);
    WRITEUW(0x1002, 0x6400); // IEEE single output replaces the complete Dn value.
    assert(syn68k_x64_execute(code) == 0x1004 && EM_D7 == 0x3fc00000);
    assert((cpu_state.fpu.fpsr & 0xff000000) == 0x08000000);
    forbid_fpu_tail = 0;
    syn68k_x64_release(code);

    program(0xF20F); // FPIAR transfers also permit address registers.
    EM_A7 = 0x89abcdef;
    WRITEUW(0x1002, 0x8400);
    code = compile(1);
    forbid_fpu_tail = 1;
    assert(syn68k_x64_execute(code) == 0x1004 && cpu_state.fpu.fpiar == 0x89abcdef);
    EM_A7 = 0;
    WRITEUW(0x1002, 0xa400);
    assert(syn68k_x64_execute(code) == 0x1004 && EM_A7 == 0x89abcdef);
    forbid_fpu_tail = 0;
    syn68k_x64_release(code);

    program(0xF24B); // FDBcc with a live condition word and low-word counter.
    EM_D3 = 0xcafe0002;
    WRITEUW(0x1002, 1); // EQ is false with reset FPCC.
    WRITEUW(0x1004, 0xfffc);
    code = compile(1);
    forbid_fpu_tail = 1;
    assert(syn68k_x64_execute(code) == 0x1000 && EM_D3 == 0xcafe0001);
    WRITEUW(0x1002, 15); // A changed command selects true without decrementing.
    assert(syn68k_x64_execute(code) == 0x1006 && EM_D3 == 0xcafe0001);
    forbid_fpu_tail = 0;
    syn68k_x64_release(code);

    program(0xF243); // FSUN D3, including a quiet unordered predicate.
    cpu_state.fpu.fpsr = 0x01000000;
    EM_D3 = 0xabcd1200;
    WRITEUW(0x1002, 8);
    code = compile(1);
    forbid_fpu_tail = 1;
    assert(syn68k_x64_execute(code) == 0x1004 && EM_D3 == 0xabcd12ff);
    forbid_fpu_tail = 0;
    syn68k_x64_release(code);

    program(0xF2CF); // Long FBT uses a live signed displacement.
    WRITEUL(0x1002, 0xfffffffc);
    code = compile(1);
    forbid_fpu_tail = 1;
    assert(syn68k_x64_execute(code) == 0x0ffe);
    forbid_fpu_tail = 0;
    syn68k_x64_release(code);

    program(0xF000); /* Tail callback may release its own native allocation. */
    retire_code = compile(1);
    assert(syn68k_x64_execute(retire_code) == 0x1234 && !retire_code && decoder_calls == 1);

    /* A bounded prefix keeps its translated count and nonterminal contract. */
    int8 lengths[70];
    for (unsigned i = 0; i < 70; ++i) {
        WRITEUW(0x1000 + i * 2, 0x7000 | i);
        lengths[i] = 1;
    }
    unsigned translated;
    int terminal;
    uint64 hits = syn68k_aot_hit_count(), emissions = syn68k_native_compile_count();
    clock_t before = clock();
    code = syn68k_x64_compile(SYN68K_TO_US(0x1000), lengths, 70, &translated, 0x1000, &terminal);
    translation_time += clock() - before;
    assert(code && translated == 64 && !terminal);
    assert(syn68k_aot_hit_count() == hits + warm && syn68k_native_compile_count() == emissions + !warm);
    syn68k_x64_execute(code);
    assert(EM_D0 == 63);
    syn68k_x64_release(code);
    ++compilations;

    /* Normal Block ownership: changed source invalidates and replaces code,
       then returning to the original source may restore its disk entry. */
    program(0x702A); /* MOVEQ #42,D0 */
    WRITEUW(0x1002, 0x4EF9);
    WRITEUL(0x1004, MAGIC_EXIT_EMULATOR_ADDRESS);
    for (unsigned i = 0; i < 3; ++i) {
        WRITEUW(0x1000, i == 1 ? 0x702B : 0x702A);
        destroy_blocks(0x1000, 8);
        uint64 executions = syn68k_native_execution_count();
        interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
        assert(EM_D0 == (i == 1 ? 43 : 42) && syn68k_native_execution_count() > executions);
    }
    /* A native checked store to a translated source page must invalidate it. */
    program(0x3080); /* MOVE.W D0,(A0) */
    EM_D0 = 0x702C;
    EM_A0 = 0x1000;
    /* The previous block at 0x1000 is still registered; avoid executing it. */
    WRITEUW(0x2000, 0x3080);
    WRITEUW(0x2002, 0x4EF9);
    WRITEUL(0x2004, MAGIC_EXIT_EMULATOR_ADDRESS);
    interpret_code(hash_lookup_code_and_create_if_needed(0x2000));
    assert(READUW(0x1000) == 0x702C);
    interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
    assert(EM_D0 == 44);
    destroy_blocks(0x1000, 0x2000);
    printf("%s: %u explicit prefixes, %llu hits, %llu emissions, %.3f ms translation/restoration; "
           "live extensions, callbacks and invalidation passed.\n",
           warm ? "warm" : "cold", compilations, (unsigned long long)syn68k_aot_hit_count(),
           (unsigned long long)syn68k_native_compile_count(), 1000.0 * translation_time / CLOCKS_PER_SEC);
    free(memory);
    return 0;
}
