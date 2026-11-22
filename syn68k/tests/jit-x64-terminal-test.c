/*+===================================================================
  File:      jit-x64-terminal-test.c

  Summary:   Implements jit x64 terminal test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: __wrap_trap_direct, main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Decoder-supported register forms and user-mode CCR transfer semantics. */
#include "jit-x64.h"
#include "trap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned decoder_calls, faults, cases;
static void* retire_code;
syn68k_addr_t __real_trap_direct(uint32 selector, uint32 pc, uint32 address);

syn68k_addr_t __wrap_trap_direct(uint32 selector, uint32 pc, uint32 address)
{
    ++decoder_calls;
    if (retire_code) {
        syn68k_x64_release(retire_code);
        retire_code = NULL;
    }
    return __real_trap_direct(selector, pc, address);
}

static void flags(unsigned value)
{
    cpu_state.ccc = value & 1 ? 0x80000000 : 0;
    cpu_state.ccv = value & 2 ? 0x12345678 : 0;
    cpu_state.ccnz = value & 4 ? 0 : 0x80008000;
    cpu_state.ccn = value & 8 ? 0x87654321 : 0;
    cpu_state.ccx = value & 16 ? 0x11111111 : 0;
}

static unsigned status(void)
{
    return !!cpu_state.ccc | !!cpu_state.ccv << 1 | !cpu_state.ccnz << 2 | !!cpu_state.ccn << 3 | !!cpu_state.ccx << 4;
}

static void* compile(unsigned op)
{
    WRITEUW(0x1000, op);
    int8 length = 1;
    unsigned translated;
    int terminal;
    void* code = syn68k_x64_compile(SYN68K_TO_US(0x1000), &length, 1, &translated, 0x1000, &terminal);
    assert(code && translated == 1 && terminal);
    return code;
}

