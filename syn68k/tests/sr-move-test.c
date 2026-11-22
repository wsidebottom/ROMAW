/*+===================================================================
  File:      sr-move-test.c

  Summary:   Implements sr move test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Original MOVE SR words and EA fixtures from the Motorola PRM. */
#include "syn68k_public.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static uint32 pc = 0x1000;
static uint16 original_sr;
static unsigned trapped;

static syn68k_addr_t handler(syn68k_addr_t address, void* argument)
{
    trapped = (unsigned)(uintptr_t)argument;
    assert(address == pc && READUL(EM_A7 + 2) == pc);
    assert(READUW(EM_A7) == original_sr);
    assert(READUW(EM_A7 + 6) == (trapped == 2 ? 0x7008 : trapped * 4));
    if (trapped == 2)
        assert(READUL(EM_A7 + 20) == 0x100000);
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static uint16 status(void)
{
    return cpu_state.sr | !!cpu_state.ccc | (!!cpu_state.ccv << 1) | (!cpu_state.ccnz << 2) | (!!cpu_state.ccn << 3) |
           (!!cpu_state.ccx << 4);
}

static void reset(uint16 sr)
{
    original_sr = sr;
    cpu_state.sr = sr & ~31;
    cpu_state.ccc = sr & 1;
    cpu_state.ccv = sr & 2;
    cpu_state.ccnz = (~sr) & 4;
    cpu_state.ccn = sr & 8;
    cpu_state.ccx = sr & 16;
    for (unsigned i = 0; i < 8; ++i)
        cpu_state.regs[i].ul.n = 0xdead0000 | i;
    for (unsigned i = 0; i < 7; ++i)
        EM_AREG(i) = 0x2000;
    EM_D1 = 4;
    cpu_state.usp = 0xfe000;
    cpu_state.isp = 0xff000;
    cpu_state.msp = 0xfd000;
    EM_A7 = sr & 0x2000 ? 0xff000 : 0xfe000;
    trapped = 0;
}

static void program(unsigned opcode, const uint16* extensions, unsigned words)
{
    pc = 0x1000;
    destroy_blocks(pc, 32);
    WRITEUW(pc, opcode);
    for (unsigned i = 0; i < words; ++i)
        WRITEUW(pc + 2 + i * 2, extensions[i]);
    WRITEUW(pc + 2 + words * 2, 0x4ef9);
    WRITEUL(pc + 4 + words * 2, MAGIC_EXIT_EMULATOR_ADDRESS);
}

static void execute(void)
{
    interpret_code(hash_lookup_code_and_create_if_needed(pc));
}

static void privilege(void)
{
    for (unsigned from = 0; from < 2; ++from)
        for (unsigned ea = 0; ea < 64; ++ea) {
            const uint16 extensions[] = {0, 0, 0};
            program((from ? 0x40c0 : 0x46c0) | ea, extensions, 3);
            reset(0x001f);
            WRITEUW(0x2000, 0x201f);
            execute();
            uint64 valid = from ? UINT64_C(0x03ffffffffff00ff) : UINT64_C(0x1fffffffffff00ff);
            assert(trapped == ((valid & (UINT64_C(1) << ea)) ? 8 : 4));
            assert(status() == 0x201f && cpu_state.usp == 0xfe000);
            for (unsigned i = 0; i < 7; ++i)
                assert(EM_AREG(i) == 0x2000);
            assert(READUW(0x2000) == 0x201f && EM_D0 == 0xdead0000);
        }
    /* Runtime extensions are not in the cached block: privilege must win. */
    const uint16 controls[] = {0x40f9, 0x46f9, 0x027c, 0x007c, 0x0a7c, 0x4e7a, 0x4e7b};
    for (unsigned i = 0; i < sizeof controls / sizeof *controls; ++i)
        for (unsigned supervisor = 0; supervisor < 2; ++supervisor) {
            pc = 0xffffe;
            destroy_blocks(pc, 2);
            WRITEUW(pc, controls[i]);
            reset(supervisor ? 0x201f : 0x001f);
            execute();
            assert(trapped == (supervisor ? 2 : 8));
        }
}

static void transfers(void)
{
    const struct {
        unsigned ea, count;
        uint16 extension[3];
        uint32 base, address;
        int delta;
        unsigned pointer, source_only;
    } cases[] = {{0x00, 0, {0}, 0x2000, 0, 0, 0, 0},
                 {0x10, 0, {0}, 0x2000, 0x2000, 0, 0, 0},
                 {0x18, 0, {0}, 0x2000, 0x2000, 2, 0, 0},
                 {0x20, 0, {0}, 0x2002, 0x2000, -2, 0, 0},
                 {0x28, 1, {16}, 0x2000, 0x2010, 0, 0, 0},
                 {0x30, 1, {0x100c}, 0x2000, 0x2010, 0, 0, 0},
                 {0x30, 3, {0x1930, 0, 16}, 0x2000, 0x2014, 0, 0, 0},
                 {0x30, 3, {0x19b0, 0, 0x1ffc}, 0x2000, 0x2000, 0, 0, 0},
                 {0x30, 1, {0x1911}, 0x2100, 0x2000, 0, 1, 0},
                 {0x30, 1, {0x1915}, 0x2100, 0x2000, 0, 2, 0},
                 {0x38, 1, {0x2000}, 0x2000, 0x2000, 0, 0, 0},
                 {0x39, 2, {0, 0x2000}, 0x2000, 0x2000, 0, 0, 0},
                 {0x3a, 1, {0xffe}, 0x2000, 0x2000, 0, 0, 1},
                 {0x3b, 3, {0x19b0, 0, 0x1ffc}, 0x2000, 0x2000, 0, 0, 1},
                 {0x3c, 1, {0}, 0x2000, 0x1002, 0, 0, 1}};

    const uint16 values[] = {0, 0x001f, 0x2000, 0x201f, 0x3015, 0x28ff};
    unsigned count = 0;
    for (unsigned from = 0; from < 2; ++from)
        for (unsigned i = 0; i < sizeof cases / sizeof *cases; ++i)
            for (unsigned value = 0; value < sizeof values / sizeof *values; ++value)
                for (unsigned ccr = 0; ccr < 32; ++ccr) {
                    if (from && cases[i].source_only)
                        continue;
                    program((from ? 0x40c0 : 0x46c0) | cases[i].ea, cases[i].extension, cases[i].count);
                    reset(0x2000 | ccr);
                    EM_A0 = cases[i].base;
                    EM_D0 = 0xdead0000 | values[value];
                    if (cases[i].pointer == 1)
                        WRITEUL(0x2104, 0x2000);
                    if (cases[i].pointer == 2)
                        WRITEUL(0x2100, 0x1ffc);
                    if (cases[i].address)
                        WRITEUW(cases[i].address, from ? 0xffff : values[value]);
                    execute();
                    assert(!trapped);
                    if (from) {
                        assert(status() == (0x2000 | ccr));
                        if (cases[i].address)
                            assert(READUW(cases[i].address) == (0x2000 | ccr));
                        else
                            assert(EM_D0 == (0xdead0000u | 0x2000 | ccr));
                    }
                    else {
                        uint16 expected = values[value] & 0xf71f;
                        assert(status() == expected);
                        uint32 stack = !(expected & 0x2000) ? 0xfe000 : expected & 0x1000 ? 0xfd000 : 0xff000;
                        assert(EM_A7 == stack);
                    }
                    assert(EM_A0 == cases[i].base + cases[i].delta);
                    ++count;
                }
    printf("%u MOVE SR register/EA/CCR/stack transfers passed\n", count);
}

static void stack_and_faults(void)
{
    for (unsigned pre = 0; pre < 2; ++pre)
        for (unsigned from = 0; from < 2; ++from) {
            program((from ? 0x40c0 : 0x46c0) | (pre ? 0x27 : 0x1f), NULL, 0);
            reset(0x201f);
            uint32 address = pre ? 0xfeffe : 0xff000;
            WRITEUW(address, 0x001f);
            execute();
            assert(!trapped);
            if (from) {
                assert(READUW(address) == 0x201f);
                assert(EM_A7 == (pre ? 0xfeffe : 0xff002));
            }
            else {
                assert(status() == 0x001f && EM_A7 == 0xfe000);
                assert(cpu_state.isp == (pre ? 0xfeffe : 0xff002));
            }
        }
    for (unsigned from = 0; from < 2; ++from) {
        program((from ? 0x40c0 : 0x46c0) | 0x10, NULL, 0);
        reset(0x201f);
        EM_A0 = 0xfffff;
        WRITEUB(0xfffff, 0x55);
        execute();
        assert(trapped == 2 && READUB(0xfffff) == 0x55);
    }
    const uint16 extension[] = {0};
    program(0x40e8, extension, 1);
    reset(0x201f);
    execute();
    assert(!trapped);
    WRITEUW(0x1002, 2);
    WRITEUW(0x2002, 0);
    reset(0x201f);
    execute();
    assert(!trapped && READUW(0x2002) == 0x201f);
}

static void native_register_chains(void)
{
    /* Native producers/consumers must see helper register and CCR changes. */
    const uint16 register_chain[] = {0xdead, 0x201f, 0x46c0, 0x40c1};
    program(0x203c, register_chain, 4); /* MOVE.L #dead201f,D0; SR round trip. */
    reset(0x2000);
    execute();
    assert(!trapped && EM_D0 == 0xdead201f && EM_D1 == 0x201f);
    const uint16 address_chain[] = {0, 0x2000, 0x46d8, 0x2408};
    program(0x207c, address_chain, 4); /* MOVEA.L #2000,A0; (A0)+ to SR; A0 to D2. */
    reset(0x2000);
    WRITEUW(0x2000, 0x201f);
    execute();
    assert(!trapped && EM_A0 == 0x2002 && EM_D2 == 0x2002);
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
    transfers();
    stack_and_faults();
    native_register_chains();
    printf("68040 MOVE SR privilege, bounds and extension changes passed (%s).\n", native ? "native" : "interpreter");
    VirtualFree(memory, 0, MEM_RELEASE);
    return 0;
}
