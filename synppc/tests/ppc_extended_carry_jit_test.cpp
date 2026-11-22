/*+===================================================================
  File:      ppc_extended_carry_jit_test.cpp

  Summary:   Implements ppc extended carry jit test support for synppc PowerPC execution and fragment loading.

  Classes:   Used: ROMlib_ppc_environment, ROMlib_ppc_registers

  Functions: main

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Original widened-integer oracle for carry-consuming native arithmetic. */
#include "synppc/ppc.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned char memory[4096];
static unsigned services;
static ROMlib_ppc_registers expected;
static bool invalid_rewrite;
static unsigned invalid_bits;

static void* map(void*, uint32_t address, size_t size, int)
{
    return address <= sizeof memory && size <= sizeof memory - address ? memory + address : NULL;
}

static void word(unsigned address, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        memory[address + i] = value >> (24 - i * 8);
}

static uint32_t readword(unsigned address)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value = value * 256 + memory[address + i];
    return value;
}

static void condition(unsigned field, int64_t value)
{
    unsigned nibble = (value == 0 ? 2 : value < 0 ? 8 : 4) | (expected.xer >> 31);
    unsigned shift = 28 - field * 4;
    expected.cr = (expected.cr & ~(15u << shift)) | (nibble << shift);
}

static int service(void*, uint32_t id, ROMlib_ppc_registers* r)
{
    assert(id == 1);
    for (unsigned i = 0; i < 6; ++i) {
        uint32_t op = readword(0x100 + i * 4);
        unsigned ra = (op >> 16) & 31, rd = (op >> 21) & 31;
        if (i == 0 || i == 5) {
            condition((op >> 23) & 7, int64_t(int32_t(expected.gpr[ra])) - int16_t(op));
            continue;
        }
        unsigned xo = (op >> 1) & 511;
        uint32_t a = expected.gpr[ra];
        if (xo == 8 || xo == 136 || xo == 232 || xo == 200)
            a = ~a;
        uint32_t b = xo == 10 || xo == 8 || xo == 138 || xo == 136 ? expected.gpr[(op >> 11) & 31]
                     : xo == 234 || xo == 232                      ? UINT32_MAX
                                                                   : 0;
        unsigned carry = xo == 10 ? 0 : xo == 8 ? 1 : ((expected.xer >> 29) & 1);
        uint64_t sum = uint64_t(a) + b + carry;
        int64_t signed_sum = int64_t(int32_t(a)) + int32_t(b) + carry;
        expected.gpr[rd] = uint32_t(sum);
        expected.xer = (expected.xer & ~0x20000000u) | (sum > UINT32_MAX ? 0x20000000u : 0);
        if (op & 0x400)
            expected.xer =
                (expected.xer & ~0x40000000u) | (signed_sum < INT32_MIN || signed_sum > INT32_MAX ? 0xc0000000u : 0);
        if (op & 1)
            condition(0, int32_t(sum));
    }
    // An interpreted MFXER must observe CA committed by the native block.
    expected.gpr[5] = expected.xer;
    assert(!memcmp(r->gpr, expected.gpr, sizeof r->gpr));
    assert(r->cr == expected.cr && r->xer == expected.xer);
    ++services;
    r->lr = r->pc + 4;
    expected.cr = r->cr = services * 0x13579bdu;
    expected.xer = r->xer = ((services & 7) << 29) | (services & 0x7f);
    for (unsigned i = 0; i < 32; ++i)
        expected.gpr[i] = r->gpr[i] = (r->gpr[i] << 1) | (r->gpr[i] >> 31);
    if (invalid_rewrite && services == 12) {
        // Replace the first word of a hot block with an invalid unary RB field.
        word(0x100, readword(0x104) | invalid_bits);
    }
    else if (services == 12 || services == 24)
        for (unsigned i = 1; i < 5; ++i)
            // Swap addition/subtraction and toggle OE/Rc in the warm native block.
            word(0x100 + i * 4, readword(0x100 + i * 4) ^ 0x405);
    return 1;
}

