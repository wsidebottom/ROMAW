/*+===================================================================
  File:      ppc_cr_jit_test.cpp

  Summary:   Implements ppc cr jit test support for synppc PowerPC execution and fragment loading.

  Classes:   Used: ROMlib_ppc_environment, ROMlib_ppc_registers, std::vector

  Functions: main

  Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Original CR truth-table, field, live-producer and native rewrite fixtures. */
#include "synppc/ppc.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>

static unsigned char memory[512];
static uint32_t base, invalid_word;
static unsigned services, polls, mode;
static bool invalid_run, warm;
static ROMlib_ppc_registers incoming;
static std::vector<ROMlib_ppc_registers> states;
static const unsigned operations[] = {257, 129, 289, 225, 33, 449, 417, 193};

static void* map(void*, uint32_t address, size_t size, int)
{
    static unsigned char service_code[] = {0x18, 0, 0, 1};
    static unsigned char exit_code[] = {0x18, 0, 0, 0};
    if (size <= 4) {
        if (address == base + 0x114 || address == base + 0x210)
            return service_code;
        if (address == base + 0x380)
            return exit_code;
    }
    uint32_t offset = address - base;
    return offset <= 0x114 && size <= 0x114 - offset ? memory + offset : nullptr;
}

static void word(unsigned offset, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        memory[offset + i] = value >> (24 - i * 8);
}

static uint32_t readword(unsigned offset)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value = value * 256 + memory[offset + i];
    return value;
}

static uint32_t logic(unsigned operation, unsigned dest, unsigned left, unsigned right)
{
    return (19u << 26) | (dest << 21) | (left << 16) | (right << 11) | (operation << 1);
}

static uint32_t field(unsigned dest, unsigned source)
{
    return (19u << 26) | (dest << 23) | (source << 18);
}

static uint32_t apply(uint32_t opcode, uint32_t cr)
{
    unsigned xo = (opcode >> 1) & 1023;
    if (!xo) {
        unsigned dest = 28 - ((opcode >> 23) & 7) * 4;
        unsigned source = 28 - ((opcode >> 18) & 7) * 4;
        return (cr & ~(15u << dest)) | ((cr >> source) & 15) << dest;
    }
    bool a = cr & (0x80000000u >> ((opcode >> 16) & 31));
    bool b = cr & (0x80000000u >> ((opcode >> 11) & 31));
    bool value;
    switch (xo) {
    case 257:
        value = a && b;
        break;
    case 129:
        value = a && !b;
        break;
    case 289:
        value = a == b;
        break;
    case 225:
        value = !(a && b);
        break;
    case 33:
        value = !(a || b);
        break;
    case 449:
        value = a || b;
        break;
    case 417:
        value = a || !b;
        break;
    case 193:
        value = a != b;
        break;
    default:
        assert(false);
        value = false;
    }
    uint32_t mask = 0x80000000u >> ((opcode >> 21) & 31);
    return (cr & ~mask) | (value ? mask : 0);
}

static int service(void*, uint32_t id, ROMlib_ppc_registers* r)
{
    assert(id == 1);
    uint32_t cr = incoming.cr;
    if (invalid_run) {
        cr = apply(readword(0x10c), cr);
        assert(r->pc == base + 0x110 && r->lr == incoming.lr);
    }
    else {
        uint32_t compare = readword(0x100);
        if ((compare >> 26) == 11) {
            int32_t value = incoming.gpr[3];
            unsigned nibble = (value == 0 ? 2 : value < 0 ? 8 : 4) | (incoming.xer >> 31);
            unsigned shift = 28 - ((compare >> 23) & 7) * 4;
            cr = (cr & ~(15u << shift)) | (nibble << shift);
        }
        for (unsigned offset = 0x104; offset <= 0x10c; offset += 4)
            cr = apply(readword(offset), cr);
        uint32_t branch = readword(0x110);
        unsigned bi = (branch >> 16) & 31, bo = (branch >> 21) & 31;
        bool taken = !!(cr & (0x80000000u >> bi)) == !!(bo & 8);
        assert(r->pc == base + (taken ? 0x210 : 0x114));
        assert(r->lr == ((branch & 1) ? base + 0x114 : incoming.lr));
    }
    assert(r->cr == cr && r->xer == incoming.xer && r->ctr == incoming.ctr);
    assert(!memcmp(r->gpr, incoming.gpr, sizeof r->gpr));
    if (!mode)
        states.push_back(*r);
    else
        assert(!memcmp(r, &states[services], sizeof *r));
    ++services;
    r->cr = services * 0x9e3779b9u;
    r->xer = ((services & 7) << 29) | (services & 0x7f);
    for (unsigned i = 0; i < 32; ++i)
        r->gpr[i] = (r->gpr[i] << 1) | (r->gpr[i] >> 31);
    r->gpr[3] = services % 3 - 1;
    if (invalid_run) {
        if (warm && services == 16)
            word(0x10c, invalid_word);
    }
    else if (services == 12 || services == 24) {
        word(0x104, services == 12 ? logic(193, 0, 7, 31) : field(7, 0));
        word(0x110, readword(0x110) ^ 0x01000001);
    }
    r->lr = services == 256 ? base + 0x380 : base + 0x100;
    incoming = *r;
    return 1;
}

