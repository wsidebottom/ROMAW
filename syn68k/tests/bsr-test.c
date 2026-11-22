/*+===================================================================
  File:      bsr-test.c

  Summary:   Implements bsr test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Original BSR displacement, stack, cache and condition fixtures. */
#include "syn68k_public.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned cases, trapped, cache_hits;
static uint32 start_pc = 0x1000, expected_stack;
static uint32 expected_pc = 0x1000, expected_fault;
static uint16 expected_sr;

static uint16 status(void)
{
    return cpu_state.sr | !!cpu_state.ccc | (!!cpu_state.ccv << 1) | (!cpu_state.ccnz << 2) | (!!cpu_state.ccn << 3) |
           (!!cpu_state.ccx << 4);
}

static void reset(unsigned ccr, unsigned bank)
{
    cpu_state.sr = bank == 0 ? 0 : bank == 1 ? 0x2000 : 0x3000;
    cpu_state.ccc = ccr & 1;
    cpu_state.ccv = ccr & 2;
    cpu_state.ccnz = (~ccr) & 4;
    cpu_state.ccn = ccr & 8;
    cpu_state.ccx = ccr & 16;
    cpu_state.usp = 0x6000;
    /* Keep all four stack alignments away from the -32768 callee at 0x8002. */
    cpu_state.isp = 0xb000;
    cpu_state.msp = 0xa000;
    EM_A7 = bank == 0 ? cpu_state.usp : bank == 1 ? cpu_state.isp : cpu_state.msp;
    expected_sr = status();
    trapped = 0;
    expected_stack = EM_A7;
}

