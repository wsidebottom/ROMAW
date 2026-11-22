/*+===================================================================
  File:      jit-x64-exception-test.c

  Summary:   Implements jit x64 exception test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: __wrap_trap_direct, main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Native software traps and checked RTE frames, including bank transitions. */
#include "jit-x64.h"
#include "trap.h"
#include "destroyblock.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned hits, expected_vector, expected_pc, expected_address, cases;
static uint16 expected_sr;
static void* retire_code;
syn68k_addr_t __real_trap_direct(uint32 selector, uint32 pc, uint32 address);

syn68k_addr_t __wrap_trap_direct(uint32 selector, uint32 pc, uint32 address)
{
    if (retire_code) {
        syn68k_x64_release(retire_code);
        retire_code = NULL;
    }
    return __real_trap_direct(selector, pc, address);
}

static syn68k_addr_t handler(syn68k_addr_t pc, void* argument)
{
    unsigned vector = (uintptr_t)argument;
    assert(vector == expected_vector && pc == expected_pc);
    assert(READUW(EM_A7) == expected_sr);
    assert(READUL(EM_A7 + 2) == expected_pc);
    assert(READUW(EM_A7 + 6) == ((vector == 7 ? 0x2000 : vector == 2 ? 0x7000 : 0) | vector * 4));
    if (vector == 7)
        assert(READUL(EM_A7 + 8) == expected_address);
    if (vector == 2)
        assert(READUL(EM_A7 + 20) == expected_address);
    ++hits;
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static void flags(unsigned f)
{
    cpu_state.ccc = f & 1 ? 0x80000000 : 0;
    cpu_state.ccv = f & 2 ? 0x12345678 : 0;
    cpu_state.ccnz = f & 4 ? 0 : 0x87654321;
    cpu_state.ccn = f & 8 ? 0x12345678 : 0;
    cpu_state.ccx = f & 16 ? 0x80000000 : 0;
}

static int condition(unsigned cc, unsigned f)
{
    int c = !!(f & 1), v = !!(f & 2), z = !!(f & 4), n = !!(f & 8);
    const int values[] = {1,  0, !c && !z, c || z, !c,     c,      !z,           z,
                          !v, v, !n,       n,      n == v, n != v, !z && n == v, z || n != v};
    return values[cc];
}

static void* compile(unsigned op, uint32 pc)
{
    WRITEUW(pc, op);
    int8 length = 1;
    unsigned translated;
    int terminal;
    void* code = syn68k_x64_compile(SYN68K_TO_US(pc), &length, 1, &translated, pc, &terminal);
    assert(code && translated == 1 && terminal);
    return code;
}

static void reset(unsigned f, unsigned supervisor)
{
    cpu_state.sr = supervisor ? 0x2000 : 0;
    cpu_state.usp = 0xD000;
    cpu_state.isp = 0xE000;
    cpu_state.msp = 0xC000;
    EM_A7 = supervisor ? 0xE000 : 0xD000;
    flags(f);
    expected_sr = cpu_state.sr | f;
}

int main(void)
{
    void* ram = calloc(1, 0x10000);
    assert(ram);
    ROMlib_offset = (uintptr_t)ram;
    syn68k_set_memory_window(0x10000, 0, 0);
    uint32 vectors[64] = {0};
    initialize_68k_emulator(NULL, 1, vectors, 0);
    for (unsigned v = 0; v < 64; ++v)
        trap_install_handler(v, handler, (void*)(uintptr_t)v);
    for (unsigned mode = 0; mode < 3; ++mode)
        for (unsigned cc = 0; cc < 16; ++cc)
            for (unsigned form = 0; form < 3; ++form) {
                unsigned op = 0x50FA | cc << 8 | (form == 2 ? 4 : form);
                /* FA, FB and FC: word, long and no payload. */
                op = 0x50FA + form | cc << 8;
                unsigned size = form == 0 ? 4 : form == 1 ? 6 : 2;
                destroy_blocks(0x1000, 32);
                void* code = compile(op, 0x1000);
                WRITEUL(0x1002, 0xA55A1234);
                WRITEUW(0x1000 + size, 0x4EF9);
                WRITEUL(0x1002 + size, MAGIC_EXIT_EMULATOR_ADDRESS);
                for (unsigned f = 0; f < 32; ++f)
                    for (unsigned supervisor = 0; supervisor < 2; ++supervisor) {
                        reset(f, supervisor);
                        unsigned before = hits;
                        expected_vector = 7;
                        expected_pc = 0x1000 + size;
                        expected_address = 0x1000;
                        if (mode == 0)
                            assert(syn68k_x64_execute(code) ==
                                   (condition(cc, f) ? MAGIC_EXIT_EMULATOR_ADDRESS : expected_pc));
                        else {
                            syn68k_x64_enabled = mode == 2;
                            interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
                        }
                        assert(hits == before + condition(cc, f));
                        ++cases;
                    }
                syn68k_x64_release(code);
            }
    destroy_blocks(0x1000, 32);
    const unsigned ops[] = {0x4AFC, 0x4848, 0x484F, 0x4E76, 0x4E40, 0x4E4F};
    for (unsigned i = 0; i < sizeof ops / sizeof *ops; ++i) {
        void* code = compile(ops[i], 0x1000);
        for (unsigned f = 0; f < 32; ++f) {
            reset(f, 0);
            expected_vector = i < 3 ? 4 : i == 3 ? 7 : 32 + (ops[i] & 15);
            expected_pc = i < 3 ? 0x1000 : 0x1002;
            expected_address = i == 3 ? 0x1000 : 0;
            unsigned before = hits;
            uint32 next = syn68k_x64_execute(code);
            assert(hits == before + (i != 3 || !!(f & 2)));
            assert(next == (i == 3 && !(f & 2) ? 0x1002 : MAGIC_EXIT_EMULATOR_ADDRESS));
            ++cases;
        }
        syn68k_x64_release(code);
    }
    void* rte = compile(0x4E73, 0x1000);
    const unsigned formats[] = {0, 2, 3, 7};
    for (unsigned mode = 0; mode < 3; ++mode)
        for (unsigned i = 0; i < 4; ++i)
            for (unsigned bank = 0; bank < 3; ++bank)
                for (unsigned f = 0; f < 32; ++f) {
                    reset(31, 1);
                    uint16 sr = f | (bank == 0 ? 0 : bank == 1 ? 0x2000 : 0x3000);
                    WRITEUW(0xE000, sr);
                    WRITEUL(0xE002, MAGIC_EXIT_EMULATOR_ADDRESS);
                    WRITEUW(0xE006, formats[i] << 12);
                    unsigned before = hits;
                    if (!mode)
                        assert(syn68k_x64_execute(rte) == MAGIC_EXIT_EMULATOR_ADDRESS);
                    else {
                        syn68k_x64_enabled = mode == 2;
                        interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
                    }
                    assert(hits == before && COMPUTE_SR_FROM_CPU_STATE() == sr);
                    assert(EM_A7 == (bank == 0   ? 0xD000
                                     : bank == 2 ? 0xC000
                                                 : 0xE000 + (formats[i] == 7 ? 60
                                                             : i             ? 12
                                                                             : 8)));
                    ++cases;
                }
    reset(31, 1);
    WRITEUW(0xE000, 0x301F);
    WRITEUW(0xE006, 0x1000);
    WRITEUW(0xC000, 0);
    WRITEUL(0xC002, MAGIC_EXIT_EMULATOR_ADDRESS);
    WRITEUW(0xC006, 0);
    assert(syn68k_x64_execute(rte) == MAGIC_EXIT_EMULATOR_ADDRESS);
    assert(EM_A7 == 0xD000 && cpu_state.msp == 0xC008);
    /* Format 7 validates only its 14-byte header; its writeback tail belongs
       to the guest handler and may extend beyond readable memory. */
    reset(31, 1);
    EM_A7 = 0xFFF2;
    WRITEUW(0xFFF2, 0x2000);
    WRITEUL(0xFFF4, MAGIC_EXIT_EMULATOR_ADDRESS);
    WRITEUW(0xFFF8, 0x7000);
    WRITEUL(0xFFFA, 0);
    WRITEUW(0xFFFE, 0);
    unsigned before = hits;
    assert(syn68k_x64_execute(rte) == MAGIC_EXIT_EMULATOR_ADDRESS);
    assert(hits == before && EM_A7 == 0x1002E && COMPUTE_SR_FROM_CPU_STATE() == 0x2000);
    ++cases;
    for (unsigned failure = 0; failure < 3; ++failure) {
        reset(31, failure != 0);
        expected_vector = failure == 0 ? 8 : failure == 1 ? 14 : 2;
        expected_pc = 0x1000;
        expected_address = failure == 2 ? 0x10000 : 0;
        if (failure == 0)
            EM_A7 = UINT32_MAX;
        else if (failure == 1)
            WRITEUW(0xE006, 0xF000);
        else {
            EM_A7 = 0xFFF4;
            WRITEUW(0xFFF4, 0x2000);
            WRITEUL(0xFFF6, 0x3000);
            WRITEUW(0xFFFA, 0x7000);
            WRITEUL(0xFFFC, 0);
        }
        retire_code = rte;
        assert(syn68k_x64_execute(rte) == MAGIC_EXIT_EMULATOR_ADDRESS);
        assert(!retire_code);
        rte = compile(0x4E73, 0x1000);
    }
    syn68k_x64_release(rte);
    void* truncated = compile(0x51FB, 0xFFFC); /* false still fetches payload */
    reset(31, 1);
    expected_vector = 2;
    expected_pc = 0xFFFC;
    expected_address = 0x10000;
    assert(syn68k_x64_execute(truncated) == MAGIC_EXIT_EMULATOR_ADDRESS);
    syn68k_x64_release(truncated);
    printf("%u software trap/RTE cases and checked frame failures passed.\n", cases);
    free(ram);
    return 0;
}
