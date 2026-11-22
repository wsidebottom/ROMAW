/*+===================================================================
  File:      checked-store-coherence-test.c

  Summary:   Implements checked store coherence test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Completed checked writes invalidate code; failed transfers leave it live. */
#include "jit-x64.h"
#include "hash.h"
#include "trap.h"
#include "fpu.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned faults, cases;
static int native;

static syn68k_addr_t fault_handler(syn68k_addr_t pc, void* argument)
{
    (void)argument;
    assert(pc == 0x1000);
    ++faults;
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static void reset(void)
{
    destroy_blocks(0, UINT32_MAX);
    syn68k_set_memory_window(0x10000, 0, 0);
    cpu_state.sr = 0x2000;
    cpu_state.ccc = cpu_state.ccv = cpu_state.ccn = cpu_state.ccx = 0;
    cpu_state.ccnz = 1;
    cpu_state.dfc = 1;
    EM_A7 = cpu_state.isp = 0xF000;
    EM_A0 = 0x4000;
    EM_D0 = 0x702A4EF9;
}

static void cache(uint32 address)
{
    WRITEUW(address, 0x4E75);
    hash_lookup_code_and_create_if_needed(address);
    assert(hash_lookup(address));
}

static void stores(void)
{
    const unsigned ops[] = {0x48D0, 0x01C8, 0x4E50, 0x0E50, 0x40D0, 0xF620, 0x4808};
    for (unsigned i = 0; i < sizeof ops / sizeof *ops; ++i) {
        reset();
        cache(0x4000);
        WRITEUW(0x1000, ops[i]);
        WRITEUL(0x1002, 0);
        unsigned length = i == 4 ? 2 : i == 6 ? 6 : 4;
        if (i == 0)
            WRITEUW(0x1002, 1);
        else if (i == 2 || i == 6) {
            EM_A0 = 0x702A4EF9;
            EM_A7 = 0x4004;
        }
        else if (i == 3)
            WRITEUW(0x1002, 0x0800);
        else if (i == 5) {
            EM_A0 = 0x5000;
            EM_A1 = 0x4000;
            WRITEUW(0x1002, 0x9000);
            for (unsigned j = 0; j < 16; ++j)
                WRITEUB(0x5000 + j, j + 1);
        }
        assert(trap_direct(i == 5 ? 11 : 4, 0x1000, 0) == 0x1000 + length);
        assert(!hash_lookup(0x4000));
        if (i == 1) {
            assert(READUB(0x4000) == 0x70 && READUB(0x4002) == 0x2A);
            assert(READUB(0x4004) == 0x4E && READUB(0x4006) == 0xF9);
        }
        else if (i == 3)
            assert(READUW(0x4000) == 0x4EF9);
        else if (i == 4)
            assert(READUW(0x4000) == 0x2000);
        else if (i == 5)
            assert(!memcmp(SYN68K_TO_US(0x4000), SYN68K_TO_US(0x5000), 16));
        else
            assert(READUL(0x4000) == 0x702A4EF9);
        ++cases;
    }
}

static void partial_and_failed(void)
{
    reset();
    cache(0xFFFC);
    WRITEUW(0x1000, 0x48D0);
    WRITEUW(0x1002, 3); /* D0 succeeds; D1 faults. */
    EM_A0 = 0xFFFC;
    unsigned before = faults;
    assert(trap_direct(4, 0x1000, 0) == MAGIC_EXIT_EMULATOR_ADDRESS);
    assert(faults == before + 1 && !hash_lookup(0xFFFC));
    assert(READUL(0xFFFC) == 0x702A4EF9);
    ++cases;

    reset();
    for (unsigned i = 0; i < 3; ++i)
        cache(0xFFFA + i * 2);
    WRITEUW(0x1000, 0x01C8);
    WRITEUW(0x1002, 0);
    EM_A0 = 0xFFFA;
    before = faults;
    assert(trap_direct(4, 0x1000, 0) == MAGIC_EXIT_EMULATOR_ADDRESS);
    assert(faults == before + 1);
    for (unsigned i = 0; i < 3; ++i) {
        assert(!hash_lookup(0xFFFA + i * 2));
        assert(READUB(0xFFFA + i * 2) == ((EM_D0 >> (24 - i * 8)) & 255));
    }
    ++cases;

    reset();
    syn68k_set_memory_window(0x4000, 0, 0);
    EM_A7 = cpu_state.isp = 0x3000;
    cache(0x3FFE);
    WRITEUW(0x1000, 0x48D0);
    WRITEUW(0x1002, 1);
    EM_A0 = 0x3FFE;
    before = faults;
    assert(trap_direct(4, 0x1000, 0) == MAGIC_EXIT_EMULATOR_ADDRESS);
    assert(faults == before + 1 && hash_lookup(0x3FFE));
    assert(READUW(0x3FFE) == 0x4E75);
    ++cases;
}

static void self_write(void)
{
    reset();
    WRITEUW(0x1000, 0x48D0);
    WRITEUW(0x1002, 1);
    WRITEUW(0x1004, 0x4EF9);
    WRITEUL(0x1006, MAGIC_EXIT_EMULATOR_ADDRESS);
    EM_A0 = 0x1000;
    uint64 before = syn68k_native_execution_count();
    interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
    assert(READUL(0x1000) == 0x702A4EF9 && !hash_lookup(0x1000));
    assert(syn68k_native_execution_count() == before + native * 2);
    ++cases;
}

static void fpu_stores(void)
{
    const unsigned commands[] = {0x7800, 0x7000, 0x6000, 0x6400, 0x7400, 0x6800, 0xB000, 0xF080};
    for (unsigned i = 0; i < 10; ++i) {
        reset();
        syn68k_fpu_reset();
        cache(0x4000);
        cpu_state.fpu.regs[0].exponent = 0x3FFF;
        cpu_state.fpu.regs[0].significand = UINT64_C(0x8000000000000000);
        cpu_state.fpu.null_state = i == 9;
        WRITEUW(0x1000, i < 8 ? 0xF210 : 0xF310);
        if (i < 8)
            WRITEUW(0x1002, commands[i]);
        Syn68kFPUStep result;
        assert(native ? syn68k_x64_flat_fpu(0x1000, READUW(0x1000), &result) : syn68k_fpu_step(0x1000, NULL, &result));
        assert(!result.vector && result.next == 0x1000 + (i < 8 ? 4 : 2));
        assert(!hash_lookup(0x4000));
        assert(READUW(0x4000) != 0x4E75);
        ++cases;
    }

    reset();
    syn68k_fpu_reset();
    cache(0xFFFC);
    EM_A0 = 0xFFFC;
    WRITEUW(0x1000, 0xF210);
    WRITEUW(0x1002, 0x7400); /* A rejected double store writes nothing. */
    Syn68kFPUStep result;
    assert(native ? syn68k_x64_flat_fpu(0x1000, READUW(0x1000), &result) : syn68k_fpu_step(0x1000, NULL, &result));
    assert(result.vector == 2 && hash_lookup(0xFFFC));
    assert(READUW(0xFFFC) == 0x4E75);
    ++cases;

    reset();
    syn68k_fpu_reset();
    cpu_state.fpu.regs[0].exponent = 0x3FFF;
    cpu_state.fpu.regs[0].significand = UINT64_C(0x8000000000000000);
    WRITEUW(0x1000, 0xF210);
    WRITEUW(0x1002, 0x6000);
    WRITEUW(0x1004, 0x4EF9);
    WRITEUL(0x1006, MAGIC_EXIT_EMULATOR_ADDRESS);
    EM_A0 = 0x1000;
    uint64 before = syn68k_native_execution_count();
    interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
    assert(READUL(0x1000) == 1 && !hash_lookup(0x1000));
    /* The managed instruction enters its native FPU body before the next block. */
    assert(syn68k_native_execution_count() == before + native * 3);
    ++cases;
}

int main(int argc, char** argv)
{
    void* memory = calloc(1, 0x10000);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(0x10000, 0, 0);
    uint32 vectors[64] = {0};
    native = argc > 1 && atoi(argv[1]);
    initialize_68k_emulator(NULL, native, vectors, 0);
    trap_install_handler(2, fault_handler, NULL);
    stores();
    partial_and_failed();
    self_write();
    fpu_stores();
    printf("%u checked-store coherence/partial/fault/self-write cases passed (%d).\n", cases, native);
    return 0;
}