static syn68k_addr_t handler(syn68k_addr_t pc, void* argument)
{
    trapped = (unsigned)(uintptr_t)argument;
    if (trapped != 2 || pc != expected_pc)
        fprintf(stderr, "BSR case %u: vector %u PC %08x expected %08x\n", cases, trapped, pc, expected_pc);
    assert(trapped == 2 && pc == expected_pc);
    assert(READUW(EM_A7) == expected_sr);
    assert(READUL(EM_A7 + 2) == expected_pc);
    assert(READUW(EM_A7 + 6) == 0x7008);
    assert(READUL(EM_A7 + 20) == expected_fault);
    assert(cpu_state.usp == expected_stack);
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static syn68k_addr_t cache_probe(syn68k_addr_t pc, void* argument)
{
    unsigned index = cpu_state.jsr_stack_byte_index / sizeof(jsr_stack_elt_t);
    if (cpu_state.jsr_stack[index].code && cpu_state.jsr_stack[index].tag == SWAPUL_IFLE(READUL(EM_A7)))
        ++cache_hits;
    return 0x3000;
}

static void instruction(unsigned size, int32 displacement)
{
    start_pc = expected_pc = 0x10000;
    WRITEUW(start_pc, size == 1 ? 0x6100 | (displacement & 255) : size == 2 ? 0x6100 : 0x61ff);
    unsigned length = size == 1 ? 2 : size + 2;
    if (size == 2)
        WRITEUW(start_pc + 2, displacement);
    else if (size == 4)
        WRITEUL(start_pc + 2, displacement);
    WRITEUW(start_pc + length, 0x4ef9);
    WRITEUL(start_pc + length + 2, MAGIC_EXIT_EMULATOR_ADDRESS);
    uint32 target = start_pc + 2 + displacement;
    WRITEUW(target, 0x2c7c); // MOVEA.L #marker,A6.
    WRITEUL(target + 2, 0xabc);
    WRITEUW(target + 6, 0x4e75);
    destroy_blocks(start_pc, 16);
    destroy_blocks(target, 8);
}

static void run(void)
{
    interpret_code(hash_lookup_code_and_create_if_needed(start_pc));
    ++cases;
}

int main(int argc, char** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    void* memory = VirtualAlloc(NULL, 1024 * 1024, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(1024 * 1024, 0, 0);
    uint32 vectors[64] = {0};
    int native = argc > 1 && atoi(argv[1]);
    initialize_68k_emulator(NULL, native, vectors, 0);
    native = syn68k_native_code_enabled();
    for (unsigned i = 0; i < 64; ++i)
        trap_install_handler(i, handler, (void*)(uintptr_t)i);
    WRITEUW(0x1000, 0x6100);
    WRITEUW(0x1002, 0x1ffe);
    WRITEUW(0x1004, 0x4ef9);
    WRITEUL(0x1006, MAGIC_EXIT_EMULATOR_ADDRESS);
    for (unsigned i = 0; i < 2; ++i) {
        WRITEUW(0x3000 + i * 256, 0x2c7c);
        WRITEUL(0x3002 + i * 256, i + 1);
        WRITEUW(0x3006 + i * 256, 0x4e75);
    }
    reset(31, 0);
    run();
    assert(EM_A6 == 1);
    WRITEUW(0x1002, 0x20fe);
    reset(31, 0);
    run();
    assert(EM_A6 == 2);
    // All useful even byte displacements and signed word/long boundaries in RAM.
    for (int displacement = -128; displacement <= 126; displacement += 2) {
        if (displacement > -12 && displacement < 12)
            continue; // Keep the target outside the call/return instruction storage.
        instruction(1, displacement);
        for (unsigned bank = 0; bank < 3; ++bank)
            for (unsigned align = 0; align < 4; ++align)
                for (unsigned ccr = 0; ccr < 32; ++ccr) {
                    reset(ccr, bank);
                    EM_A7 += align;
                    uint32 stack = EM_A7;
                    run();
                    assert(!trapped && EM_A6 == 0xabc && EM_A7 == stack);
                    assert(status() == expected_sr && READUL(stack - 4) == start_pc + 2);
                }
    }
    const int32 displacements[] = {-32768, -32766, -256, -128, -16, 16, 126, 128, 256, 32766};
    for (unsigned size = 2; size <= 4; size += 2)
        for (unsigned i = 0; i < sizeof displacements / sizeof displacements[0]; ++i) {
            instruction(size, displacements[i]);
            for (unsigned bank = 0; bank < 3; ++bank)
                for (unsigned align = 0; align < 4; ++align)
                    for (unsigned ccr = 0; ccr < 32; ++ccr) {
                        reset(ccr, bank);
                        EM_A7 += align;
                        uint32 stack = EM_A7;
                        run();
                        assert(!trapped && EM_A6 == 0xabc && EM_A7 == stack);
                        assert(status() == expected_sr);
                        assert(READUL(stack - 4) == start_pc + size + 2);
                    }
        }
    instruction(4, 0x80000);
    reset(31, 0);
    run();
    assert(!trapped && EM_A6 == 0xabc && status() == expected_sr);

    // Warm checked-decoder return-cache hits use existing blocks, and
    // invalidation discards them. Cross-page pushes exercise that path even
    // with native dispatch; native calls/returns dispatch by guest PC instead.
    uint32 callback = callback_install(cache_probe, NULL);
    for (unsigned relative = 0; relative < 2; ++relative) {
        start_pc = expected_pc = 0x1000;
        WRITEUW(0x1000, relative ? 0x61ff : 0x4eb9);
        WRITEUL(0x1002, relative ? callback - 0x1002 : callback);
        WRITEUW(0x1006, 0x4ef9);
        WRITEUL(0x1008, MAGIC_EXIT_EMULATOR_ADDRESS);
        WRITEUW(0x3000, 0x4e75);
        destroy_blocks(0x1000, 16);
        destroy_blocks(0x3000, 2);
        cache_hits = 0;
        for (unsigned repeat = 0; repeat < 4; ++repeat) {
            reset(31, 0);
            EM_A7 = 0x6002;
            run();
            assert(!trapped && EM_A7 == 0x6002 && status() == expected_sr);
        }
        assert(cache_hits >= 3);
        unsigned hits = cache_hits;
        assert(destroy_blocks(0x1006, 2) > 0);
        reset(31, 0);
        EM_A7 = 0x6002;
        run();
        assert(cache_hits == hits && EM_A7 == 0x6002 && status() == expected_sr);
        reset(31, 0);
        EM_A7 = 0x6002;
        run();
        assert(cache_hits == hits + 1 && EM_A7 == 0x6002);
    }

    unsigned valid = 0;
    for (unsigned i = 0; i < sizeof cpu_state.jsr_stack / sizeof(jsr_stack_elt_t); ++i)
        valid += cpu_state.jsr_stack[i].tag != UINT32_MAX;
    assert(valid);
    callback_remove(callback); // Uses direct block destruction, not destroy_blocks.
    for (unsigned i = 0; i < sizeof cpu_state.jsr_stack / sizeof(jsr_stack_elt_t); ++i)
        assert(cpu_state.jsr_stack[i].tag == UINT32_MAX);

    DWORD old;
    assert(VirtualProtect((char*)memory + 0xff000, 0x1000, PAGE_NOACCESS, &old));
    syn68k_set_memory_window(0xff000, 0, 0);
    for (unsigned size = 2; size <= 4; size += 2)
        for (unsigned available = 0; available < size; ++available) {
            start_pc = expected_pc = 0xfeffe;
            WRITEUW(start_pc, size == 2 ? 0x6100 : 0x61ff);
            syn68k_set_memory_window(start_pc + 2 + available, 0, 0);
            // Use a byte-truncated declared extent before the protected host page.
            if (available) {
                start_pc = expected_pc = 0xfef00;
                WRITEUW(start_pc, size == 2 ? 0x6100 : 0x61ff);
                syn68k_set_memory_window(start_pc + 2 + available, 0, 0);
            }
            destroy_blocks(start_pc, 8);
            for (unsigned ccr = 0; ccr < 32; ++ccr) {
                reset(ccr, 0);
                expected_fault = start_pc + 2 + available;
                run();
                assert(trapped == 2);
            }
        }
    syn68k_set_memory_window(0xff000, 0, 0);
    for (unsigned available = 0; available < 4; ++available)
        for (unsigned ccr = 0; ccr < 32; ++ccr) {
            instruction(2, 0x1ffe);
            reset(ccr, 0);
            EM_A7 = 0xff004 - available;
            expected_stack = EM_A7 - 4;
            expected_fault = 0xff000;
            for (unsigned i = 0; i < available; ++i)
                WRITEUB(expected_stack + i, 0xa5);
            run();
            assert(trapped == 2);
            for (unsigned i = 0; i < available; ++i)
                assert(READUB(expected_stack + i) == 0xa5);
        }
    printf("%u BSR cases passed (%s)\n", cases, native ? "native" : "interpreter");
    return 0;
}