static void poll(void*)
{
    ++polls;
    if (polls == 1) {
        assert(!invalid_run && services == 170);
        // The poll precedes MCRF, so its replacement executes in this iteration.
        word(0x10c, field(0, 7));
    }
}

static ROMlib_ppc_registers initial(unsigned seed)
{
    ROMlib_ppc_registers r = {};
    r.pc = base + 0x100;
    r.lr = 0x13579bdf;
    r.ctr = UINT32_MAX;
    r.cr = seed * 0x9e3779b9u;
    r.xer = ((seed & 7) << 29) | 0x42;
    for (unsigned i = 0; i < 32; ++i)
        r.gpr[i] = 0x87654321u * i;
    r.gpr[3] = seed % 3 - 1;
    return r;
}

static ROMlib_ppc_environment environment(void)
{
    ROMlib_ppc_environment env = {};
    env.memory = map;
    env.linear_memory = memory;
    env.linear_address = base;
    env.linear_size = 0x114;
    env.service = service;
    env.poll = poll;
    env.execution_flags = mode ? ROMLIB_PPC_EXEC_JIT : 0;
    return env;
}

static void valid(unsigned seed, uint32_t opcode)
{
    invalid_run = false;
    base = (seed & 4) ? 0xfffffee0 : 0;
    states.clear();
    ROMlib_ppc_registers reference = {};
    uint64_t count = 0;
    unsigned poll_count = 0;
    for (mode = 0; mode < 2; ++mode) {
        word(0x100, seed & 1 ? (11u << 26) | (((seed >> 3) & 7) << 23) | (3 << 16) : 0x60000000);
        word(0x104, opcode);
        word(0x108, logic(operations[seed & 7], (opcode >> 16) & 31, (opcode >> 21) & 31, (opcode >> 11) & 31));
        word(0x10c, field((seed >> 3) & 7, seed & 7));
        word(0x110, (16u << 26) | (12 << 21) | ((seed & 31) << 16) | 0x100 | (seed & 1));
        ROMlib_ppc_registers r = initial(seed);
        incoming = r;
        services = polls = 0;
        ROMlib_ppc_environment env = environment();
        bool limited = seed % 7 == 0;
        env.instruction_limit = limited ? 300 + seed % 6 : 2000;
        assert(!!ROMlib_ppc_execute(&r, &env) != limited);
        assert(services == (limited ? env.instruction_limit / 6 : 256));
        if (!mode) {
            reference = r;
            count = env.instructions;
            poll_count = polls;
        }
        else {
            assert(!memcmp(&r, &reference, sizeof r));
            assert(env.instructions == count && polls == poll_count);
            if (ROMlib_ppc_jit_available())
                assert(env.native_instructions >= (limited ? 100u : 600u));
        }
    }
}

static void reserved(uint32_t good, uint32_t bad)
{
    invalid_run = true;
    invalid_word = bad;
    base = 0;
    for (unsigned warmed = 0; warmed < 2; ++warmed) {
        warm = warmed;
        states.clear();
        for (mode = 0; mode < 2; ++mode) {
            for (unsigned offset = 0x100; offset < 0x10c; offset += 4)
                word(offset, 0x60000000);
            word(0x10c, warm ? good : bad);
            word(0x110, 0x18000001);
            ROMlib_ppc_registers r = initial(bad);
            incoming = r;
            services = polls = 0;
            ROMlib_ppc_environment env = environment();
            env.instruction_limit = 1000;
            assert(!ROMlib_ppc_execute(&r, &env));
            assert(strstr(env.error, "illegal PowerPC instruction"));
            incoming.pc = base + 0x10c;
            assert(!memcmp(&r, &incoming, sizeof r));
            assert(services == (warm ? 16u : 0u));
            assert(env.instructions == (warm ? 84u : 4u));
            if (mode && warm && ROMlib_ppc_jit_available())
                assert(env.native_instructions >= 16);
        }
    }
}

int main(void)
{
    unsigned cases = 0, invalid = 0;
    for (unsigned operation : operations)
        for (unsigned dest = 0; dest < 32; ++dest)
            for (unsigned left = 0; left < 32; ++left)
                for (unsigned alias = 0; alias < 4; ++alias) {
                    unsigned right = alias == 0 ? left : alias == 1 ? dest : alias == 2 ? 31 - left : (left + 17) & 31;
                    valid(cases++, logic(operation, dest, left, right));
                }
    for (unsigned dest = 0; dest < 8; ++dest)
        for (unsigned source = 0; source < 8; ++source)
            valid(cases++, field(dest, source));
    for (unsigned operation : operations)
        for (unsigned dest = 0; dest < 32; ++dest) {
            uint32_t good = logic(operation, dest, (dest + 7) & 31, 31 - dest);
            reserved(good, good | 1);
            ++invalid;
        }
    for (unsigned dest = 0; dest < 8; ++dest)
        for (unsigned source = 0; source < 8; ++source)
            for (unsigned bit = 0; bit < 26; ++bit)
                if ((1u << bit) & 0x0063f801) {
                    uint32_t good = field(dest, source);
                    reserved(good, good | (1u << bit));
                    ++invalid;
                }
    fflush(stdout);
    fprintf(stderr,
            "%u CR logic/field/alias/producer/branch/limit/rewrite cases; "
            "%u cold and hot reserved encodings passed\n",
            cases, invalid);
}
