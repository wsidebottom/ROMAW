/*+===================================================================
  File:      reset-test.c

  Summary:   Implements reset test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Original RESET privilege/device-bus/state fixtures, Motorola PRM. */
#include "syn68k_public.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned calls, trapped;
static uint32 expected_registers[16];
static uint16 expected_sr;

static uint16 status(void)
{
    return cpu_state.sr | !!cpu_state.ccc | (!!cpu_state.ccv << 1) | (!cpu_state.ccnz << 2) | (!!cpu_state.ccn << 3) |
           (!!cpu_state.ccx << 4);
}

static void reset_bus(void* argument)
{
    assert(argument == &calls && (status() & 0x2000));
    assert(status() == expected_sr);
    for (unsigned i = 0; i < 16; ++i)
        assert(cpu_state.regs[i].ul.n == expected_registers[i]);
    ++calls;
}

static syn68k_addr_t handler(syn68k_addr_t pc, void* argument)
{
    trapped = (unsigned)(uintptr_t)argument;
    assert(trapped == 8 && pc == 0x1000);
    assert(READUW(EM_A7) == expected_sr && READUL(EM_A7 + 2) == pc);
    assert(READUW(EM_A7 + 6) == 32);
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static void prepare(unsigned bank, unsigned mask, unsigned ccr)
{
    expected_sr = (bank == 0 ? 0 : bank == 1 ? 0x2000 : 0x3000) | (mask << 8) | ccr;
    cpu_state.sr = expected_sr & ~31;
    cpu_state.ccc = ccr & 1;
    cpu_state.ccv = ccr & 2;
    cpu_state.ccnz = (~ccr) & 4;
    cpu_state.ccn = ccr & 8;
    cpu_state.ccx = ccr & 16;
    for (unsigned i = 0; i < 16; ++i)
        cpu_state.regs[i].ul.n = 0x87654321u ^ (0x12345678u * (i + ccr + 1));
    cpu_state.usp = 0x7000;
    cpu_state.isp = 0x8000;
    cpu_state.msp = 0x9000;
    EM_A7 = bank == 0 ? 0x7000 : bank == 1 ? 0x8000 : 0x9000;
    cpu_state.sfc = 1;
    cpu_state.dfc = 6;
    cpu_state.cacr = 0x80008000;
    cpu_state.fpu.fpcr = 0x1230;
    cpu_state.fpu.fpsr = 0x01234500;
    cpu_state.fpu.fpiar = 0x6000;
    for (unsigned i = 0; i < 16; ++i)
        expected_registers[i] = cpu_state.regs[i].ul.n;
    trapped = 0;
    WRITEUL(0x4000, 0xdeadbeef);
}

static void execute(unsigned bank, int enabled)
{
    unsigned before = calls;
    interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
    assert(trapped == (bank ? 0 : 8));
    assert(calls == before + (bank && enabled));
    assert(status() == (bank ? expected_sr : expected_sr | 0x2000));
    for (unsigned i = 0; i < 15; ++i)
        assert(cpu_state.regs[i].ul.n == expected_registers[i]);
    assert(EM_A7 == (bank ? expected_registers[15] : 0x7ff8));
    assert(cpu_state.sfc == 1 && cpu_state.dfc == 6 && cpu_state.cacr == 0x80008000);
    assert(cpu_state.fpu.fpcr == 0x1230 && cpu_state.fpu.fpsr == 0x01234500 && cpu_state.fpu.fpiar == 0x6000);
    assert(READUL(0x4000) == 0xdeadbeef);
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
        trap_install_handler(i, handler, (void*)(uintptr_t)i);
    WRITEUW(0x1000, 0x4e70);
    WRITEUW(0x1002, 0x4ef9);
    WRITEUL(0x1004, MAGIC_EXIT_EMULATOR_ADDRESS);
    unsigned count = 0;
    for (unsigned enabled = 0; enabled < 2; ++enabled) {
        if (enabled)
            syn68k_set_reset_handler(reset_bus, &calls);
        for (unsigned bank = 0; bank < 3; ++bank)
            for (unsigned mask = 0; mask < 8; ++mask)
                for (unsigned ccr = 0; ccr < 32; ++ccr) {
                    prepare(bank, mask, ccr);
                    execute(bank, enabled);
                    ++count;
                }
    }
    // Changing a host device hook must not require discarding cached opcodes.
    syn68k_set_reset_handler(NULL, &calls);
    prepare(1, 0, 31);
    execute(1, 0);
    syn68k_set_reset_handler(reset_bus, &calls);
    prepare(2, 7, 31);
    execute(2, 1);
    // Native producers must be visible at the reset bus and preserve their CCR.
    destroy_blocks(0x1000, 32);
    WRITEUW(0x1000, 0x203c);
    WRITEUL(0x1002, 0x7fffffff);
    WRITEUW(0x1006, 0x5280);
    WRITEUW(0x1008, 0x4e70);
    WRITEUW(0x100a, 0x4ef9);
    WRITEUL(0x100c, MAGIC_EXIT_EMULATOR_ADDRESS);
    prepare(1, 0, 0x15);
    expected_sr = 0x200a;
    expected_registers[0] = 0x80000000;
    execute(1, 1);
    syn68k_set_reset_handler(NULL, NULL);
    printf("%u RESET privilege/CCR/register/bank/device cases and native chain passed (%s)\n", count + 3,
           native ? "native" : "interpreter");
    VirtualFree(memory, 0, MEM_RELEASE);
}