static syn68k_addr_t fault(syn68k_addr_t pc, void* argument)
{
    unsigned vector = (uintptr_t)argument;
    assert(pc == 0x1000 && (READUW(EM_A7) & 31) == 31);
    assert(READUL(EM_A7 + 2) == 0x1000);
    assert(READUW(EM_A7 + 6) == (vector == 2 ? 0x7008 : 0x10));
    if (vector == 2)
        assert(READUL(EM_A7 + 20) == 0x10000);
    ++faults;
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

int main(void)
{
    void* memory = calloc(1, 0x10000);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(0x10000, 0, 0);
    uint32 vectors[64] = {0};
    initialize_68k_emulator(NULL, 1, vectors, 0);
    const uint32 values[] = {0, 1, 0x7F, 0x80, 0xFFFF, 0x8000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF};
    for (unsigned kind = 0; kind < 7; ++kind)
        for (unsigned reg = 0; reg < 8; ++reg)
            for (unsigned dest = 0; dest < (kind == 0 ? 8U : 1U); ++dest) {
                unsigned op = kind == 0   ? 0xB008 | dest << 9 | reg
                              : kind == 1 ? 0x4A48 | reg
                              : kind == 2 ? 0x4A88 | reg
                                          : 0xE4C0 | (kind - 3) << 8 | reg;
                void* native = compile(op);
                for (unsigned a = 0; a < 9; ++a)
                    for (unsigned b = 0; b < (kind == 0 ? 9U : 1U); ++b)
                        for (unsigned incoming = 0; incoming < 32; ++incoming) {
                            for (unsigned r = 0; r < 16; ++r)
                                EM_DREG(r) = 0x12340000 + r;
                            EM_AREG(reg) = values[kind == 0 ? b : a];
                            EM_DREG(kind == 0 ? dest : reg) = values[a];
                            uint32 expected[16];
                            for (unsigned r = 0; r < 16; ++r)
                                expected[r] = EM_DREG(r);
                            flags(incoming);
                            unsigned wanted = incoming & 16;
                            if (kind == 0) {
                                unsigned left = values[a] & 255, right = values[b] & 255;
                                unsigned result = (left - right) & 255;
                                wanted |= (left < right) | (!!((left ^ right) & (left ^ result) & 128) << 1) |
                                          (!result << 2) | (!!(result & 128) << 3);
                            }
                            else if (kind <= 2) {
                                uint32 result = kind == 1 ? values[a] & 65535 : values[a];
                                wanted |= (!result << 2) | (!!(result & (kind == 1 ? 0x8000U : 0x80000000U)) << 3);
                            }
                            else {
                                unsigned input = values[a] & 65535, left = (kind - 3) & 1;
                                unsigned carry = left ? input >> 15 : input & 1;
                                unsigned bit = kind <= 4 ? !!(incoming & 16) : carry;
                                unsigned result = left ? ((input << 1) | bit) & 65535 : (input >> 1) | bit << 15;
                                expected[reg] = (expected[reg] & 0xFFFF0000U) | result;
                                if (kind <= 4)
                                    wanted = carry << 4;
                                wanted |= carry | (!result << 2) | (!!(result & 0x8000) << 3);
                            }
                            unsigned calls = decoder_calls;
                            assert(syn68k_x64_execute(native) == 0x1002);
                            assert(decoder_calls == calls && status() == wanted);
                            for (unsigned r = 0; r < 16; ++r)
                                assert(EM_DREG(r) == expected[r]);
                            ++cases;
                        }
                syn68k_x64_release(native);
            }
    for (unsigned to = 0; to < 2; ++to)
        for (unsigned code = 0; code <= (to ? 60U : 57U); ++code) {
            if (code >= 8 && code < 16)
                continue;
            void* native = compile((to ? 0x44C0 : 0x42C0) | code);
            for (unsigned incoming = 0; incoming < 32; ++incoming)
                for (unsigned low = 0; low < 32; ++low) {
                    uint32 expected[16];
                    uint32 word = 0xA500 | low;
                    for (unsigned r = 0; r < 8; ++r)
                        EM_DREG(r) = 0xBEEF0000 | word, EM_AREG(r) = 0x5000 + r * 0x100;
                    EM_D6 = 32;
                    uint32 location = 0, next = 0x1002, mode = code >> 3;
                    if (mode >= 2 && mode <= 4)
                        location = EM_AREG(code & 7) - (mode == 4 ? 2 : 0);
                    else if (mode == 5)
                        WRITEUW(next, 16), location = EM_AREG(code & 7) + 16, next += 2;
                    else if (mode == 6 || code == 59) {
                        WRITEUW(next, 0x01F0);
                        WRITEUL(next + 2, 0x4801);
                        location = 0x4801;
                        next += 6;
                    }
                    else if (code == 56 || code == 58)
                        WRITEUW(next, code == 56 ? 0x4800 : 0x4800 - next), location = 0x4800, next += 2;
                    else if (code == 57)
                        WRITEUL(next, 0x4801), location = 0x4801, next += 4;
                    else if (code == 60)
                        location = next, next += 2;
                    if (location)
                        WRITEUW(location, word);
                    for (unsigned r = 0; r < 16; ++r)
                        expected[r] = EM_DREG(r);
                    if (mode == 3)
                        expected[8 + (code & 7)] += 2;
                    if (mode == 4)
                        expected[8 + (code & 7)] -= 2;
                    unsigned wanted = to ? (code < 8 ? expected[code] : word) & 31 : incoming;
                    if (!to && code < 8)
                        expected[code] = (expected[code] & 0xFFFF0000U) | incoming;
                    cpu_state.sr = incoming & 1 ? 0x2000 : 0;
                    flags(incoming);
                    unsigned calls = decoder_calls;
                    assert(syn68k_x64_execute(native) == next);
                    assert(decoder_calls == calls && status() == wanted);
                    assert(cpu_state.sr == (incoming & 1 ? 0x2000 : 0));
                    for (unsigned r = 0; r < 16; ++r)
                        assert(EM_DREG(r) == expected[r]);
                    if (!to && location)
                        assert(READUW(location) == incoming);
                    ++cases;
                }
            syn68k_x64_release(native);
        }
    for (unsigned native = 0; native < 2; ++native) {
        syn68k_x64_enabled = native;
        destroy_blocks(0x1000, 64);
        WRITEUW(0x1000, 0x44F9);
        WRITEUW(0x1006, 0x4EF9);
        WRITEUL(0x1008, MAGIC_EXIT_EMULATOR_ADDRESS);
        WRITEUW(0x4801, 5);
        WRITEUW(0x4811, 26);
        for (unsigned alternate = 0; alternate < 4; ++alternate) {
            WRITEUL(0x1002, alternate & 1 ? 0x4811 : 0x4801);
            cpu_state.sr = 0;
            EM_A7 = 0xE000;
            flags(31);
            interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
            assert(status() == (alternate & 1 ? 26 : 5) && cpu_state.sr == 0);
        }
    }
    destroy_blocks(0x1000, 64);
    /* Protected stores and source faults must use the checked CCR implementation
     after leaving native code, even when that callback frees the native block. */
    retire_code = compile(0x42D0);
    EM_A0 = 0x1000;
    flags(31);
    syn68k_x64_code_pages[1] = 1;
    assert(syn68k_x64_execute(retire_code) == 0x1002 && !retire_code);
    assert(READUW(0x1000) == 31 && status() == 31);
    syn68k_x64_code_pages[1] = 0;
    trap_install_handler(2, fault, (void*)(uintptr_t)2);
    trap_install_handler(4, fault, (void*)(uintptr_t)4);
    for (unsigned illegal = 0; illegal < 2; ++illegal) {
        retire_code = compile(illegal ? 0x44C8 : 0x44D8);
        cpu_state.sr = 0x2000;
        EM_A7 = cpu_state.isp = 0xE000;
        EM_A0 = illegal ? UINT32_MAX : 0xFFFF;
        flags(31);
        assert(syn68k_x64_execute(retire_code) == MAGIC_EXIT_EMULATOR_ADDRESS);
        assert(!retire_code && faults == illegal + 1);
        assert(EM_A0 == (illegal ? UINT32_MAX : 0x10001));
    }
    printf("%u terminal register/CCR cases and checked fault tails passed.\n", cases);
    free(memory);
    return 0;
}
