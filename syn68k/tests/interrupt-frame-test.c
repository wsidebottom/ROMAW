/*+===================================================================
  File:      interrupt-frame-test.c

  Summary:   Implements interrupt frame test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Original interrupt/RTE fixtures for the 68040's three stack banks. */
#include "syn68k_public.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* Internal exception entry, also called by the interrupt dispatcher. */
extern uint32 trap_direct(uint32 vector, uint32 pc, uint32 address);
static unsigned expected_vector, master;
static uint16 original_sr, active_sr;
static int roundtrip;
static unsigned magic_interrupts;

static syn68k_addr_t magic_interrupt(syn68k_addr_t pc, void* argument)
{
    (void)argument;
    assert(pc == MAGIC_EXIT_EMULATOR_ADDRESS);
    ++magic_interrupts;
    return MAGIC_RTE_ADDRESS;
}

static uint16 status(void)
{
    return cpu_state.sr | !!cpu_state.ccc | (!!cpu_state.ccv << 1) | (!cpu_state.ccnz << 2) | (!!cpu_state.ccn << 3) |
           (!!cpu_state.ccx << 4);
}

static void frame(uint32 stack, unsigned format, uint16 sr)
{
    assert(READUW(stack) == sr && READUL(stack + 2) == 0x1000);
    assert(READUW(stack + 6) == (format << 12) + expected_vector * 4);
}

static syn68k_addr_t handler(syn68k_addr_t pc, void* argument)
{
    assert((uintptr_t)argument == expected_vector && pc == 0x1000);
    assert(EM_A7 == 0x7ff8 && status() == active_sr);
    if (master && cpu_state.msp != 0x8ff8)
        fprintf(stderr, "Master IRQ %u: MSP=%08x, expected 00008ff8\n", expected_vector, cpu_state.msp);
    assert(cpu_state.msp == (master ? 0x8ff8 : 0x9000));
    frame(0x7ff8, master ? 1 : 0, original_sr);
    if (master)
        frame(0x8ff8, 0, original_sr);
    return roundtrip ? 0x2000 : MAGIC_EXIT_EMULATOR_ADDRESS;
}

