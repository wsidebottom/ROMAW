/*+===================================================================
  File:      jit-x64-movem-test.c

  Summary:   Implements jit x64 movem test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: __wrap_trap_direct, main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Independent MOVEM model, exhaustive masks, live masks and checked guards. */
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

static void run_case(void* code, unsigned form, unsigned address, unsigned size, unsigned mask, unsigned char* ram)
{
    uint32 expected[16];
    uint32 base = mask & 1 ? 0xAA005200 : 0x5200;
    for (unsigned i = 0; i < 16; ++i)
        expected[i] = EM_DREG(i) = 0xFEDCBA98 ^ (i * 0x1020304U) ^ (mask * 0x10001U);
    expected[address + 8] = EM_AREG(address) = base;
    WRITEUW(0x1002, mask);
    unsigned char bytes[128];
    for (unsigned i = 0; i < sizeof bytes; ++i)
        bytes[i] = ram[0x51C0 + i] = i * 37 + mask;
    uint32* cc[] = {&cpu_state.ccc, &cpu_state.ccv, &cpu_state.ccnz, &cpu_state.ccn, &cpu_state.ccx};
    uint32 before_cc[5];
    for (unsigned i = 0; i < 5; ++i)
        before_cc[i] = *cc[i] = mask & (1U << i) ? 0x87654321 + i : 0;
    uint32 cursor = base;
    int reading = form < 2, pre = form == 3, post = form == 1;
    for (unsigned bit = 0; bit < 16; ++bit)
        if (mask & (1U << bit)) {
            unsigned selected = pre ? 15 - bit : bit;
            if (pre)
                expected[8 + address] = cursor -= size;
            unsigned offset = (cursor & 0xFFFFFF) - 0x51C0;
            if (reading) {
                uint32 value = 0;
                for (unsigned i = 0; i < size; ++i)
                    value = value << 8 | bytes[offset + i];
                if (size == 2)
                    value = (int32)(int16)value;
                if (!post || selected != address + 8)
                    expected[selected] = value;
            }
            else {
                uint32 value = pre && selected == address + 8 ? base - size : expected[selected];
                for (unsigned i = 0; i < size; ++i)
                    bytes[offset + i] = value >> (8 * (size - i - 1));
            }
            if (!pre)
                cursor += size;
            if (post)
                expected[8 + address] = cursor;
        }
    unsigned calls = decoder_calls;
    assert(syn68k_x64_execute(code) == 0x1004);
    assert(decoder_calls == calls);
    for (unsigned i = 0; i < 16; ++i)
        assert(EM_DREG(i) == expected[i]);
    for (unsigned i = 0; i < 5; ++i)
        assert(*cc[i] == before_cc[i]);
    assert(memcmp(bytes, ram + 0x51C0, sizeof bytes) == 0);
}

int main(void)
{
    unsigned char* memory = calloc(1, 0x10000);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(0x10000, 0, 0);
    uint32 vectors[64] = {0};
    initialize_68k_emulator(NULL, 1, vectors, 0);
    syn68k_address_mask = 0xFFFFFF;
    unsigned cases = 0;
    for (unsigned form = 0; form < 4; ++form)
        for (unsigned wide = 0; wide < 2; ++wide)
            for (unsigned address = 0; address < 8; ++address) {
                unsigned mode = form == 1 ? 3 : form == 3 ? 4 : 2;
                WRITEUW(0x1000, 0x4880 | (form < 2 ? 0x400 : 0) | wide << 6 | mode << 3 | address);
                int8 length = 1;
                unsigned translated;
                int terminal;
                void* code = syn68k_x64_compile(SYN68K_TO_US(0x1000), &length, 1, &translated, 0x1000, &terminal);
                assert(code && translated == 1 && terminal);
                unsigned count = address == 0 || address == 7 ? 65536 : 32;
                for (unsigned sample = 0; sample < count; ++sample) {
                    unsigned mask = count == 65536 ? sample
                                    : sample < 16  ? 1U << sample
                                                   : 0xFFFF ^ (1U << (sample - 16));
                    run_case(code, form, address, wide ? 4 : 2, mask, memory);
                    ++cases;
                }
                WRITEUW(0x1002, 0);
                EM_AREG(address) = UINT32_MAX;
                unsigned calls = decoder_calls;
                assert(syn68k_x64_execute(code) == 0x1004);
                assert(decoder_calls == calls && EM_AREG(address) == UINT32_MAX);
                for (unsigned guard = 0; guard < 3; ++guard) {
                    WRITEUW(0x1002, 0xFFFF);
                    EM_AREG(address) = guard == 0 ? (mode == 4 ? 0x6002 : 0x5FFE) : 0x5200;
                    uint32 before[16];
                    memcpy(before, cpu_state.regs, sizeof before);
                    unsigned char bytes[128];
                    memcpy(bytes, memory + 0x51C0, sizeof bytes);
                    if (guard == 1)
                        syn68k_x64_code_pages[5] = 1;
                    if (guard == 2) {
                        syn68k_host_alias_count = 1;
                        syn68k_host_alias_base = mode == 4 ? 0x51F0 : 0x5210;
                        syn68k_host_alias_size = 2;
                    }
                    calls = decoder_calls;
                    assert(syn68k_x64_execute(code) == 0x2000);
                    assert(decoder_calls == calls + 1);
                    assert(memcmp(before, cpu_state.regs, sizeof before) == 0);
                    assert(memcmp(bytes, memory + 0x51C0, sizeof bytes) == 0);
                    syn68k_x64_code_pages[5] = 0;
                    syn68k_host_alias_count = syn68k_host_alias_size = 0;
                }
                syn68k_x64_release(code);
            }
    printf("%u native MOVEM mask/alias/update cases and frame guards passed.\n", cases);
    free(memory);
    return 0;
}
