/*+===================================================================
  File:      indexed-test.c

  Summary:   Implements indexed test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Full-format encodings: M68000PRM Figure 2-2 and Table 2-2. */
#include "syn68k_public.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned trapped;
static uint32 fault_pc;

static syn68k_addr_t exception_handler(syn68k_addr_t address, void* argument)
{
    (void)address;
    trapped = (unsigned)(uintptr_t)argument;
    if (READUL(EM_A7 + 2) != fault_pc)
        fprintf(stderr, "trap %u PC %08x expected %08x opcode %04x extension %04x\n", trapped, READUL(EM_A7 + 2),
                fault_pc, READUW(fault_pc), READUW(fault_pc + 2));
    assert(READUL(EM_A7 + 2) == fault_pc);
    assert(READUW(EM_A7 + 6) == trapped * 4);
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static void execute(const uint16* words, size_t count, unsigned expected)
{
    destroy_blocks(0x1000, 64);
    for (size_t i = 0; i < count; ++i)
        WRITEUW(0x1000 + i * 2, words[i]);
    WRITEUW(0x1000 + count * 2, 0x4e75);
    EM_A7 = 0xff000;
    fault_pc = 0x1000;
    trapped = 0;
    CALL_EMULATOR(0x1000);
    assert(trapped == expected);
}

static void reserved_encodings(void)
{
    /* Independent truth table for IS and I/IS. Brief extensions ignore it. */
    const unsigned valid_indirection[] = {0xef, 0x0f};
    const uint16 instructions[] = {
        0x2030, /* MOVE.L (indexed,A0),D0. */
        0x203b, /* MOVE.L (indexed,PC),D0. */
        0x43f0, /* LEA (indexed,A0),A1. */
        0x2199, /* MOVE.L (A1)+,(indexed,A0): no source side effects. */
        0x22f0  /* MOVE.L (indexed,A0),(A1)+: no destination side effects. */
    };
    for (unsigned low = 0; low < 256; ++low) {
        int valid = !(low & 8) && ((low >> 4) & 3) && (valid_indirection[(low >> 6) & 1] & (1u << (low & 7)));
        if (valid)
            continue;
        for (unsigned i = 0; i < sizeof instructions / sizeof *instructions; ++i) {
            uint16 words[] = {instructions[i], 0x0100 | low, 0, 0, 0, 0};
            EM_A0 = 0x100000;
            EM_A1 = 0x2000;
            EM_D0 = 0x76543210;
            WRITEUL(0x2000, 0xabcdef01);
            execute(words, sizeof words / sizeof *words, 4);
            assert(EM_A0 == 0x100000 && EM_A1 == 0x2000 && EM_D0 == 0x76543210 && READUL(0x2000) == 0xabcdef01);
        }
        for (unsigned writing = 0; writing < 2; ++writing) {
            uint16 words[] = {0xf230, writing ? 0x6000 : 0x4000, 0x0100 | low, 0, 0, 0, 0};
            syn68k_fpu_reset();
            cpu_state.fpu.regs[0].exponent = 0x4000;
            cpu_state.fpu.regs[0].significand = UINT64_C(0x8000000000000000);
            EM_A0 = 0x100000;
            execute(words, sizeof words / sizeof *words, 4);
            assert(EM_A0 == 0x100000 && cpu_state.fpu.null_state);
            assert(cpu_state.fpu.regs[0].exponent == 0x4000 &&
                   cpu_state.fpu.regs[0].significand == UINT64_C(0x8000000000000000));
            assert(!cpu_state.fpu.fpiar && !cpu_state.fpu.fpsr);
        }
    }
}

static void pc_suppression(void)
{
    const struct {
        uint16 extension;
        uint32 index, displacement, expected;
    } cases[] = {
        {0x01f0, 0, 0x12345678, 0x12345678}, /* Suppress both base and index. */
        {0x0170, 0, 0x12345678, 0x1234667a}, /* Keep PC, suppress index. */
        {0x01b0, 3, 7, 10},                  /* Suppress PC, keep word index. */
        {0x0130, 3, 7, 0x100c},              /* Keep both. */
        {0x0db0, 0xffff0001, 0, 0xfffc0004}, /* Long index scaled by four. */
        {0x05b0, 0xffff, 0, 0xfffffffc}      /* Signed word index scaled by four. */
    };

    for (unsigned i = 0; i < sizeof cases / sizeof *cases; ++i) {
        uint16 words[] = {0x41fb, cases[i].extension, cases[i].displacement >> 16, cases[i].displacement};
        EM_D0 = cases[i].index;
        execute(words, sizeof words / sizeof *words, 0);
        assert(EM_A0 == cases[i].expected);
    }
    const uint16 brief[] = {0x41fb, 0x000f};
    EM_D0 = 3;
    execute(brief, sizeof brief / sizeof *brief, 0);
    assert(EM_A0 == 0x1014);
}

static void extension_invalidation_and_bounds(void)
{
    const uint16 invalid[] = {0x2030, 0x0145};
    EM_A0 = 0x2000;
    WRITEUL(0x2000, 0x12345678);
    execute(invalid, sizeof invalid / sizeof *invalid, 4);
    /* Only the extension changes; its bytes must belong to the cached block. */
    WRITEUW(0x1002, 0x0150);
    destroy_blocks(0x1002, 2);
    EM_A7 = 0xff000;
    trapped = 0;
    CALL_EMULATOR(0x1000);
    assert(!trapped && EM_D0 == 0x12345678);
    for (unsigned floating = 0; floating < 2; ++floating) {
        uint32 pc = floating ? 0xffffa : 0xffffc;
        destroy_blocks(pc, 6);
        WRITEUW(pc, floating ? 0xf230 : 0x2030);
        if (floating)
            WRITEUW(pc + 2, 0x4000);
        /* Reserved bit three plus a long BD: no BD fetch beyond guest RAM. */
        WRITEUW(0xffffe, 0x0138);
        EM_A7 = 0xff000;
        fault_pc = pc;
        trapped = 0;
        CALL_EMULATOR(pc);
        assert(trapped == 4);
    }
}

int main(int argc, char** argv)
{
    void* memory = VirtualAlloc(NULL, 1024 * 1024, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(1024 * 1024, 0, 0);
    uint32 vectors[64] = {0};
    int native = argc > 1 && atoi(argv[1]);
    initialize_68k_emulator(NULL, native, vectors, 0);
    native = syn68k_native_code_enabled();
    for (unsigned i = 2; i < 64; ++i)
        trap_install_handler(i, exception_handler, (void*)(uintptr_t)i);
    reserved_encodings();
    pc_suppression();
    extension_invalidation_and_bounds();
    printf("Full indexed legality, PC suppression and invalidation passed (%s).\n", native ? "native" : "interpreter");
    VirtualFree(memory, 0, MEM_RELEASE);
    return 0;
}
