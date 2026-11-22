/*+===================================================================
  File:      ppc_fpscr_test.cpp

  Summary:   Implements ppc fpscr test support for synppc PowerPC execution and fragment loading.

  Classes:   Used: ROMlib_ppc_environment, ROMlib_ppc_registers

  Functions: main

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Original instruction vectors, PowerPC PEM 3.3 and instruction descriptions. */
#include "synppc/ppc.h"
#include <assert.h>
#include <fenv.h>
#include <initializer_list>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char memory[16];
static ROMlib_ppc_translation_cache* cache;
static unsigned count;

static uint32_t bit(unsigned n)
{
    return UINT32_C(0x80000000) >> n;
}

static bool cause(unsigned n)
{
    return (n >= 3 && n <= 12) || (n >= 21 && n <= 23);
}

static uint32_t summaries(uint32_t status)
{
    bool invalid = false, enabled = false;
    for (unsigned n = 7; n <= 23; ++n)
        if (cause(n) && (status & bit(n)))
            invalid = true;
    if (invalid && (status & bit(24)))
        enabled = true;
    for (unsigned n = 3; n <= 6; ++n)
        if ((status & bit(n)) && (status & bit(n + 22)))
            enabled = true;
    status &= ~(bit(1) | bit(2));
    return status | (invalid ? bit(2) : 0) | (enabled ? bit(1) : 0);
}

static void* map(void*, uint32_t address, size_t size, int)
{
    return address <= sizeof memory && size <= sizeof memory - address ? memory + address : NULL;
}

static void word(unsigned offset, uint32_t opcode)
{
    for (unsigned i = 0; i < 4; ++i)
        memory[offset + i] = opcode >> (24 - i * 8);
}

static ROMlib_ppc_registers run(uint32_t opcode, uint32_t status, uint64_t source, bool valid = true)
{
    word(0, opcode);
    word(4, 0x18000000);
    ROMlib_ppc_registers r = {};
    ROMlib_ppc_environment env = {};
    env.memory = map;
    env.instruction_limit = 4;
    r.fpscr = status;
    r.cr = 0x13579bdf;
    memcpy(&r.fpr[2], &source, 8);
    auto initial = r;
    int ok = ROMlib_ppc_execute(&r, &env);
    assert(!!ok == valid);
    if (!valid) {
        assert(strstr(env.error, "illegal PowerPC instruction"));
        assert(r.fpscr == status && r.cr == 0x13579bdf);
    }
    assert(fegetround() == FE_DOWNWARD);
    if (cache) {
        env.linear_memory = memory;
        env.linear_size = sizeof memory;
        env.translation_cache = cache;
        env.execution_flags = ROMLIB_PPC_EXEC_JIT;
        assert(ROMlib_ppc_translate_range(cache, memory, 0, 4) <= 1);
        assert(!!ROMlib_ppc_execute(&initial, &env) == valid);
        assert(!memcmp(&r, &initial, sizeof r) && fegetround() == FE_DOWNWARD);
        if (valid)
            assert(env.native_instructions == 1 && env.native_cache_hits == 1 && !env.native_kernel_instructions &&
                   !env.native_compilations);
        else
            assert(env.native_instructions == env.native_kernel_instructions);
    }
    ++count;
    return r;
}

static void check(uint32_t opcode, uint32_t input, uint64_t source, uint32_t expected, uint32_t cr)
{
    ROMlib_ppc_registers r = run(opcode, input, source);
    uint64_t actual;
    memcpy(&actual, &r.fpr[2], 8);
    if (r.fpscr != expected || r.cr != cr || actual != source) {
        fprintf(stderr,
                "FPSCR control %08x input=%08x: status %08x != %08x, "
                "CR %08x != %08x\n",
                opcode, input, r.fpscr, expected, r.cr, cr);
        exit(1);
    }
}

static uint32_t record(uint32_t expected, bool rc)
{
    return rc ? (0x13579bdf & ~0x0f000000u) | ((expected >> 4) & 0x0f000000) : 0x13579bdf;
}

static void native_sequences()
{
    ROMlib_ppc_environment env = {};
    env.memory = map;
    env.linear_memory = memory;
    env.linear_size = sizeof memory;
    env.translation_cache = cache;
    env.execution_flags = ROMLIB_PPC_EXEC_JIT;

    word(0, 0xFCA0004C); // mtfsb1 ZX
    word(4, 0xFC60048F); // mffs. f3
    word(8, 0x18000000);
    assert(ROMlib_ppc_translate_range(cache, memory, 0, 8) == 1);
    ROMlib_ppc_registers status = {};
    status.fpscr = 0x10; // ZE
    assert(ROMlib_ppc_execute(&status, &env));
    uint64_t copied;
    memcpy(&copied, &status.fpr[3], sizeof copied);
    assert(copied == 0xC4000010 && status.fpscr == copied && status.cr == 0x0C000000);
    assert(env.native_instructions == 2 && env.native_blocks == 1 && !env.native_kernel_instructions);

    for (unsigned mode = 0; mode < 4; ++mode)
        for (bool negative : {false, true}) {
            word(0, (63u << 26) | (7u << 23) | (mode << 12) | (134 << 1)); // mtfsfi 7,mode
            word(4, 0xD040000C);                                           // stfs f2,12(0)
            assert(ROMlib_ppc_translate_range(cache, memory, 0, 8) <= 2);
            ROMlib_ppc_registers r = {};
            uint64_t bits = UINT64_C(0x36A8000000000000) | (uint64_t(negative) << 63);
            memcpy(&r.fpr[2], &bits, sizeof bits);
            r.fpscr = mode ^ 1;
            assert(ROMlib_ppc_execute(&r, &env));
            bool rounds_away = !mode || mode == (negative ? 3u : 2u);
            assert(memory[12] == (negative ? 0x80 : 0) && !memory[13] && !memory[14] &&
                   memory[15] == (rounds_away ? 2 : 1));
            assert(env.native_instructions == 2 && env.native_blocks == 2 && !env.native_kernel_instructions &&
                   !env.native_compilations && fegetround() == FE_DOWNWARD);
        }
}

