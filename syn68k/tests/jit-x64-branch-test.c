/*+===================================================================
  File:      jit-x64-branch-test.c

  Summary:   Implements jit x64 branch test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Independent branch truth table, displacement, DBcc and dispatch fixtures. */
#include "jit-x64.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int native;
static unsigned cases;
static uint32 fault_pc, fault_address;
static unsigned faults;

static syn68k_addr_t fault_handler(syn68k_addr_t pc, void* argument)
{
    assert(pc == fault_pc);
    assert(READUW(EM_A7 + 6) == 0x7008);
    assert(READUL(EM_A7 + 20) == fault_address);
    ++faults;
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static void live_extensions(void)
{
    int8 lengths[] = {1};
    unsigned translated;
    int terminal;
    /* Change the displacement after compilation, including on a page already
     holding translated instructions. Only stores need the code-page guard. */
    WRITEUW(0x1000, 0x51C8);
    WRITEUW(0x1002, 16);
    hash_lookup_code_and_create_if_needed(0x1000);
    void* code = syn68k_x64_compile(SYN68K_TO_US(0x1000), lengths, 1, &translated, 0x1000, &terminal);
    assert(code && translated == 1 && terminal);
    WRITEUW(0x1002, (uint16)-32);
    EM_D0 = 0x12340002;
    assert(syn68k_x64_execute(code) == 0xFE2 && EM_D0 == 0x12340001);
    syn68k_x64_release(code);
    ++cases;

    const unsigned jumps[] = {0x4EF8, 0x4EF9, 0x4EFA, 0x4EE8};
    for (unsigned i = 0; i < sizeof jumps / sizeof *jumps; ++i) {
        WRITEUW(0x2000, jumps[i]);
        WRITEUL(0x2002, 0);
        code = syn68k_x64_compile(SYN68K_TO_US(0x2000), lengths, 1, &translated, 0x2000, &terminal);
        assert(code && translated == 1 && terminal);
        WRITEUW(0x2002, 0xFF00);
        EM_A0 = 0x4000;
        uint32 expected = i == 1 ? 0xFF000000 : i == 2 ? 0x1F02 : i == 3 ? 0x3F00 : 0xFFFFFF00;
        assert(syn68k_x64_execute(code) == expected);
        syn68k_x64_release(code);
        ++cases;
    }

    /* A long displacement crossing a page takes the checked tail. */
    WRITEUW(0x1FFC, 0x60FF);
    WRITEUL(0x1FFE, 0x100);
    code = syn68k_x64_compile(SYN68K_TO_US(0x1FFC), lengths, 1, &translated, 0x1FFC, &terminal);
    assert(code && translated == 1 && terminal);
    WRITEUL(0x1FFE, (uint32)-0x100);
    assert(syn68k_x64_execute(code) == 0x1EFE);
    syn68k_x64_release(code);
    ++cases;

    trap_install_handler(2, fault_handler, NULL);
    for (unsigned kind = 0; kind < 5; ++kind) {
        /* DBT must fetch its extension too; no failed fetch decrements D0. */
        fault_pc = kind == 2 || kind == 3 ? 0xFFFC : 0xFFFE;
        fault_address = 0x10000;
        WRITEUW(fault_pc, kind == 0 ? 0x50C8 : kind == 1 ? 0x51C8 : kind == 2 ? 0x60FF : kind == 3 ? 0x4EF9 : 0x4EE8);
        code = syn68k_x64_compile(SYN68K_TO_US(fault_pc), lengths, 1, &translated, fault_pc, &terminal);
        assert(code && translated == 1 && terminal);
        EM_D0 = 0x12340002;
        cpu_state.sr = 0x2000;
        EM_A7 = cpu_state.isp = 0xF000;
        assert(syn68k_x64_execute(code) == MAGIC_EXIT_EMULATOR_ADDRESS);
        assert(faults == kind + 1 && EM_D0 == 0x12340002);
        syn68k_x64_release(code);
        ++cases;
    }
}

static int truth(unsigned cc, unsigned flags)
{
    int c = !!(flags & 1), v = !!(flags & 2), z = !!(flags & 4), n = !!(flags & 8);
    switch (cc) {
    case 0:
        return 1;
    case 1:
        return 0;
    case 2:
        return !c && !z;
    case 3:
        return c || z;
    case 4:
        return !c;
    case 5:
        return c;
    case 6:
        return !z;
    case 7:
        return z;
    case 8:
        return !v;
    case 9:
        return v;
    case 10:
        return !n;
    case 11:
        return n;
    case 12:
        return n == v;
    case 13:
        return n != v;
    case 14:
        return !z && n == v;
    default:
        return z || n != v;
    }
}

static void state(unsigned flags)
{
    /* Test the public ABI's arbitrary nonzero representation too. */
    cpu_state.ccc = flags & 1 ? 0x80000000 : 0;
    cpu_state.ccv = flags & 2 ? 0x12345678 : 0;
    cpu_state.ccnz = flags & 4 ? 0 : 0x80008000;
    cpu_state.ccn = flags & 8 ? 0x87654321 : 0;
    cpu_state.ccx = flags & 16 ? 0x11111111 : 0;
    cpu_state.sr = 0;
    for (unsigned i = 0; i < 16; ++i)
        EM_DREG(i) = 0xDEAD0000 + i;
}

static void direct(const uint16* source, unsigned words, uint32 pc, uint32 target, int reg, uint32 expected)
{
    /* Control extensions are live operands, not translation-time constants. */
    int8 lengths[] = {1};
    if (words > 1)
        memcpy(SYN68K_TO_US(pc), source, words * 2);
    unsigned count;
    int terminal;
    void* code = syn68k_x64_compile(source, lengths, 1, &count, pc, &terminal);
    assert(code && count == 1 && terminal);
    CPUState saved = cpu_state;
    uint64 before = syn68k_native_execution_count();
    uint32 result = syn68k_x64_execute(code);
    syn68k_x64_release(code);
    assert(result == target);
    assert(syn68k_native_execution_count() == before + 1);
    if (reg >= 0)
        saved.regs[reg].ul.n = expected;
    assert(!memcmp(&saved, &cpu_state, sizeof saved));
    ++cases;
}

static void destination(unsigned pc, unsigned marker)
{
    WRITEUW(pc, 0x7E00 | marker); /* MOVEQ marker,D7 */
    WRITEUW(pc + 2, 0x4EF9);
    WRITEUL(pc + 4, MAGIC_EXIT_EMULATOR_ADDRESS);
}

static void dispatch(unsigned words, int taken)
{
    destination(0x1000 + words * 2, 11);
    destination(0x1020, 22);
    uint64 before = syn68k_native_execution_count();
    interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
    assert(EM_D7 == (taken ? 22 : 11));
    if (syn68k_native_execution_count() != before + native * 2) {
        fprintf(stderr, "branch %04x words %u taken %d cases %u: native entries %llu\n", READUW(0x1000), words, taken,
                cases, (unsigned long long)(syn68k_native_execution_count() - before));
        abort();
    }
    ++cases;
}

int main(int argc, char** argv)
{
    native = argc > 1 && atoi(argv[1]);
    void* memory = calloc(1, 0x10000);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(0x10000, 0, 0);
    uint32 vectors[64] = {0};
    initialize_68k_emulator(NULL, native, vectors, 0);
    static const uint32 pcs[] = {0, 0x1000, 0x80000000, 0xFFFFFFF0};
    static const int32 displacements[][7] = {{-128, -126, -2, 2, 4, 124, 126},
                                             {-32768, -126, -2, 0, 2, 126, 32766},
                                             {INT32_MIN, -32768, -2, 0, 2, 32766, INT32_MAX - 1}};
    static const uint32 counters[] = {0, 1, 0xFFFF, 0x10000, 0x8000, 0x7FFF};
    for (unsigned cc = 0; cc < 16; ++cc)
        for (unsigned flags = 0; flags < 32; ++flags) {
            if (cc != 1)
                for (unsigned width = 0; width < 3; ++width) {
                    if (native)
                        for (unsigned p = 0; p < 4; ++p)
                            for (unsigned d = 0; d < 7; ++d) {
                                if (width && p != 1)
                                    continue; /* Live extensions require mapped guest RAM. */
                                int32 disp = displacements[width][d];
                                uint16 source[] = {SWAPUW_IFLE(0x6000 | cc << 8 |
                                                               (width == 0   ? (uint8)disp
                                                                : width == 2 ? 255
                                                                             : 0)),
                                                   SWAPUW_IFLE(width == 2 ? (uint32)disp >> 16 : (uint16)disp),
                                                   SWAPUW_IFLE((uint16)disp)};
                                state(flags);
                                direct(source, width + 1, pcs[p],
                                       truth(cc, flags) ? pcs[p] + 2 + (uint32)disp : pcs[p] + (width + 1) * 2, -1, 0);
                            }
                    destroy_blocks(0x1000, 64);
                    state(flags);
                    WRITEUW(0x1000, 0x6000 | cc << 8 | (width == 0 ? 30 : width == 2 ? 255 : 0));
                    if (width == 1)
                        WRITEUW(0x1002, 30);
                    else if (width == 2)
                        WRITEUL(0x1002, 30);
                    dispatch(width + 1, truth(cc, flags));
                }
            for (unsigned reg = 0; reg < 8; ++reg)
                for (unsigned counter = 0; counter < 6; ++counter) {
                    uint32 original = 0xABCD0000 | counters[counter];
                    uint32 result = truth(cc, flags) ? original : (original & 0xFFFF0000) | (uint16)(original - 1);
                    int taken = !truth(cc, flags) && (uint16)result != 0xFFFF;
                    if (native)
                        for (unsigned p = 0; p < 4; ++p)
                            for (unsigned d = 0; d < 7; ++d) {
                                if (p != 1)
                                    continue;
                                int32 disp = displacements[1][d];
                                uint16 source[] = {SWAPUW_IFLE(0x50C8 | cc << 8 | reg), SWAPUW_IFLE((uint16)disp)};
                                state(flags);
                                EM_DREG(reg) = original;
                                direct(source, 2, pcs[p], taken ? pcs[p] + 2 + (uint32)disp : pcs[p] + 4, reg, result);
                            }
                    /* D7 is the destination marker, so its counter is checked
                 by direct tests while runtime marker cases use D0..D6. */
                    if (reg == 7)
                        continue;
                    destroy_blocks(0x1000, 64);
                    state(flags);
                    EM_DREG(reg) = original;
                    WRITEUW(0x1000, 0x50C8 | cc << 8 | reg);
                    WRITEUW(0x1002, 30);
                    dispatch(2, taken);
                    assert(EM_DREG(reg) == result);
                }
        }
    if (native)
        for (unsigned p = 0; p < 4; ++p)
            for (unsigned i = 0; i < 16; ++i) {
                state(i);
                uint32 target = pcs[p] + 0x1234;
                uint16 absolute[] = {SWAPUW_IFLE(0x4EF9), SWAPUW_IFLE(target >> 16), SWAPUW_IFLE((uint16)target)};
                if (p == 1)
                    direct(absolute, 3, pcs[p], target, -1, 0);
                uint16 word[] = {SWAPUW_IFLE(0x4EF8), SWAPUW_IFLE(0x8000 + i)};
                if (p == 1)
                    direct(word, 2, pcs[p], 0xFFFF8000 + i, -1, 0);
                uint16 relative[] = {SWAPUW_IFLE(0x4EFA), SWAPUW_IFLE(0x8000 + i)};
                if (p == 1)
                    direct(relative, 2, pcs[p], pcs[p] + 2 + 0xFFFF8000 + i, -1, 0);
                unsigned reg = i & 7;
                EM_AREG(reg) = target;
                uint16 indirect[] = {SWAPUW_IFLE(0x4ED0 | reg)};
                direct(indirect, 1, pcs[p], target, -1, 0);
                if (p == 1) {
                    uint16 displaced[] = {SWAPUW_IFLE(0x4EE8 | reg), SWAPUW_IFLE(0x8000 + i)};
                    direct(displaced, 2, pcs[p], target + 0xFFFF8000 + i, -1, 0);
                }
            }
    if (native) {
        const uint32 targets[] = {0, UINT32_MAX, MAGIC_EXIT_EMULATOR_ADDRESS, 0x12345678};
        uint16 source[] = {SWAPUW_IFLE(0x4E75)};
        WRITEUW(0x1000, 0x4E75);
        for (unsigned bank = 0; bank < 3; ++bank)
            for (unsigned flags = 0; flags < 32; ++flags)
                for (unsigned offset = 0; offset < 5; ++offset)
                    for (unsigned target = 0; target < sizeof targets / sizeof *targets; ++target) {
                        state(flags);
                        cpu_state.sr = bank == 0 ? 0 : bank == 1 ? 0x2000 : 0x3000;
                        uint32 stack = offset < 4 ? 0x4000 + offset : 0x4FFE;
                        EM_A7 = stack;
                        WRITEUL(stack, targets[target]);
                        direct(source, 1, 0x1000, targets[target], 15, stack + 4);
                    }
        live_extensions();
    }
    printf("%u branch truth/displacement/DBcc/JMP/dispatch cases passed (%s).\n", cases,
           native ? "x64 JIT" : "interpreter");
    return 0;
}
