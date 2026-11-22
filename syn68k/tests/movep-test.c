/*+===================================================================
  File:      movep-test.c

  Summary:   Implements movep test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Original MOVEP live-displacement and sparse byte-transfer fixtures. */
#include "syn68k_public.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned cases, trapped;
static uint32 expected_pc = 0x1000, expected_fault;
static uint16 expected_sr;

static uint16 status(void)
{
    return cpu_state.sr | !!cpu_state.ccc | (!!cpu_state.ccv << 1) | (!cpu_state.ccnz << 2) | (!!cpu_state.ccn << 3) |
           (!!cpu_state.ccx << 4);
}

static void reset(unsigned ccr)
{
    cpu_state.sr = 0;
    cpu_state.ccc = ccr & 1;
    cpu_state.ccv = ccr & 2;
    cpu_state.ccnz = (~ccr) & 4;
    cpu_state.ccn = ccr & 8;
    cpu_state.ccx = ccr & 16;
    cpu_state.usp = EM_A7 = 0x6000;
    cpu_state.isp = 0x8000;
    cpu_state.msp = 0xa000;
    for (unsigned i = 0; i < 8; ++i)
        EM_DREG(i) = 0xa1b2c3d4;
    expected_sr = status();
    trapped = 0;
}

static syn68k_addr_t handler(syn68k_addr_t pc, void* argument)
{
    trapped = (unsigned)(uintptr_t)argument;
    assert(trapped == 2 && pc == expected_pc);
    assert(READUW(EM_A7) == expected_sr);
    assert(READUL(EM_A7 + 2) == expected_pc);
    assert(READUW(EM_A7 + 6) == 0x7008);
    assert(READUL(EM_A7 + 20) == expected_fault);
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static void instruction(unsigned data, unsigned address, unsigned wide, unsigned writing, int displacement)
{
    WRITEUW(0x1000, 0x0108 | (data << 9) | address | (wide << 6) | (writing << 7));
    WRITEUW(0x1002, displacement);
    WRITEUW(0x1004, 0x4ef9);
    WRITEUL(0x1006, MAGIC_EXIT_EMULATOR_ADDRESS);
    destroy_blocks(0x1000, 16);
}

static void run(uint32 pc)
{
    interpret_code(hash_lookup_code_and_create_if_needed(pc));
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

    // A cached displacement must be fetched again without explicit invalidation.
    instruction(0, 0, 0, 0, 0);
    WRITEUB(0x3000, 0x12);
    WRITEUB(0x3002, 0x34);
    WRITEUB(0x3010, 0x56);
    WRITEUB(0x3012, 0x78);
    for (unsigned edit = 0; edit < 2; ++edit) {
        WRITEUW(0x1002, edit ? 16 : 0);
        reset(31);
        EM_A0 = 0x3000;
        run(0x1000);
        assert(EM_D0 == (edit ? 0xa1b25678 : 0xa1b21234));
    }

    const int offsets[] = {-32768, -129, -1, 0, 1, 128, 32767};
    for (unsigned data = 0; data < 8; ++data)
        for (unsigned address = 0; address < 8; ++address)
            for (unsigned wide = 0; wide < 2; ++wide)
                for (unsigned writing = 0; writing < 2; ++writing)
                    for (unsigned offset = 0; offset < sizeof offsets / sizeof offsets[0]; ++offset) {
                        unsigned count = wide ? 4 : 2;
                        instruction(data, address, wide, writing, offsets[offset]);
                        for (unsigned ccr = 0; ccr < 32; ++ccr)
                            for (unsigned alignment = 0; alignment < 2; ++alignment) {
                                reset(ccr);
                                uint32 base = 0x20000 + alignment;
                                EM_AREG(address) = base - offsets[offset];
                                for (unsigned i = 0; i < 9; ++i)
                                    WRITEUB(base + i, 0x5a);
                                if (!writing)
                                    for (unsigned i = 0; i < count; ++i)
                                        WRITEUB(base + i * 2, 0x12 + i * 0x22);
                                uint32 original[16];
                                for (unsigned i = 0; i < 16; ++i)
                                    original[i] = cpu_state.regs[i].ul.n;
                                run(0x1000);
                                for (unsigned i = 0; i < 16; ++i)
                                    if (i != data)
                                        assert(cpu_state.regs[i].ul.n == original[i]);
                                assert(!trapped && status() == expected_sr);
                                assert(EM_AREG(address) == base - offsets[offset]);
                                for (unsigned i = 0; i < 9; ++i)
                                    assert(READUB(base + i) ==
                                           (i < count * 2 && !(i & 1)
                                                ? (writing ? (0xa1b2c3d4u >> ((count - 1 - i / 2) * 8)) & 255
                                                           : 0x12 + i / 2 * 0x22)
                                                : 0x5a));
                                assert(EM_DREG(data) == (writing ? 0xa1b2c3d4u : wide ? 0x12345678u : 0xa1b21234u));
                            }
                    }

    // Arithmetic flags remain live even if the successor replaces them.
    instruction(1, 0, 1, 0, 0);
    WRITEUW(0x0ffe, 0x5280); // ADDQ.L #1,D0
    WRITEUW(0x1004, 0x7200); // MOVEQ #0,D1
    WRITEUW(0x1006, 0x4ef9);
    WRITEUL(0x1008, MAGIC_EXIT_EMULATOR_ADDRESS);
    destroy_blocks(0x0ffe, 32);
    reset(0);
    EM_D0 = 0xffffffff;
    EM_A0 = 0x3000;
    run(0x0ffe);
    assert((status() & 31) == 0x14); // MOVEQ preserves arithmetic X.

    DWORD old;
    assert(VirtualProtect((char*)memory + 0xff000, 0x1000, PAGE_NOACCESS, &old));
    for (unsigned wide = 0; wide < 2; ++wide)
        for (unsigned writing = 0; writing < 2; ++writing)
            for (unsigned valid = 0; valid < (wide ? 4 : 2); ++valid)
                for (unsigned ccr = 0; ccr < 32; ++ccr) {
                    syn68k_set_memory_window(0xff000, 0, 0);
                    instruction(0, 0, wide, writing, 0);
                    reset(ccr);
                    EM_A0 = 0xff000 - valid * 2;
                    for (unsigned i = 0; i < valid * 2; ++i)
                        WRITEUB(EM_A0 + i, 0x5a);
                    expected_fault = 0xff000;
                    run(0x1000);
                    assert(trapped == 2 && EM_D0 == 0xa1b2c3d4);
                    // Each completed sparse byte write survives a later byte fault.
                    for (unsigned i = 0; i < valid * 2; ++i)
                        assert(
                            READUB(EM_A0 + i) ==
                            (writing && !(i & 1) ? (0xa1b2c3d4u >> (((wide ? 4 : 2) - 1 - i / 2) * 8)) & 255 : 0x5a));
                }

    // An inaccessible gap after the final sparse byte is never touched.
    for (unsigned wide = 0; wide < 2; ++wide)
        for (unsigned writing = 0; writing < 2; ++writing) {
            unsigned count = wide ? 4 : 2;
            syn68k_set_memory_window(0xff000, 0, 0);
            instruction(0, 0, wide, writing, 0);
            reset(31);
            EM_A0 = 0xff000 - count * 2 + 1;
            for (unsigned i = 0; i < count; ++i)
                WRITEUB(EM_A0 + i * 2, 0x12 + i * 0x22);
            run(0x1000);
            assert(!trapped && status() == expected_sr);
            assert(EM_D0 == (writing ? 0xa1b2c3d4u : wide ? 0x12345678u : 0xa1b21234u));
        }

    // Native-produced flags and a cached address offset survive the fault.
    syn68k_set_memory_window(0xff000, 0, 0);
    instruction(1, 0, 1, 0, 0);
    WRITEUW(0x0ffc, 0x5888); // ADDQ.L #4,A0
    WRITEUW(0x0ffe, 0x5280); // ADDQ.L #1,D0
    WRITEUW(0x1004, 0x7200); // Successor replaces flags only on success.
    destroy_blocks(0x0ffc, 32);
    reset(0);
    EM_D0 = 0xffffffff;
    EM_A0 = 0xff000 - 4;
    expected_sr = 0x15;
    expected_fault = 0xff000;
    run(0x0ffc);
    assert(trapped == 2 && EM_A0 == 0xff000 && EM_D1 == 0xa1b2c3d4);

    // Displacement fetches fault before any byte transfer or register update.
    for (unsigned available = 0; available < 2; ++available)
        for (unsigned ccr = 0; ccr < 32; ++ccr) {
            uint32 pc = available ? 0xfef00 : 0xfeffe;
            WRITEUW(pc, 0x01c8);
            syn68k_set_memory_window(pc + 2 + available, 0, 0);
            destroy_blocks(pc, 8);
            reset(ccr);
            EM_A0 = 0x3000;
            WRITEUL(0x3000, 0x5a5a5a5a);
            expected_pc = pc;
            expected_fault = pc + 2 + available;
            run(pc);
            assert(trapped == 2 && READUL(0x3000) == 0x5a5a5a5a);
        }
    printf("%u MOVEP cases passed (%s)\n", cases, native ? "native" : "interpreter");
    return 0;
}
