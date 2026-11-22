/*+===================================================================
  File:      ppc_hardware_integer_test.cpp

  Summary:   Implements ppc hardware integer test support for synppc PowerPC execution and fragment loading.

  Classes:   Used: ROMlib_ppc_environment, ROMlib_ppc_registers

  Functions: main

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Original hardware/model comparison for integer arithmetic. */
#include "synppc/ppc.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static unsigned char memory[4096];
static uint32_t opcode, a, b, incoming_xer, result, xer, cr, before;
static unsigned services, cases, excluded;
static bool undefined_result;
static uint32_t cr_mask;

static void* map(void*, uint32_t address, size_t size, int)
{
    return address <= sizeof memory && size <= sizeof memory - address ? memory + address : nullptr;
}

static void word(unsigned address, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        memory[address + i] = value >> (24 - i * 8);
}

static void initialize(ROMlib_ppc_registers& r)
{
    r.gpr[3] = a;
    r.gpr[4] = b;
    r.gpr[5] = 0x1234;
    r.xer = incoming_xer;
    r.cr = before;
}

static int service(void*, uint32_t id, ROMlib_ppc_registers* r)
{
    assert(id == 1);
    unsigned rd = (opcode >> 21) & 31;
    if ((!undefined_result && r->gpr[rd] != result) || ((r->xer ^ xer) & 0xe000007f) || ((r->cr ^ cr) & cr_mask)) {
        fprintf(stderr, "case %u op=%08x a=%08x b=%08x: result %08x/%08x XER %08x/%08x CR %08x/%08x\n", cases, opcode,
                a, b, r->gpr[rd], result, r->xer, xer, r->cr, cr);
        assert(false);
    }
    ++services;
    initialize(*r);
    r->lr = services == 32 ? 0x120 : 0x100;
    return 1;
}

static void model(void)
{
    unsigned xo = (opcode >> 1) & 511;
    bool division = xo == 459 || xo == 491;
    bool sign = xo == 491;
    undefined_result = division && (!b || (sign && a == 0x80000000 && b == UINT32_MAX));
    cr_mask = undefined_result && (opcode & 1) ? 0x1fffffff : UINT32_MAX;
    int64_t product = int64_t(int32_t(a)) * int32_t(b);
    if (xo == 266)
        product = int64_t(int32_t(a)) + int32_t(b);
    else if (xo == 40)
        product = int64_t(int32_t(b)) - int32_t(a);
    else if (xo == 104)
        product = -int64_t(int32_t(a));
    bool carry_op = xo == 10 || xo == 8 || xo == 138 || xo == 136 || xo == 234 || xo == 202 || xo == 232 || xo == 200;
    uint64_t carry_sum = 0;
    if (carry_op) {
        unsigned ca = (incoming_xer >> 29) & 1;
        bool subtract = xo == 8 || xo == 136 || xo == 232 || xo == 200;
        uint32_t right = xo == 234 || xo == 232 ? UINT32_MAX : xo == 202 || xo == 200 ? 0 : b;
        unsigned cin = xo == 10 ? 0 : xo == 8 ? 1 : ca;
        carry_sum = uint64_t(subtract ? ~a : a) + right + cin;
        product = (subtract ? -int64_t(int32_t(a)) - 1 : int64_t(int32_t(a))) + int32_t(right) + cin;
    }
    uint32_t value = 0;
    if (division && !undefined_result)
        value = sign ? uint32_t(int64_t(int32_t(a)) / int32_t(b)) : a / b;
    else if (xo == 235 || xo == 266 || xo == 40 || xo == 104 || carry_op)
        value = uint32_t(product);
    else if (xo == 75)
        value = uint32_t(uint64_t(product) >> 32);
    else if (xo == 11)
        value = uint32_t((uint64_t(a) * b) >> 32);
    bool overflow = division ? undefined_result : product < INT32_MIN || product > INT32_MAX;
    uint32_t expected_xer = incoming_xer;
    if (carry_op)
        expected_xer = (expected_xer & ~0x20000000u) | (carry_sum > UINT32_MAX ? 0x20000000u : 0);
    if (opcode & 0x400)
        expected_xer = (expected_xer & ~0x40000000u) | (overflow ? 0xc0000000u : 0);
    uint32_t expected_cr = before;
    if (opcode & 1)
        expected_cr = (before & 0x0fffffff) | ((value == 0           ? 2u
                                                : int32_t(value) < 0 ? 8u
                                                                     : 4u) |
                                               (expected_xer >> 31))
                                                  << 28;
    assert(undefined_result || result == value);
    assert(!((expected_xer ^ xer) & 0xe000007f));
    assert(!((expected_cr ^ cr) & cr_mask));
    excluded += undefined_result;
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    FILE* input = fopen(argv[1], "r");
    assert(input);
    int fields;
    while ((fields = fscanf(input, "%x%x%x%x%x%x%x%x", &opcode, &a, &b, &incoming_xer, &result, &xer, &cr, &before)) ==
           8) {
        model();
        word(0x100, opcode);
        for (unsigned i = 1; i < 4; ++i)
            word(0x100 + i * 4, 0x60000000);
        word(0x110, 0x18000001);
        word(0x120, 0x18000000);
        for (unsigned mode = 0; mode < 2; ++mode) {
            ROMlib_ppc_registers r = {};
            ROMlib_ppc_environment env = {};
            r.pc = 0x100;
            initialize(r);
            services = 0;
            env.memory = map;
            env.service = service;
            env.linear_memory = memory;
            env.linear_size = sizeof memory;
            env.execution_flags = mode ? ROMLIB_PPC_EXEC_JIT : 0;
            env.instruction_limit = 1000;
            assert(ROMlib_ppc_execute(&r, &env) && services == 32);
            if (mode && ROMlib_ppc_jit_available())
                assert(env.native_instructions >= 96);
        }
        ++cases;
    }
    assert(fields == EOF && cases);
    assert(!fclose(input));
    printf("%u hardware integer/model/JIT cases matched; %u undefined result/CR0 values excluded\n", cases, excluded);
}