static syn68k_addr_t nested_handler(syn68k_addr_t pc, void* argument)
{
    unsigned vector = (unsigned)(uintptr_t)argument;
    unsigned ccr = original_sr & 31;
    assert(cpu_state.msp == 0x8ff8);
    if (vector == 31) {
        assert(pc == 0x3000 && EM_A7 == 0x7ff0);
        assert(status() == (0x2700 | ccr));
        assert(READUW(0x7ff0) == (0x2300 | ccr));
        assert(READUL(0x7ff2) == pc && READUW(0x7ff6) == 31 * 4);
    }
    else {
        assert(vector == 27 && pc == 0x1000 && EM_A7 == 0x7ff8);
        assert(status() == (0x2300 | ccr));
        expected_vector = 27;
        frame(0x8ff8, 0, original_sr);
        frame(0x7ff8, 1, original_sr);
        uint32 next = trap_direct(31, 0x3000, 0);
        assert(next == 0x2000);
        interpret_code(hash_lookup_code_and_create_if_needed(next));
        assert(EM_A7 == 0x7ff8 && status() == (0x2300 | ccr));
        frame(0x8ff8, 0, original_sr);
        frame(0x7ff8, 1, original_sr);
    }
    return 0x2000;
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
    /* Pending interrupts obtain the next guest PC from the compiled block's
     pointer-sized prefix, including the two immortal synthetic blocks. */
    const uint16* exit_code = hash_lookup_code_and_create_if_needed(MAGIC_EXIT_EMULATOR_ADDRESS);
    const uint16* rte_code = hash_lookup_code_and_create_if_needed(MAGIC_RTE_ADDRESS);
    size_t prefix_words = sizeof(void*) / sizeof(uint16);
    assert(READUL_US(exit_code - prefix_words) == MAGIC_EXIT_EMULATOR_ADDRESS);
    assert(READUL_US(rte_code - prefix_words) == MAGIC_RTE_ADDRESS);
    /* The second queued IRQ lands between RTE and the synthetic exit. */
    trap_install_handler(26, magic_interrupt, NULL);
    trap_install_handler(27, magic_interrupt, NULL);
    cpu_state.sr = 0x2000;
    cpu_state.isp = EM_A7 = 0x8000;
    cpu_state.interrupt_pending[2] = cpu_state.interrupt_pending[3] = 1;
    uint32 first = interrupt_process_any_pending(MAGIC_EXIT_EMULATOR_ADDRESS);
    interpret_code(hash_lookup_code_and_create_if_needed(first));
    assert(magic_interrupts == 2 && EM_A7 == 0x8000);
    assert(!cpu_state.interrupt_pending[2] && !cpu_state.interrupt_pending[3]);
    for (unsigned i = 25; i <= 31; ++i)
        trap_install_handler(i, handler, (void*)(uintptr_t)i);
    WRITEUW(0x1000, 0x4ef9);
    WRITEUL(0x1002, MAGIC_EXIT_EMULATOR_ADDRESS);
    WRITEUW(0x2000, 0x4e73);
    unsigned count = 0;
    for (roundtrip = 0; roundtrip < 2; ++roundtrip)
        for (unsigned bank = 0; bank < 3; ++bank)
            for (unsigned old_mask = 0; old_mask < 8; ++old_mask)
                for (unsigned level = 1; level <= 7; ++level)
                    for (unsigned ccr = 0; ccr < 32; ++ccr)
                        for (unsigned trace = 0; trace < 4; ++trace) {
                            master = bank == 2;
                            original_sr = (bank == 0   ? 0
                                           : bank == 1 ? 0x2000
                                                       : 0x3000) |
                                          (old_mask << 8) | (trace << 14) | ccr;
                            active_sr = 0x2000 | (level << 8) | ccr;
                            cpu_state.sr = original_sr & ~31;
                            cpu_state.ccc = ccr & 1;
                            cpu_state.ccv = ccr & 2;
                            cpu_state.ccnz = (~ccr) & 4;
                            cpu_state.ccn = ccr & 8;
                            cpu_state.ccx = ccr & 16;
                            cpu_state.usp = 0x7000;
                            cpu_state.isp = 0x8000;
                            cpu_state.msp = 0x9000;
                            uint32 stack = bank == 0 ? 0x7000 : bank == 1 ? 0x8000 : 0x9000;
                            EM_A7 = stack;
                            WRITEUL(0x8ff8, 0xdeadbeef);
                            expected_vector = 24 + level;
                            uint32 next = trap_direct(expected_vector, 0x1000, 0);
                            if (roundtrip) {
                                assert(next == 0x2000);
                                interpret_code(hash_lookup_code_and_create_if_needed(next));
                                assert(EM_A7 == stack && status() == original_sr);
                                if (master || bank == 0)
                                    assert(cpu_state.isp == 0x8000);
                            }
                            else
                                assert(next == MAGIC_EXIT_EMULATOR_ADDRESS);
                            if (!bank)
                                assert(cpu_state.usp == 0x7000);
                            ++count;
                        }
    /* A nested level-7 frame unwinds before the outer ISP/MSP pair. */
    for (unsigned i = 25; i <= 31; ++i)
        WRITEUL(0x4000 + i * 4, cpu_state.trap_handler_info[i].callback_address);
    cpu_state.vbr = 0x4000;
    trap_install_handler(27, nested_handler, (void*)(uintptr_t)27);
    trap_install_handler(31, nested_handler, (void*)(uintptr_t)31);
    WRITEUW(0x3000, 0x4ef9);
    WRITEUL(0x3002, MAGIC_EXIT_EMULATOR_ADDRESS);
    for (unsigned ccr = 0; ccr < 32; ++ccr) {
        original_sr = 0x3000 | ccr;
        cpu_state.sr = 0x3000;
        cpu_state.ccc = ccr & 1;
        cpu_state.ccv = ccr & 2;
        cpu_state.ccnz = (~ccr) & 4;
        cpu_state.ccn = ccr & 8;
        cpu_state.ccx = ccr & 16;
        cpu_state.usp = 0x7000;
        cpu_state.isp = 0x8000;
        cpu_state.msp = EM_A7 = 0x9000;
        uint32 next = trap_direct(27, 0x1000, 0);
        assert(next == 0x2000);
        interpret_code(hash_lookup_code_and_create_if_needed(next));
        assert(EM_A7 == 0x9000 && status() == original_sr);
        assert(cpu_state.isp == 0x8000);
        ++count;
    }
    printf("%u 68040 interrupt frame/mask/CCR/bank/RTE cases passed (%s)\n", count, native ? "native" : "interpreter");
    VirtualFree(memory, 0, MEM_RELEASE);
}