int main(void)
{
    fenv_t original;
    assert(!fegetenv(&original) && !fesetround(FE_DOWNWARD));
    cache = ROMlib_ppc_translation_cache_create();
    const uint32_t seeds[] = {
        0,          0x80000000, 0x10000000, 0x01000000, 0x00000100, 0x00040000, 0x00020000, 0x0001f000,
        0x000000f8, 0x10000040, 0x01000080, 0x08000020, 0x04000010, 0x02000008, 0x9ff7f7ff, 0x1ff7f7ff,
    };
    for (unsigned s = 0; s < sizeof seeds / sizeof *seeds; ++s) {
        uint32_t input = summaries(seeds[s]);
        for (unsigned field = 0; field < 8; ++field)
            for (unsigned imm = 0; imm < 16; ++imm)
                for (unsigned rc = 0; rc < 2; ++rc) {
                    unsigned shift = 28 - 4 * field;
                    uint32_t mask = (15u << shift) & ~bit(20);
                    uint32_t expected = summaries((input & ~mask) | (imm << shift & mask));
                    uint32_t op = (63u << 26) | (field << 23) | (imm << 12) | (134 << 1) | rc;
                    check(op, input, UINT64_C(0x7ff0000000000001), expected, record(expected, rc));
                }
        for (unsigned n = 0; n < 32; ++n)
            for (unsigned set = 0; set < 2; ++set)
                for (unsigned rc = 0; rc < 2; ++rc) {
                    uint32_t expected = input;
                    if (n != 1 && n != 2 && n != 20) {
                        if (set) {
                            if (cause(n) && !(input & bit(n)))
                                expected |= bit(0);
                            expected |= bit(n);
                        }
                        else
                            expected &= ~bit(n);
                    }
                    expected = summaries(expected);
                    uint32_t op = (63u << 26) | (n << 21) | ((set ? 38 : 70) << 1) | rc;
                    check(op, input, 0, expected, record(expected, rc));
                }
        for (unsigned fields = 0; fields < 256; ++fields)
            for (unsigned rc = 0; rc < 2; ++rc) {
                uint32_t source = ~input ^ (fields * 0x10204081u);
                uint32_t expected = input;
                for (unsigned n = 0; n < 32; ++n)
                    if ((fields & (0x80 >> (n / 4))) && n != 1 && n != 2 && n != 20)
                        expected = (expected & ~bit(n)) | (source & bit(n));
                expected = summaries(expected);
                uint32_t op = (63u << 26) | (fields << 17) | (2 << 11) | (711 << 1) | rc;
                check(op, input, UINT64_C(0x7ff0000000000000) | source, expected, record(expected, rc));
            }
        for (unsigned src = 0; src < 8; ++src)
            for (unsigned dst = 0; dst < 8; ++dst) {
                uint32_t expected = input;
                for (unsigned n = src * 4; n < src * 4 + 4; ++n)
                    if (n == 0 || cause(n))
                        expected &= ~bit(n);
                expected = summaries(expected);
                unsigned shift = 28 - 4 * dst;
                uint32_t cr = (0x13579bdf & ~(15u << shift)) | (((input >> (28 - 4 * src)) & 15) << shift);
                check((63u << 26) | (dst << 23) | (src << 18) | (64 << 1), input, 0, expected, cr);
            }
        for (unsigned rc = 0; rc < 2; ++rc) {
            ROMlib_ppc_registers r = run((63u << 26) | (3 << 21) | (583 << 1) | rc, input, 0);
            uint64_t bits;
            memcpy(&bits, &r.fpr[3], 8);
            // The upper word is undefined by the UISA.
            assert((uint32_t)bits == input && r.fpscr == input);
            assert(r.cr == record(input, rc));
        }
    }

    const struct {
        unsigned xo;
        uint32_t reserved;
    } encodings[] = {{64, 0x0063f801}, {583, 0x001ff800}, {38, 0x001ff800},
                     {70, 0x001ff800}, {711, 0x02010000}, {134, 0x007f0800}};

    for (unsigned i = 0; i < sizeof encodings / sizeof *encodings; ++i)
        for (unsigned n = 0; n < 32; ++n)
            if (encodings[i].reserved & bit(n))
                run((63u << 26) | (encodings[i].xo << 1) | bit(n), 0x12340000, UINT64_C(0x7ff0000000000001), false);
    if (cache)
        native_sequences();
    ROMlib_ppc_translation_cache_destroy(cache);
    assert(!fesetenv(&original));
    printf(
        "%u PPC FPSCR control/summary/CR/reserved-field cases passed with interpreter and available native execution\n",
        count);
}