int main(void)
{
    const unsigned operations[] = {10, 522, 8, 520, 138, 650, 136, 648, 234, 746, 202, 714, 232, 744, 200, 712};
    const uint32_t values[] = {0,          1,          0xffffffff, 0x80000000, 0x7fffffff,
                               0xfffffffe, 0x80000001, 0x87654321, 0x7fff,     0x8000};
    unsigned cases = 0, invalid_cases = 0;
    for (unsigned xo : operations)
        for (uint32_t a : values)
            for (uint32_t b : values)
                for (unsigned alias = 0; alias < 5; ++alias)
                    for (unsigned flags = 0; flags < 8; ++flags)
                        for (unsigned rc = 0; rc < 2; ++rc) {
                            unsigned base = xo & 511;
                            bool binary = base == 10 || base == 8 || base == 138 || base == 136;
                            unsigned rd = alias == 0 ? 3 : alias == 2 ? 2 : alias == 3 ? 0 : 1;
                            unsigned ra = alias == 3 ? 0 : 1;
                            unsigned rb = binary ? (alias == 4 ? ra : 2) : 0;
                            uint32_t op = (31u << 26) | (rd << 21) | (ra << 16) | (rb << 11) | (xo << 1) | rc;
                            ROMlib_ppc_registers initial = {}, reference = {};
                            initial.pc = 0x100;
                            initial.ctr = 40;
                            initial.cr = 0x13579bdf;
                            initial.xer = (flags << 29) | 0x42;
                            for (unsigned i = 0; i < 32; ++i)
                                initial.gpr[i] = 0x9e3779b9u * i;
                            initial.gpr[ra] = a;
                            if (rb != ra && binary)
                                initial.gpr[rb] = b;
                            uint64_t count = 0;
                            // Reject invalid unary encodings after they replace hot code.
                            invalid_rewrite = !binary && b == 0;
                            invalid_bits = (((flags * 5 + alias) % 31 + 1) << 11) | (rc << 10);
                            for (unsigned mode = 0; mode < 2; ++mode) {
                                word(0x100, (11u << 26) | (7 << 23) | (ra << 16));
                                for (unsigned i = 1; i < 5; ++i)
                                    word(0x100 + i * 4, (op & ~1u) | (rc ^ (i & 1)));
                                word(0x114, (11u << 26) | (6 << 23) | (rd << 16) | 0x8000);
                                word(0x118, (31u << 26) | (5 << 21) | (1 << 16) | (339 << 1));
                                word(0x11c, 0x18000001);
                                word(0x120, 0x4200ffe0);
                                word(0x124, 0x18000000);
                                ROMlib_ppc_registers r = initial;
                                expected = initial;
                                services = 0;
                                ROMlib_ppc_environment env = {};
                                env.memory = map;
                                env.linear_memory = memory;
                                env.linear_size = sizeof memory;
                                env.service = service;
                                env.execution_flags = mode ? ROMLIB_PPC_EXEC_JIT : 0;
                                assert(!!ROMlib_ppc_execute(&r, &env) != invalid_rewrite);
                                assert(services == (invalid_rewrite ? 12u : 40u));
                                if (invalid_rewrite) {
                                    assert(r.pc == 0x100);
                                    assert(strstr(env.error, "illegal PowerPC instruction"));
                                    assert(!memcmp(r.gpr, expected.gpr, sizeof r.gpr));
                                    assert(r.cr == expected.cr && r.xer == expected.xer);
                                }
                                if (!mode) {
                                    reference = r;
                                    count = env.instructions;
                                }
                                else {
                                    assert(!memcmp(&r, &reference, sizeof r));
                                    assert(env.instructions == count);
                                    if (ROMlib_ppc_jit_available())
                                        assert(env.native_instructions >= (invalid_rewrite ? 24u : 150u));
                                }
                            }
                            ++cases;
                            invalid_cases += invalid_rewrite;
                        }
    printf("%u extended-carry/CR/alias/live-input/rewrite cases passed; "
           "%u invalid hot rewrites\n",
           cases, invalid_cases);
}
