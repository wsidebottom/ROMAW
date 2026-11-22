/*+===================================================================
  File:      jit-x64-fast-memory-test.c

  Summary:   Implements jit x64 fast memory test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Native RAM guards, coherent stores and successful partial-prefix dispatch. */
#include "jit-x64.h"
#include "hash.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned char* ram;
static unsigned faults;
static uint32 expected_fault;
static uint32 expected_pc = 0x1000;

static syn68k_addr_t fault_handler(syn68k_addr_t pc, void* argument)
{
    assert(pc == expected_pc && READUW(EM_A7 + 6) == 0x7008);
    assert(READUL(EM_A7 + 20) == expected_fault);
    ++faults;
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static void* compile(unsigned op, unsigned instructions, unsigned* translated, int* terminal)
{
    int8 lengths[80];
    assert(instructions <= 80);
    for (unsigned i = 0; i < instructions; ++i)
        lengths[i] = 1;
    WRITEUW(0x1000, op);
    return syn68k_x64_compile(SYN68K_TO_US(0x1000), lengths, instructions, translated, 0x1000, terminal);
}

static void exit_at(uint32 pc)
{
    WRITEUW(pc, 0x4EF9);
    WRITEUL(pc + 2, MAGIC_EXIT_EMULATOR_ADDRESS);
}

static uint32 execute(unsigned op, uint32 address, uint32 expected_pc)
{
    exit_at(0x1002);
    EM_AREG(op & 7) = address;
    unsigned translated;
    int terminal;
    void* code = compile(op, 2, &translated, &terminal);
    assert(code && translated == 2 && terminal);
    uint32 result = syn68k_x64_execute(code);
    assert(result == expected_pc);
    syn68k_x64_release(code);
    return result;
}

int main(void)
{
    ram = calloc(1, 0x100000);
    assert(ram);
    ROMlib_offset = (uintptr_t)ram;
    syn68k_set_memory_window(0x20000, 0, 0);
    uint32 vectors[64] = {0};
    initialize_68k_emulator(NULL, 1, vectors, 0);
    unsigned cases = 0;
    for (unsigned width = 0; width < 3; ++width)
        for (unsigned mode = 2; mode <= 4; ++mode)
            for (unsigned reg = 0; reg < 8; ++reg)
                for (unsigned quick = 1; quick <= 8; ++quick)
                    for (unsigned subtract = 0; subtract < 2; ++subtract)
                        for (unsigned sample = 0; sample < 16; ++sample) {
                            unsigned bits = 8U << width;
                            uint32 mask = UINT32_MAX >> (32 - bits), sign = 1U << (bits - 1);
                            uint32 edges[] = {0, 1, mask, sign, sign - 1, sign + 1, mask - 1, 8};
                            uint32 input = edges[sample & 7], address = 0x4000 + (sample & 7);
                            WRITEUL(address, width == 2 ? input : width == 1 ? input << 16 : input << 24);
                            unsigned op = 0x5000 | mode << 3 | width << 6 | reg | (quick & 7) << 9 | subtract << 8;
                            unsigned step = width == 0 && reg == 7 ? 2 : 1U << width;
                            uint32 result = (subtract ? input - quick : input + quick) & mask;
                            unsigned carry = subtract ? input < quick : (uint64)input + quick > mask;
                            unsigned overflow = !!(
                                (subtract ? (input ^ quick) & (input ^ result) : ~(input ^ quick) & (input ^ result)) &
                                sign);
                            cpu_state.ccc = cpu_state.ccv = cpu_state.ccnz = cpu_state.ccn = cpu_state.ccx = sample;
                            execute(op, mode == 4 ? address + step : address, MAGIC_EXIT_EMULATOR_ADDRESS);
                            uint32 actual = width == 2   ? READUL(address)
                                            : width == 1 ? READUW(address)
                                                         : READUB(address);
                            assert(actual == result);
                            assert(!!cpu_state.ccc == carry && !!cpu_state.ccx == carry &&
                                   !!cpu_state.ccv == overflow && !!cpu_state.ccn == !!(result & sign) &&
                                   !!cpu_state.ccnz == !!result);
                            assert(EM_AREG(reg) == address + (mode == 3 ? step : 0));
                            ++cases;
                        }
    /* Reusing the R8 guard is legal only for the same An and sufficient width. */
    destroy_blocks(0, UINT32_MAX);
    for (unsigned i = 0; i < 70; ++i)
        WRITEUW(0x1000 + i * 2, 0x5290);
    exit_at(0x108C);
    unsigned translated;
    int terminal;
    void* code = compile(0x5290, 71, &translated, &terminal);
    assert(code && translated == 64 && terminal);
    EM_A0 = 0x4000;
    WRITEUL(0x4000, 0);
    assert(syn68k_x64_execute(code) == 0x1080 && READUL(0x4000) == 64);
    syn68k_x64_release(code);

    /* A terminal memory body can follow the cached linear prefix. Its next
     PC must dispatch dynamically before the complex instruction. */
    WRITEUW(0x1002, 0x5290);
    WRITEUW(0x1004, 0x4800); /* NBCD D0 uses a terminal semantic kernel. */
    exit_at(0x1006);
    code = compile(0x5290, 4, &translated, &terminal);
    assert(code && translated == 2 && terminal);
    WRITEUL(0x4000, 0);
    assert(syn68k_x64_execute(code) == 0x1004 && READUL(0x4000) == 2);
    syn68k_x64_release(code);

    /* Two cached blocks on a data page keep the store guard closed until both
     have gone; destroying an overlapping parent must not undercount pages. */
    destroy_blocks(0, UINT32_MAX);
    WRITEUW(0x4000, 0x4E71);
    exit_at(0x4002);
    hash_lookup_code_and_create_if_needed(0x4000);
    hash_lookup_code_and_create_if_needed(0x4002);
    assert(syn68k_x64_code_pages[4] >= 2);
    destroy_blocks(0x4000, 2);
    assert(syn68k_x64_code_pages[4]);
    uint32 before = READUL(0x4002);
    execute(0x5290, 0x4002, 0x1002); /* The decoder invalidates the other block. */
    assert(READUL(0x4002) == before + 1 && !hash_lookup(0x4002));
    assert(!syn68k_x64_code_pages[4]);

    WRITEUL(0x4FFE, 9);
    execute(0x5290, 0x4FFE, 0x1002); /* Valid transfer crosses a guarded page. */
    assert(READUL(0x4FFE) == 10);
    for (unsigned mode = 3; mode <= 4; ++mode) {
        /* Page-guard fallback updates An exactly once, on success and fault. */
        WRITEUL(0x4FFE, 9);
        execute(0x5280 | mode << 3, mode == 4 ? 0x5002 : 0x4FFE, 0x1002);
        assert(READUL(0x4FFE) == 10);
        assert(EM_A0 == (mode == 3 ? 0x5002 : 0x4FFE));
        trap_install_handler(2, fault_handler, NULL);
        cpu_state.sr = 0x2000;
        EM_A7 = cpu_state.isp = 0xF000;
        expected_fault = 0x20000;
        execute(0x5280 | mode << 3, mode == 4 ? 0x20002 : 0x1FFFE, MAGIC_EXIT_EMULATOR_ADDRESS);
        assert(EM_A0 == (mode == 3 ? 0x20002 : 0x1FFFE));
    }
    faults = 0;
    for (unsigned i = 0; i < 2; ++i) {
        trap_install_handler(2, fault_handler, NULL);
        cpu_state.sr = 0x2000;
        EM_A7 = cpu_state.isp = 0xF000;
        cpu_state.ccc = cpu_state.ccx = 1;
        cpu_state.ccv = cpu_state.ccn = cpu_state.ccnz = 0;
        WRITEUW(0x1FFFE, 0xABCD);
        expected_fault = i ? 0xFFFFFFFE : 0x20000;
        execute(0x5290, i ? 0xFFFFFFFE : 0x1FFFE, MAGIC_EXIT_EMULATOR_ADDRESS);
        assert(faults == i + 1 && READUW(0x1FFFE) == 0xABCD);
        assert(cpu_state.ccc && cpu_state.ccx && !cpu_state.ccv && !cpu_state.ccn && !cpu_state.ccnz);
    }

    /* Cached-pointer flag stores may be elided, but the last producer before a
     new failing guard must be materialized for the exception frame. */
    for (unsigned i = 0; i < 3; ++i)
        WRITEUW(0x1000 + i * 2, 0x5290);
    WRITEUW(0x1006, 0x5291);
    exit_at(0x1008);
    code = compile(0x5290, 5, &translated, &terminal);
    assert(code && translated == 5 && terminal);
    EM_A0 = 0x4000;
    EM_A1 = 0x1FFFE;
    EM_A7 = cpu_state.isp = 0xF000;
    cpu_state.sr = 0x2000;
    cpu_state.ccc = cpu_state.ccv = cpu_state.ccn = cpu_state.ccx = 0;
    cpu_state.ccnz = 1;
    WRITEUL(0x4000, UINT32_MAX - 2);
    expected_pc = 0x1006;
    expected_fault = 0x20000;
    assert(syn68k_x64_execute(code) == MAGIC_EXIT_EMULATOR_ADDRESS);
    assert(faults == 3 && !READUL(0x4000));
    assert((READUW(EM_A7) & 31) == 0x15);
    syn68k_x64_release(code);
    expected_pc = 0x1000;

    /* A later wider transfer must recheck a cached byte pointer. */
    WRITEUW(0x1002, 0x5290);
    WRITEUW(0x1004, 0x5251);
    exit_at(0x1006);
    code = compile(0x5210, 4, &translated, &terminal);
    assert(code && translated == 4 && terminal);
    EM_A0 = 0x4FFF;
    EM_A1 = 0x6000;
    WRITEUL(0x4FFF, 0x12000000);
    WRITEUW(0x6000, 1);
    assert(syn68k_x64_execute(code) == 0x1006);
    assert(READUL(0x4FFF) == 0x13000001 && READUW(0x6000) == 2);
    syn68k_x64_release(code);

    syn68k_address_mask = 0xFFFFFF;
    WRITEUL(0x4000, 10);
    execute(0x5290, 0xAA004000, MAGIC_EXIT_EMULATOR_ADDRESS);
    assert(READUL(0x4000) == 11);

    /* Host aliases can overlap the declared flat RAM window. */
    syn68k_address_mask = UINT32_MAX;
    syn68k_set_memory_window(0x100000, 0x10000, 0x40000);
    unsigned char* alias_memory = calloc(1, 0x20000);
    assert(alias_memory);
    uint32 alias = US_TO_SYN68K(alias_memory);
    WRITEUL(alias + 37, 41);
    execute(0x5290, alias + 37, 0x1002);
    assert(READUL(alias + 37) == 42 && !ram[alias + 37]);
    printf("%u direct native indirect arithmetic cases and page/alias/prefix guards passed.\n", cases);
    return 0;
}
