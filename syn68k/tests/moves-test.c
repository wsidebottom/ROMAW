/*+===================================================================
  File:      moves-test.c

  Summary:   Implements moves test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Original MOVES instruction fixtures, M68000PRM 6-24..26 / MC68040UM 3-22. */
#include "syn68k_public.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static uint32 pc = 0x1000, fault_address;
static uint16 original_sr;
static unsigned trapped;

static syn68k_addr_t handler(syn68k_addr_t address, void* argument)
{
    trapped = (unsigned)(uintptr_t)argument;
    assert(address == pc && READUL(EM_A7 + 2) == pc);
    assert(READUW(EM_A7) == original_sr);
    assert(READUW(EM_A7 + 6) == (trapped == 2 ? 0x7008 : trapped * 4));
    if (trapped == 2)
        assert(READUL(EM_A7 + 20) == fault_address);
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static uint16 status(void)
{
    return cpu_state.sr | !!cpu_state.ccc | (!!cpu_state.ccv << 1) | (!cpu_state.ccnz << 2) | (!!cpu_state.ccn << 3) |
           (!!cpu_state.ccx << 4);
}

static void reset(unsigned sr)
{
    original_sr = sr;
    cpu_state.sr = sr & ~31;
    cpu_state.ccc = sr & 1;
    cpu_state.ccv = sr & 2;
    cpu_state.ccnz = (~sr) & 4;
    cpu_state.ccn = sr & 8;
    cpu_state.ccx = sr & 16;
    for (unsigned i = 0; i < 16; ++i)
        cpu_state.regs[i].ul.n = 0x89abcdef;
    EM_D1 = 4;
    EM_A0 = 0x3000;
    cpu_state.usp = 0x8000;
    cpu_state.isp = EM_A7 = 0x9000;
    cpu_state.sfc = cpu_state.dfc = 1;
    trapped = 0;
    fault_address = 0x100000;
}

static void program(unsigned ea, unsigned size, unsigned extension, const uint16* words, unsigned count)
{
    pc = 0x1000;
    destroy_blocks(pc, 32);
    WRITEUW(pc, 0x0e00 | (size << 6) | ea);
    WRITEUW(pc + 2, extension);
    for (unsigned i = 0; i < count; ++i)
        WRITEUW(pc + 4 + i * 2, words[i]);
    WRITEUW(pc + 4 + count * 2, 0x4ef9);
    WRITEUL(pc + 6 + count * 2, MAGIC_EXIT_EMULATOR_ADDRESS);
}

static void execute(void)
{
    interpret_code(hash_lookup_code_and_create_if_needed(pc));
}

static void privilege(void)
{
    for (unsigned size = 0; size < 3; ++size)
        for (unsigned ea = 0; ea < 64; ++ea) {
            reset(31);
            EM_A7 = cpu_state.usp;
            uint16 words[] = {0, 0};
            program(ea, size, 0x0800, words, 2);
            execute();
            uint64 valid = UINT64_C(0x03ffffffffff0000);
            assert(trapped == ((valid & (UINT64_C(1) << ea)) ? 8 : 4));
            assert(EM_A0 == 0x3000 && cpu_state.usp == 0x8000);
            assert(status() == 0x201f);
        }
    for (unsigned bit = 0; bit < 11; ++bit) {
        reset(0x201f);
        program(0x18, 2, 0x0800 | (1u << bit), NULL, 0);
        WRITEUL(0x3000, 0x12345678);
        execute();
        assert(trapped == 4 && EM_A0 == 0x3000);
        assert(READUL(0x3000) == 0x12345678 && status() == 0x201f);
    }
    for (unsigned supervisor = 0; supervisor < 2; ++supervisor) {
        reset(supervisor ? 0x201f : 31);
        if (!supervisor)
            EM_A7 = cpu_state.usp;
        pc = 0xffffe;
        destroy_blocks(pc, 2);
        WRITEUW(pc, 0x0e90);
        execute();
        assert(trapped == (supervisor ? 2 : 8));
        assert(EM_A0 == 0x3000);
    }
}

static unsigned transfers(void)
{
    const struct {
        unsigned ea, count;
        uint16 words[3];
        uint32 address, pointer;
    } cases[] = {
        {0x10, 0, {0}, 0x3000, 0},
        {0x18, 0, {0}, 0x3000, 0},
        {0x20, 0, {0}, 0x3000, 0},
        {0x28, 1, {16}, 0x3010, 0},
        {0x30, 1, {0x180c}, 0x3010, 0},
        {0x30, 3, {0x0170, 0, 16}, 0x3010, 0},
        {0x30, 2, {0x01e0, 0x4000}, 0x4000, 0},
        {0x30, 3, {0x1922, 16, 4}, 0x5004, 0x3014},
        {0x30, 3, {0x1926, 16, 4}, 0x5008, 0x3010},
        {0x38, 1, {0x4000}, 0x4000, 0},
        {0x39, 2, {0, 0x4001}, 0x4001, 0},
        {0x1f, 0, {0}, 0x9000, 0},
        {0x27, 0, {0}, 0x9000, 0},
    };

    const unsigned functions[] = {1, 2, 5, 6};
    unsigned count = 0;
    for (unsigned sz = 0; sz < 3; ++sz)
        for (unsigned writing = 0; writing < 2; ++writing)
            for (unsigned reg = 0; reg < 16; ++reg)
                for (unsigned ccr = 0; ccr < 32; ++ccr)
                    for (unsigned c = 0; c < sizeof cases / sizeof *cases; ++c) {
                        unsigned size = 1u << sz;
                        reset(0x2000 | ccr);
                        unsigned fc = functions[(c + reg + ccr) % 4];
                        /* An unused function-code register must not affect the access. */
                        cpu_state.sfc = writing ? 7 : fc;
                        cpu_state.dfc = writing ? fc : 7;
                        program(cases[c].ea, sz, (reg << 12) | (writing << 11), cases[c].words, cases[c].count);
                        uint32 address = cases[c].address;
                        unsigned ea_mode = cases[c].ea >> 3, ea_reg = cases[c].ea & 7;
                        unsigned step = size == 1 && ea_reg == 7 ? 2 : size;
                        if (ea_mode == 4)
                            address -= step;
                        if (cases[c].pointer)
                            WRITEUL(cases[c].pointer, 0x5000);
                        const uint32 values[] = {0, 0x12345678, 0x89abcdef, UINT32_MAX};
                        uint32 value = values[(reg + ccr + c) % 4];
                        WRITEUL(address, value);
                        WRITEUB(address - 1, 0x5a);
                        WRITEUB(address + size, 0xa5);
                        uint32 before[16];
                        for (unsigned i = 0; i < 16; ++i)
                            before[i] = cpu_state.regs[i].ul.n;
                        uint32 expected[16];
                        for (unsigned i = 0; i < 16; ++i)
                            expected[i] = before[i];
                        if (ea_mode == 3)
                            expected[8 + ea_reg] += step;
                        if (ea_mode == 4)
                            expected[8 + ea_reg] -= step;
                        uint32 mask = size == 4 ? UINT32_MAX : (1u << (size * 8)) - 1;
                        uint32 input = value >> ((4 - size) * 8);
                        uint32 output = expected[reg] & mask;
                        if (!writing)
                            expected[reg] = reg < 8   ? (expected[reg] & ~mask) | input
                                            : sz == 0 ? (int8)input
                                            : sz == 1 ? (int16)input
                                                      : input;
                        execute();
                        assert(!trapped && status() == original_sr);
                        for (unsigned i = 0; i < 16; ++i)
                            assert(cpu_state.regs[i].ul.n == expected[i]);
                        uint32 observed = 0;
                        for (unsigned i = 0; i < size; ++i)
                            observed = (observed << 8) | READUB(address + i);
                        assert(observed == (writing ? output : input));
                        assert(READUB(address - 1) == 0x5a);
                        assert(READUB(address + size) == 0xa5);
                        ++count;
                    }
    return count;
}

static void faults(void)
{
    for (unsigned writing = 0; writing < 2; ++writing)
        for (unsigned fc = 0; fc < 8; ++fc) {
            reset(0x201f);
            program(0x10, 2, writing << 11, NULL, 0);
            cpu_state.sfc = cpu_state.dfc = fc;
            fault_address = 0x3000;
            WRITEUL(0x3000, 0x12345678);
            execute();
            int supported = fc == 1 || fc == 2 || fc == 5 || fc == 6;
            assert(trapped == (supported ? 0 : 2));
            assert(status() == 0x201f);
            if (!supported)
                assert(EM_D0 == 0x89abcdef && READUL(0x3000) == 0x12345678);
        }
    for (unsigned writing = 0; writing < 2; ++writing)
        for (unsigned size = 1; size < 3; ++size) {
            reset(0x201f);
            program(0x10, size, writing << 11, NULL, 0);
            EM_A0 = 0xfffff;
            WRITEUB(0xfffff, 0x55);
            execute();
            assert(trapped == 2 && EM_D0 == 0x89abcdef);
            assert(READUB(0xfffff) == 0x55 && EM_A0 == 0xfffff);
        }
    /* The runtime extension is read afresh, independent of the cached opcode. */
    reset(0x201f);
    program(0x10, 2, 0x0800, NULL, 0);
    execute();
    assert(!trapped && READUL(0x3000) == 0x89abcdef);
    EM_D0 = 0;
    WRITEUW(0x1002, 0x0000);
    execute();
    assert(!trapped && EM_D0 == 0x89abcdef);
    WRITEUW(0x1002, 0x0001);
    execute();
    assert(trapped == 4 && EM_D0 == 0x89abcdef);

    /* Real native producers/consumers and MOVEC-selected destination space. */
    reset(0x201f);
    destroy_blocks(0x1000, 40);
    const uint16 chain[] = {
        0x203c, 0x1234, 0x5678, /* MOVE.L #$12345678,D0 */
        0x207c, 0x0000, 0x4000, /* MOVEA.L #$4000,A0 */
        0x7201,                 /* MOVEQ #1,D1 */
        0x4e7b, 0x1001,         /* MOVEC D1,DFC */
        0x0e98, 0x0800,         /* MOVES.L D0,(A0)+ */
        0x0ea0, 0x2000,         /* MOVES.L -(A0),D2 */
        0x2602,                 /* MOVE.L D2,D3 */
        0x4ef9, 0,      0,
    };
    for (unsigned i = 0; i < sizeof chain / sizeof *chain; ++i)
        WRITEUW(0x1000 + i * 2, chain[i]);
    WRITEUL(0x101e, MAGIC_EXIT_EMULATOR_ADDRESS);
    execute();
    assert(!trapped && READUL(0x4000) == 0x12345678);
    assert(EM_A0 == 0x4000 && EM_D2 == 0x12345678 && EM_D3 == EM_D2);
    assert(cpu_state.dfc == 1 && status() == 0x2010);

    /* Size 11 in the same opcode page is CAS.L/CAS2.L, not MOVES. */
    reset(0x201f);
    destroy_blocks(0x1000, 32);
    WRITEUW(0x1000, 0x0ed0);
    WRITEUW(0x1002, 0x0081);
    WRITEUW(0x1004, 0x4ef9);
    WRITEUL(0x1006, MAGIC_EXIT_EMULATOR_ADDRESS);
    EM_D1 = 0x12345678;
    EM_D2 = 0xabcdef01;
    WRITEUL(0x3000, EM_D1);
    execute();
    assert(!trapped && READUL(0x3000) == EM_D2 && status() == 0x2014);
    reset(0x201f);
    destroy_blocks(0x1000, 32);
    WRITEUW(0x1000, 0x0efc);
    WRITEUW(0x1002, 0x8081);
    WRITEUW(0x1004, 0x9103);
    WRITEUW(0x1006, 0x4ef9);
    WRITEUL(0x1008, MAGIC_EXIT_EMULATOR_ADDRESS);
    EM_A1 = 0x4000;
    EM_D1 = 0x12345678;
    EM_D2 = 0xabcdef01;
    EM_D3 = 0x89abcdef;
    EM_D4 = 0x12345678;
    WRITEUL(0x3000, EM_D1);
    WRITEUL(0x4000, EM_D3);
    execute();
    assert(!trapped && READUL(0x3000) == EM_D2 && READUL(0x4000) == EM_D4);
    assert(status() == 0x2014);
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
    privilege();
    unsigned count = transfers();
    faults();
    printf("%u MOVES register/EA/CCR/function-code transfers passed (%s).\n", count, native ? "native" : "interpreter");
    VirtualFree(memory, 0, MEM_RELEASE);
    return 0;
}
