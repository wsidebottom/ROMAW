/*+===================================================================
  File:      jit-x64-source-test.c

  Summary:   Implements jit x64 source test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: __wrap_trap_direct, main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Direct arithmetic EAs, extension ordering and semantic-exception rollback. */
#include "jit-x64.h"
#include "trap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned decoder_calls;

syn68k_addr_t __wrap_trap_direct(uint32 selector, uint32 pc, uint32 address)
{
    assert(selector == 4 && pc == 0x1000 && address == 0);
    ++decoder_calls;
    return 0x2000;
}

static void put(uint32 address, unsigned size, uint32 value)
{
    for (unsigned i = 0; i < size; ++i)
        WRITEUB(address + i, value >> (8 * (size - i - 1)));
}

/* Build an EA without using the runtime decoder as the expected-value model. */
static unsigned operand(unsigned code, unsigned size, unsigned offset, unsigned variant, uint32* location)
{
    unsigned mode = code >> 3, reg = code & 7;
    uint32 base = EM_AREG(reg);
    *location = 0;
    if (mode == 0)
        return offset;
    if (mode <= 4) {
        *location = mode == 4 ? base - size : base;
        return offset;
    }
    uint32 extension = 0x1000 + offset;
    if (mode == 5) {
        WRITEUW(extension, variant ? 11 : -7);
        *location = base + (variant ? 11 : -7);
    }
    else if (mode == 6 || code == 59) {
        WRITEUW(extension, 0x5807 | (variant ? 0x200 : 0));
        *location = (mode == 6 ? base : extension) + (0x2000U << variant) + 7;
    }
    else if (code == 56) {
        *location = 0x3400 + variant * 3;
        WRITEUW(extension, *location);
    }
    else if (code == 57) {
        *location = 0x3501 + variant * 3;
        WRITEUL(extension, *location);
        return offset + 4;
    }
    else if (code == 58) {
        WRITEUW(extension, 0x2500 + variant);
        *location = extension + 0x2500 + variant;
    }
    else {
        assert(code == 60);
        *location = extension;
        return offset + size;
    }
    return offset + 2;
}

int main(void)
{
    void* memory = calloc(1, 0x10000);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(0x10000, 0, 0);
    uint32 vectors[64] = {0};
    initialize_68k_emulator(NULL, 1, vectors, 0);
    unsigned cases = 0;
    for (unsigned family = 0; family < 12; ++family)
        for (unsigned sign = 0; sign < (family < 4 ? 2U : 1U); ++sign)
            for (unsigned code = 0; code <= 60; ++code) {
                unsigned mode = code >> 3;
                if ((code >= 8 && code < 16) || (family >= 6 && (mode < 2 || mode == 3 || mode == 4 || code == 60)))
                    continue;
                unsigned width = family < 2 ? 1 : family < 4 ? 2 : family == 4 ? 1 : family == 5 ? 2 : (family - 6) % 3;
                unsigned size = 1U << width;
                unsigned op = family == 0   ? 0xC0C0 | 7 << 9 | sign << 8
                              : family == 1 ? 0x80C0 | 7 << 9 | sign << 8
                              : family == 2 ? 0x4C00
                              : family == 3 ? 0x4C40
                              : family == 4 ? 0x4180 | 7 << 9
                              : family == 5 ? 0x4100 | 7 << 9
                                            : 0x00C0 | width << 9;
                WRITEUW(0x1000, op | code);
                int8 length = 1;
                unsigned translated;
                int terminal;
                void* native = syn68k_x64_compile(SYN68K_TO_US(0x1000), &length, 1, &translated, 0x1000, &terminal);
                assert(native && translated == 1 && terminal);
                for (unsigned variant = 0; variant < 2; ++variant)
                    for (unsigned flags = 0; flags < 32; ++flags) {
                        uint32 expected[16];
                        for (unsigned i = 0; i < 8; ++i)
                            EM_DREG(i) = 9, EM_AREG(i) = 0x5100 + i * 0x100;
                        EM_D5 = 0x2000;
                        EM_D7 = family < 4 ? 1000 : 5;
                        unsigned offset = family == 2 || family == 3 || family >= 6 ? 4 : 2;
                        if (offset == 4)
                            WRITEUW(0x1002, family >= 6 ? 0x7000 | (family >= 9 ? 0x800 : 0) : 0x7006 | sign << 11);
                        uint32 location;
                        unsigned next = 0x1000 + operand(code, size, offset, variant, &location);
                        if (location) {
                            put(location, size, family >= 6 ? 2 : 9);
                            if (family >= 6)
                                put(location + size, size, 9);
                        }
                        uint32 source = mode == 0 ? EM_DREG(code) : 9;
                        for (unsigned i = 0; i < 16; ++i)
                            expected[i] = EM_DREG(i);
                        if (mode == 3)
                            expected[8 + (code & 7)] += size;
                        if (mode == 4)
                            expected[8 + (code & 7)] -= size;
                        uint32* cc[] = {&cpu_state.ccc, &cpu_state.ccv, &cpu_state.ccnz, &cpu_state.ccn,
                                        &cpu_state.ccx};
                        uint32 expected_cc[5];
                        for (unsigned i = 0; i < 5; ++i)
                            expected_cc[i] = *cc[i] = flags & (1U << i) ? 0x87654321 + i : 0;
                        if (family < 4) {
                            uint32 dividend = expected[7], result;
                            if (family == 0 || family == 2)
                                result = dividend * source;
                            else {
                                result = dividend / source;
                                uint32 remainder = dividend % source;
                                if (family == 1)
                                    result |= remainder << 16;
                                else
                                    expected[6] = remainder;
                            }
                            expected[7] = result;
                            expected_cc[0] = expected_cc[1] = expected_cc[3] = 0;
                            expected_cc[2] = family == 1 ? (result & 0xFFFF) != 0 : result != 0;
                        }
                        else if (family < 6)
                            expected_cc[3] = 0;
                        else
                            expected_cc[0] = 0, expected_cc[2] = 1;
                        unsigned calls = decoder_calls;
                        uint32 actual = syn68k_x64_execute(native);
                        if (actual != next || decoder_calls != calls)
                            fprintf(stderr, "family %u code %u width %u: next %x/%x decoder %u\n", family, code, width,
                                    actual, next, decoder_calls - calls);
                        assert(actual == next && decoder_calls == calls);
                        for (unsigned i = 0; i < 16; ++i)
                            assert(EM_DREG(i) == expected[i]);
                        for (unsigned i = 0; i < 5; ++i)
                            if (i == 4 || (family >= 4 && family < 6 && i != 3) || (family >= 6 && (i == 1 || i == 3)))
                                assert(*cc[i] == expected_cc[i]);
                            else
                                assert(!!*cc[i] == !!expected_cc[i]);
                        ++cases;
                    }
                if ((family == 1 || family == 3 || family == 4 || family == 5) && (mode == 3 || mode == 4)) {
                    EM_AREG(code & 7) = 0x5100;
                    EM_D7 = 10;
                    if (family == 3)
                        WRITEUW(0x1002, 0x7006);
                    put(mode == 3 ? 0x5100 : 0x5100 - size, size, family == 1 || family == 3 ? 0 : 9);
                    unsigned calls = decoder_calls;
                    assert(syn68k_x64_execute(native) == 0x2000);
                    assert(decoder_calls == calls + 1 && EM_AREG(code & 7) == 0x5100);
                }
                syn68k_x64_release(native);
            }
    printf("%u direct arithmetic/CHK/CMP2/CHK2 source-EA cases passed.\n", cases);
    free(memory);
    return 0;
}
